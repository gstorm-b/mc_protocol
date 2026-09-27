// Ethernet faults + the drain contract (spec SPEC-core-session.md, "Faults, retries, EOT" --
// Ethernet column only; "Drain contract"): SES-16, SES-19 (Ethernet), SES-20, SES-24 (Ethernet),
// SES-25. Serial (SES-17, 18, 27) is not implemented yet (no EOT/Flushing support), so this file
// covers Ethernet only, matching session.cpp/session_rx.cpp's own scope this task.
#include "doctest/doctest.h"

#include "harness.h"

#include "core/session/drain_violation.h"
#include "mc/core/convert.h"
#include "mc/core/protocol.h"
#include "mc/core/session.h"

#include <utility>
#include <vector>

using mc::ByteView;
using mc::ChunkState;
using mc::Device;
using mc::DeviceType;
using mc::ErrorCode;
using mc::FrameConfig;
using mc::LinkFaultKind;
using mc::LogLevel;
using mc::McProtocol;
using mc::MutableByteView;
using mc::OutputKind;
using mc::Request;
using mc::Session;
using mc::SessionConfig;
using mc::TimeMs;
using mc::test::FakeClock;
using mc::test::OutputRecorder;
using mc::test::PeerScript;
using mc::test::RecordingLogSink;
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

// SES-25's own recording drain-violation handler (drain_violation.h, T-026 rework): a plain
// function pointer (mc::detail::DrainViolationHandler's own signature), so state lives in a
// namespace-scope counter rather than a captured lambda. RaiiRestoreHandler's destructor restores
// the default handler unconditionally -- including when a REQUIRE/CHECK failure unwinds the test
// case early (doctest's own failed-assertion path is exception-based, so this destructor still
// runs) -- so a later test case never inherits a stale handler.
int g_drainViolationCalls = 0;

void recordDrainViolation() noexcept { ++g_drainViolationCalls; }

struct RaiiRestoreHandler {
    ~RaiiRestoreHandler() { mc::detail::setDrainViolationHandler(nullptr); }
};

PeerScript plcError(uint16_t endCode) {
    PeerScript p;
    p.kind = PeerScript::Kind::PlcError;
    p.plcEndCode = endCode;
    return p;
}

} // namespace

TEST_CASE("SES-16 Ethernet timeout: in-flight read -> chunk Failed; queued ad-hoc -> LinkDown; "
          "LinkFault{Timeout, reopen}; nothing sent until linkDown + linkUp; round 1 restarts") {
    FrameConfig frame = FrameConfig::frame3E();

    SUBCASE("in-flight poll chunk") {
        Session s = makeSession(frame);
        FakeClock clock;
        OutputRecorder rec;
        REQUIRE(s.subscribe(Device{DeviceType::D, 100}, 1).hasValue());
        s.linkUp(clock.now());
        rec.drain(s);
        REQUIRE(rec.size() == 1);
        CHECK(rec[0].kind == OutputKind::Send);
        rec.clear();

        s.tick(clock.advance(frame.effectiveTimeoutMs()));
        rec.drain(s);
        REQUIRE(rec.size() == 1);
        CHECK(rec[0].kind == OutputKind::LinkFault);
        CHECK(rec[0].fault == LinkFaultKind::Timeout);
        CHECK(rec[0].error.code == ErrorCode::Timeout);
        CHECK(rec[0].reopenTransport == true);
        CHECK(s.isFaulted());
        CHECK(s.plan().chunk(0).state == ChunkState::Failed);
        CHECK(s.plan().chunk(0).lastError.code == ErrorCode::Timeout);
        rec.clear();

        // Nothing sent until linkDown() + linkUp() (state Faulted: bytesIn/tick do nothing).
        s.tick(clock.advance(1000));
        rec.drain(s);
        CHECK(rec.empty());
        s.bytesIn(ByteView{}, clock.advance(1));
        rec.drain(s);
        CHECK(rec.empty());

        s.linkDown(clock.advance(1));
        rec.drain(s);
        rec.clear();
        CHECK_FALSE(s.isFaulted());

        // Round 1 restarts: the very next round after recovery is numbered 1 again, not a
        // continuation of the round the fault interrupted.
        s.linkUp(clock.advance(1));
        rec.drain(s);
        REQUIRE(rec.size() == 1);
        CHECK(rec[0].kind == OutputKind::Send);
        rec.clear();

        ScriptedPeer peer(frame);
        auto bytes = peer.respond(Request::readWords(Device{DeviceType::D, 100}, 1), okWords({7}));
        s.bytesIn(ByteView{bytes.data(), bytes.size()}, clock.advance(1));
        rec.drain(s);
        // Round 1's own snapshot (decision S2, held back to endRound()) precedes CycleDone; one
        // subscribed device type here means exactly this pair, same as SES-01..08's own round-1
        // completions.
        REQUIRE(rec.size() == 2);
        CHECK(rec[0].kind == OutputKind::Snapshot);
        CHECK(rec[1].kind == OutputKind::CycleDone);
        CHECK(rec[1].cycle.round == 1);
    }

    SUBCASE("in-flight ad-hoc write, 2 more queued behind it") {
        Session s = makeSession(frame);
        FakeClock clock;
        OutputRecorder rec;
        s.linkUp(clock.now());
        rec.drain(s);
        rec.clear();

        auto data = mc::convert::fromWords({1});
        ByteView dv{data.data(), data.size()};
        // id0 is sent immediately (idle): drain its own Send before the next submit() call (drain
        // contract; T-026 rework: an undrained output now trips the real debug assert). id1/id2
        // produce nothing to drain (something is already in flight).
        auto id0 = s.submit(Request::writeWords(Device{DeviceType::D, 0}, dv), clock.now());
        REQUIRE(id0.hasValue());
        rec.drain(s);
        rec.clear();
        auto id1 = s.submit(Request::writeWords(Device{DeviceType::D, 1}, dv), clock.now());
        REQUIRE(id1.hasValue());
        rec.drain(s);
        auto id2 = s.submit(Request::writeWords(Device{DeviceType::D, 2}, dv), clock.now());
        REQUIRE(id2.hasValue());
        rec.drain(s);
        rec.clear();

        s.tick(clock.advance(frame.effectiveTimeoutMs()));
        rec.drain(s);
        REQUIRE(rec.size() == 4);
        CHECK(rec[0].kind == OutputKind::RequestDone);
        CHECK(rec[0].requestId == id0.value());
        CHECK(rec[0].error.code == ErrorCode::Timeout);
        CHECK(rec[0].payload.empty());
        CHECK(rec[1].kind == OutputKind::RequestDone);
        CHECK(rec[1].requestId == id1.value());
        CHECK(rec[1].error.code == ErrorCode::LinkDown);
        CHECK(rec[2].kind == OutputKind::RequestDone);
        CHECK(rec[2].requestId == id2.value());
        CHECK(rec[2].error.code == ErrorCode::LinkDown);
        CHECK(rec[3].kind == OutputKind::LinkFault);
        CHECK(rec[3].fault == LinkFaultKind::Timeout);
        CHECK(s.isFaulted());
    }
}

TEST_CASE("SES-19 Ethernet wrong subheader -> LinkFault{ProtocolError, reopen}") {
    FrameConfig frame = FrameConfig::frame3E();
    Session s = makeSession(frame);
    FakeClock clock;
    OutputRecorder rec;
    REQUIRE(s.subscribe(Device{DeviceType::D, 100}, 1).hasValue());
    s.linkUp(clock.now());
    rec.drain(s);
    REQUIRE(rec.size() == 1);
    rec.clear();

    ScriptedPeer peer(frame);
    auto bytes = peer.respond(Request::readWords(Device{DeviceType::D, 100}, 1), okWords({7}));
    REQUIRE(bytes.size() >= 2);
    bytes[0] = static_cast<uint8_t>(~bytes[0]); // Corrupt the 3E response subheader.

    s.bytesIn(ByteView{bytes.data(), bytes.size()}, clock.advance(1));
    rec.drain(s);
    REQUIRE(rec.size() == 1);
    CHECK(rec[0].kind == OutputKind::LinkFault);
    CHECK(rec[0].fault == LinkFaultKind::ProtocolError);
    CHECK(rec[0].error.code == ErrorCode::FrameMismatch);
    CHECK(rec[0].reopenTransport == true);
    CHECK(s.isFaulted());
    CHECK(s.plan().chunk(0).state == ChunkState::Failed);
    CHECK(s.plan().chunk(0).lastError.code == ErrorCode::FrameMismatch);
}

TEST_CASE("SES-20 A PLC error response resets the consecutive-error counter (never faults, "
          "however many occur in a row)") {
    FrameConfig frame = FrameConfig::frame3E();
    Session s = makeSession(frame);
    FakeClock clock;
    OutputRecorder rec;
    ScriptedPeer peer(frame);
    REQUIRE(s.subscribe(Device{DeviceType::D, 100}, 1).hasValue());
    s.linkUp(clock.now());
    rec.drain(s);
    REQUIRE(rec.size() == 1);
    rec.clear();

    for (uint32_t round = 1; round <= 5; ++round) {
        auto bytes = peer.respond(Request::readWords(Device{DeviceType::D, 100}, 1), plcError(0x4031));
        s.bytesIn(ByteView{bytes.data(), bytes.size()}, clock.advance(1));
        rec.drain(s);
        // One subscribed device type, one chunk: its own completion is simultaneously "last chunk
        // of its type" and "last chunk of the round", so a Snapshot always precedes CycleDone here
        // (round 1 via endRound()'s own catch-up loop, round 2+ via completeCurrentChunk()'s Rule
        // 2) -- a PLC error never suppresses either, only ValuesChanged (gated on `ok`, spec Rule
        // 1).
        REQUIRE(rec.size() == 2);
        CHECK(rec[0].kind == OutputKind::Snapshot);
        CHECK(rec[0].deviceType == DeviceType::D);
        CHECK(rec[0].round == round);
        CHECK(rec[1].kind == OutputKind::CycleDone);
        CHECK(rec[1].cycle.round == round);
        CHECK_FALSE(s.isFaulted());
        CHECK(s.plan().chunk(0).state == ChunkState::Failed);
        CHECK(s.plan().chunk(0).lastError.code == ErrorCode::PlcError);
        rec.clear();

        // Next round's own chunk goes out normally (link never faulted).
        s.tick(clock.advance(200));
        rec.drain(s);
        REQUIRE(rec.size() == 1);
        CHECK(rec[0].kind == OutputKind::Send);
        rec.clear();
    }
    CHECK(s.stats().plcErrors == 5);
    CHECK(s.stats().timeouts == 0);
    CHECK(s.stats().protocolErrors == 0);
}

TEST_CASE("SES-24 Unsolicited bytes: Ethernet idle -> fault; down -> discarded") {
    FrameConfig frame = FrameConfig::frame3E();

    SUBCASE("Idle") {
        Session s = makeSession(frame); // No subscriptions: round 1 ends at once, leaving Idle.
        FakeClock clock;
        OutputRecorder rec;
        s.linkUp(clock.now());
        rec.drain(s);
        REQUIRE(rec.size() == 1);
        CHECK(rec[0].kind == OutputKind::CycleDone);
        rec.clear();
        CHECK_FALSE(s.isFaulted());

        uint8_t garbage[4] = {1, 2, 3, 4};
        s.bytesIn(ByteView{garbage, sizeof(garbage)}, clock.advance(1));
        rec.drain(s);
        REQUIRE(rec.size() == 1);
        CHECK(rec[0].kind == OutputKind::LinkFault);
        CHECK(rec[0].fault == LinkFaultKind::ProtocolError);
        CHECK(rec[0].reopenTransport == true);
        CHECK(s.isFaulted());
    }

    SUBCASE("Down") {
        Session s = makeSession(frame); // Never linked up: state Down.
        FakeClock clock;
        OutputRecorder rec;
        uint8_t garbage[4] = {1, 2, 3, 4};
        s.bytesIn(ByteView{garbage, sizeof(garbage)}, clock.advance(1));
        rec.drain(s);
        CHECK(rec.empty()); // Discarded, no fault (spec fault table: Down -> discarded).
        CHECK_FALSE(s.isFaulted());
    }
}

TEST_CASE("SES-25 Drain contract: an input call with pending outputs discards them and logs "
          "Error; the debug-only violation handler runs first") {
    FrameConfig frame = FrameConfig::frame3E();
    RecordingLogSink sink;
    SessionConfig cfg;
    cfg.log = &sink;
    Session s = makeSession(frame, cfg);
    FakeClock clock;
    OutputRecorder rec;
    s.linkUp(clock.now());
    rec.drain(s);
    rec.clear();
    sink.clear();

    auto data = mc::convert::fromWords({1});
    auto submitted = s.submit(
        Request::writeWords(Device{DeviceType::D, 0}, ByteView{data.data(), data.size()}),
        clock.now());
    REQUIRE(submitted.hasValue());
    // Deliberately not draining: submit() above queued exactly one Send.

    g_drainViolationCalls = 0;
    mc::detail::setDrainViolationHandler(&recordDrainViolation);
    RaiiRestoreHandler restoreHandler; // Runs even if a REQUIRE below fails and unwinds early.

    s.tick(clock.now()); // Another input call while that Send is still pending.

#ifndef NDEBUG
    CHECK(g_drainViolationCalls == 1); // Debug: notifyDrainViolation() calls the handler.
#else
    CHECK(g_drainViolationCalls == 0); // Release: notifyDrainViolation() is a no-op.
#endif

    mc::Output out;
    CHECK_FALSE(s.nextOutput(out)); // The stale Send was discarded either way, not delivered late.
    CHECK(sink.any(LogLevel::Error));
}

TEST_CASE("Rework (phase review blocker, T-026): a drain violation must release "
          "m_pendingPlanRefs for a discarded Snapshot/ValuesChanged, or dispatch()'s own "
          "round-start guard hangs forever") {
    // Prove-It: this test is written to fail on the pre-fix code (checkDrained()'s own raw
    // m_outputRing->pop() loop, which never touches m_pendingPlanRefs) and pass once
    // checkDrained() releases a discarded output the same way nextOutput() does. See Dev notes
    // for the exact pre-fix failure this reproduced.
    FrameConfig frame = FrameConfig::frame3E();
    Session s = makeSession(frame);
    FakeClock clock;
    ScriptedPeer peer(frame);

    REQUIRE(s.subscribe(Device{DeviceType::D, 100}, 1).hasValue());
    s.linkUp(clock.now());
    mc::Output out;
    REQUIRE(s.nextOutput(out));
    REQUIRE(out.kind == OutputKind::Send);
    REQUIRE_FALSE(s.nextOutput(out));

    auto bytes = peer.respond(Request::readWords(Device{DeviceType::D, 100}, 1), okWords({1}));
    s.bytesIn(ByteView{bytes.data(), bytes.size()}, clock.advance(1));
    // Round 1 completes: decision S2's own round-1 catch-up loop emits Snapshot{D} (bumping
    // m_pendingPlanRefs, session_rx.cpp's emitSnapshot()) then CycleDone. Deliberately not
    // draining either -- the whole point is a caller that violates the drain contract right here.

    g_drainViolationCalls = 0;
    mc::detail::setDrainViolationHandler(&recordDrainViolation);
    RaiiRestoreHandler restoreHandler; // Runs even if a REQUIRE below fails and unwinds early.

    // A second subscribe() call: checkDrained() (its own first line) finds the ring still holding
    // the undrained Snapshot+CycleDone and discards them (one violation), then subscribe() itself
    // sets m_rePlanPending = true for this new subscription.
    REQUIRE(s.subscribe(Device{DeviceType::D, 200}, 1).hasValue());
    CHECK(g_drainViolationCalls == 1);
    REQUIRE_FALSE(s.nextOutput(out)); // Confirms the ring really is empty: the violation did fire.

    // Round 2 must still be able to start. On the pre-fix code, m_pendingPlanRefs is stuck at 1
    // (never decremented for the discarded Snapshot), so dispatch()'s own round-start guard
    // (`m_rePlanPending && m_pendingPlanRefs > 0`, session.cpp) returns early forever: nothing
    // ever gets sent again, for any number of further tick() calls. Checked several times over
    // (not just once) so a single missed round cannot look like ordinary scheduling.
    bool sawAnyOutput = false;
    for (int i = 0; i < 5 && !sawAnyOutput; ++i) {
        s.tick(clock.advance(200)); // Default cycleIntervalMs is 100: comfortably past due.
        if (s.nextOutput(out)) {
            sawAnyOutput = true;
        }
    }
    CHECK(sawAnyOutput); // Fails on the pre-fix code: no round ever starts again (a silent hang).
}

TEST_CASE("Rework (phase review blocker, T-026): a drain violation must release an ad-hoc job's "
          "own arena/job-ring slot (AdHocQueue::confirmDrained()) for a discarded RequestDone, "
          "or adHocCapacity is permanently, cumulatively lost") {
    // Prove-It: same reasoning as the Snapshot/ValuesChanged test above, for the RequestDone/
    // AdHocQueue side of the identical bug. See Dev notes for the exact pre-fix failure.
    FrameConfig frame = FrameConfig::frame3E();
    SessionConfig cfg;
    cfg.adHocCapacity = 4;
    cfg.adHocArenaBytes = 4096; // Generous: this test is about job-ring slots, not arena bytes.
    Session s = makeSession(frame, cfg);
    FakeClock clock;
    ScriptedPeer peer(frame);
    s.linkUp(clock.now());
    mc::Output out;
    while (s.nextOutput(out)) {
        // Round 1's own CycleDone (empty plan): drained normally here, before the measured part.
    }

    g_drainViolationCalls = 0;
    mc::detail::setDrainViolationHandler(&recordDrainViolation);
    RaiiRestoreHandler restoreHandler;

    auto data = mc::convert::fromWords({1});
    ByteView dv{data.data(), data.size()};

    // Strictly more submit/complete cycles than adHocCapacity: on the pre-fix code, every cycle's
    // own RequestDone gets discarded by a drain violation (this same test's own design) instead of
    // ever reaching nextOutput(), so AdHocQueue::confirmDrained() never runs for it and that job's
    // ring slot is never freed -- a permanent, cumulative leak that must exhaust adHocCapacity
    // well before this loop finishes.
    const int cycles = static_cast<int>(cfg.adHocCapacity) + 3;
    for (int i = 0; i < cycles; ++i) {
        auto submitted = s.submit(
            Request::writeWords(Device{DeviceType::D, static_cast<uint32_t>(i)}, dv), clock.now());
        REQUIRE(submitted.hasValue()); // The real proof: still succeeds well past adHocCapacity.

        REQUIRE(s.nextOutput(out));
        REQUIRE(out.kind == OutputKind::Send);
        REQUIRE_FALSE(s.nextOutput(out));

        auto bytes = peer.respond(Request::writeWords(Device{DeviceType::D, 0}, dv), PeerScript{});
        s.bytesIn(ByteView{bytes.data(), bytes.size()}, clock.advance(1));
        // Deliberately not draining the resulting RequestDone: the *next* iteration's own
        // submit() call (its own checkDrained()) discards it via a violation instead. The very
        // last cycle's own RequestDone is left for the trailing tick() below to discard.
    }
    s.tick(clock.now());

    CHECK(g_drainViolationCalls == cycles); // One per cycle: cycles - 1 from later submit()s, one
                                              // from the trailing tick() for the last cycle.
}
