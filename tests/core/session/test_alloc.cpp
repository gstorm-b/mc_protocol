// ALC-01/ALC-02 (spec SPEC-core-session.md, "Testing Strategy": "test_alloc.cpp ALC-01, ALC-02";
// "Complexity and allocation"): ALC-01 (T-027) is the engine's own steady-state zero-allocation
// guarantee -- rounds 3-10, changes every round, heartbeat on, no ad-hoc. ALC-02 (T-025) is the
// ad-hoc queue's -- 1000 submit/complete cycles, including a linkDown() with a full queue. Both
// share this one .cpp (see the duplicate-symbol note below) rather than getting their own files.
// A second ALC-01 case (T-051) runs the same steady-state check on 3C with the serial fault paths
// in every round: a timeout, the EOT and flush, a resend, and a timed-out ad-hoc write.
//
// This file also carries three older, narrower zero-allocation checks against ValueStore alone,
// predating ALC-01's own real definition (T-021, before this task's ALC-01 -- the engine-level
// rounds-3-10 test above -- existed to claim that name): they used the "ALC-01" label as a
// placeholder at the time, which is no longer correct now that a real, spec-defined ALC-01 exists
// in this same file. Renamed here (T-027) to drop the misleading tag rather than deleting them --
// they still isolate ValueStore's own apply()/markFailed() allocation behaviour from the rest of
// the engine, which is still useful for diagnosing a future regression precisely. Flagged in Dev
// notes for the checkpoint reviewer.
//
// This is the one .cpp in mc_core_session_tests that includes tests/common/alloc_counter.h (see
// that file's banner: a second .cpp in this binary including it too would fail to link with a
// duplicate-symbol error). mc_core_model_tests and mc_core_protocol_tests (separate
// binaries/processes) already include the same header in their own test_alloc.cpp; none of the
// three share a linker symbol table with another.
#include "doctest/doctest.h"

#include "common/alloc_counter.h"
#include "harness.h"

#include "mc/core/poll_plan.h"
#include "mc/core/value_store.h"

#include <cstdint>
#include <utility>
#include <vector>

using mc::ByteView;
using mc::ChunkInfo;
using mc::Device;
using mc::DeviceType;
using mc::ErrorCode;
using mc::FrameConfig;
using mc::Output;
using mc::OutputKind;
using mc::PlanOptions;
using mc::RangeSet;
using mc::ReadPlan;
using mc::Request;
using mc::Session;
using mc::SessionConfig;
using mc::TimeMs;
using mc::ValueStore;
using mc::test::PeerScript;
using mc::test::ScriptedPeer;

namespace {

PeerScript okWords(std::initializer_list<uint16_t> words) {
    PeerScript p;
    p.kind = PeerScript::Kind::Ok;
    p.words = words;
    return p;
}

} // namespace

TEST_CASE("ALC positive control: the allocation counter is live") {
    // Same proof as tests/core/model/test_alloc.cpp: if this fails, every zero-count assertion
    // in this file (ALC-01, ALC-02, and the two ValueStore-only checks below) would be
    // meaningless (a broken counter reads 0 for everything).
    mc::test::resetAllocCount();
    auto* p = new int(42);
    size_t count = mc::test::allocCount();
    delete p;

    CHECK(count >= 1);
}

TEST_CASE("ValueStore::apply(): zero allocations (narrower than ALC-01: ValueStore alone, no "
          "Session)") {
    RangeSet subs;
    REQUIRE(subs.add(Device{DeviceType::D, 100}, 10).hasValue());
    FrameConfig cfg = FrameConfig::frame3E();
    auto planResult = ReadPlan::build(subs, cfg, PlanOptions{});
    REQUIRE(planResult.hasValue());
    ReadPlan& plan = planResult.value();

    ValueStore store;
    store.rebuild(subs); // Allocates; must run before the measured window.

    std::vector<uint8_t> payload(20, 0);
    mc::ByteView payloadView{payload.data(), payload.size()};
    mc::Change changes[16];

    // Warm-up outside the measured window, in case anything is lazily initialized only on its
    // first call ever.
    (void)store.apply(plan, 0, payloadView, changes, 16);

    mc::test::resetAllocCount();
    size_t n = store.apply(plan, 0, payloadView, changes, 16);
    size_t count = mc::test::allocCount();

    CHECK(n == 0); // Same payload as the warm-up call: no change to report, but that is not
                   // what this test checks.
    CHECK(count == 0);
}

TEST_CASE("ValueStore::markFailed(): zero allocations (narrower than ALC-01: ValueStore alone, "
          "no Session)") {
    RangeSet subs;
    REQUIRE(subs.add(Device{DeviceType::D, 100}, 10).hasValue());
    FrameConfig cfg = FrameConfig::frame3E();
    auto planResult = ReadPlan::build(subs, cfg, PlanOptions{});
    REQUIRE(planResult.hasValue());
    ReadPlan& plan = planResult.value();

    ValueStore store;
    store.rebuild(subs);

    store.markFailed(plan, 0); // Warm-up.

    mc::test::resetAllocCount();
    store.markFailed(plan, 0);
    size_t count = mc::test::allocCount();

    CHECK(count == 0);
}

TEST_CASE("ALC-01 Session steady state: zero allocation across rounds 3-10 with changes every "
          "round and the heartbeat on") {
    // Deliberately not using OutputRecorder here either, and for the same reason as ALC-02 below:
    // it copies every view into owned storage on every drain() call, which would allocate and
    // swamp the very thing this test measures. Output is popped directly via
    // Session::nextOutput(), mirroring ALC-02's own pattern.
    FrameConfig frame = FrameConfig::frame3E();
    SessionConfig cfg;
    cfg.heartbeat.enabled = true;
    cfg.heartbeat.device = Device{DeviceType::M, 2000};
    // Default cycleIntervalMs (100): each runRound() below jumps `now` forward by 1000 (well past
    // it) only right before its own tick(), so a round never cascades into starting the next one
    // within the same input call (spec "Dispatch": back-to-back rounds only happen when nothing
    // is pending -- see runRound()'s own "no cascade" check below for why that matters here).
    auto created = Session::create(frame, cfg);
    REQUIRE(created.hasValue());
    Session s = std::move(created.value());
    REQUIRE(s.subscribe(Device{DeviceType::D, 100}, 4).hasValue());

    ScriptedPeer peer(frame);
    TimeMs now = 0;
    Output out;

    // Every response byte buffer used below is built here, before the measured window:
    // ScriptedPeer::respond() returns a std::vector<uint8_t> by value and would itself allocate on
    // every call (a test-harness allocation, not a Session one, but mc::test::allocCount() cannot
    // tell those apart -- ALC-02's own identical reasoning above). A write ack's own bytes never
    // depend on the request at all (harness.cpp's buildReadData(): "a write's response carries no
    // data", and nothing else in the frame is request-specific for an Ok response), so one fixed
    // heartbeat-ack buffer covers both bit values; the poll chunk's response bytes do depend on
    // its own word values (this test changes them every round), so one buffer per round's own
    // value (1..10) is precomputed up front instead.
    uint8_t oneBit = 1;
    auto hbAck = peer.respond(Request::writeBits(cfg.heartbeat.device, ByteView{&oneBit, 1}),
                               PeerScript{});
    std::vector<std::vector<uint8_t>> pollResponses;
    for (uint16_t value = 1; value <= 10; ++value) {
        pollResponses.push_back(peer.respond(Request::readWords(Device{DeviceType::D, 100}, 4),
                                              okWords({value, value, value, value})));
    }

    // Completes this round's heartbeat write and its one poll chunk (both pre-built above),
    // feeding every word as `value` so ValueStore::apply() finds a change on every round from
    // round 2 on (decision S4's silent baseline only suppresses round 1's own report). Leaves the
    // engine Idle with the ring fully drained and the next round not yet started.
    auto runRound = [&](uint32_t roundNumber, uint16_t value) {
        REQUIRE(s.nextOutput(out));
        REQUIRE(out.kind == OutputKind::Send); // The heartbeat write.
        now += 1;
        s.bytesIn(ByteView{hbAck.data(), hbAck.size()}, now);

        REQUIRE(s.nextOutput(out));
        REQUIRE(out.kind == OutputKind::Send); // The poll chunk.
        const std::vector<uint8_t>& pollBytes = pollResponses[value - 1];
        now += 1;
        s.bytesIn(ByteView{pollBytes.data(), pollBytes.size()}, now);

        bool sawCycleDone = false;
        while (s.nextOutput(out)) {
            // No cascade into the next round's own Send within this same call (cycleIntervalMs
            // keeps them apart; see the comment above) -- if this ever fired, the loop below
            // would desync in a way a plain allocation-count mismatch would not explain.
            REQUIRE(out.kind != OutputKind::Send);
            if (out.kind == OutputKind::CycleDone) {
                sawCycleDone = true;
                CHECK(out.cycle.round == roundNumber);
                CHECK(out.cycle.heartbeatOk == true);
            }
        }
        REQUIRE(sawCycleDone);
    };

    // Round 1 is started by linkUp() itself (not tick()); rounds 1 and 2 both run outside the
    // measured window (re-plan settles at round 1; decision S4's silent baseline means round 1
    // never reports a change regardless of value).
    s.linkUp(now);
    runRound(1, 1);
    now += 1000;
    s.tick(now);
    runRound(2, 2);

    mc::test::resetAllocCount();
    for (uint32_t round = 3; round <= 10; ++round) {
        now += 1000;
        s.tick(now);
        runRound(round, static_cast<uint16_t>(round));
    }
    size_t count = mc::test::allocCount();
    CHECK(count == 0);
}

TEST_CASE("ALC-02 Session ad-hoc submit/complete: zero allocations across 1000 cycles, incl. "
          "linkDown with a full queue") {
    // Deliberately not using OutputRecorder here (harness.h): RecordedOutput copies every view
    // into owned storage on every drain() call, by design (its own doc comment) -- that would
    // allocate on every single call and swamp the very thing this test measures. Output is
    // instead popped directly via Session::nextOutput() and inspected in place, mirroring this
    // file's own ValueStore tests above.
    FrameConfig frame = FrameConfig::frame3E();
    SessionConfig cfg;
    cfg.adHocCapacity = 8;
    cfg.adHocArenaBytes = 4096;
    // No subscriptions in this test (only the ad-hoc path is measured), but the default
    // cycleIntervalMs (100) would otherwise still fire an empty round every 100ms of simulated
    // time (spec "Dispatch": "a round with no chunks still runs ... and still emits CycleDone")
    // -- 1000 cycles below advance `now` by 1ms each, so without this it would allocate nothing
    // extra but *would* desync the strict Send/RequestDone-only expectations per cycle (the same
    // interference SES-13's own "arena wraps" test hit). Pinned far out so only ad-hoc traffic
    // ever reaches the output ring here.
    cfg.cycleIntervalMs = 10'000'000;

    auto created = Session::create(frame, cfg);
    REQUIRE(created.hasValue());
    Session s = std::move(created.value());

    TimeMs now = 0;
    s.linkUp(now);
    Output out;
    while (s.nextOutput(out)) {
        // Round 1's own CycleDone (empty plan); drained here, before the measured window.
    }

    uint8_t writeData[2] = {0, 0};
    Request writeReq = Request::writeWords(Device{DeviceType::D, 0}, ByteView{writeData, 2});

    // A write's own echo response never carries the data back (harness.cpp's buildReadData(), and
    // test_session_adhoc.cpp's own comment on the same point), so it does not depend on this
    // request's data content either -- one fixed response buffer, built once here (before the
    // measured window: ScriptedPeer::respond() returns by value and would allocate every call),
    // is reused for every one of the 1000 cycles below.
    ScriptedPeer peer(frame);
    std::vector<uint8_t> ackBytes = peer.respond(writeReq, PeerScript{});
    ByteView ack{ackBytes.data(), ackBytes.size()};

    // Warm-up cycle, outside the measured window (any lazy one-time init inside Session/
    // AdHocQueue/McProtocol -- none is expected, but ALC-01's own test above follows the same
    // belt-and-braces pattern).
    {
        auto submitted = s.submit(writeReq, now);
        REQUIRE(submitted.hasValue());
        REQUIRE(s.nextOutput(out));
        REQUIRE(out.kind == OutputKind::Send);
        REQUIRE_FALSE(s.nextOutput(out));

        now += 1;
        s.bytesIn(ack, now);
        REQUIRE(s.nextOutput(out));
        REQUIRE(out.kind == OutputKind::RequestDone);
        REQUIRE_FALSE(s.nextOutput(out));
    }

    mc::test::resetAllocCount();

    for (int i = 0; i < 1000; ++i) {
        auto submitted = s.submit(writeReq, now);
        REQUIRE(submitted.hasValue());
        REQUIRE(s.nextOutput(out));
        REQUIRE(out.kind == OutputKind::Send);
        REQUIRE_FALSE(s.nextOutput(out));

        now += 1;
        s.bytesIn(ack, now);
        REQUIRE(s.nextOutput(out));
        REQUIRE(out.kind == OutputKind::RequestDone);
        REQUIRE_FALSE(s.nextOutput(out));
    }

    // Still in the measured window: fill the queue to adHocCapacity (one sent immediately while
    // idle, the rest queued behind it -- SES-13's own "QueueFull at adHocCapacity" test covers the
    // functional side of this), confirm the next submit() is QueueFull, then linkDown() with that
    // full queue and confirm exactly adHocCapacity RequestDone{LinkDown} outputs, all allocation-
    // free too. Drains after every submit() (the drain contract, spec "Drain contract"; T-026's
    // checkDrained() now enforces it -- an undrained Send left pending from submit() i would
    // otherwise be silently discarded by submit() i+1's own checkDrained() call): only the first
    // one actually produces a Send (idle); the rest are only queued (something is already in
    // flight), producing nothing to drain.
    for (uint16_t i = 0; i < cfg.adHocCapacity; ++i) {
        auto submitted = s.submit(writeReq, now);
        REQUIRE(submitted.hasValue());
        if (i == 0) {
            REQUIRE(s.nextOutput(out));
            REQUIRE(out.kind == OutputKind::Send);
        }
        REQUIRE_FALSE(s.nextOutput(out));
    }
    auto overflow = s.submit(writeReq, now);
    CHECK_FALSE(overflow.hasValue());
    CHECK(overflow.error().code == ErrorCode::QueueFull);
    REQUIRE_FALSE(s.nextOutput(out));

    s.linkDown(now);
    size_t doneCount = 0;
    while (s.nextOutput(out)) {
        CHECK(out.kind == OutputKind::RequestDone);
        CHECK(out.error.code == ErrorCode::LinkDown);
        ++doneCount;
    }
    CHECK(doneCount == cfg.adHocCapacity);

    size_t count = mc::test::allocCount();
    CHECK(count == 0);
}

TEST_CASE("ALC-01 Session serial (3C): zero allocation across rounds 3-10, each with a timeout, "
          "an EOT/flush cycle, a retry and a timed-out ad-hoc write") {
    // Same pattern as ALC-01 above (Session::nextOutput() directly, every response precomputed
    // before the measured window), on 3C Format 1 with the serial fault paths in every round:
    // the poll chunk times out once (EOT, a byte discarded during the flush, resend, response),
    // the heartbeat write is acknowledged, and an ad-hoc write times out (EOT, flush, then
    // RequestDone{Timeout}). Rounds are long (a timeout each), so the next round is pushed far
    // out and started by tick(nextDeadline()), like a driver would.
    FrameConfig frame = FrameConfig::frame3C();
    frame.readRetries = 2;
    frame.timeoutMs = 1000;
    SessionConfig cfg;
    cfg.heartbeat.enabled = true;
    cfg.heartbeat.device = Device{DeviceType::M, 2000};
    cfg.cycleIntervalMs = 100000;
    auto created = Session::create(frame, cfg);
    REQUIRE(created.hasValue());
    Session s = std::move(created.value());
    REQUIRE(s.subscribe(Device{DeviceType::D, 100}, 4).hasValue());

    const std::vector<uint8_t> hbAck = mc::test::vectorBytes("3c_f1.vec", "V-3C1-04");
    std::vector<std::vector<uint8_t>> pollResponses;
    for (uint16_t value = 1; value <= 10; ++value) {
        pollResponses.push_back(mc::test::serialReadResponse3C({value, value, value, value}));
    }
    const uint8_t junk = 0x55;
    const uint8_t bits[8] = {1, 1, 0, 0, 1, 1, 0, 0};
    const Request writeReq = Request::writeBits(Device{DeviceType::M, 100}, ByteView{bits, 8});

    TimeMs now = 0;
    Output out;
    auto expectSend = [&]() {
        REQUIRE(s.nextOutput(out));
        REQUIRE(out.kind == OutputKind::Send);
    };
    auto expectEot = [&]() {
        expectSend();
        REQUIRE(out.bytes.size == 1);
        REQUIRE(out.bytes.data[0] == 0x04);
    };
    auto expectNothing = [&]() { REQUIRE_FALSE(s.nextOutput(out)); };

    auto runRound = [&](uint32_t round, uint16_t value) {
        if (round == 1) {
            s.linkUp(now);
        } else {
            now = s.nextDeadline(); // Idle: the next round's start.
            s.tick(now);
        }
        expectSend(); // The heartbeat write.
        expectNothing();
        now += 1;
        s.bytesIn(ByteView{hbAck.data(), hbAck.size()}, now);
        expectSend(); // The poll chunk.
        expectNothing();

        now = s.nextDeadline(); // The poll chunk times out: EOT, flush.
        s.tick(now);
        expectEot();
        expectNothing();
        now += 10;
        s.bytesIn(ByteView{&junk, 1}, now); // Discarded; restarts the silence window.
        expectNothing();
        now = s.nextDeadline(); // Silent for serialFlushMs: the read is resent.
        s.tick(now);
        expectSend();
        expectNothing();
        now += 1;
        const std::vector<uint8_t>& pollBytes = pollResponses[value - 1];
        s.bytesIn(ByteView{pollBytes.data(), pollBytes.size()}, now);
        bool sawCycleDone = false;
        while (s.nextOutput(out)) {
            REQUIRE(out.kind != OutputKind::Send);
            if (out.kind == OutputKind::CycleDone) {
                sawCycleDone = true;
                CHECK(out.cycle.round == round);
                CHECK(out.cycle.requests == 3); // Heartbeat, poll chunk, its resend.
                CHECK(out.cycle.failedChunks == 0);
                CHECK(out.cycle.heartbeatOk == true);
            }
        }
        REQUIRE(sawCycleDone);

        now += 1; // An ad-hoc write that times out: EOT, flush, then RequestDone{Timeout}.
        auto id = s.submit(writeReq, now);
        REQUIRE(id.hasValue());
        expectSend();
        expectNothing();
        now = s.nextDeadline();
        s.tick(now);
        expectEot();
        expectNothing();
        now = s.nextDeadline();
        s.tick(now);
        REQUIRE(s.nextOutput(out));
        REQUIRE(out.kind == OutputKind::RequestDone);
        REQUIRE(out.error.code == ErrorCode::Timeout);
        expectNothing();
    };

    // Rounds 1 and 2 are outside the measured window (re-plan, buffer capacities, round 1's
    // silent baseline).
    runRound(1, 1);
    runRound(2, 2);

    mc::test::resetAllocCount();
    for (uint32_t round = 3; round <= 10; ++round) {
        runRound(round, static_cast<uint16_t>(round));
    }
    size_t count = mc::test::allocCount();
    CHECK(count == 0);

    CHECK_FALSE(s.isFaulted());
    CHECK(s.stats().retries == 10);  // One resend per round.
    CHECK(s.stats().eotsSent == 20); // Poll chunk and ad-hoc write, every round.
}

TEST_CASE("ALC-01 Session serial: junk with no start byte, in many small calls and in one big call, "
          "allocates nothing and keeps the receive buffer bounded") {
    // The receive buffer is dropped while only junk has arrived (nothing counts toward the
    // overflow bound before the start byte). If it were kept, 20000 junk bytes would outgrow the
    // capacity the warm-up call left and allocate.
    FrameConfig frame = FrameConfig::frame3C();
    frame.timeoutMs = 1000;
    SessionConfig cfg;
    cfg.cycleIntervalMs = 100000;
    auto created = Session::create(frame, cfg);
    REQUIRE(created.hasValue());
    Session s = std::move(created.value());
    REQUIRE(s.subscribe(Device{DeviceType::D, 100}, 3).hasValue());

    Output out;
    s.linkUp(0);
    REQUIRE(s.nextOutput(out));
    REQUIRE(out.kind == OutputKind::Send);
    REQUIRE_FALSE(s.nextOutput(out));

    // Far more than the largest response to this read (26 bytes), 0x55 is no start byte.
    const std::vector<uint8_t> big(5000, 0x55);
    const uint8_t one = 0x55;
    TimeMs now = 1;

    // Warm-up: the buffer's capacity grows once, here.
    s.bytesIn(ByteView{big.data(), big.size()}, now);
    REQUIRE_FALSE(s.nextOutput(out));

    mc::test::resetAllocCount();
    for (int i = 0; i < 20000; ++i) {
        s.bytesIn(ByteView{&one, 1}, ++now);
    }
    s.bytesIn(ByteView{big.data(), big.size()}, ++now);
    size_t count = mc::test::allocCount();
    CHECK(count == 0);

    CHECK_FALSE(s.nextOutput(out)); // no overflow, no EOT: junk is skipped
    CHECK(s.stats().protocolErrors == 0);
    CHECK(s.stats().eotsSent == 0);
    CHECK_FALSE(s.isFaulted());
}
