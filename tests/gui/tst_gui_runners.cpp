// GUI-01, GUI-03, GUI-04 (SPEC-gui-tool.md "Testing"): runner threads, containment and shutdown.
//
// Every device and mock under test is a DeviceHost / MockHost: the object tree lives on a runner
// thread, the test (the GUI thread) only sends commands and receives value copies. The tests assert
// where the objects live (QObject::thread() of the runner, the McDevice and the transport; the
// thread a MockPlc was created on), that the GUI thread keeps processing events while a runner
// fails or is busy, and that shutdown joins every thread within a bound without a foreign delete.
#include "gui_suites.h"

#include "mc/device/meta_types.h"
#include "mc_workbench/device_host.h"
#include "mc_workbench/device_runner.h"
#include "mc_workbench/mock_host.h"
#include "mc_workbench/mock_runner.h"
#include "mc_workbench/runner_base.h"
#include "mc_workbench/runner_types.h"

#include <QElapsedTimer>
#include <QSerialPort>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTest>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace mc::workbench;

namespace {

constexpr int kWaitMs = 8000;
constexpr int kStopBoundMs = RunnerThread::kDefaultStopTimeoutMs;
// The longest gap between two ticks of a 10 ms timer on the GUI thread that still counts as "the
// GUI thread keeps processing events".
constexpr int kGuiGapLimitMs = 250;

quintptr identity(const QThread* thread) {
    return reinterpret_cast<quintptr>(thread);
}

// Measures how long the GUI thread's event loop goes without running a 10 ms timer.
class Ticker : public QObject {
public:
    Ticker() {
        m_timer.setInterval(10);
        connect(&m_timer, &QTimer::timeout, this, [this]() {
            const qint64 now = m_clock.elapsed();
            m_maxGapMs = qMax(m_maxGapMs, now - m_last);
            m_last = now;
        });
        restart();
    }
    void restart() {
        m_clock.start();
        m_last = 0;
        m_maxGapMs = 0;
        m_timer.start();
    }
    qint64 maxGapMs() const { return m_maxGapMs; }

private:
    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_last{0};
    qint64 m_maxGapMs{0};
};

mc::McDeviceConfig tcpConfig(quint16 port) {
    mc::McDeviceConfig cfg;
    cfg.session.cycleIntervalMs = 20;
    cfg.tcp.host = QStringLiteral("127.0.0.1");
    cfg.tcp.port = port;
    cfg.tcp.connectTimeoutMs = 500;
    cfg.subscriptions = {{QStringLiteral("D100"), 4}, {QStringLiteral("M0"), 16}};
    return cfg;
}

quint16 unusedPort() {
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) {
        return 0;
    }
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}

// The first CommandResult with this token, waiting for it to arrive. A timeout returns a result with
// ok = false, errorCode = -1 and message "timeout", which no check below accepts.
CommandResult resultOf(const QSignalSpy& spy, quint64 token) {
    CommandResult found;
    found.ok = false;
    found.errorCode = -1;
    found.message = QStringLiteral("timeout");
    const auto look = [&]() {
        for (const QList<QVariant>& args : spy) {
            const auto result = args.at(0).value<CommandResult>();
            if (result.token == token) {
                found = result;
                return true;
            }
        }
        return false;
    };
    (void)QTest::qWaitFor(look, kWaitMs); // a timeout leaves the "timeout" result
    return found;
}

bool sawLink(const QSignalSpy& spy, mc::LinkState state, int reason = -1) {
    for (const QList<QVariant>& args : spy) {
        if (args.at(0).value<mc::LinkState>() == state &&
            (reason < 0 || static_cast<int>(args.at(1).value<mc::LinkReason>()) == reason)) {
            return true;
        }
    }
    return false;
}

// A mock that listens on a system-chosen port with D100..D103 = 10..40 and M3 = 1.
struct MockRig {
    explicit MockRig(const QString& name) : host(name, mc::FrameConfig::frame3E()), done(&host, &MockHost::commandDone) {
        host.setWords(QStringLiteral("D100"), {10, 20, 30, 40});
        host.setBits(QStringLiteral("M0"), {false, false, false, true});
        const quint64 token = host.listen(0);
        const CommandResult listening = resultOf(done, token);
        ok = listening.ok;
        port = static_cast<quint16>(listening.value);
    }
    MockHost host;
    QSignalSpy done;
    bool ok{false};
    quint16 port{0};
};

} // namespace

class RunnerThreadsTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        registerRunnerMetaTypes();
        m_guiThread = QThread::currentThread();
    }

    // GUI-01: a DeviceRunner on its own thread connects to a MockRunner on another thread over
    // loopback, subscribes, and delivers values to the GUI thread as copies. The McDevice, its
    // transport and the MockPlc live on their runner threads, never on the GUI thread.
    void GUI_01_deviceAndMockRunOnTheirOwnThreadsAndValuesArriveAsCopies() {
        MockRig mock(QStringLiteral("t068-mock"));
        QVERIFY(mock.ok);
        QVERIFY(mock.port != 0);

        DeviceHost dev(QStringLiteral("t068-device"), tcpConfig(mock.port));
        QSignalSpy link(&dev, &DeviceHost::linkStateChanged);
        QSignalSpy values(&dev, &DeviceHost::valuesBatch);
        QSignalSpy frames(&dev, &DeviceHost::framesBatch);
        QSignalSpy logs(&dev, &DeviceHost::logBatch);
        QSignalSpy stats(&mock.host, &MockHost::statsChanged);
        QSignalSpy clients(&mock.host, &MockHost::clientConnected);

        // Every batch must be delivered on the GUI thread.
        bool deliveredOnGui = true;
        connect(&dev, &DeviceHost::valuesBatch, this, [&](const ValueBatch&) {
            deliveredOnGui = deliveredOnGui && QThread::currentThread() == m_guiThread;
        });

        dev.setLogLevel(mc::LogLevel::Debug);
        dev.connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(sawLink(link, mc::LinkState::Connected), kWaitMs);

        // Round 1: the snapshot of D100..D103.
        const auto hasD = [&](int index, int expected) {
            for (const QList<QVariant>& args : values) {
                const auto batch = args.at(0).value<ValueBatch>();
                for (const mc::DeviceSnapshot& snapshot : batch.snapshots) {
                    if (snapshot.type != mc::DeviceType::D || snapshot.segments.isEmpty()) {
                        continue;
                    }
                    const QByteArray& bytes = snapshot.segments.first().values;
                    if (bytes.size() >= (index + 1) * 2) {
                        const int word = static_cast<quint8>(bytes[index * 2]) |
                                         (static_cast<quint8>(bytes[index * 2 + 1]) << 8);
                        if (word == expected) {
                            return true;
                        }
                    }
                }
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(hasD(0, 10) && hasD(3, 40), kWaitMs);

        // A change made on the mock runner's thread arrives as a ValueUpdate copy.
        const quint64 setToken = mock.host.setWords(QStringLiteral("D101"), {77});
        QVERIFY(resultOf(mock.done, setToken).ok);
        const auto sawChange = [&]() {
            for (const QList<QVariant>& args : values) {
                const auto batch = args.at(0).value<ValueBatch>();
                for (const ValueUpdate& update : batch.changes) {
                    for (const mc::Change& change : update.changes) {
                        if (change.device.type == mc::DeviceType::D && change.device.number == 101 &&
                            change.newValue == 77) {
                            return true;
                        }
                    }
                }
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(sawChange(), kWaitMs);
        QVERIFY(deliveredOnGui);

        // Frames, log lines and the mock's own counters arrive too.
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() > 0, kWaitMs);
        bool sawTx = false;
        bool sawRx = false;
        for (const QList<QVariant>& args : frames) {
            for (const FrameRecord& frame : args.at(0).value<QVector<FrameRecord>>()) {
                QVERIFY(!frame.bytes.isEmpty());
                (frame.tx ? sawTx : sawRx) = true;
            }
        }
        QVERIFY(sawTx);
        QVERIFY(sawRx);
        QTRY_VERIFY_WITH_TIMEOUT(logs.count() > 0, kWaitMs);
        QCOMPARE(clients.count(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(
            !stats.isEmpty() && stats.last().at(0).value<MockStats>().requests > 0, kWaitMs);

        // Where everything lives.
        QSignalSpy deviceThreads(&dev, &DeviceHost::threadReport);
        QSignalSpy mockThreads(&mock.host, &MockHost::threadReport);
        dev.requestThreadReport();
        mock.host.requestThreadReport();
        QTRY_COMPARE_WITH_TIMEOUT(deviceThreads.count(), 1, kWaitMs);
        QTRY_COMPARE_WITH_TIMEOUT(mockThreads.count(), 1, kWaitMs);

        const quintptr deviceThread = identity(dev.runnerThread()->workerThread());
        const quintptr mockThread = identity(mock.host.runnerThread()->workerThread());
        const quintptr guiThread = identity(m_guiThread);
        QVERIFY(deviceThread != guiThread);
        QVERIFY(mockThread != guiThread);
        QVERIFY(deviceThread != mockThread);

        const auto d = deviceThreads.first().at(0).value<ThreadReport>();
        QCOMPARE(d.runnerThread, deviceThread);
        QCOMPARE(d.objectThread, deviceThread);
        QCOMPARE(d.deviceThread, deviceThread);    // QObject::thread() of the McDevice
        QCOMPARE(d.transportThread, deviceThread); // and of its RecordingTransport
        QCOMPARE(d.currentThread, deviceThread);
        const auto m = mockThreads.first().at(0).value<ThreadReport>();
        QCOMPARE(m.runnerThread, mockThread);
        QCOMPARE(m.objectThread, mockThread);
        QCOMPARE(m.deviceThread, mockThread);    // the MockPlc was created there
        QCOMPARE(m.transportThread, mockThread); // and so was the QTcpServer
        QCOMPARE(m.currentThread, mockThread);

        // The same question asked directly on the runner thread, through post().
        std::atomic<quintptr> seenDevice{0};
        std::atomic<quintptr> seenRunner{0};
        dev.post([&](DeviceRunner& runner) {
            seenDevice = identity(runner.device()->thread());
            seenRunner = identity(QThread::currentThread());
        });
        QTRY_VERIFY_WITH_TIMEOUT(seenRunner.load() != 0, kWaitMs);
        QCOMPARE(seenDevice.load(), deviceThread);
        QCOMPARE(seenRunner.load(), deviceThread);
    }

    // GUI-01: commands are answered with their token; an ad-hoc read and write travel the full
    // path and come back as value copies.
    void GUI_01_commandsAreAnsweredWithTheirTokenAndAdHocRequestsRoundTrip() {
        MockRig mock(QStringLiteral("t068-mock"));
        QVERIFY(mock.ok);
        DeviceHost dev(QStringLiteral("t068-device"), tcpConfig(mock.port));
        QSignalSpy done(&dev, &DeviceHost::commandDone);
        QSignalSpy link(&dev, &DeviceHost::linkStateChanged);
        QSignalSpy finished(&dev, &DeviceHost::requestFinished);

        // A device the frame cannot use is refused with the library's code, not by a crash.
        const CommandResult bad = resultOf(done, dev.subscribe(QStringLiteral("Q9"), 1));
        QVERIFY(!bad.ok);
        QCOMPARE(bad.errorCode, static_cast<int>(mc::ErrorCode::InvalidDevice));
        QVERIFY(!bad.message.isEmpty());

        const CommandResult sub = resultOf(done, dev.subscribe(QStringLiteral("D200"), 2));
        QVERIFY(sub.ok);
        QVERIFY(sub.value != 0);
        QVERIFY(resultOf(done, dev.unsubscribe(static_cast<quint32>(sub.value))).ok);
        const CommandResult again = resultOf(done, dev.unsubscribe(static_cast<quint32>(sub.value)));
        QVERIFY(!again.ok);
        QCOMPARE(again.errorCode, static_cast<int>(mc::ErrorCode::NotSubscribed));

        // Not connected: an ad-hoc read is refused (LinkDown) in the answer.
        const CommandResult early = resultOf(done, dev.readWords(QStringLiteral("D100"), 2));
        QVERIFY(!early.ok);
        QCOMPARE(early.errorCode, static_cast<int>(mc::ErrorCode::LinkDown));

        dev.connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(sawLink(link, mc::LinkState::Connected), kWaitMs);

        const CommandResult write = resultOf(done, dev.writeWords(QStringLiteral("D300"), {0x1234, 0xABCD}));
        QVERIFY(write.ok);
        const CommandResult read = resultOf(done, dev.readWords(QStringLiteral("D300"), 2));
        QVERIFY(read.ok);
        const auto outcomeOf = [&](quint64 id) {
            RequestOutcome found;
            found.errorCode = -1;
            const auto look = [&]() {
                for (const QList<QVariant>& args : finished) {
                    const auto outcome = args.at(0).value<RequestOutcome>();
                    if (outcome.id == id) {
                        found = outcome;
                        return true;
                    }
                }
                return false;
            };
            (void)QTest::qWaitFor(look, kWaitMs); // a timeout leaves the "timeout" result
            return found;
        };
        QVERIFY(outcomeOf(write.value).errorCode == 0);
        const RequestOutcome got = outcomeOf(read.value);
        QCOMPARE(got.errorCode, 0);
        QCOMPARE(got.payload, QByteArray("\x34\x12\xCD\xAB", 4));
    }

    // GUI-03: a command that throws is contained at the slot boundary: the runner reports it, the
    // device is stopped, later commands are refused, the thread and the GUI thread keep running.
    void GUI_03_aThrowingCommandIsContainedAndLaterCommandsAreRefused() {
        DeviceHost dev(QStringLiteral("t068-throw"));
        QSignalSpy failed(&dev, &DeviceHost::failed);
        QSignalSpy done(&dev, &DeviceHost::commandDone);
        Ticker ticker;

        dev.post([](DeviceRunner&) { throw std::runtime_error("boom"); });
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, kWaitMs);
        QVERIFY(failed.first().at(0).toString().contains(QLatin1String("boom")));

        const CommandResult refused = resultOf(done, dev.subscribe(QStringLiteral("D100"), 1));
        QVERIFY(!refused.ok);
        QVERIFY(refused.message.contains(QLatin1String("stopped")));
        dev.connectToPlc(); // ignored, no crash

        // A second throw (also a non-std type) is contained again; the thread is still alive.
        dev.post([](DeviceRunner&) { throw 42; });
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 2, kWaitMs);
        QVERIFY(dev.runnerThread()->isRunning());
        QTest::qWait(100);
        QVERIFY2(ticker.maxGapMs() < kGuiGapLimitMs, qPrintable(QString::number(ticker.maxGapMs())));
    }

    // GUI-03: a slot connected to a McDevice signal that throws is contained too (it runs inside
    // the event loop, not under a posted command); the device is disconnected, the GUI is told.
    void GUI_03_aThrowingDeviceSlotIsContainedAndTheDeviceIsDisconnected() {
        MockRig mock(QStringLiteral("t068-mock"));
        QVERIFY(mock.ok);
        DeviceHost dev(QStringLiteral("t068-device"), tcpConfig(mock.port));
        QSignalSpy failed(&dev, &DeviceHost::failed);
        QSignalSpy link(&dev, &DeviceHost::linkStateChanged);
        QSignalSpy done(&dev, &DeviceHost::commandDone);
        Ticker ticker;

        dev.post([](DeviceRunner& runner) {
            runner.setValuesSlotHook([]() { throw std::logic_error("slot failed"); });
        });
        dev.connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(sawLink(link, mc::LinkState::Connected), kWaitMs);
        // Round 2 of the poll sees a changed value and runs the throwing slot.
        QVERIFY(resultOf(mock.done, mock.host.setWords(QStringLiteral("D100"), {999})).ok);
        QTRY_VERIFY_WITH_TIMEOUT(failed.count() >= 1, kWaitMs);
        QVERIFY(failed.first().at(0).toString().contains(QLatin1String("slot failed")));
        QTRY_VERIFY_WITH_TIMEOUT(sawLink(link, mc::LinkState::Disconnected), kWaitMs);

        const CommandResult after = resultOf(done, dev.subscribe(QStringLiteral("D100"), 1));
        QVERIFY(!after.ok);
        QVERIFY(after.message.contains(QLatin1String("stopped")));
        QVERIFY(dev.runnerThread()->isRunning());
        QVERIFY2(ticker.maxGapMs() < kGuiGapLimitMs, qPrintable(QString::number(ticker.maxGapMs())));
        // The mock is a different runner and is untouched.
        QVERIFY(mock.host.runnerThread()->isRunning());
    }

    // GUI-03: a transport that fails to open (a closed TCP port) is reported as OpenFailed; the GUI
    // thread keeps processing events while the runner waits for the refused connection.
    void GUI_03_aTcpPortThatRefusesTheConnectionReportsOpenFailed() {
        const quint16 port = unusedPort();
        QVERIFY(port != 0);
        DeviceHost dev(QStringLiteral("t068-refused"), tcpConfig(port));
        QSignalSpy link(&dev, &DeviceHost::linkStateChanged);
        QSignalSpy failed(&dev, &DeviceHost::failed);
        Ticker ticker;

        dev.connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(
            sawLink(link, mc::LinkState::Disconnected, static_cast<int>(mc::LinkReason::OpenFailed)),
            kWaitMs);
        QCOMPARE(failed.count(), 0); // a refused connection is an error report, not an exception
        QVERIFY2(ticker.maxGapMs() < kGuiGapLimitMs, qPrintable(QString::number(ticker.maxGapMs())));
    }

    // GUI-03: a COM port that does not exist is reported the same way.
    void GUI_03_aMissingComPortReportsOpenFailed() {
        mc::McDeviceConfig cfg = tcpConfig(1);
        cfg.transport = mc::TransportKind::Serial;
        cfg.serial.portName = QStringLiteral("COM253");
        DeviceHost dev(QStringLiteral("t068-nocom"), cfg);
        QSignalSpy link(&dev, &DeviceHost::linkStateChanged);
        Ticker ticker;

        dev.connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(
            sawLink(link, mc::LinkState::Disconnected, static_cast<int>(mc::LinkReason::OpenFailed)),
            kWaitMs);
        QVERIFY2(ticker.maxGapMs() < kGuiGapLimitMs, qPrintable(QString::number(ticker.maxGapMs())));
    }

    // GUI-03: a COM port that another process holds open is reported as OpenFailed. Needs the
    // virtual COM pair (MC_TEST_SERIAL_PAIR, e.g. "COM54,COM55"); the port is held by this test.
    void GUI_03_aBusyComPortReportsOpenFailed() {
        const QString pair = qEnvironmentVariable("MC_TEST_SERIAL_PAIR");
        if (pair.isEmpty()) {
            QSKIP("MC_TEST_SERIAL_PAIR is not set (e.g. \"COM54,COM55\"): no virtual COM pair");
        }
        const QString portName = pair.split(QLatin1Char(',')).first().trimmed();

        QSerialPort holder;
        holder.setPortName(portName);
        QVERIFY2(holder.open(QIODevice::ReadWrite), qPrintable(holder.errorString()));

        mc::McDeviceConfig cfg = tcpConfig(1);
        cfg.transport = mc::TransportKind::Serial;
        cfg.serial.portName = portName;
        DeviceHost dev(QStringLiteral("t068-busy"), cfg);
        QSignalSpy link(&dev, &DeviceHost::linkStateChanged);
        Ticker ticker;

        dev.connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(
            sawLink(link, mc::LinkState::Disconnected, static_cast<int>(mc::LinkReason::OpenFailed)),
            kWaitMs);
        QVERIFY2(ticker.maxGapMs() < kGuiGapLimitMs, qPrintable(QString::number(ticker.maxGapMs())));
        holder.close();
    }

    // GUI-03 / GUI-04: a runner stuck in a long call does not freeze the GUI thread; stop() waits
    // only the bound and reports the stuck thread; the thread ends by itself once the call returns.
    void GUI_03_aStuckRunnerDoesNotFreezeTheGuiAndStopReportsIt() {
        const int liveBefore = RunnerBase::liveRunners();
        {
            DeviceHost dev(QStringLiteral("t068-stuck"));
            QSignalSpy stuck(dev.runnerThread(), &RunnerThread::stuck);
            QTRY_COMPARE_WITH_TIMEOUT(RunnerBase::liveRunners(), liveBefore + 1, kWaitMs);
            Ticker ticker;

            dev.post([](DeviceRunner&) { QThread::msleep(900); }); // a "driver call" that hangs
            QTest::qWait(400);                                     // the GUI keeps ticking
            QVERIFY2(ticker.maxGapMs() < kGuiGapLimitMs, qPrintable(QString::number(ticker.maxGapMs())));

            QElapsedTimer waited;
            waited.start();
            QVERIFY(!dev.runnerThread()->stop(150));
            QVERIFY2(waited.elapsed() < 800, qPrintable(QString::number(waited.elapsed())));
            QCOMPARE(stuck.count(), 1);
            QVERIFY(dev.runnerThread()->isStuck());
            QVERIFY(dev.runnerThread()->isRunning());

            // The call returns, the queued shutdown runs, the thread finishes and is joined.
            QVERIFY(dev.runnerThread()->stop(kStopBoundMs));
            QVERIFY(!dev.runnerThread()->isStuck());
            QVERIFY(!dev.runnerThread()->isRunning());
        }
        QCOMPARE(RunnerBase::liveRunners(), liveBefore);
    }

    // GUI-03: a host destroyed while its thread is still stuck abandons the thread instead of
    // blocking: the thread deletes its objects itself on its own thread later.
    void GUI_03_destroyingAHostWithAStuckThreadIsBoundedAndTheThreadCleansUpItself() {
        const int liveBefore = RunnerBase::liveRunners();
        const int foreignBefore = RunnerBase::foreignDeletes();
        auto dev = std::make_unique<DeviceHost>(QStringLiteral("t068-abandon"));
        QTRY_COMPARE_WITH_TIMEOUT(RunnerBase::liveRunners(), liveBefore + 1, kWaitMs);
        dev->post([](DeviceRunner&) { QThread::msleep(kStopBoundMs + 1500); });
        QTest::qWait(50);

        QElapsedTimer waited;
        waited.start();
        dev.reset(); // waits the default bound, then abandons the thread
        QVERIFY2(waited.elapsed() < kStopBoundMs + 1200, qPrintable(QString::number(waited.elapsed())));

        QTRY_COMPARE_WITH_TIMEOUT(RunnerBase::liveRunners(), liveBefore, 4 * kStopBoundMs);
        QCOMPARE(RunnerBase::foreignDeletes(), foreignBefore);
    }

    // GUI-04: closing every tab (4 devices, 2 mocks, all connected) joins every runner thread within
    // the bound; every runner object is gone (object count), none was deleted from a foreign thread.
    void GUI_04_closingEveryTabJoinsEveryThreadInTimeWithoutLeaksOrForeignDeletes() {
        const int liveBefore = RunnerBase::liveRunners();
        const int foreignBefore = RunnerBase::foreignDeletes();

        std::vector<std::unique_ptr<MockRig>> mocks;
        for (int i = 0; i < 2; ++i) {
            mocks.push_back(std::make_unique<MockRig>(QStringLiteral("t068-mock%1").arg(i)));
            QVERIFY(mocks.back()->ok);
        }
        std::vector<std::unique_ptr<DeviceHost>> devices;
        std::vector<std::unique_ptr<QSignalSpy>> links;
        for (int i = 0; i < 4; ++i) {
            devices.push_back(std::make_unique<DeviceHost>(QStringLiteral("t068-device%1").arg(i),
                                                           tcpConfig(mocks[i / 2]->port)));
            links.push_back(std::make_unique<QSignalSpy>(devices.back().get(),
                                                         &DeviceHost::linkStateChanged));
            devices.back()->connectToPlc();
        }
        for (int i = 0; i < 4; ++i) {
            QTRY_VERIFY_WITH_TIMEOUT(sawLink(*links[i], mc::LinkState::Connected), kWaitMs);
        }
        QCOMPARE(RunnerBase::liveRunners(), liveBefore + 6);

        QElapsedTimer closing;
        closing.start();
        for (auto& device : devices) {
            device->runnerThread()->requestStop();
        }
        for (auto& mock : mocks) {
            mock->host.runnerThread()->requestStop();
        }
        for (auto& device : devices) {
            QVERIFY(device->runnerThread()->waitFinished(kStopBoundMs));
        }
        for (auto& mock : mocks) {
            QVERIFY(mock->host.runnerThread()->waitFinished(kStopBoundMs));
        }
        QVERIFY2(closing.elapsed() < kStopBoundMs, qPrintable(QString::number(closing.elapsed())));
        QCOMPARE(RunnerBase::liveRunners(), liveBefore);

        // Every device ends Disconnected, and the news arrives on the GUI thread: its own
        // shutdown disconnects it (Requested), or the mock that shut down first closed the link
        // (PeerClosed); which comes first is a race, the final state is not.
        for (int i = 0; i < 4; ++i) {
            QTRY_VERIFY_WITH_TIMEOUT(
                links[i]->last().at(0).value<mc::LinkState>() == mc::LinkState::Disconnected,
                kWaitMs);
        }

        devices.clear();
        mocks.clear();
        QCOMPARE(RunnerBase::liveRunners(), liveBefore);
        QCOMPARE(RunnerBase::foreignDeletes(), foreignBefore);
    }

    // GUI-04: a host destroyed while its device is still connecting (a connect to a closed port
    // takes seconds on Windows) is joined within the bound.
    void GUI_04_destroyingAHostThatIsStillConnectingIsBounded() {
        const int liveBefore = RunnerBase::liveRunners();
        mc::McDeviceConfig cfg = tcpConfig(unusedPort());
        cfg.tcp.connectTimeoutMs = 10000;
        auto dev = std::make_unique<DeviceHost>(QStringLiteral("t068-connecting"), cfg);
        QSignalSpy link(dev.get(), &DeviceHost::linkStateChanged);
        dev->connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(sawLink(link, mc::LinkState::Connecting), kWaitMs);

        QElapsedTimer waited;
        waited.start();
        dev.reset();
        QVERIFY2(waited.elapsed() < kStopBoundMs, qPrintable(QString::number(waited.elapsed())));
        QCOMPARE(RunnerBase::liveRunners(), liveBefore);
    }

    // GUI-04: stop() twice is harmless, and a command posted after the stop is ignored.
    void GUI_04_stopIsIdempotentAndCommandsAfterStopAreIgnored() {
        DeviceHost dev(QStringLiteral("t068-stop"));
        QSignalSpy done(&dev, &DeviceHost::commandDone);
        QVERIFY(dev.runnerThread()->stop(kStopBoundMs));
        QVERIFY(dev.runnerThread()->stop(kStopBoundMs));
        QVERIFY(!dev.runnerThread()->isRunning());
        dev.subscribe(QStringLiteral("D100"), 1);
        dev.connectToPlc();
        QTest::qWait(100);
        QCOMPARE(done.count(), 0);
    }

private:
    QThread* m_guiThread{nullptr};
};

namespace mc::workbench::test {

QObject* makeRunnerSuite() {
    return new RunnerThreadsTest;
}

} // namespace mc::workbench::test

#include "tst_gui_runners.moc"
