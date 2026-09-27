// Session scheduling + the receive path / value-publishing contract (spec SPEC-core-session.md):
// SES-01, 02..08, 23, 26. SES-09..27 (ad-hoc, faults, dynamic subscriptions, heartbeat) arrive
// with the later Phase 3 tasks that touch this same file again (todo.md T21..T23).
//
// SES-02..05 are one test (see below): the spec's own "Publishing values" section walks through
// exactly this scenario (M0x128, D100x64, D2000x64, third chunk NAKs in round 1) as a worked
// example, so reproducing it verbatim -- one bytesIn()/tick() per row of the spec's own table, one
// checked output group per row -- is both the most direct test of "the timeline is reproduced
// exactly" (T-023 acceptance criterion 1) and the clearest reading for someone checking the test
// against the spec side by side (Checkpoint C1).
#include "doctest/doctest.h"

#include "harness.h"

#include "mc/core/protocol.h"
#include "mc/core/session.h"

#include <utility>
#include <vector>

using mc::Change;
using mc::ChunkState;
using mc::CycleMode;
using mc::Device;
using mc::DeviceType;
using mc::ErrorCategory;
using mc::ErrorCode;
using mc::FrameConfig;
using mc::FrameType;
using mc::LogLevel;
using mc::LinkFaultKind;
using mc::McProtocol;
using mc::OutputKind;
using mc::PlanOptions;
using mc::Session;
using mc::SessionConfig;
using mc::TimeMs;
using mc::kNoDeadline;
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

// A word-read PeerScript with `words[index] = value`, every other requested word 0.
PeerScript wordAt(size_t index, uint16_t value) {
    PeerScript p;
    p.kind = PeerScript::Kind::Ok;
    p.words.assign(index + 1, 0);
    p.words[index] = value;
    return p;
}

PeerScript allZeroWords() {
    PeerScript p;
    p.kind = PeerScript::Kind::Ok;
    return p; // Empty words: buildReadData() pads every requested word with 0.
}

PeerScript plcError(uint16_t endCode) {
    PeerScript p;
    p.kind = PeerScript::Kind::PlcError;
    p.plcEndCode = endCode;
    return p;
}

} // namespace

TEST_CASE("SES-01 Session::linkUp(): sends the first chunk at once") {
    FrameConfig frame = FrameConfig::frame3E();
    Session s = makeSession(frame);
    REQUIRE(s.subscribe(Device{DeviceType::D, 100}, 10).hasValue());

    FakeClock clock;
    OutputRecorder rec;
    s.linkUp(clock.now());
    rec.drain(s);

    REQUIRE(rec.size() >= 1);
    CHECK(rec[0].kind == OutputKind::Send);

    McProtocol proto(frame);
    auto expected = proto.encode(s.plan().chunk(0).request);
    REQUIRE(expected.hasValue());
    CHECK(rec[0].bytes == expected.value());
}

TEST_CASE("SES-02..05 Session: the \"Publishing values\" timeline, reproduced from the spec's "
          "own worked example") {
    // Subscriptions and chunk order exactly as the spec's own timeline: M (DeviceType order 4)
    // before D (order 9); within D, D100 before D2000 (far enough apart that autoGap() never
    // merges them into one chunk). Plan: [M-chunk(M0x8 words), D#1(D100x64), D#2(D2000x64)].
    FrameConfig frame = FrameConfig::frame3E();
    Session s = makeSession(frame);
    REQUIRE(s.subscribe(Device{DeviceType::M, 0}, 128).hasValue());
    REQUIRE(s.subscribe(Device{DeviceType::D, 100}, 64).hasValue());
    REQUIRE(s.subscribe(Device{DeviceType::D, 2000}, 64).hasValue());

    ScriptedPeer peer(frame);
    FakeClock clock;
    OutputRecorder rec;

    // --- Round 1 -------------------------------------------------------------------------
    // linkUp(t0) → Send(M chunk)  round 1 starts
    s.linkUp(clock.now());
    rec.drain(s);
    REQUIRE(rec.size() == 1);
    CHECK(rec[0].kind == OutputKind::Send);
    rec.clear();

    // bytesIn(M ok) → Send(D chunk#1)  silent (round 1: no ValuesChanged, spec decision S2/S4)
    {
        auto bytes = peer.respond(s.plan().chunk(0).request, allZeroWords());
        s.bytesIn(mc::ByteView{bytes.data(), bytes.size()}, clock.advance(2));
        rec.drain(s);
        REQUIRE(rec.size() == 1);
        CHECK(rec[0].kind == OutputKind::Send);
        rec.clear();
    }

    // bytesIn(D#1 ok) → Send(D chunk#2)  silent
    {
        auto bytes = peer.respond(s.plan().chunk(1).request, wordAt(5, 7)); // D105 = 7.
        s.bytesIn(mc::ByteView{bytes.data(), bytes.size()}, clock.advance(2));
        rec.drain(s);
        REQUIRE(rec.size() == 1);
        CHECK(rec[0].kind == OutputKind::Send);
        rec.clear();
    }

    // bytesIn(D#2 PLC C051) → Snapshot{M, r1} → Snapshot{D, r1: #1 Ok, #2 Failed C051}
    //                       → CycleDone{r1, failedChunks=1}
    {
        auto bytes = peer.respond(s.plan().chunk(2).request, plcError(0xC051));
        s.bytesIn(mc::ByteView{bytes.data(), bytes.size()}, clock.advance(2));
        rec.drain(s);
        REQUIRE(rec.size() == 3);

        CHECK(rec[0].kind == OutputKind::Snapshot);
        CHECK(rec[0].deviceType == DeviceType::M);
        CHECK(rec[0].round == 1);

        CHECK(rec[1].kind == OutputKind::Snapshot);
        CHECK(rec[1].deviceType == DeviceType::D);
        CHECK(rec[1].round == 1);
        REQUIRE(rec[1].chunks.size() == 2);
        CHECK(rec[1].chunks[0].state == ChunkState::Ok);
        CHECK(rec[1].chunks[1].state == ChunkState::Failed);
        CHECK(rec[1].chunks[1].lastError.code == ErrorCode::PlcError);
        CHECK(rec[1].chunks[1].lastError.plcCode == 0xC051);

        CHECK(rec[2].kind == OutputKind::CycleDone);
        CHECK(rec[2].cycle.round == 1);
        CHECK(rec[2].cycle.failedChunks == 1);
        rec.clear();
    }

    // --- Round 2 ---------------------------------------------------------------------------
    // tick(t0+100) → Send(M chunk)  round 2 starts
    s.tick(100);
    rec.drain(s);
    REQUIRE(rec.size() == 1);
    CHECK(rec[0].kind == OutputKind::Send);
    rec.clear();

    // bytesIn(M, M5 0→1) → ValuesChanged{M, r2, [M5 0→1]} → Snapshot{M, r2} → Send(D#1)
    {
        auto bytes = peer.respond(s.plan().chunk(0).request, wordAt(0, 0x0020)); // bit 5 of word0.
        s.bytesIn(mc::ByteView{bytes.data(), bytes.size()}, clock.advance(5));
        rec.drain(s);
        REQUIRE(rec.size() == 3);

        CHECK(rec[0].kind == OutputKind::ValuesChanged);
        CHECK(rec[0].deviceType == DeviceType::M);
        CHECK(rec[0].round == 2);
        REQUIRE(rec[0].changes.size() == 1);
        CHECK(rec[0].changes[0].device == Device{DeviceType::M, 5});
        CHECK(rec[0].changes[0].oldValue == 0);
        CHECK(rec[0].changes[0].newValue == 1);

        CHECK(rec[1].kind == OutputKind::Snapshot);
        CHECK(rec[1].deviceType == DeviceType::M);
        CHECK(rec[1].round == 2);

        CHECK(rec[2].kind == OutputKind::Send);
        rec.clear();
    }

    // bytesIn(D#1, D105 7→9) → ValuesChanged{D, r2, [D105 7→9]} → Send(D#2)
    {
        auto bytes = peer.respond(s.plan().chunk(1).request, wordAt(5, 9));
        s.bytesIn(mc::ByteView{bytes.data(), bytes.size()}, clock.advance(5));
        rec.drain(s);
        REQUIRE(rec.size() == 2);

        CHECK(rec[0].kind == OutputKind::ValuesChanged);
        CHECK(rec[0].deviceType == DeviceType::D);
        CHECK(rec[0].round == 2);
        REQUIRE(rec[0].changes.size() == 1);
        CHECK(rec[0].changes[0].device == Device{DeviceType::D, 105});
        CHECK(rec[0].changes[0].oldValue == 7);
        CHECK(rec[0].changes[0].newValue == 9);

        CHECK(rec[1].kind == OutputKind::Send);
        rec.clear();
    }

    // bytesIn(D#2 ok, first time) → Snapshot{D, r2: all Ok}  (D2000.. silent baseline, S4)
    //                             → CycleDone{r2, failedChunks=0}
    {
        auto bytes = peer.respond(s.plan().chunk(2).request, allZeroWords());
        s.bytesIn(mc::ByteView{bytes.data(), bytes.size()}, clock.advance(5));
        rec.drain(s);
        REQUIRE(rec.size() == 2);

        CHECK(rec[0].kind == OutputKind::Snapshot);
        CHECK(rec[0].deviceType == DeviceType::D);
        CHECK(rec[0].round == 2);
        REQUIRE(rec[0].chunks.size() == 2);
        CHECK(rec[0].chunks[0].state == ChunkState::Ok); // D#1, was Ok already.
        CHECK(rec[0].chunks[1].state == ChunkState::Ok); // D#2, Failed in r1, silently Ok now.

        CHECK(rec[1].kind == OutputKind::CycleDone);
        CHECK(rec[1].cycle.round == 2);
        CHECK(rec[1].cycle.failedChunks == 0);
    }
}

TEST_CASE("SES-06 Session: a value changed while Failed reports ValuesChanged from the last "
          "known value") {
    FrameConfig frame = FrameConfig::frame3E();
    Session s = makeSession(frame);
    REQUIRE(s.subscribe(Device{DeviceType::D, 100}, 1).hasValue());
    ScriptedPeer peer(frame);
    FakeClock clock;
    OutputRecorder rec;

    auto completeRound = [&](const PeerScript& script) {
        auto bytes = peer.respond(s.plan().chunk(0).request, script);
        s.bytesIn(mc::ByteView{bytes.data(), bytes.size()}, clock.advance(1));
        rec.drain(s);
    };

    s.linkUp(clock.now()); // Round 1 send.
    rec.drain(s);
    rec.clear();

    completeRound(wordAt(0, 10)); // Round 1: silent baseline, value 10.
    rec.clear();

    s.tick(100); // Round 2 send.
    rec.drain(s);
    rec.clear();
    completeRound(wordAt(0, 20)); // Round 2: Ok, 10 -> 20.
    REQUIRE(rec.size() >= 1);
    CHECK(rec[0].kind == OutputKind::ValuesChanged);
    REQUIRE(rec[0].changes.size() == 1);
    CHECK(rec[0].changes[0].oldValue == 10);
    CHECK(rec[0].changes[0].newValue == 20);
    rec.clear();

    s.tick(200); // Round 3 send.
    rec.drain(s);
    rec.clear();
    completeRound(plcError(0x1234)); // Round 3: Failed; value stays 20.
    rec.clear();

    s.tick(300); // Round 4 send.
    rec.drain(s);
    rec.clear();
    completeRound(wordAt(0, 25)); // Round 4: Ok, 20 -> 25 (last *known* value, not the failure).
    REQUIRE(rec.size() >= 1);
    CHECK(rec[0].kind == OutputKind::ValuesChanged);
    REQUIRE(rec[0].changes.size() == 1);
    CHECK(rec[0].changes[0].oldValue == 20);
    CHECK(rec[0].changes[0].newValue == 25);
}

TEST_CASE("T-024 2a regression: a queued Snapshot stays valid across a same-call re-plan "
          "(dangling-pointer finding from T-023 Dev notes)") {
    // cycleIntervalMs = 0 reproduces T-023's own back-to-back-within-one-call path (SES-07's
    // interval-0 SUBCASE): a round's own last chunk completing immediately starts the next
    // round in the *same* bytesIn() call, because nextRoundAt == end == now. The hazard T-023
    // flagged is specifically what happens when that same call *also* has a re-plan pending:
    // applyRePlan() replaces m_plan (destroying its old backing storage) after this round's own
    // Snapshot has already been pushed into the output ring pointing *into* that storage.
    FrameConfig frame = FrameConfig::frame3E();
    SessionConfig cfg;
    cfg.cycleIntervalMs = 0;
    Session s = makeSession(frame, cfg);
    REQUIRE(s.subscribe(Device{DeviceType::D, 100}, 1).hasValue());

    ScriptedPeer peer(frame);
    FakeClock clock;
    OutputRecorder rec;

    s.linkUp(clock.now()); // Round 1 send.
    rec.drain(s);
    rec.clear();

    // Round 1 completes (silent baseline); round 2 starts in the same call (interval 0), but no
    // re-plan is pending yet, so this particular transition is not the dangerous one.
    {
        auto bytes = peer.respond(s.plan().chunk(0).request, wordAt(0, 1));
        s.bytesIn(mc::ByteView{bytes.data(), bytes.size()}, clock.advance(1));
        rec.drain(s);
        rec.clear();
    }

    // Subscribe to a second, disjoint device *while round 2 is in flight* (Waiting): deferred
    // re-plan now pending; decision S6/this task's own acceptance criterion 2 says the current
    // round (2) must be unaffected by it.
    REQUIRE(s.subscribe(Device{DeviceType::D, 200}, 1).hasValue());

    // Round 2 completes with a changed value (1 -> 2): its own ValuesChanged (pointing into
    // m_changeBuf) and Snapshot (one chunk, D100, pointing into m_plan) are both queued
    // referencing the *current* plan/buffers. Interval 0 means round 3 starts within this same
    // call, applying the pending re-plan (now two chunks: D100, D200; m_changeBuf/m_plan both
    // replaced) -- the exact scenario T-023 flagged, for both output kinds it named. Read the
    // queued outputs directly with nextOutput(), not through OutputRecorder's own deep copy, so
    // nothing masks *when* the corruption would happen: this checks the raw Output's own view,
    // at the point a real caller would read it, immediately after the input call that produced
    // it returns.
    auto bytes = peer.respond(s.plan().chunk(0).request, wordAt(0, 2));
    s.bytesIn(mc::ByteView{bytes.data(), bytes.size()}, clock.advance(1));

    mc::Output changed;
    REQUIRE(s.nextOutput(changed));
    REQUIRE(changed.kind == OutputKind::ValuesChanged);
    REQUIRE(changed.changeCount == 1);
    CHECK(changed.changes[0].device == Device{DeviceType::D, 100});
    CHECK(changed.changes[0].oldValue == 1);
    CHECK(changed.changes[0].newValue == 2);

    mc::Output snapshot;
    REQUIRE(s.nextOutput(snapshot));
    REQUIRE(snapshot.kind == OutputKind::Snapshot);
    REQUIRE(snapshot.chunkCount == 1); // Round 2's own plan had exactly one D chunk (D100).
    CHECK(snapshot.chunks[0].request.head == Device{DeviceType::D, 100});
    CHECK(snapshot.chunks[0].request.count == 1);
}

TEST_CASE("T-024 2: a subscription made mid-round leaves the current round's remaining chunks "
          "and outputs unchanged") {
    FrameConfig frame = FrameConfig::frame3E();
    Session s = makeSession(frame);
    REQUIRE(s.subscribe(Device{DeviceType::D, 100}, 1).hasValue());
    REQUIRE(s.subscribe(Device{DeviceType::D, 300}, 1).hasValue()); // Far enough from D100/D200
                                                                     // below: no autoGap() merge.
    FakeClock clock;
    OutputRecorder rec;
    ScriptedPeer peer(frame);

    s.linkUp(clock.now()); // Round 1 starts: chunk0 = D100.
    rec.drain(s);
    REQUIRE(rec.size() == 1);
    CHECK(rec[0].kind == OutputKind::Send);
    rec.clear();

    REQUIRE(s.plan().size() == 2); // D100, D300; not yet re-planned.

    // What chunk 1 (D300) *would* send, computed before the mid-round subscribe() below, to
    // compare byte-for-byte against what the session actually sends afterwards.
    McProtocol proto(frame);
    auto expectedChunk1Bytes = proto.encode(s.plan().chunk(1).request);
    REQUIRE(expectedChunk1Bytes.hasValue());

    // Mid-round: chunk 0 (D100) is in flight. This must not touch the current round at all.
    REQUIRE(s.subscribe(Device{DeviceType::D, 200}, 1).hasValue());
    REQUIRE(s.plan().size() == 2); // Still unchanged: deferred to the next round boundary.

    auto bytes0 = peer.respond(s.plan().chunk(0).request, wordAt(0, 1));
    s.bytesIn(mc::ByteView{bytes0.data(), bytes0.size()}, clock.advance(1));
    rec.drain(s);
    REQUIRE(rec.size() == 1);
    CHECK(rec[0].kind == OutputKind::Send); // Chunk 1 (D300), unaffected by the pending re-plan.
    CHECK(rec[0].bytes == expectedChunk1Bytes.value());
    CHECK(s.plan().size() == 2); // Still round 1's own plan.
}

TEST_CASE("SES-22 Session: subscribe()/unsubscribe() take effect at the next round boundary; a "
          "newly subscribed point's first read is silent") {
    FrameConfig frame = FrameConfig::frame3E();
    Session s = makeSession(frame);
    auto d100 = s.subscribe(Device{DeviceType::D, 100}, 1);
    REQUIRE(d100.hasValue());

    FakeClock clock;
    OutputRecorder rec;
    ScriptedPeer peer(frame);

    s.linkUp(clock.now()); // Round 1 starts.
    rec.drain(s);
    rec.clear();

    // Mid-round (round 1, chunk 0 / D100 in flight): subscribe a new device.
    REQUIRE(s.subscribe(Device{DeviceType::D, 200}, 1).hasValue());
    REQUIRE(s.plan().size() == 1); // Round 1 unaffected.

    {
        auto bytes = peer.respond(s.plan().chunk(0).request, wordAt(0, 5));
        s.bytesIn(mc::ByteView{bytes.data(), bytes.size()}, clock.advance(1));
        rec.drain(s);
        rec.clear();
    }
    REQUIRE(s.plan().size() == 1); // Round 1 just ended; re-plan still deferred to round 2.

    clock.set(100);
    s.tick(clock.now()); // Round 2 starts: the re-plan applies now.
    rec.drain(s);
    rec.clear();
    REQUIRE(s.plan().size() == 2); // D100, D200.
    CHECK(s.plan().chunk(0).request.head == Device{DeviceType::D, 100});
    CHECK(s.plan().chunk(1).request.head == Device{DeviceType::D, 200});

    // Round 2: D100 (has a round-1 baseline already) changes value -> ValuesChanged; D200's
    // first-ever read is silent (decision S4) even though this round is >= 2.
    {
        auto bytes = peer.respond(s.plan().chunk(0).request, wordAt(0, 6)); // D100: 5 -> 6.
        s.bytesIn(mc::ByteView{bytes.data(), bytes.size()}, clock.advance(1));
        rec.drain(s);
        REQUIRE(rec.size() >= 1);
        CHECK(rec[0].kind == OutputKind::ValuesChanged);
        rec.clear();
    }
    {
        auto bytes = peer.respond(s.plan().chunk(1).request, wordAt(0, 999)); // D200 first read.
        s.bytesIn(mc::ByteView{bytes.data(), bytes.size()}, clock.advance(1));
        rec.drain(s);
        for (const auto& o : rec.all()) {
            CHECK(o.kind != OutputKind::ValuesChanged); // Silent baseline: no changes reported.
        }
        rec.clear();
    }

    clock.set(200);
    s.tick(clock.now()); // Round 3 starts.
    rec.drain(s);
    rec.clear();
    REQUIRE(s.plan().size() == 2);

    // Mid-round-3: unsubscribe D100. Round 3 itself must be unaffected.
    REQUIRE(s.unsubscribe(d100.value()).hasValue());
    REQUIRE(s.plan().size() == 2);

    {
        auto bytes = peer.respond(s.plan().chunk(0).request, wordAt(0, 6));
        s.bytesIn(mc::ByteView{bytes.data(), bytes.size()}, clock.advance(1));
        rec.drain(s);
        rec.clear();
    }
    {
        auto bytes = peer.respond(s.plan().chunk(1).request, wordAt(0, 999));
        s.bytesIn(mc::ByteView{bytes.data(), bytes.size()}, clock.advance(1));
        rec.drain(s);
        rec.clear();
    }
    REQUIRE(s.plan().size() == 2); // Round 3 just ended; re-plan still deferred to round 4.

    clock.set(300);
    s.tick(clock.now()); // Round 4 starts: D100 is now dropped.
    rec.drain(s);
    REQUIRE(s.plan().size() == 1);
    CHECK(s.plan().chunk(0).request.head == Device{DeviceType::D, 200});
}

TEST_CASE("SES-08 Session::nextDeadline(): response deadline while Waiting, next round while "
          "Idle, kNoDeadline when Down") {
    FrameConfig frame = FrameConfig::frame3E();
    Session s = makeSession(frame);
    CHECK(s.nextDeadline() == kNoDeadline);

    REQUIRE(s.subscribe(Device{DeviceType::D, 100}, 1).hasValue());
    FakeClock clock;
    OutputRecorder rec;
    s.linkUp(clock.now());
    rec.drain(s);

    CHECK(s.nextDeadline() == frame.effectiveTimeoutMs());

    ScriptedPeer peer(frame);
    clock.advance(10);
    auto bytes = peer.respond(s.plan().chunk(0).request, wordAt(0, 1));
    s.bytesIn(mc::ByteView{bytes.data(), bytes.size()}, clock.now());
    rec.drain(s);

    CHECK(s.nextDeadline() == 100); // FixedRate default interval 100, round took 10ms.

    s.linkDown(clock.now());
    CHECK(s.nextDeadline() == kNoDeadline);
}

TEST_CASE("SES-07 Session: round scheduling") {
    FrameConfig frame = FrameConfig::frame3E();
    ScriptedPeer peer(frame);

    // Completes the in-flight round (assumed: exactly one chunk) after `latencyMs`.
    auto completeRound = [&](Session& s, FakeClock& clock, OutputRecorder& rec, TimeMs latencyMs) {
        clock.advance(latencyMs);
        auto bytes = peer.respond(s.plan().chunk(0).request, wordAt(0, 7));
        s.bytesIn(mc::ByteView{bytes.data(), bytes.size()}, clock.now());
        rec.drain(s);
    };

    SUBCASE("FixedRate, 30ms rounds: starts at 0, 100, 200") {
        SessionConfig cfg;
        cfg.cycleIntervalMs = 100;
        cfg.cycleMode = CycleMode::FixedRate;
        Session s = makeSession(frame, cfg);
        REQUIRE(s.subscribe(Device{DeviceType::D, 100}, 1).hasValue());

        FakeClock clock;
        OutputRecorder rec;
        s.linkUp(clock.now()); // Round 1 sent at t=0.
        rec.drain(s);
        REQUIRE(rec.size() == 1);
        CHECK(rec[0].kind == OutputKind::Send);
        rec.clear();

        // Round 1 ends at t=30; round 1 always emits its held-back Snapshot(s) before CycleDone
        // (decision S2), then goes idle: nextRoundAt = max(100, 30) = 100.
        completeRound(s, clock, rec, 30);
        REQUIRE(rec.size() == 2);
        CHECK(rec[0].kind == OutputKind::Snapshot);
        CHECK(rec[1].kind == OutputKind::CycleDone);
        CHECK(rec[1].cycle.round == 1);
        rec.clear();

        clock.set(99);
        s.tick(clock.now());
        rec.drain(s);
        CHECK(rec.empty()); // Not due yet.

        clock.set(100);
        s.tick(clock.now());
        rec.drain(s);
        REQUIRE(rec.size() == 1);
        CHECK(rec[0].kind == OutputKind::Send); // Round 2 starts at t=100.
        rec.clear();

        completeRound(s, clock, rec, 30); // Round 2 ends at t=130; nextRoundAt = max(200,130)=200.
        rec.clear();
        clock.set(199);
        s.tick(clock.now());
        rec.drain(s);
        CHECK(rec.empty());

        clock.set(200);
        s.tick(clock.now());
        rec.drain(s);
        REQUIRE(rec.size() == 1);
        CHECK(rec[0].kind == OutputKind::Send); // Round 3 starts at t=200.
    }

    SUBCASE("FixedRate, 130ms rounds: starts at 0, 130, 260 (no burst)") {
        SessionConfig cfg;
        cfg.cycleIntervalMs = 100;
        cfg.cycleMode = CycleMode::FixedRate;
        Session s = makeSession(frame, cfg);
        REQUIRE(s.subscribe(Device{DeviceType::D, 100}, 1).hasValue());

        FakeClock clock;
        OutputRecorder rec;
        s.linkUp(clock.now()); // Round 1 starts and sends at t=0.
        rec.drain(s);
        REQUIRE(rec.size() == 1);
        CHECK(rec[0].kind == OutputKind::Send);
        rec.clear();

        // Round 1 takes 130ms, longer than the 100ms interval: nextRoundAt = max(100, 130) =
        // 130, which is *now* the instant round 1 ends -- round 2 starts within this same call
        // (no burst: exactly one CycleDone, one Send, never more than one round's worth).
        completeRound(s, clock, rec, 130);
        REQUIRE(rec.size() == 3);
        CHECK(rec[0].kind == OutputKind::Snapshot);
        CHECK(rec[1].kind == OutputKind::CycleDone);
        CHECK(rec[1].cycle.round == 1);
        CHECK(rec[2].kind == OutputKind::Send);
        rec.clear();

        // Round 2: started at 130, also takes 130ms -> ends at 260; nextRoundAt =
        // max(230, 260) = 260 -- round 3 again starts within this same call.
        completeRound(s, clock, rec, 130);
        REQUIRE(rec.size() == 3);
        CHECK(rec[0].kind == OutputKind::Snapshot);
        CHECK(rec[1].kind == OutputKind::CycleDone);
        CHECK(rec[1].cycle.round == 2);
        CHECK(rec[2].kind == OutputKind::Send);
    }

    SUBCASE("FixedDelay: round k+1 starts at end(k) + interval") {
        SessionConfig cfg;
        cfg.cycleIntervalMs = 50;
        cfg.cycleMode = CycleMode::FixedDelay;
        Session s = makeSession(frame, cfg);
        REQUIRE(s.subscribe(Device{DeviceType::D, 100}, 1).hasValue());

        FakeClock clock;
        OutputRecorder rec;
        s.linkUp(clock.now());
        rec.drain(s);
        rec.clear();

        completeRound(s, clock, rec, 20); // ends at 20; nextRoundAt = 20 + 50 = 70.
        rec.clear();

        clock.set(69);
        s.tick(clock.now());
        rec.drain(s);
        CHECK(rec.empty());

        clock.set(70);
        s.tick(clock.now());
        rec.drain(s);
        REQUIRE(rec.size() == 1);
        CHECK(rec[0].kind == OutputKind::Send);
    }

    SUBCASE("interval 0: rounds run back-to-back within one input call") {
        SessionConfig cfg;
        cfg.cycleIntervalMs = 0;
        cfg.cycleMode = CycleMode::FixedRate;
        Session s = makeSession(frame, cfg);
        REQUIRE(s.subscribe(Device{DeviceType::D, 100}, 1).hasValue());

        FakeClock clock;
        OutputRecorder rec;
        s.linkUp(clock.now());
        rec.drain(s);
        rec.clear();

        // The single bytesIn() call that completes round 1 also starts round 2 immediately
        // (nextRoundAt == end == now), with no separate tick() needed.
        completeRound(s, clock, rec, 5);
        REQUIRE(rec.size() == 3);
        CHECK(rec[0].kind == OutputKind::Snapshot);
        CHECK(rec[1].kind == OutputKind::CycleDone);
        CHECK(rec[1].cycle.round == 1);
        CHECK(rec[2].kind == OutputKind::Send);
    }
}

TEST_CASE("SES-23 Session: byte-at-a-time delivery yields the same outputs as delivered whole") {
    FrameConfig frame = FrameConfig::frame3E();
    ScriptedPeer peer(frame);

    auto runScenario = [&](bool byteAtATime) {
        Session s = makeSession(frame);
        REQUIRE(s.subscribe(Device{DeviceType::D, 100}, 1).hasValue());
        FakeClock clock;
        OutputRecorder rec;
        s.linkUp(clock.now());
        rec.drain(s);

        clock.advance(10);
        auto bytes = peer.respond(s.plan().chunk(0).request, wordAt(0, 42));
        if (byteAtATime) {
            for (size_t i = 0; i < bytes.size(); ++i) {
                s.bytesIn(mc::ByteView{bytes.data() + i, 1}, clock.now());
            }
        } else {
            s.bytesIn(mc::ByteView{bytes.data(), bytes.size()}, clock.now());
        }
        rec.drain(s);
        return rec;
    };

    OutputRecorder whole = runScenario(false);
    OutputRecorder piecemeal = runScenario(true);

    REQUIRE(whole.size() == piecemeal.size());
    for (size_t i = 0; i < whole.size(); ++i) {
        CHECK(whole[i].kind == piecemeal[i].kind);
        CHECK(whole[i].deviceType == piecemeal[i].deviceType);
        CHECK(whole[i].round == piecemeal[i].round);
        CHECK(whole[i].changes.size() == piecemeal[i].changes.size());
        CHECK(whole[i].cycle.round == piecemeal[i].cycle.round);
        CHECK(whole[i].cycle.failedChunks == piecemeal[i].cycle.failedChunks);
    }
}

TEST_CASE("SES-26 Session: clock going backwards is clamped and logged; no deadline fires "
          "early") {
    FrameConfig frame = FrameConfig::frame3E();
    RecordingLogSink logSink;
    SessionConfig cfg;
    cfg.log = &logSink;
    Session s = makeSession(frame, cfg);
    REQUIRE(s.subscribe(Device{DeviceType::D, 100}, 1).hasValue());

    OutputRecorder rec;
    s.linkUp(1000); // Round 1 starts at t=1000.
    rec.drain(s);
    REQUIRE(!rec.empty());

    ScriptedPeer peer(frame);
    auto bytes = peer.respond(s.plan().chunk(0).request, wordAt(0, 1));
    s.bytesIn(mc::ByteView{bytes.data(), bytes.size()}, 1010); // Round 1 ends at t=1010.
    rec.drain(s);

    CHECK(s.nextDeadline() == 1100); // FixedRate default interval 100: max(1100, 1010) = 1100.
    rec.clear();

    logSink.clear();
    s.tick(500); // Backwards: clamped to 1010 internally.
    rec.drain(s);
    CHECK(rec.empty());               // Not due (clamped now < 1100): no deadline fired early.
    CHECK(s.nextDeadline() == 1100);   // Unchanged by the bogus backwards call.
    CHECK(logSink.any(LogLevel::Warn)); // "logged at Warn" (spec: clock going backwards).

    // Real time catching up to the deadline still works correctly afterwards.
    s.tick(1100);
    rec.drain(s);
    REQUIRE(!rec.empty());
    CHECK(rec[0].kind == OutputKind::Send);
}

TEST_CASE("Session::create(): SessionConfig/FrameConfig own error paths (Checkpoint C coverage "
          "gap, T-028: every other test here builds with an already-valid frame3E() and a "
          "default-or-heartbeat-off SessionConfig)") {
    FrameConfig frame = FrameConfig::frame3E();

    SUBCASE("heartbeat.enabled with a non-bit device") {
        SessionConfig cfg;
        cfg.heartbeat.enabled = true;
        cfg.heartbeat.device = Device{DeviceType::D, 0}; // D is a word device.
        auto created = Session::create(frame, cfg);
        CHECK_FALSE(created.hasValue());
        CHECK(created.error().code == ErrorCode::InvalidConfig);
    }

    SUBCASE("heartbeat.enabled with a bit device this frame does not support") {
        SessionConfig cfg;
        cfg.heartbeat.enabled = true;
        cfg.heartbeat.device = Device{DeviceType::SM, 0}; // No 1E code at all (PLN-09's own note).
        auto created = Session::create(FrameConfig::frame1E(), cfg);
        CHECK_FALSE(created.hasValue());
        CHECK(created.error().code == ErrorCode::InvalidDevice);
    }

    SUBCASE("an invalid FrameConfig propagates FrameConfig::validate()'s own error") {
        FrameConfig badFrame = FrameConfig::frame3E();
        badFrame.frame = FrameType::F4E; // "reserved for v2" (frame_config.cpp's own check).
        auto created = Session::create(badFrame, SessionConfig{});
        CHECK_FALSE(created.hasValue());
        CHECK(created.error().code == ErrorCode::InvalidConfig);
    }
}

TEST_CASE("Session::subscribe()/unsubscribe(): own error paths (Checkpoint C coverage gap, "
          "T-028: every PLN-xx/SES-xx test above only ever subscribes with an already-valid "
          "count/device, and never unsubscribes an unknown id through Session itself)") {
    FrameConfig frame = FrameConfig::frame3E();
    Session s = makeSession(frame);

    auto zeroCount = s.subscribe(Device{DeviceType::D, 100}, 0);
    CHECK_FALSE(zeroCount.hasValue());
    CHECK(zeroCount.error().code == ErrorCode::PointCount); // RangeSet::add()'s own check.

    // A device number RangeSet::add() itself accepts (any nonzero count in range), but that
    // chunkCount()'s own immediate frame-level check rejects: subscribe() must undo the add() (it
    // "fails atomically"), leaving nothing behind for a later subscribe() to collide with. 1C's D
    // field is 4 decimal digits (PLN-09's own note); 3E's own D field is wider, so 1C is used here
    // specifically to force this rejection.
    Session s1c = makeSession(FrameConfig::frame1C());
    auto tooWide1c = s1c.subscribe(Device{DeviceType::D, 10000}, 1);
    CHECK_FALSE(tooWide1c.hasValue());
    CHECK(tooWide1c.error().code == ErrorCode::InvalidDevice);
    // subscribe() failed atomically: a later, valid subscribe() on the same session still works,
    // proving the earlier add() was really undone rather than merely reported as failed.
    CHECK(s1c.subscribe(Device{DeviceType::D, 100}, 1).hasValue());

    // bitsAsWords = false: subscribe()'s own immediate check must use Op::ReadBits (its own
    // ternary's other branch), not just ReadWords.
    SessionConfig cfg;
    cfg.plan.bitsAsWords = false;
    Session sBits = makeSession(frame, cfg);
    auto bitSub = sBits.subscribe(Device{DeviceType::M, 5}, 3);
    REQUIRE(bitSub.hasValue());

    auto unknownId = s.unsubscribe(static_cast<mc::SubscriptionId>(999999));
    CHECK_FALSE(unknownId.hasValue());
    CHECK(unknownId.error().code == ErrorCode::NotSubscribed);
}

TEST_CASE("Session::linkUp(): ignored (logged Warn) when already up; Session move-assignment "
          "(Checkpoint C coverage gap, T-028)") {
    FrameConfig frame = FrameConfig::frame3E();
    RecordingLogSink sink;
    SessionConfig cfg;
    cfg.log = &sink;
    Session s = makeSession(frame, cfg);
    OutputRecorder rec;

    s.linkUp(0);
    rec.drain(s);
    rec.clear();
    sink.clear();

    s.linkUp(1); // Already up: ignored.
    rec.drain(s);
    CHECK(rec.empty());
    CHECK(sink.any(LogLevel::Warn));
    CHECK(s.isLinkUp());

    // Session's own move-assignment operator (as opposed to move-construction, which every other
    // test's own `Session s = std::move(created.value());` uses instead).
    Session other = makeSession(frame);
    other = std::move(s);
    CHECK(other.isLinkUp());
}

TEST_CASE("Session::values(); linkDown()'s own Info log line; faultLink()'s own Error log line "
          "(Checkpoint C coverage gap, T-028)") {
    FrameConfig frame = FrameConfig::frame3E();
    RecordingLogSink sink;
    SessionConfig cfg;
    cfg.log = &sink;
    Session s = makeSession(frame, cfg);
    REQUIRE(s.subscribe(Device{DeviceType::D, 100}, 1).hasValue());
    OutputRecorder rec;

    s.linkUp(0);
    rec.drain(s);
    rec.clear();

    ScriptedPeer peer(frame);
    auto bytes = peer.respond(s.plan().chunk(0).request, wordAt(0, 42));
    s.bytesIn(mc::ByteView{bytes.data(), bytes.size()}, 1);
    rec.drain(s);

    CHECK(s.values().word(Device{DeviceType::D, 100}) == 42); // Never called directly elsewhere.

    sink.clear();
    s.linkDown(2);
    rec.drain(s);
    CHECK(sink.any(LogLevel::Info)); // "link down".

    // A fault (Ethernet timeout) also logs at Error (faultLink()'s own line), separate from the
    // drain-contract's own Error log (SES-25) -- attach a fresh sink-carrying session so this one
    // is unambiguous.
    Session s2 = makeSession(frame, cfg);
    REQUIRE(s2.subscribe(Device{DeviceType::D, 100}, 1).hasValue());
    s2.linkUp(0);
    rec.drain(s2);
    rec.clear();
    sink.clear();
    s2.tick(frame.effectiveTimeoutMs());
    rec.drain(s2);
    REQUIRE(!rec.empty());
    CHECK(rec[0].kind == OutputKind::LinkFault);
    CHECK(sink.any(LogLevel::Error));
}
