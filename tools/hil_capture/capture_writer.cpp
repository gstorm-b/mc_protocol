#include "hil_capture/capture_writer.h"

#include "hil_capture/resolve.h"
#include "mc/version.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <cmath>

namespace mc::hil {

namespace {

QString hexLine(const QByteArray& bytes) {
    QString text;
    text.reserve(bytes.size() * 3);
    for (qsizetype i = 0; i < bytes.size(); ++i) {
        if (i != 0) {
            text += QLatin1Char(' ');
        }
        text += QStringLiteral("%1")
                    .arg(static_cast<uint>(static_cast<uint8_t>(bytes[i])), 2, 16, QLatin1Char('0'))
                    .toUpper();
    }
    return text;
}

// A metadata value is one line of tokens: no line break, no empty value.
QString clean(const QString& value) { return value.simplified(); }

QString ms3(double v) { return QString::number(v, 'f', 3); }

void putU16(QByteArray& b, uint16_t v) {
    b.append(static_cast<char>(v & 0xFF));
    b.append(static_cast<char>((v >> 8) & 0xFF));
}

void putU32(QByteArray& b, uint32_t v) {
    putU16(b, static_cast<uint16_t>(v & 0xFFFF));
    putU16(b, static_cast<uint16_t>(v >> 16));
}

void putU64(QByteArray& b, uint64_t v) {
    putU32(b, static_cast<uint32_t>(v & 0xFFFFFFFFu));
    putU32(b, static_cast<uint32_t>(v >> 32));
}

SessionEvent makeEvent(const QString& name, uint8_t tag, qint64 tNs) {
    SessionEvent e;
    e.name = name;
    e.tNs = tNs;
    e.payload.append(static_cast<char>(tag));
    return e;
}

QString jsonText(const QJsonValue& v) {
    if (v.isBool()) {
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (v.isDouble()) {
        const double d = v.toDouble();
        if (d == std::floor(d) && std::fabs(d) < 9e15) {
            return QString::number(static_cast<qint64>(d));
        }
        return QString::number(d, 'g', 17);
    }
    return v.toString();
}

QString kv(const QString& key, const QString& value) {
    return key + QStringLiteral(": ") + clean(value) + QLatin1Char('\n');
}

const char* chunkStateName(ChunkState s) {
    switch (s) {
    case ChunkState::NotRead:
        return "notRead";
    case ChunkState::Ok:
        return "ok";
    case ChunkState::Failed:
        return "failed";
    }
    return "?";
}

QString deviceTypeName(DeviceType t) { return deviceSymbol(t); }

} // namespace

QString errorCodeName(ErrorCode code) {
    switch (code) {
    case ErrorCode::Ok:
        return QStringLiteral("Ok");
    case ErrorCode::InvalidConfig:
        return QStringLiteral("InvalidConfig");
    case ErrorCode::NotSubscribed:
        return QStringLiteral("NotSubscribed");
    case ErrorCode::InvalidDevice:
        return QStringLiteral("InvalidDevice");
    case ErrorCode::PointCount:
        return QStringLiteral("PointCount");
    case ErrorCode::UnsupportedCommand:
        return QStringLiteral("UnsupportedCommand");
    case ErrorCode::DataSizeMismatch:
        return QStringLiteral("DataSizeMismatch");
    case ErrorCode::BufferTooSmall:
        return QStringLiteral("BufferTooSmall");
    case ErrorCode::Timeout:
        return QStringLiteral("Timeout");
    case ErrorCode::LinkDown:
        return QStringLiteral("LinkDown");
    case ErrorCode::QueueFull:
        return QStringLiteral("QueueFull");
    case ErrorCode::FrameMismatch:
        return QStringLiteral("FrameMismatch");
    case ErrorCode::LengthMismatch:
        return QStringLiteral("LengthMismatch");
    case ErrorCode::SumCheck:
        return QStringLiteral("SumCheck");
    case ErrorCode::InvalidCharacter:
        return QStringLiteral("InvalidCharacter");
    case ErrorCode::PlcError:
        return QStringLiteral("PlcError");
    }
    return QStringLiteral("Unknown");
}

QString outcomeText(const Error& e) {
    const auto hex = [](uint v, int width) {
        return QStringLiteral("%1").arg(v, width, 16, QLatin1Char('0')).toUpper();
    };
    if (e.ok()) {
        return QStringLiteral("ok");
    }
    if (e.code == ErrorCode::PlcError) {
        QString text = QStringLiteral("plcError ") + hex(e.plcCode, 4);
        if (e.abnormalCode != 0) {
            text += QStringLiteral(" abnormal ") + hex(e.abnormalCode, 2);
        }
        if (e.info.command != 0) {
            text += QStringLiteral(" info ") + hex(e.info.network, 2) + QLatin1Char('/') +
                    hex(e.info.pc, 2) + QLatin1Char('/') + hex(e.info.io, 4) + QLatin1Char('/') +
                    hex(e.info.station, 2) + QLatin1Char('/') + hex(e.info.command, 4) +
                    QLatin1Char('/') + hex(e.info.subcommand, 4);
        }
        return text;
    }
    if (e.code == ErrorCode::Timeout) {
        return QStringLiteral("timeout");
    }
    switch (e.category) {
    case ErrorCategory::Protocol:
        return QStringLiteral("protocolError ") + errorCodeName(e.code);
    case ErrorCategory::Transport:
        return QStringLiteral("transportError ") + errorCodeName(e.code);
    default:
        return QStringLiteral("notSent ") + errorCodeName(e.code);
    }
}

QString expectText(const Expect& e, bool bitUnit) {
    QString text;
    switch (e.kind) {
    case ExpectKind::Ok:
        text = QStringLiteral("ok");
        break;
    case ExpectKind::PlcError:
        text = QStringLiteral("plcError");
        break;
    case ExpectKind::Timeout:
        text = QStringLiteral("timeout");
        break;
    case ExpectKind::NoResponse:
        text = QStringLiteral("noResponse");
        break;
    case ExpectKind::NotSent:
        text = QStringLiteral("notSent");
        break;
    case ExpectKind::Record:
        text = QStringLiteral("record");
        break;
    }
    if (e.kind == ExpectKind::Ok && e.hasValues) {
        text = bitUnit ? QStringLiteral("bits") : QStringLiteral("words");
        for (const uint16_t v : e.values) {
            text += QLatin1Char(' ') +
                    (bitUnit ? QString::number(v) : QStringLiteral("%1").arg(v, 0, 16).toUpper());
        }
    } else if (e.kind == ExpectKind::Ok && e.hasBitsOn) {
        text = QStringLiteral("bitsOn");
        for (const int i : e.bitsOn) {
            text += QLatin1Char(' ') + QString::number(i);
        }
    }
    if (!e.valuesFrom.isEmpty()) {
        text += QStringLiteral(" valuesFrom ") + e.valuesFrom;
    }
    if (e.frames >= 0) {
        text += QStringLiteral(" frames ") + QString::number(e.frames);
    }
    return text;
}

SessionEvent snapshotEvent(const DeviceSnapshot& s, qint64 tNs, XyNumbering xy) {
    SessionEvent e = makeEvent(QStringLiteral("snapshot"), 0x01, tNs);
    QStringList segments;
    for (const SnapshotSegment& seg : s.segments) {
        putU32(e.payload, seg.head.number);
        putU32(e.payload, seg.count);
        e.payload.append(seg.values);
        e.payload.append(seg.states);
        segments << QStringLiteral("%1x%2").arg(deviceText(seg.head, xy)).arg(seg.count);
    }
    QStringList chunks;
    for (const ChunkStatus& c : s.chunks) {
        chunks << QLatin1String(chunkStateName(c.state));
    }
    e.keys.push_back({QStringLiteral("type"), deviceTypeName(s.type)});
    e.keys.push_back({QStringLiteral("round"), QString::number(s.round)});
    e.keys.push_back({QStringLiteral("segments"), segments.isEmpty()
                                                      ? QStringLiteral("none")
                                                      : segments.join(QLatin1Char(' '))});
    e.keys.push_back({QStringLiteral("chunks"),
                      chunks.isEmpty() ? QStringLiteral("none") : chunks.join(QLatin1Char(' '))});
    return e;
}

SessionEvent changesEvent(DeviceType type, quint32 round, const QVector<Change>& changes,
                          qint64 tNs) {
    SessionEvent e = makeEvent(QStringLiteral("valuesChanged"), 0x02, tNs);
    for (const Change& c : changes) {
        putU32(e.payload, c.device.number);
        putU16(e.payload, c.oldValue);
        putU16(e.payload, c.newValue);
    }
    e.keys.push_back({QStringLiteral("type"), deviceTypeName(type)});
    e.keys.push_back({QStringLiteral("round"), QString::number(round)});
    e.keys.push_back({QStringLiteral("changes"), QString::number(changes.size())});
    return e;
}

SessionEvent cycleEvent(const CycleInfo& c, qint64 tNs) {
    SessionEvent e = makeEvent(QStringLiteral("cycleDone"), 0x03, tNs);
    putU32(e.payload, c.round);
    putU64(e.payload, c.startedAt);
    putU32(e.payload, c.durationMs);
    putU16(e.payload, c.requests);
    putU16(e.payload, c.failedChunks);
    e.payload.append(static_cast<char>(c.heartbeatOk ? 1 : 0));
    e.keys.push_back({QStringLiteral("round"), QString::number(c.round)});
    e.keys.push_back({QStringLiteral("duration_ms"), QString::number(c.durationMs)});
    e.keys.push_back({QStringLiteral("requests"), QString::number(c.requests)});
    e.keys.push_back({QStringLiteral("failed_chunks"), QString::number(c.failedChunks)});
    e.keys.push_back({QStringLiteral("heartbeat_ok"),
                      c.heartbeatOk ? QStringLiteral("true") : QStringLiteral("false")});
    return e;
}

SessionEvent requestFinishedEvent(RequestId id, const Error& error, const QByteArray& payload,
                                  qint64 tNs) {
    SessionEvent e = makeEvent(QStringLiteral("requestFinished"), 0x04, tNs);
    e.payload.append(payload);
    e.keys.push_back({QStringLiteral("request_id"), QString::number(id)});
    e.keys.push_back({QStringLiteral("outcome"), outcomeText(error)});
    e.keys.push_back({QStringLiteral("bytes"), QString::number(payload.size())});
    return e;
}

const char* linkStateName(LinkState state) {
    switch (state) {
    case LinkState::Disconnected:
        return "Disconnected";
    case LinkState::Connecting:
        return "Connecting";
    case LinkState::Connected:
        return "Connected";
    case LinkState::Faulted:
        return "Faulted";
    }
    return "?";
}

const char* linkReasonName(LinkReason reason) {
    switch (reason) {
    case LinkReason::Requested:
        return "Requested";
    case LinkReason::OpenFailed:
        return "OpenFailed";
    case LinkReason::PeerClosed:
        return "PeerClosed";
    case LinkReason::TransportError:
        return "TransportError";
    case LinkReason::Fault:
        return "Fault";
    }
    return "?";
}

SessionEvent linkStateEvent(LinkState state, LinkReason reason, qint64 tNs) {
    SessionEvent e = makeEvent(QStringLiteral("linkState"), 0x05, tNs);
    e.payload.append(static_cast<char>(state));
    e.payload.append(static_cast<char>(reason));
    e.keys.push_back({QStringLiteral("state"), QLatin1String(linkStateName(state))});
    e.keys.push_back({QStringLiteral("reason"), QLatin1String(linkReasonName(reason))});
    return e;
}

SessionEvent linkFaultEvent(const LinkFaultInfo& f, qint64 tNs) {
    SessionEvent e = makeEvent(QStringLiteral("linkFault"), 0x06, tNs);
    e.payload.append(static_cast<char>(f.kind));
    putU16(e.payload, static_cast<uint16_t>(f.error.code));
    putU16(e.payload, f.error.plcCode);
    e.payload.append(static_cast<char>(f.reopenTransport ? 1 : 0));
    e.keys.push_back({QStringLiteral("fault"), f.kind == LinkFaultKind::Timeout
                                                   ? QStringLiteral("Timeout")
                                                   : QStringLiteral("ProtocolError")});
    e.keys.push_back({QStringLiteral("error"), errorCodeName(f.error.code)});
    e.keys.push_back({QStringLiteral("reopen"),
                      f.reopenTransport ? QStringLiteral("true") : QStringLiteral("false")});
    return e;
}

SessionEvent heartbeatInput(bool enabled, const Device& device, qint64 tNs, XyNumbering xy) {
    SessionEvent e = makeEvent(QStringLiteral("heartbeat"), 0x07, tNs);
    e.input = true;
    e.payload.append(static_cast<char>(enabled ? 1 : 0));
    e.payload.append(static_cast<char>(device.type));
    putU32(e.payload, device.number);
    e.keys.push_back(
        {QStringLiteral("enabled"), enabled ? QStringLiteral("true") : QStringLiteral("false")});
    e.keys.push_back({QStringLiteral("device"), deviceText(device, xy)});
    return e;
}

SessionEvent subscribeInput(const QString& name, const Device& head, quint32 count, qint64 tNs,
                            XyNumbering xy) {
    SessionEvent e = makeEvent(QStringLiteral("subscribe"), 0x08, tNs);
    e.input = true;
    e.payload.append(static_cast<char>(head.type));
    putU32(e.payload, head.number);
    putU32(e.payload, count);
    e.keys.push_back({QStringLiteral("name"), name});
    e.keys.push_back({QStringLiteral("device"), deviceText(head, xy)});
    e.keys.push_back({QStringLiteral("count"), QString::number(count)});
    return e;
}

SessionEvent unsubscribeInput(const QString& name, qint64 tNs) {
    SessionEvent e = makeEvent(QStringLiteral("unsubscribe"), 0x09, tNs);
    e.input = true;
    e.payload.append(name.toUtf8());
    e.keys.push_back({QStringLiteral("name"), name});
    return e;
}

SessionEvent writeInput(Op op, const Device& head, quint16 count, const ByteBuf& data, qint64 tNs,
                        XyNumbering xy) {
    SessionEvent e = makeEvent(QStringLiteral("write"), 0x0A, tNs);
    e.input = true;
    e.payload.append(static_cast<char>(op));
    e.payload.append(static_cast<char>(head.type));
    putU32(e.payload, head.number);
    putU16(e.payload, count);
    e.payload.append(reinterpret_cast<const char*>(data.data()),
                     static_cast<qsizetype>(data.size()));
    static const char* const ops[] = {"ReadBits", "ReadWords", "WriteBits", "WriteWords"};
    e.keys.push_back({QStringLiteral("op"), QLatin1String(ops[static_cast<int>(op)])});
    e.keys.push_back({QStringLiteral("device"), deviceText(head, xy)});
    e.keys.push_back({QStringLiteral("count"), QString::number(count)});
    return e;
}

QString stepsText(const QString& profileId, const QVector<StepRecord>& records) {
    QString out;
    for (const StepRecord& r : records) {
        if (r.request.isEmpty()) {
            continue; // nothing was sent: run.meta lists it as notsent.<id>
        }
        const QString id = QStringLiteral("CAP-%1-%2").arg(profileId, r.recordId);
        out += QStringLiteral("# id: %1\n").arg(id);
        out += QStringLiteral("# source: %1  profile: %2  step: %3")
                   .arg(clean(r.source).isEmpty() ? QStringLiteral("plc") : clean(r.source),
                        profileId, r.recordId);
        if (!clean(r.mirrors).isEmpty()) {
            out += QStringLiteral("  mirrors: %1").arg(clean(r.mirrors));
        }
        if (!clean(r.overrideText).isEmpty()) {
            out += QStringLiteral("  override: %1").arg(clean(r.overrideText));
        }
        out += QLatin1Char('\n');
        out += QStringLiteral("# frame: %1  code: %2").arg(r.frame, r.code);
        if (r.format != 0) {
            out += QStringLiteral("  format: %1").arg(r.format);
        }
        out += QStringLiteral("  op: %1").arg(r.op);
        if (!r.device.isEmpty()) {
            out += QStringLiteral("  device: %1").arg(r.device);
        }
        if (r.count != 0) {
            out += QStringLiteral("  count: %1").arg(r.count);
        }
        out += QStringLiteral("  via: %1\n").arg(r.via);
        out += QStringLiteral("# kind: request\n");
        if (r.response.isEmpty()) {
            // No response record can exist (a record needs a byte): the outcome rides here.
            QString line = QStringLiteral("# outcome: %1").arg(clean(r.outcome));
            if (!clean(r.expect).isEmpty()) {
                line += QStringLiteral("  expect: %1").arg(clean(r.expect));
            }
            if (!std::isnan(r.waitedMs)) {
                line += QStringLiteral("  waited_ms: %1").arg(ms3(r.waitedMs));
            }
            if (!r.verdict.isEmpty()) {
                line += QStringLiteral("  verdict: %1").arg(r.verdict);
            }
            out += line + QLatin1Char('\n');
        }
        out += hexLine(r.request) + QLatin1Char('\n');
        if (!r.response.isEmpty()) {
            out += QLatin1Char('\n');
            out += QStringLiteral("# id: %1-R\n").arg(id);
            out += QStringLiteral("# kind: %1  of: %2")
                       .arg(r.partial ? QStringLiteral("response-partial")
                                      : QStringLiteral("response"),
                            id);
            if (!clean(r.overrideText).isEmpty()) {
                out += QStringLiteral("  override: %1").arg(clean(r.overrideText));
            }
            out += QLatin1Char('\n');
            QString line = QStringLiteral("# outcome: %1").arg(clean(r.outcome));
            if (!clean(r.expect).isEmpty()) {
                line += QStringLiteral("  expect: %1").arg(clean(r.expect));
            }
            if (!r.verdict.isEmpty()) {
                line += QStringLiteral("  verdict: %1").arg(r.verdict);
            }
            out += line + QLatin1Char('\n');
            QStringList times;
            if (!std::isnan(r.ttfbMs)) {
                times << QStringLiteral("ttfb_ms: %1").arg(ms3(r.ttfbMs));
            }
            if (!std::isnan(r.rxMs)) {
                times << QStringLiteral("rx_ms: %1").arg(ms3(r.rxMs));
            }
            if (!std::isnan(r.rttMs)) {
                times << QStringLiteral("rtt_ms: %1").arg(ms3(r.rttMs));
            }
            if (!times.isEmpty()) {
                out += QStringLiteral("# ") + times.join(QStringLiteral("  ")) + QLatin1Char('\n');
            }
            out += hexLine(r.response) + QLatin1Char('\n');
        }
        out += QLatin1Char('\n');
    }
    return out;
}

QString sessionText(const QString& profileId, const QVector<SessionCapture>& captures) {
    QString out;
    for (const SessionCapture& cap : captures) {
        const QString base = QStringLiteral("CAP-%1-%2").arg(profileId, cap.stepId);
        const QString source =
            QStringLiteral("# source: plc  profile: %1  step: %2%3\n")
                .arg(profileId, cap.stepId,
                     clean(cap.overrideText).isEmpty()
                         ? QString()
                         : QStringLiteral("  override: ") + clean(cap.overrideText));
        int n = 0;
        for (const SessionChunk& c : cap.chunks) {
            if (c.bytes.isEmpty()) {
                continue;
            }
            out += QStringLiteral("# id: %1-T%2\n").arg(base).arg(++n, 4, 10, QLatin1Char('0'));
            out += source;
            out += QStringLiteral("# kind: %1  t_ns: %2  seq: %3\n")
                       .arg(c.tx ? QStringLiteral("tx") : QStringLiteral("rx"))
                       .arg(c.tNs)
                       .arg(c.seq);
            out += hexLine(c.bytes) + QStringLiteral("\n\n");
        }
        n = 0;
        int inputs = 0;
        for (const SessionEvent& e : cap.events) {
            if (e.input) {
                out += QStringLiteral("# id: %1-I%2\n")
                           .arg(base)
                           .arg(++inputs, 4, 10, QLatin1Char('0'));
            } else {
                out += QStringLiteral("# id: %1-E%2\n").arg(base).arg(++n, 4, 10, QLatin1Char('0'));
            }
            out += source;
            out += QStringLiteral("# kind: %1  %2: %3  t_ns: %4  seq: %5\n")
                       .arg(e.input ? QStringLiteral("input") : QStringLiteral("event"),
                            e.input ? QStringLiteral("input") : QStringLiteral("event"), e.name)
                       .arg(e.tNs)
                       .arg(e.seq);
            if (!e.keys.isEmpty()) {
                QStringList parts;
                for (const auto& k : e.keys) {
                    parts << QStringLiteral("%1: %2").arg(k.first, clean(k.second));
                }
                out += QStringLiteral("# ") + parts.join(QStringLiteral("  ")) + QLatin1Char('\n');
            }
            out += hexLine(e.payload) + QStringLiteral("\n\n");
        }
    }
    return out;
}

QString runMetaText(const RunMeta& meta) {
    const Profile& p = meta.profile;
    QString out;
    out += kv(QStringLiteral("format"), QStringLiteral("hil-capture-1"));
    out += kv(QStringLiteral("tool"), QStringLiteral("hil_capture"));
    out += kv(QStringLiteral("tool_version"), QStringLiteral(MC_VERSION_STRING));
    out += kv(QStringLiteral("library_version"), QStringLiteral(MC_VERSION_STRING));
    out += kv(QStringLiteral("git_commit"), meta.gitCommit);
    out += kv(QStringLiteral("date"), meta.date);
    out += kv(QStringLiteral("operator_note"), meta.operatorNote);
    out += kv(QStringLiteral("plc_state"), meta.plcState);
    out += kv(QStringLiteral("profile"), p.id);
    out += kv(QStringLiteral("plc"), p.plc);
    out += kv(QStringLiteral("module"), p.module);
    out += kv(QStringLiteral("firmware"), p.firmware);
    out += kv(QStringLiteral("adapter"), p.adapter);
    out += kv(QStringLiteral("plc_state_note"), p.plcState);
    const QJsonObject json = p.device.toJson();
    out += kv(QStringLiteral("transport"), p.device.transport == TransportKind::Serial
                                               ? QStringLiteral("serial")
                                               : QStringLiteral("tcp"));
    if (p.device.transport == TransportKind::Serial) {
        // The line settings only; the port name is a machine's business, not the capture's.
        const QJsonObject serial = json.value(QStringLiteral("transport"))
                                       .toObject()
                                       .value(QStringLiteral("serial"))
                                       .toObject();
        for (const char* key : {"baudRate", "dataBits", "parity", "stopBits", "flowControl"}) {
            out += kv(QStringLiteral("serial.") + QLatin1String(key),
                      jsonText(serial.value(QLatin1String(key))));
        }
    }
    QStringList scratch;
    for (const ScratchRange& r : p.scratch) {
        scratch << QStringLiteral("%1%2-%1%3")
                       .arg(deviceSymbol(r.type),
                            formatDeviceNumber(r.type, r.first, p.device.frame.xyNotation),
                            formatDeviceNumber(r.type, r.last, p.device.frame.xyNotation));
    }
    out += kv(QStringLiteral("scratch"), scratch.join(QLatin1Char(' ')));
    QStringList ends;
    for (size_t i = 0; i < static_cast<size_t>(DeviceType::Count); ++i) {
        const auto t = static_cast<DeviceType>(i);
        if (const std::optional<uint32_t> end = p.end(t)) {
            ends << QStringLiteral("%1=%2").arg(
                deviceSymbol(t), formatDeviceNumber(t, *end, p.device.frame.xyNotation));
        }
    }
    out += kv(QStringLiteral("device_end"), ends.join(QLatin1Char(' ')));
    QStringList supports;
    for (const DeviceType t : p.supports) {
        supports << deviceSymbol(t);
    }
    out += kv(QStringLiteral("supports"), supports.join(QLatin1Char(' ')));
    out += kv(QStringLiteral("special_bit"), deviceText(p.specialBit, p.device.frame.xyNotation));
    out += kv(QStringLiteral("special_word"), deviceText(p.specialWord, p.device.frame.xyNotation));

    // Every FrameConfig and SessionConfig field, through the JSON mapping of the device layer.
    const QJsonObject frame = json.value(QStringLiteral("frame")).toObject();
    for (auto it = frame.begin(); it != frame.end(); ++it) {
        out += kv(QStringLiteral("frame.") + it.key(), jsonText(it.value()));
    }
    const QJsonObject session = json.value(QStringLiteral("session")).toObject();
    for (auto it = session.begin(); it != session.end(); ++it) {
        if (it.value().isObject()) {
            const QJsonObject inner = it.value().toObject();
            for (auto in = inner.begin(); in != inner.end(); ++in) {
                out += kv(QStringLiteral("session.%1.%2").arg(it.key(), in.key()),
                          jsonText(in.value()));
            }
        } else {
            out += kv(QStringLiteral("session.") + it.key(), jsonText(it.value()));
        }
    }
    for (const auto& s : meta.skipped) {
        out += kv(QStringLiteral("skipped.") + s.first, s.second);
    }
    for (const auto& s : meta.notSent) {
        out += kv(QStringLiteral("notsent.") + s.first, s.second);
    }
    for (const auto& s : meta.extra) {
        out += kv(s.first, s.second);
    }
    return scrubAddresses(out, p);
}

QString scrubAddresses(const QString& text, const Profile& profile) {
    const QString redacted = QStringLiteral("<redacted>");
    const TcpSettings& tcp = profile.device.tcp;
    const SerialSettings& serial = profile.device.serial;
    QString out = text;
    // "host:port" as a whole first, then the host alone, then ":port" alone, then the COM name.
    if (!tcp.host.isEmpty()) {
        out.replace(QStringLiteral("%1:%2").arg(tcp.host).arg(tcp.port), redacted,
                    Qt::CaseInsensitive);
        out.replace(tcp.host, redacted, Qt::CaseInsensitive);
    }
    out.replace(QRegularExpression(QStringLiteral(":%1(?![0-9])").arg(tcp.port)), redacted);
    if (!serial.portName.isEmpty()) {
        out.replace(serial.portName, redacted, Qt::CaseInsensitive);
    }
    return out;
}

QString benchText(const QString& profileId, const QVector<BenchRow>& rows) {
    QString out = benchHeader();
    const auto ms = [](double v) { return std::isnan(v) ? QString() : QString::number(v, 'f', 3); };
    for (const BenchRow& r : rows) {
        out += QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8,%9,%10,%11,%12,%13\n")
                   .arg(profileId, r.plcState, r.step, r.op, r.device)
                   .arg(r.count)
                   .arg(r.reqBytes)
                   .arg(r.respBytes)
                   .arg(r.rep)
                   .arg(ms(r.ttfbMs), ms(r.rxMs), ms(r.rttMs), r.scanMs);
    }
    return out;
}

QString benchHeader() {
    return QStringLiteral("profile,plc_state,step,op,device,count,req_bytes,resp_bytes,rep,ttfb_ms,"
                          "rx_ms,rtt_ms,scan_ms\n");
}

CaptureWriter::CaptureWriter(QString outputRoot, QString profileId)
    : m_root(std::move(outputRoot)), m_profileId(std::move(profileId)) {}

QString CaptureWriter::folder() const { return QDir(m_root).absoluteFilePath(m_profileId); }

bool CaptureWriter::prepare(QString* error, bool keepExisting) {
    const auto fail = [&](const QString& why) {
        if (error != nullptr) {
            *error = why;
        }
        return false;
    };
    if (!folderSafe(m_profileId)) {
        return fail(QStringLiteral("profile id '%1' is not a folder name").arg(m_profileId));
    }
    if (!QDir().mkpath(QDir(m_root).absolutePath())) {
        return fail(QStringLiteral("cannot create %1").arg(m_root));
    }
    const QString path = folder();
    QDir dir(path);
    if (dir.exists() && !keepExisting) {
        // Replace what the tool wrote; the owner's divergences.txt stays.
        const QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot |
                                                        QDir::Hidden | QDir::System);
        for (const QFileInfo& info : entries) {
            if (info.fileName() == QLatin1String("divergences.txt")) {
                continue;
            }
            const bool removed = info.isDir() ? QDir(info.absoluteFilePath()).removeRecursively()
                                              : QFile::remove(info.absoluteFilePath());
            if (!removed) {
                return fail(QStringLiteral("cannot remove %1").arg(info.absoluteFilePath()));
            }
        }
    }
    if (!QDir().mkpath(path)) {
        return fail(QStringLiteral("cannot create %1").arg(path));
    }
    return true;
}

bool CaptureWriter::writeFile(const QString& name, const QString& text, QString* error) const {
    QFile file(QDir(folder()).filePath(name));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot write %1: %2").arg(file.fileName(), file.errorString());
        }
        return false;
    }
    const QByteArray bytes = text.toUtf8();
    if (file.write(bytes) != bytes.size()) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot write %1: %2").arg(file.fileName(), file.errorString());
        }
        return false;
    }
    return true;
}

bool CaptureWriter::writeSteps(const QVector<StepRecord>& records, QString* error) const {
    return writeFile(QStringLiteral("steps.vec"), stepsText(m_profileId, records), error);
}

bool CaptureWriter::writeSession(const QVector<SessionCapture>& captures, QString* error) const {
    return writeFile(QStringLiteral("session.vec"), sessionText(m_profileId, captures), error);
}

bool CaptureWriter::has(const QString& name) const {
    return QFileInfo::exists(QDir(folder()).filePath(name));
}

bool CaptureWriter::writeRunMeta(const RunMeta& meta, QString* error, bool append) const {
    if (!append || !has(QStringLiteral("run.meta"))) {
        return writeFile(QStringLiteral("run.meta"), runMetaText(meta), error);
    }
    QFile existing(QDir(folder()).filePath(QStringLiteral("run.meta")));
    QString text;
    if (existing.open(QIODevice::ReadOnly)) {
        text = QString::fromUtf8(existing.readAll());
        existing.close();
    }
    QStringList kept;
    for (const QString& line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        if (!line.startsWith(QStringLiteral("stop_pass."))) {
            kept << line;
        }
    }
    text = kept.join(QLatin1Char('\n')) + QLatin1Char('\n');
    text += kv(QStringLiteral("stop_pass.plc_state"), meta.plcState);
    text += kv(QStringLiteral("stop_pass.date"), meta.date);
    text += kv(QStringLiteral("stop_pass.git_commit"), meta.gitCommit);
    text += kv(QStringLiteral("stop_pass.operator_note"), meta.operatorNote);
    return writeFile(QStringLiteral("run.meta"), scrubAddresses(text, meta.profile), error);
}

bool CaptureWriter::writeBench(const QVector<BenchRow>& rows, bool merge, QString* error) const {
    QVector<BenchRow> all = rows;
    QString kept;
    if (merge && has(QStringLiteral("bench.csv"))) {
        QSet<QString> states;
        for (const BenchRow& r : rows) {
            states.insert(r.plcState);
        }
        QFile f(QDir(folder()).filePath(QStringLiteral("bench.csv")));
        if (f.open(QIODevice::ReadOnly)) {
            const QStringList lines =
                QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (int i = 1; i < lines.size(); ++i) { // line 0 is the header
                const QStringList cols = lines[i].split(QLatin1Char(','));
                if (cols.size() > 1 && !states.contains(cols[1])) {
                    kept += lines[i] + QLatin1Char('\n');
                }
            }
        }
    }
    return writeFile(QStringLiteral("bench.csv"),
                     benchText(m_profileId, all).trimmed() + QLatin1Char('\n') + kept, error);
}

bool CaptureWriter::writeBenchHeader(QString* error) const {
    return writeFile(QStringLiteral("bench.csv"), benchHeader(), error);
}

} // namespace mc::hil
