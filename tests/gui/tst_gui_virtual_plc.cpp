// Checkpoint H (T-074): the GUI against examples/virtual_plc, the stand-alone PLC process (the other
// target of "proven against MockPlc and virtual_plc"). virtual_plc is started on loopback with an
// OS-chosen port and always stopped again (the helper kills it in its destructor, also when a case
// fails). It stays unchanged; each TCP connection gets its own MockPlc.
//
//   - a device tab connects to it, subscribes, sees the starting image and a wiggling device change,
//     and an ad-hoc write reads back;
//   - the HIL view runs the whole tests/hil/e2e/plan_3e.json, E-10 (truncated frame, reconnect)
//     included, and the capture it writes replays green.
// No real PLC: every target is a process or a listener on loopback.
#include "gui_suites.h"
#include "gui_test_support.h"

#include "mc_workbench/console_model.h"
#include "mc_workbench/device_tab.h"
#include "mc_workbench/hil_confirm_dialog.h"
#include "mc_workbench/hil_host.h"
#include "mc_workbench/hil_view.h"
#include "mc_workbench/point_table_model.h"
#include "mc_workbench/runner_base.h"
#include "mc_workbench/runner_thread.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <QTimer>

#include <functional>
#include <memory>

using namespace mc::workbench;
using namespace mc::workbench::test;

namespace {

constexpr int kRunWaitMs = 60000;

// A virtual_plc process that the test starts and stops.
class VirtualPlcProcess {
public:
    ~VirtualPlcProcess() { stop(); }

    // Empty on success, else why it could not start (the case then skips).
    QString start(const QStringList& args) {
        const QString path = QStringLiteral(MC_VIRTUAL_PLC_PATH);
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            return QStringLiteral("examples/virtual_plc is not built here (%1)").arg(path);
        }
        m_process = std::make_unique<QProcess>();
        m_process->setProcessChannelMode(QProcess::MergedChannels);
        QObject::connect(m_process.get(), &QProcess::readyReadStandardOutput, m_process.get(),
                         [this]() { m_output += m_process->readAllStandardOutput(); });
        m_process->start(path, args);
        if (!m_process->waitForStarted(5000)) {
            return QStringLiteral("virtual_plc did not start: %1").arg(m_process->errorString());
        }
        static const QRegularExpression listening(QStringLiteral("listening on 127\\.0\\.0\\.1:(\\d+)"));
        for (int i = 0; i < 100; ++i) {
            m_process->waitForReadyRead(100);
            QCoreApplication::processEvents();
            const QRegularExpressionMatch m = listening.match(QString::fromUtf8(m_output));
            if (m.hasMatch()) {
                m_port = static_cast<quint16>(m.captured(1).toUInt());
                return QString();
            }
            if (m_process->state() != QProcess::Running) {
                break;
            }
        }
        return QStringLiteral("virtual_plc did not report that it listens: %1").arg(QString::fromUtf8(m_output));
    }

    void stop() {
        if (m_process && m_process->state() != QProcess::NotRunning) {
            m_process->kill();
            m_process->waitForFinished(5000);
        }
    }

    bool running() const { return m_process && m_process->state() == QProcess::Running; }
    quint16 port() const { return m_port; }

private:
    std::unique_ptr<QProcess> m_process;
    QByteArray m_output;
    quint16 m_port{0};
};

QString sourceTestsDir() {
    return QStringLiteral(MC_TESTS_SOURCE_DIR);
}

QJsonObject readJsonFile(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}

QString readText(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

// A copy of an example profile aimed at a loopback port, with short timeouts and rounds.
QString writeVpProfile(const QString& dir, const QString& newId, quint16 port) {
    QJsonObject root = readJsonFile(sourceTestsDir() + QStringLiteral("/hil/profiles/q03ude-eth-3e-bin.example.json"));
    QJsonObject profile = root.value(QStringLiteral("profile")).toObject();
    profile.insert(QStringLiteral("id"), newId);
    root.insert(QStringLiteral("profile"), profile);
    QJsonObject device = root.value(QStringLiteral("device")).toObject();
    QJsonObject frame = device.value(QStringLiteral("frame")).toObject();
    frame.insert(QStringLiteral("timeoutMs"), 400);
    device.insert(QStringLiteral("frame"), frame);
    QJsonObject session = device.value(QStringLiteral("session")).toObject();
    session.insert(QStringLiteral("cycleIntervalMs"), 20);
    device.insert(QStringLiteral("session"), session);
    QJsonObject transport = device.value(QStringLiteral("transport")).toObject();
    QJsonObject tcp = transport.value(QStringLiteral("tcp")).toObject();
    tcp.insert(QStringLiteral("host"), QStringLiteral("127.0.0.1"));
    tcp.insert(QStringLiteral("port"), static_cast<int>(port));
    tcp.insert(QStringLiteral("connectTimeoutMs"), 1000);
    transport.insert(QStringLiteral("tcp"), tcp);
    device.insert(QStringLiteral("transport"), transport);
    root.insert(QStringLiteral("device"), device);
    const QString path = dir + QStringLiteral("/") + newId + QStringLiteral(".json");
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(root).toJson());
    }
    return path;
}

// Drives the modal confirmation dialog that requestRun() opens, once it is on screen.
class DialogHand {
public:
    explicit DialogHand(std::function<void(HilConfirmDialog*)> action) : m_action(std::move(action)) {
        m_timer.setInterval(10);
        QObject::connect(&m_timer, &QTimer::timeout, &m_timer, [this]() {
            auto* dialog = qobject_cast<HilConfirmDialog*>(QApplication::activeModalWidget());
            if (dialog != nullptr) {
                m_timer.stop();
                m_seen = true;
                m_action(dialog);
            }
        });
        m_timer.start();
    }
    bool seen() const { return m_seen; }

private:
    std::function<void(HilConfirmDialog*)> m_action;
    QTimer m_timer;
    bool m_seen{false};
};

} // namespace

class VirtualPlcGuiTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { registerRunnerMetaTypes(); }

    // The program itself (not only its libraries) starts and stays up: a link that picked another
    // tool's main.cpp (jom, T-074) would print that tool's usage and exit at once.
    void GUI_09_theProgramStartsAndStaysUp() {
        const QString path = QStringLiteral(MC_WORKBENCH_PATH);
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            QSKIP(qPrintable(QStringLiteral("mc_workbench is not built here (%1)").arg(path)));
        }
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        // The recent-files folder goes into the build tree, never under the user profile.
        const QString configDir = freshOutputDir(QStringLiteral("gui-t074-config"));
        environment.insert(QStringLiteral("MC_WORKBENCH_CONFIG_DIR"), configDir);
        QProcess program;
        program.setProcessEnvironment(environment);
        program.setProcessChannelMode(QProcess::MergedChannels);
        program.start(path, QStringList());
        QVERIFY(program.waitForStarted(5000));
        QTest::qWait(2500);
        const bool stillRunning = program.state() == QProcess::Running;
        const QString output = QString::fromUtf8(program.readAll());
        program.kill();
        program.waitForFinished(5000);
        QVERIFY2(stillRunning, qPrintable(QStringLiteral("exited early: ") + output));
    }

    // A device tab connects to a started virtual_plc, subscribes, sees the starting image and a
    // device that virtual_plc changes by itself, and reads back an ad-hoc write.
    void VirtualPlc_aDeviceTabConnectsSubscribesAndReadsBack() {
        VirtualPlcProcess plc;
        const QString why = plc.start({QStringLiteral("--frame"), QStringLiteral("3E"), QStringLiteral("--code"),
                                       QStringLiteral("Binary"), QStringLiteral("--port"), QStringLiteral("0"),
                                       QStringLiteral("--set"), QStringLiteral("D100=11"),
                                       QStringLiteral("--set"), QStringLiteral("D101=22"),
                                       QStringLiteral("--set"), QStringLiteral("D102=33"),
                                       QStringLiteral("--set"), QStringLiteral("D103=44"),
                                       QStringLiteral("--set"), QStringLiteral("M1=1"),
                                       QStringLiteral("--wiggle"), QStringLiteral("D105")});
        if (!why.isEmpty()) {
            QSKIP(qPrintable(why));
        }
        QVERIFY(plc.port() != 0);

        mc::McDeviceConfig cfg = deviceConfig(plc.port());
        cfg.subscriptions = {{QStringLiteral("D100"), 6}, {QStringLiteral("M0"), 8}};
        DeviceTab tab(QStringLiteral("vp-tab"));
        QSignalSpy applied(&tab, &DeviceTab::configApplied);
        QString error;
        QVERIFY2(tab.setConfig(cfg, &error), qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, kWait);
        QVERIFY(applied.last().at(0).toBool());

        tab.connectToPlc();
        QTRY_COMPARE_WITH_TIMEOUT(tab.linkState(), mc::LinkState::Connected, kWait);

        PointTableModel* points = tab.points();
        const auto row = [&](const char* device) { return points->rowOfDevice(QLatin1String(device)); };
        QTRY_VERIFY_WITH_TIMEOUT(row("D105") >= 0 && row("M7") >= 0 && points->valueAt(row("D103")) == 44, kWait);
        QCOMPARE(points->valueAt(row("D100")), static_cast<quint16>(11));
        QCOMPARE(points->valueAt(row("D101")), static_cast<quint16>(22));
        QCOMPARE(points->valueAt(row("D102")), static_cast<quint16>(33));
        QCOMPARE(points->valueAt(row("M1")), static_cast<quint16>(1));
        QCOMPARE(points->valueAt(row("M0")), static_cast<quint16>(0));
        QCOMPARE(points->stateAt(row("D100")), mc::PointState::Valid);
        QCOMPARE(points->rowCount(), 6 + 8);

        // virtual_plc counts D105 up once a second: the tab sees it change.
        QTRY_VERIFY_WITH_TIMEOUT(points->valueAt(row("D105")) >= 2, kWait);

        // An ad-hoc write reads back (the connection has its own mock behind it).
        const quint64 writeToken = tab.sendWrite(false, QStringLiteral("D200"), QStringLiteral("7 8 9"), &error);
        QVERIFY2(writeToken != 0, qPrintable(error));
        ConsoleModel* console = tab.console();
        QTRY_VERIFY_WITH_TIMEOUT(console->rowOfToken(writeToken) >= 0 &&
                                     console->entry(console->rowOfToken(writeToken)).state == ConsoleEntry::State::Ok,
                                 kWait);
        const quint64 readToken = tab.sendRead(false, QStringLiteral("D200"), 3, &error);
        QVERIFY2(readToken != 0, qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(console->rowOfToken(readToken) >= 0 &&
                                     console->entry(console->rowOfToken(readToken)).state == ConsoleEntry::State::Ok,
                                 kWait);
        QCOMPARE(console->entry(console->rowOfToken(readToken)).result, QStringLiteral("ok: 7 8 9"));

        // The process going away is a link fault in the tab, not a crash; the tab stays usable.
        plc.stop();
        QVERIFY(!plc.running());
        QTRY_VERIFY_WITH_TIMEOUT(tab.linkState() != mc::LinkState::Connected, kWait);
        QCOMPARE(points->stateAt(row("D100")), mc::PointState::Stale);
    }

    // The HIL view runs the full e2e plan, E-10 included, against virtual_plc and the capture replays.
    void VirtualPlc_theHilViewRunsPlan3EWithE10AndTheCaptureReplaysGreen() {
        VirtualPlcProcess plc;
        const QString why = plc.start({QStringLiteral("--frame"), QStringLiteral("3E"), QStringLiteral("--code"),
                                       QStringLiteral("Binary"), QStringLiteral("--port"), QStringLiteral("0"),
                                       QStringLiteral("--set"), QStringLiteral("D100=1234")});
        if (!why.isEmpty()) {
            QSKIP(qPrintable(why));
        }
        const QString dir = freshOutputDir(QStringLiteral("gui-t074-vp-hil"));
        const QString root = dir + QStringLiteral("/captured");

        HilView view;
        view.setProfilePath(writeVpProfile(dir, QStringLiteral("gui-vp-3e"), plc.port()));
        view.setPlanPath(sourceTestsDir() + QStringLiteral("/hil/e2e/plan_3e.json")); // the full plan, E-10 in it
        view.setOutputRoot(root);
        view.setBenchReps(3);
        view.setNote(QStringLiteral("T-074 run against virtual_plc"));
        view.setCapturedRoot(freshOutputDir(QStringLiteral("gui-t074-vp-fake")) +
                             QStringLiteral("/tests/vectors/captured"));
        QSignalSpy checked(&view, &HilView::checkDone);
        view.check();
        QTRY_COMPARE_WITH_TIMEOUT(checked.count(), 1, kWait);
        QVERIFY2(view.lastCheck().ok(), qPrintable(view.gateText()));
        QVERIFY(view.lastCheck().loopbackTcp);
        view.setSource(CaptureSource::VirtualPlc);
        QVERIFY(view.canRun());

        Ticker ticker;
        QSignalSpy done(&view, &HilView::runDone);
        {
            DialogHand hand([&](HilConfirmDialog* d) {
                d->typeId(QStringLiteral("gui-vp-3e"));
                d->okButton()->click();
            });
            QVERIFY(view.requestRun() != 0);
            QVERIFY(hand.seen());
        }
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, kRunWaitMs);
        const HilRunResult run = view.lastRun();
        const QString lines = run.lines.join(QLatin1Char('\n'));
        QVERIFY2(run.status == HilRunStatus::Finished, qPrintable(run.reason + lines));
        // The whole plan: 14 steps pass (E-10 among them), E-15 is skipped (no scan time).
        QVERIFY2(run.passed == 14 && run.failed == 0 && run.diverged == 0 && run.notSupported == 0 &&
                     run.skipped == 1,
                 qPrintable(lines));
        QCOMPARE(run.exitCode(), 0);
        QVERIFY(!run.onGuiThread);
        QCOMPARE(run.threadName, QStringLiteral("hil-runner"));
        QCOMPARE(view.outcomeRows(), 15);
        bool sawE10 = false;
        for (int r = 0; r < view.outcomeRows(); ++r) {
            if (view.outcomeCell(r, 0) == QLatin1String("E-10")) {
                sawE10 = true;
                QCOMPARE(view.outcomeCell(r, 1), QStringLiteral("PASS"));
            }
        }
        QVERIFY(sawE10);
        QVERIFY2(ticker.maxGapMs() < 250, qPrintable(QString::number(ticker.maxGapMs())));
        QVERIFY(plc.running()); // a truncated frame and a reconnect do not kill virtual_plc

        // The capture: marked as virtual_plc, with E-10's records, never under tests/vectors/captured.
        const QString folder = run.folder;
        QVERIFY(folder.endsWith(QStringLiteral("gui-vp-3e")));
        for (const char* name : {"run.meta", "steps.vec", "session.vec", "bench.csv"}) {
            QVERIFY2(QFileInfo::exists(folder + QStringLiteral("/") + QLatin1String(name)), name);
        }
        const QString steps = readText(folder + QStringLiteral("/steps.vec"));
        QVERIFY(steps.contains(QStringLiteral("# source: virtual_plc  profile: gui-vp-3e")));
        QVERIFY(!steps.contains(QStringLiteral("# source: plc")));
        QVERIFY(steps.contains(QStringLiteral("CAP-gui-vp-3e-E-10")));
        QVERIFY(steps.contains(QStringLiteral("CAP-gui-vp-3e-E-10.2-R")));
        const QString meta = readText(folder + QStringLiteral("/run.meta"));
        QVERIFY(meta.contains(QStringLiteral("capture_source: virtual_plc")));
        QVERIFY(meta.contains(QStringLiteral("not hardware")));
        QVERIFY(!QDir(sourceTestsDir() + QStringLiteral("/vectors/captured/gui-vp-3e")).exists());

        // It replays green (RPL-01..06).
        int exitCode = 0;
        const QString output = runReplay(root, &exitCode);
        if (exitCode == -1) {
            QSKIP(qPrintable(output));
        }
        QVERIFY2(exitCode == 0, qPrintable(output));
        QVERIFY2(output.contains(QStringLiteral("replayed gui-vp-3e")), qPrintable(output));
    }
};

namespace mc::workbench::test {

QObject* makeVirtualPlcSuite() {
    return new VirtualPlcGuiTest;
}

} // namespace mc::workbench::test

#include "tst_gui_virtual_plc.moc"
