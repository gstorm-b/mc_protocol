// Faults and the drain contract (spec SPEC-core-session.md, "Faults, retries, EOT"; "Drain
// contract"). The Ethernet column on 3E (SES-16, SES-19, SES-20, SES-24, SES-25) comes first; the
// serial column on 3C (SES-17, SES-18, SES-19, SES-20, SES-23, SES-24, SES-27 and the receive
// buffer overflow row) follows after the "Serial column" banner below, driven by the golden
// vectors of tests/vectors/3c_f*.vec.
#include "doctest/doctest.h"

#include "harness.h"

#include "core/session/drain_violation.h"
#include "mc/core/convert.h"
#include "mc/core/protocol.h"
#include "mc/core/session.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using mc::ByteView;
using mc::ChunkState;
using mc::Device;
using mc::DeviceType;
using mc::ErrorCode;
using mc::FrameConfig;
using mc::kNoDeadline;
using mc::LinkFaultKind;
using mc::LogLevel;
using mc::McProtocol;
using mc::MutableByteView;
using mc::OutputKind;
using mc::PointState;
using mc::Request;
using mc::SerialFormat;
using mc::Session;
using mc::SessionConfig;
using mc::TimeMs;
using mc::test::FakeClock;
using mc::test::OutputRecorder;
using mc::test::PeerScript;
using mc::test::RecordedOutput;
using mc::test::RecordingLogSink;
using mc::test::ScriptedPeer;
using mc::test::serialReadResponse3C;
using mc::test::vectorBytes;

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

// =============================================================================================
// Serial column (T-051): 3C, golden vectors of tests/vectors/3c_f*.vec.
//
// Every test below uses readRetries = 2 (spec "Testing Strategy") unless it says otherwise, and an
// explicit 1000 ms response timeout (FrameConfig::timeoutMs) so every deadline is a literal; the
// serialFlushMs (50) and serialInterCharMs (100) defaults are the spec's own. The next round is
// pushed 1000 s out, so one round at a time is under test. Every input call is followed by a
// complete drain (Rig), and every batch is checked for its exact size and kinds.
// =============================================================================================

namespace {

using Bytes = std::vector<uint8_t>;

const Device kD100{DeviceType::D, 100};
const Device kM100{DeviceType::M, 100};
const Bytes kEot = {0x04};
const Bytes kEotCrLf = {0x04, 0x0D, 0x0A};

FrameConfig serialFrame(SerialFormat format = SerialFormat::Format1) {
    FrameConfig frame = FrameConfig::frame3C(format);
    frame.readRetries = 2;
    frame.timeoutMs = 1000;
    return frame;
}

SessionConfig serialConfig() {
    SessionConfig cfg;
    cfg.cycleIntervalMs = 1'000'000;
    return cfg;
}

// A Session and its recorder, drained after every input call.
struct Rig {
    Session s;
    OutputRecorder rec;

    explicit Rig(const FrameConfig& frame, const SessionConfig& cfg = serialConfig())
        : s(makeSession(frame, cfg)) {}

    void up(TimeMs t) {
        s.linkUp(t);
        rec.drain(s);
    }
    void down(TimeMs t) {
        s.linkDown(t);
        rec.drain(s);
    }
    void tick(TimeMs t) {
        s.tick(t);
        rec.drain(s);
    }
    void feed(const Bytes& bytes, TimeMs t) {
        s.bytesIn(ByteView{bytes.data(), bytes.size()}, t);
        rec.drain(s);
    }
    mc::Expected<mc::RequestId> submit(const Request& r, TimeMs t) {
        auto id = s.submit(r, t);
        rec.drain(s);
        return id;
    }
};

bool isSend(const RecordedOutput& o, const Bytes& bytes) {
    return o.kind == OutputKind::Send && o.bytes == bytes;
}

} // namespace

TEST_CASE("Serial harness: serialReadResponse3C is the golden V-3C1-02 for its three words and "
          "sums a second frame as spec 2.5 says") {
    CHECK(serialReadResponse3C({0x1995, 0x1202, 0x1130}) == vectorBytes("3c_f1.vec", "V-3C1-02"));

    // One word of 0000: the characters after STX through ETX sum to 0x2EE (route 0x22B, data
    // 4 x '0' = 0xC0, ETX 0x03), so the sum check is "EE".
    const Bytes one = serialReadResponse3C({0x0000});
    CHECK(std::string(one.begin(), one.end()) ==
          std::string("\x02") + "F90000FF000000" + std::string("\x03") + "EE");
}

TEST_CASE("SES-17 Serial timeout: EOT, then a flush that every byte restarts (a byte at +40 ms "
          "moves the end to +90 ms), then the read is resent with identical bytes") {
    FrameConfig frame = serialFrame();
    const Bytes request = vectorBytes("3c_f1.vec", "V-3C1-01");
    const Bytes response = vectorBytes("3c_f1.vec", "V-3C1-02");

    Rig r(frame);
    REQUIRE(r.s.subscribe(kD100, 3).hasValue());
    r.up(0);
    REQUIRE(r.rec.size() == 1);
    CHECK(isSend(r.rec[0], request)); // V-3C1-01
    CHECK(r.s.nextDeadline() == 1000);
    r.rec.clear();

    r.tick(999);
    CHECK(r.rec.empty());
    CHECK(r.s.stats().timeouts == 0);

    r.tick(1000); // The response deadline: no byte arrived.
    REQUIRE(r.rec.size() == 1);
    CHECK(isSend(r.rec[0], kEot));
    CHECK(r.s.stats().timeouts == 1);
    CHECK(r.s.stats().eotsSent == 1);
    CHECK(r.s.isLinkUp());
    CHECK_FALSE(r.s.isFaulted());
    CHECK(r.s.nextDeadline() == 1050); // Silence window: serialFlushMs after the EOT.
    r.rec.clear();

    r.feed({0xAA}, 1040); // Discarded, and the silence window restarts.
    CHECK(r.rec.empty());
    CHECK(r.s.nextDeadline() == 1090);
    r.tick(1089);
    CHECK(r.rec.empty());
    CHECK(r.s.stats().retries == 0);

    r.tick(1090); // Silent for 50 ms: the read is resent.
    REQUIRE(r.rec.size() == 1);
    CHECK(isSend(r.rec[0], request)); // Identical bytes.
    CHECK(r.s.stats().retries == 1);
    CHECK(r.s.stats().framesSent == 2); // The EOT is not a request frame.
    CHECK(r.s.nextDeadline() == 2090);  // A fresh response deadline.
    r.rec.clear();

    r.feed(response, 1100); // V-3C1-02
    REQUIRE(r.rec.size() == 2);
    CHECK(r.rec[0].kind == OutputKind::Snapshot);
    REQUIRE(r.rec[0].chunks.size() == 1);
    CHECK(r.rec[0].chunks[0].state == ChunkState::Ok);
    CHECK(r.rec[1].kind == OutputKind::CycleDone);
    CHECK(r.rec[1].cycle.round == 1);
    CHECK(r.rec[1].cycle.requests == 2); // The resend is counted.
    CHECK(r.rec[1].cycle.failedChunks == 0);
    CHECK(r.s.values().word(kD100) == 0x1995);
    CHECK(r.s.values().word(Device{DeviceType::D, 101}) == 0x1202);
    CHECK(r.s.values().word(Device{DeviceType::D, 102}) == 0x1130);
    // bytesSent: the request twice (33 bytes each, V-3C1-01) and one EOT byte.
    CHECK(r.s.stats().bytesSent == 33 + 1 + 33);
}

TEST_CASE("SES-17 Serial timeout: readRetries resends with identical bytes, then the chunk is "
          "Failed and the round completes") {
    FrameConfig frame = serialFrame(); // readRetries = 2
    SessionConfig cfg = serialConfig();
    cfg.maxConsecutiveLinkErrors = 10; // The link errors below must not fault the link.
    const Bytes request = vectorBytes("3c_f1.vec", "V-3C1-01");

    Rig r(frame, cfg);
    REQUIRE(r.s.subscribe(kD100, 3).hasValue());
    r.up(0); // Attempt 1.
    REQUIRE(r.rec.size() == 1);
    CHECK(isSend(r.rec[0], request));
    r.rec.clear();

    r.tick(1000); // Timeout 1.
    REQUIRE(r.rec.size() == 1);
    CHECK(isSend(r.rec[0], kEot));
    r.rec.clear();
    r.tick(1050); // Retry 1.
    REQUIRE(r.rec.size() == 1);
    CHECK(isSend(r.rec[0], request));
    r.rec.clear();

    r.tick(2050); // Timeout 2.
    REQUIRE(r.rec.size() == 1);
    CHECK(isSend(r.rec[0], kEot));
    r.rec.clear();
    r.tick(2100); // Retry 2.
    REQUIRE(r.rec.size() == 1);
    CHECK(isSend(r.rec[0], request));
    r.rec.clear();

    r.tick(3100); // Timeout 3: no retry is left.
    REQUIRE(r.rec.size() == 1);
    CHECK(isSend(r.rec[0], kEot));
    CHECK(r.s.plan().chunk(0).state == ChunkState::NotRead); // Still in flight through the flush.
    r.rec.clear();

    r.tick(3150); // The flush ends: the chunk fails, the round completes. Nothing is resent.
    REQUIRE(r.rec.size() == 2);
    CHECK(r.rec[0].kind == OutputKind::Snapshot);
    REQUIRE(r.rec[0].chunks.size() == 1);
    CHECK(r.rec[0].chunks[0].state == ChunkState::Failed);
    CHECK(r.rec[0].chunks[0].lastError.code == ErrorCode::Timeout);
    CHECK(r.rec[1].kind == OutputKind::CycleDone);
    CHECK(r.rec[1].cycle.round == 1);
    CHECK(r.rec[1].cycle.requests == 3);
    CHECK(r.rec[1].cycle.failedChunks == 1);
    CHECK(r.s.values().state(kD100) == PointState::NoValue); // Never zero-filled.
    CHECK_FALSE(r.s.isFaulted());
    CHECK(r.s.stats().timeouts == 3);
    CHECK(r.s.stats().retries == 2);
    CHECK(r.s.stats().eotsSent == 3);
    CHECK(r.s.stats().framesSent == 3);
    CHECK(r.s.nextDeadline() == 1'000'000); // Idle again, waiting for the next round.
}

TEST_CASE("SES-17 Serial flush: bytes every 10 ms end it at the effectiveTimeoutMs() cap, which "
          "counts as one more link error") {
    const Bytes request = vectorBytes("3c_f1.vec", "V-3C1-01");
    FrameConfig frame = serialFrame();
    frame.readRetries = 5;

    SUBCASE("the cap ends the flush and the read is resent; the next timeout is the third error") {
        Rig r(frame); // maxConsecutiveLinkErrors = 3 (default)
        REQUIRE(r.s.subscribe(kD100, 3).hasValue());
        r.up(0);
        r.rec.clear();
        r.tick(1000); // Link error 1; the flush begins and is capped at 2000.
        REQUIRE(r.rec.size() == 1);
        CHECK(isSend(r.rec[0], kEot));
        r.rec.clear();

        for (TimeMs t = 1010; t <= 1990; t += 10) {
            r.feed({0x55}, t);
            REQUIRE(r.rec.empty()); // Never silent for 50 ms.
        }
        CHECK(r.s.nextDeadline() == 2000); // The silence window would end at 2040, past the cap.
        r.tick(1999);
        CHECK(r.rec.empty());
        CHECK(r.s.stats().retries == 0);

        r.tick(2000); // The cap: link error 2, then the read is resent.
        REQUIRE(r.rec.size() == 1);
        CHECK(isSend(r.rec[0], request));
        CHECK(r.s.stats().retries == 1);
        CHECK_FALSE(r.s.isFaulted());
        CHECK(r.s.nextDeadline() == 3000);
        r.rec.clear();

        r.tick(3000); // Link error 3 reaches maxConsecutiveLinkErrors.
        REQUIRE(r.rec.size() == 2);
        CHECK(isSend(r.rec[0], kEot));
        CHECK(r.rec[1].kind == OutputKind::LinkFault);
        CHECK(r.rec[1].fault == LinkFaultKind::Timeout);
        CHECK(r.rec[1].reopenTransport == false);
        CHECK(r.s.isFaulted());
    }

    SUBCASE("a cap that reaches maxConsecutiveLinkErrors faults at the cap; the chunk keeps the "
            "error of the attempt that began the flush") {
        SessionConfig cfg = serialConfig();
        cfg.maxConsecutiveLinkErrors = 2;
        Rig r(frame, cfg);
        REQUIRE(r.s.subscribe(kD100, 3).hasValue());
        r.up(0);
        r.rec.clear();

        r.feed(vectorBytes("3c_f1.vec", "3C1-SUM"), 100); // A wrong SUM: link error 1.
        REQUIRE(r.rec.size() == 1);
        CHECK(isSend(r.rec[0], kEot));
        r.rec.clear();
        for (TimeMs t = 110; t <= 1090; t += 10) {
            r.feed({0x55}, t);
            REQUIRE(r.rec.empty());
        }
        CHECK(r.s.nextDeadline() == 1100); // The flush began at 100: cap = 100 + 1000.

        r.tick(1100); // The cap: link error 2. No second EOT.
        REQUIRE(r.rec.size() == 1);
        CHECK(r.rec[0].kind == OutputKind::LinkFault);
        CHECK(r.rec[0].fault == LinkFaultKind::Timeout);
        CHECK(r.rec[0].error.code == ErrorCode::Timeout);
        CHECK(r.rec[0].reopenTransport == false);
        CHECK(r.s.isFaulted());
        CHECK(r.s.nextDeadline() == kNoDeadline);
        CHECK(r.s.plan().chunk(0).state == ChunkState::Failed);
        CHECK(r.s.plan().chunk(0).lastError.code == ErrorCode::SumCheck);
        CHECK(r.s.stats().eotsSent == 1);
    }
}

TEST_CASE("SES-17 Serial: maxConsecutiveLinkErrors -> LinkFault{Timeout, reopen=false}; "
          "linkDown starts the count again") {
    FrameConfig frame = serialFrame();
    frame.readRetries = 5; // Retries remain: it is the error count that stops the engine.
    SessionConfig cfg = serialConfig();
    cfg.maxConsecutiveLinkErrors = 2;
    const Bytes request = vectorBytes("3c_f1.vec", "V-3C1-01");

    Rig r(frame, cfg);
    REQUIRE(r.s.subscribe(kD100, 3).hasValue());
    r.up(0);
    r.rec.clear();
    r.tick(1000); // Link error 1.
    REQUIRE(r.rec.size() == 1);
    CHECK(isSend(r.rec[0], kEot));
    r.rec.clear();
    r.tick(1050); // Resent.
    REQUIRE(r.rec.size() == 1);
    CHECK(isSend(r.rec[0], request));
    r.rec.clear();

    r.tick(2050); // Link error 2: the EOT still goes out, then the fault.
    REQUIRE(r.rec.size() == 2);
    CHECK(isSend(r.rec[0], kEot));
    CHECK(r.rec[1].kind == OutputKind::LinkFault);
    CHECK(r.rec[1].fault == LinkFaultKind::Timeout);
    CHECK(r.rec[1].error.code == ErrorCode::Timeout);
    CHECK(r.rec[1].reopenTransport == false);
    CHECK(r.s.isFaulted());
    CHECK(r.s.nextDeadline() == kNoDeadline);
    CHECK(r.s.plan().chunk(0).state == ChunkState::Failed);
    CHECK(r.s.plan().chunk(0).lastError.code == ErrorCode::Timeout);
    r.rec.clear();

    r.tick(9000); // Nothing is sent until linkDown + linkUp.
    CHECK(r.rec.empty());

    r.down(9010);
    CHECK(r.rec.empty());
    CHECK_FALSE(r.s.isFaulted());
    r.up(9020);
    REQUIRE(r.rec.size() == 1);
    CHECK(isSend(r.rec[0], request)); // Round 1 restarts.
    r.rec.clear();
    r.tick(10020); // One link error again, not two: linkDown reset the count.
    REQUIRE(r.rec.size() == 1);
    CHECK(isSend(r.rec[0], kEot));
    CHECK_FALSE(r.s.isFaulted());
}

TEST_CASE("SES-17 Serial EOT: 04 (Formats 1-3), 04 0D 0A (Format 4); none when sendEotOnError is "
          "off, but the flush still runs") {
    SUBCASE("every format") {
        const struct {
            SerialFormat format;
            const char* file;
            const char* request;
            const char* response;
            const Bytes* eot;
        } formats[] = {
            {SerialFormat::Format1, "3c_f1.vec", "V-3C1-01", "V-3C1-02", &kEot},
            {SerialFormat::Format2, "3c_f2.vec", "V-3C2-01", "V-3C2-02", &kEot},
            {SerialFormat::Format3, "3c_f3.vec", "V-3C3-01", "V-3C3-02", &kEot},
            {SerialFormat::Format4, "3c_f4.vec", "V-3C4-01", "V-3C4-02", &kEotCrLf},
        };
        for (const auto& f : formats) {
            INFO("format ", static_cast<int>(f.format));
            const Bytes request = vectorBytes(f.file, f.request);
            const Bytes response = vectorBytes(f.file, f.response);

            Rig r(serialFrame(f.format));
            REQUIRE(r.s.subscribe(kD100, 3).hasValue());
            r.up(0);
            REQUIRE(r.rec.size() == 1);
            CHECK(isSend(r.rec[0], request));
            r.rec.clear();

            r.tick(1000);
            REQUIRE(r.rec.size() == 1);
            CHECK(isSend(r.rec[0], *f.eot));
            r.rec.clear();

            r.tick(1050); // The resend is the vector's request again, byte for byte.
            REQUIRE(r.rec.size() == 1);
            CHECK(isSend(r.rec[0], request));
            r.rec.clear();

            r.feed(response, 1100); // The vector's response completes the round.
            REQUIRE(r.rec.size() == 2);
            CHECK(r.rec[0].kind == OutputKind::Snapshot);
            CHECK(r.rec[1].kind == OutputKind::CycleDone);
            CHECK(r.rec[1].cycle.failedChunks == 0);
            CHECK(r.s.values().word(kD100) == 0x1995);
            CHECK(r.s.values().word(Device{DeviceType::D, 102}) == 0x1130);
        }
    }

    SUBCASE("sendEotOnError off: no EOT, the flush still discards and waits for silence") {
        FrameConfig frame = serialFrame();
        frame.sendEotOnError = false;
        const Bytes request = vectorBytes("3c_f1.vec", "V-3C1-01");

        Rig r(frame);
        REQUIRE(r.s.subscribe(kD100, 3).hasValue());
        r.up(0);
        r.rec.clear();
        r.tick(1000);
        CHECK(r.rec.empty());
        CHECK(r.s.stats().eotsSent == 0);
        CHECK(r.s.stats().timeouts == 1);
        CHECK(r.s.nextDeadline() == 1050);
        r.feed({0xAA}, 1030);
        CHECK(r.rec.empty());
        CHECK(r.s.nextDeadline() == 1080);
        r.tick(1080);
        REQUIRE(r.rec.size() == 1);
        CHECK(isSend(r.rec[0], request));
        CHECK(r.s.stats().bytesSent == 33 + 33); // No EOT byte.
    }
}

TEST_CASE("SES-18 Serial timeout on a write: EOT, RequestDone{Timeout}, never resent") {
    FrameConfig frame = serialFrame(); // readRetries = 2: a write must not use them.
    const Bytes writeRequest = vectorBytes("3c_f1.vec", "V-3C1-03");
    const uint8_t bits[8] = {1, 1, 0, 0, 1, 1, 0, 0};

    SUBCASE("ad-hoc write") {
        Rig r(frame); // No subscription: round 1 is empty and ends at once.
        r.up(0);
        REQUIRE(r.rec.size() == 1);
        CHECK(r.rec[0].kind == OutputKind::CycleDone);
        r.rec.clear();

        auto id = r.submit(Request::writeBits(kM100, ByteView{bits, 8}), 10);
        REQUIRE(id.hasValue());
        REQUIRE(r.rec.size() == 1);
        CHECK(isSend(r.rec[0], writeRequest)); // V-3C1-03
        CHECK(r.s.nextDeadline() == 1010);
        r.rec.clear();

        r.tick(1010);
        REQUIRE(r.rec.size() == 1);
        CHECK(isSend(r.rec[0], kEot));
        r.rec.clear();

        r.tick(1060); // The flush ends: the write fails; it is not resent.
        REQUIRE(r.rec.size() == 1);
        CHECK(r.rec[0].kind == OutputKind::RequestDone);
        CHECK(r.rec[0].requestId == id.value());
        CHECK(r.rec[0].error.code == ErrorCode::Timeout);
        CHECK(r.rec[0].payload.empty());
        CHECK(r.s.stats().framesSent == 1);
        CHECK(r.s.stats().retries == 0);
        CHECK_FALSE(r.s.isFaulted());
        CHECK(r.s.nextDeadline() == 1'000'000);
        r.rec.clear();

        r.tick(500'000); // Still nothing: no late resend.
        CHECK(r.rec.empty());
    }

    SUBCASE("the heartbeat write: heartbeatOk false, no RequestDone, never resent") {
        SessionConfig cfg = serialConfig();
        cfg.heartbeat.enabled = true;
        cfg.heartbeat.device = Device{DeviceType::M, 2000};
        Rig r(frame, cfg);
        r.up(0);
        REQUIRE(r.rec.size() == 1);
        CHECK(r.rec[0].kind == OutputKind::Send); // Round 1's heartbeat write.
        r.rec.clear();

        r.tick(1000);
        REQUIRE(r.rec.size() == 1);
        CHECK(isSend(r.rec[0], kEot));
        r.rec.clear();

        r.tick(1050);
        REQUIRE(r.rec.size() == 1);
        CHECK(r.rec[0].kind == OutputKind::CycleDone);
        CHECK(r.rec[0].cycle.round == 1);
        CHECK(r.rec[0].cycle.heartbeatOk == false);
        CHECK(r.rec[0].cycle.requests == 1);
        CHECK(r.s.stats().framesSent == 1);
        CHECK(r.s.stats().retries == 0);
    }
}

TEST_CASE("SES-19 Serial wrong SUM: EOT, flush, the read is resent and succeeds") {
    FrameConfig frame = serialFrame();
    const Bytes request = vectorBytes("3c_f1.vec", "V-3C1-01");
    const Bytes response = vectorBytes("3c_f1.vec", "V-3C1-02");
    const Bytes wrongSum = vectorBytes("3c_f1.vec", "3C1-SUM");

    Rig r(frame);
    REQUIRE(r.s.subscribe(kD100, 3).hasValue());
    r.up(0);
    r.rec.clear();

    r.feed(wrongSum, 10); // Complete, well-delimited, but the SUM is wrong.
    REQUIRE(r.rec.size() == 1);
    CHECK(isSend(r.rec[0], kEot));
    CHECK(r.s.stats().protocolErrors == 1);
    CHECK(r.s.stats().timeouts == 0);
    CHECK_FALSE(r.s.isFaulted());
    CHECK(r.s.nextDeadline() == 60);
    r.rec.clear();

    r.tick(60);
    REQUIRE(r.rec.size() == 1);
    CHECK(isSend(r.rec[0], request));
    CHECK(r.s.stats().retries == 1);
    r.rec.clear();

    r.feed(response, 70);
    REQUIRE(r.rec.size() == 2);
    CHECK(r.rec[0].kind == OutputKind::Snapshot);
    CHECK(r.rec[0].chunks[0].state == ChunkState::Ok);
    CHECK(r.rec[1].kind == OutputKind::CycleDone);
    CHECK(r.rec[1].cycle.failedChunks == 0);
    CHECK(r.rec[1].cycle.requests == 2);
    CHECK(r.s.values().word(kD100) == 0x1995);
}

TEST_CASE("SES-19 Serial protocol error on a write: EOT, then RequestDone with the protocol "
          "error; never resent") {
    FrameConfig frame = serialFrame();
    const Bytes writeRequest = vectorBytes("3c_f1.vec", "V-3C1-03");
    const Bytes dataToWrite = vectorBytes("3c_f1.vec", "3C1-DATAWRITE"); // LengthMismatch
    const uint8_t bits[8] = {1, 1, 0, 0, 1, 1, 0, 0};

    Rig r(frame);
    r.up(0);
    r.rec.clear();
    auto id = r.submit(Request::writeBits(kM100, ByteView{bits, 8}), 10);
    REQUIRE(id.hasValue());
    REQUIRE(r.rec.size() == 1);
    CHECK(isSend(r.rec[0], writeRequest));
    r.rec.clear();

    r.feed(dataToWrite, 100);
    REQUIRE(r.rec.size() == 1);
    CHECK(isSend(r.rec[0], kEot));
    CHECK(r.s.stats().protocolErrors == 1);
    r.rec.clear();

    r.tick(150);
    REQUIRE(r.rec.size() == 1);
    CHECK(r.rec[0].kind == OutputKind::RequestDone);
    CHECK(r.rec[0].requestId == id.value());
    CHECK(r.rec[0].error.code == ErrorCode::LengthMismatch);
    CHECK(r.s.stats().framesSent == 1);
    CHECK(r.s.stats().retries == 0);
    CHECK_FALSE(r.s.isFaulted());
}

TEST_CASE("SES-19 Serial NAK: a PLC error is not a link error (no EOT, no resend, the chunk "
          "fails with the PLC code)") {
    FrameConfig frame = serialFrame();
    const Bytes request = vectorBytes("3c_f1.vec", "V-3C1-01");
    const Bytes nak = vectorBytes("3c_f1.vec", "V-3C1-05"); // NAK, error code 7151

    Rig r(frame);
    REQUIRE(r.s.subscribe(kD100, 3).hasValue());
    r.up(0);
    r.rec.clear();

    r.feed(nak, 10);
    REQUIRE(r.rec.size() == 2);
    CHECK(r.rec[0].kind == OutputKind::Snapshot);
    REQUIRE(r.rec[0].chunks.size() == 1);
    CHECK(r.rec[0].chunks[0].state == ChunkState::Failed);
    CHECK(r.rec[0].chunks[0].lastError.code == ErrorCode::PlcError);
    CHECK(r.rec[0].chunks[0].lastError.plcCode == 0x7151);
    CHECK(r.rec[1].kind == OutputKind::CycleDone);
    CHECK(r.rec[1].cycle.requests == 1);
    CHECK(r.rec[1].cycle.failedChunks == 1);
    CHECK(r.s.stats().plcErrors == 1);
    CHECK(r.s.stats().eotsSent == 0);
    CHECK(r.s.stats().retries == 0);
}

TEST_CASE("SES-20 Serial: a well-formed response (a PLC error, or data) resets the "
          "consecutive-error count") {
    SessionConfig cfg = serialConfig();
    cfg.cycleIntervalMs = 100;
    cfg.maxConsecutiveLinkErrors = 2;
    const Bytes request = vectorBytes("3c_f1.vec", "V-3C1-01");
    const Bytes response = vectorBytes("3c_f1.vec", "V-3C1-02");
    const Bytes nak = vectorBytes("3c_f1.vec", "V-3C1-05");

    SUBCASE("a PLC error response") {
        FrameConfig frame = serialFrame();
        frame.readRetries = 0;
        Rig r(frame, cfg);
        REQUIRE(r.s.subscribe(kD100, 3).hasValue());
        r.up(0);
        r.rec.clear();

        r.tick(1000); // Link error 1.
        REQUIRE(r.rec.size() == 1);
        CHECK(isSend(r.rec[0], kEot));
        r.rec.clear();
        r.tick(1050); // The chunk fails (no retries); round 2 starts at once (FixedRate).
        REQUIRE(r.rec.size() == 3);
        CHECK(r.rec[0].kind == OutputKind::Snapshot);
        CHECK(r.rec[1].kind == OutputKind::CycleDone);
        CHECK(isSend(r.rec[2], request));
        r.rec.clear();

        r.feed(nak, 1100); // A well-formed response: the count goes back to 0.
        REQUIRE(r.rec.size() == 2);
        CHECK(r.rec[0].kind == OutputKind::Snapshot);
        CHECK(r.rec[1].kind == OutputKind::CycleDone);
        r.rec.clear();
        r.tick(1150); // Round 3.
        REQUIRE(r.rec.size() == 1);
        CHECK(isSend(r.rec[0], request));
        r.rec.clear();

        r.tick(2150); // Link error 1 again, not 2.
        REQUIRE(r.rec.size() == 1);
        CHECK(isSend(r.rec[0], kEot));
        CHECK_FALSE(r.s.isFaulted());
    }

    SUBCASE("a data response") {
        FrameConfig frame = serialFrame();
        Rig r(frame, cfg);
        REQUIRE(r.s.subscribe(kD100, 3).hasValue());
        r.up(0);
        r.rec.clear();

        r.tick(1000); // Link error 1.
        r.rec.clear();
        r.tick(1050); // Resent.
        REQUIRE(r.rec.size() == 1);
        CHECK(isSend(r.rec[0], request));
        r.rec.clear();
        r.feed(response, 1100); // Round 1 completes; round 2 starts at once.
        REQUIRE(r.rec.size() == 3);
        CHECK(r.rec[1].kind == OutputKind::CycleDone);
        CHECK(isSend(r.rec[2], request));
        r.rec.clear();

        r.tick(2100); // Link error 1 again, not 2.
        REQUIRE(r.rec.size() == 1);
        CHECK(isSend(r.rec[0], kEot));
        CHECK_FALSE(r.s.isFaulted());
    }
}

TEST_CASE("SES-24 Serial unsolicited bytes: idle -> discarded and logged at Warn; flushing, "
          "faulted and down -> discarded") {
    FrameConfig frame = serialFrame();
    const Bytes junk = {1, 2, 3, 4};
    RecordingLogSink sink;
    SessionConfig cfg = serialConfig();
    cfg.log = &sink;

    SUBCASE("Idle") {
        Rig r(frame, cfg); // No subscription: round 1 ends at once, leaving Idle.
        r.up(0);
        REQUIRE(r.rec.size() == 1);
        CHECK(r.rec[0].kind == OutputKind::CycleDone);
        r.rec.clear();
        sink.clear();

        r.feed(junk, 10);
        CHECK(r.rec.empty());
        CHECK_FALSE(r.s.isFaulted());
        CHECK(sink.any(LogLevel::Warn));
        CHECK(r.s.stats().bytesReceived == 4);
        CHECK(r.s.stats().protocolErrors == 0);
        CHECK(r.s.nextDeadline() == 1'000'000); // Still Idle, waiting for the next round.
    }

    SUBCASE("Flushing") {
        Rig r(frame, cfg);
        REQUIRE(r.s.subscribe(kD100, 3).hasValue());
        r.up(0);
        r.tick(1000); // EOT, flushing.
        r.rec.clear();
        sink.clear();

        r.feed(junk, 1010);
        CHECK(r.rec.empty());
        CHECK_FALSE(r.s.isFaulted());
        CHECK(sink.any(LogLevel::Trace));
        CHECK_FALSE(sink.any(LogLevel::Warn));
        CHECK(r.s.nextDeadline() == 1060);
    }

    SUBCASE("Faulted") {
        cfg.maxConsecutiveLinkErrors = 1; // The first link error faults.
        Rig r(frame, cfg);
        REQUIRE(r.s.subscribe(kD100, 3).hasValue());
        r.up(0);
        r.tick(1000);
        CHECK(r.s.isFaulted());
        r.rec.clear();
        sink.clear();

        r.feed(junk, 1010);
        CHECK(r.rec.empty());
        CHECK(r.s.isFaulted());
        CHECK(r.s.nextDeadline() == kNoDeadline);
        CHECK(sink.any(LogLevel::Trace));
    }

    SUBCASE("Down") {
        Rig r(frame, cfg); // Never linked up.
        r.feed(junk, 10);
        CHECK(r.rec.empty());
        CHECK_FALSE(r.s.isFaulted());
        CHECK(r.s.nextDeadline() == kNoDeadline);
    }
}

TEST_CASE("SES-27 Serial deadlines: the response timeout runs to the first byte only; after it "
          "only the gap between bytes counts") {
    SUBCASE("no first byte within the timeout (default 3000 ms for serial)") {
        FrameConfig frame = FrameConfig::frame3C();
        frame.readRetries = 2;
        Rig r(frame);
        REQUIRE(r.s.subscribe(kD100, 3).hasValue());
        r.up(0);
        r.rec.clear();
        CHECK(r.s.nextDeadline() == 3000);
        r.tick(2999);
        CHECK(r.rec.empty());
        r.tick(3000);
        REQUIRE(r.rec.size() == 1);
        CHECK(isSend(r.rec[0], kEot));
        CHECK(r.s.stats().timeouts == 1);
    }

    SUBCASE("a response trickling one byte every 50 ms for 10 s completes") {
        FrameConfig frame = serialFrame();
        Rig r(frame);
        REQUIRE(r.s.subscribe(kD100, 48).hasValue());
        r.up(0);
        REQUIRE(r.rec.size() == 1);
        REQUIRE(r.rec[0].kind == OutputKind::Send);
        r.rec.clear();

        std::vector<uint16_t> words(48);
        for (size_t i = 0; i < words.size(); ++i) {
            words[i] = static_cast<uint16_t>(0x1000 + i);
        }
        const Bytes response = serialReadResponse3C(words);
        // Spec 5.5: STX + 10 route characters + 48 words x 4 characters + ETX + 2 SUM characters.
        REQUIRE(response.size() == 1 + 10 + 192 + 1 + 2);

        TimeMs t = 0;
        for (size_t i = 0; i < response.size(); ++i) {
            t += 50;
            r.tick(t); // Never early: the deadline is the previous byte + 100 ms.
            REQUIRE(r.rec.empty());
            r.feed(Bytes{response[i]}, t);
            if (i + 1 < response.size()) {
                REQUIRE(r.rec.empty());
                CHECK(r.s.nextDeadline() == t + 100);
            }
        }
        CHECK(t == 10300); // 206 bytes x 50 ms: far past the 1000 ms first-byte timeout.
        REQUIRE(r.rec.size() == 2);
        CHECK(r.rec[0].kind == OutputKind::Snapshot);
        CHECK(r.rec[0].chunks[0].state == ChunkState::Ok);
        CHECK(r.rec[1].kind == OutputKind::CycleDone);
        CHECK(r.rec[1].cycle.failedChunks == 0);
        CHECK(r.s.stats().timeouts == 0);
        CHECK(r.s.values().word(Device{DeviceType::D, 100 + 47}) == 0x1000 + 47);
    }

    SUBCASE("a response that stops after its first 20 bytes times out serialInterCharMs after the "
            "20th byte") {
        FrameConfig frame = serialFrame();
        const Bytes response = vectorBytes("3c_f1.vec", "V-3C1-02");
        REQUIRE(response.size() == 26);
        const Bytes first20(response.begin(), response.begin() + 20);

        Rig r(frame);
        REQUIRE(r.s.subscribe(kD100, 3).hasValue());
        r.up(0);
        r.rec.clear();

        r.feed(first20, 100);
        CHECK(r.rec.empty());
        CHECK(r.s.nextDeadline() == 200); // Not the 1000 ms of the send.
        r.tick(199);
        CHECK(r.rec.empty());
        CHECK(r.s.stats().timeouts == 0);
        r.tick(200);
        REQUIRE(r.rec.size() == 1);
        CHECK(isSend(r.rec[0], kEot));
        CHECK(r.s.stats().timeouts == 1);
    }

    SUBCASE("serialInterCharMs is the configured value") {
        FrameConfig frame = serialFrame();
        SessionConfig cfg = serialConfig();
        cfg.serialInterCharMs = 250;
        const Bytes response = vectorBytes("3c_f1.vec", "V-3C1-02");
        const Bytes first20(response.begin(), response.begin() + 20);

        Rig r(frame, cfg);
        REQUIRE(r.s.subscribe(kD100, 3).hasValue());
        r.up(0);
        r.rec.clear();
        r.feed(first20, 100);
        CHECK(r.s.nextDeadline() == 350);
        r.tick(349);
        CHECK(r.rec.empty());
        r.tick(350);
        REQUIRE(r.rec.size() == 1);
        CHECK(isSend(r.rec[0], kEot));
    }

    SUBCASE("Ethernet keeps one overall deadline: received bytes do not move it") {
        FrameConfig frame = FrameConfig::frame3E(); // monitoringTimer 0x10 x 250 ms + 1000 ms
        Session s = makeSession(frame);
        OutputRecorder rec;
        ScriptedPeer peer(frame);
        REQUIRE(s.subscribe(kD100, 1).hasValue());
        s.linkUp(0);
        rec.drain(s);
        rec.clear();
        CHECK(s.nextDeadline() == 5000);

        auto bytes = peer.respond(Request::readWords(kD100, 1), PeerScript{});
        REQUIRE(bytes.size() > 7);
        s.bytesIn(ByteView{bytes.data(), 5}, 1000); // A partial response.
        rec.drain(s);
        CHECK(rec.empty());
        CHECK(s.nextDeadline() == 5000);
        s.bytesIn(ByteView{bytes.data() + 5, 2}, 4000);
        rec.drain(s);
        CHECK(rec.empty());
        CHECK(s.nextDeadline() == 5000);

        s.tick(5000);
        rec.drain(s);
        REQUIRE(rec.size() == 1);
        CHECK(rec[0].kind == OutputKind::LinkFault);
        CHECK(rec[0].fault == LinkFaultKind::Timeout);
        CHECK(rec[0].reopenTransport == true);
    }
}

TEST_CASE("SES-23 Serial: a response delivered one byte at a time yields the same outputs as "
          "delivered whole") {
    FrameConfig frame = serialFrame();
    const Bytes response = vectorBytes("3c_f1.vec", "V-3C1-02");

    Rig whole(frame);
    REQUIRE(whole.s.subscribe(kD100, 3).hasValue());
    whole.up(0);
    whole.rec.clear();
    whole.feed(response, 30);

    Rig bytewise(frame);
    REQUIRE(bytewise.s.subscribe(kD100, 3).hasValue());
    bytewise.up(0);
    bytewise.rec.clear();
    for (size_t i = 0; i < response.size(); ++i) {
        bytewise.feed(Bytes{response[i]}, 5 + i);
        if (i + 1 < response.size()) {
            REQUIRE(bytewise.rec.empty());
        }
    }

    REQUIRE(whole.rec.size() == 2);
    REQUIRE(bytewise.rec.size() == whole.rec.size());
    for (size_t i = 0; i < whole.rec.size(); ++i) {
        INFO("output ", i);
        CHECK(bytewise.rec[i].kind == whole.rec[i].kind);
        CHECK(bytewise.rec[i].deviceType == whole.rec[i].deviceType);
        CHECK(bytewise.rec[i].round == whole.rec[i].round);
        CHECK(bytewise.rec[i].cycle.round == whole.rec[i].cycle.round);
        CHECK(bytewise.rec[i].cycle.requests == whole.rec[i].cycle.requests);
        CHECK(bytewise.rec[i].cycle.failedChunks == whole.rec[i].cycle.failedChunks);
        REQUIRE(bytewise.rec[i].chunks.size() == whole.rec[i].chunks.size());
        for (size_t c = 0; c < whole.rec[i].chunks.size(); ++c) {
            CHECK(bytewise.rec[i].chunks[c].state == whole.rec[i].chunks[c].state);
        }
    }
    CHECK(bytewise.s.values().word(kD100) == whole.s.values().word(kD100));
    CHECK(bytewise.s.stats().eotsSent == 0);
}

TEST_CASE("Serial receive buffer: junk before the start byte is skipped and never counts; more "
          "than the largest response after it is the overflow row (discard, EOT, flush)") {
    FrameConfig frame = serialFrame();
    const Bytes request = vectorBytes("3c_f1.vec", "V-3C1-01");
    const Bytes response = vectorBytes("3c_f1.vec", "V-3C1-02");
    // The largest response to a read of 3 words is the data frame (spec 5.5): 26 bytes, the
    // length of V-3C1-02; the NAK frame is shorter.
    REQUIRE(response.size() == 26);

    SUBCASE("junk bytes and then a valid maximum-size response: Done, no EOT, no retry") {
        for (size_t junkSize : {size_t{1}, size_t{5}, size_t{40}}) {
            INFO("junk bytes ", junkSize);
            Rig r(frame);
            REQUIRE(r.s.subscribe(kD100, 3).hasValue());
            r.up(0);
            r.rec.clear();

            Bytes wire(junkSize, 0x55);
            wire.insert(wire.end(), response.begin(), response.end());
            r.feed(wire, 10);
            REQUIRE(r.rec.size() == 2);
            CHECK(r.rec[0].kind == OutputKind::Snapshot);
            CHECK(r.rec[0].chunks[0].state == ChunkState::Ok);
            CHECK(r.rec[1].kind == OutputKind::CycleDone);
            CHECK(r.rec[1].cycle.requests == 1);
            CHECK(r.rec[1].cycle.failedChunks == 0);
            CHECK(r.s.values().word(kD100) == 0x1995);
            CHECK(r.s.stats().eotsSent == 0);
            CHECK(r.s.stats().retries == 0);
            CHECK(r.s.stats().protocolErrors == 0);
            CHECK(r.s.stats().timeouts == 0);
        }
    }

    SUBCASE("the junk may arrive in earlier calls than the frame") {
        Rig r(frame);
        REQUIRE(r.s.subscribe(kD100, 3).hasValue());
        r.up(0);
        r.rec.clear();
        r.feed(Bytes(30, 0x55), 10); // longer than any response, but no start byte yet
        r.feed(Bytes(30, 0x55), 20);
        CHECK(r.rec.empty());
        CHECK(r.s.stats().protocolErrors == 0);
        CHECK(r.s.nextDeadline() == 1000); // junk alone is no response start: first-byte deadline
        r.feed({0x55}, 30);
        r.feed(response, 40);
        REQUIRE(r.rec.size() == 2);
        CHECK(r.rec[0].chunks[0].state == ChunkState::Ok);
        CHECK(r.s.stats().eotsSent == 0);
    }

    SUBCASE("noise without a start byte never overflows, however long") {
        Rig r(frame);
        REQUIRE(r.s.subscribe(kD100, 3).hasValue());
        r.up(0);
        r.rec.clear();
        TimeMs t = 0;
        for (int i = 0; i < 200; ++i) {
            t += 10;
            r.feed({0x55}, t);
            REQUIRE(r.rec.empty());
        }
        CHECK(r.s.stats().protocolErrors == 0);
        CHECK(r.s.stats().eotsSent == 0);
    }

    SUBCASE("noise every 10 ms without a start byte times out at the first-byte deadline") {
        Rig r(frame);
        REQUIRE(r.s.subscribe(kD100, 3).hasValue());
        r.up(0);
        r.rec.clear();
        for (TimeMs t = 10; t <= 990; t += 10) {
            r.feed({0x55}, t);
            r.tick(t);
            REQUIRE(r.rec.empty());
            REQUIRE(r.s.nextDeadline() == 1000);
        }
        r.tick(1000); // the normal timeout row: EOT, flush, then the read is resent
        REQUIRE(r.rec.size() == 1);
        CHECK(isSend(r.rec[0], kEot));
        CHECK(r.s.stats().timeouts == 1);
        CHECK(r.s.stats().protocolErrors == 0);
        r.rec.clear();
        r.tick(1050);
        REQUIRE(r.rec.size() == 1);
        CHECK(isSend(r.rec[0], request));
    }

    SUBCASE("junk and then a start byte: the inter-character deadline runs from the start byte") {
        Rig r(frame);
        REQUIRE(r.s.subscribe(kD100, 3).hasValue());
        r.up(0);
        r.rec.clear();
        r.feed(Bytes(5, 0x55), 10);
        CHECK(r.s.nextDeadline() == 1000);
        r.feed({0x02}, 500); // STX: the response has started
        CHECK(r.s.nextDeadline() == 600);
        r.tick(599);
        CHECK(r.rec.empty());
        r.tick(600);
        REQUIRE(r.rec.size() == 1);
        CHECK(isSend(r.rec[0], kEot));
        CHECK(r.s.stats().timeouts == 1);
    }

    SUBCASE("as many bytes as the largest response after the start byte are not an overflow") {
        Rig r(frame);
        REQUIRE(r.s.subscribe(kD100, 3).hasValue());
        r.up(0);
        r.rec.clear();
        Bytes wire(7, 0x55);              // junk
        wire.push_back(0x02);             // STX
        wire.insert(wire.end(), 25, 'F'); // 26 bytes from the start byte, no ETX yet
        r.feed(wire, 10);
        CHECK(r.rec.empty());
        CHECK(r.s.stats().protocolErrors == 0);
        CHECK(r.s.nextDeadline() == 110);
    }

    SUBCASE("one byte more after the start byte is: EOT, flush, the read is resent") {
        Rig r(frame);
        REQUIRE(r.s.subscribe(kD100, 3).hasValue());
        r.up(0);
        r.rec.clear();
        Bytes wire(7, 0x55);              // junk, which does not count
        wire.push_back(0x02);             // STX
        wire.insert(wire.end(), 26, 'F'); // 27 bytes from the start byte
        r.feed(wire, 10);
        REQUIRE(r.rec.size() == 1);
        CHECK(isSend(r.rec[0], kEot));
        CHECK(r.s.stats().protocolErrors == 1);
        CHECK(r.s.stats().timeouts == 0);
        CHECK_FALSE(r.s.isFaulted());
        CHECK(r.s.nextDeadline() == 60);
        r.rec.clear();

        r.tick(60);
        REQUIRE(r.rec.size() == 1);
        CHECK(isSend(r.rec[0], request));
    }

    SUBCASE("a line that starts a frame and never stops sending is ended by the overflow") {
        Rig r(frame);
        REQUIRE(r.s.subscribe(kD100, 3).hasValue());
        r.up(0);
        r.rec.clear();
        TimeMs t = 10;
        r.feed({0x55}, t); // junk, not counted
        t += 10;
        r.feed({0x02}, t); // the start byte: byte 1 of 26
        for (int i = 0; i < 25; ++i) {
            t += 10;
            r.feed({'F'}, t);
            REQUIRE(r.rec.empty());
        }
        t += 10;
        r.feed({'F'}, t); // the 27th byte from the start byte
        REQUIRE(r.rec.size() == 1);
        CHECK(isSend(r.rec[0], kEot));
        CHECK(t == 280);
    }
}

TEST_CASE("SES-17 Serial: an ad-hoc read is resent with identical bytes (second chunk of a "
          "multi-chunk read included); linkDown while flushing completes the queue exactly once") {
    FrameConfig frame = serialFrame();

    SUBCASE("a single-chunk ad-hoc read") {
        const Bytes request = vectorBytes("3c_f1.vec", "V-3C1-01");
        const Bytes response = vectorBytes("3c_f1.vec", "V-3C1-02");
        Rig r(frame);
        r.up(0);
        r.rec.clear();

        auto id = r.submit(Request::readWords(kD100, 3), 10);
        REQUIRE(id.hasValue());
        REQUIRE(r.rec.size() == 1);
        CHECK(isSend(r.rec[0], request));
        r.rec.clear();

        r.tick(1010);
        REQUIRE(r.rec.size() == 1);
        CHECK(isSend(r.rec[0], kEot));
        r.rec.clear();
        r.tick(1060);
        REQUIRE(r.rec.size() == 1);
        CHECK(isSend(r.rec[0], request));
        r.rec.clear();

        r.feed(response, 1100);
        REQUIRE(r.rec.size() == 1);
        CHECK(r.rec[0].kind == OutputKind::RequestDone);
        CHECK(r.rec[0].requestId == id.value());
        CHECK(r.rec[0].error.code == ErrorCode::Ok);
        CHECK(r.rec[0].payload == Bytes{0x95, 0x19, 0x02, 0x12, 0x30, 0x11});
    }

    SUBCASE("the second chunk of D0 x 1000 (960 + 40 words) is the one that is resent") {
        Rig r(frame);
        r.up(0);
        r.rec.clear();

        auto id = r.submit(Request::readWords(Device{DeviceType::D, 0}, 1000), 10);
        REQUIRE(id.hasValue());
        REQUIRE(r.rec.size() == 1);
        REQUIRE(r.rec[0].kind == OutputKind::Send);
        const Bytes first = r.rec[0].bytes;
        r.rec.clear();

        std::vector<uint16_t> words(1000);
        for (size_t i = 0; i < words.size(); ++i) {
            words[i] = static_cast<uint16_t>(i * 7);
        }
        r.feed(serialReadResponse3C(std::vector<uint16_t>(words.begin(), words.begin() + 960)),
               100);
        REQUIRE(r.rec.size() == 1); // The second chunk goes out.
        REQUIRE(r.rec[0].kind == OutputKind::Send);
        const Bytes second = r.rec[0].bytes;
        CHECK(second != first);
        r.rec.clear();

        r.tick(1100); // The second chunk times out.
        REQUIRE(r.rec.size() == 1);
        CHECK(isSend(r.rec[0], kEot));
        r.rec.clear();
        r.tick(1150);
        REQUIRE(r.rec.size() == 1);
        CHECK(isSend(r.rec[0], second)); // Not the first chunk's frame, not the next one's.
        r.rec.clear();

        r.feed(serialReadResponse3C(std::vector<uint16_t>(words.begin() + 960, words.end())), 1200);
        REQUIRE(r.rec.size() == 1);
        CHECK(r.rec[0].kind == OutputKind::RequestDone);
        CHECK(r.rec[0].requestId == id.value());
        CHECK(r.rec[0].error.code == ErrorCode::Ok);
        REQUIRE(r.rec[0].payload.size() == 2000);
        bool payloadOk = true;
        for (size_t i = 0; i < words.size(); ++i) {
            payloadOk = payloadOk && r.rec[0].payload[2 * i] == (words[i] & 0xFF) &&
                        r.rec[0].payload[2 * i + 1] == (words[i] >> 8);
        }
        CHECK(payloadOk);
        CHECK(r.s.stats().retries == 1);
    }

    SUBCASE("linkDown while flushing: every request completes once with LinkDown, in order; the "
            "next link starts with a clean count") {
        SessionConfig cfg = serialConfig();
        cfg.maxConsecutiveLinkErrors = 2;
        FrameConfig f = serialFrame();
        const Bytes request = vectorBytes("3c_f1.vec", "V-3C1-01");
        const uint8_t bits[8] = {1, 1, 0, 0, 1, 1, 0, 0};
        Rig r(f, cfg);
        r.up(0);
        r.rec.clear();

        auto id0 = r.submit(Request::writeBits(kM100, ByteView{bits, 8}), 10);
        auto id1 = r.submit(Request::writeBits(kM100, ByteView{bits, 8}), 11);
        REQUIRE(id0.hasValue());
        REQUIRE(id1.hasValue());
        r.rec.clear();
        r.tick(1010); // The first write times out: link error 1, flushing.
        REQUIRE(r.rec.size() == 1);
        CHECK(isSend(r.rec[0], kEot));
        r.rec.clear();

        r.down(1020);
        REQUIRE(r.rec.size() == 2);
        CHECK(r.rec[0].kind == OutputKind::RequestDone);
        CHECK(r.rec[0].requestId == id0.value());
        CHECK(r.rec[0].error.code == ErrorCode::LinkDown);
        CHECK(r.rec[1].kind == OutputKind::RequestDone);
        CHECK(r.rec[1].requestId == id1.value());
        CHECK(r.rec[1].error.code == ErrorCode::LinkDown);
        CHECK(r.s.nextDeadline() == kNoDeadline);
        r.rec.clear();

        REQUIRE(r.s.subscribe(kD100, 3).hasValue());
        r.up(2000);
        REQUIRE(r.rec.size() == 1);
        CHECK(isSend(r.rec[0], request));
        r.rec.clear();
        r.tick(3000); // Link error 1 of the new link (the old count did not survive linkDown).
        REQUIRE(r.rec.size() == 1);
        CHECK(isSend(r.rec[0], kEot));
        CHECK_FALSE(r.s.isFaulted());
    }
}

TEST_CASE("SES-16 Serial fault: the in-flight write completes with Timeout and every queued "
          "request with LinkDown, in submission order, then LinkFault{reopen=false}") {
    FrameConfig frame = serialFrame();
    const uint8_t bits[8] = {1, 1, 0, 0, 1, 1, 0, 0};

    SUBCASE("the error count is reached at the timeout: EOT first, then the completions") {
        SessionConfig cfg = serialConfig();
        cfg.maxConsecutiveLinkErrors = 1;
        Rig r(frame, cfg);
        r.up(0);
        r.rec.clear();
        auto id0 = r.submit(Request::writeBits(kM100, ByteView{bits, 8}), 10);
        auto id1 = r.submit(Request::writeBits(kM100, ByteView{bits, 8}), 11);
        auto id2 = r.submit(Request::writeBits(kM100, ByteView{bits, 8}), 12);
        REQUIRE(id0.hasValue());
        REQUIRE(id1.hasValue());
        REQUIRE(id2.hasValue());
        r.rec.clear();

        r.tick(1010);
        REQUIRE(r.rec.size() == 5);
        CHECK(isSend(r.rec[0], kEot));
        CHECK(r.rec[1].kind == OutputKind::RequestDone);
        CHECK(r.rec[1].requestId == id0.value());
        CHECK(r.rec[1].error.code == ErrorCode::Timeout);
        CHECK(r.rec[2].kind == OutputKind::RequestDone);
        CHECK(r.rec[2].requestId == id1.value());
        CHECK(r.rec[2].error.code == ErrorCode::LinkDown);
        CHECK(r.rec[3].kind == OutputKind::RequestDone);
        CHECK(r.rec[3].requestId == id2.value());
        CHECK(r.rec[3].error.code == ErrorCode::LinkDown);
        CHECK(r.rec[4].kind == OutputKind::LinkFault);
        CHECK(r.rec[4].fault == LinkFaultKind::Timeout);
        CHECK(r.rec[4].reopenTransport == false);
        CHECK(r.s.isFaulted());
        r.rec.clear();

        auto late = r.submit(Request::writeBits(kM100, ByteView{bits, 8}), 1020);
        REQUIRE_FALSE(late.hasValue()); // The link is not up.
        CHECK(late.error().code == ErrorCode::LinkDown);
        r.tick(5000);
        CHECK(r.rec.empty());
    }

    SUBCASE("the error count is reached by the flush cap: the in-flight write keeps its own "
            "Timeout") {
        SessionConfig cfg = serialConfig();
        cfg.maxConsecutiveLinkErrors = 2;
        Rig r(frame, cfg);
        r.up(0);
        r.rec.clear();
        auto id0 = r.submit(Request::writeBits(kM100, ByteView{bits, 8}), 10);
        auto id1 = r.submit(Request::writeBits(kM100, ByteView{bits, 8}), 11);
        REQUIRE(id0.hasValue());
        REQUIRE(id1.hasValue());
        r.rec.clear();

        r.tick(1010); // Link error 1; the flush begins and is capped at 2010.
        REQUIRE(r.rec.size() == 1);
        CHECK(isSend(r.rec[0], kEot));
        r.rec.clear();
        for (TimeMs t = 1020; t <= 2000; t += 10) {
            r.feed({0x55}, t);
            REQUIRE(r.rec.empty());
        }
        r.tick(2010); // Link error 2: the cap.
        REQUIRE(r.rec.size() == 3);
        CHECK(r.rec[0].kind == OutputKind::RequestDone);
        CHECK(r.rec[0].requestId == id0.value());
        CHECK(r.rec[0].error.code == ErrorCode::Timeout);
        CHECK(r.rec[1].kind == OutputKind::RequestDone);
        CHECK(r.rec[1].requestId == id1.value());
        CHECK(r.rec[1].error.code == ErrorCode::LinkDown);
        CHECK(r.rec[2].kind == OutputKind::LinkFault);
        CHECK(r.rec[2].fault == LinkFaultKind::Timeout);
        CHECK(r.rec[2].reopenTransport == false);
        CHECK(r.s.isFaulted());
    }
}
