#include "mc_workbench/capture_builder.h"

#include "hil_capture/resolve.h"
#include "mc/core/protocol.h"
#include "mc/mock/mock_plc.h"

#include <QDateTime>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace mc::workbench {

namespace {

const char* opName(mc::Op op) {
    switch (op) {
    case mc::Op::ReadBits:
        return "ReadBits";
    case mc::Op::ReadWords:
        return "ReadWords";
    case mc::Op::WriteBits:
        return "WriteBits";
    case mc::Op::WriteWords:
        return "WriteWords";
    }
    return "Raw";
}

QString deviceText(const mc::Device& device, mc::XyNumbering xy) {
    char buffer[24];
    const size_t size = mc::formatDevice(device, buffer, sizeof(buffer), xy);
    return QString::fromLatin1(buffer, static_cast<qsizetype>(size));
}

mc::ByteView viewOf(const QByteArray& bytes) {
    return mc::ByteView{reinterpret_cast<const uint8_t*>(bytes.constData()),
                        static_cast<size_t>(bytes.size())};
}

double msBetween(qint64 fromNs, qint64 toNs) {
    return static_cast<double>(toNs - fromNs) / 1.0e6;
}

bool isWrite(mc::Op op) {
    return op == mc::Op::WriteBits || op == mc::Op::WriteWords;
}

// Points of the head device's type a request touches: a word operation on a bit device touches 16
// points per word.
quint32 pointsTouched(mc::Op op, const mc::Device& head, quint16 count) {
    if (op == mc::Op::ReadBits || op == mc::Op::WriteBits) {
        return count;
    }
    return mc::deviceInfo(head.type).kind == mc::DeviceKind::Bit ? 16u * count : count;
}

// What the library's parser says about @p answer to a request of this shape.
struct Judgement {
    enum Kind { Ok, PlcOrProtocolError, NeedMore } kind{NeedMore};
    mc::Error error{};
};

Judgement judge(const mc::FrameConfig& frame, mc::Op op, const mc::Device& head, quint16 count,
                const QByteArray& answer) {
    std::vector<uint8_t> filler;
    mc::Request request;
    switch (op) {
    case mc::Op::ReadBits:
        request = mc::Request::readBits(head, count);
        break;
    case mc::Op::ReadWords:
        request = mc::Request::readWords(head, count);
        break;
    case mc::Op::WriteBits:
        filler.assign(count, 0);
        request = mc::Request::writeBits(head, mc::ByteView{filler.data(), filler.size()});
        break;
    case mc::Op::WriteWords:
        filler.assign(static_cast<size_t>(count) * 2, 0);
        request = mc::Request::writeWords(head, mc::ByteView{filler.data(), filler.size()});
        break;
    }
    const mc::McProtocol protocol(frame);
    mc::Parser parser = protocol.parser(request);
    Judgement out;
    switch (parser.feed(viewOf(answer))) {
    case mc::ParseStatus::Done:
        out.kind = Judgement::Ok;
        break;
    case mc::ParseStatus::Failed:
        out.kind = Judgement::PlcOrProtocolError;
        out.error = parser.error();
        break;
    case mc::ParseStatus::NeedMore:
        out.kind = Judgement::NeedMore;
        break;
    }
    return out;
}

} // namespace

BuiltCapture buildCapture(const QVector<FrameRecord>& chunks, const CaptureSettings& settings,
                          const mc::McDeviceConfig& device, bool truncated) {
    BuiltCapture built;
    built.profileId = settings.profileId;

    const mc::FrameConfig& frame = device.frame;
    mc::hil::Profile profile;
    profile.id = settings.profileId;
    profile.plc = QStringLiteral("MC Workbench capture of %1").arg(captureSourceName(settings.source));
    profile.module = QStringLiteral("unknown");
    profile.firmware = QStringLiteral("unknown");
    profile.adapter = QStringLiteral("none");
    profile.plcState = QStringLiteral("as found");
    profile.device = device;
    profile.device.subscriptions.clear();

    const QString frameText = mc::hil::frameName(frame);
    const QString codeText =
        frame.code == mc::DataCode::Binary ? QStringLiteral("Binary") : QStringLiteral("Ascii");
    const int formatNumber = frame.isSerial() ? static_cast<int>(frame.format) : 0;

    mc::MockPlc reader(frame); // reads the request chunks; its memory is never looked at
    int strayRx = 0;
    int trailingRequests = 0;
    int number = 0;
    const int count = static_cast<int>(chunks.size());
    int i = 0;
    while (i < count) {
        if (!chunks[i].tx) {
            ++strayRx;
            ++i;
            continue;
        }
        int j = i + 1;
        QByteArray answer;
        qint64 firstRx = 0;
        qint64 lastRx = 0;
        while (j < count && !chunks[j].tx) {
            if (answer.isEmpty()) {
                firstRx = chunks[j].tNs;
            }
            lastRx = chunks[j].tNs;
            answer.append(chunks[j].bytes);
            ++j;
        }
        const FrameRecord& request = chunks[i];
        if (answer.isEmpty() && j >= count) {
            // The recording ended before an answer came: that is not a timeout (the next request
            // would have told), and a replay would find the mock answering. Left out, and noted.
            ++trailingRequests;
            break;
        }

        // What is the request? One the mock decodes as exactly one operation is a known command.
        reader.bytesIn(viewOf(request.bytes));
        mc::ByteView drained;
        while (reader.nextResponse(drained)) {
        }
        bool decoded = false;
        mc::MockRequestRecord op;
        if (reader.requests().size() == 1 && reader.requests().front().count != 0) {
            op = reader.requests().front();
            decoded = true;
        }
        reader.clearLog();

        mc::hil::StepRecord rec;
        rec.recordId = QStringLiteral("T%1").arg(++number, 6, 10, QLatin1Char('0'));
        rec.source = captureSourceName(settings.source);
        rec.frame = frameText;
        rec.code = codeText;
        rec.format = formatNumber;
        rec.request = request.bytes;
        rec.response = answer;
        rec.expect = QStringLiteral("record");
        if (!answer.isEmpty()) {
            rec.ttfbMs = msBetween(request.tNs, firstRx);
            rec.rxMs = msBetween(firstRx, lastRx);
            rec.rttMs = msBetween(request.tNs, lastRx);
        }

        // Only a read re-encodes from its metadata (a write would need its read-back for RPL-05).
        const bool api = decoded && !isWrite(op.op);
        if (api) {
            rec.via = QStringLiteral("api");
            rec.op = QLatin1String(opName(op.op));
            rec.device = deviceText(op.head, frame.xyNotation);
            rec.count = op.count;
            ++built.apiRecords;
        } else {
            rec.via = QStringLiteral("raw");
            rec.op = QStringLiteral("Raw");
            ++built.rawRecords;
        }

        if (answer.isEmpty()) {
            rec.outcome = api ? QStringLiteral("timeout") : QStringLiteral("noResponse");
            const qint64 nextT = j < count ? chunks[j].tNs : request.tNs;
            rec.waitedMs = msBetween(request.tNs, nextT);
        } else if (decoded) {
            const Judgement verdict = judge(frame, op.op, op.head, op.count, answer);
            switch (verdict.kind) {
            case Judgement::Ok:
                rec.outcome = QStringLiteral("ok");
                break;
            case Judgement::PlcOrProtocolError:
                rec.outcome = mc::hil::outcomeText(verdict.error);
                break;
            case Judgement::NeedMore:
                rec.outcome = QStringLiteral("timeout");
                rec.partial = true;
                break;
            }
            if (verdict.kind == Judgement::Ok) {
                const quint32 touched = pointsTouched(op.op, op.head, op.count);
                std::optional<quint32>& end =
                    profile.deviceEnd[static_cast<size_t>(op.head.type)];
                const quint32 last = op.head.number + touched - 1;
                if (!end || *end < last) {
                    end = last;
                }
            }
        } else {
            rec.outcome = QStringLiteral("protocolError");
        }
        built.steps.push_back(rec);
        i = j;
    }

    QString note = settings.note.trimmed();
    if (settings.source != CaptureSource::RealPlc) {
        const QString what = settings.source == CaptureSource::MockPlc
                                 ? QStringLiteral("mock PLC")
                                 : QStringLiteral("virtual_plc");
        note = QStringLiteral("MC Workbench capture of a %1, not hardware%2")
                   .arg(what, note.isEmpty() ? QString() : QStringLiteral("; ") + note);
    } else {
        note = QStringLiteral("MC Workbench capture of a real PLC%1")
                   .arg(note.isEmpty() ? QString() : QStringLiteral("; ") + note);
    }
    built.meta.profile = profile;
    built.meta.date = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    built.meta.operatorNote = note;
    built.meta.plcState = QStringLiteral("RUN");
    built.meta.extra.push_back({QStringLiteral("gui_capture"), QStringLiteral("mc_workbench")});
    built.meta.extra.push_back({QStringLiteral("capture_source"), captureSourceName(settings.source)});
    built.meta.extra.push_back({QStringLiteral("chunks"), QString::number(count)});
    built.meta.extra.push_back({QStringLiteral("exchanges"), QString::number(built.steps.size())});
    built.meta.extra.push_back({QStringLiteral("api_records"), QString::number(built.apiRecords)});
    built.meta.extra.push_back({QStringLiteral("raw_records"), QString::number(built.rawRecords)});
    if (strayRx != 0) {
        built.meta.extra.push_back({QStringLiteral("stray_rx"), QString::number(strayRx)});
    }
    if (trailingRequests != 0) {
        built.meta.extra.push_back({QStringLiteral("unanswered_last_request"), QStringLiteral("left out")});
    }
    if (truncated) {
        built.meta.extra.push_back({QStringLiteral("truncated"), QStringLiteral("yes")});
    }
    return built;
}

bool writeCapture(const BuiltCapture& capture, const QString& outputRoot, QString* error,
                  QStringList* files) {
    mc::hil::CaptureWriter writer(outputRoot, capture.profileId);
    if (!writer.prepare(error)) {
        return false;
    }
    if (!writer.writeSteps(capture.steps, error)) {
        return false;
    }
    if (!writer.writeSession({}, error)) {
        return false;
    }
    if (!writer.writeRunMeta(capture.meta, error)) {
        return false;
    }
    if (files != nullptr) {
        *files = {QStringLiteral("run.meta"), QStringLiteral("steps.vec"),
                  QStringLiteral("session.vec")};
    }
    return true;
}

} // namespace mc::workbench
