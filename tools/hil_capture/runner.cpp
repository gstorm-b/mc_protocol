#include "hil_capture/runner.h"

#include "mc/core/protocol.h"
#include "mc/device/serial_transport.h"
#include "mc/device/tcp_transport.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QProcess>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <climits>
#include <cmath>
#include <thread>

namespace mc::hil {

namespace {

constexpr int kReconnectPauseMs = 400; ///< Pause between two reconnect attempts.
constexpr int kMaxPollRecoveries = 3;  ///< Reconnects of one poll step before it gives up.

/// What an operation turned out to be, before it is compared with the expectation.
enum class Observed { Ok, PlcError, Timeout, NoResponse, NotSent, Protocol, LinkLost, Peer };

/// Outcome classes of a step, ordered by severity.
enum class Category { Passed = 0, NotSupported = 1, Diverged = 2, Failed = 3 };

struct Observation {
    Observed kind{Observed::Ok};
    QString outcome; ///< The spelled outcome (`outcomeText`).
    Error error{};
    QByteArray payload; ///< The normalized read payload.
    int frames{0};      ///< Frames put on the wire (not counting EOT).
    double waitedMs{std::numeric_limits<double>::quiet_NaN()};
};

struct Verdict {
    Category category{Category::Passed};
    QString note;
};

const char* verdictWord(Category c) {
    switch (c) {
    case Category::Passed:
        return "passed";
    case Category::NotSupported:
        return "unsupported";
    case Category::Diverged:
        return "diverged";
    case Category::Failed:
        return "failed";
    }
    return "failed";
}

bool isEot(const QByteArray& b) {
    return b == QByteArray("\x04", 1) || b == QByteArray("\x04\x0D\x0A", 3);
}

double msBetween(qint64 fromNs, qint64 toNs) { return static_cast<double>(toNs - fromNs) / 1.0e6; }

const char* categoryWord(Category c) {
    switch (c) {
    case Category::Passed:
        return "PASS";
    case Category::NotSupported:
        return "UNSUPPORTED";
    case Category::Diverged:
        return "DIVERGED";
    case Category::Failed:
        return "FAIL";
    }
    return "?";
}

/// One request written and what came back before the next request.
struct Exchange {
    QByteArray tx;
    qint64 tTx{0};
    QByteArray rx;
    qint64 tFirstRx{0};
    qint64 tLastRx{0};
};

QVector<Exchange> exchangesOf(const QVector<WireChunk>& chunks, int from, int to) {
    QVector<Exchange> list;
    for (int i = from; i < to; ++i) {
        const WireChunk& c = chunks[i];
        if (c.dir == WireDirection::Tx) {
            Exchange e;
            e.tx = c.bytes;
            e.tTx = c.tNs;
            list.push_back(e);
        } else {
            if (list.isEmpty()) {
                list.push_back(Exchange()); // bytes with no request before them
            }
            Exchange& e = list.last();
            if (e.rx.isEmpty()) {
                e.tFirstRx = c.tNs;
            }
            e.tLastRx = c.tNs;
            e.rx += c.bytes;
        }
    }
    return list;
}

/// Reads one word of a payload (two bytes, little endian).
uint16_t wordAt(const QByteArray& payload, int index) {
    return static_cast<uint16_t>(static_cast<uint8_t>(payload[index * 2]) |
                                 (static_cast<uint8_t>(payload[index * 2 + 1]) << 8));
}

} // namespace

struct Runner::Impl {
    Impl(const Profile& p, const ResolveResult& r, RunnerSettings s, const ToolIo& i)
        : profile(p), resolved(r), settings(std::move(s)), io(i),
          clock(std::make_shared<RecordingClock>()), writer(settings.outputRoot, p.id) {}

    const Profile& profile;
    const ResolveResult& resolved;
    RunnerSettings settings;
    ToolIo io;
    std::shared_ptr<RecordingClock> clock;
    CaptureWriter writer;

    std::unique_ptr<McDevice> device;
    RecordingTransport* rec{nullptr}; ///< Owned by `device`.
    QObject ctx;                      ///< Context of the signal connections.
    QString configKey;                ///< Serialised configuration in force.
    bool aborted{false}; ///< The link could not be restored: the rest of the run is not tried.
    QString abortReason; ///< Why.

    LinkState state{LinkState::Disconnected};
    LinkReason reason{LinkReason::Requested};
    QString detail;
    bool faulted{false};
    struct Finished {
        Error error;
        QByteArray payload;
    };
    QMap<RequestId, Finished> finished;

    // The poll transcript being recorded, when a poll step runs.
    SessionCapture* poll{nullptr};
    int pollSeq{0};
    int pollChunkIndex{0};
    int cycles{0};
    QSet<RequestId> pollOutstanding;

    QVector<StepRecord> records;
    QVector<SessionCapture> sessions;
    QVector<BenchRow> benchRows;
    bool keepSession{true}; ///< Store the transcript of a poll in session.vec (not for a bench).
    SessionCapture lastCapture; ///< The transcript of the poll that ran last.
    RunMeta meta;
    RunSummary summary;
    QMap<QString, QVector<uint16_t>> written;
    int recoveries{0};

    // ---- small helpers ----------------------------------------------------------------------

    void say(const QString& line) {
        if (io.out != nullptr) {
            *io.out << line << "\n";
            io.out->flush();
        }
    }

    bool waitUntil(const std::function<bool()>& done, int timeoutMs) {
        QElapsedTimer timer;
        timer.start();
        while (!done()) {
            if (timer.elapsed() >= timeoutMs) {
                return false;
            }
            QEventLoop loop;
            QTimer::singleShot(2, &loop, &QEventLoop::quit);
            loop.exec();
        }
        return true;
    }

    void pause(int ms) {
        waitUntil([]() { return false; }, ms);
    }

    /// The reason and state of the link in words. The text McDevice attaches to a link change comes
    /// from the transport and holds the port name or the address, so it goes to the console only,
    /// never into a capture file.
    QString linkWhy() const {
        return QStringLiteral("%1 (%2)").arg(QLatin1String(linkReasonName(reason)),
                                             QLatin1String(linkStateName(state)));
    }

    static QString configKeyOf(const McDeviceConfig& cfg) {
        return QString::fromUtf8(QJsonDocument(cfg.toJson()).toJson(QJsonDocument::Compact));
    }

    /// Waits for the operator's line without blocking the event loop: a thread reads the input and
    /// posts the answer back, while rounds, deadlines and the recording go on.
    /// @return How long the operator took, in ms.
    qint64 askOperator() {
        if (io.in == nullptr) {
            return 0;
        }
        QElapsedTimer asked;
        asked.start();
        bool answered = false;
        std::thread reader([this, &answered]() {
            io.in->readLine();
            QMetaObject::invokeMethod(
                &ctx, [&answered]() { answered = true; }, Qt::QueuedConnection);
        });
        waitUntil([&answered]() { return answered; }, INT_MAX);
        reader.join();
        return asked.elapsed();
    }

    // ---- the device -------------------------------------------------------------------------

    void syncPollChunks() {
        if (poll == nullptr || rec == nullptr) {
            return;
        }
        const QVector<WireChunk>& chunks = rec->chunks();
        for (; pollChunkIndex < chunks.size(); ++pollChunkIndex) {
            const WireChunk& c = chunks[pollChunkIndex];
            SessionChunk s;
            s.tNs = c.tNs;
            s.seq = ++pollSeq;
            s.tx = c.dir == WireDirection::Tx;
            s.bytes = c.bytes;
            poll->chunks.push_back(s);
        }
    }

    void pollEvent(SessionEvent e) {
        if (poll == nullptr) {
            return;
        }
        syncPollChunks();
        e.seq = ++pollSeq;
        poll->events.push_back(std::move(e));
    }

    void createDevice(const McDeviceConfig& cfg) {
        std::unique_ptr<Transport> inner;
        if (cfg.transport == TransportKind::Serial) {
            inner = std::make_unique<SerialTransport>(cfg.serial);
        } else {
            inner = std::make_unique<TcpTransport>(cfg.tcp);
        }
        auto recording = std::make_unique<RecordingTransport>(std::move(inner), clock);
        rec = recording.get();
        device = std::make_unique<McDevice>(cfg, std::move(recording));
        configKey = configKeyOf(cfg);
        McDevice* d = device.get();
        QObject::connect(d, &McDevice::linkStateChanged, &ctx,
                         [this](mc::LinkState s, mc::LinkReason r, const QString& text) {
                             state = s;
                             reason = r;
                             detail = text;
                             if (s == LinkState::Connected) {
                                 faulted = false;
                             }
                             pollEvent(linkStateEvent(s, r, clock->nowNs()));
                         });
        QObject::connect(d, &McDevice::linkFault, &ctx, [this](const mc::LinkFaultInfo& f) {
            faulted = true;
            pollEvent(linkFaultEvent(f, clock->nowNs()));
        });
        QObject::connect(
            d, &McDevice::valuesChanged, &ctx,
            [this](mc::DeviceType t, quint32 round, const QVector<mc::Change>& changes) {
                pollEvent(changesEvent(t, round, changes, clock->nowNs()));
            });
        QObject::connect(d, &McDevice::snapshotReady, &ctx, [this](const mc::DeviceSnapshot& s) {
            pollEvent(snapshotEvent(s, clock->nowNs()));
        });
        QObject::connect(d, &McDevice::cycleDone, &ctx, [this](const mc::CycleInfo& c) {
            ++cycles;
            pollEvent(cycleEvent(c, clock->nowNs()));
        });
        QObject::connect(d, &McDevice::requestFinished, &ctx,
                         [this](mc::RequestId id, const mc::Error& e, const QByteArray& payload) {
                             finished.insert(id, Finished{e, payload});
                             pollOutstanding.remove(id);
                             pollEvent(requestFinishedEvent(id, e, payload, clock->nowNs()));
                         });
    }

    /// Makes the configuration in force equal to @p cfg (disconnecting first when it differs).
    bool ensureConfig(const McDeviceConfig& cfg, QString* why) {
        const QString key = configKeyOf(cfg);
        if (!device) {
            createDevice(cfg);
            return true;
        }
        if (key == configKey) {
            return true;
        }
        device->disconnectFromPlc();
        QString where;
        const Expected<void> ok = device->setConfig(cfg, &where);
        if (!ok) {
            if (why != nullptr) {
                *why = QStringLiteral("setConfig refused (%1): %2")
                           .arg(where, QString::fromLatin1(ok.error().message));
            }
            return false;
        }
        configKey = key;
        // A new Session numbers its requests from 1 again: results of the old one must not match.
        finished.clear();
        return true;
    }

    McDeviceConfig configFor(const ResolvedStep& step) const {
        McDeviceConfig cfg = profile.device;
        cfg.subscriptions.clear();
        cfg.frame = step.frame;
        const ResolvedPoll* p = step.kind == StepKind::Poll ? &step.poll
                                : (step.kind == StepKind::Bench && step.bench.isPollSet)
                                    ? &step.bench.poll
                                    : nullptr;
        if (p != nullptr) {
            if (p->heartbeat) {
                cfg.session.heartbeat.enabled = true;
                cfg.session.heartbeat.device = *p->heartbeat;
            }
            if (p->bitsAsWords) {
                cfg.session.plan.bitsAsWords = *p->bitsAsWords;
            }
        }
        return cfg;
    }

    /// Connects, retrying a refused connection until @p budgetMs is used up.
    bool connectWithRetries(int budgetMs, QString* why) {
        QElapsedTimer timer;
        timer.start();
        const int openTimeout = (profile.device.transport == TransportKind::Tcp
                                     ? std::max(profile.device.tcp.connectTimeoutMs, 500) + 1500
                                     : 4000);
        for (;;) {
            device->connectToPlc();
            waitUntil([this]() { return state != LinkState::Connecting; }, openTimeout);
            if (state == LinkState::Connected) {
                return true;
            }
            if (timer.elapsed() + kReconnectPauseMs >= budgetMs) {
                const QString text = linkWhy();
                if (why != nullptr) {
                    *why = text;
                }
                if (!detail.isEmpty()) {
                    say(QStringLiteral("      could not connect: %1").arg(detail));
                }
                // The PLC is gone for good (as far as this run can tell): do not spend the budget
                // again on every remaining step.
                aborted = true;
                abortReason = text;
                return false;
            }
            pause(kReconnectPauseMs);
        }
    }

    bool ensureConnected(QString* why) {
        if (state == LinkState::Connected) {
            return true;
        }
        return connectWithRetries(settings.reconnectBudgetMs, why);
    }

    /// The link is not Connected after a step: bring it back and record how.
    void recoverLink(const QString& after) {
        if (state == LinkState::Connected) {
            return;
        }
        const QString why = linkWhy();
        QElapsedTimer timer;
        timer.start();
        QString failure;
        const bool back = connectWithRetries(settings.reconnectBudgetMs, &failure);
        ++recoveries;
        meta.extra.push_back({QStringLiteral("recovery.%1").arg(recoveries),
                              QStringLiteral("after %1: %2; %3 in %4 ms")
                                  .arg(after, why,
                                       back ? QStringLiteral("reconnected")
                                            : QStringLiteral("NOT reconnected: ") + failure)
                                  .arg(timer.elapsed())});
        say(QStringLiteral("      recovery after %1: %2 in %3 ms")
                .arg(after,
                     back ? QStringLiteral("reconnected") : QStringLiteral("could not reconnect"))
                .arg(timer.elapsed()));
    }

    // ---- records ----------------------------------------------------------------------------

    StepRecord baseRecord(const ResolvedOp& op) const {
        StepRecord r;
        r.mirrors = op.mirrors;
        r.frame = frameName(op.frame);
        r.code =
            op.frame.code == DataCode::Binary ? QStringLiteral("Binary") : QStringLiteral("Ascii");
        r.format = op.frame.isSerial() ? static_cast<int>(op.frame.format) : 0;
        return r;
    }

    /// Turns the wire exchanges of one operation into step records.
    void recordExchanges(const ResolvedOp& op, const QVector<Exchange>& exchanges,
                         const Observation& obs, const Verdict& verdict) {
        const bool bitUnit = op.request.op == Op::ReadBits || op.request.op == Op::WriteBits;
        const McProtocol codec(op.frame);
        int k = 0;
        for (const Exchange& e : exchanges) {
            if (e.tx.isEmpty()) {
                meta.extra.push_back({QStringLiteral("stray.%1").arg(op.recordId),
                                      hexText(ByteBuf(e.rx.begin(), e.rx.end()))});
                continue;
            }
            StepRecord r = baseRecord(op);
            r.verdict = QLatin1String(verdictWord(verdict.category));
            r.overrideText = op.overrideText;
            r.recordId = k == 0 ? op.recordId : QStringLiteral("%1+%2").arg(op.recordId).arg(k + 1);
            if (k != 0) {
                r.mirrors.clear();
            }
            r.request = e.tx;
            r.response = e.rx;
            // What this frame asked for: the matching frame of the operation, an EOT, or unknown.
            const ByteBuf txBuf(e.tx.begin(), e.tx.end());
            const FrameMeta* fm = nullptr;
            for (int j = 0; j < op.frames.size() && j < op.frameMeta.size(); ++j) {
                if (op.frames[j] == txBuf) {
                    fm = &op.frameMeta[j];
                    break;
                }
            }
            if (op.via == Via::Api && fm != nullptr) {
                r.via = QStringLiteral("api");
            } else if (op.via == Via::Mutate && !op.frameMeta.isEmpty()) {
                r.via = QStringLiteral("mutate");
                fm = &op.frameMeta[0];
            } else {
                r.via = QStringLiteral("raw");
                fm = nullptr;
            }
            if (fm != nullptr) {
                r.op = QLatin1String(opName(fm->op));
                r.device = deviceText(fm->head);
                r.count = fm->count;
            } else {
                r.op = QStringLiteral("Raw");
            }
            // Outcome of this exchange: the parser of its own request judges the bytes.
            const bool eot = isEot(e.tx);
            if (e.rx.isEmpty() && !eot &&
                (obs.kind == Observed::Peer || obs.kind == Observed::LinkLost ||
                 obs.kind == Observed::NotSent)) {
                r.outcome = obs.outcome; // the observed outcome: peerClosed, notSent LinkDown, ...
                r.waitedMs =
                    std::isnan(obs.waitedMs) ? msBetween(e.tTx, clock->nowNs()) : obs.waitedMs;
            } else if (e.rx.isEmpty()) {
                r.outcome = (eot || op.expect.kind == ExpectKind::NoResponse)
                                ? QStringLiteral("noResponse")
                                : QStringLiteral("timeout");
                r.waitedMs =
                    std::isnan(obs.waitedMs) ? msBetween(e.tTx, clock->nowNs()) : obs.waitedMs;
            } else if (fm != nullptr && !eot) {
                Request pr;
                pr.op = fm->op;
                pr.head = fm->head;
                pr.count = fm->count;
                Parser parser = codec.parser(pr);
                const ParseStatus st =
                    parser.feed(ByteView{reinterpret_cast<const uint8_t*>(e.rx.constData()),
                                         static_cast<size_t>(e.rx.size())});
                if (st == ParseStatus::Done) {
                    r.outcome = QStringLiteral("ok");
                } else if (st == ParseStatus::Failed) {
                    r.outcome = outcomeText(parser.error());
                } else {
                    r.outcome = QStringLiteral("timeout");
                    r.partial = true;
                }
            } else {
                r.outcome = QStringLiteral("ok");
            }
            if (k == 0) {
                r.expect = expectText(op.expect, bitUnit);
            }
            if (!e.rx.isEmpty()) {
                r.ttfbMs = msBetween(e.tTx, e.tFirstRx);
                r.rxMs = msBetween(e.tFirstRx, e.tLastRx);
                r.rttMs = msBetween(e.tTx, e.tLastRx);
            }
            records.push_back(r);
            ++k;
        }
    }

    // ---- judging ----------------------------------------------------------------------------

    Verdict judge(const ResolvedOp& op, const Observation& obs) const {
        const Expect& e = op.expect;
        Verdict v;
        const auto fail = [&](Category c, const QString& note) {
            v.category = c;
            v.note = note;
            return v;
        };
        if (e.kind == ExpectKind::Record) {
            return v;
        }
        const bool silence = obs.kind == Observed::Timeout || obs.kind == Observed::NoResponse;
        const bool linkLost = obs.kind == Observed::LinkLost || obs.kind == Observed::Peer;
        switch (e.kind) {
        case ExpectKind::Ok: {
            if (obs.kind == Observed::PlcError) {
                return fail(Category::NotSupported,
                            QStringLiteral("PLC error where ok was expected: %1").arg(obs.outcome));
            }
            if (obs.kind != Observed::Ok) {
                return fail(Category::Failed,
                            QStringLiteral("expected ok, got %1").arg(obs.outcome));
            }
            const bool bitRead = op.request.op == Op::ReadBits;
            if (e.hasValues) {
                const int n = e.values.size();
                const int have = bitRead ? obs.payload.size() : obs.payload.size() / 2;
                if (have != n) {
                    return fail(Category::Failed,
                                QStringLiteral("expected %1 values, got %2").arg(n).arg(have));
                }
                for (int i = 0; i < n; ++i) {
                    const uint16_t got =
                        bitRead ? static_cast<uint16_t>(static_cast<uint8_t>(obs.payload[i]))
                                : wordAt(obs.payload, i);
                    if (got != e.values[i]) {
                        return fail(Category::Failed, QStringLiteral("value %1 is %2, expected %3")
                                                          .arg(i)
                                                          .arg(got)
                                                          .arg(e.values[i]));
                    }
                }
            }
            if (e.hasBitsOn) {
                if (!bitRead) {
                    return fail(Category::Failed, QStringLiteral("bitsOn needs a bit read"));
                }
                for (int i = 0; i < obs.payload.size(); ++i) {
                    const bool want = e.bitsOn.contains(i);
                    const bool got = obs.payload[i] != 0;
                    if (want != got) {
                        return fail(Category::Failed, QStringLiteral("bit %1 is %2, expected %3")
                                                          .arg(i)
                                                          .arg(got ? 1 : 0)
                                                          .arg(want ? 1 : 0));
                    }
                }
            }
            if (!e.valuesFrom.isEmpty()) {
                const auto it = written.constFind(e.valuesFrom);
                if (it == written.constEnd()) {
                    return fail(Category::Failed,
                                QStringLiteral("no values were written by %1").arg(e.valuesFrom));
                }
                const int n = it->size();
                if (obs.payload.size() / 2 < n) {
                    return fail(Category::Failed,
                                QStringLiteral("payload shorter than the %1 words of %2")
                                    .arg(n)
                                    .arg(e.valuesFrom));
                }
                for (int i = 0; i < n; ++i) {
                    if (wordAt(obs.payload, i) != (*it)[i]) {
                        return fail(Category::Failed,
                                    QStringLiteral("word %1 differs from what %2 wrote")
                                        .arg(i)
                                        .arg(e.valuesFrom));
                    }
                }
            }
            if (e.frames >= 0 && obs.frames != e.frames) {
                return fail(Category::Failed, QStringLiteral("%1 frame(s) on the wire, expected %2")
                                                  .arg(obs.frames)
                                                  .arg(e.frames));
            }
            return v;
        }
        case ExpectKind::PlcError:
            if (obs.kind == Observed::PlcError) {
                return v;
            }
            return fail(linkLost ? Category::Failed : Category::Diverged,
                        QStringLiteral("expected a PLC error, got %1").arg(obs.outcome));
        case ExpectKind::Timeout:
        case ExpectKind::NoResponse:
            if (silence) {
                return v;
            }
            return fail(linkLost ? Category::Failed : Category::Diverged,
                        QStringLiteral("expected silence, got %1").arg(obs.outcome));
        case ExpectKind::NotSent:
            if (obs.kind == Observed::NotSent) {
                return v;
            }
            return fail(
                Category::Diverged,
                QStringLiteral("the library was expected to refuse, got %1").arg(obs.outcome));
        case ExpectKind::Record:
            break;
        }
        return v;
    }

    // ---- operations -------------------------------------------------------------------------

    Observation observeFinished(const Finished& f) const {
        Observation o;
        o.error = f.error;
        o.payload = f.payload;
        if (f.error.ok()) {
            o.kind = Observed::Ok;
            o.outcome = QStringLiteral("ok");
            return o;
        }
        o.outcome = outcomeText(f.error);
        switch (f.error.code) {
        case ErrorCode::PlcError:
            o.kind = Observed::PlcError;
            break;
        case ErrorCode::Timeout:
            o.kind = Observed::Timeout;
            break;
        case ErrorCode::LinkDown:
        case ErrorCode::QueueFull:
            o.kind = Observed::LinkLost;
            break;
        default:
            o.kind = f.error.category == ErrorCategory::Protocol ? Observed::Protocol
                                                                 : Observed::NotSent;
            break;
        }
        return o;
    }

    Verdict runApiOp(const ResolvedOp& op, const ResolvedStep& step, int opIndex) {
        Observation obs;
        QString why;
        if (!ensureConnected(&why)) {
            obs.kind = Observed::LinkLost;
            obs.outcome = QStringLiteral("notSent LinkDown");
            meta.notSent.push_back({op.recordId, QStringLiteral("link down: %1").arg(why)});
            return judge(op, obs);
        }
        const int from = rec->chunkCount();
        Request r;
        r.op = op.request.op;
        r.head = op.request.head;
        r.count = op.request.count;
        r.data = ByteView{op.request.data.data(), op.request.data.size()};
        const Expected<RequestId> id = device->submit(r);
        if (!id) {
            obs.error = id.error();
            obs.kind =
                id.error().code == ErrorCode::LinkDown ? Observed::LinkLost : Observed::NotSent;
            obs.outcome = QStringLiteral("notSent ") + errorCodeName(id.error().code);
            meta.notSent.push_back({op.recordId, obs.outcome});
            return judge(op, obs);
        }
        const int timeout = static_cast<int>(op.frame.effectiveTimeoutMs()) *
                                std::max<int>(2, static_cast<int>(op.frames.size()) + 1) +
                            3000;
        const bool done = waitUntil([&]() { return finished.contains(id.value()); }, timeout);
        if (!done) {
            obs.kind = Observed::LinkLost;
            obs.outcome = QStringLiteral("tool timeout (no completion within %1 ms)").arg(timeout);
        } else {
            obs = observeFinished(finished.value(id.value()));
        }
        const int to = rec->chunkCount();
        const QVector<Exchange> exchanges = exchangesOf(rec->chunks(), from, to);
        for (const Exchange& e : exchanges) {
            if (!e.tx.isEmpty() && !isEot(e.tx)) {
                ++obs.frames;
            }
        }
        const Verdict verdict = judge(op, obs);
        recordExchanges(op, exchanges, obs, verdict);
        if (obs.kind == Observed::Ok && op.request.op == Op::WriteWords) {
            QVector<uint16_t> words;
            for (size_t i = 0; i + 1 < op.request.data.size(); i += 2) {
                words.push_back(
                    static_cast<uint16_t>(op.request.data[i] | (op.request.data[i + 1] << 8)));
            }
            written.insert(op.recordId, words);
            if (opIndex == 0) {
                written.insert(step.id, words);
            }
        }
        if (obs.kind == Observed::Ok && !op.metaKey.isEmpty() && obs.payload.size() >= 2) {
            meta.extra.push_back({op.metaKey, QString::number(wordAt(obs.payload, 0))});
        }
        return verdict;
    }

    Verdict runFrameOp(const ResolvedOp& op) {
        Observation obs;
        QString why;
        if (!ensureConnected(&why)) {
            obs.kind = Observed::LinkLost;
            obs.outcome = QStringLiteral("notSent LinkDown");
            meta.notSent.push_back({op.recordId, QStringLiteral("link down: %1").arg(why)});
            return judge(op, obs);
        }
        rec->setRawMode(true);
        QByteArray received;
        qint64 lastRxNs = 0;
        QObject::connect(rec, &RecordingTransport::rawReadyRead, &ctx, [&]() {
            received += rec->takeRaw();
            lastRxNs = clock->nowNs();
        });
        const int from = rec->chunkCount();
        bool wrote = true;
        for (const ByteBuf& frame : op.frames) {
            wrote = wrote && rec->write(ByteView{frame.data(), frame.size()});
        }
        const qint64 sentNs = clock->nowNs();
        const int timeoutMs = static_cast<int>(op.frame.effectiveTimeoutMs());
        const int quietMs = op.frame.isSerial()
                                ? std::max<int>(profile.device.session.serialInterCharMs, 100)
                                : 200;
        // A response is judged by the parser of the request the frame was built from; without one
        // (raw) the answer ends when the line has been quiet for a while.
        const FrameMeta* meta0 = op.frameMeta.isEmpty() ? nullptr : &op.frameMeta[0];
        Parser parser = McProtocol(op.frame).parser(Request{});
        if (meta0 != nullptr) {
            Request pr;
            pr.op = meta0->op;
            pr.head = meta0->head;
            pr.count = meta0->count;
            parser = McProtocol(op.frame).parser(pr);
        }
        const auto complete = [&]() {
            if (meta0 == nullptr || received.isEmpty()) {
                return false;
            }
            Parser p = parser;
            return p.feed(ByteView{reinterpret_cast<const uint8_t*>(received.constData()),
                                   static_cast<size_t>(received.size())}) != ParseStatus::NeedMore;
        };
        if (wrote) {
            waitUntil(
                [&]() {
                    if (state != LinkState::Connected) {
                        return true; // the peer closed or the link faulted
                    }
                    if (received.isEmpty()) {
                        return false;
                    }
                    if (complete()) {
                        return true;
                    }
                    return (clock->nowNs() - lastRxNs) / 1000000 >= quietMs;
                },
                timeoutMs);
        }
        const bool linkLost = state != LinkState::Connected;
        obs.waitedMs = msBetween(sentNs, clock->nowNs());
        if (!wrote) {
            obs.kind = Observed::LinkLost;
            obs.outcome = QStringLiteral("notSent LinkDown");
        } else if (linkLost && received.isEmpty()) {
            obs.kind = Observed::Peer;
            obs.outcome = reason == LinkReason::PeerClosed ? QStringLiteral("peerClosed")
                                                           : QStringLiteral("transportError");
        } else if (received.isEmpty()) {
            obs.kind = Observed::NoResponse;
            obs.outcome = op.expect.kind == ExpectKind::NoResponse ? QStringLiteral("noResponse")
                                                                   : QStringLiteral("timeout");
        } else if (meta0 != nullptr) {
            Parser p = parser;
            const ParseStatus st =
                p.feed(ByteView{reinterpret_cast<const uint8_t*>(received.constData()),
                                static_cast<size_t>(received.size())});
            if (st == ParseStatus::Done) {
                obs.kind = Observed::Ok;
                obs.outcome = QStringLiteral("ok");
            } else if (st == ParseStatus::Failed) {
                obs.error = p.error();
                obs.kind = p.error().category == ErrorCategory::Plc ? Observed::PlcError
                                                                    : Observed::Protocol;
                obs.outcome = outcomeText(p.error());
            } else {
                obs.kind = Observed::Timeout;
                obs.outcome = QStringLiteral("timeout");
            }
        } else {
            obs.kind = Observed::Ok;
            obs.outcome = QStringLiteral("ok");
        }

        // Recovery before the link goes back to McDevice.
        if (op.recover == Recover::Eot && state == LinkState::Connected) {
            const ByteBuf eot = op.frame.format == SerialFormat::Format4 ? ByteBuf{0x04, 0x0D, 0x0A}
                                                                         : ByteBuf{0x04};
            rec->write(ByteView{eot.data(), eot.size()});
            pause(std::max<int>(profile.device.session.serialFlushMs * 2, 150));
            received += rec->takeRaw();
        }
        const int to = rec->chunkCount();
        rec->disconnect(&ctx);
        rec->setRawMode(false);
        const QVector<Exchange> exchanges = exchangesOf(rec->chunks(), from, to);
        const Verdict verdict = judge(op, obs);
        recordExchanges(op, exchanges, obs, verdict);
        if (!wrote) {
            this->meta.notSent.push_back({op.recordId, obs.outcome});
        }
        if (op.recover == Recover::Reconnect) {
            device->disconnectFromPlc();
            recoverLink(op.recordId);
        } else if (state != LinkState::Connected) {
            recoverLink(op.recordId);
        }
        return verdict;
    }

    Verdict runPoll(const ResolvedStep& step, const ResolvedPoll& pollSpec) {
        Verdict verdict;
        const auto failWith = [&](const QString& why) {
            verdict.category = Category::Failed;
            verdict.note = why;
            return verdict;
        };
        QString why;
        // A fresh Session: round numbering restarts and the subscriptions are exactly the plan's.
        if (device) {
            device->disconnectFromPlc();
        }
        if (!ensureConfig(configFor(step), &why)) {
            return failWith(why);
        }
        SessionCapture capture;
        capture.stepId = step.id;
        capture.overrideText = overrideTextOf(step.frameOverride);
        poll = &capture;
        pollSeq = 0;
        pollChunkIndex = rec->chunkCount();
        cycles = 0;
        pollOutstanding.clear();
        // The inputs the session gets are on record: the heartbeat, then the initial subscriptions.
        if (pollSpec.heartbeat) {
            pollEvent(heartbeatInput(true, *pollSpec.heartbeat, clock->nowNs()));
        }
        QMap<QString, SubscriptionId> ids;
        for (const ResolvedSub& s : pollSpec.subs) {
            const Expected<SubscriptionId> id = device->subscribe(s.head, s.count);
            if (!id) {
                poll = nullptr;
                return failWith(QStringLiteral("subscription '%1' refused: %2")
                                    .arg(s.name, describeError(id.error())));
            }
            ids.insert(s.name, id.value());
            pollEvent(subscribeInput(s.name, s.head, s.count, clock->nowNs()));
        }
        const auto finish = [&]() {
            device->disconnectFromPlc();
            syncPollChunks();
            poll = nullptr;
            for (auto it = ids.begin(); it != ids.end(); ++it) {
                device->unsubscribe(it.value());
            }
            lastCapture = capture;
            if (keepSession) {
                sessions.push_back(capture);
            }
        };

        if (!connectWithRetries(settings.reconnectBudgetMs, &why)) {
            finish();
            return failWith(QStringLiteral("cannot connect: %1").arg(why));
        }
        const int interval = static_cast<int>(profile.device.session.cycleIntervalMs);
        // The time the operator keeps a prompt open is not the poll's: it does not count against
        // the budget (the rounds go on meanwhile, so `done` is refreshed after every prompt).
        const int budget =
            pollSpec.rounds * (interval + static_cast<int>(step.frame.effectiveTimeoutMs())) +
            settings.pollSlackMs;
        QElapsedTimer timer;
        timer.start();
        qint64 promptMs = 0;
        int done = 0;
        int pollRecoveries = 0;
        QSet<int> actionsDone;
        while (done < pollSpec.rounds && timer.elapsed() - promptMs < budget) {
            waitUntil([&]() { return cycles > done || state != LinkState::Connected; }, 1000);
            if (state != LinkState::Connected) {
                if (++pollRecoveries > kMaxPollRecoveries) {
                    finish();
                    return failWith(
                        QStringLiteral("the link was lost %1 times").arg(pollRecoveries));
                }
                device->disconnectFromPlc();
                recoverLink(step.id);
                if (state != LinkState::Connected) {
                    finish();
                    return failWith(QStringLiteral("the link could not be restored"));
                }
                continue;
            }
            if (cycles <= done) {
                continue;
            }
            done = cycles;
            for (int a = 0; a < pollSpec.actions.size(); ++a) {
                const ResolvedAction& act = pollSpec.actions[a];
                if (act.after > done || actionsDone.contains(a)) {
                    continue;
                }
                actionsDone.insert(a);
                switch (act.kind) {
                case PollAction::Kind::Write: {
                    Request r;
                    r.op = act.write.request.op;
                    r.head = act.write.request.head;
                    r.count = act.write.request.count;
                    r.data = ByteView{act.write.request.data.data(), act.write.request.data.size()};
                    const Expected<RequestId> id = device->submit(r);
                    if (id) {
                        pollEvent(writeInput(r.op, r.head, r.count, act.write.request.data,
                                             clock->nowNs()));
                        pollOutstanding.insert(id.value());
                    } else {
                        verdict.category = Category::Failed;
                        verdict.note =
                            QStringLiteral("poll write refused: %1").arg(describeError(id.error()));
                    }
                    break;
                }
                case PollAction::Kind::Subscribe: {
                    const Expected<SubscriptionId> id =
                        device->subscribe(act.sub.head, act.sub.count);
                    if (id) {
                        ids.insert(act.sub.name, id.value());
                        pollEvent(subscribeInput(act.sub.name, act.sub.head, act.sub.count,
                                                 clock->nowNs()));
                    } else {
                        verdict.category = Category::Failed;
                        verdict.note =
                            QStringLiteral("subscription '%1' refused").arg(act.sub.name);
                    }
                    break;
                }
                case PollAction::Kind::Unsubscribe: {
                    const auto it = ids.find(act.name);
                    if (it != ids.end()) {
                        pollEvent(unsubscribeInput(act.name, clock->nowNs()));
                        device->unsubscribe(it.value());
                        ids.erase(it);
                    }
                    break;
                }
                case PollAction::Kind::Prompt:
                    if (io.out != nullptr) {
                        *io.out << "      " << act.text << " (press Enter to continue) ";
                        io.out->flush();
                    }
                    promptMs += askOperator();
                    done = std::max(done, cycles);
                    break;
                }
            }
        }
        waitUntil([&]() { return pollOutstanding.isEmpty(); }, 2000);
        const bool completed = done >= pollSpec.rounds;
        finish();
        if (!completed) {
            return failWith(QStringLiteral("only %1 of %2 rounds finished in %3 ms")
                                .arg(done)
                                .arg(pollSpec.rounds)
                                .arg(timer.elapsed() - promptMs));
        }
        return verdict;
    }

    // ---- bench ------------------------------------------------------------------------------

    /// The raw word of the scan time device, or empty when the profile declares none.
    QString readScanTime() {
        if (!profile.scanTime) {
            return QString();
        }
        Request r;
        r.op = Op::ReadWords;
        r.head = *profile.scanTime;
        r.count = 1;
        const Expected<RequestId> id = device->submit(r);
        if (!id || !waitUntil([&]() { return finished.contains(id.value()); }, 5000)) {
            return QString();
        }
        const Finished f = finished.value(id.value());
        return f.error.ok() && f.payload.size() >= 2 ? QString::number(wordAt(f.payload, 0))
                                                     : QString();
    }

    Verdict runBench(const ResolvedStep& step) {
        Verdict verdict;
        const auto failWith = [&](const QString& why) {
            verdict.category = Category::Failed;
            verdict.note = why;
            return verdict;
        };
        const int reps = step.bench.reps >= 0 ? step.bench.reps : settings.benchReps;
        const int warm = step.bench.warmup;
        if (step.bench.isPollSet) {
            ResolvedStep asPoll = step;
            asPoll.kind = StepKind::Poll;
            asPoll.poll = step.bench.poll;
            asPoll.poll.rounds = warm + reps;
            asPoll.poll.actions.clear();
            keepSession = false;
            lastCapture = SessionCapture();
            verdict = runPoll(asPoll, asPoll.poll);
            keepSession = true;
            if (verdict.category != Category::Passed) {
                return verdict;
            }
            // One repetition is one round: its duration from the cycleDone event, its bytes from
            // the wire chunks between the previous cycleDone and this one.
            qint64 prevSeq = 0;
            int round = 0;
            QVector<const SessionEvent*> cycleEvents;
            for (const SessionEvent& e : lastCapture.events) {
                if (e.name == QStringLiteral("cycleDone")) {
                    cycleEvents.push_back(&e);
                }
            }
            int points = 0;
            for (const ResolvedSub& sub : step.bench.poll.subs) {
                points += static_cast<int>(sub.count);
            }
            for (const SessionEvent* e : cycleEvents) {
                ++round;
                if (round > warm && round - warm <= reps) {
                    BenchRow row;
                    row.plcState = settings.plcState;
                    row.step = step.id;
                    row.op = QStringLiteral("PollRound");
                    row.device = step.bench.pollSetId;
                    row.count = points;
                    row.rep = round - warm;
                    for (const SessionChunk& c : lastCapture.chunks) {
                        if (c.seq > prevSeq && c.seq < e->seq) {
                            (c.tx ? row.reqBytes : row.respBytes) += c.bytes.size();
                        }
                    }
                    for (const auto& k : e->keys) {
                        if (k.first == QStringLiteral("duration_ms")) {
                            row.rttMs = k.second.toDouble();
                        }
                    }
                    benchRows.push_back(row);
                }
                prevSeq = e->seq;
            }
            if (round < warm + reps) {
                return failWith(QStringLiteral("%1 of %2 rounds").arg(round).arg(warm + reps));
            }
            return verdict;
        }

        const ResolvedOp& op = step.bench.request;
        QString why;
        if (!ensureConnected(&why)) {
            return failWith(QStringLiteral("cannot connect: %1").arg(why));
        }
        const QString scan = readScanTime();
        const int interval = static_cast<int>(profile.device.session.cycleIntervalMs);
        int errors = 0;
        QString lastError;
        for (int i = 0; i < warm + reps; ++i) {
            QElapsedTimer spent;
            spent.start();
            const int from = rec->chunkCount();
            Request r;
            r.op = op.request.op;
            r.head = op.request.head;
            r.count = op.request.count;
            r.data = ByteView{op.request.data.data(), op.request.data.size()};
            const Expected<RequestId> id = device->submit(r);
            bool ok = static_cast<bool>(id);
            if (!ok) {
                lastError = describeError(id.error());
            } else {
                const int timeout = static_cast<int>(op.frame.effectiveTimeoutMs()) * 2 + 3000;
                const bool done =
                    waitUntil([&]() { return finished.contains(id.value()); }, timeout);
                ok = done && finished.value(id.value()).error.ok();
                if (!ok) {
                    lastError = done ? outcomeText(finished.value(id.value()).error)
                                     : QStringLiteral("no completion");
                }
            }
            if (!ok) {
                ++errors;
                if (state != LinkState::Connected && !ensureConnected(&why)) {
                    return failWith(QStringLiteral("link lost during the bench: %1").arg(why));
                }
            } else if (i >= warm) {
                const QVector<Exchange> ex = exchangesOf(rec->chunks(), from, rec->chunkCount());
                BenchRow row;
                row.plcState = settings.plcState;
                row.step = step.id;
                row.op = QLatin1String(opName(op.request.op));
                row.device = deviceText(op.request.head);
                row.count = op.request.count;
                row.rep = i - warm + 1;
                row.scanMs = scan;
                qint64 tFirstTx = 0;
                qint64 tFirstRx = 0;
                qint64 tLastRx = 0;
                bool haveTx = false;
                bool haveRx = false;
                for (const Exchange& e : ex) {
                    row.reqBytes += e.tx.size();
                    row.respBytes += e.rx.size();
                    if (!e.tx.isEmpty() && !haveTx) {
                        tFirstTx = e.tTx;
                        haveTx = true;
                    }
                    if (!e.rx.isEmpty()) {
                        if (!haveRx) {
                            tFirstRx = e.tFirstRx;
                            haveRx = true;
                        }
                        tLastRx = e.tLastRx;
                    }
                }
                if (haveTx && haveRx) {
                    row.ttfbMs = msBetween(tFirstTx, tFirstRx);
                    row.rxMs = msBetween(tFirstRx, tLastRx);
                    row.rttMs = msBetween(tFirstTx, tLastRx);
                }
                benchRows.push_back(row);
            }
            // Requests are spaced by the cycle interval, so the load looks like polling.
            const qint64 wait = interval - spent.elapsed();
            if (wait > 0) {
                pause(static_cast<int>(wait));
            }
        }
        if (errors > 0) {
            return failWith(
                QStringLiteral("%1 of %2 repetitions failed (last: %3; link state %4, %5)")
                    .arg(errors)
                    .arg(warm + reps)
                    .arg(lastError)
                    .arg(static_cast<int>(state))
                    .arg(detail));
        }
        return verdict;
    }

    // ---- the run ----------------------------------------------------------------------------

    static QString gitCommit() {
        QProcess git;
        git.start(QStringLiteral("git"), {QStringLiteral("rev-parse"), QStringLiteral("--short=12"),
                                          QStringLiteral("HEAD")});
        if (!git.waitForFinished(3000) || git.exitStatus() != QProcess::NormalExit ||
            git.exitCode() != 0) {
            return QStringLiteral("unknown");
        }
        const QString text = QString::fromUtf8(git.readAllStandardOutput()).trimmed();
        return text.isEmpty() ? QStringLiteral("unknown") : text;
    }

    void count(Category c, const QString& id, const QString& what, const QString& note) {
        switch (c) {
        case Category::Passed:
            ++summary.passed;
            break;
        case Category::NotSupported:
            ++summary.notSupported;
            break;
        case Category::Diverged:
            ++summary.diverged;
            break;
        case Category::Failed:
            ++summary.failed;
            break;
        }
        const QString line =
            QStringLiteral("%1  %2  %3%4")
                .arg(QLatin1String(categoryWord(c)), -11)
                .arg(id, -10)
                .arg(what, note.isEmpty() ? QString() : QStringLiteral("  -- ") + note);
        summary.lines.push_back(line);
        say(line);
    }

    RunSummary run() {
        summary.folder = writer.folder();
        QString error;
        const bool stopPass = settings.plcState == QLatin1String("STOP");
        if (!writer.prepare(&error, stopPass)) {
            summary.error = error;
            return summary;
        }
        meta.profile = profile;
        meta.plcState = settings.plcState;
        meta.operatorNote = settings.operatorNote;
        meta.gitCommit = settings.gitCommit.isEmpty() ? gitCommit() : settings.gitCommit;
        meta.date = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

        for (const ResolvedStep& step : resolved.steps) {
            if (step.skipped()) {
                ++summary.skipped;
                meta.skipped.push_back({step.id, step.skipReason});
                const QString line = QStringLiteral("%1  %2  %3")
                                         .arg(QStringLiteral("SKIP"), -11)
                                         .arg(step.id, -10)
                                         .arg(step.skipReason);
                summary.lines.push_back(line);
                say(line);
                continue;
            }
            if (aborted) {
                count(
                    Category::Failed, step.id, QStringLiteral("not run"),
                    QStringLiteral("aborted, the link could not be restored: %1").arg(abortReason));
                continue;
            }
            if (io.stepStarted) {
                io.stepStarted(step.id);
            }
            Verdict worst;
            QString what;
            if (step.kind == StepKind::Bench) {
                const int reps = step.bench.reps >= 0 ? step.bench.reps : settings.benchReps;
                what = QStringLiteral("bench, %1 repetition(s) after %2 warm-up")
                           .arg(reps)
                           .arg(step.bench.warmup);
                QString needed;
                if (!ensureConfig(configFor(step), &needed)) {
                    worst.category = Category::Failed;
                    worst.note = needed;
                } else {
                    worst = runBench(step);
                }
                if (!step.bench.isPollSet && state != LinkState::Connected) {
                    recoverLink(step.id);
                }
            } else if (step.kind == StepKind::Poll) {
                what = QStringLiteral("poll, %1 round(s)").arg(step.poll.rounds);
                worst = runPoll(step, step.poll); // a poll ends disconnected on purpose
            } else {
                QString needed;
                const McDeviceConfig cfg = configFor(step);
                if (!ensureConfig(cfg, &needed)) {
                    worst.category = Category::Failed;
                    worst.note = needed;
                } else {
                    for (int i = 0; i < step.ops.size(); ++i) {
                        const ResolvedOp& op = step.ops[i];
                        if (what.isEmpty()) {
                            what = op.description;
                        }
                        const Verdict v =
                            op.via == Via::Api ? runApiOp(op, step, i) : runFrameOp(op);
                        if (static_cast<int>(v.category) > static_cast<int>(worst.category)) {
                            worst = v;
                            worst.note = QStringLiteral("%1: %2").arg(op.recordId, worst.note);
                        }
                    }
                }
                // A link that is not up after the step (a fault, a closed peer) is reconnected.
                if (state != LinkState::Connected) {
                    recoverLink(step.id);
                }
            }
            count(worst.category, step.id, what, worst.note);
        }
        if (device) {
            device->disconnectFromPlc();
        }

        // The capture set.
        // A STOP pass leaves the RUN pass's files alone (see capture_writer.h); it adds its bench
        // rows. The poll steps that have a transcript in session.vec: a replay checks that none is
        // missing.
        QStringList polled;
        for (const SessionCapture& capture : sessions) {
            polled << capture.stepId;
        }
        if (!polled.isEmpty()) {
            meta.extra.push_back({QStringLiteral("polls"), polled.join(QLatin1Char(' '))});
        }
        const bool writeAll = !stopPass;
        if ((writeAll || !writer.has(QStringLiteral("steps.vec"))
                 ? !writer.writeSteps(records, &error)
                 : false) ||
            (writeAll || !writer.has(QStringLiteral("session.vec"))
                 ? !writer.writeSession(sessions, &error)
                 : false) ||
            !writer.writeRunMeta(meta, &error, stopPass) ||
            !writer.writeBench(benchRows, stopPass, &error)) {
            summary.error = error;
        }
        say(QStringLiteral("passed %1, failed %2, diverged %3, not supported %4, skipped %5")
                .arg(summary.passed)
                .arg(summary.failed)
                .arg(summary.diverged)
                .arg(summary.notSupported)
                .arg(summary.skipped));
        say(QStringLiteral("capture written to %1").arg(summary.folder));
        return summary;
    }
};

Runner::Runner(const Profile& profile, const ResolveResult& resolved, RunnerSettings settings,
               const ToolIo& io)
    : m_impl(std::make_unique<Impl>(profile, resolved, std::move(settings), io)) {}

Runner::~Runner() = default;

RunSummary Runner::run() { return m_impl->run(); }

} // namespace mc::hil
