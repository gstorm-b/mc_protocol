// tst_mc_device.cpp -- McDevice against a MockPlcServer over loopback TCP (127.0.0.1, OS-chosen
// port). Test ids QDV-nn are the spec's (SPEC-qt-device.md); PMP-nn are local tests of the pump
// and signal queue. No positive outcome is gated on a fixed sleep: tests wait on state with
// QTRY_* and a bounded timeout. Signal order is asserted through DeviceRecorder::trace().
#include "device_recorder.h"
#include "device_test_support.h"
#include "fake_transport.h"
#include "mock_plc_server.h"
#include "smoke_check.h"

#include "mc/core/convert.h"
#include "mc/device/mc_device.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QSignalSpy>
#include <QtTest>

#include <functional>

using namespace testing;

class TstMcDevice : public QObject {
    Q_OBJECT

  private:
    // One server (seeded) and one device pointing at it, with a recorder on the device.
    struct Rig {
        MockPlcServer server;
        std::unique_ptr<mc::McDevice> device;
        DeviceRecorder* recorder{nullptr};

        explicit Rig(const mc::FrameConfig& frame = mc::FrameConfig::frame3E(),
                     bool subscribe = true, bool mute = false,
                     const std::function<void(mc::McDeviceConfig&)>& tweak = {})
            : server(frame) {
            server.setInit(seedMemory);
            if (!server.listen()) {
                return;
            }
            server.mute(mute);
            mc::McDeviceConfig cfg = configFor(server.port(), frame, subscribe);
            if (tweak) {
                tweak(cfg);
            }
            device = std::make_unique<mc::McDevice>(cfg);
            recorder = new DeviceRecorder(*device);
        }
        bool ok() const { return device != nullptr; }
    };

    // A device over a FakeTransport (no socket): full control of open, write and loss.
    struct FakeRig {
        FakeTransport* transport{nullptr};
        std::unique_ptr<mc::McDevice> device;
        DeviceRecorder* recorder{nullptr};

        explicit FakeRig(bool failOpen = false) {
            auto fake = std::make_unique<FakeTransport>();
            fake->failOpen = failOpen;
            transport = fake.get();
            // cfg.tcp is ignored with an injected transport, so the port 0 here is harmless.
            device = std::make_unique<mc::McDevice>(
                configFor(0, mc::FrameConfig::frame3E(), false), std::move(fake));
            recorder = new DeviceRecorder(*device);
        }
    };

    // QDV-13: one polling round and one write of each kind over loopback TCP on `frame` (the
    // shared body is smoke_check.h; QDV-14 runs it over a COM port pair).
    void smokeOverTcp(const mc::FrameConfig& frame) {
        Rig rig(frame);
        QVERIFY(rig.ok());
        rig.device->connectToPlc();
        verifySmoke(*rig.recorder, *rig.device, [&rig]() { return rig.server.plc(); });
    }

  private slots:
    void QDV_01_connectThenRoundOneSnapshotsInTypeOrderThenCycle() {
        Rig rig;
        QVERIFY(rig.ok());
        rig.device->connectToPlc();
        QCOMPARE(rig.device->linkState(), mc::LinkState::Connecting);
        QTRY_VERIFY_WITH_TIMEOUT(rig.recorder->hasCycle(1), kWaitMs);

        // Round 1: one snapshot per subscribed type in DeviceType order (M before D), then the
        // cycle; the link states come first.
        const QStringList expected{"link(Connecting,Requested)", "link(Connected,Requested)",
                                   "snapshot(M,1)", "snapshot(D,1)", "cycle(1)"};
        QCOMPARE(rig.recorder->trace().mid(0, expected.size()), expected);
        QCOMPARE(rig.device->linkState(), mc::LinkState::Connected);

        // No valuesChanged in round 1.
        for (const Event& e : rig.recorder->of(Event::Kind::Changed)) {
            QVERIFY2(e.round >= 2, "valuesChanged in round 1");
        }

        // The D snapshot carries the mock's memory in the normalized layout, all points Valid.
        const Event snapshotD = rig.recorder->of(Event::Kind::Snapshot).at(1);
        QCOMPARE(snapshotD.snapshot.type, mc::DeviceType::D);
        QCOMPARE(snapshotD.snapshot.segments.size(), 1);
        const mc::SnapshotSegment& seg = snapshotD.snapshot.segments.at(0);
        QCOMPARE(seg.head.number, 100u);
        QCOMPARE(seg.count, 4u);
        QCOMPARE(seg.values, wordsLe({10, 20, 30, 40}));
        QCOMPARE(seg.states, QByteArray(4, static_cast<char>(mc::PointState::Valid)));
        QCOMPARE(snapshotD.snapshot.chunks.size(), 1);
        QCOMPARE(snapshotD.snapshot.chunks.at(0).state, mc::ChunkState::Ok);
        QCOMPARE(snapshotD.snapshot.chunks.at(0).request.count, uint16_t{4});

        // The M snapshot: one byte per point, M3 set.
        const mc::SnapshotSegment& bits =
            rig.recorder->of(Event::Kind::Snapshot).at(0).snapshot.segments.at(0);
        QCOMPARE(bits.count, 16u);
        QCOMPARE(bits.values.size(), 16);
        QCOMPARE(int(bits.values.at(3)), 1);
        QCOMPARE(int(bits.values.at(2)), 0);

        QCOMPARE(rig.recorder->of(Event::Kind::Cycle).at(0).cycle.round, 1u);
        QCOMPARE(rig.server.connectionCount(), 1);
    }

    void QDV_02_memoryChangedBetweenRoundsGivesValuesChangedThenSnapshot() {
        Rig rig;
        QVERIFY(rig.ok());
        // Change the mock from inside the round-1 cycleDone slot: the slot runs during the
        // flush, before the round-2 timer can fire, so the change is certain to precede the
        // round-2 read.
        QObject::connect(rig.device.get(), &mc::McDevice::cycleDone, rig.device.get(),
                         [&rig](const mc::CycleInfo& c) {
                             if (c.round == 1) {
                                 rig.server.plc()->setWord(dev("D101"), 999);
                             }
                         });
        rig.device->connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(rig.recorder->hasCycle(2), kWaitMs);

        const QStringList round2 = rig.recorder->trace().mid(5, 4);
        QCOMPARE(round2,
                 (QStringList{"snapshot(M,2)", "changed(D,2)", "snapshot(D,2)", "cycle(2)"}));

        const Event changed = rig.recorder->of(Event::Kind::Changed).at(0);
        QCOMPARE(changed.type, mc::DeviceType::D);
        QCOMPARE(changed.round, 2u);
        QCOMPARE(changed.changes.size(), 1);
        QCOMPARE(changed.changes.at(0).device.number, 101u);
        QCOMPARE(changed.changes.at(0).oldValue, uint16_t{20});
        QCOMPARE(changed.changes.at(0).newValue, uint16_t{999});

        // The snapshot that follows already holds the new value.
        const Event snapshotD = rig.recorder->of(Event::Kind::Snapshot).at(3);
        QCOMPARE(snapshotD.snapshot.round, 2u);
        QCOMPARE(snapshotD.snapshot.segments.at(0).values, wordsLe({10, 999, 30, 40}));
    }

    void QDV_03_writeReadWordsAndBits() {
        Rig rig;
        QVERIFY(rig.ok());
        rig.device->connectToPlc();
        QTRY_COMPARE_WITH_TIMEOUT(rig.device->linkState(), mc::LinkState::Connected, kWaitMs);

        const auto finishedFor = [&rig](mc::RequestId id) -> const Event* {
            for (const Event& e : rig.recorder->events) {
                if (e.kind == Event::Kind::Finished && e.id == id) {
                    return &e;
                }
            }
            return nullptr;
        };

        // Write words: requestFinished(ok) and the mock's memory changed.
        const mc::Expected<mc::RequestId> w = rig.device->writeWords(u"D200", {7, 8, 9});
        QVERIFY(w);
        QTRY_VERIFY_WITH_TIMEOUT(finishedFor(w.value()) != nullptr, kWaitMs);
        QVERIFY(finishedFor(w.value())->error.ok());
        QVERIFY(finishedFor(w.value())->payload.isEmpty());
        QCOMPARE(rig.server.plc()->word(dev("D200")), uint16_t{7});
        QCOMPARE(rig.server.plc()->word(dev("D202")), uint16_t{9});

        // Write bits.
        const mc::Expected<mc::RequestId> wb = rig.device->writeBits(u"M100", {true, false, true});
        QVERIFY(wb);
        QTRY_VERIFY_WITH_TIMEOUT(finishedFor(wb.value()) != nullptr, kWaitMs);
        QVERIFY(finishedFor(wb.value())->error.ok());
        QVERIFY(rig.server.plc()->bit(dev("M100")));
        QVERIFY(!rig.server.plc()->bit(dev("M101")));
        QVERIFY(rig.server.plc()->bit(dev("M102")));

        // Read words: the payload is normalized (two bytes little-endian per word).
        const mc::Expected<mc::RequestId> rw = rig.device->readWords(u"D100", 4);
        QVERIFY(rw);
        QTRY_VERIFY_WITH_TIMEOUT(finishedFor(rw.value()) != nullptr, kWaitMs);
        QVERIFY(finishedFor(rw.value())->error.ok());
        QCOMPARE(finishedFor(rw.value())->payload, wordsLe({10, 20, 30, 40}));

        // Read bits: one byte per point.
        const mc::Expected<mc::RequestId> rb = rig.device->readBits(u"M0", 8);
        QVERIFY(rb);
        QTRY_VERIFY_WITH_TIMEOUT(finishedFor(rb.value()) != nullptr, kWaitMs);
        QCOMPARE(finishedFor(rb.value())->payload, QByteArray::fromHex("0000000100000000"));

        // Every id finished exactly once.
        QCOMPARE(rig.recorder->count(Event::Kind::Finished), 4);
    }

    void QDV_03_badArgumentsAreRefusedWithoutAnId() {
        Rig rig;
        QVERIFY(rig.ok());
        // Not connected: the Session refuses with LinkDown, no id, no signal.
        const mc::Expected<mc::RequestId> down = rig.device->writeWords(u"D200", {1});
        QVERIFY(!down);
        QCOMPARE(down.error().code, mc::ErrorCode::LinkDown);
        // A text that is not a device.
        const mc::Expected<mc::RequestId> bad = rig.device->readWords(u"Q10", 1);
        QVERIFY(!bad);
        QCOMPARE(bad.error().code, mc::ErrorCode::InvalidDevice);
        QCOMPARE(rig.recorder->count(Event::Kind::Finished), 0);
    }

    // XYN: text device arguments are read in frame.xyNotation, and the frame carries the
    // X/Y digits as frame.xyAsciiDigits says (3E ASCII, octal both: an FX5 with "ASCII (X,Y OCT)").
    void XYN_40_textDevicesAndFrameDigitsFollowTheFrameConfig() {
        mc::FrameConfig frame = mc::FrameConfig::frame3E(mc::DataCode::Ascii);
        frame.xyNotation = mc::XyNumbering::Octal;
        frame.xyAsciiDigits = mc::XyNumbering::Octal;
        Rig rig(frame, /*subscribe=*/false);
        QVERIFY(rig.ok());
        rig.device->connectToPlc();
        QTRY_COMPARE_WITH_TIMEOUT(rig.device->linkState(), mc::LinkState::Connected, kWaitMs);

        const auto finishedFor = [&rig](mc::RequestId id) -> const Event* {
            for (const Event& e : rig.recorder->events) {
                if (e.kind == Event::Kind::Finished && e.id == id) {
                    return &e;
                }
            }
            return nullptr;
        };

        // X10 octal is index 8; the mock reads the octal digits "000010" of the frame.
        const mc::Expected<mc::RequestId> wb = rig.device->writeBits(u"X10", {true, false, true});
        QVERIFY(wb);
        QTRY_VERIFY_WITH_TIMEOUT(finishedFor(wb.value()) != nullptr, kWaitMs);
        QVERIFY(finishedFor(wb.value())->error.ok());
        QVERIFY(rig.server.plc()->bit(mc::Device{mc::DeviceType::X, 8}));
        QVERIFY(!rig.server.plc()->bit(mc::Device{mc::DeviceType::X, 9}));
        QVERIFY(rig.server.plc()->bit(mc::Device{mc::DeviceType::X, 10}));
        QVERIFY(!rig.server.plc()->bit(mc::Device{mc::DeviceType::X, 16}));

        const mc::Expected<mc::RequestId> rb = rig.device->readBits(u"X10", 3);
        QVERIFY(rb);
        QTRY_VERIFY_WITH_TIMEOUT(finishedFor(rb.value()) != nullptr, kWaitMs);
        QCOMPARE(finishedFor(rb.value())->payload, QByteArray::fromHex("010001"));

        // A digit 8 is no octal number: refused before anything is sent.
        const mc::Expected<mc::RequestId> bad = rig.device->readBits(u"X18", 1);
        QVERIFY(!bad);
        QCOMPARE(bad.error().code, mc::ErrorCode::InvalidDevice);
        QVERIFY(!rig.device->subscribe(u"Y9", 1));
        QVERIFY(rig.device->subscribe(u"Y10", 8));
    }

    // XYN: a subscription of the config is parsed in frame.xyNotation when the device is built:
    // "X10" under Octal is index 8, and the mock's bit 8 (not bit 16) shows in the snapshot.
    void XYN_41_configSubscriptionsAreReadInTheConfiguredNotation() {
        mc::FrameConfig frame = mc::FrameConfig::frame3E(mc::DataCode::Ascii);
        frame.xyNotation = mc::XyNumbering::Octal;
        frame.xyAsciiDigits = mc::XyNumbering::Octal;
        Rig rig(frame, /*subscribe=*/false, /*mute=*/false, [](mc::McDeviceConfig& cfg) {
            cfg.subscriptions = {{QStringLiteral("X10"), 8}};
        });
        QVERIFY(rig.ok());
        QVERIFY(rig.device->configStatus());
        rig.server.setInit([](mc::MockPlc& plc) {
            plc.setBit(mc::Device{mc::DeviceType::X, 8}, true);  // X10 octal
            plc.setBit(mc::Device{mc::DeviceType::X, 16}, true); // X20 octal, and X10 hex
        });
        rig.device->connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(rig.recorder->hasCycle(1), kWaitMs);

        const QVector<Event> snapshots = rig.recorder->of(Event::Kind::Snapshot);
        QVERIFY(!snapshots.isEmpty()); // round 1 (a later round may already have run)
        QCOMPARE(snapshots.at(0).snapshot.type, mc::DeviceType::X);
        QCOMPARE(snapshots.at(0).snapshot.round, 1u);
        const mc::SnapshotSegment& seg = snapshots.at(0).snapshot.segments.at(0);
        QCOMPARE(seg.head.number, 8u);
        QCOMPARE(seg.count, 8u);
        QCOMPARE(int(seg.values.at(0)), 1); // index 8
        QCOMPARE(int(seg.values.at(1)), 0);
    }

    void QDV_04_serverClosesWithRequestsInFlightAndQueued() {
        Rig rig(mc::FrameConfig::frame3E(), /*subscribe=*/false, /*mute=*/true);
        QVERIFY(rig.ok());
        rig.device->connectToPlc();
        QTRY_COMPARE_WITH_TIMEOUT(rig.device->linkState(), mc::LinkState::Connected, kWaitMs);

        // The mock is muted: the first write goes out and is never answered (in flight), the
        // second waits in the queue behind it.
        const mc::Expected<mc::RequestId> first = rig.device->writeWords(u"D0", {1});
        const mc::Expected<mc::RequestId> second = rig.device->writeWords(u"D1", {2});
        QVERIFY(first && second);
        QTRY_VERIFY_WITH_TIMEOUT(rig.server.plc() != nullptr && !rig.server.plc()->requests().empty(), kWaitMs);

        rig.server.closeClient();
        QTRY_COMPARE_WITH_TIMEOUT(rig.device->linkState(), mc::LinkState::Disconnected, kWaitMs);

        // One requestFinished(LinkDown) per id, in submission order, then the state.
        const QStringList tail = withoutCycles(rig.recorder->traceAfterConnect());
        QCOMPARE(tail, (QStringList{QStringLiteral("finished(%1,LinkDown)").arg(first.value()),
                                    QStringLiteral("finished(%1,LinkDown)").arg(second.value()),
                                    "link(Disconnected,PeerClosed)"}));
    }

    void QDV_07_connectWhileConnectedRepublishesAndOpensNoSecondSocket() {
        Rig rig;
        QVERIFY(rig.ok());
        rig.device->connectToPlc();
        QTRY_COMPARE_WITH_TIMEOUT(rig.device->linkState(), mc::LinkState::Connected, kWaitMs);
        const int before = rig.recorder->count(Event::Kind::Link);

        rig.device->connectToPlc();
        QCOMPARE(rig.recorder->count(Event::Kind::Link), before + 1);
        const Event last = rig.recorder->of(Event::Kind::Link).last();
        QCOMPARE(last.state, mc::LinkState::Connected);
        QCOMPARE(last.reason, mc::LinkReason::Requested);

        // The device keeps polling on the same socket.
        QTRY_VERIFY_WITH_TIMEOUT(rig.recorder->hasCycle(3), kWaitMs);
        QCOMPARE(rig.server.connectionCount(), 1);
        QCOMPARE(rig.server.openConnectionCount(), 1);
    }

    void QDV_15_disconnectWithRequestInFlight() {
        Rig rig(mc::FrameConfig::frame3E(), /*subscribe=*/false, /*mute=*/true);
        QVERIFY(rig.ok());
        rig.device->connectToPlc();
        QTRY_COMPARE_WITH_TIMEOUT(rig.device->linkState(), mc::LinkState::Connected, kWaitMs);
        const mc::Expected<mc::RequestId> id = rig.device->writeWords(u"D0", {1});
        QVERIFY(id);
        QTRY_VERIFY_WITH_TIMEOUT(rig.server.plc() != nullptr && !rig.server.plc()->requests().empty(), kWaitMs);

        rig.device->disconnectFromPlc();
        QCOMPARE(rig.device->linkState(), mc::LinkState::Disconnected);

        const QStringList tail = withoutCycles(rig.recorder->traceAfterConnect());
        QCOMPARE(tail, (QStringList{QStringLiteral("finished(%1,LinkDown)").arg(id.value()),
                                    "link(Disconnected,Requested)"}));
        // The socket is closed: the server sees it.
        QTRY_COMPARE_WITH_TIMEOUT(rig.server.openConnectionCount(), 0, kWaitMs);
    }

    void PMP_01_connectPublishesAStateFromEveryStartingState() {
        Rig rig;
        QVERIFY(rig.ok());
        const auto linkCount = [&rig]() { return rig.recorder->count(Event::Kind::Link); };

        // Disconnected: Connecting is published before the call returns.
        rig.device->connectToPlc();
        QCOMPARE(linkCount(), 1);
        QCOMPARE(rig.recorder->of(Event::Kind::Link).last().state, mc::LinkState::Connecting);

        // Connecting: no second open, but the state is published again.
        rig.device->connectToPlc();
        QCOMPARE(linkCount(), 2);
        QCOMPARE(rig.recorder->of(Event::Kind::Link).last().state, mc::LinkState::Connecting);
        QCOMPARE(rig.recorder->of(Event::Kind::Link).last().reason, mc::LinkReason::Requested);

        // Connected: republished.
        QTRY_COMPARE_WITH_TIMEOUT(rig.device->linkState(), mc::LinkState::Connected, kWaitMs);
        const int connected = linkCount();
        rig.device->connectToPlc();
        QCOMPARE(linkCount(), connected + 1);
        QCOMPARE(rig.recorder->of(Event::Kind::Link).last().state, mc::LinkState::Connected);

        QTRY_VERIFY_WITH_TIMEOUT(rig.recorder->hasCycle(1), kWaitMs);
        QCOMPARE(rig.server.connectionCount(), 1);
    }

    void PMP_02_nestedCallFromADirectSlotEmitsAfterTheQueuedSignals() {
        // With no subscription the round ends at once, so opened() queues two signals:
        // link(Connected) and cycle(1). The slot on link(Connected) submits a write and then
        // disconnects. Its nested signals (the write's LinkDown completion and the Disconnected
        // state) must come after cycle(1), which was queued before the slot ran.
        Rig rig(mc::FrameConfig::frame3E(), /*subscribe=*/false);
        QVERIFY(rig.ok());
        mc::RequestId nestedId = 0;
        bool nestedSubmitOk = false;
        qsizetype emittedInsideSlot = -1; // signals emitted while the slot was inside the device
        QObject::connect(rig.device.get(), &mc::McDevice::linkStateChanged, rig.device.get(),
                         [&](mc::LinkState state, mc::LinkReason, const QString&) {
                             if (state != mc::LinkState::Connected) {
                                 return;
                             }
                             const qsizetype before = rig.recorder->events.size();
                             const mc::Expected<mc::RequestId> id =
                                 rig.device->writeWords(u"D0", {1});
                             nestedSubmitOk = static_cast<bool>(id);
                             if (id) {
                                 nestedId = id.value();
                             }
                             rig.device->disconnectFromPlc();
                             emittedInsideSlot = rig.recorder->events.size() - before;
                         });
        rig.device->connectToPlc();
        QTRY_COMPARE_WITH_TIMEOUT(rig.device->linkState(), mc::LinkState::Disconnected, kWaitMs);
        QVERIFY(nestedSubmitOk);
        // A nested call only queues: nothing was emitted from inside the slot; the outermost
        // call flushes after the slot returned.
        QCOMPARE(emittedInsideSlot, qsizetype(0));

        QCOMPARE(rig.recorder->trace(),
                 (QStringList{"link(Connecting,Requested)", "link(Connected,Requested)",
                              "cycle(1)", QStringLiteral("finished(%1,LinkDown)").arg(nestedId),
                              "link(Disconnected,Requested)"}));
    }

    void QDV_05_muteFaultsAndOnlyTheApplicationReconnects() {
        Rig rig(mc::FrameConfig::frame3E(), /*subscribe=*/true, /*mute=*/false,
                [](mc::McDeviceConfig& c) { c.frame.timeoutMs = 300; });
        QVERIFY(rig.ok());
        rig.device->connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(rig.recorder->hasCycle(1), kWaitMs);
        QSignalSpy accepted(&rig.server, &MockPlcServer::clientConnected);
        QSignalSpy closed(&rig.server, &MockPlcServer::clientDisconnected);

        rig.server.mute(true);
        QTRY_COMPARE_WITH_TIMEOUT(rig.device->linkState(), mc::LinkState::Faulted, kWaitMs);
        const QStringList atFault = rig.recorder->trace();
        QCOMPARE(atFault.mid(atFault.size() - 2),
                 (QStringList{"fault(Timeout,reopen=1)", "link(Faulted,Fault)"}));
        const Event fault = rig.recorder->of(Event::Kind::Fault).last();
        QCOMPARE(fault.fault.error.code, mc::ErrorCode::Timeout);
        QVERIFY(fault.fault.reopenTransport);

        // No reconnect of any kind on its own: three timeouts pass with one connection, the
        // transport stays open and nothing more is emitted.
        QVERIFY(!accepted.wait(900));
        QCOMPARE(rig.device->linkState(), mc::LinkState::Faulted);
        QCOMPARE(rig.server.connectionCount(), 1);
        QCOMPARE(closed.count(), 0);
        QCOMPARE(rig.recorder->trace(), atFault);

        // The application decides: connectToPlc() reconnects and round 1 repeats.
        rig.server.mute(false);
        const qsizetype mark = rig.recorder->events.size();
        rig.device->connectToPlc();
        QCOMPARE(rig.device->linkState(), mc::LinkState::Connecting);
        QTRY_VERIFY_WITH_TIMEOUT(rig.recorder->trace().mid(mark).contains("cycle(1)"), kWaitMs);
        const QStringList again{"link(Connecting,Requested)", "link(Connected,Requested)",
                                "snapshot(M,1)", "snapshot(D,1)", "cycle(1)"};
        QCOMPARE(rig.recorder->trace().mid(mark, again.size()), again);
        for (const Event& e : rig.recorder->of(Event::Kind::Changed)) {
            QVERIFY2(e.round >= 2, "valuesChanged in the repeated round 1");
        }
        QCOMPARE(rig.server.connectionCount(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(rig.server.openConnectionCount(), 1, kWaitMs);
    }

    void QDV_06_openToAPortNobodyListensOnFailsWithinTheTimeout() {
        const quint16 port = unusedPort();
        QVERIFY(port != 0);
        mc::McDeviceConfig cfg = configFor(port);
        cfg.tcp.connectTimeoutMs = 1000;
        mc::McDevice device(cfg);
        DeviceRecorder recorder(device, nullptr);
        QElapsedTimer clock;
        qint64 failedAtMs = -1;
        QObject::connect(&device, &mc::McDevice::linkStateChanged, &device,
                         [&](mc::LinkState state, mc::LinkReason, const QString&) {
                             if (state == mc::LinkState::Disconnected) {
                                 failedAtMs = clock.elapsed();
                             }
                         });

        clock.start();
        device.connectToPlc();
        QCOMPARE(device.linkState(), mc::LinkState::Connecting);
        QTRY_VERIFY_WITH_TIMEOUT(failedAtMs >= 0, cfg.tcp.connectTimeoutMs + 500);
        QVERIFY2(failedAtMs <= cfg.tcp.connectTimeoutMs + 500, qPrintable(QString::number(failedAtMs)));
        QCOMPARE(device.linkState(), mc::LinkState::Disconnected);
        QCOMPARE(recorder.trace(), (QStringList{"link(Connecting,Requested)",
                                                "link(Disconnected,OpenFailed)"}));
        QVERIFY(!recorder.events.last().detail.isEmpty());

        // Decision 7: no retry of any kind follows.
        QSignalSpy more(&device, &mc::McDevice::linkStateChanged);
        QVERIFY(!more.wait(300));
    }

    void QDV_08_slotCallingBackIntoTheDeviceKeepsSignalsFifo() {
        Rig rig;
        QVERIFY(rig.ok());
        QObject::connect(rig.device.get(), &mc::McDevice::cycleDone, rig.device.get(),
                         [&rig](const mc::CycleInfo& c) {
                             if (c.round == 1) {
                                 rig.server.plc()->setWord(dev("D101"), 999);
                             }
                         });
        // The first valuesChanged (round 2) makes the slot call back into the device three ways.
        bool calledBack = false;
        qsizetype emittedInsideSlot = -1;
        mc::RequestId requestId = 0;
        mc::SubscriptionId subscriptionId = 0;
        QObject::connect(
            rig.device.get(), &mc::McDevice::valuesChanged, rig.device.get(),
            [&](mc::DeviceType, quint32, const QVector<mc::Change>&) {
                if (calledBack) {
                    return;
                }
                calledBack = true;
                const qsizetype before = rig.recorder->events.size();
                const mc::Expected<mc::RequestId> request = rig.device->readWords(u"D100", 4);
                const mc::Expected<mc::SubscriptionId> subscription =
                    rig.device->subscribe(u"D200", 2);
                rig.device->connectToPlc(); // republishes Connected
                emittedInsideSlot = rig.recorder->events.size() - before;
                requestId = request ? request.value() : 0;
                subscriptionId = subscription ? subscription.value() : 0;
            });
        rig.device->connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(rig.recorder->count(Event::Kind::Finished) == 1, kWaitMs);

        QVERIFY(calledBack);
        QVERIFY(requestId != 0);
        QVERIFY(subscriptionId != 0);
        // Nested calls only queue; their signals follow the ones that were already queued.
        QCOMPARE(emittedInsideSlot, qsizetype(0));
        const QStringList trace = rig.recorder->trace();
        const int at = static_cast<int>(trace.indexOf("changed(D,2)"));
        QVERIFY(at >= 0);
        QCOMPARE(trace.mid(at, 4), (QStringList{"changed(D,2)", "snapshot(D,2)", "cycle(2)",
                                                "link(Connected,Requested)"}));

        // The request completes with the memory as changed before round 2.
        const Event done = rig.recorder->of(Event::Kind::Finished).at(0);
        QCOMPARE(done.id, requestId);
        QVERIFY(done.error.ok());
        QCOMPARE(done.payload, wordsLe({10, 999, 30, 40}));

        // The subscription takes effect at a round boundary: a later D snapshot has two
        // segments, D100 and D200.
        const auto hasTwoSegments = [&rig]() {
            for (const Event& e : rig.recorder->of(Event::Kind::Snapshot)) {
                if (e.type == mc::DeviceType::D && e.round > 2 && e.snapshot.segments.size() == 2) {
                    return e.snapshot.segments.at(1).head.number == 200u &&
                           e.snapshot.segments.at(1).count == 2u;
                }
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(hasTwoSegments(), kWaitMs);

        // And it can be removed again; a second removal reports NotSubscribed.
        QVERIFY(rig.device->unsubscribe(subscriptionId));
        const qsizetype mark = rig.recorder->events.size();
        const auto backToOne = [&rig, mark]() {
            for (qsizetype i = mark; i < rig.recorder->events.size(); ++i) {
                const Event& e = rig.recorder->events.at(i);
                if (e.kind == Event::Kind::Snapshot && e.type == mc::DeviceType::D &&
                    e.snapshot.segments.size() == 1) {
                    return true;
                }
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(backToOne(), kWaitMs);
        const mc::Expected<void> again = rig.device->unsubscribe(subscriptionId);
        QVERIFY(!again);
        QCOMPARE(again.error().code, mc::ErrorCode::NotSubscribed);
    }

    void QDV_11_timeoutFaultArrivesWithinTheTimeoutPlusMargin() {
        Rig rig(mc::FrameConfig::frame3E(), /*subscribe=*/false, /*mute=*/true,
                [](mc::McDeviceConfig& c) { c.frame.timeoutMs = 400; });
        QVERIFY(rig.ok());
        rig.device->connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(rig.recorder->hasCycle(1), kWaitMs);

        QElapsedTimer clock;
        qint64 faultAtMs = -1;
        QObject::connect(rig.device.get(), &mc::McDevice::linkFault, rig.device.get(),
                         [&](const mc::LinkFaultInfo&) { faultAtMs = clock.elapsed(); });
        const qint64 timeoutMs = rig.device->config().frame.effectiveTimeoutMs();
        QCOMPARE(timeoutMs, qint64(400));

        clock.start();
        const mc::Expected<mc::RequestId> id = rig.device->writeWords(u"D0", {1}); // sent at once
        QVERIFY(id);
        QTRY_VERIFY_WITH_TIMEOUT(faultAtMs >= 0, kWaitMs);

        // Spec: within effectiveTimeoutMs() + 100 ms of the send. The lower bound (allowing the
        // millisecond truncation of the two clocks) shows the timer is not early either.
        QVERIFY2(faultAtMs <= timeoutMs + 100, qPrintable(QString::number(faultAtMs)));
        QVERIFY2(faultAtMs >= timeoutMs - 2, qPrintable(QString::number(faultAtMs)));

        // The request fails with Timeout first, then the fault, then the state.
        const QStringList tail = withoutCycles(rig.recorder->traceAfterConnect());
        QCOMPARE(tail, (QStringList{QStringLiteral("finished(%1,Timeout)").arg(id.value()),
                                    "fault(Timeout,reopen=1)", "link(Faulted,Fault)"}));
    }

    void QDV_18_firstResponseAfterConnectWaitsForTheGraceNotTheTimeout() {
        // The server answers each connection's first request only after 1500 ms; the frame
        // timeout is 500 ms.
        const auto withGrace = [](uint32_t graceMs) {
            return [graceMs](mc::McDeviceConfig& c) {
                c.frame.timeoutMs = 500;
                c.session.firstResponseTimeoutMs = graceMs;
                c.session.cycleIntervalMs = 60000; // one round: nothing else is polled
            };
        };

        {
            Rig rig(mc::FrameConfig::frame3E(), /*subscribe=*/true, /*mute=*/false,
                    withGrace(3000));
            QVERIFY(rig.ok());
            rig.server.holdFirstRequest(1500);
            QElapsedTimer clock;
            clock.start();
            rig.device->connectToPlc();
            QTRY_VERIFY_WITH_TIMEOUT(rig.recorder->hasCycle(1), kWaitMs);
            QVERIFY2(clock.elapsed() >= 1400, qPrintable(QString::number(clock.elapsed())));
            QCOMPARE(rig.device->linkState(), mc::LinkState::Connected);
            QCOMPARE(rig.recorder->of(Event::Kind::Fault).size(), qsizetype(0));
            QCOMPARE(rig.server.connectionCount(), 1);

            // Only the first frame of a connection gets the grace: a later one that the PLC
            // never answers faults after the frame timeout.
            rig.server.mute(true);
            qint64 faultAtMs = -1;
            QElapsedTimer sent;
            QObject::connect(rig.device.get(), &mc::McDevice::linkFault, rig.device.get(),
                             [&](const mc::LinkFaultInfo&) { faultAtMs = sent.elapsed(); });
            sent.start();
            QVERIFY(rig.device->writeWords(u"D0", {1}));
            QTRY_VERIFY_WITH_TIMEOUT(faultAtMs >= 0, kWaitMs);
            QVERIFY2(faultAtMs <= 500 + 100, qPrintable(QString::number(faultAtMs)));
            QVERIFY2(faultAtMs >= 500 - 2, qPrintable(QString::number(faultAtMs)));
            QCOMPARE(rig.recorder->of(Event::Kind::Fault).last().fault.error.code,
                     mc::ErrorCode::Timeout);

            // connectToPlc() again: the grace applies to the new connection.
            rig.server.mute(false);
            const qsizetype mark = rig.recorder->events.size();
            QElapsedTimer again;
            again.start();
            rig.device->connectToPlc();
            QTRY_VERIFY_WITH_TIMEOUT(rig.recorder->trace().mid(mark).contains("cycle(1)"), kWaitMs);
            QVERIFY2(again.elapsed() >= 1400, qPrintable(QString::number(again.elapsed())));
            QCOMPARE(rig.device->linkState(), mc::LinkState::Connected);
            QCOMPARE(rig.server.connectionCount(), 2);
        }

        {
            // Grace off: the first request times out like any other, at about 500 ms.
            Rig rig(mc::FrameConfig::frame3E(), /*subscribe=*/true, /*mute=*/false, withGrace(0));
            QVERIFY(rig.ok());
            rig.server.holdFirstRequest(1500);
            QElapsedTimer clock;
            qint64 faultAtMs = -1;
            QObject::connect(rig.device.get(), &mc::McDevice::linkFault, rig.device.get(),
                             [&](const mc::LinkFaultInfo&) { faultAtMs = clock.elapsed(); });
            clock.start();
            rig.device->connectToPlc();
            QTRY_VERIFY_WITH_TIMEOUT(faultAtMs >= 0, kWaitMs);
            QVERIFY2(faultAtMs >= 450 && faultAtMs < 1400, qPrintable(QString::number(faultAtMs)));
            QCOMPARE(rig.recorder->of(Event::Kind::Fault).last().fault.error.code,
                     mc::ErrorCode::Timeout);
            QCOMPARE(rig.device->linkState(), mc::LinkState::Faulted);
        }
    }

    void QDV_13_threeEAsciiSmoke() { smokeOverTcp(mc::FrameConfig::frame3E(mc::DataCode::Ascii)); }

    void QDV_13_oneEBinarySmoke() { smokeOverTcp(mc::FrameConfig::frame1E(mc::DataCode::Binary)); }

    void QDV_13_oneEAsciiSmoke() { smokeOverTcp(mc::FrameConfig::frame1E(mc::DataCode::Ascii)); }

    // A serial frame over a TCP serial-device server: the frame, not the transport, decides.
    void QDV_13_threeCFormat1Smoke()
    {
        smokeOverTcp(mc::FrameConfig::frame3C(mc::SerialFormat::Format1));
    }

    void QDV_16_invalidConfigPublishesOpenFailedNamingTheFieldAndOpensNoSocket() {
        MockPlcServer server;
        QVERIFY(server.listen());
        mc::McDeviceConfig cfg = configFor(server.port());
        cfg.subscriptions = {{QStringLiteral("Q10"), 1}};
        mc::McDevice device(cfg);
        DeviceRecorder recorder(device);
        QVERIFY(!device.configStatus());
        QCOMPARE(device.configStatus().error().code, mc::ErrorCode::InvalidDevice);

        device.connectToPlc();
        QCOMPARE(device.linkState(), mc::LinkState::Disconnected);
        QCOMPARE(recorder.trace(), QStringList{"link(Disconnected,OpenFailed)"});
        QVERIFY2(recorder.events.at(0).detail.contains(QStringLiteral("subscriptions[0].device")),
                 qPrintable(recorder.events.at(0).detail));

        // No transport was even built, and the server saw no connection.
        QCOMPARE(device.findChildren<mc::Transport*>().size(), 0);
        QSignalSpy accepted(&server, &MockPlcServer::clientConnected);
        QVERIFY(!accepted.wait(200));

        // The unusable device refuses calls with the config error instead of crashing.
        QVERIFY(!device.subscribe(u"D0", 1));
        QVERIFY(!device.writeWords(u"D0", {1}));
        QVERIFY(!device.unsubscribe(1));
        QCOMPARE(device.values().segmentCount(mc::DeviceType::D), size_t{0});
    }

    void QDV_17_destroyingAConnectedDeviceWithQueuedRequests() {
        Rig rig(mc::FrameConfig::frame3E(), /*subscribe=*/false, /*mute=*/true);
        QVERIFY(rig.ok());
        CaptureSink sink;
        rig.device->setLogSink(&sink);
        rig.device->connectToPlc();
        QTRY_COMPARE_WITH_TIMEOUT(rig.device->linkState(), mc::LinkState::Connected, kWaitMs);
        const mc::Expected<mc::RequestId> first = rig.device->writeWords(u"D0", {1});
        const mc::Expected<mc::RequestId> second = rig.device->writeWords(u"D1", {2});
        QVERIFY(first && second);
        QTRY_VERIFY_WITH_TIMEOUT(rig.server.plc() != nullptr && !rig.server.plc()->requests().empty(),
                                 kWaitMs);

        // A counter outside the device sees every signal emitted from now on.
        QObject counter;
        int emitted = 0;
        mc::McDevice* device = rig.device.get();
        QObject::connect(device, &mc::McDevice::linkStateChanged, &counter,
                         [&emitted](mc::LinkState, mc::LinkReason, const QString&) { ++emitted; });
        QObject::connect(device, &mc::McDevice::linkFault, &counter,
                         [&emitted](const mc::LinkFaultInfo&) { ++emitted; });
        QObject::connect(device, &mc::McDevice::valuesChanged, &counter,
                         [&emitted](mc::DeviceType, quint32, const QVector<mc::Change>&) { ++emitted; });
        QObject::connect(device, &mc::McDevice::snapshotReady, &counter,
                         [&emitted](const mc::DeviceSnapshot&) { ++emitted; });
        QObject::connect(device, &mc::McDevice::cycleDone, &counter,
                         [&emitted](const mc::CycleInfo&) { ++emitted; });
        QObject::connect(device, &mc::McDevice::requestFinished, &counter,
                         [&emitted](mc::RequestId, const mc::Error&, const QByteArray&) { ++emitted; });

        rig.device.reset(); // destroyed with two requests outstanding
        QCOMPARE(emitted, 0);

        // One Warn line names both ids.
        const auto warnings = sink.of(mc::LogLevel::Warn, QStringLiteral("mc.device"));
        QCOMPARE(warnings.size(), 1);
        const QString message = warnings.at(0).message;
        QVERIFY2(message.contains(QStringLiteral("2 outstanding")), qPrintable(message));
        QVERIFY2(message.contains(QStringLiteral("ids: %1, %2").arg(first.value()).arg(second.value())),
                 qPrintable(message));

        // The server sees the socket close, and still nothing was emitted.
        QTRY_COMPARE_WITH_TIMEOUT(rig.server.openConnectionCount(), 0, kWaitMs);
        QCOMPARE(emitted, 0);
    }

    void QDV_17_destroyingAfterDisconnectLogsNothing() {
        Rig rig(mc::FrameConfig::frame3E(), /*subscribe=*/false, /*mute=*/true);
        QVERIFY(rig.ok());
        CaptureSink sink;
        rig.device->setLogSink(&sink);
        rig.device->connectToPlc();
        QTRY_COMPARE_WITH_TIMEOUT(rig.device->linkState(), mc::LinkState::Connected, kWaitMs);
        QVERIFY(rig.device->writeWords(u"D0", {1}));
        rig.device->disconnectFromPlc();
        rig.device.reset();
        QCOMPARE(sink.of(mc::LogLevel::Warn, QStringLiteral("mc.device")).size(), 0);
    }

    void FLT_01_failedWriteStopsTheSessionAndReportsTransportError() {
        FakeRig rig;
        rig.device->connectToPlc();
        QTRY_COMPARE_WITH_TIMEOUT(rig.device->linkState(), mc::LinkState::Connected, kWaitMs);
        QTRY_VERIFY_WITH_TIMEOUT(rig.recorder->hasCycle(1), kWaitMs);
        rig.transport->failWrites = true;

        // The frame cannot be written: the device stops the Session at once, so the request
        // completes with LinkDown; the transport then reports the loss from the event loop.
        const mc::Expected<mc::RequestId> id = rig.device->writeWords(u"D0", {1});
        QVERIFY(id);
        // The completion is already out before the call returns; the state follows with lost().
        QCOMPARE(rig.recorder->count(Event::Kind::Finished), 1);
        QCOMPARE(rig.device->linkState(), mc::LinkState::Connected);
        QTRY_COMPARE_WITH_TIMEOUT(rig.device->linkState(), mc::LinkState::Disconnected, kWaitMs);
        QCOMPARE(withoutCycles(rig.recorder->traceAfterConnect()),
                 (QStringList{QStringLiteral("finished(%1,LinkDown)").arg(id.value()),
                              "link(Disconnected,TransportError)"}));
        QCOMPARE(rig.recorder->of(Event::Kind::Link).last().detail, QStringLiteral("fake: write failed"));
    }

    void FLT_02_bytesWhileIdleFaultTheLinkAndTheTransportStaysOpen() {
        FakeRig rig;
        rig.device->connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(rig.recorder->hasCycle(1), kWaitMs);
        rig.transport->deliver(QByteArray("X")); // nothing was sent: an Ethernet link is desynced

        QTRY_COMPARE_WITH_TIMEOUT(rig.device->linkState(), mc::LinkState::Faulted, kWaitMs);
        const QStringList trace = rig.recorder->trace();
        QCOMPARE(trace.mid(trace.size() - 2),
                 (QStringList{"fault(ProtocolError,reopen=1)", "link(Faulted,Fault)"}));
        QCOMPARE(rig.transport->closeCalls, 0);
        QCOMPARE(rig.transport->state(), mc::Transport::State::Open);

        // A loss while Faulted ends in Disconnected with the cause the transport reports.
        rig.transport->simulateLost(QStringLiteral("gone"), /*byPeer=*/true);
        QTRY_COMPARE_WITH_TIMEOUT(rig.device->linkState(), mc::LinkState::Disconnected, kWaitMs);
        QCOMPARE(rig.recorder->of(Event::Kind::Link).last().reason, mc::LinkReason::PeerClosed);
        QCOMPARE(rig.recorder->of(Event::Kind::Link).last().detail, QStringLiteral("gone"));

        // The device does not reopen on its own; the application can.
        QCOMPARE(rig.transport->openCalls, 1);
        rig.device->connectToPlc();
        QTRY_COMPARE_WITH_TIMEOUT(rig.device->linkState(), mc::LinkState::Connected, kWaitMs);
        QCOMPARE(rig.transport->openCalls, 2);
    }

    void FLT_03_connectFromFaultedClosesAndReopens() {
        FakeRig rig;
        rig.device->connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(rig.recorder->hasCycle(1), kWaitMs);
        rig.transport->deliver(QByteArray("X"));
        QTRY_COMPARE_WITH_TIMEOUT(rig.device->linkState(), mc::LinkState::Faulted, kWaitMs);

        const qsizetype mark = rig.recorder->events.size();
        rig.device->connectToPlc();
        QCOMPARE(rig.device->linkState(), mc::LinkState::Connecting);
        QCOMPARE(rig.transport->closeCalls, 1);
        QCOMPARE(rig.transport->openCalls, 2);
        QTRY_VERIFY_WITH_TIMEOUT(rig.recorder->trace().mid(mark).contains("cycle(1)"), kWaitMs);
        QCOMPARE(rig.recorder->trace().mid(mark, 3),
                 (QStringList{"link(Connecting,Requested)", "link(Connected,Requested)", "cycle(1)"}));
    }

    void FLT_04_transportOpenFailurePublishesOpenFailedWithItsReason() {
        FakeRig rig(/*failOpen=*/true);
        rig.device->connectToPlc();
        QTRY_COMPARE_WITH_TIMEOUT(rig.device->linkState(), mc::LinkState::Disconnected, kWaitMs);
        QCOMPARE(rig.recorder->trace(), (QStringList{"link(Connecting,Requested)",
                                                     "link(Disconnected,OpenFailed)"}));
        QCOMPARE(rig.recorder->events.last().detail, QStringLiteral("fake: open refused"));
    }

    // A serial line that keeps sending bytes during Flushing ends the flush at its cap anyway: the
    // device's deadline timer is armed from Session::nextDeadline() and does not wait for a quiet
    // moment. The silent fake transport times out the first request at 200 ms; from the EOT on, it
    // delivers a byte every 10 ms (never the 50 ms of silence that would end the flush), so the
    // only way out is the cap: a second link error at 200 + 200 ms, which reaches the limit of 2.
    void SER_FLUSH_01_aChatteringLineStillEndsTheFlushAtItsCapThroughTheDeadlineTimer() {
        auto fake = std::make_unique<FakeTransport>();
        FakeTransport* transport = fake.get();
        mc::McDeviceConfig cfg =
            configFor(0, mc::FrameConfig::frame3C(mc::SerialFormat::Format1), false);
        cfg.frame.timeoutMs = 200;
        cfg.frame.readRetries = 0;
        cfg.session.serialFlushMs = 50;
        cfg.session.maxConsecutiveLinkErrors = 2;
        cfg.subscriptions = {{QStringLiteral("D100"), 4}};
        mc::McDevice device(cfg, std::move(fake));
        auto* recorder = new DeviceRecorder(device);
        QCOMPARE(qint64(device.config().frame.effectiveTimeoutMs()), qint64(200));

        QElapsedTimer clock;
        qint64 faultAtMs = -1;
        QObject::connect(&device, &mc::McDevice::linkFault, &device,
                         [&](const mc::LinkFaultInfo&) { faultAtMs = clock.elapsed(); });
        int chatterBytes = 0;
        QTimer chatter;
        chatter.setInterval(10);
        QObject::connect(&chatter, &QTimer::timeout, &chatter, [&]() {
            if (transport->written.contains('\x04')) { // the EOT went out: the flush has begun
                transport->deliver(QByteArray(1, 'x'));
                ++chatterBytes;
            }
        });

        clock.start();
        chatter.start();
        device.connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(faultAtMs >= 0, kWaitMs);
        chatter.stop();

        // Timeout at 200 ms, cap 200 ms later; bytes alone could never have ended the flush.
        QVERIFY2(faultAtMs >= 380, qPrintable(QString::number(faultAtMs)));
        QVERIFY2(faultAtMs <= 400 + 300, qPrintable(QString::number(faultAtMs)));
        QVERIFY2(chatterBytes >= 10, qPrintable(QString::number(chatterBytes)));
        QCOMPARE(int(transport->written.at(0)), 0x05); // the request begins with ENQ
        QVERIFY(transport->written.contains('\x04'));
        QCOMPARE(device.linkState(), mc::LinkState::Faulted);
        QCOMPARE(withoutCycles(recorder->traceAfterConnect()),
                 (QStringList{"fault(Timeout,reopen=0)", "link(Faulted,Fault)"}));
    }

    void CFG_DEV_01_setConfigOnlyWhileDisconnectedAndOnlyIfValid() {
        Rig rig;
        QVERIFY(rig.ok());
        QVERIFY(rig.device->configStatus());

        // Applies while Disconnected: the new subscriptions replace the old ones.
        mc::McDeviceConfig next = rig.device->config();
        next.subscriptions = {{QStringLiteral("D300"), 2}};
        QVERIFY(rig.device->setConfig(next));
        QCOMPARE(rig.device->config().subscriptions.size(), 1);

        // Runtime subscriptions are not written back to config().
        QVERIFY(rig.device->subscribe(u"D400", 1));
        QCOMPARE(rig.device->config().subscriptions.size(), 1);

        // An invalid config is refused with its path and the previous one stays.
        mc::McDeviceConfig bad = next;
        bad.subscriptions = {{QStringLiteral("Q10"), 1}};
        QString where;
        const mc::Expected<void> refused = rig.device->setConfig(bad, &where);
        QVERIFY(!refused);
        QCOMPARE(where, QStringLiteral("subscriptions[0].device"));
        QCOMPARE(rig.device->config().subscriptions.at(0).device, QStringLiteral("D300"));
        QVERIFY(rig.device->configStatus());

        // The applied config drives the round: one D snapshot, D300 x2 from the config and D400
        // from the runtime subscription made after setConfig().
        rig.device->connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(rig.recorder->hasCycle(1), kWaitMs);
        QCOMPARE(rig.recorder->trace().mid(0, 4),
                 (QStringList{"link(Connecting,Requested)", "link(Connected,Requested)",
                              "snapshot(D,1)", "cycle(1)"}));

        // Not while connected.
        const mc::Expected<void> whileUp = rig.device->setConfig(next);
        QVERIFY(!whileUp);
        QCOMPARE(whileUp.error().code, mc::ErrorCode::InvalidConfig);
    }

    void LOG_01_unusualFramePairingIsLoggedAtInfo() {
        MockPlcServer server;
        QVERIFY(server.listen());
        mc::McDeviceConfig cfg = configFor(server.port(), mc::FrameConfig::frame3C(), false);
        mc::McDevice device(cfg);
        QVERIFY(device.configStatus());
        CaptureSink sink;
        device.setLogSink(&sink);
        device.connectToPlc();
        const auto info = sink.of(mc::LogLevel::Info, QStringLiteral("mc.device"));
        QCOMPARE(info.size(), 1);
        QVERIFY2(info.at(0).message.contains(QStringLiteral("serial frame over a TCP")),
                 qPrintable(info.at(0).message));
        device.disconnectFromPlc();
    }

    void CFG_DEV_02_setConfigFromAnOpenFailedSlotThenConnectAgain() {
        // The natural retry: the connection failed, so a slot of linkStateChanged() switches to
        // another host and connects again. setConfig() runs from the flush at the end of a signal
        // slot of the real TcpTransport (its connect timer here), so it must not delete that
        // transport on the spot.
        MockPlcServer server;
        server.setInit(seedMemory);
        QVERIFY(server.listen());
        mc::McDeviceConfig closedPort = configFor(unusedPort());
        closedPort.tcp.connectTimeoutMs = 300;
        mc::McDevice device(closedPort);
        DeviceRecorder recorder(device);

        bool switched = false;
        bool applied = false;
        bool oldTransportAliveInSlot = false;
        QPointer<mc::Transport> oldTransport; // the transport that reported the failure
        QObject::connect(&device, &mc::McDevice::linkStateChanged, &device,
                         [&](mc::LinkState state, mc::LinkReason reason, const QString&) {
                             if (state == mc::LinkState::Disconnected &&
                                 reason == mc::LinkReason::OpenFailed && !switched) {
                                 switched = true;
                                 oldTransport = device.findChild<mc::Transport*>();
                                 applied = static_cast<bool>(
                                     device.setConfig(configFor(server.port())));
                                 // Deleted from the event loop, not on the spot, inside its own signal.
                                 oldTransportAliveInSlot = !oldTransport.isNull();
                                 device.connectToPlc();
                             }
                         });
        device.connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(recorder.hasCycle(1), kWaitMs);

        QVERIFY(applied);
        QVERIFY(oldTransportAliveInSlot);
        QTRY_VERIFY_WITH_TIMEOUT(oldTransport.isNull(), kWaitMs); // and then it is gone
        QCOMPARE(recorder.trace().mid(0, 6),
                 (QStringList{"link(Connecting,Requested)", "link(Disconnected,OpenFailed)",
                              "link(Connecting,Requested)", "link(Connected,Requested)",
                              "snapshot(M,1)", "snapshot(D,1)"}));
        QCOMPARE(server.connectionCount(), 1);
        // The replaced transport is gone once the event loop has run; the new one is the only one.
        QTRY_COMPARE_WITH_TIMEOUT(device.findChildren<mc::Transport*>().size(), 1, kWaitMs);
    }

    void PMP_03_disconnectWhileConnectingCancelsTheOpen() {
        const quint16 port = unusedPort();
        QVERIFY(port != 0);
        mc::McDeviceConfig cfg = configFor(port);
        cfg.tcp.connectTimeoutMs = 300;
        mc::McDevice device(cfg);
        DeviceRecorder recorder(device);

        device.connectToPlc();
        QCOMPARE(device.linkState(), mc::LinkState::Connecting);
        device.disconnectFromPlc();
        QCOMPARE(device.linkState(), mc::LinkState::Disconnected);
        QCOMPARE(recorder.trace(), (QStringList{"link(Connecting,Requested)",
                                                "link(Disconnected,Requested)"}));

        // The cancelled open reports nothing later, not even when its 300 ms timer would fire.
        QSignalSpy more(&device, &mc::McDevice::linkStateChanged);
        QVERIFY(!more.wait(600));
        QCOMPARE(recorder.trace().size(), 2);
    }
};

QTEST_GUILESS_MAIN(TstMcDevice)

#include "tst_mc_device.moc"
