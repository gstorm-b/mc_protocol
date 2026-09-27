// Ad-hoc requests (spec SPEC-core-session.md, "Ad-hoc requests", "Exactly-once completion"):
// SES-09..15, and a regression test for the T-025-own analogue of T-024's dangling-pointer
// lesson (a RequestDone's payload must stay valid until drained).
#include "doctest/doctest.h"

#include "harness.h"

#include "mc/core/convert.h"
#include "mc/core/protocol.h"
#include "mc/core/session.h"

#include <utility>
#include <vector>

using mc::BitLayout;
using mc::ByteView;
using mc::Device;
using mc::DeviceType;
using mc::ErrorCategory;
using mc::ErrorCode;
using mc::FrameConfig;
using mc::McProtocol;
using mc::MutableByteView;
using mc::Op;
using mc::OutputKind;
using mc::Request;
using mc::RequestId;
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

} // namespace

TEST_CASE("SES-09 Session::submit(): a write while idle is sent immediately; RequestDone "
          "carries its id and empty payload") {
    FrameConfig frame = FrameConfig::frame3E();
    Session s = makeSession(frame);
    FakeClock clock;
    OutputRecorder rec;

    s.linkUp(clock.now()); // Empty plan: round 1 ends immediately (CycleDone only).
    rec.drain(s);
    rec.clear();

    auto data = mc::convert::fromWords({42});
    auto submitted =
        s.submit(Request::writeWords(Device{DeviceType::D, 500}, ByteView{data.data(), data.size()}),
                 clock.now());
    REQUIRE(submitted.hasValue());
    rec.drain(s);
    REQUIRE(rec.size() == 1);
    CHECK(rec[0].kind == OutputKind::Send);
    rec.clear();

    ScriptedPeer peer(frame);
    Request writeEcho =
        Request::writeWords(Device{DeviceType::D, 0}, ByteView{data.data(), data.size()});
    auto bytes = peer.respond(writeEcho, PeerScript{});
    s.bytesIn(ByteView{bytes.data(), bytes.size()}, clock.advance(1));
    rec.drain(s);
    REQUIRE(rec.size() == 1);
    CHECK(rec[0].kind == OutputKind::RequestDone);
    CHECK(rec[0].requestId == submitted.value());
    CHECK(rec[0].error.ok());
    CHECK(rec[0].payload.empty());
}

TEST_CASE("SES-11 Session::submit(): an ad-hoc read splits (D0x2000 -> 3 frames), one "
          "RequestDone with the whole payload; bit read honours BitLayout") {
    FrameConfig frame = FrameConfig::frame3E();

    SUBCASE("word read, 3 chunks (960/960/80), one 4000-byte payload") {
        Session s = makeSession(frame);
        FakeClock clock;
        OutputRecorder rec;
        s.linkUp(clock.now());
        rec.drain(s);
        rec.clear();

        auto submitted = s.submit(Request::readWords(Device{DeviceType::D, 0}, 2000), clock.now());
        REQUIRE(submitted.hasValue());

        ScriptedPeer peer(frame);
        // Three chunks (960, 960, 80 points): reply to each with a distinct, checkable pattern.
        for (uint16_t chunkPoints : {uint16_t{960}, uint16_t{960}, uint16_t{80}}) {
            rec.drain(s);
            REQUIRE(rec.size() == 1);
            CHECK(rec[0].kind == OutputKind::Send);
            rec.clear();

            PeerScript p;
            p.kind = PeerScript::Kind::Ok;
            p.words.assign(chunkPoints, 0);
            p.words[0] = chunkPoints; // Marks which chunk this is, for the payload check below.
            auto bytes =
                peer.respond(Request::readWords(Device{DeviceType::D, 0}, chunkPoints), p);
            s.bytesIn(ByteView{bytes.data(), bytes.size()}, clock.advance(1));
        }

        rec.drain(s);
        REQUIRE(rec.size() == 1);
        CHECK(rec[0].kind == OutputKind::RequestDone);
        REQUIRE(rec[0].payload.size() == 4000); // 2000 words * 2 bytes.
        ByteView payload{rec[0].payload.data(), rec[0].payload.size()};
        CHECK(mc::convert::wordAt(payload, 0) == 960);   // Chunk 1's own marker.
        CHECK(mc::convert::wordAt(payload, 960) == 960); // Chunk 2's own marker.
        CHECK(mc::convert::wordAt(payload, 1920) == 80); // Chunk 3's own marker.
    }

    SUBCASE("bit read honours BitLayout::PackedLsbFirst") {
        Session s = makeSession(frame);
        FakeClock clock;
        OutputRecorder rec;
        s.linkUp(clock.now());
        rec.drain(s);
        rec.clear();

        Request r = Request::readBits(Device{DeviceType::M, 0}, 10);
        r.bitLayout = BitLayout::PackedLsbFirst;
        auto submitted = s.submit(r, clock.now());
        REQUIRE(submitted.hasValue());
        rec.drain(s);
        REQUIRE(rec.size() == 1);
        rec.clear();

        ScriptedPeer peer(frame);
        PeerScript p;
        p.kind = PeerScript::Kind::Ok;
        p.bits = {1, 0, 1, 0, 0, 0, 0, 0, 1, 1}; // Bits 0, 2, 8, 9 set.
        auto bytes = peer.respond(Request::readBits(Device{DeviceType::M, 0}, 10), p);
        s.bytesIn(ByteView{bytes.data(), bytes.size()}, clock.advance(1));

        rec.drain(s);
        REQUIRE(rec.size() == 1);
        CHECK(rec[0].kind == OutputKind::RequestDone);
        REQUIRE(rec[0].payload.size() == 2); // ceil(10/8) bytes.
        CHECK(rec[0].payload[0] == 0x05);     // Bits 0 and 2: 0b00000101.
        CHECK(rec[0].payload[1] == 0x03);     // Bits 8 and 9 (bit 0 and 1 of byte 2): 0b00000011.
    }
}

TEST_CASE("SES-12 Session::submit(): a write over the limit without splitWrites is PointCount, "
          "nothing queued, no RequestDone") {
    FrameConfig frame = FrameConfig::frame3E(); // splitWrites defaults to false.
    Session s = makeSession(frame);
    FakeClock clock;
    OutputRecorder rec;
    s.linkUp(clock.now());
    rec.drain(s);
    rec.clear();

    std::vector<uint8_t> data(200 * 2, 0); // 200 words > 96-ish... use a limit-exceeding count.
    // QnA word device write limit (Binary, iQ-R/Q/L) is 960; force a clean over-limit write.
    std::vector<uint8_t> bigData(1000 * 2, 0);
    auto submitted = s.submit(
        Request::writeWords(Device{DeviceType::D, 0}, ByteView{bigData.data(), bigData.size()}),
        clock.now());
    CHECK_FALSE(submitted.hasValue());
    CHECK(submitted.error().code == ErrorCode::PointCount);

    rec.drain(s);
    CHECK(rec.empty()); // Nothing queued, nothing sent.
}

TEST_CASE("SES-13 Session::submit(): QueueFull at adHocCapacity and when the arena is full; a "
          "request larger than the arena is PointCount; LinkDown when not up") {
    FrameConfig frame = FrameConfig::frame3E();

    SUBCASE("QueueFull at adHocCapacity") {
        SessionConfig cfg;
        cfg.adHocCapacity = 2;
        cfg.adHocArenaBytes = 4096;
        Session s = makeSession(frame, cfg);
        FakeClock clock;
        OutputRecorder rec;
        s.linkUp(clock.now());
        rec.drain(s);
        rec.clear();

        auto data = mc::convert::fromWords({1});
        ByteView dv{data.data(), data.size()};
        REQUIRE(s.submit(Request::writeWords(Device{DeviceType::D, 0}, dv), clock.now())
                    .hasValue());
        // The first one is sent immediately (idle); it still occupies a job slot until drained.
        rec.drain(s);
        rec.clear();
        REQUIRE(s.submit(Request::writeWords(Device{DeviceType::D, 1}, dv), clock.now())
                    .hasValue());
        auto third = s.submit(Request::writeWords(Device{DeviceType::D, 2}, dv), clock.now());
        CHECK_FALSE(third.hasValue());
        CHECK(third.error().code == ErrorCode::QueueFull);
    }

    SUBCASE("QueueFull when the arena has no room right now") {
        SessionConfig cfg;
        cfg.adHocCapacity = 64;
        cfg.adHocArenaBytes = 64; // Tiny: one small write barely fits, a second does not.
        Session s = makeSession(frame, cfg);
        FakeClock clock;
        OutputRecorder rec;
        s.linkUp(clock.now());
        rec.drain(s);
        rec.clear();

        auto data = mc::convert::fromWords({1, 2, 3, 4, 5, 6, 7, 8});
        ByteView dv{data.data(), data.size()};
        auto first = s.submit(Request::writeWords(Device{DeviceType::D, 0}, dv), clock.now());
        REQUIRE(first.hasValue());
        // The first one is sent immediately (idle): drain its own Send before the next input call
        // (drain contract; T-026 rework: an undrained output now trips the real debug assert via
        // drain_violation.h, not just a silent discard).
        rec.drain(s);
        rec.clear();
        auto second = s.submit(Request::writeWords(Device{DeviceType::D, 20}, dv), clock.now());
        CHECK_FALSE(second.hasValue());
        CHECK(second.error().code == ErrorCode::QueueFull);
    }

    SUBCASE("a request needing more than the whole arena is PointCount") {
        SessionConfig cfg;
        cfg.adHocArenaBytes = 64;
        Session s = makeSession(frame, cfg);
        FakeClock clock;
        OutputRecorder rec;
        s.linkUp(clock.now());
        rec.drain(s);
        rec.clear();

        std::vector<uint8_t> data(100, 0);
        auto submitted = s.submit(
            Request::writeWords(Device{DeviceType::D, 0}, ByteView{data.data(), data.size()}),
            clock.now());
        CHECK_FALSE(submitted.hasValue());
        CHECK(submitted.error().code == ErrorCode::PointCount);
    }

    SUBCASE("the arena wraps correctly after many submit/complete cycles") {
        SessionConfig cfg;
        cfg.adHocCapacity = 4;
        cfg.adHocArenaBytes = 256; // Small enough to force wraparound well before 200 cycles.
        // No subscriptions in this SUBCASE, so polling itself is irrelevant -- but the default
        // cycleIntervalMs (100) would otherwise still fire an empty round (spec "Dispatch": "a
        // round with no chunks still runs ... and still emits CycleDone") partway through the
        // loop below (200 iterations, clock advancing 1ms each), interleaving an unrelated
        // CycleDone with the ad-hoc RequestDone this SUBCASE is checking. Pin it far out so only
        // ad-hoc traffic ever reaches the output ring here.
        cfg.cycleIntervalMs = 1'000'000;
        Session s = makeSession(frame, cfg);
        FakeClock clock;
        OutputRecorder rec;
        ScriptedPeer peer(frame);
        s.linkUp(clock.now());
        rec.drain(s);
        rec.clear();

        for (int i = 0; i < 200; ++i) {
            auto data = mc::convert::fromWords({static_cast<uint16_t>(i)});
            auto submitted = s.submit(
                Request::writeWords(Device{DeviceType::D, 0}, ByteView{data.data(), data.size()}),
                clock.now());
            REQUIRE(submitted.hasValue());

            rec.drain(s);
            REQUIRE(rec.size() == 1);
            CHECK(rec[0].kind == OutputKind::Send);
            rec.clear();

            Request echo = Request::writeWords(Device{DeviceType::D, 0},
                                                ByteView{data.data(), data.size()});
            auto bytes = peer.respond(echo, PeerScript{});
            s.bytesIn(ByteView{bytes.data(), bytes.size()}, clock.advance(1));
            rec.drain(s);
            REQUIRE(rec.size() == 1);
            CHECK(rec[0].kind == OutputKind::RequestDone);
            CHECK(rec[0].requestId == submitted.value());
            rec.clear();
        }
    }

    SUBCASE("LinkDown from submit() when the link is not up") {
        Session s = makeSession(frame);
        auto data = mc::convert::fromWords({1});
        auto submitted = s.submit(
            Request::writeWords(Device{DeviceType::D, 0}, ByteView{data.data(), data.size()}), 0);
        CHECK_FALSE(submitted.hasValue());
        CHECK(submitted.error().code == ErrorCode::LinkDown);
    }
}

TEST_CASE("SES-14 Session::submit(): request ids are unique, increasing, never 0") {
    FrameConfig frame = FrameConfig::frame3E();
    Session s = makeSession(frame);
    FakeClock clock;
    OutputRecorder rec;
    s.linkUp(clock.now());
    rec.drain(s);
    rec.clear();

    ScriptedPeer peer(frame);
    std::vector<RequestId> ids;
    for (int i = 0; i < 5; ++i) {
        auto data = mc::convert::fromWords({static_cast<uint16_t>(i)});
        auto submitted = s.submit(
            Request::writeWords(Device{DeviceType::D, 0}, ByteView{data.data(), data.size()}),
            clock.now());
        REQUIRE(submitted.hasValue());
        CHECK(submitted.value() != 0);
        ids.push_back(submitted.value());

        rec.drain(s);
        REQUIRE(rec.size() == 1);
        rec.clear();
        Request echo = Request::writeWords(Device{DeviceType::D, 0},
                                            ByteView{data.data(), data.size()});
        auto bytes = peer.respond(echo, PeerScript{});
        s.bytesIn(ByteView{bytes.data(), bytes.size()}, clock.advance(1));
        rec.drain(s);
        rec.clear();
    }
    for (size_t i = 1; i < ids.size(); ++i) {
        CHECK(ids[i] > ids[i - 1]);
    }
}

TEST_CASE("SES-15 Session::linkDown(): 1 in flight + 3 queued -> 4 RequestDone{LinkDown} in "
          "submission order; no later output mentions them") {
    FrameConfig frame = FrameConfig::frame3E();
    Session s = makeSession(frame);
    FakeClock clock;
    OutputRecorder rec;
    s.linkUp(clock.now());
    rec.drain(s);
    rec.clear();

    auto data = mc::convert::fromWords({1});
    ByteView dv{data.data(), data.size()};
    std::vector<RequestId> ids;
    for (uint32_t i = 0; i < 4; ++i) {
        auto submitted = s.submit(Request::writeWords(Device{DeviceType::D, i}, dv), clock.now());
        REQUIRE(submitted.hasValue());
        ids.push_back(submitted.value());
        // The first one is sent immediately (idle): drain its own Send before the next submit()
        // call (drain contract; T-026 rework: an undrained output now trips the real debug
        // assert). The rest produce nothing to drain (something is already in flight).
        rec.drain(s);
    }
    rec.clear();

    s.linkDown(clock.advance(1));
    rec.drain(s);
    REQUIRE(rec.size() == 4);
    for (size_t i = 0; i < 4; ++i) {
        CHECK(rec[i].kind == OutputKind::RequestDone);
        CHECK(rec[i].requestId == ids[i]);
        CHECK(rec[i].error.code == ErrorCode::LinkDown);
        CHECK(rec[i].payload.empty());
    }
}

TEST_CASE("T-025 lifetime: a queued RequestDone's payload stays valid across a same-call arena "
          "reuse") {
    // Mirrors T-024's own regression test: cycleIntervalMs is irrelevant here (no polling
    // subscriptions at all), but a *second* submit() call, made before the first RequestDone is
    // drained, must not corrupt the first one's own payload -- exactly the hazard
    // AdHocQueue::confirmDrained() (called only from Session::nextOutput()) exists to prevent.
    FrameConfig frame = FrameConfig::frame3E();
    SessionConfig cfg;
    cfg.adHocCapacity = 4;
    cfg.adHocArenaBytes = 64; // Small: a second read's payload would reuse the first's space if
                              // confirmDrained() ran too early.
    Session s = makeSession(frame, cfg);
    FakeClock clock;
    OutputRecorder rec;
    s.linkUp(clock.now());
    rec.drain(s);
    rec.clear();

    ScriptedPeer peer(frame);
    auto firstId = s.submit(Request::readWords(Device{DeviceType::D, 0}, 4), clock.now());
    REQUIRE(firstId.hasValue());
    rec.drain(s);
    REQUIRE(rec.size() == 1);
    rec.clear();

    auto bytes1 =
        peer.respond(Request::readWords(Device{DeviceType::D, 0}, 4), okWords({11, 22, 33, 44}));
    s.bytesIn(ByteView{bytes1.data(), bytes1.size()}, clock.advance(1));

    // Read the queued RequestDone directly (not through OutputRecorder's own deep copy), exactly
    // as T-024's own regression test does, so nothing masks *when* corruption would happen.
    mc::Output firstDone;
    REQUIRE(s.nextOutput(firstDone));
    REQUIRE(firstDone.kind == OutputKind::RequestDone);
    REQUIRE(firstDone.requestId == firstId.value());
    REQUIRE(firstDone.payload.size == 8);

    // Submit a second read *before* draining anything else: if the arena freed the first job's
    // space eagerly (at completion time, not at drain time), this would land on top of it.
    auto secondId = s.submit(Request::readWords(Device{DeviceType::D, 100}, 4), clock.now());
    REQUIRE(secondId.hasValue());

    CHECK(mc::convert::wordAt(firstDone.payload, 0) == 11);
    CHECK(mc::convert::wordAt(firstDone.payload, 1) == 22);
    CHECK(mc::convert::wordAt(firstDone.payload, 2) == 33);
    CHECK(mc::convert::wordAt(firstDone.payload, 3) == 44);
}

TEST_CASE("SES-10 Session: ad-hoc dispatch order and maxAdHocBurst") {
    FrameConfig frame = FrameConfig::frame3E();

    // Four disjoint polling chunks (D100/200/300/400, gaps of 99 >> autoGap(16): never merged).
    auto subscribeFour = [](Session& s) {
        REQUIRE(s.subscribe(Device{DeviceType::D, 100}, 1).hasValue());
        REQUIRE(s.subscribe(Device{DeviceType::D, 200}, 1).hasValue());
        REQUIRE(s.subscribe(Device{DeviceType::D, 300}, 1).hasValue());
        REQUIRE(s.subscribe(Device{DeviceType::D, 400}, 1).hasValue());
    };

    McProtocol proto(frame);
    size_t readFrameSize =
        proto.encodedSize(Request::readWords(Device{DeviceType::D, 0}, 1)).value();

    // Completes whatever was last sent (a poll chunk or an ad-hoc write, told apart by frame
    // size) with a trivial Ok response, appending 'C' or 'W' to `sequence`. Every write here is
    // exactly one point, so it always completes on its own first response: its own RequestDone
    // and the next chunk's Send (dispatch() runs synchronously from within the same bytesIn()
    // call that completes it) land in the same drain batch, RequestDone first -- `rec` is then
    // 2 long, not 1, and only its *last* entry is this call's own newly-sent frame.
    auto completeNext = [&](Session& s, FakeClock& clock, OutputRecorder& rec,
                            std::string& sequence) {
        REQUIRE(rec.size() >= 1);
        REQUIRE(rec.size() <= 2);
        if (rec.size() == 2) {
            CHECK(rec[0].kind == OutputKind::RequestDone);
        }
        const auto& sent = rec.back();
        REQUIRE(sent.kind == OutputKind::Send);
        bool isPoll = (sent.bytes.size() == readFrameSize);
        sequence += isPoll ? 'C' : 'W';
        ScriptedPeer peer(frame);
        // respond()'s own buildReadData() returns no data for a write regardless of what `data`
        // claims (a write's response always carries none), so an empty placeholder is enough.
        std::vector<uint8_t> bytes =
            isPoll ? peer.respond(Request::readWords(Device{DeviceType::D, 0}, 1), okWords({1}))
                   : peer.respond(Request::writeWords(Device{DeviceType::D, 0}, ByteView{}),
                                  PeerScript{});
        rec.clear();
        s.bytesIn(ByteView{bytes.data(), bytes.size()}, clock.advance(1));
        rec.drain(s);
    };

    SUBCASE("maxAdHocBurst = 4: W1-4, C1, W5-8, C2, W9, C3") {
        SessionConfig cfg;
        cfg.maxAdHocBurst = 4;
        Session s = makeSession(frame, cfg);
        subscribeFour(s);

        FakeClock clock;
        OutputRecorder rec;
        s.linkUp(clock.now()); // Sends the first poll chunk (unlabeled: already in flight).
        rec.drain(s);
        REQUIRE(rec.size() == 1);
        rec.clear();

        auto data = mc::convert::fromWords({0});
        for (int i = 0; i < 9; ++i) {
            REQUIRE(s.submit(Request::writeWords(Device{DeviceType::D, static_cast<uint32_t>(900 + i)},
                                                  ByteView{data.data(), data.size()}),
                              clock.now())
                        .hasValue());
        }
        rec.drain(s);
        CHECK(rec.empty()); // Queued only: the first poll chunk is still in flight.

        // Complete the (unlabeled) in-flight chunk: ad-hoc priority now applies.
        ScriptedPeer peer(frame);
        auto bytes0 = peer.respond(Request::readWords(Device{DeviceType::D, 0}, 1), okWords({1}));
        s.bytesIn(ByteView{bytes0.data(), bytes0.size()}, clock.advance(1));
        rec.drain(s);

        std::string sequence;
        for (int i = 0; i < 12; ++i) { // W1-4, C1, W5-8, C2, W9, C3 = 12 sends.
            completeNext(s, clock, rec, sequence);
        }
        CHECK(sequence == "WWWWCWWWWCWC");
    }

    SUBCASE("maxAdHocBurst = 0: all nine precede C1") {
        SessionConfig cfg;
        cfg.maxAdHocBurst = 0;
        Session s = makeSession(frame, cfg);
        subscribeFour(s);

        FakeClock clock;
        OutputRecorder rec;
        s.linkUp(clock.now());
        rec.drain(s);
        rec.clear();

        auto data = mc::convert::fromWords({0});
        for (int i = 0; i < 9; ++i) {
            REQUIRE(s.submit(Request::writeWords(Device{DeviceType::D, static_cast<uint32_t>(900 + i)},
                                                  ByteView{data.data(), data.size()}),
                              clock.now())
                        .hasValue());
        }

        ScriptedPeer peer(frame);
        auto bytes0 = peer.respond(Request::readWords(Device{DeviceType::D, 0}, 1), okWords({1}));
        s.bytesIn(ByteView{bytes0.data(), bytes0.size()}, clock.advance(1));
        rec.drain(s);

        std::string sequence;
        for (int i = 0; i < 12; ++i) {
            completeNext(s, clock, rec, sequence);
        }
        CHECK(sequence == "WWWWWWWWWCCC"); // All nine, uncapped, before the three remaining polls.
    }
}

TEST_CASE("Ad-hoc write to a bit device (WriteBits), PackedLsbFirst layout (Checkpoint C "
          "coverage gap, T-028: every other ad-hoc write test above uses WriteWords only, so "
          "AdHocQueue's own Op::WriteBits chunking branch is never exercised)") {
    FrameConfig frame = FrameConfig::frame3E();
    Session s = makeSession(frame);
    FakeClock clock;
    OutputRecorder rec;
    s.linkUp(clock.now());
    rec.drain(s);
    rec.clear();

    uint8_t packed[2] = {0b00000101, 0b00000011}; // 16 points packed lsb-first (SES-11's own
                                                    // pattern, reused here for a write instead).
    Request writeReq = Request::writeBits(Device{DeviceType::M, 0}, ByteView{packed, 2},
                                           BitLayout::PackedLsbFirst);
    auto submitted = s.submit(writeReq, clock.now());
    REQUIRE(submitted.hasValue());
    rec.drain(s);
    REQUIRE(rec.size() == 1);
    CHECK(rec[0].kind == OutputKind::Send);
    rec.clear();

    ScriptedPeer peer(frame);
    // A write's own response never carries data regardless of what PeerScript claims (harness.cpp
    // buildReadData()), so an empty PeerScript{} is enough here, same as every other ad-hoc write
    // test's own write-ack pattern.
    auto bytes = peer.respond(writeReq, PeerScript{});
    s.bytesIn(ByteView{bytes.data(), bytes.size()}, clock.advance(1));
    rec.drain(s);
    REQUIRE(rec.size() == 1);
    CHECK(rec[0].kind == OutputKind::RequestDone);
    CHECK(rec[0].requestId == submitted.value());
    CHECK(rec[0].error.ok());
    CHECK(rec[0].payload.empty());
}
