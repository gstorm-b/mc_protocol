// GUI-06 (SPEC-gui-tool.md "Testing") and the device tab (task T-069): the qpb grid bound to
// McDeviceConfig, and a device tab driven offscreen against a MockRunner.
//
// GUI-06: every field of McDeviceConfig has a property, every field round-trips through the grid,
// every field is editable, and a value the library refuses is rejected with the library's own
// validate() message and not applied. The tab cases connect a DeviceTab (its McDevice lives on a
// runner thread) to a MockHost, subscribe, watch values and a trend sample, and run an ad-hoc
// write with a read-back; a console, trend and pane check complete the picture.
#include "gui_suites.h"

#include "mc/device/meta_types.h"
#include "mc_workbench/config_binding.h"
#include "mc_workbench/console_model.h"
#include "mc_workbench/device_host.h"
#include "mc_workbench/device_tab.h"
#include "mc_workbench/main_window.h"
#include "mc_workbench/mock_host.h"
#include "mc_workbench/point_table_model.h"
#include "mc_workbench/qt_compat.h"
#include "mc_workbench/runner_types.h"
#include "mc_workbench/trend_widget.h"

#include <qpb/Property.h>
#include <qpb/PropertyModel.h>

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QPixmap>
#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <QThread>

#include <algorithm>

using namespace mc::workbench;

namespace {

constexpr int kWaitMs = 8000;

QJsonValue jsonAt(const QJsonObject& root, const QString& path) {
    QJsonValue current(root);
    for (const QString& part : path.split(QLatin1Char('/'))) {
        current = current.toObject().value(part);
    }
    return current;
}

// Leaves of toJson(): every key that is not an object, except the schema marker.
void collectLeaves(const QJsonObject& object, const QString& prefix, QSet<QString>& out) {
    for (auto it = object.begin(); it != object.end(); ++it) {
        const QString path = prefix.isEmpty() ? it.key() : prefix + QLatin1Char('/') + it.key();
        if (it.value().isObject()) {
            collectLeaves(it.value().toObject(), path, out);
        } else if (path != QLatin1String("schema")) {
            out.insert(path);
        }
    }
}

QString jsonText(const QJsonValue& v) {
    if (v.isBool()) {
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (v.isDouble()) {
        return QString::number(jsonInteger(v));
    }
    return v.toString();
}

mc::McDeviceConfig baseConfig() {
    mc::McDeviceConfig cfg;
    cfg.serial.portName = QStringLiteral("COM3"); // so that switching to serial is valid
    return cfg;
}

mc::McDeviceConfig tcpConfig(quint16 port) {
    mc::McDeviceConfig cfg;
    cfg.session.cycleIntervalMs = 20;
    cfg.tcp.host = QStringLiteral("127.0.0.1");
    cfg.tcp.port = port;
    cfg.tcp.connectTimeoutMs = 500;
    cfg.subscriptions = {{QStringLiteral("D100"), 4}, {QStringLiteral("M0"), 16}};
    return cfg;
}

std::vector<mc::McDeviceConfig> sampleConfigs() {
    std::vector<mc::McDeviceConfig> list;
    list.push_back(mc::McDeviceConfig{});

    mc::McDeviceConfig a = tcpConfig(5007);
    a.frame = mc::FrameConfig::frame3E(mc::DataCode::Ascii);
    a.frame.network = 1;
    a.frame.pc = 2;
    a.frame.io = 0x3E0;
    a.frame.station = 3;
    a.frame.monitoringTimer = 8;
    a.frame.series = mc::PlcSeries::IqR;
    a.frame.checkRoute = true;
    a.frame.targetFamily = mc::TargetFamily::QnA;
    a.frame.aSeriesTarget = true;
    a.frame.timeoutMs = 1234;
    a.session.cycleMode = mc::CycleMode::FixedDelay;
    a.session.plan.bitsAsWords = true;
    a.session.plan.maxGap = 5;
    a.session.heartbeat.enabled = true;
    a.session.heartbeat.device = {mc::DeviceType::M, 3000};
    list.push_back(a);

    mc::McDeviceConfig fx = tcpConfig(5008);
    fx.frame.xyNotation = mc::XyNumbering::Octal;
    fx.frame.xyAsciiDigits = mc::XyNumbering::Octal;
    fx.subscriptions = {{QStringLiteral("X10"), 8}, {QStringLiteral("Y17"), 2}};
    list.push_back(fx);

    mc::McDeviceConfig e = mc::McDeviceConfig{};
    e.frame = mc::FrameConfig::frame1E();
    e.frame.e1AliasLS = true;
    list.push_back(e);

    mc::McDeviceConfig c = mc::McDeviceConfig{};
    c.frame = mc::FrameConfig::frame3C(mc::SerialFormat::Format2);
    c.frame.stationNo = 2;
    c.frame.selfStation = 1;
    c.frame.blockNo = 7;
    c.frame.sumCheck = false;
    c.transport = mc::TransportKind::Serial;
    c.serial.portName = QStringLiteral("COM7");
    c.serial.baudRate = 19200;
    c.serial.dataBits = QSerialPort::Data8;
    c.serial.parity = QSerialPort::NoParity;
    c.serial.stopBits = QSerialPort::TwoStop;
    c.serial.flowControl = QSerialPort::HardwareControl;
    c.session.maxConsecutiveLinkErrors = 5;
    list.push_back(c);

    mc::McDeviceConfig one = mc::McDeviceConfig{};
    one.frame = mc::FrameConfig::frame1C(mc::SerialFormat::Format4);
    one.frame.commandSet = mc::C1CommandSet::AnA;
    one.frame.messageWait = 5;
    one.transport = mc::TransportKind::Serial;
    one.serial.portName = QStringLiteral("COM8");
    list.push_back(one);
    return list;
}

// A mock listening on a system-chosen port with D100..D103 = 10..40 and M3 = 1.
struct MockRig {
    MockRig() : host(QStringLiteral("t069-mock"), mc::FrameConfig::frame3E()), done(&host, &MockHost::commandDone) {
        host.setWords(QStringLiteral("D100"), {10, 20, 30, 40});
        host.setBits(QStringLiteral("M0"), {false, false, false, true});
        const quint64 token = host.listen(0);
        (void)QTest::qWaitFor(
            [&]() {
                for (const QList<QVariant>& args : done) {
                    const auto result = args.at(0).value<CommandResult>();
                    if (result.token == token) {
                        ok = result.ok;
                        port = static_cast<quint16>(result.value);
                        return true;
                    }
                }
                return false;
            },
            kWaitMs);
    }
    MockHost host;
    QSignalSpy done;
    bool ok{false};
    quint16 port{0};
};

} // namespace

class DeviceTabTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { registerRunnerMetaTypes(); }

    // GUI-06: the property tree is built from the library's own JSON keys; a key that the library
    // adds and the GUI does not show makes this fail.
    void GUI_06_everyConfigFieldHasAProperty() {
        ConfigBinding binding;
        QSet<QString> leaves;
        collectLeaves(mc::McDeviceConfig{}.toJson(), QString(), leaves);
        QSet<QString> bound;
        for (const FieldInfo& field : ConfigBinding::fields()) {
            bound.insert(field.path);
            const qpb::Property* property = binding.model()->find(field.path);
            QVERIFY2(property != nullptr, qPrintable(field.path));
            QVERIFY2(!property->isGroup(), qPrintable(field.path));
        }
        QCOMPARE(bound, leaves);
    }

    // GUI-06: setConfig() then the grid shows exactly the configuration, for a spread of valid ones
    // (3E ASCII with every route field, FX octal X/Y, 1E, 3C serial, 1C serial).
    void GUI_06_configRoundTripsThroughTheGrid() {
        for (const mc::McDeviceConfig& cfg : sampleConfigs()) {
            QVERIFY(cfg.validate());
            ConfigBinding binding;
            QString message;
            QVERIFY2(binding.setConfig(cfg, &message), qPrintable(message));
            QCOMPARE(binding.config().toJson(), cfg.toJson());

            const QJsonObject json = cfg.toJson();
            for (const FieldInfo& field : ConfigBinding::fields()) {
                if (field.kind == FieldKind::Subscriptions) {
                    continue;
                }
                const qpb::Property* property = binding.model()->find(field.path);
                QCOMPARE(property->value().toString(), jsonText(jsonAt(json, field.path)));
            }
            const QString subscriptions = binding.model()->find(QStringLiteral("subscriptions"))->value().toString();
            for (const mc::SubscriptionSpec& spec : cfg.subscriptions) {
                QVERIFY2(subscriptions.contains(spec.device), qPrintable(subscriptions));
            }

            // The grid's own tree gives the same configuration back through a new binding.
            ConfigBinding again(binding.config());
            QCOMPARE(again.config().toJson(), cfg.toJson());
        }
    }

    // GUI-06: every field is editable. For each field, each alternative value is either accepted
    // (config() carries it, configChanged fires once) or rejected with a message and leaves the
    // value and the configuration as they were; each field accepts at least one alternative.
    void GUI_06_everyFieldIsEditableAndRefusalsLeaveEverythingUnchanged() {
        const mc::McDeviceConfig base = baseConfig();
        ConfigBinding binding(base);
        int accepted = 0;
        int rejected = 0;
        for (const FieldInfo& field : ConfigBinding::fields()) {
            const QJsonObject baseJson = base.toJson();
            const QJsonValue current = jsonAt(baseJson, field.path);
            QVector<QVariant> candidates;
            switch (field.kind) {
            case FieldKind::Bool:
                candidates.push_back(!current.toBool());
                break;
            case FieldKind::Int:
            case FieldKind::Int64: {
                const qint64 now = jsonInteger(current);
                if (now + 1 <= field.maximum) {
                    candidates.push_back(now + 1);
                }
                if (now - 1 >= field.minimum) {
                    candidates.push_back(now - 1);
                }
                break;
            }
            case FieldKind::Enum:
                for (const QString& option : field.options) {
                    if (option != current.toString()) {
                        candidates.push_back(option);
                    }
                }
                break;
            case FieldKind::Text:
                candidates.push_back(field.path == QLatin1String("session/heartbeat/device")
                                         ? QStringLiteral("M2001")
                                         : current.toString() + QStringLiteral("1"));
                break;
            case FieldKind::MaxGap:
                candidates.push_back(QStringLiteral("8"));
                break;
            case FieldKind::Subscriptions:
                candidates.push_back(QStringLiteral("D200:2; M16:8"));
                break;
            }
            int acceptedHere = 0;
            for (const QVariant& value : candidates) {
                QVERIFY(binding.setConfig(base));
                const QJsonObject before = binding.config().toJson();
                QSignalSpy changed(&binding, &ConfigBinding::configChanged);
                QSignalSpy refused(&binding, &ConfigBinding::rejected);
                const bool took = binding.model()->setValue(field.path, value);
                if (took) {
                    ++accepted;
                    ++acceptedHere;
                    QCOMPARE(changed.count(), 1);
                    QVERIFY2(binding.config().toJson() != before, qPrintable(field.path));
                    if (field.kind != FieldKind::Subscriptions) {
                        QCOMPARE(jsonText(jsonAt(binding.config().toJson(), field.path)),
                                 value.toString());
                    }
                } else {
                    ++rejected;
                    QCOMPARE(changed.count(), 0);
                    QVERIFY2(refused.count() >= 1, qPrintable(field.path));
                    QVERIFY2(!refused.last().at(1).toString().isEmpty(), qPrintable(field.path));
                    QCOMPARE(binding.config().toJson(), before);
                }
            }
            QVERIFY2(acceptedHere >= 1, qPrintable(field.path));
        }
        QVERIFY(accepted >= 50);
        QVERIFY(rejected >= 0);
    }

    // GUI-06: a value the library refuses shows the library's message (with the field path it
    // names), keeps the old value in the grid, and does not reach config().
    void GUI_06_invalidValuesShowTheLibraryMessageAndAreNotApplied() {
        ConfigBinding binding;
        const QJsonObject before = binding.config().toJson();
        QSignalSpy changed(&binding, &ConfigBinding::configChanged);
        QSignalSpy refused(&binding, &ConfigBinding::rejected);

        // validate(): "TCP port is 0" at transport.tcp.port
        QVERIFY(!binding.model()->setValue(QStringLiteral("transport/tcp/port"), 0));
        QCOMPARE(refused.count(), 1);
        const QString portMessage = refused.last().at(1).toString();
        QVERIFY2(portMessage.contains(QStringLiteral("TCP port is 0")), qPrintable(portMessage));
        QVERIFY2(portMessage.contains(QStringLiteral("transport.tcp.port")), qPrintable(portMessage));
        QCOMPARE(binding.lastMessage(), portMessage);
        QCOMPARE(binding.model()->find(QStringLiteral("transport/tcp/port"))->value().toInt(), 5000);

        // validate(): an empty host
        QVERIFY(!binding.model()->setValue(QStringLiteral("transport/tcp/host"), QStringLiteral("")));
        QVERIFY2(refused.last().at(1).toString().contains(QStringLiteral("TCP host is empty")),
                 qPrintable(refused.last().at(1).toString()));

        // the Session's rule: at least one link error
        QVERIFY(!binding.model()->setValue(QStringLiteral("session/maxConsecutiveLinkErrors"), 0));
        QVERIFY2(refused.last().at(1).toString().contains(QStringLiteral("session.maxConsecutiveLinkErrors")),
                 qPrintable(refused.last().at(1).toString()));

        // fromJson(): a heartbeat device that is not a device, a max gap that is not a number
        QVERIFY(!binding.model()->setValue(QStringLiteral("session/heartbeat/device"), QStringLiteral("ZZ9")));
        QVERIFY2(refused.last().at(1).toString().contains(QStringLiteral("not a device")),
                 qPrintable(refused.last().at(1).toString()));
        QVERIFY(!binding.model()->setValue(QStringLiteral("session/maxGap"), QStringLiteral("many")));
        QVERIFY2(refused.last().at(1).toString().contains(QStringLiteral("auto")),
                 qPrintable(refused.last().at(1).toString()));

        // a subscription the frame cannot carry, and text that is no subscription at all
        QVERIFY(!binding.model()->setValue(QStringLiteral("subscriptions"), QStringLiteral("D100:0")));
        QVERIFY2(refused.last().at(1).toString().contains(QStringLiteral("subscriptions[0].count")),
                 qPrintable(refused.last().at(1).toString()));
        QVERIFY(!binding.model()->setValue(QStringLiteral("subscriptions"), QStringLiteral("D100:four")));
        QVERIFY2(refused.last().at(1).toString().contains(QStringLiteral("DEVICE:COUNT")),
                 qPrintable(refused.last().at(1).toString()));

        // a reserved frame family
        QVERIFY(!binding.model()->setValue(QStringLiteral("frame/frame"), QStringLiteral("4E")));

        QCOMPARE(changed.count(), 0);
        QCOMPARE(binding.config().toJson(), before);

        // setConfig() refuses an invalid whole configuration the same way.
        mc::McDeviceConfig bad;
        bad.tcp.port = 0;
        QString message;
        QVERIFY(!binding.setConfig(bad, &message));
        QVERIFY2(message.contains(QStringLiteral("TCP port is 0")), qPrintable(message));
        QCOMPARE(binding.config().toJson(), before);
    }

    // Choosing another frame family loads that family's defaults when the old values do not fit.
    void GUI_06_frameFamilyChangeLoadsItsDefaultsWhenTheValuesDoNotFit() {
        ConfigBinding binding;
        QSignalSpy changed(&binding, &ConfigBinding::configChanged);
        QVERIFY(binding.model()->setValue(QStringLiteral("frame/frame"), QStringLiteral("3C")));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(binding.config().frame.frame, mc::FrameType::F3C);
        QCOMPARE(binding.config().frame.code, mc::DataCode::Ascii);
        QVERIFY(binding.config().validate());
        QCOMPARE(binding.model()->find(QStringLiteral("frame/code"))->value().toString(),
                 QStringLiteral("Ascii"));
    }

    // The tab sends an accepted edit to its runner and keeps the previous configuration when an
    // edit is refused: the refused port never reaches the device, so it still connects.
    void GUI_06_tabAppliesAcceptedEditsAndKeepsTheOldConfigOnRefusal() {
        MockRig mock;
        QVERIFY(mock.ok);
        DeviceTab tab(QStringLiteral("t069-edit"));
        QSignalSpy applied(&tab, &DeviceTab::configApplied);
        qpb::PropertyModel* model = tab.binding()->model();
        QVERIFY(model->setValue(QStringLiteral("transport/tcp/host"), QStringLiteral("127.0.0.1")));
        QVERIFY(model->setValue(QStringLiteral("transport/tcp/port"), static_cast<int>(mock.port)));
        QVERIFY(model->setValue(QStringLiteral("transport/tcp/connectTimeoutMs"), 500));
        QVERIFY(model->setValue(QStringLiteral("session/cycleIntervalMs"), static_cast<qint64>(20)));
        QVERIFY(model->setValue(QStringLiteral("subscriptions"), QStringLiteral("D100:4")));
        QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 5, kWaitMs);
        for (const QList<QVariant>& args : applied) {
            QVERIFY(args.at(0).toBool());
        }

        QVERIFY(!model->setValue(QStringLiteral("transport/tcp/port"), 0));
        QVERIFY2(tab.message().contains(QStringLiteral("TCP port is 0")), qPrintable(tab.message()));
        QCOMPARE(tab.binding()->config().tcp.port, mock.port);
        QTest::qWait(100);
        QCOMPARE(applied.count(), 5); // nothing was sent for the refused edit

        tab.connectToPlc();
        QTRY_COMPARE_WITH_TIMEOUT(tab.linkState(), mc::LinkState::Connected, kWaitMs);
        QTRY_VERIFY_WITH_TIMEOUT(tab.points()->rowOfDevice(QStringLiteral("D100")) >= 0, kWaitMs);

        // Connected: the grid is read-only for the user.
        QVERIFY(tab.binding()->model()->root()->isReadOnly());
    }

    // Acceptance: a device tab connects to a MockRunner, subscribes, sees values and a trend
    // sample, and runs an ad-hoc write and read-back; the McDevice stays on the runner thread.
    void DeviceTab_connectSubscribeTrendAndAdHocWriteReadBack() {
        MockRig mock;
        QVERIFY(mock.ok);
        DeviceTab tab(QStringLiteral("t069-tab"));
        QSignalSpy applied(&tab, &DeviceTab::configApplied);
        QString error;
        QVERIFY2(tab.setConfig(tcpConfig(mock.port), &error), qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, kWaitMs);
        QVERIFY(applied.last().at(0).toBool());

        tab.connectToPlc();
        QTRY_COMPARE_WITH_TIMEOUT(tab.linkState(), mc::LinkState::Connected, kWaitMs);

        // Live values of the configured subscriptions.
        PointTableModel* points = tab.points();
        const auto row = [&](const char* device) { return points->rowOfDevice(QLatin1String(device)); };
        QTRY_VERIFY_WITH_TIMEOUT(row("D103") >= 0 && row("M3") >= 0 && points->valueAt(row("D103")) == 40,
                                 kWaitMs);
        QCOMPARE(points->valueAt(row("D100")), static_cast<quint16>(10));
        QCOMPARE(points->valueAt(row("D101")), static_cast<quint16>(20));
        QCOMPARE(points->valueAt(row("M3")), static_cast<quint16>(1));
        QCOMPARE(points->valueAt(row("M2")), static_cast<quint16>(0));
        QCOMPARE(points->stateAt(row("D100")), mc::PointState::Valid);
        QCOMPARE(points->rowCount(), 4 + 16);

        // Trend: a ticked point puts samples into the chart.
        points->setTrend(QStringLiteral("D100"), true);
        QVERIFY(tab.trend()->hasSeries(QStringLiteral("D100")));
        QTRY_VERIFY_WITH_TIMEOUT(tab.trend()->sampleCount(QStringLiteral("D100")) >= 2, kWaitMs);
        QCOMPARE(tab.trend()->samples(QStringLiteral("D100")).last().y(), 10.0);
        QVERIFY(!tab.trend()->grab().isNull());

        // Ad-hoc write, then read back; both carry a token and a request id.
        const quint64 writeToken = tab.sendWrite(false, QStringLiteral("D100"), QStringLiteral("99 0x62"), &error);
        QVERIFY2(writeToken != 0, qPrintable(error));
        ConsoleModel* console = tab.console();
        QTRY_VERIFY_WITH_TIMEOUT(console->rowOfToken(writeToken) >= 0 &&
                                     console->entry(console->rowOfToken(writeToken)).state ==
                                         ConsoleEntry::State::Ok,
                                 kWaitMs);
        const ConsoleEntry writeEntry = console->entry(console->rowOfToken(writeToken));
        QCOMPARE(writeEntry.result, QStringLiteral("ok"));
        QVERIFY(writeEntry.requestId != 0);

        // The polled value follows, and the change is highlighted.
        QTRY_VERIFY_WITH_TIMEOUT(points->valueAt(row("D100")) == 99 && points->isHighlighted(row("D100")),
                                 kWaitMs);
        QTRY_COMPARE_WITH_TIMEOUT(points->valueAt(row("D101")), static_cast<quint16>(98), kWaitMs);

        const quint64 readToken = tab.sendRead(false, QStringLiteral("D100"), 2, &error);
        QVERIFY2(readToken != 0, qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(console->rowOfToken(readToken) >= 0 &&
                                     console->entry(console->rowOfToken(readToken)).state ==
                                         ConsoleEntry::State::Ok,
                                 kWaitMs);
        const ConsoleEntry readEntry = console->entry(console->rowOfToken(readToken));
        QCOMPARE(readEntry.result, QStringLiteral("ok: 99 98"));
        QVERIFY(readEntry.requestId != 0);
        QVERIFY(readEntry.requestId != writeEntry.requestId);

        // Bits.
        const quint64 bitWrite = tab.sendWrite(true, QStringLiteral("M0"), QStringLiteral("1 on"), &error);
        QVERIFY2(bitWrite != 0, qPrintable(error));
        const quint64 bitRead = tab.sendRead(true, QStringLiteral("M0"), 4, &error);
        QVERIFY2(bitRead != 0, qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(console->rowOfToken(bitRead) >= 0 &&
                                     console->entry(console->rowOfToken(bitRead)).state ==
                                         ConsoleEntry::State::Ok,
                                 kWaitMs);
        QCOMPARE(console->entry(console->rowOfToken(bitRead)).result, QStringLiteral("ok: 1 1 0 1"));

        // A command the library refuses ends as a failed row with its message; input that does not
        // parse is refused before anything is sent.
        const quint64 badRead = tab.sendRead(false, QStringLiteral("ZZ1"), 1, &error);
        QVERIFY(badRead != 0);
        QTRY_VERIFY_WITH_TIMEOUT(console->rowOfToken(badRead) >= 0 &&
                                     console->entry(console->rowOfToken(badRead)).state ==
                                         ConsoleEntry::State::Failed,
                                 kWaitMs);
        QVERIFY(!console->entry(console->rowOfToken(badRead)).result.isEmpty());
        const int rowsBefore = console->rowCount();
        QCOMPARE(tab.sendWrite(false, QStringLiteral("D100"), QStringLiteral("abc"), &error), quint64(0));
        QVERIFY2(error.contains(QStringLiteral("16-bit")), qPrintable(error));
        QCOMPARE(tab.sendWrite(true, QStringLiteral("M0"), QStringLiteral("2"), &error), quint64(0));
        QCOMPARE(tab.sendRead(false, QString(), 1, &error), quint64(0));
        QCOMPARE(console->rowCount(), rowsBefore);

        // A subscription added at run time shows up in the points table.
        tab.subscribe(QStringLiteral("D200"), 2);
        QTRY_COMPARE_WITH_TIMEOUT(tab.runtimeSubscriptionCount(), 1, kWaitMs);
        QTRY_VERIFY_WITH_TIMEOUT(row("D201") >= 0, kWaitMs);

        // The device and its transport live on the runner thread, never on the GUI thread.
        QSignalSpy reports(tab.host(), &DeviceHost::threadReport);
        tab.host()->requestThreadReport();
        QTRY_COMPARE_WITH_TIMEOUT(reports.count(), 1, kWaitMs);
        const auto report = reports.last().at(0).value<ThreadReport>();
        const quintptr gui = reinterpret_cast<quintptr>(QThread::currentThread());
        QVERIFY(report.deviceThread != 0);
        QVERIFY(report.deviceThread != gui);
        QCOMPARE(report.deviceThread, report.runnerThread);
        QCOMPARE(report.transportThread, report.runnerThread);

        // Disconnect: the values stay, marked Stale.
        tab.disconnectFromPlc();
        QTRY_COMPARE_WITH_TIMEOUT(tab.linkState(), mc::LinkState::Disconnected, kWaitMs);
        QTRY_COMPARE_WITH_TIMEOUT(points->stateAt(row("D100")), mc::PointState::Stale, kWaitMs);
        QCOMPARE(points->valueAt(row("D100")), static_cast<quint16>(99));
        QVERIFY(!tab.binding()->model()->root()->isReadOnly());
    }

    // A refused connect (nothing listens) is reported on the tab; the tab stays usable.
    void DeviceTab_failedConnectIsShownAndLeavesTheTabUsable() {
        DeviceTab tab(QStringLiteral("t069-refused"));
        mc::McDeviceConfig cfg = tcpConfig(1);
        cfg.tcp.port = 1; // nothing listens on port 1
        cfg.tcp.connectTimeoutMs = 300;
        QVERIFY(tab.setConfig(cfg));
        QSignalSpy states(&tab, &DeviceTab::linkStateChanged);
        tab.connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(states.count() >= 2 && tab.linkState() == mc::LinkState::Disconnected, 15000);
        QCOMPARE(states.first().at(0).value<mc::LinkState>(), mc::LinkState::Connecting);
        QVERIFY2(tab.linkStatusText().contains(QStringLiteral("open failed")), qPrintable(tab.linkStatusText()));
        QVERIFY(tab.binding()->model()->root() != nullptr);
        QVERIFY(tab.binding()->setConfig(tcpConfig(5000)));
    }

    // The console log matches a request outcome to its command in either arrival order, refuses
    // nothing silently and stays bounded.
    void ConsoleModel_matchesTokenAndRequestIdInEitherOrder() {
        ConsoleModel model;
        const int a = model.begin(1, ConsoleOp::ReadWords, QStringLiteral("read words D0 x2"));
        QCOMPARE(model.entry(a).state, ConsoleEntry::State::Pending);

        CommandResult done;
        done.token = 1;
        done.value = 40;
        model.onCommandDone(done);
        RequestOutcome outcome;
        outcome.id = 40;
        outcome.payload = QByteArray("\x01\x00\x02\x01", 4);
        model.onRequestFinished(outcome);
        QCOMPARE(model.entry(a).requestId, quint64(40));
        QCOMPARE(model.entry(a).result, QStringLiteral("ok: 1 258"));

        // The outcome first, the answer second.
        const int b = model.begin(2, ConsoleOp::WriteWords, QStringLiteral("write"));
        RequestOutcome early;
        early.id = 41;
        model.onRequestFinished(early);
        QCOMPARE(model.entry(b).state, ConsoleEntry::State::Pending);
        CommandResult second;
        second.token = 2;
        second.value = 41;
        model.onCommandDone(second);
        QCOMPARE(model.entry(b).state, ConsoleEntry::State::Ok);

        // A refusal has no request id.
        const int c = model.begin(3, ConsoleOp::ReadBits, QStringLiteral("read bits"));
        CommandResult refused;
        refused.token = 3;
        refused.ok = false;
        refused.message = QStringLiteral("link is down");
        model.onCommandDone(refused);
        QCOMPARE(model.entry(c).state, ConsoleEntry::State::Failed);
        QCOMPARE(model.entry(c).requestId, quint64(0));

        // A PLC error outcome.
        const int d = model.begin(4, ConsoleOp::ReadWords, QStringLiteral("read"));
        CommandResult fourth;
        fourth.token = 4;
        fourth.value = 42;
        model.onCommandDone(fourth);
        RequestOutcome plcError;
        plcError.id = 42;
        plcError.errorCode = 9;
        plcError.message = QStringLiteral("PLC error");
        model.onRequestFinished(plcError);
        QCOMPARE(model.entry(d).state, ConsoleEntry::State::Failed);
        QVERIFY(model.entry(d).result.contains(QStringLiteral("PLC error")));

        // Bounded: the oldest rows fall out, the lookup of a dropped token fails cleanly.
        for (int i = 0; i < ConsoleModel::kCapacity + 10; ++i) {
            model.begin(100 + static_cast<quint64>(i), ConsoleOp::ReadWords, QStringLiteral("x"));
        }
        QCOMPARE(model.rowCount(), ConsoleModel::kCapacity);
        QCOMPARE(model.rowOfToken(1), -1);
        QVERIFY(model.rowOfToken(100 + ConsoleModel::kCapacity + 9) >= 0);
    }

    void ConsoleModel_parsesWordAndBitLists() {
        QVector<quint16> words;
        QString error;
        QVERIFY(parseWordList(QStringLiteral("1, 2;0x10  65535 -1 010"), words, &error));
        QCOMPARE(words, (QVector<quint16>{1, 2, 16, 65535, 65535, 10}));
        QVERIFY(!parseWordList(QStringLiteral("1 70000"), words, &error));
        QVERIFY(error.contains(QStringLiteral("70000")));
        QVERIFY(!parseWordList(QString(), words, &error));
        QVERIFY(!parseWordList(QStringLiteral("0x"), words, &error));

        QVector<bool> bits;
        QVERIFY(parseBitList(QStringLiteral("1 0 on OFF true false"), bits, &error));
        QCOMPARE(bits, (QVector<bool>{true, false, true, false, true, false}));
        QVERIFY(!parseBitList(QStringLiteral("1 2"), bits, &error));
        QVERIFY(!parseBitList(QStringLiteral(" "), bits, &error));
    }

    // The trend is bounded and draws.
    void TrendWidget_keepsAtMostTheCapacityAndPaints() {
        TrendWidget trend;
        trend.resize(300, 160);
        trend.addSeries(QStringLiteral("D1"));
        trend.addSample(QStringLiteral("nope"), 0, 1.0); // an unknown series is ignored
        for (int i = 0; i < TrendWidget::kMaxSamples + 500; ++i) {
            trend.addSample(QStringLiteral("D1"), i * 10, i % 7);
        }
        QCOMPARE(trend.sampleCount(QStringLiteral("D1")), TrendWidget::kMaxSamples);
        QCOMPARE(trend.sampleCount(QStringLiteral("nope")), 0);
        QVERIFY(!trend.grab().isNull());
        trend.removeSeries(QStringLiteral("D1"));
        QVERIFY(!trend.hasSeries(QStringLiteral("D1")));
        QVERIFY(!trend.grab().isNull());
    }

    // The Devices dock hosts device tabs; closing one stops its thread and removes it.
    void DevicePane_addsAndClosesTabsInTheMainWindow() {
        MainWindow window;
        DevicePane* pane = window.devicePane();
        QVERIFY(pane != nullptr);
        QCOMPARE(pane->deviceCount(), 0);
        DeviceTab* first = pane->addDevice();
        DeviceTab* second = pane->addDevice();
        QCOMPARE(pane->deviceCount(), 2);
        QVERIFY(first->name() != second->name());
        QElapsedTimer waited;
        waited.start();
        pane->closeDevice(0);
        QCOMPARE(pane->deviceCount(), 1);
        QVERIFY(waited.elapsed() < 3500);
        QCOMPARE(pane->deviceAt(0)->name(), second->name());
    }
};

namespace mc::workbench::test {

QObject* makeDeviceTabSuite() {
    return new DeviceTabTest;
}

} // namespace mc::workbench::test

#include "tst_gui_device_tab.moc"
