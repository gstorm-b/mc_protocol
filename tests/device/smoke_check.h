// smoke_check.h -- the body of the smoke test every transport shares: QDV-13 (TCP, including a
// serial frame over a TCP serial-device server) and QDV-14 (a real COM port pair) both call it, so
// the two prove the same thing and only the wire differs.
#pragma once

#include "device_recorder.h"
#include "device_test_support.h"

#include "mc/device/mc_device.h"
#include "mc/mock/mock_plc.h"

#include <QtTest>

#include <functional>

namespace testing {

// One polling round and one write of each kind on a connected-or-connecting `device` whose peer is
// a MockPlc seeded with seedMemory() and `configFor()`'s subscriptions (D100 x4, M0 x16). `plc`
// returns the mock that serves the link (it may be null until the peer has accepted). The expected
// values come from the seeded image and the requests, not from the frame.
inline void verifySmoke(DeviceRecorder& recorder, mc::McDevice& device,
                        const std::function<mc::MockPlc*()>& plc, int timeoutMs = kWaitMs) {
    QTRY_VERIFY_WITH_TIMEOUT(recorder.hasCycle(1), timeoutMs);
    QVERIFY(plc() != nullptr);

    // Round 1: the M snapshot (bits, one byte per point, M3 set) then the D snapshot.
    const QVector<Event> snapshots = recorder.of(Event::Kind::Snapshot);
    QVERIFY(snapshots.size() >= 2);
    const mc::SnapshotSegment& bits = snapshots.at(0).snapshot.segments.at(0);
    QCOMPARE(bits.count, 16u);
    QCOMPARE(int(bits.values.at(3)), 1);
    QCOMPARE(int(bits.values.at(2)), 0);
    const Event snapshotD = snapshots.at(1);
    QCOMPARE(snapshotD.snapshot.segments.at(0).values, wordsLe({10, 20, 30, 40}));
    QCOMPARE(int(snapshotD.snapshot.chunks.at(0).state), int(mc::ChunkState::Ok));

    // A word write and an odd-count bit write (a padded last nibble on 1E Binary), each
    // finishing once without error.
    const mc::Expected<mc::RequestId> w = device.writeWords(u"D200", {0x1234});
    QVERIFY(w);
    QTRY_VERIFY_WITH_TIMEOUT(recorder.count(Event::Kind::Finished) == 1, timeoutMs);
    QVERIFY(recorder.of(Event::Kind::Finished).at(0).error.ok());
    QCOMPARE(plc()->word(dev("D200")), uint16_t{0x1234});

    const mc::Expected<mc::RequestId> wb = device.writeBits(u"M100", {true, false, true});
    QVERIFY(wb);
    QTRY_VERIFY_WITH_TIMEOUT(recorder.count(Event::Kind::Finished) == 2, timeoutMs);
    QVERIFY(recorder.of(Event::Kind::Finished).at(1).error.ok());
    QVERIFY(plc()->bit(dev("M100")));
    QVERIFY(!plc()->bit(dev("M101")));
    QVERIFY(plc()->bit(dev("M102")));
    QVERIFY(!plc()->bit(dev("M103")));
}

} // namespace testing
