// HIL-05 (SPEC-hil-capture.md): the capture writer's files are read back by tests/common/vectors.h
// with every metadata key intact; run.meta carries every FrameConfig and SessionConfig field and
// never an address; RecordingTransport records every chunk once with monotonic stamps and passes
// signals through. No hardware; the transport tests use a loopback TCP server.
#include "hil_suites.h"
#include "hil_test_support.h"

#include "common/vectors.h"
#include "hil_capture/bench_report.h"
#include "hil_capture/capture_writer.h"
#include "hil_capture/recording_transport.h"
#include "mc/device/tcp_transport.h"
#include "mc/version.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QSet>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

#include <cmath>
#include <memory>

namespace mc::hil::test {

namespace {

QByteArray bytes(std::initializer_list<int> values) {
    QByteArray b;
    for (const int v : values) {
        b.append(static_cast<char>(v));
    }
    return b;
}

QByteArray toQ(const std::vector<uint8_t>& v) {
    return QByteArray(reinterpret_cast<const char*>(v.data()), static_cast<int>(v.size()));
}

std::string toStd(const QString& s) { return s.toStdString(); }

QString readAll(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

// "key: value" lines of run.meta as a map (the last wins).
QMap<QString, QString> parseMeta(const QString& text) {
    QMap<QString, QString> map;
    for (const QString& line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const qsizetype colon = line.indexOf(QStringLiteral(": "));
        if (colon > 0) {
            map.insert(line.left(colon), line.mid(colon + 2));
        } else if (line.endsWith(QLatin1Char(':'))) {
            map.insert(line.chopped(1), QString());
        }
    }
    return map;
}

const mc::test::Vector* byId(const std::vector<mc::test::Vector>& vectors, const QString& id) {
    for (const mc::test::Vector& v : vectors) {
        if (v.id == toStd(id)) {
            return &v;
        }
    }
    return nullptr;
}

class HilCaptureTests : public QObject {
    Q_OBJECT

  private slots:
    // ---- steps.vec ----------------------------------------------------------------------------

    void HIL_05_stepsVecRoundTripsEveryKey() {
        const QString profile = QStringLiteral("p1");
        QVector<StepRecord> records;

        StepRecord ok;
        ok.recordId = QStringLiteral("GV-01");
        ok.mirrors = QStringLiteral("V-3E-B-05/06");
        ok.frame = QStringLiteral("3E");
        ok.code = QStringLiteral("Binary");
        ok.op = QStringLiteral("ReadWords");
        ok.device = QStringLiteral("D100");
        ok.count = 3;
        ok.request = bytes({0x50, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x0C, 0x00, 0x10, 0x00,
                            0x01, 0x04, 0x00, 0x00, 0x64, 0x00, 0x00, 0xA8, 0x03, 0x00});
        ok.response = bytes({0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x08, 0x00, 0x00, 0x00, 0x95,
                             0x19, 0x02, 0x12, 0x30, 0x11});
        ok.outcome = QStringLiteral("ok");
        ok.expect = QStringLiteral("words 1995 1202 1130");
        ok.ttfbMs = 2.814;
        ok.rxMs = 0.041;
        ok.rttMs = 2.855;
        records.push_back(ok);

        StepRecord partial;
        partial.recordId = QStringLiteral("G1-04+2");
        partial.frame = QStringLiteral("3C");
        partial.code = QStringLiteral("Ascii");
        partial.format = 4;
        partial.op = QStringLiteral("ReadBits");
        partial.device = QStringLiteral("M100");
        partial.count = 8;
        partial.request = bytes({0x05, 0x30, 0x30, 0x46, 0x46});
        partial.response = bytes({0x02, 0x30, 0x30});
        partial.partial = true;
        partial.outcome = QStringLiteral("timeout");
        partial.expect = QStringLiteral("ok");
        partial.ttfbMs = 10.5;
        records.push_back(partial);

        StepRecord plc;
        plc.recordId = QStringLiteral("G5-02");
        plc.frame = QStringLiteral("3E");
        plc.code = QStringLiteral("Binary");
        plc.op = QStringLiteral("ReadWords");
        plc.device = QStringLiteral("D12288");
        plc.count = 1;
        plc.request = bytes({1, 2, 3});
        plc.response = bytes({0xD0, 0x00, 0x51, 0xC0});
        Error e;
        e.category = ErrorCategory::Plc;
        e.code = ErrorCode::PlcError;
        e.plcCode = 0xC051;
        e.info.network = 0;
        e.info.pc = 0xFF;
        e.info.io = 0x03FF;
        e.info.station = 0;
        e.info.command = 0x0401;
        e.info.subcommand = 0;
        plc.outcome = outcomeText(e);
        plc.expect = QStringLiteral("plcError");
        plc.rttMs = 3.0;
        records.push_back(plc);

        StepRecord silent; // a timeout with no byte received: no response record can exist
        silent.recordId = QStringLiteral("G7-01");
        silent.via = QStringLiteral("mutate");
        silent.frame = QStringLiteral("3E");
        silent.code = QStringLiteral("Binary");
        silent.op = QStringLiteral("ReadWords");
        silent.device = QStringLiteral("D100");
        silent.count = 1;
        silent.request = bytes({0x50, 0x00});
        silent.outcome = QStringLiteral("timeout");
        silent.expect = QStringLiteral("record");
        silent.waitedMs = 3000.0;
        records.push_back(silent);

        StepRecord raw;
        raw.recordId = QStringLiteral("G7-05.2");
        raw.via = QStringLiteral("raw");
        raw.frame = QStringLiteral("1C");
        raw.code = QStringLiteral("Ascii");
        raw.format = 1;
        raw.op = QStringLiteral("Raw");
        raw.request = bytes({0x04});
        raw.response = bytes({0x06});
        raw.outcome = QStringLiteral("ok");
        records.push_back(raw);

        StepRecord notSent; // nothing was sent: no record, run.meta lists it
        notSent.recordId = QStringLiteral("G3-03");
        notSent.op = QStringLiteral("ReadWords");
        notSent.outcome = QStringLiteral("notSent InvalidDevice");
        records.push_back(notSent);

        const QString dir = scratchDir(QStringLiteral("capture_steps"));
        CaptureWriter writer(dir, profile);
        QString error;
        QVERIFY2(writer.prepare(&error), qPrintable(error));
        QVERIFY2(writer.writeSteps(records, &error), qPrintable(error));

        std::vector<mc::test::Vector> vectors;
        try {
            vectors = mc::test::loadVectors(toStd(writer.folder() + QStringLiteral("/steps.vec")));
        } catch (const std::exception& ex) {
            QFAIL(ex.what());
        }
        const QStringList ids{"CAP-p1-GV-01",     "CAP-p1-GV-01-R", "CAP-p1-G1-04+2",
                              "CAP-p1-G1-04+2-R", "CAP-p1-G5-02",   "CAP-p1-G5-02-R",
                              "CAP-p1-G7-01",     "CAP-p1-G7-05.2", "CAP-p1-G7-05.2-R"};
        QCOMPARE(static_cast<int>(vectors.size()), ids.size()); // the notSent step has no record
        for (int i = 0; i < ids.size(); ++i) {
            QCOMPARE(QString::fromStdString(vectors[i].id), ids[i]);
        }

        const auto field = [&](const QString& id, const char* key) {
            const mc::test::Vector* v = byId(vectors, id);
            return v == nullptr ? QStringLiteral("<no record>")
                                : QString::fromStdString(v->field(key));
        };
        // The first request, key by key.
        QCOMPARE(field(ids[0], "source"), QStringLiteral("plc"));
        QCOMPARE(field(ids[0], "profile"), QStringLiteral("p1"));
        QCOMPARE(field(ids[0], "step"), QStringLiteral("GV-01"));
        QCOMPARE(field(ids[0], "mirrors"), QStringLiteral("V-3E-B-05/06"));
        QCOMPARE(field(ids[0], "frame"), QStringLiteral("3E"));
        QCOMPARE(field(ids[0], "code"), QStringLiteral("Binary"));
        QCOMPARE(field(ids[0], "op"), QStringLiteral("ReadWords"));
        QCOMPARE(field(ids[0], "device"), QStringLiteral("D100"));
        QCOMPARE(field(ids[0], "count"), QStringLiteral("3"));
        QCOMPARE(field(ids[0], "via"), QStringLiteral("api"));
        QCOMPARE(field(ids[0], "kind"), QStringLiteral("request"));
        QCOMPARE(field(ids[0], "format"), QString()); // an Ethernet frame has none
        QCOMPARE(toQ(vectors[0].bytes), ok.request);
        // Its response.
        QCOMPARE(field(ids[1], "kind"), QStringLiteral("response"));
        QCOMPARE(field(ids[1], "of"), QStringLiteral("CAP-p1-GV-01"));
        QCOMPARE(field(ids[1], "outcome"), QStringLiteral("ok"));
        QCOMPARE(field(ids[1], "expect"), QStringLiteral("words 1995 1202 1130"));
        QCOMPARE(field(ids[1], "ttfb_ms"), QStringLiteral("2.814"));
        QCOMPARE(field(ids[1], "rx_ms"), QStringLiteral("0.041"));
        QCOMPARE(field(ids[1], "rtt_ms"), QStringLiteral("2.855"));
        QCOMPARE(toQ(vectors[1].bytes), ok.response);
        // A timeout that received part of a frame.
        QCOMPARE(field(ids[2], "format"), QStringLiteral("4"));
        QCOMPARE(field(ids[2], "frame"), QStringLiteral("3C"));
        QCOMPARE(field(ids[2], "code"), QStringLiteral("Ascii"));
        QCOMPARE(field(ids[2], "op"), QStringLiteral("ReadBits"));
        QCOMPARE(field(ids[3], "kind"), QStringLiteral("response-partial"));
        QCOMPARE(field(ids[3], "of"), QStringLiteral("CAP-p1-G1-04+2"));
        QCOMPARE(field(ids[3], "outcome"), QStringLiteral("timeout"));
        QCOMPARE(field(ids[3], "expect"), QStringLiteral("ok"));
        QCOMPARE(field(ids[3], "ttfb_ms"), QStringLiteral("10.500"));
        QCOMPARE(field(ids[3], "rx_ms"), QString()); // absent times are not written
        QCOMPARE(toQ(vectors[3].bytes), partial.response);
        // A PLC error with its information block.
        QCOMPARE(field(ids[5], "outcome"),
                 QStringLiteral("plcError C051 info 00/FF/03FF/00/0401/0000"));
        QCOMPARE(field(ids[5], "expect"), QStringLiteral("plcError"));
        // A request that got no byte back carries outcome, expect and waited_ms itself.
        QCOMPARE(field(ids[6], "via"), QStringLiteral("mutate"));
        QCOMPARE(field(ids[6], "outcome"), QStringLiteral("timeout"));
        QCOMPARE(field(ids[6], "expect"), QStringLiteral("record"));
        QCOMPARE(field(ids[6], "waited_ms"), QStringLiteral("3000.000"));
        // A raw frame has no device and no count.
        QCOMPARE(field(ids[7], "via"), QStringLiteral("raw"));
        QCOMPARE(field(ids[7], "op"), QStringLiteral("Raw"));
        QCOMPARE(field(ids[7], "device"), QString());
        QCOMPARE(field(ids[7], "count"), QString());
        QCOMPARE(field(ids[7], "format"), QStringLiteral("1"));

        // Every key the writer emits is documented, and every documented key was exercised.
        const QSet<QString> documented{"id",     "source",  "profile", "step",   "mirrors",
                                       "frame",  "code",    "format",  "op",     "device",
                                       "count",  "via",     "kind",    "of",     "outcome",
                                       "expect", "ttfb_ms", "rx_ms",   "rtt_ms", "waited_ms"};
        QSet<QString> seen;
        for (const mc::test::Vector& v : vectors) {
            for (const mc::test::VecField& f : v.fields) {
                seen.insert(QString::fromStdString(f.key));
            }
        }
        QCOMPARE(seen, documented);
    }

    void HIL_05_outcomeAndExpectSpellings() {
        Error plc;
        plc.category = ErrorCategory::Plc;
        plc.code = ErrorCode::PlcError;
        plc.plcCode = 0x5B;
        plc.abnormalCode = 0x10;
        QCOMPARE(outcomeText(plc),
                 QStringLiteral("plcError 005B abnormal 10")); // 1E: no information block
        plc.plcCode = 0xC051;
        plc.abnormalCode = 0;
        QCOMPARE(outcomeText(plc), QStringLiteral("plcError C051"));
        Error proto;
        proto.category = ErrorCategory::Protocol;
        proto.code = ErrorCode::FrameMismatch;
        QCOMPARE(outcomeText(proto), QStringLiteral("protocolError FrameMismatch"));
        Error timeout;
        timeout.category = ErrorCategory::Transport;
        timeout.code = ErrorCode::Timeout;
        QCOMPARE(outcomeText(timeout), QStringLiteral("timeout"));
        Error refused;
        refused.category = ErrorCategory::Encode;
        refused.code = ErrorCode::PointCount;
        QCOMPARE(outcomeText(refused), QStringLiteral("notSent PointCount"));
        QCOMPARE(outcomeText(Error{}), QStringLiteral("ok"));

        Expect e;
        QCOMPARE(expectText(e, false), QStringLiteral("ok"));
        e.hasValues = true;
        e.values = {0x1995, 0x1202, 0xA};
        QCOMPARE(expectText(e, false), QStringLiteral("words 1995 1202 A"));
        e.values = {1, 0, 1};
        QCOMPARE(expectText(e, true), QStringLiteral("bits 1 0 1"));
        e.hasValues = false;
        e.hasBitsOn = true;
        e.bitsOn = {2, 4, 17};
        QCOMPARE(expectText(e, true), QStringLiteral("bitsOn 2 4 17"));
        e.hasBitsOn = false;
        e.frames = 2;
        e.valuesFrom = QStringLiteral("G1-03");
        QCOMPARE(expectText(e, false), QStringLiteral("ok valuesFrom G1-03 frames 2"));
        Expect other;
        other.kind = ExpectKind::NoResponse;
        QCOMPARE(expectText(other, false), QStringLiteral("noResponse"));
        other.kind = ExpectKind::Record;
        QCOMPARE(expectText(other, false), QStringLiteral("record"));
    }

    // ---- session.vec --------------------------------------------------------------------------

    void HIL_05_sessionVecRoundTripsChunksAndEvents() {
        SessionCapture cap;
        cap.stepId = QStringLiteral("G6");
        cap.chunks.push_back(SessionChunk{1000, 1, true, bytes({0x50, 0x00, 0x01})});
        cap.chunks.push_back(SessionChunk{2500, 2, false, bytes({0xD0, 0x00})});

        DeviceSnapshot snapshot;
        snapshot.type = DeviceType::D;
        snapshot.round = 1;
        SnapshotSegment a;
        a.head = Device{DeviceType::D, 100};
        a.count = 2;
        a.values = bytes({0x34, 0x12, 0x78, 0x56});
        a.states = bytes({2, 2});
        SnapshotSegment b;
        b.head = Device{DeviceType::D, 200};
        b.count = 1;
        b.values = bytes({0x01, 0x00});
        b.states = bytes({2});
        snapshot.segments = {a, b};
        ChunkStatus ok;
        ok.state = ChunkState::Ok;
        ChunkStatus failed;
        failed.state = ChunkState::Failed;
        snapshot.chunks = {ok, failed};
        cap.events.push_back(snapshotEvent(snapshot, 3000));
        cap.events.back().seq = 3;

        cap.events.push_back(changesEvent(
            DeviceType::M, 2,
            {Change{Device{DeviceType::M, 101}, 0, 1}, Change{Device{DeviceType::M, 105}, 1, 0}},
            4000));
        cap.events.back().seq = 4;
        CycleInfo cycle{};
        cycle.round = 3;
        cycle.startedAt = 1000;
        cycle.durationMs = 12;
        cycle.requests = 4;
        cycle.failedChunks = 1;
        cycle.heartbeatOk = true;
        cap.events.push_back(cycleEvent(cycle, 5000));
        cap.events.back().seq = 5;
        Error plcError;
        plcError.category = ErrorCategory::Plc;
        plcError.code = ErrorCode::PlcError;
        plcError.plcCode = 0xC051;
        cap.events.push_back(requestFinishedEvent(7, plcError, QByteArray(), 6000));
        cap.events.back().seq = 6;
        cap.events.push_back(requestFinishedEvent(8, Error{}, bytes({1, 2}), 6500));
        cap.events.back().seq = 7;
        cap.events.push_back(linkStateEvent(LinkState::Connected, LinkReason::Requested, 7000));
        cap.events.back().seq = 8;
        LinkFaultInfo fault;
        fault.kind = LinkFaultKind::Timeout;
        fault.error.category = ErrorCategory::Transport;
        fault.error.code = ErrorCode::Timeout;
        fault.reopenTransport = true;
        cap.events.push_back(linkFaultEvent(fault, 8000));
        cap.events.back().seq = 9;

        const QString dir = scratchDir(QStringLiteral("capture_session"));
        CaptureWriter writer(dir, QStringLiteral("p1"));
        QString error;
        QVERIFY2(writer.prepare(&error), qPrintable(error));
        QVERIFY2(writer.writeSession({cap}, &error), qPrintable(error));
        std::vector<mc::test::Vector> vectors;
        try {
            vectors =
                mc::test::loadVectors(toStd(writer.folder() + QStringLiteral("/session.vec")));
        } catch (const std::exception& ex) {
            QFAIL(ex.what());
        }
        QCOMPARE(static_cast<int>(vectors.size()), 9); // 2 chunks, then 7 events
        const auto key = [&](size_t i, const char* k) {
            return QString::fromStdString(vectors[i].field(k));
        };
        QCOMPARE(key(0, "id"), QStringLiteral("CAP-p1-G6-T0001"));
        QCOMPARE(key(0, "kind"), QStringLiteral("tx"));
        QCOMPARE(key(0, "t_ns"), QStringLiteral("1000"));
        QCOMPARE(key(0, "seq"), QStringLiteral("1"));
        QCOMPARE(key(0, "step"), QStringLiteral("G6"));
        QCOMPARE(toQ(vectors[0].bytes), bytes({0x50, 0x00, 0x01}));
        QCOMPARE(key(1, "kind"), QStringLiteral("rx"));
        QCOMPARE(key(1, "t_ns"), QStringLiteral("2500"));
        QCOMPARE(toQ(vectors[1].bytes), bytes({0xD0, 0x00}));

        QCOMPARE(key(2, "id"), QStringLiteral("CAP-p1-G6-E0001"));
        QCOMPARE(key(2, "kind"), QStringLiteral("event"));
        QCOMPARE(key(2, "event"), QStringLiteral("snapshot"));
        QCOMPARE(key(2, "type"), QStringLiteral("D"));
        QCOMPARE(key(2, "round"), QStringLiteral("1"));
        QCOMPARE(key(2, "segments"), QStringLiteral("D100x2 D200x1"));
        QCOMPARE(key(2, "chunks"), QStringLiteral("ok failed"));
        QCOMPARE(toQ(vectors[2].bytes),
                 bytes({0x01, 0x64, 0,    0, 0, 0x02, 0,    0, 0, 0x34, 0x12, 0x78, 0x56,
                        0x02, 0x02, 0xC8, 0, 0, 0,    0x01, 0, 0, 0,    0x01, 0x00, 0x02}));

        QCOMPARE(key(3, "event"), QStringLiteral("valuesChanged"));
        QCOMPARE(key(3, "type"), QStringLiteral("M"));
        QCOMPARE(key(3, "changes"), QStringLiteral("2"));
        QCOMPARE(toQ(vectors[3].bytes), bytes({0x02, 0x65, 0, 0, 0, 0x00, 0x00, 0x01, 0x00, 0x69, 0,
                                               0, 0, 0x01, 0x00, 0x00, 0x00}));

        QCOMPARE(key(4, "event"), QStringLiteral("cycleDone"));
        QCOMPARE(key(4, "round"), QStringLiteral("3"));
        QCOMPARE(key(4, "duration_ms"), QStringLiteral("12"));
        QCOMPARE(key(4, "requests"), QStringLiteral("4"));
        QCOMPARE(key(4, "failed_chunks"), QStringLiteral("1"));
        QCOMPARE(key(4, "heartbeat_ok"), QStringLiteral("true"));
        QCOMPARE(toQ(vectors[4].bytes), bytes({0x03, 3, 0,  0, 0, 0xE8, 0x03, 0, 0, 0, 0,
                                               0,    0, 12, 0, 0, 0,    4,    0, 1, 0, 1}));

        QCOMPARE(key(5, "event"), QStringLiteral("requestFinished"));
        QCOMPARE(key(5, "request_id"), QStringLiteral("7"));
        QCOMPARE(key(5, "outcome"), QStringLiteral("plcError C051"));
        QCOMPARE(toQ(vectors[5].bytes), bytes({0x04}));
        QCOMPARE(key(6, "outcome"), QStringLiteral("ok"));
        QCOMPARE(key(6, "bytes"), QStringLiteral("2"));
        QCOMPARE(toQ(vectors[6].bytes), bytes({0x04, 1, 2}));

        QCOMPARE(key(7, "event"), QStringLiteral("linkState"));
        QCOMPARE(key(7, "state"), QStringLiteral("Connected"));
        QCOMPARE(key(7, "reason"), QStringLiteral("Requested"));
        QCOMPARE(toQ(vectors[7].bytes), bytes({0x05, 2, 0}));
        QCOMPARE(key(8, "event"), QStringLiteral("linkFault"));
        QCOMPARE(key(8, "fault"), QStringLiteral("Timeout"));
        QCOMPARE(key(8, "error"), QStringLiteral("Timeout"));
        QCOMPARE(key(8, "reopen"), QStringLiteral("true"));
        QCOMPARE(toQ(vectors[8].bytes),
                 bytes({0x06, 0, static_cast<int>(ErrorCode::Timeout), 0, 0, 0, 1}));
    }

    // ---- run.meta -----------------------------------------------------------------------------

    void HIL_05_runMetaHoldsEveryFrameAndSessionField() {
        // The two lists are kept here, next to the test, independently of the writer: they are the
        // fields of mc::FrameConfig and mc::SessionConfig. A field the writer drops fails the test.
        const QStringList frameFields{"frame",
                                      "code",
                                      "network",
                                      "pc",
                                      "io",
                                      "station",
                                      "monitoringTimer",
                                      "series",
                                      "checkRoute",
                                      "serialStart",
                                      "format",
                                      "stationNo",
                                      "selfStation",
                                      "sumCheck",
                                      "blockNo",
                                      "checkBlockNo",
                                      "sendEotOnError",
                                      "f3ShortResponseHasSum",
                                      "messageWait",
                                      "commandSet",
                                      "e1AliasLS",
                                      "targetFamily",
                                      "highPerformanceQcpu",
                                      "aSeriesTarget",
                                      "splitWrites",
                                      "timeoutMs",
                                      "readRetries"};
        const QStringList sessionFields{
            "cycleIntervalMs",   "cycleMode",       "bitsAsWords",       "maxGap",
            "adHocCapacity",     "adHocArenaBytes", "maxAdHocBurst",     "maxConsecutiveLinkErrors",
            "serialInterCharMs", "serialFlushMs",   "heartbeat.enabled", "heartbeat.device"};

        // A profile whose every host, port and COM name is unmistakable.
        QJsonObject root = readJsonFile(exampleProfilePath(QStringLiteral("q03ude-c24-3c-f4")));
        QJsonObject device = root.value(QStringLiteral("device")).toObject();
        QJsonObject transport = device.value(QStringLiteral("transport")).toObject();
        QJsonObject tcp = transport.value(QStringLiteral("tcp")).toObject();
        tcp.insert(QStringLiteral("host"), QStringLiteral("192.0.2.77"));
        tcp.insert(QStringLiteral("port"), 40123);
        QJsonObject serial = transport.value(QStringLiteral("serial")).toObject();
        serial.insert(QStringLiteral("portName"), QStringLiteral("COM77"));
        serial.insert(QStringLiteral("baudRate"), 19200);
        transport.insert(QStringLiteral("tcp"), tcp);
        transport.insert(QStringLiteral("serial"), serial);
        device.insert(QStringLiteral("transport"), transport);
        QJsonObject frame = device.value(QStringLiteral("frame")).toObject();
        frame.insert(QStringLiteral("blockNo"), 90);
        frame.insert(QStringLiteral("timeoutMs"), 1234);
        device.insert(QStringLiteral("frame"), frame);
        QJsonObject session = device.value(QStringLiteral("session")).toObject();
        session.insert(QStringLiteral("cycleIntervalMs"), 33);
        session.insert(QStringLiteral("maxGap"), 7);
        device.insert(QStringLiteral("session"), session);
        root.insert(QStringLiteral("device"), device);
        const ProfileLoad load = loadProfile(root);
        QVERIFY2(load.ok(), qPrintable(load.error.text()));

        RunMeta meta;
        meta.profile = *load.profile;
        meta.gitCommit = QStringLiteral("0123abc");
        meta.date = QStringLiteral("2026-10-02T12:00:00Z");
        meta.operatorNote = QStringLiteral("virtual_plc fixture, not hardware");
        meta.plcState = QStringLiteral("STOP");
        meta.skipped.push_back(
            {QStringLiteral("G1-06"), QStringLiteral("skipped: requires supports:R")});
        meta.notSent.push_back({QStringLiteral("G3-03"), QStringLiteral("notSent InvalidDevice")});
        meta.extra.push_back({QStringLiteral("scan_time_raw"), QStringLiteral("12")});

        const QString dir = scratchDir(QStringLiteral("capture_meta"));
        CaptureWriter writer(dir, meta.profile.id);
        QString error;
        QVERIFY2(writer.prepare(&error), qPrintable(error));
        QVERIFY2(writer.writeRunMeta(meta, &error), qPrintable(error));
        const QString text = readAll(writer.folder() + QStringLiteral("/run.meta"));
        const QMap<QString, QString> map = parseMeta(text);

        for (const QString& f : frameFields) {
            QVERIFY2(map.contains(QStringLiteral("frame.") + f),
                     qPrintable(QStringLiteral("frame.") + f));
        }
        for (const QString& f : sessionFields) {
            QVERIFY2(map.contains(QStringLiteral("session.") + f),
                     qPrintable(QStringLiteral("session.") + f));
        }
        QCOMPARE(map.value(QStringLiteral("frame.frame")), QStringLiteral("3C"));
        QCOMPARE(map.value(QStringLiteral("frame.code")), QStringLiteral("Ascii"));
        QCOMPARE(map.value(QStringLiteral("frame.format")), QStringLiteral("Format4"));
        QCOMPARE(map.value(QStringLiteral("frame.checkRoute")), QStringLiteral("true"));
        QCOMPARE(map.value(QStringLiteral("frame.blockNo")), QStringLiteral("90"));
        QCOMPARE(map.value(QStringLiteral("frame.timeoutMs")), QStringLiteral("1234"));
        QCOMPARE(map.value(QStringLiteral("frame.targetFamily")), QStringLiteral("IqR_Q_L"));
        QCOMPARE(map.value(QStringLiteral("session.cycleIntervalMs")), QStringLiteral("33"));
        QCOMPARE(map.value(QStringLiteral("session.maxGap")), QStringLiteral("7"));
        QCOMPARE(map.value(QStringLiteral("session.cycleMode")), QStringLiteral("FixedRate"));
        QCOMPARE(map.value(QStringLiteral("session.heartbeat.enabled")), QStringLiteral("false"));

        // The remaining keys of the spec: versions, commit, date, note, state, identity, line,
        // scratch.
        QCOMPARE(map.value(QStringLiteral("tool")), QStringLiteral("hil_capture"));
        QCOMPARE(map.value(QStringLiteral("tool_version")), QStringLiteral(MC_VERSION_STRING));
        QCOMPARE(map.value(QStringLiteral("library_version")), QStringLiteral(MC_VERSION_STRING));
        QCOMPARE(map.value(QStringLiteral("git_commit")), QStringLiteral("0123abc"));
        QCOMPARE(map.value(QStringLiteral("date")), QStringLiteral("2026-10-02T12:00:00Z"));
        QCOMPARE(map.value(QStringLiteral("operator_note")),
                 QStringLiteral("virtual_plc fixture, not hardware"));
        QCOMPARE(map.value(QStringLiteral("plc_state")), QStringLiteral("STOP"));
        QCOMPARE(map.value(QStringLiteral("profile")), QStringLiteral("q03ude-c24-3c-f4"));
        QCOMPARE(map.value(QStringLiteral("plc")), QStringLiteral("Q03UDECPU"));
        QCOMPARE(map.value(QStringLiteral("module")), QStringLiteral("QJ71C24N"));
        QCOMPARE(map.value(QStringLiteral("adapter")),
                 QStringLiteral("FTDI USB-RS232, latency timer 1 ms"));
        QCOMPARE(map.value(QStringLiteral("plc_state_note")),
                 QStringLiteral("RUN, online change enabled"));
        QCOMPARE(map.value(QStringLiteral("transport")), QStringLiteral("serial"));
        QCOMPARE(map.value(QStringLiteral("serial.baudRate")), QStringLiteral("19200"));
        QCOMPARE(map.value(QStringLiteral("serial.dataBits")), QStringLiteral("7"));
        QCOMPARE(map.value(QStringLiteral("serial.parity")), QStringLiteral("Even"));
        QCOMPARE(map.value(QStringLiteral("serial.stopBits")), QStringLiteral("1"));
        QCOMPARE(map.value(QStringLiteral("serial.flowControl")), QStringLiteral("None"));
        QCOMPARE(map.value(QStringLiteral("scratch")),
                 QStringLiteral("D100-D2099 W100-W1FF R0-R99 M100-M2099 B100-B1FF Y20-Y2F"));
        QCOMPARE(map.value(QStringLiteral("device_end")),
                 QStringLiteral("X=1FFF Y=1FFF M=8191 B=1FFF D=12287 W=1FFF R=32767"));
        QVERIFY(map.value(QStringLiteral("supports")).startsWith(QStringLiteral("D W R ZR M")));
        QCOMPARE(map.value(QStringLiteral("skipped.G1-06")),
                 QStringLiteral("skipped: requires supports:R"));
        QCOMPARE(map.value(QStringLiteral("notsent.G3-03")),
                 QStringLiteral("notSent InvalidDevice"));
        QCOMPARE(map.value(QStringLiteral("scan_time_raw")), QStringLiteral("12"));

        // Never an address, a TCP port or a COM port name (tcp settings are not written at all).
        QVERIFY(!text.contains(QStringLiteral("192.0.2.77")));
        QVERIFY(!text.contains(QStringLiteral("40123")));
        QVERIFY(!text.contains(QStringLiteral("COM77")));
        QVERIFY(!text.contains(QStringLiteral("connectTimeoutMs")));

        // A TCP profile: no line settings at all, and still no host or port.
        QJsonObject tcpRoot = readJsonFile(exampleProfilePath(QStringLiteral("q03ude-eth-3e-bin")));
        QJsonObject tcpDevice = tcpRoot.value(QStringLiteral("device")).toObject();
        QJsonObject tcpTransport = tcpDevice.value(QStringLiteral("transport")).toObject();
        QJsonObject tcpSettings = tcpTransport.value(QStringLiteral("tcp")).toObject();
        tcpSettings.insert(QStringLiteral("host"), QStringLiteral("192.0.2.88"));
        tcpSettings.insert(QStringLiteral("port"), 40456);
        tcpTransport.insert(QStringLiteral("tcp"), tcpSettings);
        tcpDevice.insert(QStringLiteral("transport"), tcpTransport);
        tcpRoot.insert(QStringLiteral("device"), tcpDevice);
        RunMeta tcpMeta;
        tcpMeta.profile = *loadProfile(tcpRoot).profile;
        const QString tcpText = runMetaText(tcpMeta);
        QVERIFY(!tcpText.contains(QStringLiteral("192.0.2.88")));
        QVERIFY(!tcpText.contains(QStringLiteral("40456")));
        QVERIFY(!tcpText.contains(QStringLiteral("serial.")));
        QVERIFY(tcpText.contains(QStringLiteral("transport: tcp")));
    }

    void HIL_05_runMetaScrubsTheProfilesOwnAddressesFromEveryValue() {
        QJsonObject root = readJsonFile(exampleProfilePath(QStringLiteral("q03ude-eth-3e-bin")));
        QJsonObject profile = root.value(QStringLiteral("profile")).toObject();
        profile.insert(QStringLiteral("adapter"),
                       QStringLiteral("gateway 10.9.8.7:4567, spare on com77"));
        profile.insert(QStringLiteral("module"), QStringLiteral("QJ71E71 at 10.9.8.7"));
        profile.insert(QStringLiteral("firmware"), QStringLiteral("ratio 1:45678"));
        profile.insert(QStringLiteral("plcState"), QStringLiteral("RUN, port :4567 open"));
        root.insert(QStringLiteral("profile"), profile);
        QJsonObject device = root.value(QStringLiteral("device")).toObject();
        QJsonObject transport = device.value(QStringLiteral("transport")).toObject();
        QJsonObject tcp = transport.value(QStringLiteral("tcp")).toObject();
        tcp.insert(QStringLiteral("host"), QStringLiteral("10.9.8.7"));
        tcp.insert(QStringLiteral("port"), 4567);
        QJsonObject serial = transport.value(QStringLiteral("serial")).toObject();
        serial.insert(QStringLiteral("portName"), QStringLiteral("COM77"));
        transport.insert(QStringLiteral("tcp"), tcp);
        transport.insert(QStringLiteral("serial"), serial);
        device.insert(QStringLiteral("transport"), transport);
        root.insert(QStringLiteral("device"), device);
        const ProfileLoad load = loadProfile(root);
        QVERIFY2(load.ok(), qPrintable(load.error.text()));

        RunMeta meta;
        meta.profile = *load.profile;
        meta.gitCommit = QStringLiteral("0123abc");
        meta.date = QStringLiteral("2026-10-02T12:00:00Z");
        meta.operatorNote = QStringLiteral("run against 10.9.8.7:4567 on COM77");
        meta.skipped.push_back(
            {QStringLiteral("S-01"), QStringLiteral("skipped: no route to 10.9.8.7")});
        meta.notSent.push_back(
            {QStringLiteral("N-01"), QStringLiteral("link down: COM77: Access is denied")});
        meta.extra.push_back(
            {QStringLiteral("recovery.1"),
             QStringLiteral(
                 "after X-01: connect to 10.9.8.7:4567 refused (state 0); com77 NOT reconnected")});
        const QString dir = scratchDir(QStringLiteral("capture_scrub"));
        CaptureWriter writer(dir, meta.profile.id);
        QString error;
        QVERIFY2(writer.prepare(&error), qPrintable(error));
        QVERIFY2(writer.writeRunMeta(meta, &error), qPrintable(error));
        QString text = readAll(writer.folder() + QStringLiteral("/run.meta"));
        const QMap<QString, QString> map = parseMeta(text);

        // The host, ":port" and the COM name go, whichever field they were typed into and in
        // whatever case; "host:port" becomes one marker.
        QCOMPARE(map.value(QStringLiteral("adapter")),
                 QStringLiteral("gateway <redacted>, spare on <redacted>"));
        QCOMPARE(map.value(QStringLiteral("module")), QStringLiteral("QJ71E71 at <redacted>"));
        QCOMPARE(map.value(QStringLiteral("plc_state_note")),
                 QStringLiteral("RUN, port <redacted> open"));
        QCOMPARE(map.value(QStringLiteral("operator_note")),
                 QStringLiteral("run against <redacted> on <redacted>"));
        QCOMPARE(map.value(QStringLiteral("skipped.S-01")),
                 QStringLiteral("skipped: no route to <redacted>"));
        QCOMPARE(map.value(QStringLiteral("notsent.N-01")),
                 QStringLiteral("link down: <redacted>: Access is denied"));
        QCOMPARE(
            map.value(QStringLiteral("recovery.1")),
            QStringLiteral(
                "after X-01: connect to <redacted> refused (state 0); <redacted> NOT reconnected"));
        // ":4567" is the port; ":45678" is another number and stays.
        QCOMPARE(map.value(QStringLiteral("firmware")), QStringLiteral("ratio 1:45678"));
        for (const char* secret : {"10.9.8.7", ":4567 ", "COM77"}) {
            QVERIFY2(!text.contains(QLatin1String(secret), Qt::CaseInsensitive), secret);
        }

        // The stop-pass block is scrubbed as well.
        meta.plcState = QStringLiteral("STOP");
        meta.operatorNote = QStringLiteral("second pass, still 10.9.8.7 and COM77");
        QVERIFY2(writer.writeRunMeta(meta, &error, true), qPrintable(error));
        text = readAll(writer.folder() + QStringLiteral("/run.meta"));
        QVERIFY2(text.contains(QStringLiteral(
                     "stop_pass.operator_note: second pass, still <redacted> and <redacted>")),
                 qPrintable(text));
        QVERIFY(!text.contains(QStringLiteral("10.9.8.7")));
        QVERIFY(!text.contains(QStringLiteral("COM77"), Qt::CaseInsensitive));

        // scrubAddresses() on its own: an empty host or port name replaces nothing.
        Profile bare = *load.profile;
        bare.device.tcp.host.clear();
        bare.device.serial.portName.clear();
        QCOMPARE(scrubAddresses(QStringLiteral("COM77 and 10.9.8.7"), bare),
                 QStringLiteral("COM77 and 10.9.8.7"));
    }

    void HIL_06_benchCsvRowsHaveExactlyTheThirteenSpecColumns() {
        const QStringList spec{"profile", "plc_state", "step",       "op",  "device",
                               "count",   "req_bytes", "resp_bytes", "rep", "ttfb_ms",
                               "rx_ms",   "rtt_ms",    "scan_ms"};
        QCOMPARE(benchHeader().trimmed().split(QLatin1Char(',')), spec);
        BenchRow full;
        full.plcState = QStringLiteral("RUN");
        full.step = QStringLiteral("GB-02");
        full.op = QStringLiteral("ReadWords");
        full.device = QStringLiteral("D100");
        full.count = 64;
        full.reqBytes = 21;
        full.respBytes = 139;
        full.rep = 3;
        full.ttfbMs = 1.25;
        full.rxMs = 0.5;
        full.rttMs = 1.75;
        full.scanMs = QStringLiteral("12");
        BenchRow sparse; // no timings, no scan time: the empty columns are still there
        sparse.plcState = QStringLiteral("STOP");
        sparse.step = QStringLiteral("GB-10");
        sparse.op = QStringLiteral("PollRound");
        sparse.device = QStringLiteral("G6");
        const QStringList lines = benchText(QStringLiteral("p1"), {full, sparse})
                                      .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 3);
        QCOMPARE(lines[1].split(QLatin1Char(',')),
                 (QStringList{"p1", "RUN", "GB-02", "ReadWords", "D100", "64", "21", "139", "3",
                              "1.250", "0.500", "1.750", "12"}));
        const QStringList sparseCols = lines[2].split(QLatin1Char(','));
        QCOMPARE(sparseCols.size(), 13);
        QCOMPARE(sparseCols[9], QString());
        QCOMPARE(sparseCols[12], QString());
    }

    void HIL_06_theReportSkipsARowThatHasNotThirteenColumns() {
        const QString root = scratchDir(QStringLiteral("bench_short_row"));
        QDir().mkpath(root + QStringLiteral("/p1"));
        QFile f(root + QStringLiteral("/p1/bench.csv"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(
            (benchHeader() +
             QStringLiteral(
                 "p1,RUN,GB-01,ReadWords,D100,1,21,17,1,5.000,0.500,5.500,\n" // 13 columns
                 "p1,RUN,GB-01,ReadWords,D100,1,21,17,2,9.000,0.500,9.500\n"  // 12: scan_ms missing
                 "p1,RUN,GB-01,ReadWords,D100,1,21,17,3,1.000\n"))            // 10
                .toUtf8());
        f.close();
        QString error;
        const QString out = root + QStringLiteral("/BENCH.md");
        QVERIFY2(writeBenchReport(root, out, &error), qPrintable(error));
        const QString text = readAll(out);
        // Only the complete row counts: n=1 and 5.000, not three values.
        QVERIFY2(
            text.contains(QStringLiteral("| ttfb | 5.000 / 5.000 / 5.000 / 5.000 (n=1) | - |")),
            qPrintable(text));
    }

    // ---- HIL-06: the bench report
    // ------------------------------------------------------------------

    void HIL_06_seriesStatisticsAreNearestRank() {
        SeriesStats s = seriesStats({5, 1, 3, 2, 4}); // odd
        QCOMPARE(s.n, 5);
        QCOMPARE(s.min, 1.0);
        QCOMPARE(s.median, 3.0);
        QCOMPARE(s.p95, 5.0); // rank ceil(0.95 * 5) = 5
        QCOMPARE(s.max, 5.0);
        s = seriesStats({40, 10, 30, 20}); // even: median is the mean of 20 and 30
        QCOMPARE(s.median, 25.0);
        QCOMPARE(s.p95, 40.0); // rank ceil(3.8) = 4
        QVector<double> twenty;
        for (int i = 20; i >= 1; --i) {
            twenty.push_back(i);
        }
        s = seriesStats(twenty);
        QCOMPARE(s.median, 10.5);
        QCOMPARE(s.p95, 19.0); // rank ceil(19.0) = 19, not 20
        QVector<double> hundred;
        for (int i = 1; i <= 100; ++i) {
            hundred.push_back(i);
        }
        QCOMPARE(seriesStats(hundred).p95, 95.0);
        QCOMPARE(seriesStats({7.5}).p95, 7.5);
        QCOMPARE(seriesStats({}).n, 0);
    }

    void HIL_06_reportMatchesHandWorkedTables() {
        const QString root = scratchDir(QStringLiteral("bench_report"));
        const auto row = [](const QString& profile, const char* state, const char* step,
                            const char* count, int rep, double ttfb, const char* req,
                            const char* resp) {
            return QStringLiteral("%1,%2,%3,ReadWords,D100,%4,%5,%6,%7,%8,0.500,%9,\n")
                .arg(profile, QLatin1String(state), QLatin1String(step), QLatin1String(count),
                     QLatin1String(req), QLatin1String(resp))
                .arg(rep)
                .arg(ttfb, 0, 'f', 3)
                .arg(ttfb + 0.5, 0, 'f', 3);
        };
        QString p1 = benchHeader();
        const double run5[] = {5, 1, 3, 2, 4};
        for (int i = 0; i < 5; ++i) {
            p1 += row(QStringLiteral("p1"), "RUN", "GB-01", "1", i + 1, run5[i], "21", "17");
        }
        const double stop4[] = {10, 20, 30, 40};
        for (int i = 0; i < 4; ++i) {
            p1 += row(QStringLiteral("p1"), "STOP", "GB-01", "1", i + 1, stop4[i], "21", "17");
        }
        for (int i = 20; i >= 1; --i) {
            p1 += row(QStringLiteral("p1"), "RUN", "GB-02", "64", 21 - i, i, "21", "147");
        }
        QDir().mkpath(root + QStringLiteral("/p1"));
        QDir().mkpath(root + QStringLiteral("/p2"));
        QFile f1(root + QStringLiteral("/p1/bench.csv"));
        QVERIFY(f1.open(QIODevice::WriteOnly));
        f1.write(p1.toUtf8());
        f1.close();
        QFile f2(root + QStringLiteral("/p2/bench.csv"));
        QVERIFY(f2.open(QIODevice::WriteOnly));
        f2.write((benchHeader() + row(QStringLiteral("p2"), "RUN", "GB-01", "1", 1, 2.0, "9", "9"))
                     .toUtf8());
        f2.close();

        QString error;
        const QString out = root + QStringLiteral("/BENCH.md");
        QVERIFY2(writeBenchReport(root, out, &error), qPrintable(error));
        const QString text = readAll(out);
        QVERIFY(text.contains(QStringLiteral("nearest-rank")));
        QVERIFY(text.contains(QStringLiteral("FTDI latency timer")));
        QVERIFY(text.contains(QStringLiteral("## p1")));
        QVERIFY(text.contains(QStringLiteral("## p2"))); // one table per profile
        QVERIFY(text.indexOf(QStringLiteral("## p1")) < text.indexOf(QStringLiteral("## p2")));
        // GB-01: RUN odd count, STOP even count; bytes out / in; rtt is ttfb + 0.5.
        QVERIFY2(text.contains(QStringLiteral(
                     "| GB-01 | ReadWords D100 x1 | 21 / 17 | ttfb | 1.000 / 3.000 / 5.000 / 5.000 "
                     "(n=5) | 10.000 / 25.000 / 40.000 / 40.000 (n=4) |")),
                 qPrintable(text));
        QVERIFY(text.contains(QStringLiteral("| rtt | 1.500 / 3.500 / 5.500 / 5.500 (n=5) | 10.500 "
                                             "/ 25.500 / 40.500 / 40.500 (n=4) |")));
        QVERIFY(text.contains(QStringLiteral(
            "| rx | 0.500 / 0.500 / 0.500 / 0.500 (n=5) | 0.500 / 0.500 / 0.500 / 0.500 (n=4) |")));
        // GB-02: twenty values 1..20, no STOP pass.
        QVERIFY(text.contains(QStringLiteral("| GB-02 | ReadWords D100 x64 | 21 / 147 | ttfb | "
                                             "1.000 / 10.500 / 19.000 / 20.000 (n=20) | - |")));
        QVERIFY(text.contains(QStringLiteral("| GB-01 | ReadWords D100 x1 | 9 / 9 | ttfb | 2.000 / "
                                             "2.000 / 2.000 / 2.000 (n=1) | - |")));
        QVERIFY(!writeBenchReport(root + QStringLiteral("/nothing"), out, &error));
        QVERIFY(error.contains(QStringLiteral("no bench.csv")));
    }

    // ---- folder -------------------------------------------------------------------------------

    void HIL_05_prepareReplacesTheFolderAndKeepsDivergences() {
        const QString dir = scratchDir(QStringLiteral("capture_replace"));
        CaptureWriter writer(dir, QStringLiteral("p9"));
        QString error;
        QVERIFY2(writer.prepare(&error), qPrintable(error));
        QVERIFY2(writer.writeBenchHeader(&error), qPrintable(error));
        QFile stale(writer.folder() + QStringLiteral("/stale.txt"));
        QVERIFY(stale.open(QIODevice::WriteOnly));
        stale.write("old");
        stale.close();
        QDir(writer.folder()).mkpath(QStringLiteral("sub/deeper"));
        QFile owner(writer.folder() + QStringLiteral("/divergences.txt"));
        QVERIFY(owner.open(QIODevice::WriteOnly));
        owner.write("G8-Q1 F-001\n");
        owner.close();

        CaptureWriter again(dir, QStringLiteral("p9"));
        QVERIFY2(again.prepare(&error), qPrintable(error));
        QStringList names =
            QDir(again.folder()).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
        QCOMPARE(names,
                 (QStringList{"divergences.txt"})); // the owner's file stays, the rest is replaced
        QCOMPARE(readAll(again.folder() + QStringLiteral("/divergences.txt")),
                 QStringLiteral("G8-Q1 F-001\n"));

        QVERIFY2(again.writeBenchHeader(&error), qPrintable(error));
        QCOMPARE(readAll(again.folder() + QStringLiteral("/bench.csv")), benchHeader());
        QCOMPARE(
            benchHeader(),
            QStringLiteral(
                "profile,plc_state,step,op,device,count,req_bytes,resp_bytes,rep,ttfb_ms,rx_ms,"
                "rtt_ms,scan_ms\n"));

        // An id that is not a folder name is refused before anything is deleted.
        for (const char* bad : {"", "..", "../x", "a/b", "a\\b", ".hidden", "c:x"}) {
            CaptureWriter w(dir, QLatin1String(bad));
            QVERIFY2(!w.prepare(&error), bad);
            QVERIFY(error.contains(QStringLiteral("not a folder name")));
        }
        QVERIFY(QDir(dir).exists(QStringLiteral("p9")));
    }

    // ---- RecordingTransport -------------------------------------------------------------------

    void HIL_05_recordingTransportRecordsEveryChunkOnceWithMonotonicStamps() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        TcpSettings settings;
        settings.host = QStringLiteral("127.0.0.1");
        settings.port = server.serverPort();
        const auto clock = std::make_shared<RecordingClock>();
        RecordingTransport rec(std::make_unique<TcpTransport>(settings), clock);
        QSignalSpy opened(&rec, &Transport::opened);
        QSignalSpy ready(&rec, &Transport::readyRead);
        QSignalSpy lost(&rec, &Transport::lost);
        QSignalSpy raw(&rec, &RecordingTransport::rawReadyRead);

        QVERIFY(rec.state() == Transport::State::Closed);
        rec.open();
        QVERIFY(opened.wait(3000));
        QCOMPARE(opened.count(), 1);
        QVERIFY(rec.state() == Transport::State::Open);
        QCOMPARE(rec.describe(), QStringLiteral("127.0.0.1:%1").arg(server.serverPort()));
        QVERIFY(server.hasPendingConnections() || server.waitForNewConnection(3000));
        QTcpSocket* peer = server.nextPendingConnection();
        QVERIFY(peer != nullptr);

        // Tx: two writes, two chunks, and the peer gets the bytes.
        const QByteArray one("hello");
        const QByteArray two("world!");
        QVERIFY(rec.write(ByteView{reinterpret_cast<const uint8_t*>(one.constData()),
                                   static_cast<size_t>(one.size())}));
        QVERIFY(rec.write(ByteView{reinterpret_cast<const uint8_t*>(two.constData()),
                                   static_cast<size_t>(two.size())}));
        // The sending socket flushes from the event loop, so wait through the loop.
        QTRY_VERIFY_WITH_TIMEOUT(peer->bytesAvailable() >= one.size() + two.size(), 3000);
        QCOMPARE(peer->readAll(), one + two);

        // Rx: the consumer reads through the decorator.
        peer->write("ab");
        peer->flush();
        QVERIFY(ready.wait(3000));
        QByteArray received;
        uint8_t buffer[16];
        for (size_t n; (n = rec.read(MutableByteView{buffer, sizeof buffer})) != 0;) {
            received.append(reinterpret_cast<const char*>(buffer), static_cast<int>(n));
        }
        QCOMPARE(received, QByteArray("ab"));
        QCOMPARE(raw.count(), 0);

        // Raw mode: McDevice would not be told (readyRead stays quiet), the runner is.
        const int readyBefore = ready.count();
        rec.setRawMode(true);
        peer->write("xyz");
        peer->flush();
        QVERIFY(raw.wait(3000));
        QCOMPARE(ready.count(), readyBefore);
        QCOMPARE(rec.takeRaw(), QByteArray("xyz"));
        QCOMPARE(rec.takeRaw(), QByteArray());
        peer->write("q"); // left unread
        peer->flush();
        QVERIFY(raw.wait(3000));
        rec.setRawMode(false);
        QCOMPARE(rec.read(MutableByteView{buffer, sizeof buffer}),
                 size_t(0)); // dropped, but recorded below
        peer->write("cde");
        peer->flush();
        QVERIFY(ready.wait(3000));
        for (size_t n; (n = rec.read(MutableByteView{buffer, sizeof buffer})) != 0;) {
            received.append(reinterpret_cast<const char*>(buffer), static_cast<int>(n));
        }

        // The recording: every byte once, in both directions, in order, stamps never going back.
        QByteArray tx;
        QByteArray rx;
        int txChunks = 0;
        qint64 last = -1;
        for (const WireChunk& c : rec.chunks()) {
            QVERIFY(c.tNs >= last);
            last = c.tNs;
            QVERIFY(!c.bytes.isEmpty());
            if (c.dir == WireDirection::Tx) {
                tx += c.bytes;
                ++txChunks;
            } else {
                rx += c.bytes;
            }
        }
        QCOMPARE(tx, one + two);
        QCOMPARE(txChunks, 2);
        QCOMPARE(rx, QByteArray("abxyzqcde")); // including what raw mode took and dropped
        QVERIFY(rec.chunks().first().tNs >= 0);
        QVERIFY(clock->nowNs() >= last);
        QCOMPARE(rec.chunkCount(), static_cast<int>(rec.chunks().size()));

        // A write the transport refuses is not recorded; the peer closing is reported with its
        // cause.
        peer->disconnectFromHost();
        QVERIFY(lost.wait(3000));
        QCOMPARE(lost.count(), 1);
        QVERIFY(rec.lastLossWasPeerClose());
        QVERIFY(rec.state() == Transport::State::Closed);
        const int chunksAtLoss = rec.chunkCount();
        QVERIFY(!rec.write(ByteView{reinterpret_cast<const uint8_t*>(one.constData()),
                                    static_cast<size_t>(one.size())}));
        QCOMPARE(rec.chunkCount(), chunksAtLoss);
    }

    void HIL_05_recordingTransportPassesTheOtherSignalsThrough() {
        // openFailed: nothing listens on this port.
        quint16 closedPort = 0;
        {
            QTcpServer probe;
            QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
            closedPort = probe.serverPort();
        }
        TcpSettings settings;
        settings.host = QStringLiteral("127.0.0.1");
        settings.port = closedPort;
        settings.connectTimeoutMs = 300;
        RecordingTransport failing(std::make_unique<TcpTransport>(settings),
                                   std::make_shared<RecordingClock>());
        QSignalSpy failed(&failing, &Transport::openFailed);
        failing.open();
        QVERIFY(failed.wait(6000));
        QCOMPARE(failed.count(), 1);
        QVERIFY(!failed.first().first().toString().isEmpty());
        QCOMPARE(failing.chunkCount(), 0);

        // close() emits nothing, and leaves the transport closed.
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        settings.port = server.serverPort();
        RecordingTransport rec(std::make_unique<TcpTransport>(settings),
                               std::make_shared<RecordingClock>());
        QSignalSpy opened(&rec, &Transport::opened);
        QSignalSpy lost(&rec, &Transport::lost);
        QSignalSpy openFailed(&rec, &Transport::openFailed);
        rec.open();
        QVERIFY(opened.wait(3000));
        rec.close();
        QVERIFY(rec.state() == Transport::State::Closed);
        QTest::qWait(100);
        QCOMPARE(lost.count(), 0);
        QCOMPARE(openFailed.count(), 0);
        QVERIFY(!rec.lastLossWasPeerClose());
    }
};

} // namespace

QObject* makeCaptureSuite() { return new HilCaptureTests; }

} // namespace mc::hil::test

#include "tst_hil_capture.moc"
