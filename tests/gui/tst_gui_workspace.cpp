// GUI-08 (SPEC-gui-tool.md "Testing"; task T-073): workspace save / load restores tabs, configs and
// the dock layout. Also: a damaged file is refused with a message and the window stays usable,
// unknown keys are reported with their JSON path, the file work stays off the GUI thread, and every
// device and mock created by a load lives on its runner thread.
//
// Workspaces are written under the build tree (MC_GUI_OUTPUT_DIR), never into the source tree. No
// address or COM name is spelled out in this file: the sample hosts and port names are built from
// numbers at run time, and the test checks that only the workspace file the user saves holds them.
#include "gui_suites.h"
#include "gui_test_support.h"

#include "mc_workbench/config_binding.h"
#include "mc_workbench/device_host.h"
#include "mc_workbench/device_tab.h"
#include "mc_workbench/hil_view.h"
#include "mc_workbench/main_window.h"
#include "mc_workbench/mock_host.h"
#include "mc_workbench/mock_tab.h"
#include "mc_workbench/recent_files.h"
#include "mc_workbench/runner_base.h"
#include "mc_workbench/workspace.h"
#include "mc_workbench/workspace_controller.h"

#include <DockManager.h>
#include <DockWidget.h>
#include <DockAreaWidget.h>

#include <qpb/PropertyModel.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMenuBar>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTest>
#include <QThread>

#include <memory>

using namespace mc::workbench;
using mc::workbench::test::Ticker;

namespace {

constexpr int kLoadWait = 30000;

QString sampleHost(int n) {
    return QStringLiteral("10.%1.%2.%3").arg(n).arg(n + 1).arg(n + 2);
}

QString sampleCom(int n) {
    return QStringLiteral("COM%1").arg(n);
}

mc::McDeviceConfig tcp3E(int n, quint16 port) {
    mc::McDeviceConfig cfg;
    cfg.tcp.host = sampleHost(n);
    cfg.tcp.port = port;
    cfg.tcp.connectTimeoutMs = 750;
    cfg.frame = mc::FrameConfig::frame3E(mc::DataCode::Ascii);
    cfg.frame.network = 2;
    cfg.frame.timeoutMs = 1500;
    cfg.session.cycleIntervalMs = 40;
    cfg.subscriptions = {{QStringLiteral("D100"), 4}, {QStringLiteral("M0"), 16}};
    return cfg;
}

mc::McDeviceConfig serial3C(int comNo) {
    mc::McDeviceConfig cfg;
    cfg.transport = mc::TransportKind::Serial;
    cfg.frame = mc::FrameConfig::frame3C(mc::SerialFormat::Format4);
    cfg.serial.portName = sampleCom(comNo);
    cfg.serial.baudRate = 19200;
    cfg.subscriptions = {{QStringLiteral("D0"), 2}};
    return cfg;
}

mc::McDeviceConfig fxOctal(int n) {
    mc::McDeviceConfig cfg;
    cfg.tcp.host = sampleHost(n);
    cfg.tcp.port = 5009;
    cfg.frame = mc::FrameConfig::frame1E();
    cfg.frame.xyNotation = mc::XyNumbering::Octal;
    cfg.frame.xyAsciiDigits = mc::XyNumbering::Octal;
    cfg.subscriptions = {{QStringLiteral("X10"), 8}};
    return cfg;
}

// A description of the dock layout that does not depend on the order of the areas: per dock whether
// it is closed or floating and which docks share its area, plus the title of each area's current tab.
QString describeLayout(const MainWindow& window) {
    QStringList lines;
    for (const QString& title : window.dockTitles()) {
        ads::CDockWidget* dock = window.dockWidget(title);
        QStringList members;
        QString current;
        if (ads::CDockAreaWidget* area = dock->dockAreaWidget()) {
            for (ads::CDockWidget* other : area->dockWidgets()) {
                members.append(other->windowTitle());
            }
            members.sort();
            current = area->currentDockWidget() != nullptr ? area->currentDockWidget()->windowTitle()
                                                           : QString();
        }
        lines.append(QStringLiteral("%1 closed=%2 floating=%3 area=[%4] current=%5")
                         .arg(title)
                         .arg(dock->isClosed())
                         .arg(dock->isFloating())
                         .arg(members.join(QLatin1Char(',')))
                         .arg(current));
    }
    return lines.join(QLatin1Char('\n'));
}

// Everything the tabs and the HIL view hold, as JSON (the layout is compared separately).
QJsonObject tabsAndHil(const MainWindow& window) {
    QJsonObject o = workspaceToJson(window.workspace()->capture());
    o.remove(QStringLiteral("layout"));
    return o;
}

MemoryBlock readBlock(MockTab* tab, const QString& head, quint16 count, bool bits) {
    QSignalSpy blocks(tab->host(), &MockHost::memoryRead);
    const quint64 token = tab->host()->readMemory(head, count, bits);
    MemoryBlock found;
    (void)QTest::qWaitFor(
        [&]() {
            for (const QList<QVariant>& args : blocks) {
                const auto block = args.at(0).value<MemoryBlock>();
                if (block.token == token) {
                    found = block;
                    return true;
                }
            }
            return false;
        },
        mc::workbench::test::kWait);
    return found;
}

QByteArray readAll(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void writeAll(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
    file.write(bytes);
}

struct DamagedCase {
    const char* what;   // for the failure text
    const char* name;   // selects the damage in damage()
    const char* expect; // text the refusal must hold
};

QList<DamagedCase> damagedCases() {
    return {
        {"truncated", "truncated", "not valid JSON"},
        {"empty", "empty", "not valid JSON"},
        {"binary garbage", "garbage", "not valid JSON"},
        {"top level array", "array", "the top level is not an object"},
        {"wrong format", "format", "format: expected"},
        {"future version", "version", "version: unsupported version 2"},
        {"missing version", "noversion", "version: missing required key"},
        {"port zero", "port0", "devices[0].config.transport.tcp.port"},
        {"wrong type in a config", "wrongtype", "devices[0].config.frame.timeoutMs"},
        {"unknown key in a config", "unknowncfg", "devices[0].config.frame.timeoutMS: unknown key"},
        {"unknown key in a mock grid", "unknownmock", "mocks[0].settings.Frame.bogus: unknown key"},
        {"refused value in a mock grid", "badmockvalue", "mocks[0].settings.Errors.unsupportedQna"},
        {"bad memory preset kind", "badkind", "mocks[0].memory[0].kind"},
        {"bad memory preset device", "baddevice", "mocks[0].memory[0].head"},
        {"preset kind against device", "badbits", "mocks[0].memory[0].head"},
        {"mock value clamped by the grid", "clampedport", "mocks[0].settings.Serving.tcpPort"},
        {"preset past the last device", "presetpast", "mocks[0].memory[0].head"},
        {"bad layout base64", "badbase64", "layout.docks: not valid base64"},
        {"bad plc state", "badstate", "hil.plcState"},
        {"too many points", "toomany", "mocks[0].memory[0].values"},
        {"missing file", "missing", "does not exist"},
        {"too large", "huge", "too large"},
    };
}

} // namespace

class WorkspaceTest : public QObject {
    Q_OBJECT

private:
    // Fills a window with: 3 device tabs (TCP 3E, serial 3C, FX octal 1E), 2 mock tabs (a COM-serving
    // 3C mock with presets and changed error codes, an octal FX mock with a bit preset), HIL inputs,
    // a changed dock layout and chosen current tabs.
    static void buildSample(MainWindow& window, Workspace* expected) {
        window.setWorkspaceDialogs(false);
        window.resize(780, 540); // inside the 800x600 offscreen screen
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        QVector<mc::McDeviceConfig> configs = {tcp3E(1, 5001), serial3C(7), fxOctal(30)};
        for (const mc::McDeviceConfig& cfg : configs) {
            DeviceTab* tab = window.devicePane()->addDevice();
            QString error;
            QVERIFY2(tab->setConfig(cfg, &error), qPrintable(error));
        }

        MockTab* m1 = window.mockPane()->addMock();
        auto* model1 = m1->configModel();
        QVERIFY(model1->setValue(QStringLiteral("Frame/frame"), 2));
        QVERIFY(model1->setValue(QStringLiteral("Frame/format"), 3));
        QVERIFY(model1->setValue(QStringLiteral("Serving/mode"), 1));
        QVERIFY(model1->setValue(QStringLiteral("Serving/comPort"), sampleCom(8)));
        QVERIFY(model1->setValue(QStringLiteral("Serving/baudRate"), 19200));
        QVERIFY(model1->setValue(QStringLiteral("Errors/unsupportedQna"), QStringLiteral("C123")));
        m1->setMemoryPresets({MemoryPreset{QStringLiteral("D100"), false, {1, 2, 3, 65535}},
                              MemoryPreset{QStringLiteral("M10"), true, {1, 0, 1}}});

        MockTab* m2 = window.mockPane()->addMock();
        QVERIFY(m2->configModel()->setValue(QStringLiteral("Frame/xyNotation"), 1));
        QVERIFY(m2->configModel()->setValue(QStringLiteral("Serving/tcpPort"), 5555));
        m2->setMemoryPresets({MemoryPreset{QStringLiteral("X10"), true, {1, 1, 0, 1}}});

        HilView* hil = window.hilView();
        hil->setProfilePath(QStringLiteral("profiles/bench.json"));
        hil->setPlanPath(QStringLiteral("plans/smoke.json"));
        hil->setGroups(QStringLiteral("G1,G3"));
        hil->setPlcState(QStringLiteral("STOP"));
        hil->setOutputRoot(QStringLiteral("captures/out"));
        hil->setNote(QStringLiteral("bench day 2"));
        hil->setBenchReps(7);
        hil->setOverwrite(true);
        hil->setSource(CaptureSource::VirtualPlc);

        // A changed layout: Properties closed, Debug log on the left, HIL runner the current tab.
        ads::CDockManager* dm = window.dockManager();
        window.dockWidget(QStringLiteral("Properties"))->toggleView(false);
        dm->addDockWidget(ads::LeftDockWidgetArea, window.dockWidget(QStringLiteral("Debug log")));
        window.dockWidget(QStringLiteral("HIL runner"))->setAsCurrentTab();
        window.devicePane()->setCurrentIndex(2);
        window.mockPane()->setCurrentIndex(1);
        QCoreApplication::processEvents();

        if (expected != nullptr) {
            *expected = window.workspace()->capture();
        }
    }

private slots:
    void initTestCase() {
        QDir().mkpath(QStringLiteral(MC_GUI_OUTPUT_DIR));
    }

    // GUI-08: save, close, load restores tabs, configs, mock settings and presets, the HIL inputs
    // and the dock layout.
    void GUI_08_saveCloseLoadRestoresTabsConfigsAndLayout() {
        const QString dir = mc::workbench::test::freshOutputDir(QStringLiteral("gui-t073-roundtrip"));
        const QString file = QDir(dir).absoluteFilePath(QStringLiteral("bench.json"));
        const int liveBefore = RunnerBase::liveRunners();
        const int foreignBefore = RunnerBase::foreignDeletes();

        Workspace expected;
        QString layoutBefore;
        QSize sizeBefore;
        {
            MainWindow window;
            buildSample(window, &expected);
            QVERIFY(!QTest::currentTestFailed());
            layoutBefore = describeLayout(window);
            sizeBefore = window.size();

            QSignalSpy saved(window.workspace(), &WorkspaceController::saveFinished);
            QVERIFY(window.workspace()->save(file));
            QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 1, kLoadWait);
            QVERIFY2(saved.first().at(1).toBool(), qPrintable(saved.first().at(2).toString()));
            QCOMPARE(window.workspace()->path(), QFileInfo(file).absoluteFilePath());
            QVERIFY(window.windowTitle().contains(QStringLiteral("bench.json")));
        } // the window closes: every runner thread is joined
        QCOMPARE(RunnerBase::liveRunners(), liveBefore);

        // The file is a workspace this build reads back unchanged.
        Workspace reread;
        QVector<WorkspaceProblem> problems;
        QVERIFY2(workspaceFromBytes(readAll(file), reread, problems),
                 qPrintable(workspaceProblemsText(problems)));
        QCOMPARE(reread.devices.size(), 3);
        QCOMPARE(reread.mocks.size(), 2);

        MainWindow window;
        window.setWorkspaceDialogs(false);
        window.resize(700, 480);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        // The fresh window differs from the saved one, so the test can fail.
        QVERIFY(describeLayout(window) != layoutBefore);
        QCOMPARE(window.devicePane()->deviceCount(), 0);

        QSignalSpy loaded(window.workspace(), &WorkspaceController::loadFinished);
        QVERIFY(window.workspace()->load(file));
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, kLoadWait);
        QVERIFY2(loaded.first().at(1).toBool(), qPrintable(loaded.first().at(2).toString()));

        // Tabs and configs.
        QCOMPARE(window.devicePane()->deviceCount(), 3);
        for (int i = 0; i < 3; ++i) {
            const DeviceTab* tab = window.devicePane()->deviceAt(i);
            QCOMPARE(tab->name(), expected.devices.at(i).name);
            QCOMPARE(tab->binding()->config().toJson(), expected.devices.at(i).config.toJson());
            QCOMPARE(tab->linkState(), mc::LinkState::Disconnected);
        }
        QCOMPARE(window.mockPane()->mockCount(), 2);
        for (int i = 0; i < 2; ++i) {
            MockTab* tab = window.mockPane()->mockAt(i);
            QCOMPARE(tab->name(), expected.mocks.at(i).name);
            QCOMPARE(tab->saveState().settings, expected.mocks.at(i).settings);
            QCOMPARE(tab->memoryPresets(), expected.mocks.at(i).presets);
            QVERIFY(!tab->isServing());
        }
        // Spot checks that the grid really holds the loaded values, not only the saved copy.
        MockTab* m1 = window.mockPane()->mockAt(0);
        QCOMPARE(m1->frameConfig().frame, mc::FrameType::F3C);
        QVERIFY(m1->servesSerial());
        QCOMPARE(m1->serialLine().portName, sampleCom(8));
        QCOMPARE(m1->mockSettings().unsupportedQna, quint16(0xC123));
        MockTab* m2 = window.mockPane()->mockAt(1);
        QCOMPARE(m2->frameConfig().xyNotation, mc::XyNumbering::Octal);

        // The presets were written into the rebuilt mocks.
        // The rebuild is answered, then the presets are sent: poll until they are in.
        QTRY_COMPARE_WITH_TIMEOUT(readBlock(m1, QStringLiteral("D100"), 4, false).values,
                                  (QVector<quint16>{1, 2, 3, 65535}), kLoadWait);
        QTRY_COMPARE_WITH_TIMEOUT(readBlock(m1, QStringLiteral("M10"), 3, true).values,
                                  (QVector<quint16>{1, 0, 1}), kLoadWait);
        QTRY_COMPARE_WITH_TIMEOUT(readBlock(m2, QStringLiteral("X10"), 4, true).values,
                                  (QVector<quint16>{1, 1, 0, 1}), kLoadWait);

        // Current tabs, HIL inputs.
        QCOMPARE(window.devicePane()->currentIndex(), 2);
        QCOMPARE(window.mockPane()->currentIndex(), 1);
        const HilView* hil = window.hilView();
        QCOMPARE(hil->profilePath(), QStringLiteral("profiles/bench.json"));
        QCOMPARE(hil->planPath(), QStringLiteral("plans/smoke.json"));
        QCOMPARE(hil->groups(), QStringLiteral("G1,G3"));
        QCOMPARE(hil->plcState(), QStringLiteral("STOP"));
        QCOMPARE(hil->outputRoot(), QStringLiteral("captures/out"));
        QCOMPARE(hil->note(), QStringLiteral("bench day 2"));
        QCOMPARE(hil->benchReps(), 7);
        QVERIFY(hil->overwrite());
        QCOMPARE(hil->source(), CaptureSource::VirtualPlc);

        // The dock layout and the window size.
        QCOMPARE(describeLayout(window), layoutBefore);
        QVERIFY(window.dockWidget(QStringLiteral("Properties"))->isClosed());
        QCOMPARE(window.size(), sizeBefore);

        // The workspace is remembered as the current file.
        QCOMPARE(window.workspace()->path(), QFileInfo(file).absoluteFilePath());
        QVERIFY(window.workspace()->recent().paths().contains(QFileInfo(file).absoluteFilePath()));

        // Saving again from the loaded window writes the same workspace (tabs, configs, HIL).
        QJsonObject firstSave = tabsAndHil(window);
        const QString second = QDir(dir).absoluteFilePath(QStringLiteral("bench-again.json"));
        QSignalSpy saved(window.workspace(), &WorkspaceController::saveFinished);
        QVERIFY(window.workspace()->save(second));
        QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 1, kLoadWait);
        Workspace again;
        QVERIFY(workspaceFromBytes(readAll(second), again, problems));
        QCOMPARE(workspaceToJson(again).value(QStringLiteral("devices")),
                 workspaceToJson(expected).value(QStringLiteral("devices")));
        QCOMPARE(workspaceToJson(again).value(QStringLiteral("mocks")),
                 workspaceToJson(expected).value(QStringLiteral("mocks")));
        QCOMPARE(workspaceToJson(again).value(QStringLiteral("hil")),
                 workspaceToJson(expected).value(QStringLiteral("hil")));
        Q_UNUSED(firstSave);

        // The runner threads of the loaded tabs: the McDevice and the mock live on them.
        const quintptr gui = reinterpret_cast<quintptr>(QThread::currentThread());
        for (int i = 0; i < 3; ++i) {
            DeviceTab* tab = window.devicePane()->deviceAt(i);
            QSignalSpy reports(tab->host(), &DeviceHost::threadReport);
            tab->host()->requestThreadReport();
            QTRY_COMPARE_WITH_TIMEOUT(reports.count(), 1, mc::workbench::test::kWait);
            const auto report = reports.last().at(0).value<ThreadReport>();
            QVERIFY(report.deviceThread != 0);
            QVERIFY(report.deviceThread != gui);
            QCOMPARE(report.deviceThread, report.runnerThread);
            QCOMPARE(report.transportThread, report.runnerThread);
        }
        for (int i = 0; i < 2; ++i) {
            MockTab* tab = window.mockPane()->mockAt(i);
            QSignalSpy reports(tab->host(), &MockHost::threadReport);
            tab->host()->requestThreadReport();
            QTRY_COMPARE_WITH_TIMEOUT(reports.count(), 1, mc::workbench::test::kWait);
            const auto report = reports.last().at(0).value<ThreadReport>();
            QVERIFY(report.objectThread != 0);
            QVERIFY(report.objectThread != gui);
            QCOMPARE(report.deviceThread, report.objectThread);
        }
        QCOMPARE(RunnerBase::foreignDeletes(), foreignBefore);
    }

    // Loading a workspace replaces the tabs that were there; the old runner threads are joined.
    void GUI_08_loadReplacesTheExistingTabs() {
        const QString dir = mc::workbench::test::freshOutputDir(QStringLiteral("gui-t073-replace"));
        const QString file = QDir(dir).absoluteFilePath(QStringLiteral("one.json"));

        MainWindow window;
        window.setWorkspaceDialogs(false);
        const int base = RunnerBase::liveRunners(); // the window's own HIL runner
        for (int i = 0; i < 4; ++i) {
            window.devicePane()->addDevice();
        }
        window.mockPane()->addMock();
        QCOMPARE(RunnerBase::liveRunners(), base + 5);

        Workspace ws;
        WorkspaceDevice device;
        device.name = QStringLiteral("only one");
        device.config = tcp3E(4, 5004);
        ws.devices.push_back(device);
        writeAll(file, workspaceToBytes(ws));

        QSignalSpy loaded(window.workspace(), &WorkspaceController::loadFinished);
        QVERIFY(window.workspace()->load(file));
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, kLoadWait);
        QVERIFY2(loaded.first().at(1).toBool(), qPrintable(loaded.first().at(2).toString()));
        QCOMPARE(window.devicePane()->deviceCount(), 1);
        QCOMPARE(window.devicePane()->deviceAt(0)->name(), QStringLiteral("only one"));
        QCOMPARE(window.mockPane()->mockCount(), 0);
        // 4 device tabs and 1 mock were closed (their threads joined), 1 device tab was opened.
        QCOMPARE(RunnerBase::liveRunners(), base + 1);
    }

    // A damaged file is refused with a message, the window stays exactly as it was, and it stays
    // usable: another tab can be added, a good file loads, a save works.
    void GUI_08_aDamagedFileIsRefusedAndTheAppStaysUsable() {
        const QString dir = mc::workbench::test::freshOutputDir(QStringLiteral("gui-t073-damaged"));
        const QString file = QDir(dir).absoluteFilePath(QStringLiteral("damaged.json"));

        // A window with something in it, and a good workspace of its own to compare against.
        MainWindow window;
        window.setWorkspaceDialogs(false);
        QVERIFY(window.devicePane()->addDevice()->setConfig(tcp3E(2, 5002)));
        QVERIFY(window.devicePane()->addDevice()->setConfig(serial3C(9)));
        window.mockPane()->addMock();
        const Workspace good = window.workspace()->capture();
        const QByteArray goodBytes = workspaceToBytes(good);
        QJsonObject json = QJsonDocument::fromJson(goodBytes).object();

        const int liveWithTabs = RunnerBase::liveRunners();
        const QJsonObject before = tabsAndHil(window);
        const QString layoutBefore = describeLayout(window);
        const QByteArray dockBefore = window.dockManager()->saveState();
        const QString titleBefore = window.windowTitle();

        for (const DamagedCase& damaged : damagedCases()) {
        const QString name = QString::fromLatin1(damaged.name);
        const QString expect = QString::fromLatin1(damaged.expect);
        qInfo("damaged file: %s", damaged.what);
        QFile::remove(file);

        // Build the damaged bytes.
        QByteArray bytes = goodBytes;
        const auto edit = [&](const std::function<void(QJsonObject&)>& change) {
            QJsonObject copy = json;
            change(copy);
            bytes = QJsonDocument(copy).toJson();
        };
        const auto device0 = [&](QJsonObject& root) { return root.value(QStringLiteral("devices")).toArray().at(0).toObject(); };
        const auto setDevice0 = [&](QJsonObject& root, const QJsonObject& device) {
            QJsonArray devices = root.value(QStringLiteral("devices")).toArray();
            devices.replace(0, device);
            root.insert(QStringLiteral("devices"), devices);
        };
        const auto mock0 = [&](QJsonObject& root) { return root.value(QStringLiteral("mocks")).toArray().at(0).toObject(); };
        const auto setMock0 = [&](QJsonObject& root, const QJsonObject& mock) {
            QJsonArray mocks = root.value(QStringLiteral("mocks")).toArray();
            mocks.replace(0, mock);
            root.insert(QStringLiteral("mocks"), mocks);
        };
        const auto withPreset = [&](QJsonObject& root, const QJsonObject& preset) {
            QJsonObject mock = mock0(root);
            mock.insert(QStringLiteral("memory"), QJsonArray{preset});
            setMock0(root, mock);
        };
        const auto goodPreset = []() {
            QJsonObject p;
            p.insert(QStringLiteral("head"), QStringLiteral("D100"));
            p.insert(QStringLiteral("kind"), QStringLiteral("words"));
            p.insert(QStringLiteral("values"), QJsonArray{1, 2});
            return p;
        };
        const auto setNested = [](QJsonObject& obj, const QStringList& path, const QJsonValue& value) {
            std::function<QJsonObject(QJsonObject, int)> go = [&](QJsonObject o, int i) {
                if (i == path.size() - 1) {
                    o.insert(path.at(i), value);
                } else {
                    o.insert(path.at(i), go(o.value(path.at(i)).toObject(), i + 1));
                }
                return o;
            };
            obj = go(obj, 0);
        };

        if (name == QLatin1String("truncated")) {
            bytes = goodBytes.left(goodBytes.size() / 2);
        } else if (name == QLatin1String("empty")) {
            bytes.clear();
        } else if (name == QLatin1String("garbage")) {
            bytes = QByteArray("\x00\x01\x02\xFF\xFE garbage", 13);
        } else if (name == QLatin1String("array")) {
            bytes = QByteArray("[1, 2, 3]");
        } else if (name == QLatin1String("format")) {
            edit([](QJsonObject& r) { r.insert(QStringLiteral("format"), QStringLiteral("something-else")); });
        } else if (name == QLatin1String("version")) {
            edit([](QJsonObject& r) { r.insert(QStringLiteral("version"), 2); });
        } else if (name == QLatin1String("noversion")) {
            edit([](QJsonObject& r) { r.remove(QStringLiteral("version")); });
        } else if (name == QLatin1String("port0")) {
            edit([&](QJsonObject& r) {
                QJsonObject d = device0(r);
                QJsonObject cfg = d.value(QStringLiteral("config")).toObject();
                setNested(cfg, {QStringLiteral("transport"), QStringLiteral("tcp"), QStringLiteral("port")}, 0);
                d.insert(QStringLiteral("config"), cfg);
                setDevice0(r, d);
            });
        } else if (name == QLatin1String("wrongtype")) {
            edit([&](QJsonObject& r) {
                QJsonObject d = device0(r);
                QJsonObject cfg = d.value(QStringLiteral("config")).toObject();
                setNested(cfg, {QStringLiteral("frame"), QStringLiteral("timeoutMs")}, QStringLiteral("soon"));
                d.insert(QStringLiteral("config"), cfg);
                setDevice0(r, d);
            });
        } else if (name == QLatin1String("unknowncfg")) {
            edit([&](QJsonObject& r) {
                QJsonObject d = device0(r);
                QJsonObject cfg = d.value(QStringLiteral("config")).toObject();
                setNested(cfg, {QStringLiteral("frame"), QStringLiteral("timeoutMS")}, 5);
                d.insert(QStringLiteral("config"), cfg);
                setDevice0(r, d);
            });
        } else if (name == QLatin1String("unknownmock")) {
            edit([&](QJsonObject& r) {
                QJsonObject m = mock0(r);
                QJsonObject settings = m.value(QStringLiteral("settings")).toObject();
                setNested(settings, {QStringLiteral("Frame"), QStringLiteral("bogus")}, 1);
                m.insert(QStringLiteral("settings"), settings);
                setMock0(r, m);
            });
        } else if (name == QLatin1String("badmockvalue")) {
            edit([&](QJsonObject& r) {
                QJsonObject m = mock0(r);
                QJsonObject settings = m.value(QStringLiteral("settings")).toObject();
                setNested(settings, {QStringLiteral("Errors"), QStringLiteral("unsupportedQna")},
                          QStringLiteral("not hex"));
                m.insert(QStringLiteral("settings"), settings);
                setMock0(r, m);
            });
        } else if (name == QLatin1String("badkind")) {
            edit([&](QJsonObject& r) {
                QJsonObject p = goodPreset();
                p.insert(QStringLiteral("kind"), QStringLiteral("floats"));
                withPreset(r, p);
            });
        } else if (name == QLatin1String("baddevice")) {
            edit([&](QJsonObject& r) {
                QJsonObject p = goodPreset();
                p.insert(QStringLiteral("head"), QStringLiteral("Q99"));
                withPreset(r, p);
            });
        } else if (name == QLatin1String("badbits")) {
            edit([&](QJsonObject& r) {
                QJsonObject p = goodPreset();
                p.insert(QStringLiteral("kind"), QStringLiteral("bits")); // D is a word device
                p.insert(QStringLiteral("values"), QJsonArray{1, 0});
                withPreset(r, p);
            });
        } else if (name == QLatin1String("clampedport")) {
            edit([&](QJsonObject& r) {
                QJsonObject m = mock0(r);
                QJsonObject settings = m.value(QStringLiteral("settings")).toObject();
                setNested(settings, {QStringLiteral("Serving"), QStringLiteral("tcpPort")}, 70000);
                m.insert(QStringLiteral("settings"), settings);
                setMock0(r, m);
            });
        } else if (name == QLatin1String("presetpast")) {
            edit([&](QJsonObject& r) {
                QJsonObject p = goodPreset();
                p.insert(QStringLiteral("head"), QStringLiteral("D16777215"));
                p.insert(QStringLiteral("values"), QJsonArray{1, 2, 3});
                withPreset(r, p);
            });
        } else if (name == QLatin1String("badbase64")) {
            edit([&](QJsonObject& r) {
                QJsonObject layout;
                layout.insert(QStringLiteral("docks"), QStringLiteral("***not base64***"));
                r.insert(QStringLiteral("layout"), layout);
            });
        } else if (name == QLatin1String("badstate")) {
            edit([&](QJsonObject& r) {
                QJsonObject hil = r.value(QStringLiteral("hil")).toObject();
                hil.insert(QStringLiteral("plcState"), QStringLiteral("HALT"));
                r.insert(QStringLiteral("hil"), hil);
            });
        } else if (name == QLatin1String("toomany")) {
            edit([&](QJsonObject& r) {
                QJsonObject p = goodPreset();
                QJsonArray values;
                for (int i = 0; i < kMaxPresetPoints + 1; ++i) {
                    values.append(0);
                }
                p.insert(QStringLiteral("values"), values);
                withPreset(r, p);
            });
        }
        if (name == QLatin1String("huge")) {
            QFile big(file);
            QVERIFY(big.open(QIODevice::WriteOnly));
            QVERIFY(big.resize(kWorkspaceMaxBytes + 1024));
            big.close();
        } else if (name != QLatin1String("missing")) {
            writeAll(file, bytes);
        }

        QSignalSpy loaded(window.workspace(), &WorkspaceController::loadFinished);
        QVERIFY(window.workspace()->load(file));
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, kLoadWait);
        QVERIFY(!loaded.first().at(1).toBool());
        const QString message = loaded.first().at(2).toString();
        QVERIFY2(message.contains(expect), qPrintable(QString::fromLatin1(damaged.what) + QStringLiteral(": ") +
                                                      expect + QStringLiteral("\n---\n") + message));
        QVERIFY2(message.contains(QStringLiteral("Nothing was changed")), qPrintable(message));
        QVERIFY(window.workspaceMessage().startsWith(QStringLiteral("Workspace refused")));

        // The window is exactly as it was; no runner thread of a throw-away tab is left.
        QCOMPARE(tabsAndHil(window), before);
        QCOMPARE(describeLayout(window), layoutBefore);
        QCOMPARE(window.dockManager()->saveState(), dockBefore);
        QCOMPARE(window.windowTitle(), titleBefore);
        QVERIFY(window.workspace()->path().isEmpty());
        QCOMPARE(RunnerBase::liveRunners(), liveWithTabs);
        QVERIFY(!window.workspace()->busy());
        } // for every damaged case

        // Still usable: a tab can be added, a save works, the good file loads.
        QVERIFY(window.devicePane()->addDevice() != nullptr);
        QCOMPARE(window.devicePane()->deviceCount(), 3);
        const QString goodFile = QDir(dir).absoluteFilePath(QStringLiteral("good.json"));
        QSignalSpy saved(window.workspace(), &WorkspaceController::saveFinished);
        QVERIFY(window.workspace()->save(goodFile));
        QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 1, kLoadWait);
        QVERIFY2(saved.first().at(1).toBool(), qPrintable(saved.first().at(2).toString()));
        QSignalSpy again(window.workspace(), &WorkspaceController::loadFinished);
        QVERIFY(window.workspace()->load(goodFile));
        QTRY_COMPARE_WITH_TIMEOUT(again.count(), 1, kLoadWait);
        QVERIFY2(again.first().at(1).toBool(), qPrintable(again.first().at(2).toString()));
        QCOMPARE(window.devicePane()->deviceCount(), 3);
    }

    // Unknown keys are reported with their JSON path, all of them, at every level the loader reads.
    void GUI_08_unknownKeysAreReportedWithTheirPath() {
        Workspace ws;
        ws.devices.push_back({QStringLiteral("a"), tcp3E(3, 5003)});
        WorkspaceMock mock;
        mock.name = QStringLiteral("m");
        mock.presets.push_back(MemoryPreset{QStringLiteral("D0"), false, {1}});
        ws.mocks.push_back(mock);
        ws.hasHil = true;
        ws.hasLayout = true;
        ws.layout.currentDevice = 0;

        QJsonObject root = workspaceToJson(ws);
        const auto addKey = [](QJsonObject& obj, const QString& key) { obj.insert(key, 1); };
        const auto child = [](QJsonObject& obj, const QString& key) { return obj.value(key).toObject(); };

        addKey(root, QStringLiteral("extra"));
        {
            QJsonArray devices = root.value(QStringLiteral("devices")).toArray();
            QJsonObject d = devices.at(0).toObject();
            addKey(d, QStringLiteral("colour"));
            QJsonObject cfg = child(d, QStringLiteral("config"));
            addKey(cfg, QStringLiteral("topLevelCfg"));
            QJsonObject frame = child(cfg, QStringLiteral("frame"));
            addKey(frame, QStringLiteral("timeoutMS"));
            cfg.insert(QStringLiteral("frame"), frame);
            QJsonObject session = child(cfg, QStringLiteral("session"));
            QJsonObject heartbeat = child(session, QStringLiteral("heartbeat"));
            addKey(heartbeat, QStringLiteral("period"));
            session.insert(QStringLiteral("heartbeat"), heartbeat);
            cfg.insert(QStringLiteral("session"), session);
            QJsonObject transport = child(cfg, QStringLiteral("transport"));
            QJsonObject tcp = child(transport, QStringLiteral("tcp"));
            addKey(tcp, QStringLiteral("tcpNoDelayHint"));
            transport.insert(QStringLiteral("tcp"), tcp);
            cfg.insert(QStringLiteral("transport"), transport);
            QJsonArray subs = cfg.value(QStringLiteral("subscriptions")).toArray();
            QJsonObject sub = subs.at(0).toObject();
            addKey(sub, QStringLiteral("rate"));
            subs.replace(0, sub);
            cfg.insert(QStringLiteral("subscriptions"), subs);
            d.insert(QStringLiteral("config"), cfg);
            devices.replace(0, d);
            root.insert(QStringLiteral("devices"), devices);
        }
        {
            QJsonArray mocks = root.value(QStringLiteral("mocks")).toArray();
            QJsonObject m = mocks.at(0).toObject();
            addKey(m, QStringLiteral("serving"));
            QJsonArray memory = m.value(QStringLiteral("memory")).toArray();
            QJsonObject p = memory.at(0).toObject();
            addKey(p, QStringLiteral("endian"));
            memory.replace(0, p);
            m.insert(QStringLiteral("memory"), memory);
            mocks.replace(0, m);
            root.insert(QStringLiteral("mocks"), mocks);
        }
        {
            QJsonObject hil = child(root, QStringLiteral("hil"));
            addKey(hil, QStringLiteral("colour"));
            root.insert(QStringLiteral("hil"), hil);
            QJsonObject layout = child(root, QStringLiteral("layout"));
            addKey(layout, QStringLiteral("zoom"));
            root.insert(QStringLiteral("layout"), layout);
        }

        Workspace out;
        QVector<WorkspaceProblem> problems;
        QVERIFY(!workspaceFromJson(root, out, problems));
        QVERIFY(out.devices.isEmpty());
        QStringList paths;
        for (const WorkspaceProblem& p : problems) {
            QCOMPARE(p.message, QStringLiteral("unknown key"));
            paths.append(p.path);
        }
        paths.sort();
        QStringList expected = {QStringLiteral("extra"),
                                QStringLiteral("devices[0].colour"),
                                QStringLiteral("devices[0].config.topLevelCfg"),
                                QStringLiteral("devices[0].config.frame.timeoutMS"),
                                QStringLiteral("devices[0].config.session.heartbeat.period"),
                                QStringLiteral("devices[0].config.transport.tcp.tcpNoDelayHint"),
                                QStringLiteral("devices[0].config.subscriptions[0].rate"),
                                QStringLiteral("mocks[0].serving"),
                                QStringLiteral("mocks[0].memory[0].endian"),
                                QStringLiteral("hil.colour"),
                                QStringLiteral("layout.zoom")};
        expected.sort();
        QCOMPARE(paths, expected);

        // The same text reaches the user: every path in the message of a refused load.
        const QString text = workspaceProblemsText(problems, 50);
        for (const QString& path : expected) {
            QVERIFY2(text.contains(path + QStringLiteral(": unknown key")), qPrintable(path));
        }
        // Without the unknown keys the same file is good.
        QVERIFY(workspaceFromJson(workspaceToJson(ws), out, problems));
        QVERIFY(problems.isEmpty());
        QCOMPARE(out.devices.size(), 1);
    }

    // The value round trip, independent of any window.
    void Workspace_jsonRoundTrip() {
        Workspace ws;
        ws.devices.push_back({QStringLiteral("PLC 1"), tcp3E(5, 5005)});
        ws.devices.push_back({QStringLiteral("PLC 2"), serial3C(11)});
        WorkspaceMock mock;
        mock.name = QStringLiteral("Mock 1");
        mock.settings.insert(QStringLiteral("Frame"), QJsonObject{{QStringLiteral("frame"), 1}});
        mock.presets.push_back(MemoryPreset{QStringLiteral("D5"), false, {0, 65535}});
        mock.presets.push_back(MemoryPreset{QStringLiteral("M0"), true, {1, 0, 0, 1}});
        ws.mocks.push_back(mock);
        ws.hasHil = true;
        ws.hil.source = QStringLiteral("plc");
        ws.hil.benchReps = 3;
        ws.hasLayout = true;
        ws.layout.dockState = QByteArray("\x00\x01 state \xFF", 10);
        ws.layout.windowGeometry = QByteArray("geometry");
        ws.layout.currentDevice = 1;
        ws.layout.currentMock = 0;

        Workspace out;
        QVector<WorkspaceProblem> problems;
        QVERIFY2(workspaceFromBytes(workspaceToBytes(ws), out, problems),
                 qPrintable(workspaceProblemsText(problems)));
        QCOMPARE(out.devices.size(), 2);
        for (int i = 0; i < 2; ++i) {
            QCOMPARE(out.devices.at(i).name, ws.devices.at(i).name);
            QCOMPARE(out.devices.at(i).config.toJson(), ws.devices.at(i).config.toJson());
        }
        QCOMPARE(out.mocks.at(0).settings, mock.settings);
        QCOMPARE(out.mocks.at(0).presets, mock.presets);
        QVERIFY(out.hasHil);
        QCOMPARE(out.hil.source, QStringLiteral("plc"));
        QCOMPARE(out.hil.benchReps, 3);
        QCOMPARE(out.layout.dockState, ws.layout.dockState);
        QCOMPARE(out.layout.windowGeometry, ws.layout.windowGeometry);
        QCOMPARE(out.layout.currentDevice, 1);
        QCOMPARE(out.layout.currentMock, 0);

        // Only "format" and "version" are required: an empty workspace is good.
        QVERIFY(workspaceFromBytes(R"({"format":"mc-workbench-workspace","version":1})", out, problems));
        QVERIFY(out.devices.isEmpty() && out.mocks.isEmpty() && !out.hasHil && !out.hasLayout);
    }

    // Every problem is collected, with a path, not only the first.
    void Workspace_allProblemsAreCollected() {
        const QByteArray bytes = R"({
            "format": "mc-workbench-workspace", "version": 1,
            "devices": [ {"config": {}}, {"name": "", "config": {"frame": {"timeoutMs": -5}}}, 7 ],
            "mocks": [ {"name": "m", "settings": 3} ],
            "hil": {"benchReps": 0, "source": "radio"}
        })";
        Workspace out;
        QVector<WorkspaceProblem> problems;
        QVERIFY(!workspaceFromBytes(bytes, out, problems));
        QStringList paths;
        for (const WorkspaceProblem& p : problems) {
            paths.append(p.path);
        }
        QVERIFY2(paths.contains(QStringLiteral("devices[0].name")), qPrintable(paths.join(',')));
        QVERIFY2(paths.contains(QStringLiteral("devices[1].name")), qPrintable(paths.join(',')));
        QVERIFY2(paths.contains(QStringLiteral("devices[2]")), qPrintable(paths.join(',')));
        QVERIFY2(paths.contains(QStringLiteral("mocks[0].settings")), qPrintable(paths.join(',')));
        QVERIFY2(paths.contains(QStringLiteral("hil.benchReps")), qPrintable(paths.join(',')));
        QVERIFY2(paths.contains(QStringLiteral("hil.source")), qPrintable(paths.join(',')));
        QVERIFY(workspaceProblemsText(problems, 2).contains(QStringLiteral("... and")));
    }

    // The file work is off the GUI thread: while a big workspace is read and parsed, the GUI thread's
    // event loop keeps running, and the two file operations exclude each other.
    void GUI_08_fileWorkStaysOffTheGuiThread() {
        const QString dir = mc::workbench::test::freshOutputDir(QStringLiteral("gui-t073-big"));
        const QString file = QDir(dir).absoluteFilePath(QStringLiteral("big.json"));

        Workspace ws;
        WorkspaceMock mock;
        mock.name = QStringLiteral("big");
        for (int i = 0; i < 8; ++i) {
            MemoryPreset preset;
            preset.head = QStringLiteral("D%1").arg(i * 100);
            preset.values.resize(kMaxPresetPoints);
            for (int k = 0; k < preset.values.size(); ++k) {
                preset.values[k] = static_cast<quint16>(k * 7 + i);
            }
            mock.presets.push_back(preset);
        }
        ws.mocks.push_back(mock);
        const QByteArray bytes = workspaceToBytes(ws);
        QVERIFY(bytes.size() > 2 * 1024 * 1024);
        writeAll(file, bytes);

        MainWindow window;
        window.setWorkspaceDialogs(false);
        Ticker ticker;
        QSignalSpy loaded(window.workspace(), &WorkspaceController::loadFinished);
        QVERIFY(window.workspace()->load(file));
        QVERIFY(window.workspace()->busy());
        // Another operation while one runs is refused and starts nothing.
        QVERIFY(!window.workspace()->load(file));
        QVERIFY(!window.workspace()->save(QDir(dir).absoluteFilePath(QStringLiteral("refused.json"))));
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, kLoadWait);
        QVERIFY2(loaded.first().at(1).toBool(), qPrintable(loaded.first().at(2).toString()));
        QVERIFY2(ticker.maxGapMs() < 250, qPrintable(QString::number(ticker.maxGapMs())));
        QVERIFY(!QFileInfo::exists(QDir(dir).absoluteFilePath(QStringLiteral("refused.json"))));
        QCOMPARE(window.mockPane()->mockCount(), 1);
        QCOMPARE(window.mockPane()->mockAt(0)->memoryPresets().size(), 8);

        // Save of the same size: the GUI keeps running, the file is complete, no temporary is left.
        ticker.restart();
        const QString out = QDir(dir).absoluteFilePath(QStringLiteral("big-out.json"));
        QSignalSpy saved(window.workspace(), &WorkspaceController::saveFinished);
        QVERIFY(window.workspace()->save(out));
        QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 1, kLoadWait);
        QVERIFY2(saved.first().at(1).toBool(), qPrintable(saved.first().at(2).toString()));
        QVERIFY2(ticker.maxGapMs() < 250, qPrintable(QString::number(ticker.maxGapMs())));
        Workspace back;
        QVector<WorkspaceProblem> problems;
        QVERIFY(workspaceFromBytes(readAll(out), back, problems));
        QCOMPARE(back.mocks.at(0).presets, mock.presets);
        QStringList left = QDir(dir).entryList(QDir::Files);
        left.sort();
        QCOMPARE(left, (QStringList{QStringLiteral("big-out.json"), QStringLiteral("big.json")}));
    }

    // A save that cannot be written is reported, leaves an existing file untouched and the window usable.
    void GUI_08_aFailedSaveIsReportedAndKeepsTheOldFile() {
        const QString dir = mc::workbench::test::freshOutputDir(QStringLiteral("gui-t073-failedsave"));
        const QString file = QDir(dir).absoluteFilePath(QStringLiteral("keep.json"));
        MainWindow window;
        window.setWorkspaceDialogs(false);
        window.devicePane()->addDevice();

        QSignalSpy saved(window.workspace(), &WorkspaceController::saveFinished);
        QVERIFY(window.workspace()->save(file));
        QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 1, kLoadWait);
        const QByteArray kept = readAll(file);
        QVERIFY(!kept.isEmpty());

        // A folder that does not exist.
        const QString nowhere = QDir(dir).absoluteFilePath(QStringLiteral("no/such/folder/x.json"));
        QVERIFY(window.workspace()->save(nowhere));
        QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 2, kLoadWait);
        QVERIFY(!saved.at(1).at(1).toBool());
        QVERIFY(saved.at(1).at(2).toString().contains(QStringLiteral("cannot write")));
        QCOMPARE(window.workspace()->path(), QFileInfo(file).absoluteFilePath()); // unchanged
        QCOMPARE(readAll(file), kept);
        QVERIFY(window.workspaceMessage().contains(QStringLiteral("cannot write")));
        QVERIFY(!window.workspace()->busy());

        // A folder where the file should be.
        const QString asFolder = QDir(dir).absoluteFilePath(QStringLiteral("folder.json"));
        QVERIFY(QDir().mkpath(asFolder));
        QVERIFY(window.workspace()->save(asFolder));
        QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 3, kLoadWait);
        QVERIFY(!saved.at(2).at(1).toBool());
        QVERIFY(window.devicePane()->addDevice() != nullptr);
    }

    // The recent list holds paths only, most recent first, bounded, and survives in its INI file.
    void RecentFiles_orderBoundAndStore() {
        const QString dir = mc::workbench::test::freshOutputDir(QStringLiteral("gui-t073-recent"));
        const QString ini = QDir(dir).absoluteFilePath(QStringLiteral("recent.ini"));
        RecentFiles memoryOnly;
        memoryOnly.add(QDir(dir).absoluteFilePath(QStringLiteral("a.json")));
        QCOMPARE(memoryOnly.paths().size(), 1);
        QVERIFY(!QFileInfo::exists(ini));

        RecentFiles list;
        list.setStorePath(ini);
        QVERIFY(list.paths().isEmpty());
        for (int i = 0; i < RecentFiles::kMaxEntries + 3; ++i) {
            list.add(QDir(dir).absoluteFilePath(QStringLiteral("w%1.json").arg(i)));
        }
        QCOMPARE(list.paths().size(), RecentFiles::kMaxEntries);
        QCOMPARE(list.paths().first(), QDir(dir).absoluteFilePath(QStringLiteral("w10.json")));
        // Adding an old one moves it to the front, without a duplicate.
        const QString old = list.paths().at(3);
        list.add(old);
        QCOMPARE(list.paths().first(), old);
        QCOMPARE(list.paths().count(old), 1);
        QCOMPARE(list.paths().size(), RecentFiles::kMaxEntries);

        RecentFiles reopened;
        reopened.setStorePath(ini);
        QCOMPARE(reopened.paths(), list.paths());
        reopened.remove(old);
        QVERIFY(!reopened.paths().contains(old));
        RecentFiles third;
        third.setStorePath(ini);
        QVERIFY(!third.paths().contains(old));
        third.clear();
        RecentFiles fourth;
        fourth.setStorePath(ini);
        QVERIFY(fourth.paths().isEmpty());
    }

    // File menu: recent entries open workspaces; nothing about a device is stored but file paths.
    void GUI_08_recentMenuAndNoAddressLeavesTheWorkspaceFile() {
        const QString dir = mc::workbench::test::freshOutputDir(QStringLiteral("gui-t073-menu"));
        const QString ini = QDir(dir).absoluteFilePath(QStringLiteral("recent.ini"));
        const QString file = QDir(dir).absoluteFilePath(QStringLiteral("net.json"));

        {
            MainWindow window;
            window.setWorkspaceDialogs(false);
            window.workspace()->recent().setStorePath(ini);
            QVERIFY(window.devicePane()->addDevice()->setConfig(tcp3E(21, 5021)));
            QVERIFY(window.devicePane()->addDevice()->setConfig(serial3C(12)));
            QSignalSpy saved(window.workspace(), &WorkspaceController::saveFinished);
            QVERIFY(window.workspace()->save(file));
            QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 1, kLoadWait);
            QVERIFY(saved.first().at(1).toBool());
        }
        // The workspace file holds the address and the port name (the user asked to save them) ...
        const QByteArray fileText = readAll(file);
        QVERIFY(fileText.contains(sampleHost(21).toUtf8()));
        QVERIFY(fileText.contains(sampleCom(12).toUtf8()));
        // ... and nothing else in the output folder does, the recent list included.
        const QDir out(dir);
        for (const QFileInfo& info : out.entryInfoList(QDir::Files)) {
            if (info.fileName() == QLatin1String("net.json")) {
                continue;
            }
            const QByteArray text = readAll(info.absoluteFilePath());
            QVERIFY2(!text.contains(sampleHost(21).toUtf8()), qPrintable(info.fileName()));
            QVERIFY2(!text.contains(sampleCom(12).toUtf8()), qPrintable(info.fileName()));
        }
        QVERIFY(readAll(ini).contains("net.json"));

        MainWindow window;
        window.setWorkspaceDialogs(false);
        window.workspace()->recent().setStorePath(ini);
        QMenu* fileMenu = nullptr;
        for (QAction* action : window.menuBar()->actions()) {
            if (action->menu() != nullptr && action->text().remove(QLatin1Char('&')) == QLatin1String("File")) {
                fileMenu = action->menu();
            }
        }
        QVERIFY(fileMenu != nullptr);
        QMenu* recent = nullptr;
        QStringList titles;
        for (QAction* action : fileMenu->actions()) {
            titles.append(action->text().remove(QLatin1Char('&')));
            if (action->menu() != nullptr) {
                recent = action->menu();
            }
        }
        QVERIFY2(titles.contains(QStringLiteral("Open workspace...")), qPrintable(titles.join('|')));
        QVERIFY2(titles.contains(QStringLiteral("Save workspace")), qPrintable(titles.join('|')));
        QVERIFY2(titles.contains(QStringLiteral("Save workspace as...")), qPrintable(titles.join('|')));
        QVERIFY(recent != nullptr);
        emit recent->aboutToShow();
        QAction* entry = nullptr;
        for (QAction* action : recent->actions()) {
            if (action->text() == QFileInfo(file).absoluteFilePath()) {
                entry = action;
            }
        }
        QVERIFY(entry != nullptr);
        QSignalSpy loaded(window.workspace(), &WorkspaceController::loadFinished);
        entry->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, kLoadWait);
        QVERIFY2(loaded.first().at(1).toBool(), qPrintable(loaded.first().at(2).toString()));
        QCOMPARE(window.devicePane()->deviceCount(), 2);
        QCOMPARE(window.devicePane()->deviceAt(0)->binding()->config().tcp.host, sampleHost(21));
        QVERIFY(window.windowTitle().contains(QStringLiteral("net.json")));
        QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("Workspace loaded")));
    }

    // An empty window saves and loads as an empty workspace; a workspace without layout keeps the layout.
    void GUI_08_aWorkspaceWithoutLayoutKeepsTheWindowAsItIs() {
        const QString dir = mc::workbench::test::freshOutputDir(QStringLiteral("gui-t073-nolayout"));
        const QString file = QDir(dir).absoluteFilePath(QStringLiteral("nolayout.json"));
        Workspace ws;
        ws.devices.push_back({QStringLiteral("x"), tcp3E(6, 5006)});
        writeAll(file, workspaceToBytes(ws));

        MainWindow window;
        window.setWorkspaceDialogs(false);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.dockWidget(QStringLiteral("Properties"))->toggleView(false);
        const QString layout = describeLayout(window);
        QSignalSpy loaded(window.workspace(), &WorkspaceController::loadFinished);
        QVERIFY(window.workspace()->load(file));
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, kLoadWait);
        QVERIFY2(loaded.first().at(1).toBool(), qPrintable(loaded.first().at(2).toString()));
        QCOMPARE(describeLayout(window), layout);
        QCOMPARE(window.devicePane()->deviceCount(), 1);
    }
};

namespace mc::workbench::test {
QObject* makeWorkspaceSuite() {
    return new WorkspaceTest;
}
} // namespace mc::workbench::test

#include "tst_gui_workspace.moc"
