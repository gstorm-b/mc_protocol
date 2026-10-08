// T-075: a mock tab serves every TCP client on its own MockPlc input stream (MCK-13 in the GUI).
//   - two raw clients send their requests one byte at a time, interleaved, to one mock tab and both
//     get their own correct answer;
//   - a client that disconnects in the middle of a request leaves nothing behind for the next one;
//   - the HIL view runs the whole tests/hil/e2e/plan_3e.json, E-10 (truncated frame, reconnect)
//     included, against a mock tab, and the capture it writes replays green (the one expected value
//     that depends on per-connection memory is adapted, see sharedMemoryPlan()).
// No real PLC: every target is a mock on loopback.
#include "gui_suites.h"
#include "gui_test_support.h"

#include "mc/core/protocol.h"
#include "mc_workbench/hil_view.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTest>

using namespace mc::workbench;
using namespace mc::workbench::test;

namespace {

constexpr int kRunWaitMs = 60000;

QString testsDir() {
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
QString writeMockProfile(const QString& dir, const QString& newId, quint16 port) {
    QJsonObject root = readJsonFile(testsDir() + QStringLiteral("/hil/profiles/q03ude-eth-3e-bin.example.json"));
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

// tests/hil/e2e/plan_3e.json with one change: the follow-up read of E-10 expects what E-02 left in D100.
// The plan is written for virtual_plc, where every connection has its own MockPlc and so a fresh D100 = 1234;
// a mock tab has one memory image for all its clients, so after E-02 it holds 1. E-10 itself, the
// truncated frame and the reconnect, is untouched.
QString sharedMemoryPlan(const QString& dir) {
    QJsonObject plan = readJsonFile(testsDir() + QStringLiteral("/hil/e2e/plan_3e.json"));
    QJsonArray steps = plan.value(QStringLiteral("steps")).toArray();
    for (int i = 0; i < steps.size(); ++i) {
        QJsonObject step = steps.at(i).toObject();
        if (step.value(QStringLiteral("id")).toString() != QLatin1String("E-10")) {
            continue;
        }
        QJsonArray then = step.value(QStringLiteral("then")).toArray();
        QJsonObject follow = then.at(0).toObject();
        QJsonObject expect = follow.value(QStringLiteral("expect")).toObject();
        expect.insert(QStringLiteral("values"), QJsonArray{1});
        follow.insert(QStringLiteral("expect"), expect);
        then.replace(0, follow);
        step.insert(QStringLiteral("then"), then);
        steps.replace(i, step);
    }
    plan.insert(QStringLiteral("steps"), steps);
    const QString path = dir + QStringLiteral("/plan_3e_shared_memory.json");
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(plan).toJson());
    }
    return path;
}

// A well-formed 3E read of `count` words of D from `number`, built by the client encoder.
QByteArray readFrame(uint32_t number, uint16_t count) {
    mc::Request read;
    read.op = mc::Op::ReadWords;
    read.head = mc::Device{mc::DeviceType::D, number};
    read.count = count;
    const auto frame = mc::McProtocol(mc::FrameConfig::frame3E()).encode(read);
    if (!frame) {
        return {};
    }
    return QByteArray(reinterpret_cast<const char*>(frame.value().data()),
                      static_cast<QByteArray::size_type>(frame.value().size()));
}

// The words of a binary 3E read response (D0 00 + route 5 + length 2 + end code 2 + data), or an
// empty list when it is not one.
QList<int> wordsOf(const QByteArray& response, int count) {
    QList<int> words;
    if (response.size() != 11 + 2 * count || static_cast<quint8>(response[0]) != 0xD0 ||
        response[9] != 0 || response[10] != 0) {
        return words;
    }
    for (int i = 0; i < count; ++i) {
        words.push_back(static_cast<quint8>(response[11 + 2 * i]) |
                        (static_cast<quint8>(response[12 + 2 * i]) << 8));
    }
    return words;
}

QByteArray readResponse(QTcpSocket& socket, int count) {
    QByteArray got;
    const int want = 11 + 2 * count;
    (void)QTest::qWaitFor(
        [&]() {
            got += socket.readAll();
            return got.size() >= want;
        },
        kWait);
    return got;
}

bool connectTo(QTcpSocket& socket, quint16 port) {
    socket.connectToHost(QHostAddress::LocalHost, port);
    socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);
    return socket.waitForConnected(kWait);
}

} // namespace

class MockStreamsGuiTest : public QObject {
    Q_OBJECT

private slots:
    // Two clients, one byte each in turn, so the mock holds two partial requests at once.
    void GUI_02_twoClientsSendingInterleavedFragmentsBothGetTheirOwnAnswer() {
        MockRig mock(QStringLiteral("t075-interleave"));
        QVERIFY(mock.ok);
        QTcpSocket a;
        QTcpSocket b;
        QVERIFY(connectTo(a, mock.port));
        QVERIFY(connectTo(b, mock.port));

        const QByteArray ra = readFrame(100, 4); // D100..D103 = 10, 20, 30, 40
        const QByteArray rb = readFrame(101, 2); // D101..D102 = 20, 30
        QVERIFY(!ra.isEmpty());
        QVERIFY(!rb.isEmpty());
        const int longest = static_cast<int>(qMax(ra.size(), rb.size()));
        for (int i = 0; i < longest; ++i) {
            if (i < ra.size()) {
                a.write(ra.constData() + i, 1);
                a.flush();
            }
            if (i < rb.size()) {
                b.write(rb.constData() + i, 1);
                b.flush();
            }
            QTest::qWait(3); // let the mock thread see this byte of both before the next
        }

        QCOMPARE(wordsOf(readResponse(a, 4), 4), (QList<int>{10, 20, 30, 40}));
        QCOMPARE(wordsOf(readResponse(b, 2), 2), (QList<int>{20, 30}));
    }

    // A client that goes away in the middle of a request leaves no half frame for the next one.
    void GUI_02_aRequestAbandonedByAClosedClientDoesNotReachTheNextClient() {
        MockRig mock(QStringLiteral("t075-abandon"));
        QVERIFY(mock.ok);
        const QByteArray whole = readFrame(100, 4);
        QVERIFY(!whole.isEmpty());
        for (int round = 0; round < 2; ++round) {
            QTcpSocket gone;
            QVERIFY(connectTo(gone, mock.port));
            gone.write(whole.constData(), whole.size() / 2);
            gone.flush();
            QTest::qWait(30);
            gone.disconnectFromHost();
            if (gone.state() != QAbstractSocket::UnconnectedState) {
                QVERIFY(gone.waitForDisconnected(kWait));
            }
            QTest::qWait(30); // the mock saw the disconnect

            QTcpSocket next;
            QVERIFY(connectTo(next, mock.port));
            next.write(whole);
            next.flush();
            QCOMPARE(wordsOf(readResponse(next, 4), 4), (QList<int>{10, 20, 30, 40}));
        }
    }

    // The full e2e plan, E-10 included, against a mock tab; the capture replays green.
    void GUI_07_theHilViewRunsPlan3EWithE10AgainstAMockTabAndTheCaptureReplaysGreen() {
        MockRig mock(QStringLiteral("t075-run"));
        QVERIFY(mock.ok);
        mock.host.setWords(QStringLiteral("D100"), {1234});
        const QString dir = freshOutputDir(QStringLiteral("gui-t075-run"));
        const QString root = dir + QStringLiteral("/captured");

        HilView view;
        view.setProfilePath(writeMockProfile(dir, QStringLiteral("gui-mock-3e"), mock.port));
        view.setPlanPath(sharedMemoryPlan(dir)); // the full plan, E-10 in it
        view.setOutputRoot(root);
        view.setBenchReps(3);
        view.setNote(QStringLiteral("T-075 run against a mock tab"));
        view.setCapturedRoot(freshOutputDir(QStringLiteral("gui-t075-fake")) +
                             QStringLiteral("/tests/vectors/captured"));
        QSignalSpy checked(&view, &HilView::checkDone);
        view.check();
        QTRY_COMPARE_WITH_TIMEOUT(checked.count(), 1, kWait);
        QVERIFY2(view.lastCheck().ok(), qPrintable(view.gateText()));
        QVERIFY(view.lastCheck().loopbackTcp);
        QCOMPARE(view.source(), CaptureSource::MockPlc);
        QVERIFY(view.canRun());

        Ticker ticker;
        QSignalSpy done(&view, &HilView::runDone);
        // The plan holds read-only frames: the id is typed, there is no way to skip it.
        QVERIFY(view.runWith(QStringLiteral("gui-mock-3e"), false) != 0);
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, kRunWaitMs);
        const HilRunResult run = view.lastRun();
        const QString lines = run.lines.join(QLatin1Char('\n'));
        QVERIFY2(run.status == HilRunStatus::Finished, qPrintable(run.reason + lines));
        // The whole plan: 14 steps pass (E-10 among them), E-15 is skipped (no scan time).
        QVERIFY2(run.passed == 14 && run.failed == 0 && run.diverged == 0 && run.notSupported == 0 &&
                     run.skipped == 1,
                 qPrintable(lines));
        QCOMPARE(run.exitCode(), 0);
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

        // The capture: marked as a mock's, with E-10's records, never under tests/vectors/captured.
        const QString folder = run.folder;
        QVERIFY(folder.endsWith(QStringLiteral("gui-mock-3e")));
        const QString steps = readText(folder + QStringLiteral("/steps.vec"));
        QVERIFY(steps.contains(QStringLiteral("# source: mock  profile: gui-mock-3e")));
        QVERIFY(steps.contains(QStringLiteral("CAP-gui-mock-3e-E-10")));
        QVERIFY(steps.contains(QStringLiteral("CAP-gui-mock-3e-E-10.2-R")));
        QVERIFY(!QDir(testsDir() + QStringLiteral("/vectors/captured/gui-mock-3e")).exists());

        // It replays green (RPL-01..06).
        int exitCode = 0;
        const QString output = runReplay(root, &exitCode);
        if (exitCode == -1) {
            QSKIP(qPrintable(output));
        }
        QVERIFY2(exitCode == 0, qPrintable(output));
        QVERIFY2(output.contains(QStringLiteral("replayed gui-mock-3e")), qPrintable(output));
    }
};

namespace mc::workbench::test {

QObject* makeMockStreamsSuite() {
    return new MockStreamsGuiTest;
}

} // namespace mc::workbench::test

#include "tst_gui_mock_streams.moc"
