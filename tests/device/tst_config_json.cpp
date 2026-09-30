// tst_config_json.cpp -- T-036: McDeviceConfig, its JSON mapping and validate()
// (SPEC-qt-device.md "mc_device_config.h"). Test ids: QDV_10 is the spec's JSON test (round trip,
// missing keys, wrong type with its path, schema); QDV_16 is the spec's invalid-config test, here
// at the validate() level (the path a device reports on connectToPlc() comes from it); CFG_nn are
// local to this task (key coverage, enumerators, log sink). No I/O, no event loop.
#include "mc/device/mc_device_config.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QtTest>

#include <cstddef>
#include <cstdint>

namespace {

// ---- the fields the JSON must cover ----------------------------------------------------------
//
// These lists mirror include/mc/core/frame_config.h (struct FrameConfig) and
// include/mc/core/session.h (struct SessionConfig, struct HeartbeatConfig, and PlanOptions from
// include/mc/core/poll_plan.h, whose two fields sit directly in "session"). When a field is
// added to one of those structs: add it here, give it a key in src/device/mc_device_config.cpp
// (toJson and fromJson), and extend nonDefaultConfig() and expectSame() below.
// CFG_01 fails when the JSON keys and these lists differ, and (best effort, on 64-bit builds)
// when one of the two structs changes size.

const QStringList kFrameKeys = {"frame",
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

// SessionConfig without `log` (never serialised) and with PlanOptions and HeartbeatConfig
// flattened as the spec's JSON shows: heartbeat is a nested object.
const QStringList kSessionKeys = {
    "cycleIntervalMs",   "cycleMode",       "bitsAsWords",   "maxGap",
    "adHocCapacity",     "adHocArenaBytes", "maxAdHocBurst", "maxConsecutiveLinkErrors",
    "serialInterCharMs", "serialFlushMs",   "heartbeat"};
const QStringList kHeartbeatKeys = {"enabled", "device"};
const QStringList kTcpKeys = {"host", "port", "connectTimeoutMs"};
const QStringList kSerialKeys = {"portName", "baudRate", "dataBits",
                                 "parity",   "stopBits", "flowControl"};

QStringList sortedKeys(const QJsonObject& o) {
    QStringList keys = o.keys();
    keys.sort();
    return keys;
}

QStringList sorted(QStringList list) {
    list.sort();
    return list;
}

// ---- helpers ---------------------------------------------------------------------------------

const mc::Device kHeartbeatDevice{mc::DeviceType::Y, 0x2A};

// A configuration whose every field differs from its default.
mc::McDeviceConfig nonDefaultConfig() {
    mc::McDeviceConfig c;
    c.frame.frame = mc::FrameType::F3C;
    c.frame.code = mc::DataCode::Ascii;
    c.frame.network = 3;
    c.frame.pc = 0x12;
    c.frame.io = 0x03E1;
    c.frame.station = 7;
    c.frame.monitoringTimer = 0x25;
    c.frame.series = mc::PlcSeries::IqR;
    c.frame.checkRoute = true;
    c.frame.serialStart = 9;
    c.frame.format = mc::SerialFormat::Format4;
    c.frame.stationNo = 5;
    c.frame.selfStation = 6;
    c.frame.sumCheck = false;
    c.frame.blockNo = 8;
    c.frame.checkBlockNo = false;
    c.frame.sendEotOnError = false;
    c.frame.f3ShortResponseHasSum = true;
    c.frame.messageWait = 4;
    c.frame.commandSet = mc::C1CommandSet::AnA;
    c.frame.e1AliasLS = true;
    c.frame.targetFamily = mc::TargetFamily::A;
    c.frame.highPerformanceQcpu = true;
    c.frame.aSeriesTarget = true;
    c.frame.splitWrites = true;
    c.frame.timeoutMs = 4321;
    c.frame.readRetries = 2;

    c.session.cycleIntervalMs = 250;
    c.session.cycleMode = mc::CycleMode::FixedDelay;
    c.session.plan.bitsAsWords = false;
    c.session.plan.maxGap = 7;
    c.session.adHocCapacity = 32;
    c.session.adHocArenaBytes = 4096;
    c.session.maxAdHocBurst = 2;
    c.session.maxConsecutiveLinkErrors = 5;
    c.session.serialInterCharMs = 200;
    c.session.serialFlushMs = 80;
    c.session.heartbeat.enabled = true;
    c.session.heartbeat.device = kHeartbeatDevice;

    c.transport = mc::TransportKind::Serial;
    c.tcp.host = QStringLiteral("10.0.0.7");
    c.tcp.port = 1281;
    c.tcp.connectTimeoutMs = 750;
    c.serial.portName = QStringLiteral("COM7");
    c.serial.baudRate = 19200;
    c.serial.dataBits = QSerialPort::Data8;
    c.serial.parity = QSerialPort::OddParity;
    c.serial.stopBits = QSerialPort::TwoStop;
    c.serial.flowControl = QSerialPort::HardwareControl;

    c.subscriptions = {
        {QStringLiteral("D2000"), 64}, {QStringLiteral("M2000"), 32}, {QStringLiteral("x1F"), 1}};
    return c;
}

// Every field, one QCOMPARE each, so a failure names the field.
void expectSameFrame(const mc::FrameConfig& a, const mc::FrameConfig& b) {
    QCOMPARE(a.frame, b.frame);
    QCOMPARE(a.code, b.code);
    QCOMPARE(a.network, b.network);
    QCOMPARE(a.pc, b.pc);
    QCOMPARE(a.io, b.io);
    QCOMPARE(a.station, b.station);
    QCOMPARE(a.monitoringTimer, b.monitoringTimer);
    QCOMPARE(a.series, b.series);
    QCOMPARE(a.checkRoute, b.checkRoute);
    QCOMPARE(a.serialStart, b.serialStart);
    QCOMPARE(a.format, b.format);
    QCOMPARE(a.stationNo, b.stationNo);
    QCOMPARE(a.selfStation, b.selfStation);
    QCOMPARE(a.sumCheck, b.sumCheck);
    QCOMPARE(a.blockNo, b.blockNo);
    QCOMPARE(a.checkBlockNo, b.checkBlockNo);
    QCOMPARE(a.sendEotOnError, b.sendEotOnError);
    QCOMPARE(a.f3ShortResponseHasSum, b.f3ShortResponseHasSum);
    QCOMPARE(a.messageWait, b.messageWait);
    QCOMPARE(a.commandSet, b.commandSet);
    QCOMPARE(a.e1AliasLS, b.e1AliasLS);
    QCOMPARE(a.targetFamily, b.targetFamily);
    QCOMPARE(a.highPerformanceQcpu, b.highPerformanceQcpu);
    QCOMPARE(a.aSeriesTarget, b.aSeriesTarget);
    QCOMPARE(a.splitWrites, b.splitWrites);
    QCOMPARE(a.timeoutMs, b.timeoutMs);
    QCOMPARE(a.readRetries, b.readRetries);
}

void expectSameSession(const mc::SessionConfig& a, const mc::SessionConfig& b) {
    QCOMPARE(a.cycleIntervalMs, b.cycleIntervalMs);
    QCOMPARE(a.cycleMode, b.cycleMode);
    QCOMPARE(a.plan.bitsAsWords, b.plan.bitsAsWords);
    QCOMPARE(a.plan.maxGap, b.plan.maxGap);
    QCOMPARE(a.adHocCapacity, b.adHocCapacity);
    QCOMPARE(a.adHocArenaBytes, b.adHocArenaBytes);
    QCOMPARE(a.maxAdHocBurst, b.maxAdHocBurst);
    QCOMPARE(a.maxConsecutiveLinkErrors, b.maxConsecutiveLinkErrors);
    QCOMPARE(a.serialInterCharMs, b.serialInterCharMs);
    QCOMPARE(a.serialFlushMs, b.serialFlushMs);
    QCOMPARE(a.heartbeat.enabled, b.heartbeat.enabled);
    QVERIFY(a.heartbeat.device == b.heartbeat.device);
    QVERIFY(a.log == b.log);
}

void expectSame(const mc::McDeviceConfig& a, const mc::McDeviceConfig& b) {
    expectSameFrame(a.frame, b.frame);
    expectSameSession(a.session, b.session);
    QCOMPARE(a.transport, b.transport);
    QCOMPARE(a.tcp.host, b.tcp.host);
    QCOMPARE(a.tcp.port, b.tcp.port);
    QCOMPARE(a.tcp.connectTimeoutMs, b.tcp.connectTimeoutMs);
    QCOMPARE(a.serial.portName, b.serial.portName);
    QCOMPARE(a.serial.baudRate, b.serial.baudRate);
    QCOMPARE(a.serial.dataBits, b.serial.dataBits);
    QCOMPARE(a.serial.parity, b.serial.parity);
    QCOMPARE(a.serial.stopBits, b.serial.stopBits);
    QCOMPARE(a.serial.flowControl, b.serial.flowControl);
    QCOMPARE(a.subscriptions.size(), b.subscriptions.size());
    for (int i = 0; i < a.subscriptions.size(); ++i) {
        QCOMPARE(a.subscriptions[i].device, b.subscriptions[i].device);
        QCOMPARE(a.subscriptions[i].count, b.subscriptions[i].count);
    }
}

QJsonObject parseJson(const char* text) {
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(text), &error);
    if (error.error != QJsonParseError::NoError) {
        qWarning("bad test JSON: %s", qPrintable(error.errorString()));
    }
    return doc.object();
}

// A default configuration as JSON, with one section replaced: `path` is "frame" or
// "transport.tcp" style (two levels at most), `patch` is merged into that object.
QJsonObject withPatch(const QString& path, const QJsonObject& patch) {
    QJsonObject root = mc::McDeviceConfig{}.toJson();
    const QStringList parts = path.split(QLatin1Char('.'));
    if (parts.size() == 1) {
        QJsonObject sub = root.value(parts[0]).toObject();
        for (auto it = patch.begin(); it != patch.end(); ++it) {
            sub.insert(it.key(), it.value());
        }
        root.insert(parts[0], sub);
    } else {
        QJsonObject top = root.value(parts[0]).toObject();
        QJsonObject sub = top.value(parts[1]).toObject();
        for (auto it = patch.begin(); it != patch.end(); ++it) {
            sub.insert(it.key(), it.value());
        }
        top.insert(parts[1], sub);
        root.insert(parts[0], top);
    }
    return root;
}

} // namespace

class TstConfigJson : public QObject {
    Q_OBJECT

  private slots:
    // ---- QDV-10 ----------------------------------------------------------------------------

    void QDV_10_roundTripKeepsEveryFieldOfEveryStruct() {
        const mc::McDeviceConfig original = nonDefaultConfig();
        // The fixture itself must differ from the defaults in every field, or the round trip
        // would prove nothing: compare against a default config field by field.
        const mc::McDeviceConfig defaults;
        QVERIFY(original.frame.frame != defaults.frame.frame);
        QVERIFY(original.session.cycleIntervalMs != defaults.session.cycleIntervalMs);
        QVERIFY(original.transport != defaults.transport);

        QString where = QStringLiteral("stale");
        const mc::Expected<mc::McDeviceConfig> back =
            mc::McDeviceConfig::fromJson(original.toJson(), &where);
        QVERIFY(back.hasValue());
        QVERIFY(where.isEmpty());
        expectSame(back.value(), original);
    }

    void QDV_10_roundTripSurvivesTextAndIsStable() {
        const mc::McDeviceConfig original = nonDefaultConfig();
        const QByteArray text = QJsonDocument(original.toJson()).toJson(QJsonDocument::Compact);
        const QJsonObject parsed = QJsonDocument::fromJson(text).object();
        const mc::Expected<mc::McDeviceConfig> back = mc::McDeviceConfig::fromJson(parsed);
        QVERIFY(back.hasValue());
        expectSame(back.value(), original);
        QCOMPARE(back.value().toJson(), original.toJson());
    }

    void QDV_10_defaultsRoundTripWithMaxGapAuto() {
        const mc::McDeviceConfig defaults;
        const QJsonObject json = defaults.toJson();
        QCOMPARE(json.value("schema").toInt(), 1);
        QCOMPARE(json.value("session").toObject().value("maxGap").toString(),
                 QStringLiteral("auto"));
        const mc::Expected<mc::McDeviceConfig> back = mc::McDeviceConfig::fromJson(json);
        QVERIFY(back.hasValue());
        expectSame(back.value(), defaults);
        QCOMPARE(back.value().session.plan.maxGap, mc::kAutoGap);
    }

    void QDV_10_maxGapAcceptsAutoOrANumber() {
        auto gapOf = [](const QJsonValue& v, mc::ErrorCode* code = nullptr,
                        QString* where = nullptr) -> uint32_t {
            QJsonObject patch;
            patch.insert("maxGap", v);
            const auto r = mc::McDeviceConfig::fromJson(withPatch("session", patch), where);
            if (!r) {
                if (code) {
                    *code = r.error().code;
                }
                return 0;
            }
            return r.value().session.plan.maxGap;
        };
        QCOMPARE(gapOf("auto"), mc::kAutoGap);
        QCOMPARE(gapOf(0), uint32_t{0});
        QCOMPARE(gapOf(12), uint32_t{12});

        QString where;
        mc::ErrorCode code = mc::ErrorCode::Ok;
        gapOf("wide", &code, &where);
        QCOMPARE(code, mc::ErrorCode::InvalidConfig);
        QCOMPARE(where, QStringLiteral("session.maxGap"));
        // The sentinel value itself has one spelling only: "auto".
        where.clear();
        gapOf(static_cast<double>(mc::kAutoGap), &code, &where);
        QCOMPARE(where, QStringLiteral("session.maxGap"));
    }

    void QDV_10_missingKeysTakeDefaults() {
        const mc::McDeviceConfig defaults;

        // An empty object is a complete default configuration.
        const auto empty = mc::McDeviceConfig::fromJson(QJsonObject{});
        QVERIFY(empty.hasValue());
        expectSame(empty.value(), defaults);

        // A partial section keeps the defaults of every key it does not mention.
        const auto partial = mc::McDeviceConfig::fromJson(parseJson(R"({
            "frame": { "network": 9 },
            "session": { "heartbeat": { "enabled": true } },
            "transport": { "tcp": { "host": "plc.local" } }
        })"));
        QVERIFY(partial.hasValue());
        mc::McDeviceConfig expected;
        expected.frame.network = 9;
        expected.session.heartbeat.enabled = true;
        expected.tcp.host = QStringLiteral("plc.local");
        expectSame(partial.value(), expected);
        QCOMPARE(partial.value().tcp.port, quint16{5000});
        QCOMPARE(partial.value().session.heartbeat.device.number, 2000u);
    }

    void QDV_10_missingSubscriptionKeysTakeDefaults() {
        const auto r = mc::McDeviceConfig::fromJson(
            parseJson(R"({"subscriptions": [ {"device": "D5"}, {"count": 7} ]})"));
        QVERIFY(r.hasValue());
        QCOMPARE(r.value().subscriptions.size(), 2);
        QCOMPARE(r.value().subscriptions[0].device, QStringLiteral("D5"));
        QCOMPARE(r.value().subscriptions[0].count, 1u);
        QVERIFY(r.value().subscriptions[1].device.isEmpty());
        QCOMPARE(r.value().subscriptions[1].count, 7u);
    }

    void QDV_10_unknownKeysAreIgnored() {
        const auto r = mc::McDeviceConfig::fromJson(parseJson(R"({
            "schema": 1, "future": {"a": 1},
            "frame": { "network": 4, "nope": true },
            "session": { "cycleIntervalMs": 20, "extra": [1, 2] },
            "transport": { "kind": "Tcp", "tcp": { "port": 6000, "keepAlive": 1 }, "rdma": {} },
            "subscriptions": [ { "device": "D1", "count": 2, "note": "x" } ]
        })"));
        QVERIFY(r.hasValue());
        QCOMPARE(r.value().frame.network, uint8_t{4});
        QCOMPARE(r.value().session.cycleIntervalMs, 20u);
        QCOMPARE(r.value().tcp.port, quint16{6000});
        QCOMPARE(r.value().subscriptions.size(), 1);
    }

    void QDV_10_wrongTypeOrValueFailsWithItsPath_data() {
        QTest::addColumn<QString>("section"); // where to patch ("" = the root object)
        QTest::addColumn<QString>("json");    // the patch object
        QTest::addColumn<QString>("path");    // the path fromJson must report

        // wrong JSON type
        QTest::newRow("frame not object") << "" << R"({"frame": 5})" << "frame";
        QTest::newRow("frame.timeoutMs string") << "frame" << R"({"timeoutMs": "fast"})"
                                                << "frame.timeoutMs";
        QTest::newRow("frame.checkRoute number") << "frame" << R"({"checkRoute": 1})"
                                                 << "frame.checkRoute";
        QTest::newRow("frame.frame number") << "frame" << R"({"frame": 3})" << "frame.frame";
        QTest::newRow("session not object") << "" << R"({"session": []})" << "session";
        QTest::newRow("session.bitsAsWords string") << "session" << R"({"bitsAsWords": "yes"})"
                                                    << "session.bitsAsWords";
        QTest::newRow("session.heartbeat not object") << "session" << R"({"heartbeat": true})"
                                                      << "session.heartbeat";
        QTest::newRow("session.heartbeat.enabled") << "session.heartbeat"
                                                   << R"({"enabled": "on"})"
                                                   << "session.heartbeat.enabled";
        QTest::newRow("transport not object") << "" << R"({"transport": "tcp"})" << "transport";
        QTest::newRow("transport.tcp.host number") << "transport.tcp" << R"({"host": 5})"
                                                   << "transport.tcp.host";
        QTest::newRow("transport.serial.portName") << "transport.serial"
                                                   << R"({"portName": []})"
                                                   << "transport.serial.portName";
        QTest::newRow("subscriptions not array") << "" << R"({"subscriptions": {}})"
                                                 << "subscriptions";
        QTest::newRow("subscriptions[1] not object")
            << "" << R"({"subscriptions": [{"device": "D1"}, 5]})" << "subscriptions[1]";
        QTest::newRow("subscriptions[2].count string")
            << "" << R"({"subscriptions": [{}, {}, {"count": "many"}]})"
            << "subscriptions[2].count";
        QTest::newRow("subscriptions[0].device number")
            << "" << R"({"subscriptions": [{"device": 5}]})" << "subscriptions[0].device";
        QTest::newRow("null is a wrong type") << "frame" << R"({"network": null})"
                                              << "frame.network";

        // right type, invalid value
        QTest::newRow("frame.frame unknown") << "frame" << R"({"frame": "5E"})" << "frame.frame";
        QTest::newRow("frame.code lower case") << "frame" << R"({"code": "binary"})"
                                               << "frame.code";
        QTest::newRow("frame.network 256") << "frame" << R"({"network": 256})" << "frame.network";
        QTest::newRow("frame.io negative") << "frame" << R"({"io": -1})" << "frame.io";
        QTest::newRow("frame.timeoutMs fraction") << "frame" << R"({"timeoutMs": 1.5})"
                                                  << "frame.timeoutMs";
        QTest::newRow("frame.timeoutMs 2^32") << "frame" << R"({"timeoutMs": 4294967296})"
                                              << "frame.timeoutMs";
        QTest::newRow("session.cycleMode") << "session" << R"({"cycleMode": "Sometimes"})"
                                           << "session.cycleMode";
        QTest::newRow("session.adHocCapacity 65536") << "session"
                                                     << R"({"adHocCapacity": 65536})"
                                                     << "session.adHocCapacity";
        QTest::newRow("heartbeat.device not a device") << "session.heartbeat"
                                                       << R"({"device": "Q10"})"
                                                       << "session.heartbeat.device";
        QTest::newRow("transport.kind") << "transport" << R"({"kind": "Udp"})" << "transport.kind";
        QTest::newRow("transport.tcp.port 70000") << "transport.tcp" << R"({"port": 70000})"
                                                  << "transport.tcp.port";
        QTest::newRow("serial.dataBits 9") << "transport.serial" << R"({"dataBits": 9})"
                                           << "transport.serial.dataBits";
        QTest::newRow("serial.dataBits 4") << "transport.serial" << R"({"dataBits": 4})"
                                           << "transport.serial.dataBits";
        QTest::newRow("serial.parity") << "transport.serial" << R"({"parity": "Maybe"})"
                                       << "transport.serial.parity";
        QTest::newRow("serial.stopBits 3") << "transport.serial" << R"({"stopBits": "3"})"
                                           << "transport.serial.stopBits";
        QTest::newRow("serial.flowControl") << "transport.serial" << R"({"flowControl": "XON"})"
                                            << "transport.serial.flowControl";
    }

    void QDV_10_wrongTypeOrValueFailsWithItsPath() {
        QFETCH(QString, section);
        QFETCH(QString, json);
        QFETCH(QString, path);

        const QJsonObject patch = QJsonDocument::fromJson(json.toUtf8()).object();
        QVERIFY2(!patch.isEmpty(), "test JSON did not parse");
        QJsonObject root;
        if (section.isEmpty()) {
            root = mc::McDeviceConfig{}.toJson();
            for (auto it = patch.begin(); it != patch.end(); ++it) {
                root.insert(it.key(), it.value());
            }
        } else {
            root = withPatch(section, patch);
        }

        QString where = QStringLiteral("stale");
        const auto r = mc::McDeviceConfig::fromJson(root, &where);
        QVERIFY(!r.hasValue());
        QCOMPARE(r.error().category, mc::ErrorCategory::Config);
        QCOMPARE(r.error().code, mc::ErrorCode::InvalidConfig);
        QVERIFY(r.error().message != nullptr && r.error().message[0] != '\0');
        QCOMPARE(where, path);

        // A null `where` is allowed.
        QVERIFY(!mc::McDeviceConfig::fromJson(root).hasValue());
    }

    void QDV_10_schemaMustBeOne() {
        QJsonObject root = mc::McDeviceConfig{}.toJson();

        root.insert("schema", 2);
        QString where;
        auto r = mc::McDeviceConfig::fromJson(root, &where);
        QVERIFY(!r.hasValue());
        QCOMPARE(r.error().code, mc::ErrorCode::InvalidConfig);
        QCOMPARE(where, QStringLiteral("schema"));

        root.insert("schema", 0);
        QVERIFY(!mc::McDeviceConfig::fromJson(root).hasValue());
        root.insert("schema", QStringLiteral("1"));
        r = mc::McDeviceConfig::fromJson(root, &where);
        QVERIFY(!r.hasValue());
        QCOMPARE(where, QStringLiteral("schema"));
        root.insert("schema", 1.5);
        QVERIFY(!mc::McDeviceConfig::fromJson(root).hasValue());

        root.insert("schema", 1);
        QVERIFY(mc::McDeviceConfig::fromJson(root, &where).hasValue());
        QVERIFY(where.isEmpty());

        // A missing schema counts as 1 (the defaults of a missing key).
        root.remove("schema");
        QVERIFY(mc::McDeviceConfig::fromJson(root).hasValue());
    }

    void QDV_10_theFirstFailureIsReported() {
        // Two bad values: the one met first, in the order schema, frame, session, transport,
        // subscriptions, is the one named.
        QJsonObject root = mc::McDeviceConfig{}.toJson();
        QJsonObject transport = root.value("transport").toObject();
        transport.insert("kind", "Udp");
        root.insert("transport", transport);
        root.insert("subscriptions", QJsonArray{5});
        QString where;
        QVERIFY(!mc::McDeviceConfig::fromJson(root, &where).hasValue());
        QCOMPARE(where, QStringLiteral("transport.kind"));
    }

    // ---- CFG: keys, enumerators, log sink --------------------------------------------------

    void CFG_01_everyFieldHasAKeyAndNoKeyIsInventedWithoutOne() {
        const QJsonObject json = mc::McDeviceConfig{}.toJson();
        QCOMPARE(sortedKeys(json.value("frame").toObject()), sorted(kFrameKeys));
        QCOMPARE(sortedKeys(json.value("session").toObject()), sorted(kSessionKeys));
        QCOMPARE(sortedKeys(json.value("session").toObject().value("heartbeat").toObject()),
                 sorted(kHeartbeatKeys));
        QCOMPARE(sortedKeys(json.value("transport").toObject().value("tcp").toObject()),
                 sorted(kTcpKeys));
        QCOMPARE(sortedKeys(json.value("transport").toObject().value("serial").toObject()),
                 sorted(kSerialKeys));
        QCOMPARE(sortedKeys(json),
                 sorted({"schema", "frame", "session", "transport", "subscriptions"}));
        QCOMPARE(sortedKeys(json.value("transport").toObject()), sorted({"kind", "tcp", "serial"}));

        // A struct that grew without this file being updated changes size (best effort; see the
        // comment above the lists).
        QCOMPARE(kFrameKeys.size(), 27);
#if defined(_WIN64) || defined(__x86_64__)
        // 64-bit MSVC and MinGW GCC 13 agree on these sizes (checked on both).
        QCOMPARE(sizeof(mc::FrameConfig), size_t{40});
        QCOMPARE(sizeof(mc::SessionConfig), size_t{56});
#endif
    }

    void CFG_02_everyEnumeratorSurvivesTheRoundTrip() {
        auto roundTrip = [](const mc::McDeviceConfig& c) {
            const auto r = mc::McDeviceConfig::fromJson(c.toJson());
            QVERIFY(r.hasValue());
            expectSame(r.value(), c);
        };

        for (auto v : {mc::FrameType::F3E, mc::FrameType::F1E, mc::FrameType::F3C,
                       mc::FrameType::F1C, mc::FrameType::F4E, mc::FrameType::F4C}) {
            mc::McDeviceConfig c;
            c.frame.frame = v;
            roundTrip(c);
        }
        for (auto v : {mc::DataCode::Binary, mc::DataCode::Ascii}) {
            mc::McDeviceConfig c;
            c.frame.code = v;
            roundTrip(c);
        }
        for (auto v :
             {mc::SerialFormat::Format1, mc::SerialFormat::Format2, mc::SerialFormat::Format3,
              mc::SerialFormat::Format4, mc::SerialFormat::Format5}) {
            mc::McDeviceConfig c;
            c.frame.format = v;
            roundTrip(c);
        }
        for (auto v : {mc::PlcSeries::QL, mc::PlcSeries::IqR}) {
            mc::McDeviceConfig c;
            c.frame.series = v;
            roundTrip(c);
        }
        for (auto v : {mc::TargetFamily::IqR_Q_L, mc::TargetFamily::QnA, mc::TargetFamily::A}) {
            mc::McDeviceConfig c;
            c.frame.targetFamily = v;
            roundTrip(c);
        }
        for (auto v : {mc::C1CommandSet::ACPU, mc::C1CommandSet::AnA}) {
            mc::McDeviceConfig c;
            c.frame.commandSet = v;
            roundTrip(c);
        }
        for (auto v : {mc::CycleMode::FixedRate, mc::CycleMode::FixedDelay}) {
            mc::McDeviceConfig c;
            c.session.cycleMode = v;
            roundTrip(c);
        }
        for (auto v : {mc::TransportKind::Tcp, mc::TransportKind::Serial}) {
            mc::McDeviceConfig c;
            c.transport = v;
            roundTrip(c);
        }
        for (auto v : {QSerialPort::NoParity, QSerialPort::EvenParity, QSerialPort::OddParity,
                       QSerialPort::SpaceParity, QSerialPort::MarkParity}) {
            mc::McDeviceConfig c;
            c.serial.parity = v;
            roundTrip(c);
        }
        for (auto v : {QSerialPort::OneStop, QSerialPort::OneAndHalfStop, QSerialPort::TwoStop}) {
            mc::McDeviceConfig c;
            c.serial.stopBits = v;
            roundTrip(c);
        }
        for (auto v : {QSerialPort::NoFlowControl, QSerialPort::HardwareControl,
                       QSerialPort::SoftwareControl}) {
            mc::McDeviceConfig c;
            c.serial.flowControl = v;
            roundTrip(c);
        }
        for (auto v :
             {QSerialPort::Data5, QSerialPort::Data6, QSerialPort::Data7, QSerialPort::Data8}) {
            mc::McDeviceConfig c;
            c.serial.dataBits = v;
            roundTrip(c);
        }
    }

    void CFG_03_enumeratorsAreWrittenAsTheSpecsStrings() {
        mc::McDeviceConfig c;
        c.frame.frame = mc::FrameType::F3C;
        c.serial.parity = QSerialPort::EvenParity;
        c.serial.stopBits = QSerialPort::OneStop;
        c.serial.flowControl = QSerialPort::NoFlowControl;
        c.serial.dataBits = QSerialPort::Data7;
        c.transport = mc::TransportKind::Serial;
        const QJsonObject json = c.toJson();
        QCOMPARE(json.value("frame").toObject().value("frame").toString(), QStringLiteral("3C"));
        QCOMPARE(json.value("frame").toObject().value("code").toString(), QStringLiteral("Binary"));
        const QJsonObject transport = json.value("transport").toObject();
        QCOMPARE(transport.value("kind").toString(), QStringLiteral("Serial"));
        const QJsonObject serial = transport.value("serial").toObject();
        QCOMPARE(serial.value("parity").toString(), QStringLiteral("Even"));
        QCOMPARE(serial.value("stopBits").toString(), QStringLiteral("1"));
        QCOMPARE(serial.value("flowControl").toString(), QStringLiteral("None"));
        QCOMPARE(serial.value("dataBits").toInt(), 7);
        QCOMPARE(serial.value("baudRate").toInt(), 9600);
        QCOMPARE(json.value("session")
                     .toObject()
                     .value("heartbeat")
                     .toObject()
                     .value("device")
                     .toString(),
                 QStringLiteral("M2000"));
    }

    void CFG_04_theSessionLogSinkIsNotSerialisedAndStaysNull() {
        mc::NullLogSink sink;
        mc::McDeviceConfig c = nonDefaultConfig();
        c.session.log = &sink;
        const QJsonObject json = c.toJson();
        QVERIFY(!json.value("session").toObject().contains("log"));
        const auto back = mc::McDeviceConfig::fromJson(json);
        QVERIFY(back.hasValue());
        QVERIFY(back.value().session.log == nullptr);
    }

    void CFG_05_theDefaultConfigIsTheDocumentedDefaults() {
        const mc::McDeviceConfig c;
        QVERIFY(c.frame.frame == mc::FrameType::F3E);
        QVERIFY(c.frame.code == mc::DataCode::Binary);
        QVERIFY(c.transport == mc::TransportKind::Tcp);
        QCOMPARE(c.tcp.host, QStringLiteral("192.168.0.1"));
        QCOMPARE(c.tcp.port, quint16{5000});
        QCOMPARE(c.tcp.connectTimeoutMs, 2000);
        QVERIFY(c.serial.portName.isEmpty());
        QCOMPARE(c.serial.baudRate, qint32{9600});
        QVERIFY(c.serial.dataBits == QSerialPort::Data7);
        QVERIFY(c.serial.parity == QSerialPort::EvenParity);
        QVERIFY(c.serial.stopBits == QSerialPort::OneStop);
        QVERIFY(c.serial.flowControl == QSerialPort::NoFlowControl);
        QVERIFY(c.subscriptions.isEmpty());
    }

    // ---- QDV-16 (validate level) -----------------------------------------------------------

    void QDV_16_aDefaultConfigIsValidAndClearsWhere() {
        QString where = QStringLiteral("stale");
        const mc::Expected<void> r = mc::McDeviceConfig{}.validate(&where);
        QVERIFY(r.hasValue());
        QVERIFY(where.isEmpty());
        QVERIFY(mc::McDeviceConfig{}.validate().hasValue()); // null `where` is allowed
    }

    void QDV_16_aCompleteValidConfigPasses() {
        mc::McDeviceConfig c;
        c.tcp.host = QStringLiteral("192.168.0.10");
        c.subscriptions = {{QStringLiteral("D2000"), 64},
                           {QStringLiteral("M2000"), 64},
                           {QStringLiteral("X1F"), 16}};
        c.session.heartbeat.enabled = true; // M2000, a bit device on 3E
        QString where;
        const auto r = c.validate(&where);
        QVERIFY2(r.hasValue(), qPrintable(where));
    }

    void QDV_16_anInvalidSubscriptionNamesItsDevicePath() {
        mc::McDeviceConfig c;
        c.subscriptions = {
            {QStringLiteral("D0"), 1}, {QStringLiteral("D10"), 1}, {QStringLiteral("Q10"), 4}};
        QString where;
        const auto r = c.validate(&where);
        QVERIFY(!r.hasValue());
        QCOMPARE(r.error().code, mc::ErrorCode::InvalidDevice);
        QCOMPARE(where, QStringLiteral("subscriptions[2].device"));

        c.subscriptions = {{QStringLiteral("Q10"), 1}};
        QVERIFY(!c.validate(&where).hasValue());
        QCOMPARE(where, QStringLiteral("subscriptions[0].device"));
    }

    void QDV_16_anEmptyOrUnparsableDeviceTextIsInvalid() {
        for (const QString& text : {QString(), QStringLiteral("D"), QStringLiteral("D 1"),
                                    QStringLiteral("D1x"), QStringLiteral("Ä100")}) {
            mc::McDeviceConfig c;
            c.subscriptions = {{text, 1}};
            QString where;
            const auto r = c.validate(&where);
            QVERIFY2(!r.hasValue(), qPrintable(text));
            QCOMPARE(r.error().code, mc::ErrorCode::InvalidDevice);
            QCOMPARE(where, QStringLiteral("subscriptions[0].device"));
        }
    }

    void QDV_16_aDeviceBeyondTheFrameFieldWidthIsInvalid() {
        mc::McDeviceConfig c; // 3E binary: device numbers are 3 bytes wide
        c.subscriptions = {{QStringLiteral("D16777216"), 1}};
        QString where;
        const auto r = c.validate(&where);
        QVERIFY(!r.hasValue());
        QCOMPARE(r.error().code, mc::ErrorCode::InvalidDevice);
        QCOMPARE(where, QStringLiteral("subscriptions[0].device"));
    }

    void QDV_16_aZeroCountNamesItsCountPath() {
        mc::McDeviceConfig c;
        c.subscriptions = {{QStringLiteral("D0"), 1}, {QStringLiteral("D5"), 0}};
        QString where;
        const auto r = c.validate(&where);
        QVERIFY(!r.hasValue());
        QCOMPARE(r.error().code, mc::ErrorCode::PointCount);
        QCOMPARE(where, QStringLiteral("subscriptions[1].count"));
    }

    void QDV_16_aTcpConfigNeedsAHostAndAPort() {
        mc::McDeviceConfig c;
        c.tcp.host.clear();
        QString where;
        auto r = c.validate(&where);
        QVERIFY(!r.hasValue());
        QCOMPARE(r.error().category, mc::ErrorCategory::Config);
        QCOMPARE(r.error().code, mc::ErrorCode::InvalidConfig);
        QCOMPARE(where, QStringLiteral("transport.tcp.host"));

        c.tcp.host = QStringLiteral("plc");
        c.tcp.port = 0;
        r = c.validate(&where);
        QVERIFY(!r.hasValue());
        QCOMPARE(r.error().code, mc::ErrorCode::InvalidConfig);
        QCOMPARE(where, QStringLiteral("transport.tcp.port"));
    }

    void QDV_16_aSerialConfigNeedsAPortNameAndABaudRate() {
        mc::McDeviceConfig c;
        c.frame = mc::FrameConfig::frame3C();
        c.transport = mc::TransportKind::Serial;
        QString where;
        auto r = c.validate(&where); // portName is empty by default
        QVERIFY(!r.hasValue());
        QCOMPARE(r.error().category, mc::ErrorCategory::Config);
        QCOMPARE(r.error().code, mc::ErrorCode::InvalidConfig);
        QCOMPARE(where, QStringLiteral("transport.serial.portName"));

        c.serial.portName = QStringLiteral("COM3");
        QVERIFY2(c.validate(&where).hasValue(), qPrintable(where));

        c.serial.baudRate = 0;
        r = c.validate(&where);
        QVERIFY(!r.hasValue());
        QCOMPARE(where, QStringLiteral("transport.serial.baudRate"));
        c.serial.baudRate = -9600;
        QVERIFY(!c.validate(&where).hasValue());
        QCOMPARE(where, QStringLiteral("transport.serial.baudRate"));
    }

    void QDV_16_onlyTheSelectedTransportIsChecked() {
        mc::McDeviceConfig tcp;
        tcp.serial.baudRate = 0; // unusable, but not selected
        QVERIFY(tcp.validate().hasValue());

        mc::McDeviceConfig serial;
        serial.frame = mc::FrameConfig::frame3C();
        serial.transport = mc::TransportKind::Serial;
        serial.serial.portName = QStringLiteral("COM3");
        serial.tcp.host.clear(); // unusable, but not selected
        serial.tcp.port = 0;
        QVERIFY(serial.validate().hasValue());
    }

    void QDV_16_anInvalidFrameNamesFrame() {
        mc::McDeviceConfig c;
        c.frame.frame = mc::FrameType::F4E; // reserved for v2
        QString where;
        const auto r = c.validate(&where);
        QVERIFY(!r.hasValue());
        QCOMPARE(r.error().code, mc::ErrorCode::InvalidConfig);
        QCOMPARE(where, QStringLiteral("frame"));
    }

    void QDV_16_anInvalidSessionNamesTheHeartbeatDevice() {
        mc::McDeviceConfig c;
        c.session.heartbeat.enabled = true;
        c.session.heartbeat.device = mc::Device{mc::DeviceType::D, 100}; // not a bit device
        QString where;
        const auto r = c.validate(&where);
        QVERIFY(!r.hasValue());
        QCOMPARE(r.error().code, mc::ErrorCode::InvalidConfig);
        QCOMPARE(where, QStringLiteral("session.heartbeat.device"));

        // A disabled heartbeat is not checked.
        c.session.heartbeat.enabled = false;
        QVERIFY(c.validate().hasValue());
    }

    void QDV_16_theFirstFailureWinsInTheDocumentedOrder() {
        mc::McDeviceConfig c;
        c.frame.frame = mc::FrameType::F4C; // 1. frame
        c.session.heartbeat.enabled = true; // 2. session
        c.session.heartbeat.device = mc::Device{mc::DeviceType::D, 1};
        c.subscriptions = {{QStringLiteral("Q10"), 1}}; // 3. subscriptions
        c.tcp.host.clear();                             // 4. transport
        QString where;
        QVERIFY(!c.validate(&where).hasValue());
        QCOMPARE(where, QStringLiteral("frame"));

        c.frame = mc::FrameConfig::frame3E();
        QVERIFY(!c.validate(&where).hasValue());
        QCOMPARE(where, QStringLiteral("session.heartbeat.device"));

        c.session.heartbeat.enabled = false;
        QVERIFY(!c.validate(&where).hasValue());
        QCOMPARE(where, QStringLiteral("subscriptions[0].device"));

        c.subscriptions.clear();
        QVERIFY(!c.validate(&where).hasValue());
        QCOMPARE(where, QStringLiteral("transport.tcp.host"));

        c.tcp.host = QStringLiteral("plc");
        QVERIFY(c.validate(&where).hasValue());
        QVERIFY(where.isEmpty());
    }

    void QDV_16_aConfigReadFromJsonIsValidatedSeparately() {
        // fromJson() accepts a well-typed document that validate() then rejects: the subscription
        // text is only checked by validate().
        const auto r = mc::McDeviceConfig::fromJson(
            parseJson(R"({"subscriptions": [ {"device": "Q10", "count": 1} ]})"));
        QVERIFY(r.hasValue());
        QString where;
        QVERIFY(!r.value().validate(&where).hasValue());
        QCOMPARE(where, QStringLiteral("subscriptions[0].device"));
    }
};

QTEST_GUILESS_MAIN(TstConfigJson)

#include "tst_config_json.moc"
