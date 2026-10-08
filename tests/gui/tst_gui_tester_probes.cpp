// Phase 8 tester probes (T-074, Checkpoint H, tester step): cases the developers did not write.
//   - the HIL gate and the typed confirmation: every way the tester found to reach a connection
//     without the gate or without the typed profile id (a listener on loopback counts connections);
//   - the export rule for tests/vectors/captured: a capture of a mock must not land there;
//   - the GUI thread under load and under faults with the real views on screen;
//   - a HIL run stuck in a step while the host is destroyed;
//   - workspace files the developers' damaged-file list does not hold;
//   - T-078/T-079 (Qt 5.15): jsonInteger() against toInteger()'s rule, and UTF-8 HIL run lines.
// No real PLC: every target is a mock or a listener on loopback.
#include "gui_suites.h"
#include "gui_test_support.h"

#include "mc/core/protocol.h"
#include "mc_workbench/capture_export.h"
#include "mc_workbench/device_host.h"
#include "mc_workbench/device_tab.h"
#include "mc_workbench/tab_telemetry.h"
#include "mc_workbench/hil_confirm_dialog.h"
#include "mc_workbench/hil_host.h"
#include "mc_workbench/hil_prepare.h"
#include "mc_workbench/hil_view.h"
#include "mc_workbench/main_window.h"
#include "mc_workbench/mock_fault_panel.h"
#include "mc_workbench/mock_tab.h"
#include "mc_workbench/point_table_model.h"
#include "mc_workbench/qt_compat.h"
#include "mc_workbench/runner_base.h"
#include "mc_workbench/runner_thread.h"
#include "mc_workbench/workspace_controller.h"

#include <qpb/PropertyGroup.h>
#include <qpb/PropertyModel.h>
#include <qpb/Serialization.h>

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include <functional>
#include <limits>
#include <memory>

using namespace mc::workbench;
using namespace mc::workbench::test;

namespace {

constexpr int kRunWaitMs = 60000;

QString testsDir() {
    return QStringLiteral(MC_TESTS_SOURCE_DIR);
}

QJsonObject readJson(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}

void writeBytes(const QString& path, const QByteArray& bytes) {
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(bytes);
    }
}

// A copy of an example profile aimed at a loopback port.
QString writeProfile(const QString& dir, const QString& newId, quint16 port, int timeoutMs = 400,
                     const QString& host = QStringLiteral("127.0.0.1")) {
    QJsonObject root = readJson(testsDir() + QStringLiteral("/hil/profiles/q03ude-eth-3e-bin.example.json"));
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
    tcp.insert(QStringLiteral("host"), host);
    tcp.insert(QStringLiteral("port"), static_cast<int>(port == 0 ? 1 : port));
    tcp.insert(QStringLiteral("connectTimeoutMs"), 1000);
    transport.insert(QStringLiteral("tcp"), tcp);
    device.insert(QStringLiteral("transport"), transport);
    root.insert(QStringLiteral("device"), device);
    const QString path = dir + QStringLiteral("/") + newId + QStringLiteral(".json");
    writeBytes(path, QJsonDocument(root).toJson());
    return path;
}

// tests/hil/e2e/plan_3e.json without E-10 (E-09 stays: a read-only frame).
QString planWithReadOnly(const QString& dir) {
    QJsonObject plan = readJson(testsDir() + QStringLiteral("/hil/e2e/plan_3e.json"));
    QJsonArray kept;
    for (const QJsonValue& step : plan.value(QStringLiteral("steps")).toArray()) {
        if (step.toObject().value(QStringLiteral("id")).toString() != QLatin1String("E-10")) {
            kept.append(step);
        }
    }
    plan.insert(QStringLiteral("steps"), kept);
    const QString path = dir + QStringLiteral("/plan_ro.json");
    writeBytes(path, QJsonDocument(plan).toJson());
    return path;
}

constexpr const char* kSimplePlan = R"JSON({"schema":1,"plan":{"id":"t8_simple"},"steps":[
  {"id":"S-01","kind":"read","device":"D@s","expect":{"kind":"ok","values":[1234]}},
  {"id":"S-02","kind":"write","device":"D@s+1","values":[7,8],"readBack":true}
]})JSON";

constexpr const char* kOtherPlan = R"JSON({"schema":1,"plan":{"id":"t8_other"},"steps":[
  {"id":"S-01","kind":"write","device":"D@s+3","values":[9],"readBack":true}
]})JSON";

constexpr const char* kRefusedPlan = R"JSON({"schema":1,"plan":{"id":"t8_refuse"},"steps":[
  {"id":"R-02","kind":"write","device":"D5000","values":[1,2]}
]})JSON";

// A server on loopback that only counts connections.
struct Listener {
    Listener() : spy(&server, &QTcpServer::newConnection) { ok = server.listen(QHostAddress::LocalHost, 0); }
    quint16 port() const { return server.serverPort(); }
    int connectionsAfter(int ms) {
        QTest::qWait(ms);
        return spy.count() + (server.hasPendingConnections() ? 1 : 0);
    }
    QTcpServer server;
    QSignalSpy spy;
    bool ok{false};
};

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
    return waitFor<HilCheckResult>(spy, host.check(input), out);
}

bool runVia(HilHost& host, const HilRunRequest& request, HilRunResult* out) {
    QSignalSpy spy(&host, &HilHost::runFinished);
    return waitFor<HilRunResult>(spy, host.run(request), out, kRunWaitMs);
}

HilRunRequest requestFor(const HilCheckInput& input, const HilCheckResult& check, const QString& outputRoot) {
    HilRunRequest r;
    r.input = input;
    r.expectedDigest = check.digest;
    r.typedId = check.profileId;
    r.outputRoot = outputRoot;
    r.source = CaptureSource::MockPlc;
    r.benchReps = 3;
    return r;
}

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

} // namespace

class TesterProbesTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { registerRunnerMetaTypes(); }

    // ---- the gate and the typed confirmation: nothing reaches the listener -------------------

    void T8_noWayAroundTheTypedIdOrTheGateReachesTheListener() {
        const QString dir = freshOutputDir(QStringLiteral("t8-gate"));
        Listener listener;
        QVERIFY(listener.ok);
        const QString profile = writeProfile(dir, QStringLiteral("t8-ro"), listener.port());
        const QString roPlan = planWithReadOnly(dir);

        HilHost host(QStringLiteral("t8-gate"));
        HilCheckInput input;
        input.profilePath = profile;
        input.planPath = roPlan;
        HilCheckResult check;
        QVERIFY(checkVia(host, input, &check));
        QVERIFY2(check.ok(), qPrintable(check.errorText + check.refusalText));
        QVERIFY2(!check.readOnlyFrames.isEmpty(), "the plan must hold read-only frames");
        const QString out = dir + QStringLiteral("/out");

        const auto expectNotRun = [&](const char* what, const HilRunRequest& request, HilRunStatus status) {
            HilRunResult result;
            QVERIFY2(runVia(host, request, &result), what);
            QVERIFY2(result.status == status, what);
            QVERIFY2(!QFileInfo::exists(out + QStringLiteral("/t8-ro")), what);
        };

        { // --yes style skip with read-only frames and nothing typed
            HilRunRequest r = requestFor(input, check, out);
            r.typedId.clear();
            r.skipTyping = true;
            expectNotRun("skipTyping, nothing typed, read-only frames", r, HilRunStatus::NotConfirmed);
        }
        { // skip with a wrong id
            HilRunRequest r = requestFor(input, check, out);
            r.typedId = QStringLiteral("anything");
            r.skipTyping = true;
            expectNotRun("skipTyping, wrong id", r, HilRunStatus::NotConfirmed);
        }
        { // other case
            HilRunRequest r = requestFor(input, check, out);
            r.typedId = QStringLiteral("T8-RO");
            expectNotRun("typed id in another case", r, HilRunStatus::NotConfirmed);
        }
        { // a prefix / a longer id
            HilRunRequest r = requestFor(input, check, out);
            r.typedId = QStringLiteral("t8-r");
            expectNotRun("typed id a prefix", r, HilRunStatus::NotConfirmed);
            r.typedId = QStringLiteral("t8-ro-");
            expectNotRun("typed id longer", r, HilRunStatus::NotConfirmed);
            r.typedId = QStringLiteral("t8-ro\nx");
            expectNotRun("typed id with a line", r, HilRunStatus::NotConfirmed);
        }
        { // no digest / another digest
            HilRunRequest r = requestFor(input, check, out);
            r.expectedDigest.clear();
            expectNotRun("empty digest", r, HilRunStatus::NotConfirmed);
            r.expectedDigest = QStringLiteral("0000");
            expectNotRun("wrong digest", r, HilRunStatus::NotConfirmed);
        }
        { // a check of another plan does not confirm this one
            const QString otherPath = dir + QStringLiteral("/other.json");
            writeBytes(otherPath, kOtherPlan);
            HilCheckInput otherInput = input;
            otherInput.planPath = otherPath;
            HilCheckResult otherCheck;
            QVERIFY(checkVia(host, otherInput, &otherCheck));
            QVERIFY(otherCheck.ok());
            HilRunRequest r = requestFor(input, otherCheck, out);
            expectNotRun("digest of another plan", r, HilRunStatus::NotConfirmed);
        }
        { // the plan is replaced by a refused one after the check
            const QString swap = dir + QStringLiteral("/swap.json");
            writeBytes(swap, kSimplePlan);
            HilCheckInput swapInput = input;
            swapInput.planPath = swap;
            HilCheckResult swapCheck;
            QVERIFY(checkVia(host, swapInput, &swapCheck));
            QVERIFY2(swapCheck.ok(), qPrintable(swapCheck.errorText));
            writeBytes(swap, kRefusedPlan);
            HilRunRequest r = requestFor(swapInput, swapCheck, out);
            expectNotRun("plan swapped to a refused plan after the check", r, HilRunStatus::GateRefused);
            writeBytes(swap, kOtherPlan);
            expectNotRun("plan swapped to another passing plan after the check", r, HilRunStatus::NotConfirmed);
        }
        { // the profile is re-aimed after the check
            Listener second;
            QVERIFY(second.ok);
            const QString moved = writeProfile(dir, QStringLiteral("t8-ro"), second.port());
            QCOMPARE(moved, profile);
            HilRunRequest r = requestFor(input, check, out);
            expectNotRun("profile re-aimed after the check", r, HilRunStatus::NotConfirmed);
            QCOMPARE(second.connectionsAfter(300), 0);
            writeProfile(dir, QStringLiteral("t8-ro"), listener.port());
        }
        { // a refused plan, run directly with the digest of a good check
            const QString refused = dir + QStringLiteral("/refused.json");
            writeBytes(refused, kRefusedPlan);
            HilCheckInput refusedInput = input;
            refusedInput.planPath = refused;
            HilRunRequest r = requestFor(refusedInput, check, out);
            r.skipTyping = true;
            expectNotRun("refused plan", r, HilRunStatus::GateRefused);
        }
        QCOMPARE(listener.connectionsAfter(500), 0);
    }

    void T8_theConfirmationDialogCannotBeSkippedForReadOnlyFrames() {
        const QString dir = freshOutputDir(QStringLiteral("t8-dialog"));
        Listener listener;
        QVERIFY(listener.ok);
        HilView view;
        view.setProfilePath(writeProfile(dir, QStringLiteral("t8-dlg"), listener.port()));
        view.setPlanPath(planWithReadOnly(dir));
        view.setOutputRoot(dir + QStringLiteral("/out"));
        QSignalSpy checked(&view, &HilView::checkDone);
        view.check();
        QTRY_COMPARE_WITH_TIMEOUT(checked.count(), 1, kWait);
        QVERIFY2(view.lastCheck().ok(), qPrintable(view.gateText()));
        QVERIFY(!view.lastCheck().readOnlyFrames.isEmpty());
        QVERIFY(view.canRun());

        bool acceptedByCall = true;
        bool enabledAtStart = true;
        bool skipOffered = true;
        quint64 token = 99;
        {
            DialogDriver driver([&](HilConfirmDialog* d) {
                enabledAtStart = d->acceptEnabled() || d->okButton()->isEnabled();
                skipOffered = d->skipTypingAvailable();
                d->setSkipTyping(true);
                d->accept(); // programmatic accept without the typed id
                acceptedByCall = d->result() == QDialog::Accepted;
                QTest::keyClick(d, Qt::Key_Return);
                QTest::keyClick(d, Qt::Key_Enter);
                const bool stillOpen = d->isVisible();
                if (stillOpen) {
                    d->okButton()->click();
                }
                acceptedByCall = acceptedByCall || d->result() == QDialog::Accepted;
                if (d->isVisible()) {
                    d->reject();
                }
            });
            token = view.requestRun();
            QVERIFY(driver.seen());
        }
        QVERIFY(!enabledAtStart);
        QVERIFY(!skipOffered);
        QVERIFY(!acceptedByCall);
        QCOMPARE(token, quint64(0));
        QVERIFY(!view.isRunning());
        QCOMPARE(listener.connectionsAfter(500), 0);
    }

    void T8_aRefusedPlanOffersNoRunInTheView() {
        const QString dir = freshOutputDir(QStringLiteral("t8-refused-view"));
        Listener listener;
        QVERIFY(listener.ok);
        const QString plan = dir + QStringLiteral("/p.json");
        writeBytes(plan, kRefusedPlan);
        HilView view;
        view.setProfilePath(writeProfile(dir, QStringLiteral("t8-refv"), listener.port()));
        view.setPlanPath(plan);
        view.setOutputRoot(dir + QStringLiteral("/out"));
        QSignalSpy checked(&view, &HilView::checkDone);
        view.check();
        QTRY_COMPARE_WITH_TIMEOUT(checked.count(), 1, kWait);
        QVERIFY(!view.lastCheck().ok());
        QVERIFY(!view.canRun());
        bool dialogSeen = false;
        DialogDriver driver([&](HilConfirmDialog* d) {
            dialogSeen = true;
            d->reject();
        });
        QCOMPARE(view.requestRun(), quint64(0));
        QTest::qWait(200);
        QVERIFY(!dialogSeen);
        QCOMPARE(listener.connectionsAfter(300), 0);
    }

    // ---- the export rule ---------------------------------------------------------------------

    void T8_aMockCaptureIsRefusedUnderVectorsCapturedForEveryProfileId() {
        const QString dir = freshOutputDir(QStringLiteral("t8-export"));
        const QString tree = dir + QStringLiteral("/tree");
        QDir().mkpath(tree + QStringLiteral("/tests/vectors"));
        const QString capturedRoot = tree + QStringLiteral("/tests/vectors/captured");
        MockRig mock(QStringLiteral("t8-export-mock"));
        QVERIFY(mock.ok);
        mock.host.setWords(QStringLiteral("D100"), {1234});
        // The profile id "captured" is a legal folder name; the capture folder is <root>/<id>.
        const QString profile = writeProfile(dir, QStringLiteral("captured"), mock.port);
        const QString plan = dir + QStringLiteral("/p.json");
        writeBytes(plan, kSimplePlan);

        HilHost host(QStringLiteral("t8-export"));
        HilCheckInput input;
        input.profilePath = profile;
        input.planPath = plan;
        HilCheckResult check;
        QVERIFY(checkVia(host, input, &check));
        QVERIFY2(check.ok(), qPrintable(check.errorText + check.refusalText));

        HilRunRequest r = requestFor(input, check, tree + QStringLiteral("/tests/vectors"));
        r.capturedRoot = capturedRoot;
        r.overwrite = true;
        HilRunResult result;
        QVERIFY(runVia(host, r, &result));
        const bool landed = QFileInfo::exists(capturedRoot + QStringLiteral("/steps.vec")) ||
                            QFileInfo::exists(capturedRoot + QStringLiteral("/run.meta"));
        // T-074 tester finding F-1 (MEDIUM): the rule is checked on <root> only, the capture goes to
        // <root>/<profile id>. Expected-fail until fixed; once fixed this XPASSes: delete the line.
        QVERIFY2(result.status == HilRunStatus::OutputRefused && !landed,
                 qPrintable(QStringLiteral("a MOCK capture was written into <repo>/tests/vectors/captured/ "
                                           "(status %1, files landed: %2, reason: %3)")
                                .arg(static_cast<int>(result.status))
                                .arg(landed)
                                .arg(result.reason)));
    }

    void T8_loopbackSpellings() {
        // The guard that stops "Real PLC" for a mock only knows these spellings; report what it says.
        const QStringList spellings = {QStringLiteral("127.0.0.1"), QStringLiteral("localhost"),
                                       QStringLiteral("LOCALHOST"), QStringLiteral("localhost."),
                                       QStringLiteral("::1"),       QStringLiteral("[::1]"),
                                       QStringLiteral("127.1"),     QStringLiteral("127.0.0.2"),
                                       QStringLiteral("0.0.0.0"),   QStringLiteral("::ffff:127.0.0.1"),
                                       QStringLiteral(" 127.0.0.1 "), QStringLiteral("0x7f.0.0.1"),
                                       QStringLiteral("2130706433")};
        for (const QString& s : spellings) {
            qInfo("isLoopbackHost(\"%s\") = %d", qPrintable(s), isLoopbackHost(s) ? 1 : 0);
        }
        QVERIFY(isLoopbackHost(QStringLiteral("127.0.0.1")));
    }

    // ---- T-075 batch tester (tester9): client churn on one mock tab --------------------------------

    // One steady client keeps asking while other clients connect, send half a request or junk, and
    // leave (reset or orderly close), with the trace decoder toggled meanwhile. The steady client and
    // a fresh client at the end must always get the right words: no byte of one link reaches another.
    void T9_clientChurnNeverDisturbsAnAnsweringClient() {
        MockRig mock(QStringLiteral("t9-churn"));
        QVERIFY(mock.ok);
        mc::Request read;
        read.op = mc::Op::ReadWords;
        read.head = mc::Device{mc::DeviceType::D, 100};
        read.count = 4;
        const auto encoded = mc::McProtocol(mc::FrameConfig::frame3E()).encode(read);
        QVERIFY(encoded.hasValue());
        const QByteArray whole(reinterpret_cast<const char*>(encoded.value().data()),
                               static_cast<QByteArray::size_type>(encoded.value().size()));
        const auto connectSock = [&](QTcpSocket& s) {
            s.connectToHost(QHostAddress::LocalHost, mock.port);
            s.setSocketOption(QAbstractSocket::LowDelayOption, 1);
            return s.waitForConnected(kWait);
        };
        const auto askAndCheck = [&](QTcpSocket& s, const char* what) {
            s.write(whole);
            s.flush();
            QByteArray got;
            (void)QTest::qWaitFor(
                [&]() {
                    got += s.readAll();
                    return got.size() >= 19;
                },
                kWait);
            QVERIFY2(got.size() == 19, what);
            QVERIFY2(static_cast<quint8>(got[0]) == 0xD0 && got.at(9) == 0 && got.at(10) == 0,
                     what);
            QVERIFY2(static_cast<quint8>(got[11]) == 10 && static_cast<quint8>(got[13]) == 20 &&
                         static_cast<quint8>(got[15]) == 30 && static_cast<quint8>(got[17]) == 40,
                     what);
        };
        QTcpSocket steady;
        QVERIFY(connectSock(steady));
        for (int round = 0; round < 60; ++round) {
            if (round % 7 == 3) {
                mock.host.setTraceDecode(round % 2 == 0);
            }
            QTcpSocket gone;
            QVERIFY(connectSock(gone));
            if (round % 3 == 0) {
                gone.write(whole.constData(), 1 + (round % (whole.size() - 1)));
            } else if (round % 3 == 1) {
                gone.write(QByteArray(20, '\xFF'));
            } else {
                gone.write(whole.constData(), whole.size() - 1);
            }
            gone.flush();
            QTest::qWait(round % 5);
            askAndCheck(steady, "steady client while another link holds a partial request");
            if (round % 2 == 0) {
                gone.abort();
            } else {
                gone.disconnectFromHost();
            }
        }
        QTcpSocket fresh;
        QVERIFY(connectSock(fresh));
        askAndCheck(fresh, "fresh client after the churn");
        askAndCheck(steady, "steady client after the churn");
    }

    // ---- T-076 batch tester (tester9): skipTyping for non-loopback spellings and edited profiles --

    // Every host spelling that is NOT loopback (all aimed at TEST-NET-1 / reserved / invalid names, never
    // at anything real) must be refused by the runner thread when skipTyping is forced: NotConfirmed, no
    // capture folder, quickly (no connect attempt). A spelling the profile loader refuses is also safe.
    void T9_noNonLoopbackSpellingGetsASkippedTyping() {
        const QString dir = freshOutputDir(QStringLiteral("t9-spell"));
        const QString plan = dir + QStringLiteral("/simple.json");
        writeBytes(plan, kSimplePlan);
        const QStringList nonLoopback = {
            QStringLiteral("192.0.2.10"),          QStringLiteral("192.000.002.010"),
            QStringLiteral("0xC000020A"),          QStringLiteral("3221226010"),
            QStringLiteral("::ffff:192.0.2.10"),   QStringLiteral("[::ffff:192.0.2.10]"),
            QStringLiteral("0.0.0.0"),             QStringLiteral("::"),
            QStringLiteral("localhost.example.invalid"), QStringLiteral("127.0.0.1.example.invalid"),
            QStringLiteral("localhost.."),         QStringLiteral("localhost:502"),
            QStringLiteral("127.0.0.1:502"),       QStringLiteral("[::1]:502"),
            QStringLiteral("127.0.0.1 evil.invalid"), QStringLiteral("evil.invalid"),
            QStringLiteral("128.0.0.1"),           QStringLiteral("::ffff:0:127.0.0.1"),
            QStringLiteral("0127.0.0.1"),          QStringLiteral("1.invalid"),
            QStringLiteral("localhost.localdomain"), QStringLiteral("ip6-localhost"),
            QStringLiteral(""),                    QStringLiteral(" ")};
        HilHost host(QStringLiteral("t9-spell"));
        const QString out = dir + QStringLiteral("/out");
        int refusedAtLoad = 0;
        int refusedByRunner = 0;
        QStringList bad;
        for (const QString& spelling : nonLoopback) {
            // 0127.0.0.1 is 87.0.0.1 (a public address): only the check is made, never a run
            const bool checkOnly = spelling == QLatin1String("0127.0.0.1");
            const QString profile = writeProfile(dir, QStringLiteral("t9-sp"), 0, 400, spelling);
            HilCheckInput input;
            input.profilePath = profile;
            input.planPath = plan;
            HilCheckResult check;
            QVERIFY(checkVia(host, input, &check));
            if (!check.ok()) {
                ++refusedAtLoad;
                qInfo("spelling '%s': profile/check refused (%s)", qPrintable(spelling),
                      qPrintable(check.errorText.left(60)));
                continue;
            }
            if (check.loopbackTcp) {
                bad.push_back(spelling + QStringLiteral(" (loopbackTcp)"));
                continue;
            }
            if (skipTypingAllowed(check)) {
                bad.push_back(spelling + QStringLiteral(" (skipTypingAllowed)"));
                continue;
            }
            if (checkOnly) {
                qInfo("spelling '%s': check only, loopbackTcp=0", qPrintable(spelling));
                continue;
            }
            HilRunRequest r = requestFor(input, check, out);
            r.typedId.clear();
            r.skipTyping = true;
            QElapsedTimer timer;
            timer.start();
            HilRunResult result;
            QVERIFY2(runVia(host, r, &result), qPrintable(spelling));
            if (result.status != HilRunStatus::NotConfirmed || timer.elapsed() > 1500 ||
                QFileInfo::exists(out + QStringLiteral("/t9-sp"))) {
                bad.push_back(spelling + QStringLiteral(" (run status %1)").arg(int(result.status)));
            } else {
                ++refusedByRunner;
            }
        }
        qInfo("non-loopback spellings: %d refused at load, %d refused by the runner, %d bad",
              refusedAtLoad, refusedByRunner, int(bad.size()));
        QVERIFY2(bad.isEmpty(), qPrintable(bad.join(QStringLiteral("; "))));
    }

    // The loopback verdict of every spelling the owner listed, as the check derives it from a profile.
    void T9_loopbackVerdictOfTheListedSpellings() {
        const QString dir = freshOutputDir(QStringLiteral("t9-loopverdict"));
        const QString plan = dir + QStringLiteral("/simple.json");
        writeBytes(plan, kSimplePlan);
        const QStringList spellings = {
            QStringLiteral("127.0.0.1"),       QStringLiteral("127.000.000.001"), QStringLiteral("0x7f000001"),
            QStringLiteral("localhost"),       QStringLiteral("localhost."),      QStringLiteral("[::1]"),
            QStringLiteral("::1"),             QStringLiteral("::ffff:127.0.0.1"), QStringLiteral("127.1"),
            QStringLiteral("LocalHost"),       QStringLiteral(" 127.0.0.1")};
        HilHost host(QStringLiteral("t9-verdict"));
        for (const QString& spelling : spellings) {
            const QString profile = writeProfile(dir, QStringLiteral("t9-lv"), 0, 400, spelling);
            HilCheckInput input;
            input.profilePath = profile;
            input.planPath = plan;
            HilCheckResult check;
            QVERIFY(checkVia(host, input, &check));
            qInfo("profile host '%s': check ok=%d loopbackTcp=%d skipAllowed=%d", qPrintable(spelling),
                  check.ok() ? 1 : 0, check.loopbackTcp ? 1 : 0, skipTypingAllowed(check) ? 1 : 0);
        }
    }

    // The profile changes between the check and the run: loopback -> another host / a COM profile.
    // The runner re-derives everything, so skipTyping (and the old digest) buy nothing, through the
    // host and through the view.
    void T9_aProfileEditedAfterTheCheckCannotSkipTyping() {
        const QString dir = freshOutputDir(QStringLiteral("t9-edit"));
        Listener listener;
        QVERIFY(listener.ok);
        const QString plan = dir + QStringLiteral("/simple.json");
        writeBytes(plan, kSimplePlan);
        const QString out = dir + QStringLiteral("/out");
        const QString profile = writeProfile(dir, QStringLiteral("t9-ed"), listener.port());
        HilHost host(QStringLiteral("t9-edit"));
        HilCheckInput input;
        input.profilePath = profile;
        input.planPath = plan;
        HilCheckResult check;
        QVERIFY(checkVia(host, input, &check));
        QVERIFY2(check.ok() && check.loopbackTcp, qPrintable(check.errorText));
        QVERIFY(skipTypingAllowed(check));

        // 1. host changed to TEST-NET-1 after the check
        writeProfile(dir, QStringLiteral("t9-ed"), listener.port(), 400, QStringLiteral("192.0.2.10"));
        {
            HilRunRequest r = requestFor(input, check, out);
            r.typedId.clear();
            r.skipTyping = true;
            HilRunResult result;
            QVERIFY(runVia(host, r, &result));
            QVERIFY2(result.status == HilRunStatus::NotConfirmed, qPrintable(result.reason));
            QVERIFY(!QFileInfo::exists(out + QStringLiteral("/t9-ed")));
        }
        // 2. the same, as the view does it (skipTyping, no id, the stale loopback check on screen)
        {
            writeProfile(dir, QStringLiteral("t9-ed"), listener.port());
            HilView view;
            view.setProfilePath(profile);
            view.setPlanPath(plan);
            view.setOutputRoot(out);
            QSignalSpy checked(&view, &HilView::checkDone);
            view.check();
            QTRY_COMPARE_WITH_TIMEOUT(checked.count(), 1, kWait);
            QVERIFY(view.lastCheck().loopbackTcp);
            writeProfile(dir, QStringLiteral("t9-ed"), listener.port(), 400, QStringLiteral("192.0.2.10"));
            QSignalSpy done(&view, &HilView::runDone);
            QVERIFY(view.runWith(QString(), true) != 0);
            QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, kWait);
            QVERIFY2(view.lastRun().status == HilRunStatus::NotConfirmed, qPrintable(view.lastRun().reason));
            QVERIFY(!QFileInfo::exists(out + QStringLiteral("/t9-ed")));
        }
        // 3. profile replaced by a COM example (same id) after the check
        {
            QJsonObject com = readJson(testsDir() + QStringLiteral("/hil/profiles/q03ude-c24-3c-f4.example.json"));
            QJsonObject p = com.value(QStringLiteral("profile")).toObject();
            p.insert(QStringLiteral("id"), QStringLiteral("t9-ed"));
            com.insert(QStringLiteral("profile"), p);
            writeBytes(profile, QJsonDocument(com).toJson());
            HilRunRequest r = requestFor(input, check, out);
            r.typedId.clear();
            r.skipTyping = true;
            HilRunResult result;
            QElapsedTimer timer;
            timer.start();
            QVERIFY(runVia(host, r, &result));
            QVERIFY2(result.status == HilRunStatus::NotConfirmed, qPrintable(result.reason));
            QVERIFY(timer.elapsed() < 1500);
            QVERIFY(!QFileInfo::exists(out + QStringLiteral("/t9-ed")));
        }
        QCOMPARE(listener.connectionsAfter(400), 0);
    }

    // ---- the GUI thread under load and faults, with the real views -----------------------------

    void T8_theWindowStaysResponsiveUnderFloodAndFaultsAndClosesCleanly() {
        const int liveBefore = RunnerBase::liveRunners();
        const int foreignBefore = RunnerBase::foreignDeletes();
        qint64 maxGap = 0;
        {
            MainWindow window;
            window.resize(1280, 800);
            window.show();
            DevicePane* devices = window.devicePane();
            MockPane* mocks = window.mockPane();
            MockTab* mockTab[2] = {mocks->addMock(), mocks->addMock()};
            for (MockTab* tab : mockTab) {
                tab->startServing();
            }
            QTRY_VERIFY_WITH_TIMEOUT(mockTab[0]->isServing() && mockTab[1]->isServing(), kWait);

            DeviceTab* tabs[6];
            for (int i = 0; i < 6; ++i) {
                tabs[i] = devices->addDevice();
                mc::McDeviceConfig cfg;
                cfg.session.cycleIntervalMs = 0; // back to back
                cfg.tcp.host = QStringLiteral("127.0.0.1");
                cfg.tcp.connectTimeoutMs = 500;
                if (i < 4) {
                    cfg.tcp.port = mockTab[i / 2]->servingPort();
                    cfg.subscriptions = {{QStringLiteral("D0"), 100}, {QStringLiteral("M0"), 64}};
                } else if (i == 4) {
                    cfg.tcp.port = 1; // refused
                    cfg.subscriptions = {{QStringLiteral("D0"), 4}};
                } else {
                    cfg.transport = mc::TransportKind::Serial;
                    cfg.serial.portName = QStringLiteral("COM99"); // no such port
                    cfg.subscriptions = {{QStringLiteral("D0"), 4}};
                }
                QSignalSpy applied(tabs[i], &DeviceTab::configApplied);
                QString error;
                QVERIFY2(tabs[i]->setConfig(cfg, &error), qPrintable(error));
                QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, kWait);
            }
            for (DeviceTab* tab : tabs) {
                tab->connectToPlc();
            }
            QTRY_COMPARE_WITH_TIMEOUT(tabs[0]->linkState(), mc::LinkState::Connected, kWait);
            QTRY_COMPARE_WITH_TIMEOUT(tabs[3]->linkState(), mc::LinkState::Connected, kWait);

            Ticker ticker;
            QTest::qWait(1500);
            auto* mute = mockTab[0]->faultPanel()->findChild<QCheckBox*>();
            QVERIFY(mute != nullptr);
            mute->click(); // a fault in one mock while everything floods
            QTest::qWait(2500);
            mute->click(); // and back
            QTest::qWait(1500);
            maxGap = ticker.maxGapMs();
            qInfo("T8 responsive: largest GUI event gap %lld ms (6 device tabs flooding, 2 mocks, fault)",
                  static_cast<long long>(maxGap));
            QVERIFY2(maxGap < 400, qPrintable(QString::number(maxGap)));
            QCOMPARE(tabs[3]->linkState(), mc::LinkState::Connected);
        } // the window closes: every runner thread joins
        QCOMPARE(RunnerBase::liveRunners(), liveBefore);
        QCOMPARE(RunnerBase::foreignDeletes(), foreignBefore);
    }

    // ---- a stuck HIL step while the host is destroyed -------------------------------------------

    void T8_destroyingTheHilHostDuringAStuckStepIsBounded() {
        const QString dir = freshOutputDir(QStringLiteral("t8-stuck"));
        Listener silent; // accepts nothing, answers nothing: the first read waits for its timeout
        QVERIFY(silent.ok);
        const QString plan = dir + QStringLiteral("/p.json");
        writeBytes(plan, kSimplePlan);
        const QString profile = writeProfile(dir, QStringLiteral("t8-stuck"), silent.port(), 20000);
        HilCheckInput input;
        input.profilePath = profile;
        input.planPath = plan;
        qint64 ms = -1;
        {
            auto host = std::make_unique<HilHost>(QStringLiteral("t8-stuck"));
            HilCheckResult check;
            QVERIFY(checkVia(*host, input, &check));
            QVERIFY(check.ok());
            QSignalSpy started(host.get(), &HilHost::stepStarted);
            host->run(requestFor(input, check, dir + QStringLiteral("/out")));
            QTRY_VERIFY_WITH_TIMEOUT(started.count() >= 1, kWait);
            QTest::qWait(500); // inside the step now
            QElapsedTimer timer;
            timer.start();
            host.reset();
            ms = timer.elapsed();
        }
        qInfo("T8 stuck HIL step: destroying the host took %lld ms", static_cast<long long>(ms));
        QVERIFY2(ms < 3000 + 2500, qPrintable(QString::number(ms)));
        QTest::qWait(1000); // the abandoned thread must end by itself without a crash
    }

    // ---- workspace files the developers' list does not hold ------------------------------------

    void T8_moreDamagedWorkspaceFilesAreRefusedAndTheWindowIsUnchanged() {
        const QString dir = freshOutputDir(QStringLiteral("t8-workspace"));
        MainWindow window;
        window.setWorkspaceDialogs(false);
        QVERIFY(window.devicePane()->addDevice() != nullptr);
        window.mockPane()->addMock();
        const Workspace good = window.workspace()->capture();
        QJsonObject goodJson = QJsonDocument::fromJson(workspaceToBytes(good)).object();
        const QByteArray goodBytes = workspaceToBytes(good);
        QVERIFY(!goodBytes.isEmpty());

        struct Case {
            const char* name;
            QByteArray bytes;
            bool isDir;
        };
        QList<Case> cases;
        cases.append({"trailing garbage", goodBytes + QByteArray(" garbage"), false});
        cases.append({"deep nesting", QByteArray(5000, '[') + QByteArray(5000, ']'), false});
        cases.append({"null", QByteArray("null"), false});
        cases.append({"string", QByteArray("\"workspace\""), false});
        cases.append({"huge number", QByteArray("{\"format\":\"mc_workbench_workspace\",\"version\":1e999}"), false});
        {
            QJsonObject o = goodJson;
            o.insert(QStringLiteral("surprise"), 1);
            cases.append({"unknown top-level key", QJsonDocument(o).toJson(), false});
        }
        {
            QJsonObject o = goodJson;
            QJsonArray mocks = o.value(QStringLiteral("mocks")).toArray();
            QJsonObject m = mocks.at(0).toObject();
            QJsonObject settings = m.value(QStringLiteral("settings")).toObject();
            QJsonObject serving = settings.value(QStringLiteral("Serving")).toObject();
            serving.insert(QStringLiteral("tcpPort"), 70000);
            settings.insert(QStringLiteral("Serving"), serving);
            m.insert(QStringLiteral("settings"), settings);
            mocks.replace(0, m);
            o.insert(QStringLiteral("mocks"), mocks);
            cases.append({"mock tcpPort 70000", QJsonDocument(o).toJson(), false});
        }
        {
            QJsonObject o = goodJson;
            QJsonArray mocks = o.value(QStringLiteral("mocks")).toArray();
            QJsonObject m = mocks.at(0).toObject();
            QJsonObject p;
            p.insert(QStringLiteral("head"), QStringLiteral("D16777200"));
            p.insert(QStringLiteral("kind"), QStringLiteral("words"));
            QJsonArray values;
            for (int i = 0; i < 100; ++i) {
                values.append(i);
            }
            p.insert(QStringLiteral("values"), values); // runs past the end of D
            m.insert(QStringLiteral("memory"), QJsonArray{p});
            mocks.replace(0, m);
            o.insert(QStringLiteral("mocks"), mocks);
            cases.append({"preset past the end of D", QJsonDocument(o).toJson(), false});
        }
        cases.append({"path is a directory", QByteArray(), true});

        const int devicesBefore = window.devicePane()->deviceCount();
        const int mocksBefore = window.mockPane()->mockCount();
        const int liveBefore = RunnerBase::liveRunners();
        for (const Case& c : cases) {
            const QString file = QDir(dir).absoluteFilePath(QStringLiteral("w.json"));
            QFile::remove(file);
            QString path = file;
            if (c.isDir) {
                QDir().mkpath(file);
            } else {
                writeBytes(file, c.bytes);
            }
            QSignalSpy finished(window.workspace(), &WorkspaceController::loadFinished);
            window.workspace()->load(path);
            QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kWait);
            const bool ok = finished.at(0).at(1).toBool();
            const QString message = finished.at(0).at(2).toString();
            qInfo("damaged '%s': ok=%d message=%s", c.name, ok ? 1 : 0, qPrintable(message.left(120)));
            QVERIFY2(!ok, c.name);
            QVERIFY2(!message.isEmpty(), c.name);
            QVERIFY2(window.devicePane()->deviceCount() == devicesBefore &&
                         window.mockPane()->mockCount() == mocksBefore &&
                         RunnerBase::liveRunners() == liveBefore,
                     c.name);
            if (c.isDir) {
                QDir(file).removeRecursively();
            }
        }
        // the window is still usable
        QVERIFY(window.devicePane()->addDevice() != nullptr);
    }

    // ---- Re-test of F-1: other spellings of the protected folder ------------------------------

    void T8_theExportRuleHoldsForEveryIdAndRootSpelling() {
        const QString dir = freshOutputDir(QStringLiteral("t8-spell"));
        const QString tree = dir + QStringLiteral("/tree");
        const QString vectors = tree + QStringLiteral("/tests/vectors");
        const QString captured = vectors + QStringLiteral("/captured");
        const auto cleanTree = [&]() {
            QDir(tree).removeRecursively();
            QDir().mkpath(vectors);
            QDir().mkpath(tree + QStringLiteral("/tests/x"));
        };
        const auto landed = [&]() {
            QDir c(captured);
            return c.exists() && !c.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty();
        };
        MockRig mock(QStringLiteral("t8-spell-mock"));
        QVERIFY(mock.ok);
        mock.host.setWords(QStringLiteral("D100"), {1234});
        const QString plan = dir + QStringLiteral("/p.json");
        writeBytes(plan, kSimplePlan);

        cleanTree();
        // a junction inside the scratch area that points at tests/vectors
        const QString junction = dir + QStringLiteral("/junction");
        QProcess link;
        link.start(QStringLiteral("cmd"), {QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"),
                                           QDir::toNativeSeparators(junction), QDir::toNativeSeparators(vectors)});
        const bool haveJunction = link.waitForFinished(10000) && link.exitCode() == 0;
        qInfo("junction available: %d", haveJunction ? 1 : 0);

        QStringList roots = {vectors, vectors + QStringLiteral("/"), vectors + QStringLiteral("/."),
                             tree + QStringLiteral("/tests/x/../vectors"),
                             tree + QStringLiteral("/TESTS/Vectors"), tree + QStringLiteral("/tests/vectors.")};
        if (haveJunction) {
            roots << junction;
        }
        const QStringList ids = {QStringLiteral("captured"), QStringLiteral("Captured"), QStringLiteral("CAPTURED"),
                                 QStringLiteral("captured."), QStringLiteral("captured...")};
        HilHost host(QStringLiteral("t8-spell"));
        QStringList leaks;
        int runs = 0;
        for (const QString& id : ids) {
            for (const QString& root : roots) {
                for (const bool withRoot : {true, false}) {
                    cleanTree();
                    const QString profile = writeProfile(dir, id, mock.port);
                    HilCheckInput input;
                    input.profilePath = profile;
                    input.planPath = plan;
                    HilCheckResult check;
                    QVERIFY(checkVia(host, input, &check));
                    if (!check.ok()) {
                        continue; // the loader refuses this id: nothing can run
                    }
                    HilRunRequest r = requestFor(input, check, root);
                    r.capturedRoot = withRoot ? captured : QString();
                    r.overwrite = true;
                    HilRunResult result;
                    QVERIFY(runVia(host, r, &result));
                    ++runs;
                    if (landed()) {
                        leaks.append(QStringLiteral("HIL id='%1' root='%2' capturedRoot=%3 status=%4")
                                         .arg(id, root).arg(withRoot).arg(static_cast<int>(result.status)));
                    }
                }
            }
        }
        qInfo("HIL spellings: %d runs reached the writer", runs);

        // The capture panel's "Save to folder": the same spellings through a device tab's capture.
        for (const QString& id : {QStringLiteral("captured"), QStringLiteral("captured."), QStringLiteral("Captured")}) {
            DeviceHost dev(QStringLiteral("t8-spell-dev"), deviceConfig(mock.port, 10));
            TabTelemetry tele(QStringLiteral("PLC 1"), TabTelemetry::Kind::Device);
            tele.attachHost(&dev);
            QSignalSpy done(&dev, &DeviceHost::commandDone);
            QSignalSpy linkSpy(&dev, &DeviceHost::linkStateChanged);
            QSignalSpy saved(&tele, &TabTelemetry::captureSaved);
            CaptureSettings settings;
            settings.profileId = id;
            settings.source = CaptureSource::MockPlc;
            QVERIFY(resultOf(done, tele.startCapture(settings)).ok);
            dev.connectToPlc();
            QTRY_VERIFY_WITH_TIMEOUT(sawLink(linkSpy, mc::LinkState::Connected), kWait);
            QTRY_VERIFY_WITH_TIMEOUT(tele.captureStatus().chunks >= 6, kWait);
            QVERIFY(resultOf(done, tele.stopCapture()).ok);
            QTRY_VERIFY_WITH_TIMEOUT(tele.captureStatus().hasData && !tele.captureStatus().active, kWait);
            int expected = 0;
            for (const QString& root : roots) {
                for (const bool withRoot : {true, false}) {
                    cleanTree();
                    CaptureSaveRequest request;
                    request.outputRoot = root;
                    request.capturedRoot = withRoot ? captured : QString();
                    request.overwrite = true;
                    tele.saveCapture(request);
                    ++expected;
                    QTRY_VERIFY_WITH_TIMEOUT(saved.count() == expected, kWait);
                    if (landed() || saved.last().at(0).value<CaptureSaveResult>().ok) {
                        leaks.append(QStringLiteral("PANEL id='%1' root='%2' capturedRoot=%3").arg(id, root).arg(withRoot));
                    }
                }
            }
        }
        for (const QString& leak : leaks) {
            qInfo("LEAK %s", qPrintable(leak));
        }
        // T-074 re-test finding F-1b (MEDIUM): trailing-dot ids / roots and junctions defeat the rule.
        // Expected-fail until fixed; once fixed this XPASSes: delete the line.
        QVERIFY2(leaks.isEmpty(), qPrintable(QStringLiteral("a mock capture reached tests/vectors/captured: ") + leaks.join(QStringLiteral("; "))));
        QVERIFY(!QFileInfo::exists(QStringLiteral(MC_TESTS_SOURCE_DIR) + QStringLiteral("/vectors/captured")));
    }

    // ---- Re-test 2: junction in the middle, mixed slashes, UNC loopback, 8.3, long paths ---------

    void T8_theRuleHoldsForMidPathJunctionsMixedSlashesUncLongPathsAnd83Names() {
        const QString dir = freshOutputDir(QStringLiteral("t8-spell2"));
        const QString tree = dir + QStringLiteral("/tree");
        const QString tests = tree + QStringLiteral("/tests");
        const QString vectors = tests + QStringLiteral("/vectors");
        const QString captured = vectors + QStringLiteral("/captured");
        const QStringList junctions = {dir + QStringLiteral("/jm"), dir + QStringLiteral("/jv"),
                                       dir + QStringLiteral("/out/x"), dir + QStringLiteral("/jl")};
        const auto rmJunctions = [&]() {
            for (const QString& j : junctions) {
                if (QFileInfo::exists(j)) {
                    QProcess p;
                    p.start(QStringLiteral("cmd"), {QStringLiteral("/c"), QStringLiteral("rmdir"), QDir::toNativeSeparators(j)});
                    p.waitForFinished(10000);
                }
            }
        };
        const auto mkJunction = [&](const QString& link, const QString& target) {
            QDir().mkpath(QFileInfo(link).absolutePath());
            QProcess p;
            p.start(QStringLiteral("cmd"), {QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"),
                                            QDir::toNativeSeparators(link), QDir::toNativeSeparators(target)});
            return p.waitForFinished(10000) && p.exitCode() == 0;
        };
        const auto landed = [&]() {
            QDir c(captured);
            return c.exists() && !c.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty();
        };
        MockRig mock(QStringLiteral("t8-spell2-mock"));
        QVERIFY(mock.ok);
        mock.host.setWords(QStringLiteral("D100"), {1234});
        const QString plan = dir + QStringLiteral("/p.json");
        writeBytes(plan, kSimplePlan);
        HilHost host(QStringLiteral("t8-spell2"));

        QStringList leaks;
        int refused = 0;
        int tried = 0;
        for (const bool capturedExists : {false, true}) {
            rmJunctions();
            QDir(tree).removeRecursively();
            QDir().mkpath(vectors);
            if (capturedExists) {
                QDir().mkpath(captured);
            }
            QVERIFY(mkJunction(junctions[0], tests));      // jm -> tests
            QVERIFY(mkJunction(junctions[1], vectors));    // jv -> tests/vectors
            if (capturedExists) {
                QVERIFY(mkJunction(junctions[2], captured)); // out/x -> tests/vectors/captured
            }
            // a very long directory chain with a junction at its end
            QString longDir = dir + QStringLiteral("/long");
            for (int i = 0; i < 14; ++i) {
                longDir += QStringLiteral("/") + QString(24, QLatin1Char(char('a' + i)));
            }
            const bool haveLong = QDir().mkpath(longDir);
            const bool longJunction = haveLong && mkJunction(longDir + QStringLiteral("/jl"), vectors);
            qInfo("capturedExists=%d long path %d chars made=%d junction in it=%d", capturedExists ? 1 : 0,
                  int(longDir.size()), haveLong ? 1 : 0, longJunction ? 1 : 0);

            struct Try { QString root; QString id; const char* what; };
            QList<Try> tries = {
                {dir + QStringLiteral("/jm/vectors"), QStringLiteral("captured"), "junction in the middle"},
                {dir + QStringLiteral("/jm\\vectors/"), QStringLiteral("captured"), "mixed slashes + middle junction"},
                {QDir::toNativeSeparators(vectors) + QStringLiteral("/"), QStringLiteral("captured"), "backslashes + trailing slash"},
                {tree + QStringLiteral("\\tests/vectors\\.\\"), QStringLiteral("captured"), "mixed with dot segment"},
                {dir + QStringLiteral("/jv"), QStringLiteral("captured"), "junction as root"},
                {dir + QStringLiteral("/jv/"), QStringLiteral("CAPTURED"), "junction root, upper id"},
                {dir + QStringLiteral("/jm/vectors/captured/.."), QStringLiteral("captured"), "dotdot under captured"},
                {vectors + QStringLiteral("/captured"), QStringLiteral("sub"), "plain subfolder of captured"},
                {vectors, QStringLiteral("CAPTUR~1"), "8.3 name as id"},
                {tests + QStringLiteral("/VECTOR~1"), QStringLiteral("captured"), "8.3 name as root"},
                {QStringLiteral("\\\\localhost\\") + QString(QDir::toNativeSeparators(vectors)).replace(QStringLiteral(":"), QStringLiteral("$")),
                 QStringLiteral("captured"), "UNC loopback admin share"},
                {QStringLiteral("//127.0.0.1/") + QString(vectors).replace(QStringLiteral(":"), QStringLiteral("$")),
                 QStringLiteral("captured"), "UNC loopback ip admin share"},
            };
            if (capturedExists) {
                tries.append({dir + QStringLiteral("/out"), QStringLiteral("x"), "id folder is a junction to captured"});
            }
            if (longJunction) {
                tries.append({longDir + QStringLiteral("/jl"), QStringLiteral("captured"), "long path + junction"});
            }
            for (const Try& t : tries) {
                for (const bool withRoot : {true, false}) {
                    if (withRoot && !capturedExists) {
                        continue;
                    }
                    const QString profile = writeProfile(dir, t.id, mock.port);
                    HilCheckInput input;
                    input.profilePath = profile;
                    input.planPath = plan;
                    HilCheckResult check;
                    QVERIFY(checkVia(host, input, &check));
                    if (!check.ok()) {
                        continue;
                    }
                    // fresh captured each time: only what the run writes counts
                    if (capturedExists) {
                        QDir(captured).removeRecursively();
                        QDir().mkpath(captured);
                    }
                    HilRunRequest r = requestFor(input, check, t.root);
                    r.capturedRoot = withRoot ? captured : QString();
                    r.overwrite = true;
                    HilRunResult result;
                    QVERIFY(runVia(host, r, &result));
                    ++tried;
                    if (landed()) {
                        leaks.append(QStringLiteral("%1 (captured exists=%2, capturedRoot=%3, status=%4)")
                                         .arg(QString::fromLatin1(t.what)).arg(capturedExists).arg(withRoot)
                                         .arg(static_cast<int>(result.status)));
                    } else if (result.status == HilRunStatus::OutputRefused) {
                        ++refused;
                    }
                }
            }
            rmJunctions();
            QDir(dir + QStringLiteral("/long")).removeRecursively();
        }
        rmJunctions();
        qInfo("spell2: %d runs, %d refused, %d leaks", tried, refused, int(leaks.size()));
        for (const QString& leak : leaks) {
            qInfo("LEAK %s", qPrintable(leak));
        }
        QVERIFY2(leaks.isEmpty(), qPrintable(leaks.join(QStringLiteral("; "))));
        QVERIFY(!QFileInfo::exists(QStringLiteral(MC_TESTS_SOURCE_DIR) + QStringLiteral("/vectors/captured")));
    }

    // ---- T-078/T-079 tester probes: the Qt 5 / Qt 6 compat helpers ---------------------------

    // jsonInteger() keeps QJsonValue::toInteger()'s rule on both majors: a whole number inside
    // qint64 comes back exactly (the config fields go up to 2^32 - 1), anything else is 0.
    void T10_jsonIntegerKeepsTheRuleOfToIntegerOnBothMajors() {
        QCOMPARE(jsonInteger(QJsonValue(0)), qint64(0));
        QCOMPARE(jsonInteger(QJsonValue(-1)), qint64(-1));
        QCOMPARE(jsonInteger(QJsonValue(65535)), qint64(65535));
        QCOMPARE(jsonInteger(QJsonValue(4294967295.0)), qint64(4294967295LL));
        QCOMPARE(jsonInteger(QJsonValue(4294967296.0)), qint64(4294967296LL));
        QCOMPARE(jsonInteger(QJsonValue(9007199254740992.0)), qint64(9007199254740992LL)); // 2^53
        QCOMPARE(jsonInteger(QJsonValue(-9007199254740992.0)), qint64(-9007199254740992LL));
        QCOMPARE(jsonInteger(QJsonValue(1.5)), qint64(0));
        QCOMPARE(jsonInteger(QJsonValue(-0.25)), qint64(0));
        QCOMPARE(jsonInteger(QJsonValue(4294967295.5)), qint64(0));
        QCOMPARE(jsonInteger(QJsonValue(9223372036854775808.0)), qint64(0)); // 2^63
        QCOMPARE(jsonInteger(QJsonValue(1e300)), qint64(0));
        QCOMPARE(jsonInteger(QJsonValue(-1e300)), qint64(0));
        QCOMPARE(jsonInteger(QJsonValue(std::numeric_limits<double>::infinity())), qint64(0));
        QCOMPARE(jsonInteger(QJsonValue(QStringLiteral("12"))), qint64(0));
        QCOMPARE(jsonInteger(QJsonValue(true)), qint64(0));
        QCOMPARE(jsonInteger(QJsonValue()), qint64(0));
        QCOMPARE(jsonInteger(QJsonValue(QJsonArray{1})), qint64(0));
        // As a workspace or config file is read.
        const QJsonObject parsed =
            QJsonDocument::fromJson("{\"a\":4294967295,\"b\":1e3,\"c\":2.5,\"d\":-7,\"e\":\"5\"}")
                .object();
        QCOMPARE(jsonInteger(parsed.value(QStringLiteral("a"))), qint64(4294967295LL));
        QCOMPARE(jsonInteger(parsed.value(QStringLiteral("b"))), qint64(1000));
        QCOMPARE(jsonInteger(parsed.value(QStringLiteral("c"))), qint64(0));
        QCOMPARE(jsonInteger(parsed.value(QStringLiteral("d"))), qint64(-7));
        QCOMPARE(jsonInteger(parsed.value(QStringLiteral("e"))), qint64(0));
        QCOMPARE(jsonInteger(parsed.value(QStringLiteral("missing"))), qint64(0));
    }

    // A HIL run on the runner thread reports its lines in UTF-8 on both majors (useUtf8(); Qt 5
    // would otherwise write the locale's code page into the UTF-8 line sink): an output folder
    // with non-ASCII characters comes back unchanged in "capture written to ...".
    void T10_aHilRunReportsANonAsciiOutputFolderUnchanged() {
        MockRig mock(QStringLiteral("t10-utf8"));
        QVERIFY(mock.ok);
        const QString dir = freshOutputDir(QStringLiteral("t10-utf8"));
        const QString profile = writeProfile(dir, QStringLiteral("t10-utf8"), mock.port);
        const QString plan = dir + QStringLiteral("/plan.json");
        writeBytes(plan, QByteArray(kOtherPlan));
        // Greek capital omega, u umlaut, Vietnamese e with circumflex and dot below.
        const QString nonAscii = QString::fromUtf8("\xCE\xA9\xC3\xBC\xE1\xBB\x87");
        const QString out = dir + QStringLiteral("/out-") + nonAscii;

        HilHost host(QStringLiteral("t10-utf8"));
        HilCheckInput input;
        input.profilePath = profile;
        input.planPath = plan;
        HilCheckResult check;
        QVERIFY(checkVia(host, input, &check));
        QVERIFY2(check.ok(), qPrintable(check.errorText + check.refusalText));
        QSignalSpy output(&host, &HilHost::outputLine);
        HilRunResult run;
        QVERIFY(runVia(host, requestFor(input, check, out), &run));
        QVERIFY2(run.status == HilRunStatus::Finished, qPrintable(run.reason));
        QString lines;
        const auto collect = [&]() {
            QStringList all;
            for (const QList<QVariant>& args : output) {
                all << args.at(0).toString();
            }
            lines = all.join(QLatin1Char('\n'));
            return lines.contains(QStringLiteral("capture written to"));
        };
        (void)QTest::qWaitFor(collect, kWait);
        QVERIFY2(!lines.contains(QChar(0xFFFD)), qPrintable(lines));
        QVERIFY2(lines.contains(QStringLiteral("capture written to")), qPrintable(lines));
        QVERIFY2(lines.contains(QStringLiteral("out-") + nonAscii), qPrintable(lines));
    }
};

namespace mc::workbench::test {
QObject* makeTesterProbesSuite() {
    return new TesterProbesTest;
}
} // namespace mc::workbench::test

#include "tst_gui_tester_probes.moc"
