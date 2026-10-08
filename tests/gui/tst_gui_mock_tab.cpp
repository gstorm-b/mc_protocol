// GUI-02 and the mock tab (SPEC-gui-tool.md "Testing", T-070): several device hosts and mocks in
// parallel, a fault in one mock leaves the others undisturbed, COM serving, memory editor, faults and
// corruption, the request log and counters, and the mock tab and pane as widgets (offscreen).
//
// Every mock is a MockHost: the MockPlc, its QTcpServer and its QSerialPort live on a runner thread;
// the test (the GUI thread) only sends commands and receives value copies. Device sides are
// DeviceHosts. The COM case needs the virtual pair (MC_TEST_SERIAL_PAIR, e.g. "COM54,COM55") and
// QSKIPs without it; it runs under the mc_serial_pair resource lock like the rest of the binary.
#include "gui_suites.h"

#include "mc/core/protocol.h"
#include "mc/device/meta_types.h"
#include "mc_workbench/device_host.h"
#include "mc_workbench/device_tab.h"
#include "mc_workbench/main_window.h"
#include "mc_workbench/mock_fault_panel.h"
#include "mc_workbench/mock_host.h"
#include "mc_workbench/mock_memory_editor.h"
#include "mc_workbench/mock_request_log_model.h"
#include "mc_workbench/mock_runner.h"
#include "mc_workbench/mock_tab.h"
#include "mc_workbench/point_table_model.h"
#include "mc_workbench/runner_base.h"
#include "mc_workbench/runner_types.h"

#include <DockManager.h>
#include <DockWidget.h>
#include <qpb/PropertyModel.h>

#include <QCheckBox>
#include <QHostAddress>
#include <QSerialPort>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTest>
#include <QThread>

#include <atomic>
#include <memory>
#include <vector>

using namespace mc::workbench;

namespace {

constexpr int kWaitMs = 8000;

quintptr identity(const QThread* thread) {
    return reinterpret_cast<quintptr>(thread);
}

mc::McDeviceConfig tcpConfig(quint16 port, bool subscribe, quint32 timeoutMs = 400) {
    mc::McDeviceConfig cfg;
    cfg.session.cycleIntervalMs = 20;
    cfg.frame.timeoutMs = timeoutMs;
    cfg.tcp.host = QStringLiteral("127.0.0.1");
    cfg.tcp.port = port;
    cfg.tcp.connectTimeoutMs = 500;
    if (subscribe) {
        cfg.subscriptions = {{QStringLiteral("D100"), 4}, {QStringLiteral("M0"), 16}};
    }
    return cfg;
}

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

RequestOutcome outcomeOf(const QSignalSpy& spy, quint64 id) {
    RequestOutcome found;
    found.errorCode = -1;
    found.message = QStringLiteral("timeout");
    const auto look = [&]() {
        for (const QList<QVariant>& args : spy) {
            const auto outcome = args.at(0).value<RequestOutcome>();
            if (outcome.id == id) {
                found = outcome;
                return true;
            }
        }
        return false;
    };
    (void)QTest::qWaitFor(look, kWaitMs);
    return found;
}

bool sawLink(const QSignalSpy& spy, mc::LinkState state) {
    for (const QList<QVariant>& args : spy) {
        if (args.at(0).value<mc::LinkState>() == state) {
            return true;
        }
    }
    return false;
}

// Whether a value batch of D carried word `index` equal to `expected` (snapshots are the full image).
bool sawWord(const QSignalSpy& values, int index, int expected) {
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
}

// A mock on a system-chosen loopback port with D100..D103 = base+1..base+4.
struct MockRig {
    MockRig(const QString& name, const mc::FrameConfig& frame, quint16 base = 0)
        : host(name, frame), done(&host, &MockHost::commandDone) {
        host.setWords(QStringLiteral("D100"),
                      {quint16(base + 1), quint16(base + 2), quint16(base + 3), quint16(base + 4)});
        const CommandResult listening = resultOf(done, host.listen(0));
        ok = listening.ok;
        port = static_cast<quint16>(listening.value);
    }
    MockHost host;
    QSignalSpy done;
    bool ok{false};
    quint16 port{0};
};

// A device against a mock; subscribed or ad hoc.
struct DeviceRig {
    DeviceRig(const QString& name, const mc::McDeviceConfig& cfg)
        : host(name, cfg), link(&host, &DeviceHost::linkStateChanged),
          fault(&host, &DeviceHost::linkFault), values(&host, &DeviceHost::valuesBatch),
          done(&host, &DeviceHost::commandDone), finished(&host, &DeviceHost::requestFinished) {}
    DeviceHost host;
    QSignalSpy link;
    QSignalSpy fault;
    QSignalSpy values;
    QSignalSpy done;
    QSignalSpy finished;
};

QString comPortOne() {
    const QString pair = qEnvironmentVariable("MC_TEST_SERIAL_PAIR");
    return pair.split(QLatin1Char(',')).value(0).trimmed();
}

QString comPortTwo() {
    const QString pair = qEnvironmentVariable("MC_TEST_SERIAL_PAIR");
    return pair.split(QLatin1Char(',')).value(1).trimmed();
}

} // namespace

class MockTabTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        registerMockMetaTypes();
        m_guiThread = QThread::currentThread();
    }

    // GUI-02: four device hosts and two mocks run in parallel with independent link states; a fault
    // (mute) injected into one mock disturbs only the two devices that talk to it.
    void GUI_02_fourDevicesAndTwoMocksRunInParallelAndAFaultStaysInOneMock() {
        MockRig mockA(QStringLiteral("t070-mockA"), mc::FrameConfig::frame3E(), 0);
        MockRig mockB(QStringLiteral("t070-mockB"), mc::FrameConfig::frame3E(), 100);
        QVERIFY(mockA.ok && mockB.ok);
        QVERIFY(mockA.port != mockB.port);
        QSignalSpy statsA(&mockA.host, &MockHost::statsChanged);

        std::vector<std::unique_ptr<DeviceRig>> devices;
        for (int i = 0; i < 4; ++i) {
            const quint16 port = i < 2 ? mockA.port : mockB.port;
            devices.push_back(std::make_unique<DeviceRig>(QStringLiteral("t070-dev%1").arg(i),
                                                          tcpConfig(port, true)));
        }
        for (auto& device : devices) {
            device->host.connectToPlc();
        }
        for (int i = 0; i < 4; ++i) {
            QTRY_VERIFY_WITH_TIMEOUT(sawLink(devices[i]->link, mc::LinkState::Connected), kWaitMs);
            const int base = i < 2 ? 0 : 100;
            QTRY_VERIFY_WITH_TIMEOUT(sawWord(devices[i]->values, 0, base + 1), kWaitMs);
        }

        // Both devices of a mock are served at the same time.
        const auto twoClients = [&]() {
            for (const QList<QVariant>& args : statsA) {
                if (args.at(0).value<MockStats>().clients == 2) {
                    return true;
                }
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(twoClients(), kWaitMs);

        // The fault: mock A swallows every request. Its two devices fault; mock B's two do not.
        mockA.host.mute(true);
        QTRY_VERIFY_WITH_TIMEOUT(devices[0]->fault.count() >= 1, kWaitMs);
        QTRY_VERIFY_WITH_TIMEOUT(devices[1]->fault.count() >= 1, kWaitMs);
        QCOMPARE(devices[0]->fault.first().at(0).value<FaultReport>().kind,
                 static_cast<int>(mc::LinkFaultKind::Timeout));

        // Mock B keeps serving: a change reaches both of its devices, with no fault and no link loss.
        QVERIFY(resultOf(mockB.done, mockB.host.setWords(QStringLiteral("D101"), {777})).ok);
        QTRY_VERIFY_WITH_TIMEOUT(sawWord(devices[2]->values, 1, 777), kWaitMs);
        QTRY_VERIFY_WITH_TIMEOUT(sawWord(devices[3]->values, 1, 777), kWaitMs);
        QCOMPARE(devices[2]->fault.count(), 0);
        QCOMPARE(devices[3]->fault.count(), 0);
        QVERIFY(!sawLink(devices[2]->link, mc::LinkState::Disconnected));
        QVERIFY(!sawLink(devices[3]->link, mc::LinkState::Disconnected));

        // Every runner is alive and on its own thread: six distinct threads, none the GUI thread.
        std::vector<quintptr> threads;
        for (auto& device : devices) {
            threads.push_back(identity(device->host.runnerThread()->workerThread()));
        }
        threads.push_back(identity(mockA.host.runnerThread()->workerThread()));
        threads.push_back(identity(mockB.host.runnerThread()->workerThread()));
        for (size_t i = 0; i < threads.size(); ++i) {
            QVERIFY(threads[i] != identity(m_guiThread));
            for (size_t j = i + 1; j < threads.size(); ++j) {
                QVERIFY(threads[i] != threads[j]);
            }
        }
        QVERIFY(mockA.host.runnerThread()->isRunning());
        QVERIFY(mockB.host.runnerThread()->isRunning());
    }

    // The same, one level up: the mock tabs and the main window (two tabs next to each other).
    void GUI_02_twoMockTabsServeIndependentlyAndAFaultStaysInOneTab() {
        MockPane pane;
        MockTab* a = pane.addMock();
        MockTab* b = pane.addMock();
        QCOMPARE(pane.mockCount(), 2);
        QCOMPARE(a->name(), QStringLiteral("Mock 1"));
        QCOMPARE(b->name(), QStringLiteral("Mock 2"));

        a->startServing();
        b->startServing();
        QTRY_VERIFY_WITH_TIMEOUT(a->isServing() && b->isServing(), kWaitMs);
        QVERIFY(a->servingPort() != 0 && b->servingPort() != 0);
        QVERIFY(a->servingPort() != b->servingPort());
        QVERIFY(a->statusText().startsWith(QStringLiteral("Serving tcp 127.0.0.1:")));

        // The memory editors write into their own mocks.
        QVERIFY(a->memoryEditor()->editRow(1, 11));
        QVERIFY(b->memoryEditor()->editRow(1, 22));
        DeviceRig da(QStringLiteral("t070-da"), tcpConfig(a->servingPort(), false));
        DeviceRig db(QStringLiteral("t070-db"), tcpConfig(b->servingPort(), false));
        da.host.connectToPlc();
        db.host.connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(sawLink(da.link, mc::LinkState::Connected), kWaitMs);
        QTRY_VERIFY_WITH_TIMEOUT(sawLink(db.link, mc::LinkState::Connected), kWaitMs);
        const CommandResult ra = resultOf(da.done, da.host.readWords(QStringLiteral("D1"), 1));
        const CommandResult rb = resultOf(db.done, db.host.readWords(QStringLiteral("D1"), 1));
        QVERIFY(ra.ok && rb.ok);
        QCOMPARE(outcomeOf(da.finished, ra.value).payload, QByteArray("\x0B\x00", 2));
        QCOMPARE(outcomeOf(db.finished, rb.value).payload, QByteArray("\x16\x00", 2));

        // A fault in tab A (the mute box of its fault panel): B still answers, A does not.
        auto* mute = a->faultPanel()->findChild<QCheckBox*>();
        QVERIFY(mute != nullptr);
        mute->click();
        QVERIFY(a->faultPanel()->isMuted());
        const CommandResult muted = resultOf(da.done, da.host.readWords(QStringLiteral("D1"), 1));
        QVERIFY(muted.ok); // accepted; the answer never comes, the link faults
        QTRY_VERIFY_WITH_TIMEOUT(da.fault.count() >= 1, kWaitMs);
        const CommandResult still = resultOf(db.done, db.host.readWords(QStringLiteral("D1"), 1));
        QVERIFY(still.ok);
        QCOMPARE(outcomeOf(db.finished, still.value).errorCode, 0);
        QCOMPARE(db.fault.count(), 0);
    }

    // GUI-02 with the real tabs: one MainWindow, four DeviceTabs (DevicePane) on two MockTabs
    // (MockPane). Muting mock A faults only its own two device tabs; the two on mock B keep running.
    void GUI_02_fourDeviceTabsOnTwoMockTabsInTheMainWindow() {
        MainWindow window;
        DevicePane* devices = window.devicePane();
        auto* mocks = qobject_cast<MockPane*>(window.dockWidget(QStringLiteral("Mock PLCs"))->widget());
        QVERIFY(devices != nullptr && mocks != nullptr);

        MockTab* mockTab[2] = {mocks->addMock(), mocks->addMock()};
        for (MockTab* tab : mockTab) {
            tab->startServing();
        }
        QTRY_VERIFY_WITH_TIMEOUT(mockTab[0]->isServing() && mockTab[1]->isServing(), kWaitMs);
        QVERIFY(mockTab[0]->memoryEditor()->editRow(0, 11)); // D0 of mock A
        QVERIFY(mockTab[1]->memoryEditor()->editRow(0, 22)); // D0 of mock B

        DeviceTab* tabs[4];
        std::vector<std::unique_ptr<QSignalSpy>> states;
        for (int i = 0; i < 4; ++i) {
            tabs[i] = devices->addDevice();
            states.push_back(std::make_unique<QSignalSpy>(tabs[i], &DeviceTab::linkStateChanged));
            QSignalSpy applied(tabs[i], &DeviceTab::configApplied);
            mc::McDeviceConfig cfg = tcpConfig(mockTab[i / 2]->servingPort(), false);
            cfg.subscriptions = {{QStringLiteral("D0"), 2}};
            QString error;
            QVERIFY2(tabs[i]->setConfig(cfg, &error), qPrintable(error));
            QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, kWaitMs);
            QVERIFY(applied.last().at(0).toBool());
        }
        for (DeviceTab* tab : tabs) {
            tab->connectToPlc();
        }
        for (int i = 0; i < 4; ++i) {
            QTRY_COMPARE_WITH_TIMEOUT(tabs[i]->linkState(), mc::LinkState::Connected, kWaitMs);
            PointTableModel* points = tabs[i]->points();
            const quint16 expected = i < 2 ? 11 : 22;
            QTRY_VERIFY_WITH_TIMEOUT(points->rowOfDevice(QStringLiteral("D0")) >= 0 &&
                                         points->valueAt(points->rowOfDevice(QStringLiteral("D0"))) == expected,
                                     kWaitMs);
        }

        // The fault: mute mock A through its fault panel.
        auto* mute = mockTab[0]->faultPanel()->findChild<QCheckBox*>();
        QVERIFY(mute != nullptr);
        mute->click();
        QTRY_VERIFY_WITH_TIMEOUT(!tabs[0]->faultText().isEmpty() && !tabs[1]->faultText().isEmpty(), kWaitMs);

        // Mock B and its tabs are undisturbed, and still follow changes.
        QVERIFY(mockTab[1]->memoryEditor()->editRow(0, 33));
        for (int i = 2; i < 4; ++i) {
            PointTableModel* points = tabs[i]->points();
            QTRY_VERIFY_WITH_TIMEOUT(points->valueAt(points->rowOfDevice(QStringLiteral("D0"))) == 33, kWaitMs);
            QVERIFY(tabs[i]->faultText().isEmpty());
            QCOMPARE(tabs[i]->linkState(), mc::LinkState::Connected);
            QVERIFY(!sawLink(*states[static_cast<size_t>(i)], mc::LinkState::Disconnected));
        }
        QVERIFY(mockTab[1]->isServing());
        QVERIFY(mockTab[1]->statusText().startsWith(QStringLiteral("Serving")));
    }

    // The mock window pane is the content of the "Mock PLCs" dock of the main window.
    void theMainWindowHostsTheMockPane() {
        MainWindow window;
        ads::CDockWidget* dock = window.dockWidget(QStringLiteral("Mock PLCs"));
        QVERIFY(dock != nullptr);
        auto* pane = qobject_cast<MockPane*>(dock->widget());
        QVERIFY(pane != nullptr);
        QCOMPARE(pane->mockCount(), 0);
        MockTab* tab = pane->addMock();
        QVERIFY(tab != nullptr);
        QCOMPARE(pane->mockCount(), 1);
        pane->closeMock(0);
        QCOMPARE(pane->mockCount(), 0);
    }

    // Closing a mock tab joins its runner thread; no runner object is left behind and none was
    // deleted on a foreign thread.
    void closingAMockTabJoinsItsThread() {
        const int liveBefore = RunnerBase::liveRunners();
        const int foreignBefore = RunnerBase::foreignDeletes();
        MockPane pane;
        MockTab* tab = pane.addMock();
        QTRY_COMPARE_WITH_TIMEOUT(RunnerBase::liveRunners(), liveBefore + 1, kWaitMs);
        tab->startServing();
        QTRY_VERIFY_WITH_TIMEOUT(tab->isServing(), kWaitMs);
        QPointer<RunnerThread> thread = tab->host()->runnerThread();
        QVERIFY(thread->isRunning());
        pane.closeMock(0);
        QCOMPARE(pane.mockCount(), 0);
        QCOMPARE(RunnerBase::liveRunners(), liveBefore);
        QCOMPARE(RunnerBase::foreignDeletes(), foreignBefore);
    }

    // The mock, its server and the settings changes all live on the runner thread.
    void theMockLivesOnItsRunnerThreadAlsoAfterReconfigure() {
        MockHost host(QStringLiteral("t070-thread"), mc::FrameConfig::frame3E());
        QSignalSpy done(&host, &MockHost::commandDone);
        QSignalSpy threads(&host, &MockHost::threadReport);
        QVERIFY(resultOf(done, host.reconfigure(mc::FrameConfig::frame1E(), MockSettings{})).ok);
        host.requestThreadReport();
        QTRY_COMPARE_WITH_TIMEOUT(threads.count(), 1, kWaitMs);
        const quintptr mockThread = identity(host.runnerThread()->workerThread());
        QVERIFY(mockThread != identity(m_guiThread));
        const auto report = threads.first().at(0).value<ThreadReport>();
        QCOMPARE(report.objectThread, mockThread);
        QCOMPARE(report.deviceThread, mockThread);
        QCOMPARE(report.transportThread, mockThread);
    }

    // Memory editor path: write words and bits, read them back as copies; bad input is refused.
    void memoryIsWrittenAndReadBackAsCopies() {
        MockHost host(QStringLiteral("t070-mem"), mc::FrameConfig::frame3E());
        QSignalSpy done(&host, &MockHost::commandDone);
        QSignalSpy blocks(&host, &MockHost::memoryRead);
        QVERIFY(resultOf(done, host.setWords(QStringLiteral("D100"), {1, 2, 65535})).ok);
        QVERIFY(resultOf(done, host.setBits(QStringLiteral("M5"), {true, false, true})).ok);

        const quint64 wordsToken = host.readMemory(QStringLiteral("D100"), 3, false);
        QVERIFY(resultOf(done, wordsToken).ok);
        QTRY_COMPARE_WITH_TIMEOUT(blocks.count(), 1, kWaitMs);
        const auto words = blocks.at(0).at(0).value<MemoryBlock>();
        QCOMPARE(words.token, wordsToken);
        QVERIFY(!words.bits);
        QCOMPARE(words.values, (QVector<quint16>{1, 2, 65535}));

        QVERIFY(resultOf(done, host.readMemory(QStringLiteral("M4"), 5, true)).ok);
        QTRY_COMPARE_WITH_TIMEOUT(blocks.count(), 2, kWaitMs);
        const auto bits = blocks.at(1).at(0).value<MemoryBlock>();
        QVERIFY(bits.bits);
        QCOMPARE(bits.values, (QVector<quint16>{0, 1, 0, 1, 0}));

        const CommandResult badHead = resultOf(done, host.readMemory(QStringLiteral("Q9"), 1, false));
        QVERIFY(!badHead.ok);
        QVERIFY(!badHead.message.isEmpty());
        QVERIFY(!resultOf(done, host.readMemory(QStringLiteral("D0"), 0, false)).ok);
        QVERIFY(!resultOf(done, host.readMemory(QStringLiteral("D0"), MockRunner::kMaxMemoryPoints + 1, false)).ok);
        QVERIFY(!resultOf(done, host.setWords(QStringLiteral("nope"), {1})).ok);
        QCOMPARE(blocks.count(), 2);
    }

    // The memory editor widget: edits leave as signals with the device text, the shown block is a
    // copy, a block for another range is ignored, X/Y follow the numbering.
    void theMemoryEditorSendsEditsAndShowsCopies() {
        MockMemoryEditor editor;
        QSignalSpy words(&editor, &MockMemoryEditor::wordEdited);
        QSignalSpy bitsEdited(&editor, &MockMemoryEditor::bitEdited);
        QSignalSpy reads(&editor, &MockMemoryEditor::readRequested);

        QVERIFY(editor.setRange(QStringLiteral("D100"), 4));
        QVERIFY(!editor.isBitRange());
        QVERIFY(editor.editRow(2, 4660));
        QCOMPARE(words.count(), 1);
        QCOMPARE(words.at(0).at(0).toString(), QStringLiteral("D102"));
        QCOMPARE(words.at(0).at(1).value<quint16>(), quint16(4660));

        MemoryBlock block;
        block.head = QStringLiteral("D100");
        block.values = {5, 6, 7, 8};
        editor.showBlock(block);
        QCOMPARE(editor.valueAt(0), quint16(5));
        QCOMPARE(editor.valueAt(3), quint16(8));
        block.head = QStringLiteral("D200"); // not the shown range
        block.values = {9, 9, 9, 9};
        editor.showBlock(block);
        QCOMPARE(editor.valueAt(0), quint16(5));

        QVERIFY(editor.setRange(QStringLiteral("M0"), 8));
        QVERIFY(editor.isBitRange());
        QVERIFY(editor.editRow(3, 1));
        QCOMPARE(bitsEdited.count(), 1);
        QCOMPARE(bitsEdited.at(0).at(0).toString(), QStringLiteral("M3"));
        QVERIFY(bitsEdited.at(0).at(1).toBool());

        QVERIFY(!editor.setRange(QStringLiteral("not a device"), 4));
        QVERIFY(!editor.setRange(QStringLiteral("D0"), MockMemoryEditor::kMaxPoints + 1));
        QCOMPARE(editor.head(), QStringLiteral("M0")); // unchanged by the refused ranges

        // Hex numbering: X18 is a point; octal: X8 is not a number, X17 is.
        QVERIFY(editor.setRange(QStringLiteral("X18"), 4));
        editor.setXyNotation(mc::XyNumbering::Octal);
        QVERIFY(!editor.setRange(QStringLiteral("X18"), 4));
        QVERIFY(editor.setRange(QStringLiteral("X17"), 4));
        reads.clear();
        editor.refreshNow();
        QCOMPARE(reads.count(), 1);
        QCOMPARE(reads.at(0).at(0).toString(), QStringLiteral("X17"));
        QVERIFY(reads.at(0).at(2).toBool());
    }

    // failRange: an access to the range is answered with the PLC error, outside it with the data;
    // the request log shows both; clearFaults removes the fault.
    void failRangeAnswersWithThePlcErrorAndClearFaultsRemovesIt() {
        MockRig mock(QStringLiteral("t070-fail"), mc::FrameConfig::frame3E(), 0);
        QVERIFY(mock.ok);
        QSignalSpy log(&mock.host, &MockHost::requestsLogged);
        DeviceRig dev(QStringLiteral("t070-faildev"), tcpConfig(mock.port, false));
        dev.host.connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(sawLink(dev.link, mc::LinkState::Connected), kWaitMs);

        QVERIFY(resultOf(mock.done, mock.host.failRange(mc::DeviceType::D, 100, 109, 0xC050)).ok);
        const CommandResult bad = resultOf(dev.done, dev.host.readWords(QStringLiteral("D105"), 1));
        QVERIFY(bad.ok);
        const RequestOutcome failed = outcomeOf(dev.finished, bad.value);
        QVERIFY(failed.errorCode != 0);
        QVERIFY(failed.payload.isEmpty());

        const CommandResult good = resultOf(dev.done, dev.host.readWords(QStringLiteral("D200"), 1));
        QCOMPARE(outcomeOf(dev.finished, good.value).errorCode, 0);

        const auto find = [&](const QString& head, bool wantError) {
            for (const QList<QVariant>& args : log) {
                for (const MockRequestEntry& e : args.at(0).value<MockRequestBatch>().entries) {
                    if (e.head == head && e.op == QLatin1String("ReadWords") &&
                        (e.errorCode != 0) == wantError && (!wantError || e.plcCode == 0xC050)) {
                        return true;
                    }
                }
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(find(QStringLiteral("D105"), true), kWaitMs);
        QTRY_VERIFY_WITH_TIMEOUT(find(QStringLiteral("D200"), false), kWaitMs);

        QVERIFY(resultOf(mock.done, mock.host.clearFaults()).ok);
        const CommandResult again = resultOf(dev.done, dev.host.readWords(QStringLiteral("D105"), 1));
        QCOMPARE(outcomeOf(dev.finished, again.value).errorCode, 0);

        // A reversed range is refused by the runner.
        QVERIFY(!resultOf(mock.done, mock.host.failRange(mc::DeviceType::D, 10, 5, 0xC050)).ok);
    }

    // setDeviceLimit: beyond the limit the mock answers with the out-of-range code of its settings.
    void deviceLimitAnswersWithTheConfiguredOutOfRangeCode() {
        MockSettings settings;
        settings.outOfRangeQna = 0xC123;
        MockHost host(QStringLiteral("t070-limit"), mc::FrameConfig::frame3E(), settings);
        QSignalSpy done(&host, &MockHost::commandDone);
        QSignalSpy log(&host, &MockHost::requestsLogged);
        const CommandResult listening = resultOf(done, host.listen(0));
        QVERIFY(listening.ok);
        QVERIFY(resultOf(done, host.setDeviceLimit(mc::DeviceType::D, 50)).ok);

        DeviceRig dev(QStringLiteral("t070-limitdev"), tcpConfig(static_cast<quint16>(listening.value), false));
        dev.host.connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(sawLink(dev.link, mc::LinkState::Connected), kWaitMs);
        const CommandResult over = resultOf(dev.done, dev.host.readWords(QStringLiteral("D60"), 1));
        QVERIFY(outcomeOf(dev.finished, over.value).errorCode != 0);
        const auto sawCode = [&]() {
            for (const QList<QVariant>& args : log) {
                for (const MockRequestEntry& e : args.at(0).value<MockRequestBatch>().entries) {
                    if (e.head == QLatin1String("D60") && e.plcCode == 0xC123) {
                        return true;
                    }
                }
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(sawCode(), kWaitMs);
    }

    // corruptNext: one damaged response makes the Ethernet client fault with ProtocolError, then
    // the mock answers normally again (a reconnect is the client's business, not asserted here).
    void corruptNextDamagesTheNextResponse() {
        MockRig mock(QStringLiteral("t070-corrupt"), mc::FrameConfig::frame3E(), 0);
        QVERIFY(mock.ok);
        DeviceRig dev(QStringLiteral("t070-corruptdev"), tcpConfig(mock.port, false));
        dev.host.connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(sawLink(dev.link, mc::LinkState::Connected), kWaitMs);

        QVERIFY(resultOf(mock.done, mock.host.corruptNext(mc::Corruption::WrongSubheader, 1)).ok);
        const CommandResult read = resultOf(dev.done, dev.host.readWords(QStringLiteral("D100"), 1));
        QVERIFY(read.ok);
        QTRY_VERIFY_WITH_TIMEOUT(dev.fault.count() >= 1, kWaitMs);
        QCOMPARE(dev.fault.first().at(0).value<FaultReport>().kind,
                 static_cast<int>(mc::LinkFaultKind::ProtocolError));
    }

    // muteNext: exactly the next request goes unanswered and is logged as such; the one after is
    // answered (a second client, so that the first one's link fault does not matter).
    void muteNextSwallowsExactlyTheNextRequest() {
        MockRig mock(QStringLiteral("t070-mutenext"), mc::FrameConfig::frame3E(), 0);
        QVERIFY(mock.ok);
        QSignalSpy log(&mock.host, &MockHost::requestsLogged);
        DeviceRig first(QStringLiteral("t070-mn1"), tcpConfig(mock.port, false, 300));
        first.host.connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(sawLink(first.link, mc::LinkState::Connected), kWaitMs);

        QVERIFY(resultOf(mock.done, mock.host.muteNext(1)).ok);
        QVERIFY(resultOf(first.done, first.host.readWords(QStringLiteral("D100"), 1)).ok);
        QTRY_VERIFY_WITH_TIMEOUT(first.fault.count() >= 1, kWaitMs);
        QCOMPARE(first.fault.first().at(0).value<FaultReport>().kind,
                 static_cast<int>(mc::LinkFaultKind::Timeout));

        DeviceRig second(QStringLiteral("t070-mn2"), tcpConfig(mock.port, false, 300));
        second.host.connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(sawLink(second.link, mc::LinkState::Connected), kWaitMs);
        const CommandResult read = resultOf(second.done, second.host.readWords(QStringLiteral("D100"), 1));
        QCOMPARE(outcomeOf(second.finished, read.value).errorCode, 0);

        int unanswered = 0;
        int answered = 0;
        const auto count = [&]() {
            unanswered = 0;
            answered = 0;
            for (const QList<QVariant>& args : log) {
                for (const MockRequestEntry& e : args.at(0).value<MockRequestBatch>().entries) {
                    (e.answered ? answered : unanswered)++;
                }
            }
            return unanswered >= 1 && answered >= 1;
        };
        QTRY_VERIFY_WITH_TIMEOUT(count(), kWaitMs);
        QCOMPARE(unanswered, 1);
    }

    // Counters: on a serial frame served over TCP the mock counts skipped bytes and EOT, and logs
    // the skipped bytes at Trace through its LogSink into the log batches.
    void eotAndSkippedBytesAreCountedAndLogged() {
        MockHost host(QStringLiteral("t070-count"), mc::FrameConfig::frame3C());
        QSignalSpy done(&host, &MockHost::commandDone);
        QSignalSpy stats(&host, &MockHost::statsChanged);
        QSignalSpy logs(&host, &MockHost::logBatch);
        host.setLogLevel(mc::LogLevel::Trace);
        const CommandResult listening = resultOf(done, host.listen(0));
        QVERIFY(listening.ok);

        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, static_cast<quint16>(listening.value));
        QVERIFY(socket.waitForConnected(kWaitMs));
        // "abc" before the ENQ is skipped; the EOT (0x04) after the ENQ discards the partial request.
        socket.write(QByteArray("abc\x05\x04", 5));
        QVERIFY(socket.waitForBytesWritten(kWaitMs));

        const auto totals = [&]() {
            if (stats.isEmpty()) {
                return false;
            }
            const auto s = stats.last().at(0).value<MockStats>();
            return s.skippedBytes == 3 && s.eotCount == 1;
        };
        QTRY_VERIFY_WITH_TIMEOUT(totals(), kWaitMs);
        const auto sawMockLine = [&]() {
            for (const QList<QVariant>& args : logs) {
                for (const LogLine& line : args.at(0).value<QVector<LogLine>>()) {
                    if (line.category == QLatin1String("mc.mock")) {
                        return true;
                    }
                }
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(sawMockLine(), kWaitMs);
    }

    // Request log model: bounded ring, drop count, clear.
    void theRequestLogIsABoundedRing() {
        MockRequestLogModel model;
        model.setCapacity(5);
        const auto batchOf = [](int from, int to, quint32 dropped = 0) {
            MockRequestBatch batch;
            batch.dropped = dropped;
            for (int i = from; i <= to; ++i) {
                MockRequestEntry e;
                e.seq = static_cast<quint64>(i);
                e.op = QStringLiteral("ReadWords");
                e.head = QStringLiteral("D%1").arg(i);
                e.answered = true;
                batch.entries.push_back(e);
            }
            return batch;
        };
        model.append(batchOf(1, 3));
        QCOMPARE(model.rowCount(), 3);
        model.append(batchOf(4, 8, 2)); // 3 + 5 > 5: the three oldest and three more go
        QCOMPARE(model.rowCount(), 5);
        QCOMPARE(model.entry(0).seq, quint64(4));
        QCOMPARE(model.entry(4).seq, quint64(8));
        QCOMPARE(model.droppedTotal(), quint64(2 + 3));
        model.append(batchOf(9, 20)); // larger than the capacity: only the newest five stay
        QCOMPARE(model.rowCount(), 5);
        QCOMPARE(model.entry(0).seq, quint64(16));
        QCOMPARE(model.entry(4).seq, quint64(20));
        QCOMPARE(model.data(model.index(4, MockRequestLogModel::Result)).toString(), QStringLiteral("ok"));
        model.setCapacity(2);
        QCOMPARE(model.rowCount(), 2);
        model.clear();
        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(model.droppedTotal(), quint64(0));
    }

    // One flush carries at most kMaxRequestsPerBatch entries; a burst bigger than that is reported
    // as dropped, and the totals stay exact. The burst is fed into the mock on its own thread (as
    // the serving code does), one well-formed 3E read after the other, between two flushes.
    void aBurstOfRequestsIsBatchedWithABound() {
        MockHost host(QStringLiteral("t070-burst"), mc::FrameConfig::frame3E());
        QSignalSpy done(&host, &MockHost::commandDone);
        QSignalSpy log(&host, &MockHost::requestsLogged);
        QSignalSpy stats(&host, &MockHost::statsChanged);
        const CommandResult listening = resultOf(done, host.listen(0));
        QVERIFY(listening.ok);

        static constexpr int kBurst = MockRunner::kMaxRequestsPerBatch + 500;
        std::atomic<int> fed{0};
        host.post([&fed](MockRunner& runner) {
            mc::Request read;
            read.op = mc::Op::ReadWords;
            read.head = mc::Device{mc::DeviceType::D, 0};
            read.count = 1;
            const auto frame = mc::McProtocol(mc::FrameConfig::frame3E()).encode(read);
            if (!frame) {
                return;
            }
            for (int i = 0; i < kBurst; ++i) {
                runner.plc()->bytesIn(mc::ByteView{frame.value().data(), frame.value().size()});
                mc::ByteView out;
                while (runner.plc()->nextResponse(out)) {
                }
            }
            fed = kBurst;
        });
        QTRY_VERIFY_WITH_TIMEOUT(fed.load() == kBurst, kWaitMs);

        // A client connecting makes the runner flush what the mock logged.
        QTcpSocket poke;
        poke.connectToHost(QHostAddress::LocalHost, static_cast<quint16>(listening.value));
        QVERIFY(poke.waitForConnected(kWaitMs));

        const auto accounted = [&]() {
            int entries = 0;
            for (const QList<QVariant>& args : log) {
                const auto batch = args.at(0).value<MockRequestBatch>();
                if (batch.entries.size() > MockRunner::kMaxRequestsPerBatch) {
                    return -1; // a batch beyond its bound
                }
                entries += static_cast<int>(batch.entries.size() + batch.dropped);
            }
            return entries;
        };
        QTRY_COMPARE_WITH_TIMEOUT(accounted(), kBurst, kWaitMs);
        bool sawDrop = false;
        for (const QList<QVariant>& args : log) {
            sawDrop = sawDrop || args.at(0).value<MockRequestBatch>().dropped == 500;
        }
        QVERIFY(sawDrop);
        QTRY_VERIFY_WITH_TIMEOUT(
            !stats.isEmpty() && stats.last().at(0).value<MockStats>().requests == quint64(kBurst), kWaitMs);
    }

    // reconfigure: refused while serving, applied while stopped (memory empty again), an invalid
    // frame is refused with the library's message, an octal FX frame makes X10 index 8.
    void reconfigureReplacesTheMockOnlyWhileStopped() {
        MockHost host(QStringLiteral("t070-reconf"), mc::FrameConfig::frame3E());
        QSignalSpy done(&host, &MockHost::commandDone);
        QSignalSpy blocks(&host, &MockHost::memoryRead);
        QVERIFY(resultOf(done, host.setWords(QStringLiteral("D0"), {42})).ok);
        QVERIFY(resultOf(done, host.listen(0)).ok);

        const CommandResult busy = resultOf(done, host.reconfigure(mc::FrameConfig::frame1E(), MockSettings{}));
        QVERIFY(!busy.ok);
        QVERIFY(busy.message.contains(QLatin1String("stop")));

        host.stopListening();
        mc::FrameConfig bad = mc::FrameConfig::frame3E();
        bad.frame = mc::FrameType::F4E;
        const CommandResult invalid = resultOf(done, host.reconfigure(bad, MockSettings{}));
        QVERIFY(!invalid.ok);
        QVERIFY(!invalid.message.isEmpty());

        mc::FrameConfig fx = mc::FrameConfig::frame3E();
        fx.xyNotation = mc::XyNumbering::Octal;
        QVERIFY(resultOf(done, host.reconfigure(fx, MockSettings{})).ok);
        QVERIFY(resultOf(done, host.readMemory(QStringLiteral("D0"), 1, false)).ok);
        QTRY_COMPARE_WITH_TIMEOUT(blocks.count(), 1, kWaitMs);
        QCOMPARE(blocks.at(0).at(0).value<MemoryBlock>().values, (QVector<quint16>{0})); // image emptied
        QVERIFY(resultOf(done, host.setBits(QStringLiteral("X10"), {true})).ok);
        QVERIFY(resultOf(done, host.readMemory(QStringLiteral("X7"), 3, true)).ok);
        QTRY_COMPARE_WITH_TIMEOUT(blocks.count(), 2, kWaitMs);
        QCOMPARE(blocks.at(1).at(0).value<MemoryBlock>().values, (QVector<quint16>{0, 1, 0}));
        // The server can listen again on the new mock.
        QVERIFY(resultOf(done, host.listen(0)).ok);
    }

    // The tab: settings become the frame and the codes, validation words come from the library, a
    // changed frame rebuilds the mock at the next start (octal numbering reaches the editor).
    void theTabAppliesChangedSettingsWhenServingStarts() {
        MockTab tab(QStringLiteral("t070-tab"));
        QVERIFY(tab.configError().isEmpty());
        QCOMPARE(tab.frameConfig().frame, mc::FrameType::F3E);
        QVERIFY(tab.configModel()->setValue(QStringLiteral("Frame/frame"), 2));
        QCOMPARE(tab.frameConfig().frame, mc::FrameType::F3C);
        QCOMPARE(tab.frameConfig().code, mc::DataCode::Ascii);
        QVERIFY(tab.configModel()->setValue(QStringLiteral("Frame/format"), 1));
        QCOMPARE(tab.frameConfig().format, mc::SerialFormat::Format2);
        QVERIFY(tab.configError().isEmpty());

        // The hex error code fields reject what is not hex and what is too wide.
        QVERIFY(!tab.configModel()->setValue(QStringLiteral("Errors/unsupportedQna"), QStringLiteral("zz")));
        QVERIFY(!tab.configModel()->setValue(QStringLiteral("Errors/unsupported1e"), QStringLiteral("1FF")));
        QVERIFY(tab.configModel()->setValue(QStringLiteral("Errors/unsupportedQna"), QStringLiteral("C059")));
        QVERIFY(tab.configModel()->setValue(QStringLiteral("Errors/outOfRangeQna"), QStringLiteral("0xC111")));
        QCOMPARE(tab.mockSettings().outOfRangeQna, quint16(0xC111));

        QVERIFY(tab.configModel()->setValue(QStringLiteral("Frame/xyNotation"), 1));
        QVERIFY(tab.memoryEditor()->setRange(QStringLiteral("X18"), 2)); // hex until the mock is rebuilt
        tab.startServing();
        QTRY_VERIFY_WITH_TIMEOUT(tab.isServing(), kWaitMs);
        QVERIFY(!tab.memoryEditor()->setRange(QStringLiteral("X18"), 2)); // octal now
        QVERIFY(tab.memoryEditor()->setRange(QStringLiteral("X17"), 2));

        // While serving a further change only asks for a restart; it does not touch the mock.
        QVERIFY(tab.configModel()->setValue(QStringLiteral("Frame/blockNo"), 3));
        QVERIFY(tab.statusText().contains(QLatin1String("stop and start")));
        tab.stopServing();
        QTRY_VERIFY_WITH_TIMEOUT(!tab.isServing(), kWaitMs);
        QCOMPARE(tab.servingPort(), quint16(0));
        QCOMPARE(tab.statusText().left(7), QStringLiteral("Stopped"));
    }

    // An empty COM port name is caught before anything is sent to the runner.
    void anInvalidSettingIsReportedAndNothingStarts() {
        MockTab tab(QStringLiteral("t070-invalid"));
        QVERIFY(tab.configModel()->setValue(QStringLiteral("Serving/mode"), 1));
        QVERIFY(tab.configModel()->setValue(QStringLiteral("Serving/comPort"), QString()));
        QVERIFY(tab.servesSerial());
        QVERIFY(!tab.configError().isEmpty());
        tab.startServing();
        QVERIFY(!tab.isStarting());
        QVERIFY(!tab.isServing());
        QVERIFY(tab.statusText().contains(QLatin1String("not valid")));
    }

    // The mock log level of the grid reaches the runner; the mock log lines show up in the tab log.
    void theLogLevelOfTheGridReachesTheMockAndLinesShowInTheTabLog() {
        MockTab tab(QStringLiteral("t070-log"));
        QVERIFY(tab.configModel()->setValue(QStringLiteral("Frame/frame"), 2)); // 3C
        QVERIFY(tab.configModel()->setValue(QStringLiteral("Serving/logLevel"), 0)); // Trace
        tab.startServing();
        QTRY_VERIFY_WITH_TIMEOUT(tab.isServing(), kWaitMs);

        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, tab.servingPort());
        QVERIFY(socket.waitForConnected(kWaitMs));
        socket.write(QByteArray("xy\x05\x04", 4));
        QVERIFY(socket.waitForBytesWritten(kWaitMs));
        QTRY_VERIFY_WITH_TIMEOUT(tab.logText().contains(QLatin1String("mc.mock")), kWaitMs);
        QTRY_VERIFY_WITH_TIMEOUT(tab.stats().skippedBytes == 2 && tab.stats().eotCount == 1, kWaitMs);
    }

    // The fault panel: every control only emits; the list records what was asked for.
    void theFaultPanelEmitsRequestsAndRecordsFaults() {
        MockFaultPanel panel;
        QSignalSpy failRange(&panel, &MockFaultPanel::failRangeRequested);
        QSignalSpy clear(&panel, &MockFaultPanel::clearFaultsRequested);
        QSignalSpy mute(&panel, &MockFaultPanel::muteToggled);
        panel.requestFailRange(mc::DeviceType::D, 10, 20, 0xC050, 0);
        QCOMPARE(failRange.count(), 1);
        QCOMPARE(panel.faultLines().size(), 1);
        QVERIFY(panel.faultLines().first().contains(QLatin1String("D10..20")));
        QVERIFY(panel.faultLines().first().contains(QLatin1String("C050")));
        panel.setMuted(true); // programmatic: no signal
        QCOMPARE(mute.count(), 0);
        QVERIFY(panel.isMuted());
        panel.clearFaultLines();
        QCOMPARE(panel.faultLines().size(), 0);
        QCOMPARE(clear.count(), 0);
        QCOMPARE(MockFaultPanel::corruptionName(mc::Corruption::ExtraByte), QStringLiteral("ExtraByte"));
    }

    // COM case: the mock serves a 3C frame on the first port of the virtual pair, a device on the
    // second one polls it, values and a change arrive, the request log and counters fill, the port
    // is released again by stopListening. QSKIP without MC_TEST_SERIAL_PAIR.
    void GUI_02_comPairMockServesADeviceOverSerial() {
        if (qEnvironmentVariableIsEmpty("MC_TEST_SERIAL_PAIR")) {
            QSKIP("MC_TEST_SERIAL_PAIR is not set (e.g. \"COM54,COM55\"): no virtual COM pair");
        }
        const QString mockPort = comPortOne();
        const QString devicePort = comPortTwo();
        QVERIFY(!mockPort.isEmpty() && !devicePort.isEmpty());

        MockHost mock(QStringLiteral("t070-com-mock"), mc::FrameConfig::frame3C());
        QSignalSpy done(&mock, &MockHost::commandDone);
        QSignalSpy serving(&mock, &MockHost::servingChanged);
        QSignalSpy log(&mock, &MockHost::requestsLogged);
        QSignalSpy stats(&mock, &MockHost::statsChanged);
        QVERIFY(resultOf(done, mock.setWords(QStringLiteral("D100"), {10, 20, 30, 40})).ok);

        SerialLine line;
        line.portName = mockPort; // 9600 7E1, as the device side below
        const CommandResult opened = resultOf(done, mock.openSerial(line));
        QVERIFY2(opened.ok, qPrintable(opened.message));
        QTRY_VERIFY_WITH_TIMEOUT(serving.count() >= 1, kWaitMs);
        QVERIFY(serving.last().at(0).toBool());
        QVERIFY(serving.last().at(1).toString().startsWith(mockPort));
        QVERIFY(serving.last().at(1).toString().contains(QLatin1String("9600 7E1")));

        // One mode at a time: TCP serving is refused while the COM port is open; a second open too.
        QVERIFY(!resultOf(done, mock.listen(0)).ok);
        QVERIFY(!resultOf(done, mock.openSerial(line)).ok);

        mc::McDeviceConfig cfg;
        cfg.frame = mc::FrameConfig::frame3C();
        cfg.transport = mc::TransportKind::Serial;
        cfg.serial.portName = devicePort;
        cfg.session.cycleIntervalMs = 50;
        cfg.frame.timeoutMs = 2000;
        cfg.subscriptions = {{QStringLiteral("D100"), 4}};
        DeviceRig dev(QStringLiteral("t070-com-dev"), cfg);
        dev.host.connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(sawLink(dev.link, mc::LinkState::Connected), kWaitMs);
        QTRY_VERIFY_WITH_TIMEOUT(sawWord(dev.values, 0, 10) && sawWord(dev.values, 3, 40), kWaitMs);

        QVERIFY(resultOf(done, mock.setWords(QStringLiteral("D101"), {555})).ok);
        QTRY_VERIFY_WITH_TIMEOUT(sawWord(dev.values, 1, 555), kWaitMs);
        QCOMPARE(dev.fault.count(), 0);

        const auto sawRead = [&]() {
            for (const QList<QVariant>& args : log) {
                for (const MockRequestEntry& e : args.at(0).value<MockRequestBatch>().entries) {
                    if (e.head == QLatin1String("D100") && e.op == QLatin1String("ReadWords") &&
                        e.answered && e.errorCode == 0) {
                        return true;
                    }
                }
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(sawRead(), kWaitMs);
        QTRY_VERIFY_WITH_TIMEOUT(!stats.isEmpty() && stats.last().at(0).value<MockStats>().requests > 0,
                                 kWaitMs);

        // Stop: the port is released, so a plain QSerialPort can open it, and serving can restart.
        dev.host.disconnectFromPlc();
        QTRY_VERIFY_WITH_TIMEOUT(sawLink(dev.link, mc::LinkState::Disconnected), kWaitMs);
        mock.stopListening();
        QTRY_VERIFY_WITH_TIMEOUT(!serving.last().at(0).toBool(), kWaitMs);
        {
            QSerialPort probe;
            probe.setPortName(mockPort);
            QVERIFY2(probe.open(QIODevice::ReadWrite), qPrintable(probe.errorString()));
            probe.close();
        }
        QVERIFY(resultOf(done, mock.openSerial(line)).ok);
        mock.stopListening();
    }

    // COM errors: a port that does not exist, and one that another process holds, are refused with
    // the port's own message (the holder case needs the pair).
    void openSerialReportsMissingAndBusyPorts() {
        MockHost mock(QStringLiteral("t070-com-err"), mc::FrameConfig::frame3C());
        QSignalSpy done(&mock, &MockHost::commandDone);
        SerialLine missing;
        missing.portName = QStringLiteral("COM253");
        const CommandResult none = resultOf(done, mock.openSerial(missing));
        QVERIFY(!none.ok);
        QVERIFY(!none.message.isEmpty());
        SerialLine unnamed;
        QVERIFY(!resultOf(done, mock.openSerial(unnamed)).ok);

        if (qEnvironmentVariableIsEmpty("MC_TEST_SERIAL_PAIR")) {
            QSKIP("MC_TEST_SERIAL_PAIR is not set: the busy-port part needs the virtual COM pair");
        }
        QSerialPort holder;
        holder.setPortName(comPortOne());
        QVERIFY2(holder.open(QIODevice::ReadWrite), qPrintable(holder.errorString()));
        SerialLine busy;
        busy.portName = comPortOne();
        const CommandResult refused = resultOf(done, mock.openSerial(busy));
        QVERIFY(!refused.ok);
        QVERIFY(!refused.message.isEmpty());
        holder.close();
    }

private:
    QThread* m_guiThread{nullptr};
};

namespace mc::workbench::test {

QObject* makeMockTabSuite() {
    return new MockTabTest;
}

} // namespace mc::workbench::test

#include "tst_gui_mock_tab.moc"
