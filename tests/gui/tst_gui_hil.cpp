// GUI-07 (SPEC-gui-tool.md "Testing"): the HIL runner view. The gate decisions and the dry runs equal
// those of hil_capture on every committed plan and example profile; a refused plan sends nothing; the
// confirmation for read-only frames needs the typed profile id and cannot be skipped; a run against a
// MockRunner writes a capture that replays green. No real PLC: every target is a mock or a listener
// on loopback.
#include "gui_suites.h"
#include "gui_test_support.h"

#include "hil_capture/options.h"
#include "hil_capture/tool.h"
#include "mc_workbench/hil_confirm_dialog.h"
#include "mc_workbench/hil_host.h"
#include "mc_workbench/hil_prepare.h"
#include "mc_workbench/hil_view.h"
#include "mc_workbench/main_window.h"
#include "mc_workbench/runner_base.h"
#include "mc_workbench/runner_thread.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTest>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#include <functional>
#include <memory>

using namespace mc::workbench;
using namespace mc::workbench::test;

namespace {

constexpr int kRunWait = 60000;

QString testsDir() {
    return QStringLiteral(MC_TESTS_SOURCE_DIR);
}

QString exampleProfile(const QString& id) {
    return testsDir() + QStringLiteral("/hil/profiles/") + id + QStringLiteral(".example.json");
}

QString e2ePlan(const char* name) {
    return testsDir() + QStringLiteral("/hil/e2e/") + QLatin1String(name);
}

QJsonObject readJson(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object()
                                       : QJsonObject();
}

// A copy of an example profile aimed at a loopback port, with short timeouts and rounds.
QString writeProfile(const QString& dir, const QString& exampleId, const QString& newId,
                     quint16 port, int timeoutMs = 400) {
    QJsonObject root = readJson(exampleProfile(exampleId));
    QJsonObject profile = root.value(QStringLiteral("profile")).toObject();
    profile.insert(QStringLiteral("id"), newId);
    root.insert(QStringLiteral("profile"), profile);
    QJsonObject device = root.value(QStringLiteral("device")).toObject();
    QJsonObject frame = device.value(QStringLiteral("frame")).toObject();
    frame.insert(QStringLiteral("timeoutMs"), timeoutMs);
    device.insert(QStringLiteral("frame"), frame);
    QJsonObject session = device.value(QStringLiteral("session")).toObject();
    session.insert(QStringLiteral("cycleIntervalMs"), 20);
    device.insert(QStringLiteral("session"), session);
    QJsonObject transport = device.value(QStringLiteral("transport")).toObject();
    QJsonObject tcp = transport.value(QStringLiteral("tcp")).toObject();
    tcp.insert(QStringLiteral("host"), QStringLiteral("127.0.0.1"));
    tcp.insert(QStringLiteral("port"), static_cast<int>(port == 0 ? 1 : port));
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

// A copy of an example profile under a new id whose tcp host is @p host (the transport kind of the
// example is kept: a serial example stays serial). The example's other settings are not changed.
QString writeProfileHost(const QString& dir, const QString& exampleId, const QString& newId,
                         const QString& host) {
    QJsonObject root = readJson(exampleProfile(exampleId));
    QJsonObject profile = root.value(QStringLiteral("profile")).toObject();
    profile.insert(QStringLiteral("id"), newId);
    root.insert(QStringLiteral("profile"), profile);
    QJsonObject device = root.value(QStringLiteral("device")).toObject();
    QJsonObject transport = device.value(QStringLiteral("transport")).toObject();
    QJsonObject tcp = transport.value(QStringLiteral("tcp")).toObject();
    tcp.insert(QStringLiteral("host"), host);
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

QString writePlan(const QString& dir, const QString& name, const QByteArray& json) {
    const QString path = dir + QStringLiteral("/") + name;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(json);
    }
    return path;
}

// tests/hil/e2e/plan_3e.json without E-10. E-10's follow-up read expects the fresh image of a new
// virtual_plc connection (D100 = 1234), while a mock tab keeps one memory for all its clients; the
// full plan, E-10 included, runs against a mock tab in tst_gui_mock_streams.cpp (T-075). E-09
// stays: it is a read-only frame too, so the typed confirmation is part of the run.
QString mockPlan(const QString& dir) {
    QJsonObject plan = readJson(e2ePlan("plan_3e.json"));
    QJsonArray kept;
    for (const QJsonValue& step : plan.value(QStringLiteral("steps")).toArray()) {
        if (step.toObject().value(QStringLiteral("id")).toString() != QLatin1String("E-10")) {
            kept.append(step);
        }
    }
    plan.insert(QStringLiteral("steps"), kept);
    const QString path = dir + QStringLiteral("/plan_3e_mock.json");
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(plan).toJson());
    }
    return path;
}

struct CliRun {
    int code{-1};
    QString out;
    QString err;
};

// The very program path of hil_capture (runTool), with its streams captured.
CliRun runCli(const mc::hil::Options& options, const QString& input = QString()) {
    CliRun run;
    QString inText = input;
    QTextStream out(&run.out);
    QTextStream err(&run.err);
    QTextStream in(&inText);
    const mc::hil::ToolIo io{&out, &err, &in};
    run.code = static_cast<int>(mc::hil::runTool(options, io));
    out.flush();
    err.flush();
    return run;
}

// A server on loopback that only counts connections: stands for a PLC nothing may reach.
struct Listener {
    Listener() : spy(&server, &QTcpServer::newConnection) {
        ok = server.listen(QHostAddress::LocalHost, 0);
    }
    quint16 port() const { return server.serverPort(); }
    int connectionsAfter(int ms) {
        QTest::qWait(ms);
        return spy.count() + (server.hasPendingConnections() ? 1 : 0);
    }
    QTcpServer server;
    QSignalSpy spy;
    bool ok{false};
};

// Waits for the signal whose first argument carries @p token.
template <class Result> bool waitFor(QSignalSpy& spy, quint64 token, Result* out, int ms = kWait) {
    const auto find = [&]() {
        for (const QList<QVariant>& args : spy) {
            const auto value = args.at(0).value<Result>();
            if (value.token == token) {
                *out = value;
                return true;
            }
        }
        return false;
    };
    return QTest::qWaitFor(find, ms);
}

bool checkVia(HilHost& host, const HilCheckInput& input, HilCheckResult* out) {
    QSignalSpy spy(&host, &HilHost::checked);
    const quint64 token = host.check(input);
    return waitFor<HilCheckResult>(spy, token, out);
}

bool runVia(HilHost& host, const HilRunRequest& request, HilRunResult* out) {
    QSignalSpy spy(&host, &HilHost::runFinished);
    const quint64 token = host.run(request);
    return waitFor<HilRunResult>(spy, token, out, kRunWait);
}

HilRunRequest requestFor(const HilCheckInput& input, const HilCheckResult& check,
                         const QString& outputRoot, CaptureSource source = CaptureSource::MockPlc) {
    HilRunRequest r;
    r.input = input;
    r.expectedDigest = check.digest;
    r.typedId = check.profileId;
    r.outputRoot = outputRoot;
    r.source = source;
    r.benchReps = 3;
    return r;
}

// Drives the modal confirmation dialog that requestRun() opens, once it is on screen.
class DialogDriver {
public:
    explicit DialogDriver(std::function<void(HilConfirmDialog*)> action) : m_action(std::move(action)) {
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

QString readAll(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

constexpr const char* kRefusedPlan = R"JSON({"schema":1,"plan":{"id":"gui_refuse"},"steps":[
  {"id":"R-01","kind":"read","device":"D@s"},
  {"id":"R-02","kind":"write","device":"D5000","values":[1,2]},
  {"id":"R-03","kind":"poll","heartbeat":"M5000","rounds":2,
   "subscribe":[{"name":"d","device":"D@s","count":1}]},
  {"id":"R-04","kind":"raw","hex":"50 00 00 FF FF 03 00 0E 00 10 00 01 14 00 00 88 13 00 A8 01 00 01 00",
   "readOnly":true,"recover":"reconnect"}
]})JSON";

constexpr const char* kSimplePlan = R"JSON({"schema":1,"plan":{"id":"gui_simple"},"steps":[
  {"id":"S-01","kind":"read","device":"D@s","expect":{"kind":"ok","values":[1234]}},
  {"id":"S-02","kind":"write","device":"D@s+1","values":[7,8],"readBack":true}
]})JSON";

constexpr const char* kPromptPlan = R"JSON({"schema":1,"plan":{"id":"gui_prompt"},"steps":[
  {"id":"P-01","kind":"poll","rounds":3,"subscribe":[{"name":"d","device":"D@s","count":2}],
   "actions":[{"after":1,"prompt":"Pull the cable now"}]},
  {"id":"P-02","kind":"read","device":"D@s"}
]})JSON";

} // namespace

class HilViewTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { registerRunnerMetaTypes(); }

    // ---- GUI-07: the gate of hil_capture, nothing else ----------------------------------------

    void GUI_07_gateDecisionsAndDryRunsEqualHilCaptureOnEveryCommittedPlanAndProfile() {
        const QDir plans(testsDir() + QStringLiteral("/hil/plans"));
        const QDir profiles(testsDir() + QStringLiteral("/hil/profiles"));
        const QStringList planFiles = plans.entryList({QStringLiteral("*.json")}, QDir::Files);
        const QStringList profileFiles =
            profiles.entryList({QStringLiteral("*.example.json")}, QDir::Files);
        QVERIFY2(planFiles.size() >= 4, "the four committed plans");
        QVERIFY2(profileFiles.size() >= 5, "the committed example profiles");

        HilHost host(QStringLiteral("gui07-equality"));
        int ok = 0;
        int refused = 0;
        int bad = 0;
        QStringList okPlans;
        for (const QString& planFile : planFiles) {
            for (const QString& profileFile : profileFiles) {
                const QString label = planFile + QStringLiteral(" x ") + profileFile;
                HilCheckInput input;
                input.profilePath = profiles.filePath(profileFile);
                input.planPath = plans.filePath(planFile);

                // The GUI pipeline, called directly...
                const HilPrepared prepared = prepareHil(input);
                const HilCheckResult direct = prepared.toResult();

                // ... hil_capture's own dry run (the same function the program calls) ...
                mc::hil::Options options;
                options.profilePath = input.profilePath;
                options.planPath = input.planPath;
                options.dryRun = true;
                const CliRun cli = runCli(options);
                QVERIFY2(cli.code == direct.exitCode, qPrintable(label));
                if (direct.exitCode == 0) {
                    QVERIFY2(cli.out == direct.dryRunText, qPrintable(label + " dry run"));
                    QVERIFY2(!direct.dryRunText.isEmpty(), qPrintable(label));
                    ++ok;
                    okPlans << planFile;
                } else if (direct.exitCode == 3) {
                    QVERIFY2(cli.err == direct.refusalText, qPrintable(label + " refusal"));
                    QVERIFY2(direct.dryRunText.isEmpty(), qPrintable(label));
                    ++refused;
                } else {
                    QStringList expected;
                    for (const QString& line : direct.errorText.split(QLatin1Char('\n'))) {
                        expected << QStringLiteral("hil_capture: ") + line + QLatin1Char('\n');
                    }
                    QVERIFY2(cli.err == expected.join(QString()), qPrintable(label + " error"));
                    ++bad;
                }

                // ... and the same verdict from the runner thread.
                HilCheckResult viaThread;
                QVERIFY2(checkVia(host, input, &viaThread), qPrintable(label));
                QVERIFY2(viaThread.exitCode == direct.exitCode, qPrintable(label));
                QVERIFY2(viaThread.dryRunText == direct.dryRunText, qPrintable(label));
                QVERIFY2(viaThread.refusalText == direct.refusalText, qPrintable(label));
                QVERIFY2(viaThread.errorText == direct.errorText, qPrintable(label));
                QVERIFY2(viaThread.confirmationText == direct.confirmationText, qPrintable(label));
                QVERIFY2(viaThread.digest == direct.digest, qPrintable(label));
                QVERIFY2(viaThread.readOnlyFrames.size() == direct.readOnlyFrames.size(),
                         qPrintable(label));

                if (direct.exitCode == 0) {
                    // The confirmation of the program: a wrong id is not a confirmation, and the
                    // program prints the very summary the GUI shows.
                    options.dryRun = false;
                    const CliRun wrong = runCli(options, QStringLiteral("not-the-id\n"));
                    QVERIFY2(wrong.code == static_cast<int>(mc::hil::ExitCode::BadInput),
                             qPrintable(label));
                    QVERIFY2(wrong.out.startsWith(direct.confirmationText), qPrintable(label));
                    QVERIFY2(wrong.err.contains(QStringLiteral("not confirmed")), qPrintable(label));
                }
            }
        }
        qInfo("GUI-07 equality: %d combinations, %d pass the gate, %d refused, %d bad input",
              static_cast<int>(planFiles.size() * profileFiles.size()), ok, refused, bad);
        QVERIFY2(ok >= 5, "the plan of each family passes the gate with its profiles");
        for (const QString& planFile : planFiles) {
            QVERIFY2(okPlans.contains(planFile), qPrintable(planFile + " passes with no profile"));
        }
    }

    void GUI_07_aRefusedPlanEqualsHilCaptureAndSendsNothing() {
        Listener target;
        QVERIFY(target.ok);
        const QString dir = freshOutputDir(QStringLiteral("gui-t072-refuse"));
        HilCheckInput input;
        input.profilePath = writeProfile(dir, QStringLiteral("q03ude-eth-3e-bin"),
                                         QStringLiteral("gui-refuse"), target.port());
        input.planPath = writePlan(dir, QStringLiteral("refuse.json"), kRefusedPlan);

        const HilCheckResult direct = prepareHil(input).toResult();
        QCOMPARE(direct.exitCode, 3);
        for (const char* id : {"R-02", "R-03", "R-04"}) {
            QVERIFY2(direct.refusalText.contains(QLatin1String(id)), id);
        }
        QVERIFY2(!direct.refusalText.contains(QStringLiteral("R-01")), "a read is not refused");

        mc::hil::Options options;
        options.profilePath = input.profilePath;
        options.planPath = input.planPath;
        options.dryRun = true;
        options.yes = true;
        const CliRun cli = runCli(options, QStringLiteral("gui-refuse\n"));
        QCOMPARE(cli.code, static_cast<int>(mc::hil::ExitCode::GateRefused));
        QCOMPARE(cli.err, direct.refusalText);
        QVERIFY(cli.out.isEmpty());

        // The runner thread refuses it too, whatever the request says: typed id, --yes, a digest.
        HilHost host(QStringLiteral("gui07-refused"));
        HilCheckResult viaThread;
        QVERIFY(checkVia(host, input, &viaThread));
        QCOMPARE(viaThread.exitCode, 3);
        QCOMPARE(viaThread.refusalText, direct.refusalText);
        QVERIFY(viaThread.digest.isEmpty());

        const QString root = dir + QStringLiteral("/captured");
        for (const bool skip : {false, true}) {
            HilRunRequest request = requestFor(input, viaThread, root);
            request.typedId = QStringLiteral("gui-refuse");
            request.skipTyping = skip;
            HilRunResult result;
            QVERIFY(runVia(host, request, &result));
            QVERIFY(result.status == HilRunStatus::GateRefused);
            QCOMPARE(result.reason, direct.refusalText);
            QCOMPARE(result.exitCode(), 3);
            QVERIFY(!result.onGuiThread);
        }
        QVERIFY(!QDir(root).exists());

        // The view: the refusal is shown, there is no way to run, and nothing is asked of the runner.
        HilView view;
        view.setProfilePath(input.profilePath);
        view.setPlanPath(input.planPath);
        view.setOutputRoot(root);
        QSignalSpy checked(&view, &HilView::checkDone);
        view.check();
        QTRY_COMPARE_WITH_TIMEOUT(checked.count(), 1, kWait);
        QVERIFY(view.gateText().contains(QStringLiteral("REFUSED")));
        QVERIFY(view.gateText().contains(QStringLiteral("R-02")));
        QVERIFY(view.dryRunText().isEmpty());
        QVERIFY(!view.canRun());
        QCOMPARE(view.requestRun(), quint64(0));
        QCOMPARE(view.runWith(QStringLiteral("gui-refuse"), true), quint64(0));

        QCOMPARE(target.connectionsAfter(400), 0); // the loopback listener never saw a connection
    }

    void GUI_07_badInputIsReportedAsHilCaptureDoes() {
        const QString dir = freshOutputDir(QStringLiteral("gui-t072-bad"));
        HilCheckInput input;
        input.profilePath = dir + QStringLiteral("/missing.json");
        input.planPath = e2ePlan("plan_3e.json");
        HilHost host(QStringLiteral("gui07-bad"));
        HilCheckResult r;
        QVERIFY(checkVia(host, input, &r));
        QCOMPARE(r.exitCode, 2);
        QVERIFY(!r.errorText.isEmpty());
        mc::hil::Options options;
        options.profilePath = input.profilePath;
        options.planPath = input.planPath;
        options.dryRun = true;
        QCOMPARE(runCli(options).code, 2);

        // An unknown group is a typo, not "run nothing".
        input.profilePath = exampleProfile(QStringLiteral("q03ude-eth-3e-bin"));
        input.only = QStringList{QStringLiteral("ZZ")};
        QVERIFY(checkVia(host, input, &r));
        QCOMPARE(r.exitCode, 2);
        QVERIFY(r.errorText.contains(QStringLiteral("ZZ")));
        options.profilePath = input.profilePath;
        options.only = QStringList{QStringLiteral("ZZ")};
        QCOMPARE(runCli(options).code, 2);

        input.only.clear();
        input.plcState = QStringLiteral("RUNNING");
        QVERIFY(checkVia(host, input, &r));
        QCOMPARE(r.exitCode, 2);
    }

    // ---- GUI-07: the typed confirmation cannot be skipped --------------------------------------

    void GUI_07_confirmationRuleIsTheRuleOfHilCapture() {
        HilCheckResult withFrames;
        withFrames.exitCode = 0;
        withFrames.loopbackTcp = true;
        withFrames.profileId = QStringLiteral("prof-1");
        withFrames.readOnlyFrames = {{QStringLiteral("E-09"), QStringLiteral("frame"), QStringLiteral("50 00")}};
        HilCheckResult plain = withFrames;
        plain.readOnlyFrames.clear();

        // read-only frames: only the exact id confirms, whatever --yes says
        for (const bool skip : {false, true}) {
            QVERIFY(!confirmationAccepted(withFrames, QString(), skip));
            QVERIFY(!confirmationAccepted(withFrames, QStringLiteral("prof"), skip));
            QVERIFY(!confirmationAccepted(withFrames, QStringLiteral("PROF-1"), skip));
            QVERIFY(!confirmationAccepted(withFrames, QStringLiteral("prof-1 x"), skip));
            QVERIFY(confirmationAccepted(withFrames, QStringLiteral("prof-1"), skip));
            QVERIFY(confirmationAccepted(withFrames, QStringLiteral("  prof-1 "), skip));
            QVERIFY(mustTypeProfileId(withFrames, skip));
        }
        // no read-only frames: the id, or --yes
        QVERIFY(!confirmationAccepted(plain, QString(), false));
        QVERIFY(confirmationAccepted(plain, QStringLiteral("prof-1"), false));
        QVERIFY(confirmationAccepted(plain, QString(), true));
        QVERIFY(!mustTypeProfileId(plain, true));
        // a run the gate did not pass is never confirmed
        HilCheckResult refused = plain;
        refused.exitCode = 3;
        QVERIFY(!confirmationAccepted(refused, QStringLiteral("prof-1"), true));
        QString why;
        QVERIFY(!confirmationAccepted(withFrames, QString(), true, &why));
        QVERIFY2(why.contains(QStringLiteral("read-only")), qPrintable(why));
    }

    void GUI_07_theDialogEnablesRunOnlyForTheTypedIdWhenReadOnlyFramesExist() {
        HilCheckResult withFrames;
        withFrames.exitCode = 0;
        withFrames.loopbackTcp = true;
        withFrames.profileId = QStringLiteral("prof-1");
        withFrames.confirmationText = QStringLiteral("summary");
        withFrames.readOnlyFrames = {{QStringLiteral("E-09"), QStringLiteral("frame"), QStringLiteral("50 00")}};
        HilConfirmDialog dialog(withFrames);
        QVERIFY(!dialog.skipTypingAvailable());
        QVERIFY(!dialog.acceptEnabled());
        QVERIFY(!dialog.okButton()->isEnabled());
        dialog.setSkipTyping(true); // there is no such box: ignored
        QVERIFY(!dialog.skipTyping());
        QVERIFY(!dialog.acceptEnabled());
        dialog.typeId(QStringLiteral("prof-"));
        QVERIFY(!dialog.okButton()->isEnabled());
        dialog.accept(); // programmatic accept does not get past the rule
        QVERIFY(dialog.result() != QDialog::Accepted);
        dialog.typeId(QStringLiteral("prof-1"));
        QVERIFY(dialog.okButton()->isEnabled());
        dialog.accept();
        QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));

        HilCheckResult plain = withFrames;
        plain.readOnlyFrames.clear();
        HilConfirmDialog repeat(plain);
        QVERIFY(repeat.skipTypingAvailable());
        QVERIFY(!repeat.acceptEnabled());
        repeat.setSkipTyping(true);
        QVERIFY(repeat.acceptEnabled());
        QVERIFY(repeat.okButton()->isEnabled());
    }

    // T-076 (owner decision C, 2026-10-04): "confirm without typing" only for a loopback TCP profile
    // whose run has no read-only frames. {loopback TCP, other TCP host, COM} x {read-only frames,
    // none} x {typed right, typed wrong, nothing typed} x {skip off, skip on}.
    void GUI_07_skipTypingTruthTable() {
        enum class Target { Loopback, OtherHost, Com };
        for (const Target target : {Target::Loopback, Target::OtherHost, Target::Com}) {
            for (const bool readOnly : {false, true}) {
                HilCheckResult check;
                check.exitCode = 0;
                check.profileId = QStringLiteral("prof-1");
                check.loopbackTcp = target == Target::Loopback;
                if (readOnly) {
                    check.readOnlyFrames = {{QStringLiteral("E-09"), QStringLiteral("frame"), QStringLiteral("50 00")}};
                }
                const bool allowed = target == Target::Loopback && !readOnly;
                QCOMPARE(skipTypingAllowed(check), allowed);
                for (const QString& typed : {QStringLiteral("prof-1"), QStringLiteral("wrong"), QString()}) {
                    for (const bool skip : {false, true}) {
                        const bool right = typed == QLatin1String("prof-1");
                        const bool expected = right || (skip && allowed);
                        QString why;
                        const bool got = confirmationAccepted(check, typed, skip, &why);
                        QVERIFY2(got == expected,
                                 qPrintable(QStringLiteral("target %1 readOnly %2 typed '%3' skip %4: got %5")
                                                .arg(static_cast<int>(target)).arg(readOnly).arg(typed)
                                                .arg(skip).arg(got)));
                        QCOMPARE(mustTypeProfileId(check, skip), !(skip && allowed));
                        QCOMPARE(why.isEmpty(), expected);

                        // the dialog follows the same rule, and offers the box only where it counts
                        HilCheckResult shown = check;
                        shown.confirmationText = QStringLiteral("summary");
                        HilConfirmDialog dialog(shown);
                        QCOMPARE(dialog.skipTypingAvailable(), allowed);
                        dialog.setSkipTyping(skip);
                        QCOMPARE(dialog.skipTyping(), skip && allowed);
                        dialog.typeId(typed);
                        QCOMPARE(dialog.acceptEnabled(), expected);
                        QCOMPARE(dialog.okButton()->isEnabled(), expected);
                        dialog.accept();
                        QCOMPARE(dialog.result() == QDialog::Accepted, expected);
                    }
                }
            }
        }
        // a run the gate did not pass is never confirmed, loopback or not
        HilCheckResult refused;
        refused.exitCode = 3;
        refused.loopbackTcp = true;
        refused.profileId = QStringLiteral("prof-1");
        QVERIFY(!confirmationAccepted(refused, QStringLiteral("prof-1"), true));
        // the refusal says why a non-loopback profile cannot skip
        HilCheckResult other;
        other.exitCode = 0;
        other.profileId = QStringLiteral("prof-1");
        QString why;
        QVERIFY(!confirmationAccepted(other, QString(), true, &why));
        QVERIFY2(why.contains(QStringLiteral("loopback")), qPrintable(why));
    }

    // The check derives loopbackTcp from the profile: 127.0.0.0/8, ::1, localhost (as isLoopbackHost)
    // are loopback; another host and a COM profile are not.
    void GUI_07_loopbackIsDerivedFromTheProfile() {
        const QString dir = freshOutputDir(QStringLiteral("gui-t076-derive"));
        const QString plan = writePlan(dir, QStringLiteral("simple.json"), kSimplePlan);
        const auto check = [&](const QString& profilePath) {
            HilCheckInput input;
            input.profilePath = profilePath;
            input.planPath = plan;
            const HilPrepared prepared = prepareHil(input);
            [&]() { QVERIFY2(prepared.ok(), qPrintable(prepared.errorText + prepared.refusalText)); }();
            return prepared.toResult();
        };
        for (const QString& host : {QStringLiteral("127.0.0.1"), QStringLiteral("127.8.9.10"),
                                    QStringLiteral("::1"), QStringLiteral("localhost")}) {
            const HilCheckResult r = check(writeProfileHost(dir, QStringLiteral("q03ude-eth-3e-bin"),
                                                            QStringLiteral("t76-loop"), host));
            QVERIFY2(r.ok(), qPrintable(host));
            QVERIFY2(r.loopbackTcp, qPrintable(host));
            QVERIFY2(skipTypingAllowed(r), qPrintable(host));
        }
        for (const QString& host : {QStringLiteral("192.0.2.10"), QStringLiteral("10.0.0.1"),
                                    QStringLiteral("example.invalid"), QStringLiteral("128.0.0.1")}) {
            const HilCheckResult r = check(writeProfileHost(dir, QStringLiteral("q03ude-eth-3e-bin"),
                                                            QStringLiteral("t76-other"), host));
            QVERIFY2(r.ok(), qPrintable(host));
            QVERIFY2(!r.loopbackTcp, qPrintable(host));
            QVERIFY2(!skipTypingAllowed(r), qPrintable(host));
        }
        // a COM profile, even one whose (unused) tcp block names 127.0.0.1
        const QString com = writeProfileHost(dir, QStringLiteral("q03ude-c24-3c-f4"),
                                             QStringLiteral("t76-com"), QStringLiteral("127.0.0.1"));
        const HilCheckResult r = check(com);
        QVERIFY(r.ok());
        QVERIFY(!r.loopbackTcp);
        QVERIFY(!skipTypingAllowed(r));
    }

    // The runner thread refuses skipTyping for a non-loopback or COM profile on its own, whatever the
    // dialog did. The profiles are the committed examples (192.0.2.10, COM1): a run that got past the
    // refusal would try to connect or open the port, so the check is the status, the missing capture
    // folder and the time it took. A loopback listener cannot stand in for a non-loopback host (the
    // profile would then be loopback), so the runner is also shown to start and connect for a
    // loopback profile in the same setup, and the decision function carries the truth table above.
    void GUI_07_theRunnerRefusesSkipTypingForANonLoopbackOrComProfile() {
        const QString dir = freshOutputDir(QStringLiteral("gui-t076-runner"));
        const QString plan = writePlan(dir, QStringLiteral("simple.json"), kSimplePlan);
        const QString root = dir + QStringLiteral("/captured");
        struct Case {
            const char* name;
            QString profile;
            QString id;
        };
        const QVector<Case> cases = {
            {"other tcp host", writeProfileHost(dir, QStringLiteral("q03ude-eth-3e-bin"),
                                                QStringLiteral("t76-other"), QStringLiteral("192.0.2.10")),
             QStringLiteral("t76-other")},
            {"com port", writeProfileHost(dir, QStringLiteral("q03ude-c24-3c-f4"),
                                          QStringLiteral("t76-com"), QStringLiteral("192.0.2.10")),
             QStringLiteral("t76-com")},
        };
        HilHost host(QStringLiteral("gui07-t076"));
        for (const Case& c : cases) {
            HilCheckInput input;
            input.profilePath = c.profile;
            input.planPath = plan;
            HilCheckResult check;
            QVERIFY2(checkVia(host, input, &check), c.name);
            QVERIFY2(check.ok(), qPrintable(check.errorText + check.refusalText));
            QVERIFY2(check.readOnlyFrames.isEmpty(), c.name);
            QVERIFY2(!check.loopbackTcp, c.name);

            for (const QString& typed : {QString(), QStringLiteral("wrong")}) {
                HilRunRequest request = requestFor(input, check, root);
                request.typedId = typed;
                request.skipTyping = true; // as if the dialog had been bypassed
                QElapsedTimer timer;
                timer.start();
                HilRunResult result;
                QVERIFY2(runVia(host, request, &result), c.name);
                QVERIFY2(result.status == HilRunStatus::NotConfirmed, c.name);
                QVERIFY2(result.reason.contains(c.id), qPrintable(result.reason));
                QVERIFY2(!result.onGuiThread, c.name);
                QVERIFY2(timer.elapsed() < 1500, c.name); // no connect attempt, no port open
                QVERIFY2(!QDir(root).exists(), c.name);
            }
            // typed id, no skip: allowed by the rule (the request is only judged, not run, here:
            // the digest of another check keeps it from starting)
            HilRunRequest typed = requestFor(input, check, root);
            typed.expectedDigest = QStringLiteral("0000");
            HilRunResult result;
            QVERIFY(runVia(host, typed, &result));
            QVERIFY(result.status == HilRunStatus::NotConfirmed);
            QVERIFY(result.reason.contains(QStringLiteral("changed")));
        }

        // the view: its dialog does not offer the box for these profiles, and the view's own call
        // with skipTyping and no id still reaches the runner's refusal
        for (const Case& c : cases) {
            HilView view;
            view.setProfilePath(c.profile);
            view.setPlanPath(plan);
            view.setOutputRoot(root);
            QSignalSpy checked(&view, &HilView::checkDone);
            view.check();
            QTRY_COMPARE_WITH_TIMEOUT(checked.count(), 1, kWait);
            QVERIFY2(view.lastCheck().ok(), c.name);
            QVERIFY2(view.canRun(), c.name);
            bool skipBox = true;
            bool enabled = true;
            {
                DialogDriver driver([&](HilConfirmDialog* d) {
                    skipBox = d->skipTypingAvailable();
                    d->setSkipTyping(true);
                    enabled = d->okButton()->isEnabled();
                    d->accept();
                    d->reject();
                });
                QCOMPARE(view.requestRun(), quint64(0));
                QVERIFY(driver.seen());
            }
            QVERIFY2(!skipBox, c.name);
            QVERIFY2(!enabled, c.name);
            QSignalSpy done(&view, &HilView::runDone);
            QVERIFY2(view.runWith(QString(), true) != 0, c.name);
            QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, kWait);
            QVERIFY2(view.lastRun().status == HilRunStatus::NotConfirmed, c.name);
            QVERIFY2(!QDir(root).exists(), c.name);
        }

        // the loopback twin of the first profile does take --yes (to a mock, on the runner thread)
        MockRig mock(QStringLiteral("gui07-t076"));
        QVERIFY(mock.ok);
        mock.host.setWords(QStringLiteral("D100"), {1234});
        HilCheckInput input;
        input.profilePath = writeProfile(dir, QStringLiteral("q03ude-eth-3e-bin"),
                                         QStringLiteral("t76-loop"), mock.port);
        input.planPath = plan;
        HilCheckResult check;
        QVERIFY(checkVia(host, input, &check));
        QVERIFY(check.loopbackTcp);
        HilRunRequest request = requestFor(input, check, root);
        request.typedId.clear();
        request.skipTyping = true;
        HilRunResult result;
        QVERIFY(runVia(host, request, &result));
        QVERIFY2(result.status == HilRunStatus::Finished, qPrintable(result.reason));
    }

    void GUI_07_aRunWithReadOnlyFramesIsNotStartedWithoutTheTypedId() {
        Listener target;
        QVERIFY(target.ok);
        const QString dir = freshOutputDir(QStringLiteral("gui-t072-confirm"));
        HilCheckInput input;
        input.profilePath = writeProfile(dir, QStringLiteral("q03ude-eth-3e-bin"),
                                         QStringLiteral("gui-confirm"), target.port());
        input.planPath = e2ePlan("plan_3e.json"); // E-09 and E-10 are declared readOnly
        HilHost host(QStringLiteral("gui07-confirm"));
        HilCheckResult check;
        QVERIFY(checkVia(host, input, &check));
        QCOMPARE(check.exitCode, 0);
        QVERIFY2(check.readOnlyFrames.size() >= 2, "plan_3e holds read-only frames");
        const QString root = dir + QStringLiteral("/captured");

        const auto attempt = [&](const QString& typed, bool skip, const QString& digest,
                                 HilRunResult* result) {
            HilRunRequest request = requestFor(input, check, root);
            request.typedId = typed;
            request.skipTyping = skip;
            request.expectedDigest = digest;
            return runVia(host, request, result);
        };
        HilRunResult r;
        QVERIFY(attempt(QString(), true, check.digest, &r)); // --yes, nothing typed
        QVERIFY(r.status == HilRunStatus::NotConfirmed);
        QVERIFY2(r.reason.contains(QStringLiteral("read-only")), qPrintable(r.reason));
        QVERIFY(attempt(QStringLiteral("wrong"), true, check.digest, &r));
        QVERIFY(r.status == HilRunStatus::NotConfirmed);
        QVERIFY(attempt(QStringLiteral("wrong"), false, check.digest, &r));
        QVERIFY(r.status == HilRunStatus::NotConfirmed);
        // the right id, but for a check other than the one on the table
        QVERIFY(attempt(QStringLiteral("gui-confirm"), false, QStringLiteral("0000"), &r));
        QVERIFY(r.status == HilRunStatus::NotConfirmed);
        QVERIFY2(r.reason.contains(QStringLiteral("changed")), qPrintable(r.reason));
        QVERIFY(!QDir(root).exists());
        QCOMPARE(target.connectionsAfter(300), 0);

        // The view: cancelling the dialog, or a wrong id, starts nothing; Run stays disabled in the
        // dialog until the exact id is typed.
        HilView view;
        view.setProfilePath(input.profilePath);
        view.setPlanPath(input.planPath);
        view.setOutputRoot(root);
        QSignalSpy checked(&view, &HilView::checkDone);
        view.check();
        QTRY_COMPARE_WITH_TIMEOUT(checked.count(), 1, kWait);
        QVERIFY(view.canRun());
        QVERIFY(view.dryRunText().contains(QStringLiteral("tx ")));
        QVERIFY(view.gateText().contains(QStringLiteral("read-only")));

        bool enabledAfterWrong = true;
        bool skipBoxAvailable = true;
        bool openAfterAccept = false;
        {
            DialogDriver driver([&](HilConfirmDialog* d) {
                skipBoxAvailable = d->skipTypingAvailable();
                d->setSkipTyping(true);
                d->typeId(QStringLiteral("wrong"));
                enabledAfterWrong = d->okButton()->isEnabled();
                d->accept();
                openAfterAccept = d->isVisible();
                d->reject();
            });
            QCOMPARE(view.requestRun(), quint64(0));
            QVERIFY(driver.seen());
        }
        QVERIFY(!skipBoxAvailable);
        QVERIFY(!enabledAfterWrong);
        QVERIFY(openAfterAccept);
        QVERIFY(!view.isRunning());
        QVERIFY(view.statusText().contains(QStringLiteral("Not confirmed")));
        QCOMPARE(target.connectionsAfter(300), 0);

        // The view's own call with a wrong id still reaches the runner's rule.
        QSignalSpy done(&view, &HilView::runDone);
        const quint64 token = view.runWith(QStringLiteral("wrong"), true);
        QVERIFY(token != 0);
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, kWait);
        QVERIFY(view.lastRun().status == HilRunStatus::NotConfirmed);
        QCOMPARE(target.connectionsAfter(300), 0);
        QVERIFY(!QDir(root).exists());
    }

    // ---- a run against a MockRunner ------------------------------------------------------------

    void GUI_07_aRunAgainstAMockRunnerWritesACaptureThatReplaysGreen() {
        MockRig mock(QStringLiteral("gui07-mock"));
        QVERIFY(mock.ok);
        mock.host.setWords(QStringLiteral("D100"), {1234});
        const QString dir = freshOutputDir(QStringLiteral("gui-t072-run"));
        const QString root = dir + QStringLiteral("/captured");

        HilView view;
        view.setProfilePath(writeProfile(dir, QStringLiteral("q03ude-eth-3e-bin"),
                                         QStringLiteral("gui-run-3e"), mock.port));
        view.setPlanPath(mockPlan(dir));
        view.setOutputRoot(root);
        view.setBenchReps(3);
        view.setNote(QStringLiteral("GUI-07 run against a mock"));
        view.setCapturedRoot(freshOutputDir(QStringLiteral("gui-t072-fake")) + QStringLiteral("/tests/vectors/captured"));
        QSignalSpy checked(&view, &HilView::checkDone);
        view.check();
        QTRY_COMPARE_WITH_TIMEOUT(checked.count(), 1, kWait);
        QVERIFY2(view.lastCheck().ok(), qPrintable(view.gateText()));
        QVERIFY(view.lastCheck().loopbackTcp);
        QCOMPARE(view.source(), CaptureSource::MockPlc); // chosen from the profile's loopback host
        QVERIFY(view.canRun());

        Ticker ticker;
        QSignalSpy started(view.host(), &HilHost::stepStarted);
        QSignalSpy done(&view, &HilView::runDone);
        {
            DialogDriver driver([&](HilConfirmDialog* d) {
                d->typeId(QStringLiteral("gui-run-3e"));
                d->okButton()->click();
            });
            QVERIFY(view.requestRun() != 0);
            QVERIFY(driver.seen());
        }
        QVERIFY(view.isRunning());
        QVERIFY(!view.canRun()); // not twice
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, kRunWait);
        const HilRunResult run = view.lastRun();
        QVERIFY2(run.status == HilRunStatus::Finished, qPrintable(run.reason));
        const QString lines = run.lines.join(QLatin1Char('\n'));
        QVERIFY2(run.passed == 13 && run.failed == 0 && run.diverged == 0 && run.notSupported == 0 &&
                     run.skipped == 1,
                 qPrintable(lines));
        QCOMPARE(run.exitCode(), 0);
        // the run executed on the runner thread, not on the GUI thread
        QVERIFY(!run.onGuiThread);
        QCOMPARE(run.threadName, QStringLiteral("hil-runner"));
        QVERIFY(QThread::currentThread()->objectName() != run.threadName);
        QCOMPARE(view.host()->runnerThread()->workerThread()->objectName(), run.threadName);
        QVERIFY2(ticker.maxGapMs() < 250, qPrintable(QString::number(ticker.maxGapMs())));
        qInfo("GUI-07 run: largest GUI event gap %lld ms", static_cast<long long>(ticker.maxGapMs()));

        // live outcomes arrived while it ran, one row per step
        QVERIFY(started.count() >= 13);
        QCOMPARE(view.outcomeRows(), 14);
        QCOMPARE(view.outcomeCell(0, 0), QStringLiteral("E-01"));
        QCOMPARE(view.outcomeCell(0, 1), QStringLiteral("PASS"));
        QCOMPARE(view.outcomeCell(13, 0), QStringLiteral("E-15"));
        QCOMPARE(view.outcomeCell(13, 1), QStringLiteral("SKIP"));
        QVERIFY(view.statusText().contains(QStringLiteral("passed 13")));

        // the capture: on disk, marked as a mock's, never under tests/vectors/captured
        const QString folder = run.folder;
        QVERIFY(folder.endsWith(QStringLiteral("gui-run-3e")));
        for (const char* name : {"run.meta", "steps.vec", "session.vec", "bench.csv"}) {
            QVERIFY2(QFileInfo::exists(folder + QStringLiteral("/") + QLatin1String(name)), name);
        }
        const QString steps = readAll(folder + QStringLiteral("/steps.vec"));
        QVERIFY(steps.contains(QStringLiteral("# source: mock  profile: gui-run-3e")));
        QVERIFY(!steps.contains(QStringLiteral("# source: plc")));
        const QString meta = readAll(folder + QStringLiteral("/run.meta"));
        QVERIFY(meta.contains(QStringLiteral("capture_source: mock")));
        QVERIFY(meta.contains(QStringLiteral("not hardware")));
        QVERIFY(!meta.contains(QStringLiteral("127.0.0.1")));
        QVERIFY(!QDir(testsDir() + QStringLiteral("/vectors/captured/gui-run-3e")).exists());

        // the capture replays green (RPL-01..06), through the view's own replay
        view.setReplayProgram(QStringLiteral(MC_REPLAY_TESTS_PATH));
        if (!QFileInfo::exists(view.replayProgram())) {
            QSKIP("mc_replay_tests is not built here");
        }
        QSignalSpy replayed(&view, &HilView::replayFinished);
        view.runReplay();
        QTRY_COMPARE_WITH_TIMEOUT(replayed.count(), 1, 90000);
        const auto replay = replayed.first().first().value<HilReplayResult>();
        QVERIFY2(replay.started && replay.exitCode == 0, qPrintable(replay.output));
        QVERIFY2(replay.output.contains(QStringLiteral("replayed gui-run-3e")), qPrintable(replay.output));
        QVERIFY(view.replayStatusText().contains(QStringLiteral("green")));
        // the same replay through the helper of the other GUI tests
        int exitCode = 0;
        const QString output = runReplay(root, &exitCode);
        QVERIFY2(exitCode == 0, qPrintable(output));

        // open the capture: files are read on the runner thread and shown
        QSignalSpy shown(&view, &HilView::fileShown);
        view.showCaptureFile(QStringLiteral("steps.vec"));
        QTRY_COMPARE_WITH_TIMEOUT(shown.count(), 1, kWait);
        QVERIFY(view.fileViewText().contains(QStringLiteral("CAP-gui-run-3e-E-01")));

        // the bench report (E-14 repeats a read 3 times)
        QSignalSpy bench(&view, &HilView::benchShown);
        view.showBenchReport();
        QTRY_COMPARE_WITH_TIMEOUT(bench.count(), 1, kWait);
        QVERIFY2(view.benchText().contains(QStringLiteral("gui-run-3e")), qPrintable(view.benchText()));

        // a second run of the same profile id needs "replace"
        view.check();
        QTRY_COMPARE_WITH_TIMEOUT(checked.count(), 2, kWait);
        QSignalSpy again(&view, &HilView::runDone);
        view.runWith(QStringLiteral("gui-run-3e"));
        QTRY_COMPARE_WITH_TIMEOUT(again.count(), 1, kWait);
        QVERIFY(view.lastRun().status == HilRunStatus::OutputRefused);
        QVERIFY(view.lastRun().reason.contains(QStringLiteral("exists")));
        view.setOverwrite(true);
        view.runWith(QStringLiteral("gui-run-3e"));
        QTRY_COMPARE_WITH_TIMEOUT(again.count(), 2, kRunWait);
        QVERIFY2(view.lastRun().status == HilRunStatus::Finished, qPrintable(view.lastRun().reason));
    }

    void GUI_07_confirmingWithoutTypingWorksOnlyWhenNoReadOnlyFrameExists() {
        MockRig mock(QStringLiteral("gui07-skip"));
        QVERIFY(mock.ok);
        mock.host.setWords(QStringLiteral("D100"), {1234});
        const QString dir = freshOutputDir(QStringLiteral("gui-t072-skip"));
        HilHost host(QStringLiteral("gui07-skiphost"));
        HilCheckInput input;
        input.profilePath = writeProfile(dir, QStringLiteral("q03ude-eth-3e-bin"),
                                         QStringLiteral("gui-skip"), mock.port);
        input.planPath = writePlan(dir, QStringLiteral("simple.json"), kSimplePlan);
        HilCheckResult check;
        QVERIFY(checkVia(host, input, &check));
        QCOMPARE(check.exitCode, 0);
        QVERIFY(check.readOnlyFrames.isEmpty());

        HilRunRequest request = requestFor(input, check, dir + QStringLiteral("/captured"));
        request.typedId.clear();
        HilRunResult r;
        QVERIFY(runVia(host, request, &r)); // nothing typed, no --yes
        QVERIFY(r.status == HilRunStatus::NotConfirmed);
        request.skipTyping = true; // --yes
        QVERIFY(runVia(host, request, &r));
        QVERIFY2(r.status == HilRunStatus::Finished, qPrintable(r.reason));
        QCOMPARE(r.passed, 2);
    }

    // ---- the operator prompt, cancel, output rules, shutdown ----------------------------------

    void GUI_07_aPromptWaitsForTheOperatorAndContinueAnswersIt() {
        MockRig mock(QStringLiteral("gui07-prompt"));
        QVERIFY(mock.ok);
        const QString dir = freshOutputDir(QStringLiteral("gui-t072-prompt"));
        HilView view;
        view.setProfilePath(writeProfile(dir, QStringLiteral("q03ude-eth-3e-bin"),
                                         QStringLiteral("gui-prompt"), mock.port));
        view.setPlanPath(writePlan(dir, QStringLiteral("prompt.json"), kPromptPlan));
        view.setOutputRoot(dir + QStringLiteral("/captured"));
        QSignalSpy checked(&view, &HilView::checkDone);
        view.check();
        QTRY_COMPARE_WITH_TIMEOUT(checked.count(), 1, kWait);
        QVERIFY2(view.lastCheck().ok(), qPrintable(view.gateText()));
        QSignalSpy done(&view, &HilView::runDone);
        QVERIFY(view.runWith(QString(), true) != 0);
        QTRY_VERIFY_WITH_TIMEOUT(!view.pendingPrompt().isEmpty(), kRunWait);
        QVERIFY2(view.pendingPrompt().contains(QStringLiteral("Pull the cable now")),
                 qPrintable(view.pendingPrompt()));
        QTest::qWait(300); // the run waits for the operator: it does not end by itself
        QVERIFY(view.isRunning());
        QCOMPARE(done.count(), 0);
        view.continueRun();
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, kRunWait);
        QVERIFY2(view.lastRun().status == HilRunStatus::Finished, qPrintable(view.lastRun().reason));
        QCOMPARE(view.lastRun().passed, 2);
        QVERIFY(view.pendingPrompt().isEmpty());
    }

    void GUI_07_stopEndsTheRunBetweenStepsAndLeavesNoCapture() {
        MockRig mock(QStringLiteral("gui07-cancel"));
        QVERIFY(mock.ok);
        const QString dir = freshOutputDir(QStringLiteral("gui-t072-cancel"));
        HilView view;
        view.setProfilePath(writeProfile(dir, QStringLiteral("q03ude-eth-3e-bin"),
                                         QStringLiteral("gui-cancel"), mock.port));
        view.setPlanPath(writePlan(dir, QStringLiteral("prompt.json"), kPromptPlan));
        const QString root = dir + QStringLiteral("/captured");
        view.setOutputRoot(root);
        QSignalSpy checked(&view, &HilView::checkDone);
        view.check();
        QTRY_COMPARE_WITH_TIMEOUT(checked.count(), 1, kWait);
        QSignalSpy done(&view, &HilView::runDone);
        view.runWith(QString(), true);
        QTRY_VERIFY_WITH_TIMEOUT(!view.pendingPrompt().isEmpty(), kRunWait);
        view.cancelRun(); // also releases the prompt the run waits on
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, kRunWait);
        QVERIFY2(view.lastRun().status == HilRunStatus::Cancelled, qPrintable(view.lastRun().reason));
        QCOMPARE(view.outcomeRows(), 1); // P-01 ended, P-02 never started
        QCOMPARE(view.outcomeCell(0, 0), QStringLiteral("P-01"));
        QVERIFY(!QDir(root + QStringLiteral("/gui-cancel")).exists());

        // and the view can run again
        view.runWith(QString(), true);
        QTRY_VERIFY_WITH_TIMEOUT(!view.pendingPrompt().isEmpty(), kRunWait);
        view.continueRun();
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 2, kRunWait);
        QVERIFY2(view.lastRun().status == HilRunStatus::Finished, qPrintable(view.lastRun().reason));
    }

    void GUI_07_aCaptureOfAMockNeverGoesUnderTheHardwareOnlyFolder() {
        Listener target;
        QVERIFY(target.ok);
        const QString dir = freshOutputDir(QStringLiteral("gui-t072-target"));
        HilHost host(QStringLiteral("gui07-target"));
        HilCheckInput input;
        input.profilePath = writeProfile(dir, QStringLiteral("q03ude-eth-3e-bin"),
                                         QStringLiteral("gui-target"), target.port());
        input.planPath = writePlan(dir, QStringLiteral("simple.json"), kSimplePlan);
        HilCheckResult check;
        QVERIFY(checkVia(host, input, &check));
        QVERIFY(check.loopbackTcp);

        const QString fakeRoot = dir + QStringLiteral("/repo/tests/vectors/captured");
        QDir().mkpath(fakeRoot);
        for (const CaptureSource source : {CaptureSource::MockPlc, CaptureSource::VirtualPlc}) {
            HilRunRequest request = requestFor(input, check, fakeRoot, source);
            request.capturedRoot = fakeRoot;
            request.skipTyping = true;
            HilRunResult r;
            QVERIFY(runVia(host, request, &r));
            QVERIFY2(r.status == HilRunStatus::OutputRefused, qPrintable(r.reason));
            QVERIFY(r.reason.contains(QStringLiteral("tests/vectors/captured")));
            // the same folder by another spelling
            request.outputRoot = dir + QStringLiteral("/repo/tests/vectors/../vectors/captured/");
            QVERIFY(runVia(host, request, &r));
            QVERIFY(r.status == HilRunStatus::OutputRefused);
        }
        // a loopback profile cannot be passed off as a real PLC
        HilRunRequest real = requestFor(input, check, dir + QStringLiteral("/other"), CaptureSource::RealPlc);
        real.capturedRoot = fakeRoot;
        real.skipTyping = true;
        HilRunResult r;
        QVERIFY(runVia(host, real, &r));
        QVERIFY(r.status == HilRunStatus::OutputRefused);
        QVERIFY(r.reason.contains(QStringLiteral("not to a real PLC")));
        QVERIFY(QDir(fakeRoot).isEmpty());
        QCOMPARE(target.connectionsAfter(300), 0);
    }

    void GUI_07_closingTheViewDuringARunStopsItWithinTheBound() {
        MockRig mock(QStringLiteral("gui07-close"));
        QVERIFY(mock.ok);
        const QString dir = freshOutputDir(QStringLiteral("gui-t072-close"));
        const int liveBefore = RunnerBase::liveRunners();
        const int foreignBefore = RunnerBase::foreignDeletes();
        {
            auto view = std::make_unique<HilView>();
            view->setProfilePath(writeProfile(dir, QStringLiteral("q03ude-eth-3e-bin"),
                                              QStringLiteral("gui-close"), mock.port));
            view->setPlanPath(writePlan(dir, QStringLiteral("prompt.json"), kPromptPlan));
            view->setOutputRoot(dir + QStringLiteral("/captured"));
            QSignalSpy checked(view.get(), &HilView::checkDone);
            view->check();
            QTRY_COMPARE_WITH_TIMEOUT(checked.count(), 1, kWait);
            view->runWith(QString(), true);
            QTRY_VERIFY_WITH_TIMEOUT(!view->pendingPrompt().isEmpty(), kRunWait);
            QElapsedTimer closing;
            closing.start();
            view.reset(); // cancel, release the prompt, stop the thread
            QVERIFY2(closing.elapsed() < RunnerThread::kDefaultStopTimeoutMs + 500,
                     qPrintable(QString::number(closing.elapsed())));
        }
        QTRY_COMPARE_WITH_TIMEOUT(RunnerBase::liveRunners(), liveBefore, kWait);
        QCOMPARE(RunnerBase::foreignDeletes(), foreignBefore);
    }

    void GUI_07_theMainWindowHoldsTheHilView() {
        MainWindow window;
        QVERIFY(window.hilView() != nullptr);
        QVERIFY(window.dockTitles().contains(QStringLiteral("HIL runner")));
        QVERIFY(window.dockWidget(QStringLiteral("HIL runner")) != nullptr);
    }
};

namespace mc::workbench::test {
QObject* makeHilSuite() {
    return new HilViewTest;
}
} // namespace mc::workbench::test

#include "tst_gui_hil.moc"
