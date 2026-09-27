// Optional heartbeat write (spec SPEC-core-session.md, "Ad-hoc requests": "The heartbeat write is
// internal"; "Dispatch" step 3; HeartbeatConfig): SES-21.
#include "doctest/doctest.h"

#include "harness.h"

#include "mc/core/protocol.h"
#include "mc/core/session.h"

#include <utility>
#include <vector>

using mc::ByteView;
using mc::Device;
using mc::DeviceType;
using mc::FrameConfig;
using mc::LinkFaultKind;
using mc::McProtocol;
using mc::OutputKind;
using mc::Request;
using mc::Session;
using mc::SessionConfig;
using mc::TimeMs;
using mc::test::FakeClock;
using mc::test::OutputRecorder;
using mc::test::PeerScript;
using mc::test::ScriptedPeer;

namespace {

Session makeSession(const FrameConfig& frame, const SessionConfig& cfg = {}) {
    auto created = Session::create(frame, cfg);
    REQUIRE(created.hasValue());
    return std::move(created.value());
}

PeerScript okWords(std::initializer_list<uint16_t> words) {
    PeerScript p;
    p.kind = PeerScript::Kind::Ok;
    p.words = words;
    return p;
}

PeerScript plcError(uint16_t endCode) {
    PeerScript p;
    p.kind = PeerScript::Kind::PlcError;
    p.plcEndCode = endCode;
    return p;
}

} // namespace

// Every batch drained below is checked for an exact size and exact kinds at every index (never
// just "at least"/"contains"), so "no RequestDone ever appears" is already fully covered without
// a separate standing check: a stray RequestDone anywhere would fail the very next size/kind
// assertion on sight.

TEST_CASE("SES-21 Heartbeat off by default: never sent; heartbeatOk always true") {
    FrameConfig frame = FrameConfig::frame3E();
    Session s = makeSession(frame); // Default SessionConfig: heartbeat.enabled == false.
    FakeClock clock;
    OutputRecorder rec;
    ScriptedPeer peer(frame);

    REQUIRE(s.subscribe(Device{DeviceType::D, 100}, 1).hasValue());
    s.linkUp(clock.now());
    rec.drain(s);
    REQUIRE(rec.size() == 1);
    CHECK(rec[0].kind == OutputKind::Send);
    // The one and only frame sent is the poll chunk's own read, not a heartbeat write.
    auto pollFrame = McProtocol(frame).encode(Request::readWords(Device{DeviceType::D, 100}, 1));
    REQUIRE(pollFrame.hasValue());
    CHECK(rec[0].bytes == pollFrame.value());
    rec.clear();

    auto bytes = peer.respond(Request::readWords(Device{DeviceType::D, 100}, 1), okWords({1}));
    s.bytesIn(ByteView{bytes.data(), bytes.size()}, clock.advance(1));
    rec.drain(s);
    REQUIRE(rec.size() == 2); // Snapshot(D, round 1) then CycleDone.
    CHECK(rec[0].kind == OutputKind::Snapshot);
    CHECK(rec[1].kind == OutputKind::CycleDone);
    CHECK(rec[1].cycle.heartbeatOk == true);
}

TEST_CASE("SES-21 Heartbeat on: first frame of every round writes 1, 0, 1, ...; no RequestDone") {
    FrameConfig frame = FrameConfig::frame3E();
    SessionConfig cfg;
    cfg.heartbeat.enabled = true;
    cfg.heartbeat.device = Device{DeviceType::M, 2000};
    cfg.cycleIntervalMs = 1000; // Comfortably later than every clock.advance() below.
    Session s = makeSession(frame, cfg);
    FakeClock clock;
    OutputRecorder rec;
    ScriptedPeer peer(frame);
    McProtocol proto(frame);

    uint8_t one = 1;
    uint8_t zero = 0;
    auto hbOn = proto.encode(Request::writeBits(cfg.heartbeat.device, ByteView{&one, 1}));
    auto hbOff = proto.encode(Request::writeBits(cfg.heartbeat.device, ByteView{&zero, 1}));
    REQUIRE(hbOn.hasValue());
    REQUIRE(hbOff.hasValue());
    auto pollFrame = proto.encode(Request::readWords(Device{DeviceType::D, 100}, 1));
    REQUIRE(pollFrame.hasValue());

    REQUIRE(s.subscribe(Device{DeviceType::D, 100}, 1).hasValue());

    // Round 1: heartbeat bit 1, then the poll chunk.
    s.linkUp(clock.now());
    rec.drain(s);
    REQUIRE(rec.size() == 1);
    CHECK(rec[0].kind == OutputKind::Send);
    CHECK(rec[0].bytes == hbOn.value());
    rec.clear();

    auto hbAck1 = peer.respond(Request::writeBits(cfg.heartbeat.device, ByteView{&one, 1}), PeerScript{});
    s.bytesIn(ByteView{hbAck1.data(), hbAck1.size()}, clock.advance(1));
    rec.drain(s);
    REQUIRE(rec.size() == 1);
    CHECK(rec[0].kind == OutputKind::Send);
    CHECK(rec[0].bytes == pollFrame.value());
    rec.clear();

    auto pollBytes1 = peer.respond(Request::readWords(Device{DeviceType::D, 100}, 1), okWords({1}));
    s.bytesIn(ByteView{pollBytes1.data(), pollBytes1.size()}, clock.advance(1));
    rec.drain(s);
    REQUIRE(rec.size() == 2); // Snapshot(D, round 1) then CycleDone.
    CHECK(rec[0].kind == OutputKind::Snapshot);
    CHECK(rec[1].kind == OutputKind::CycleDone);
    CHECK(rec[1].cycle.round == 1);
    CHECK(rec[1].cycle.heartbeatOk == true);
    rec.clear();

    // Round 2 (after cycleIntervalMs): heartbeat bit 0, then the poll chunk again.
    s.tick(clock.advance(1000));
    rec.drain(s);
    REQUIRE(rec.size() == 1);
    CHECK(rec[0].kind == OutputKind::Send);
    CHECK(rec[0].bytes == hbOff.value());
    rec.clear();

    auto hbAck2 = peer.respond(Request::writeBits(cfg.heartbeat.device, ByteView{&zero, 1}), PeerScript{});
    s.bytesIn(ByteView{hbAck2.data(), hbAck2.size()}, clock.advance(1));
    rec.drain(s);
    REQUIRE(rec.size() == 1);
    CHECK(rec[0].kind == OutputKind::Send);
    CHECK(rec[0].bytes == pollFrame.value());
    rec.clear();

    auto pollBytes2 = peer.respond(Request::readWords(Device{DeviceType::D, 100}, 1), okWords({1}));
    s.bytesIn(ByteView{pollBytes2.data(), pollBytes2.size()}, clock.advance(1));
    rec.drain(s);
    REQUIRE(rec.size() == 2);
    CHECK(rec[0].kind == OutputKind::Snapshot);
    CHECK(rec[1].kind == OutputKind::CycleDone);
    CHECK(rec[1].cycle.round == 2);
    CHECK(rec[1].cycle.heartbeatOk == true);
}

TEST_CASE("SES-21 Heartbeat write failure sets heartbeatOk=false only: no RequestDone, no "
          "LinkFault, polling continues") {
    FrameConfig frame = FrameConfig::frame3E();
    SessionConfig cfg;
    cfg.heartbeat.enabled = true;
    cfg.heartbeat.device = Device{DeviceType::M, 2000};
    Session s = makeSession(frame, cfg);
    FakeClock clock;
    OutputRecorder rec;
    ScriptedPeer peer(frame);

    REQUIRE(s.subscribe(Device{DeviceType::D, 100}, 1).hasValue());
    s.linkUp(clock.now());
    rec.drain(s);
    REQUIRE(rec.size() == 1); // The heartbeat write (round 1, bit 1).
    rec.clear();

    uint8_t one = 1;
    auto hbFail =
        peer.respond(Request::writeBits(cfg.heartbeat.device, ByteView{&one, 1}), plcError(0x4031));
    s.bytesIn(ByteView{hbFail.data(), hbFail.size()}, clock.advance(1));
    rec.drain(s);
    REQUIRE(rec.size() == 1); // Polling continues right away: the poll chunk's own Send.
    CHECK(rec[0].kind == OutputKind::Send);
    rec.clear();
    CHECK_FALSE(s.isFaulted());

    auto pollBytes = peer.respond(Request::readWords(Device{DeviceType::D, 100}, 1), okWords({1}));
    s.bytesIn(ByteView{pollBytes.data(), pollBytes.size()}, clock.advance(1));
    rec.drain(s);
    REQUIRE(rec.size() == 2);
    CHECK(rec[0].kind == OutputKind::Snapshot);
    CHECK(rec[1].kind == OutputKind::CycleDone);
    CHECK(rec[1].cycle.heartbeatOk == false); // This round's own heartbeat write failed.
    CHECK_FALSE(s.isFaulted());
}

TEST_CASE("Heartbeat: a fault while the heartbeat write itself is in flight (Checkpoint C "
          "coverage gap, T-028: SES-16 faults a poll chunk or an ad-hoc write, never the "
          "heartbeat; faultLink()'s own heartbeat branch just sets heartbeatOkThisRound=false, "
          "with no RequestDone/ChunkInfo to touch, and this round is abandoned before any "
          "CycleDone reports it either way)") {
    FrameConfig frame = FrameConfig::frame3E();
    SessionConfig cfg;
    cfg.heartbeat.enabled = true;
    cfg.heartbeat.device = Device{DeviceType::M, 2000};
    Session s = makeSession(frame, cfg);
    FakeClock clock;
    OutputRecorder rec;

    s.linkUp(clock.now());
    rec.drain(s);
    REQUIRE(rec.size() == 1);
    CHECK(rec[0].kind == OutputKind::Send); // The heartbeat write, round 1's own first frame.
    rec.clear();

    s.tick(clock.advance(frame.effectiveTimeoutMs())); // Times out while the heartbeat itself is
                                                        // the in-flight item.
    rec.drain(s);
    REQUIRE(rec.size() == 1);
    CHECK(rec[0].kind == OutputKind::LinkFault);
    CHECK(rec[0].fault == LinkFaultKind::Timeout);
    CHECK(s.isFaulted());
}

TEST_CASE("Heartbeat: enabled with zero subscriptions completes an empty round via dispatch()'s "
          "own heartbeat-only-round branch (Checkpoint C coverage gap, T-028: flagged in this "
          "task's own T-026 Dev notes as an untested corner -- every other heartbeat test "
          "subscribes at least one device)") {
    FrameConfig frame = FrameConfig::frame3E();
    SessionConfig cfg;
    cfg.heartbeat.enabled = true;
    cfg.heartbeat.device = Device{DeviceType::M, 2000};
    Session s = makeSession(frame, cfg); // No subscriptions at all.
    FakeClock clock;
    OutputRecorder rec;
    ScriptedPeer peer(frame);

    s.linkUp(clock.now());
    rec.drain(s);
    REQUIRE(rec.size() == 1);
    CHECK(rec[0].kind == OutputKind::Send); // The heartbeat write: the round's only frame.
    rec.clear();

    uint8_t one = 1;
    auto hbAck =
        peer.respond(Request::writeBits(cfg.heartbeat.device, ByteView{&one, 1}), PeerScript{});
    s.bytesIn(ByteView{hbAck.data(), hbAck.size()}, clock.advance(1));
    rec.drain(s);
    // No poll chunk exists to send next: dispatch()'s own m_currentChunk < m_plan.size() guard
    // (T-026) routes straight to endRound() instead, from *this* call rather than the heartbeat's
    // own completion falling through to a poll send.
    REQUIRE(rec.size() == 1);
    CHECK(rec[0].kind == OutputKind::CycleDone); // No Snapshot: no subscribed device type at all.
    CHECK(rec[0].cycle.round == 1);
    CHECK(rec[0].cycle.heartbeatOk == true);
}
