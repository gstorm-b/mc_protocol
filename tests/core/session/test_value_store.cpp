// ValueStore (spec SPEC-core-session.md, value_store.h): STO-01..04, and PLN-10 (moved here from
// T-020/test_range_set.cpp by the leader, 2026-09-27: PLN-10 exercises ValueStore::rebuild()'s
// own carry-over contract, not anything in RangeSet/ReadPlan).
#include "doctest/doctest.h"

#include "mc/core/poll_plan.h"
#include "mc/core/value_store.h"

#include "mc/core/convert.h"

#include <cstdint>
#include <string_view>
#include <vector>

using mc::ChunkInfo;
using mc::Device;
using mc::DeviceType;
using mc::ErrorCode;
using mc::FrameConfig;
using mc::MutableByteView;
using mc::PlanOptions;
using mc::PointState;
using mc::RangeSet;
using mc::ReadPlan;
using mc::SegmentView;
using mc::ValueStore;

namespace {

// Builds a normalized ReadWords payload of `count` words, value(i) = base + i.
std::vector<uint8_t> wordsPayload(uint32_t count, uint16_t base) {
    std::vector<uint8_t> buf(static_cast<size_t>(count) * 2);
    mc::MutableByteView view{buf.data(), buf.size()};
    for (uint32_t i = 0; i < count; ++i) {
        mc::convert::putWord(view, i, static_cast<uint16_t>(base + i));
    }
    return buf;
}

mc::ByteView view(const std::vector<uint8_t>& v) { return mc::ByteView{v.data(), v.size()}; }

} // namespace

TEST_CASE("STO-01 ValueStore: word(), bulk words() across two chunks of one segment") {
    RangeSet subs;
    REQUIRE(subs.add(Device{DeviceType::D, 0}, 2000).hasValue()); // splits 960/960/80 (PLN-06).
    FrameConfig cfg = FrameConfig::frame3E();
    auto planResult = ReadPlan::build(subs, cfg, PlanOptions{});
    REQUIRE(planResult.hasValue());
    ReadPlan& plan = planResult.value();
    REQUIRE(plan.size() == 3);

    ValueStore store;
    store.rebuild(subs);

    for (size_t i = 0; i < plan.size(); ++i) {
        const ChunkInfo& c = plan.chunk(i);
        auto payload = wordsPayload(c.request.count, static_cast<uint16_t>(c.request.head.number));
        mc::Change changes[16];
        (void)store.apply(plan, i, view(payload), changes, 16);
    }

    CHECK(store.state(Device{DeviceType::D, 0}) == PointState::Valid);
    CHECK(store.word(Device{DeviceType::D, 0}) == 0);
    CHECK(store.word(Device{DeviceType::D, 959}) == 959);
    CHECK(store.word(Device{DeviceType::D, 960}) == 960); // second chunk's own first point.
    CHECK(store.word(Device{DeviceType::D, 1999}) == 1999); // third chunk's own last point.

    // Bulk words() straddling the 960/960 chunk boundary: one segment, no chunk seam visible.
    uint8_t buf[20];
    auto r = store.words(Device{DeviceType::D, 955}, 10, mc::MutableByteView{buf, 20});
    REQUIRE(r.hasValue());
    CHECK(r.value() == 20);
    mc::ByteView bv{buf, 20};
    for (uint32_t i = 0; i < 10; ++i) {
        CHECK(mc::convert::wordAt(bv, i) == 955 + i);
    }
}

TEST_CASE("STO-01 ValueStore: bit(), bulk bits() across two chunks of one segment") {
    RangeSet subs;
    REQUIRE(subs.add(Device{DeviceType::M, 0}, 8000).hasValue());
    PlanOptions opt;
    opt.bitsAsWords = false; // splits 7168/832 (PLN-06), one byte per point.
    FrameConfig cfg = FrameConfig::frame3E();
    auto planResult = ReadPlan::build(subs, cfg, opt);
    REQUIRE(planResult.hasValue());
    ReadPlan& plan = planResult.value();
    REQUIRE(plan.size() == 2);

    ValueStore store;
    store.rebuild(subs);

    // Global pattern bit(n) = n % 2, expressed relative to each chunk's own head so it stays
    // continuous across the chunk boundary.
    for (size_t i = 0; i < plan.size(); ++i) {
        const ChunkInfo& c = plan.chunk(i);
        uint32_t head = c.request.head.number;
        std::vector<uint8_t> payload(c.request.count);
        for (uint32_t k = 0; k < c.request.count; ++k) {
            payload[k] = ((head + k) % 2 == 1) ? 1 : 0;
        }
        mc::Change changes[64];
        (void)store.apply(plan, i, view(payload), changes, 64);
    }

    CHECK(store.bit(Device{DeviceType::M, 0}) == false);
    CHECK(store.bit(Device{DeviceType::M, 1}) == true);
    CHECK(store.bit(Device{DeviceType::M, 7167}) == (7167 % 2 == 1));
    CHECK(store.bit(Device{DeviceType::M, 7168}) == (7168 % 2 == 1)); // second chunk's own start.

    uint8_t buf[20];
    auto r = store.bits(Device{DeviceType::M, 7160}, 20, mc::MutableByteView{buf, 20});
    REQUIRE(r.hasValue());
    CHECK(r.value() == 20);
    for (uint32_t i = 0; i < 20; ++i) {
        CHECK(buf[i] == ((7160 + i) % 2));
    }

    // Same range, PackedLsbFirst: point i at bit (i % 8) of byte (i / 8).
    uint8_t packed[3] = {0, 0, 0};
    auto rp = store.bits(Device{DeviceType::M, 7160}, 20, mc::MutableByteView{packed, 3},
                         mc::BitLayout::PackedLsbFirst);
    REQUIRE(rp.hasValue());
    CHECK(rp.value() == 3);
    for (uint32_t i = 0; i < 20; ++i) {
        bool bitVal = (packed[i / 8] >> (i % 8)) & 0x01u;
        CHECK(bitVal == ((7160 + i) % 2 == 1));
    }
}

TEST_CASE("STO-02 ValueStore: NoValue -> Valid -> Failed (value kept) -> Valid; markStale; "
          "resetBaselines") {
    RangeSet subs;
    REQUIRE(subs.add(Device{DeviceType::D, 100}, 1).hasValue());
    FrameConfig cfg = FrameConfig::frame3E();
    auto planResult = ReadPlan::build(subs, cfg, PlanOptions{});
    REQUIRE(planResult.hasValue());
    ReadPlan& plan = planResult.value();
    REQUIRE(plan.size() == 1);

    ValueStore store;
    store.rebuild(subs);
    Device d{DeviceType::D, 100};

    CHECK(store.state(d) == PointState::NoValue);
    CHECK(store.word(d) == 0);

    // NoValue -> Valid: silent baseline (decision S4), no Change.
    {
        auto payload = wordsPayload(1, 42);
        mc::Change changes[4];
        size_t n = store.apply(plan, 0, view(payload), changes, 4);
        CHECK(n == 0);
        CHECK(store.state(d) == PointState::Valid);
        CHECK(store.word(d) == 42);
    }

    // Valid -> Valid, changed value: reported.
    {
        auto payload = wordsPayload(1, 43);
        mc::Change changes[4];
        size_t n = store.apply(plan, 0, view(payload), changes, 4);
        REQUIRE(n == 1);
        CHECK(changes[0].device == d);
        CHECK(changes[0].oldValue == 42);
        CHECK(changes[0].newValue == 43);
        CHECK(store.word(d) == 43);
    }

    // Failed: value kept.
    store.markFailed(plan, 0);
    CHECK(store.state(d) == PointState::Failed);
    CHECK(store.word(d) == 43);

    // Failed -> Valid, changed value: still reported (Failed counts as a baseline).
    {
        auto payload = wordsPayload(1, 44);
        mc::Change changes[4];
        size_t n = store.apply(plan, 0, view(payload), changes, 4);
        REQUIRE(n == 1);
        CHECK(changes[0].oldValue == 43);
        CHECK(changes[0].newValue == 44);
        CHECK(store.state(d) == PointState::Valid);
    }

    // markStale(): value kept, state Stale.
    store.markStale();
    CHECK(store.state(d) == PointState::Stale);
    CHECK(store.word(d) == 44);

    // resetBaselines(): baseline cleared.
    store.resetBaselines();
    CHECK(store.state(d) == PointState::NoValue);
    CHECK(store.word(d) == 0);
}

TEST_CASE("STO-03 ValueStore: gap points stay NotSubscribed, never stored, never in a Change") {
    RangeSet subs;
    REQUIRE(subs.add(Device{DeviceType::D, 100}, 5).hasValue());  // D100..D104
    REQUIRE(subs.add(Device{DeviceType::D, 110}, 5).hasValue());  // D110..D114, gap D105..D109
    FrameConfig cfg = FrameConfig::frame3E();
    // autoGap() for 3E words is 16 (PLN-03): the 5-unit gap merges into one 15-word chunk.
    auto planResult = ReadPlan::build(subs, cfg, PlanOptions{});
    REQUIRE(planResult.hasValue());
    ReadPlan& plan = planResult.value();
    REQUIRE(plan.size() == 1);
    CHECK(plan.chunk(0).request.count == 15);

    ValueStore store;
    store.rebuild(subs);
    // ValueStore's own segments are the true (non-gap-merged) footprint: two segments, not one.
    REQUIRE(store.segmentCount(DeviceType::D) == 2);

    {
        auto payload = wordsPayload(15, 1000); // baseline, silent.
        mc::Change changes[16];
        (void)store.apply(plan, 0, view(payload), changes, 16);
    }
    {
        auto payload = wordsPayload(15, 2000);
        mc::Change changes[16];
        size_t n = store.apply(plan, 0, view(payload), changes, 16);
        REQUIRE(n == 10); // only the 10 real subscribed points, never the 5 gap points.
        for (size_t i = 0; i < n; ++i) {
            CHECK((changes[i].device.number < 105 || changes[i].device.number >= 110));
        }
    }

    for (uint32_t n = 105; n < 110; ++n) {
        CHECK(store.state(Device{DeviceType::D, n}) == PointState::NotSubscribed);
        CHECK(store.word(Device{DeviceType::D, n}) == 0);
    }
    CHECK(store.state(Device{DeviceType::D, 100}) == PointState::Valid);
    CHECK(store.word(Device{DeviceType::D, 100}) == 2000);
    CHECK(store.state(Device{DeviceType::D, 114}) == PointState::Valid);
    CHECK(store.word(Device{DeviceType::D, 114}) == 2014);
}

TEST_CASE("STO-04 ValueStore: apply() on a bitsAsWords chunk reports per-point bit changes with "
          "correct hex device numbers") {
    RangeSet subs;
    REQUIRE(subs.add(Device{DeviceType::X, 0x1A}, 4).hasValue()); // aligns to X10x1 (PLN-04).
    FrameConfig cfg = FrameConfig::frame3E();
    auto planResult = ReadPlan::build(subs, cfg, PlanOptions{});
    REQUIRE(planResult.hasValue());
    ReadPlan& plan = planResult.value();
    REQUIRE(plan.size() == 1);
    CHECK(plan.chunk(0).request.head == Device{DeviceType::X, 0x10});
    CHECK(plan.chunk(0).request.count == 1);

    ValueStore store;
    store.rebuild(subs);

    // Baseline: word 0x0000 -> X1A..X1D silently become Valid/0.
    {
        auto payload = wordsPayload(1, 0x0000);
        mc::Change changes[8];
        size_t n = store.apply(plan, 0, view(payload), changes, 8);
        CHECK(n == 0);
    }

    // Flip bit 10 (X1A) and bit 12 (X1C); leave bit 11 (X1B) and bit 13 (X1D) at 0.
    {
        auto payload = wordsPayload(1, static_cast<uint16_t>((1u << 10) | (1u << 12)));
        mc::Change changes[8];
        size_t n = store.apply(plan, 0, view(payload), changes, 8);
        REQUIRE(n == 2);

        char text[8];
        (void)mc::formatDevice(changes[0].device, text, sizeof(text));
        CHECK(changes[0].device.number == 0x1A);
        CHECK(std::string_view(text) == "X1A");
        CHECK(changes[0].oldValue == 0);
        CHECK(changes[0].newValue == 1);

        (void)mc::formatDevice(changes[1].device, text, sizeof(text));
        CHECK(changes[1].device.number == 0x1C);
        CHECK(std::string_view(text) == "X1C");
        CHECK(changes[1].oldValue == 0);
        CHECK(changes[1].newValue == 1);
    }

    CHECK(store.bit(Device{DeviceType::X, 0x1A}) == true);
    CHECK(store.bit(Device{DeviceType::X, 0x1B}) == false);
    CHECK(store.bit(Device{DeviceType::X, 0x1C}) == true);
    CHECK(store.bit(Device{DeviceType::X, 0x1D}) == false);
    // Alignment padding (X10..X19, X1E, X1F): never subscribed, never stored.
    CHECK(store.state(Device{DeviceType::X, 0x10}) == PointState::NotSubscribed);
    CHECK(store.state(Device{DeviceType::X, 0x1F}) == PointState::NotSubscribed);
}

TEST_CASE("PLN-10 ValueStore::rebuild(): carries value/state for surviving points; new points "
          "NoValue; dropped points NotSubscribed") {
    RangeSet subs;
    REQUIRE(subs.add(Device{DeviceType::D, 100}, 10).hasValue()); // D100..D109
    FrameConfig cfg = FrameConfig::frame3E();
    auto planResult = ReadPlan::build(subs, cfg, PlanOptions{});
    REQUIRE(planResult.hasValue());
    ReadPlan& plan = planResult.value();
    REQUIRE(plan.size() == 1);

    ValueStore store;
    store.rebuild(subs);
    {
        auto payload = wordsPayload(10, 100); // D100=100, D101=101, ... D109=109.
        mc::Change changes[16];
        (void)store.apply(plan, 0, view(payload), changes, 16);
    }
    REQUIRE(store.state(Device{DeviceType::D, 105}) == PointState::Valid);
    REQUIRE(store.word(Device{DeviceType::D, 105}) == 105);

    // Re-plan: drop D100..D104, keep D105..D109 (via a new, overlapping subscription), add
    // D110..D114.
    RangeSet newSubs;
    REQUIRE(newSubs.add(Device{DeviceType::D, 105}, 10).hasValue()); // D105..D114

    store.rebuild(newSubs);

    for (uint32_t n = 100; n < 105; ++n) {
        CHECK(store.state(Device{DeviceType::D, n}) == PointState::NotSubscribed);
    }
    for (uint32_t n = 105; n < 110; ++n) {
        CHECK(store.state(Device{DeviceType::D, n}) == PointState::Valid);
        CHECK(store.word(Device{DeviceType::D, n}) == n);
    }
    for (uint32_t n = 110; n < 115; ++n) {
        CHECK(store.state(Device{DeviceType::D, n}) == PointState::NoValue);
        CHECK(store.word(Device{DeviceType::D, n}) == 0);
    }
}

TEST_CASE("ValueStore::word()/bit(): wrong device kind, gap, and NoValue paths (Checkpoint C "
          "coverage gap, T-028: every STO-xx test above only ever calls word()/bit() with its "
          "own matching device kind, on an already-subscribed-and-valued point)") {
    RangeSet subs;
    REQUIRE(subs.add(Device{DeviceType::D, 100}, 1).hasValue());
    REQUIRE(subs.add(Device{DeviceType::M, 0}, 1).hasValue());

    ValueStore store;
    store.rebuild(subs);

    // Wrong device kind: word() asked of a bit device, bit() asked of a word device.
    CHECK(store.word(Device{DeviceType::M, 0}) == 0);
    CHECK(store.bit(Device{DeviceType::D, 100}) == false);

    // bit() on a never-subscribed device number (gap/out of range).
    CHECK(store.bit(Device{DeviceType::M, 50}) == false);

    // bit() on a subscribed device that has never been given a value yet (NoValue).
    CHECK(store.state(Device{DeviceType::M, 0}) == PointState::NoValue);
    CHECK(store.bit(Device{DeviceType::M, 0}) == false);
}

TEST_CASE("ValueStore::words()/bits(): own error paths (Checkpoint C coverage gap, T-028: every "
          "STO-xx test above only ever exercises the success path)") {
    RangeSet subs;
    REQUIRE(subs.add(Device{DeviceType::D, 100}, 5).hasValue()); // D100..D104
    REQUIRE(subs.add(Device{DeviceType::D, 110}, 5).hasValue()); // D110..D114, gap D105..D109
    REQUIRE(subs.add(Device{DeviceType::M, 0}, 5).hasValue());   // M0..M4
    REQUIRE(subs.add(Device{DeviceType::M, 10}, 5).hasValue());  // M10..M14, gap M5..M9

    ValueStore store;
    store.rebuild(subs);

    uint8_t buf[16];

    // Wrong device kind: word device type asked of bits(), bit device type asked of words().
    auto wrongKindWords = store.words(Device{DeviceType::M, 0}, 1, MutableByteView{buf, 16});
    CHECK_FALSE(wrongKindWords.hasValue());
    CHECK(wrongKindWords.error().code == ErrorCode::NotSubscribed);

    auto wrongKindBits = store.bits(Device{DeviceType::D, 100}, 1, MutableByteView{buf, 16});
    CHECK_FALSE(wrongKindBits.hasValue());
    CHECK(wrongKindBits.error().code == ErrorCode::NotSubscribed);

    // [head, head + count) is not entirely inside one segment: head is subscribed, but the range
    // extends past that segment's own end, into the gap.
    auto spansGapWords = store.words(Device{DeviceType::D, 100}, 15, MutableByteView{buf, 16});
    CHECK_FALSE(spansGapWords.hasValue());
    CHECK(spansGapWords.error().code == ErrorCode::NotSubscribed);

    auto spansGapBits = store.bits(Device{DeviceType::M, 0}, 15, MutableByteView{buf, 16});
    CHECK_FALSE(spansGapBits.hasValue());
    CHECK(spansGapBits.error().code == ErrorCode::NotSubscribed);

    // Buffer too small: words() needs count * 2 bytes; bits() (BytePerPoint, the default) needs
    // count bytes.
    auto tooSmallWords = store.words(Device{DeviceType::D, 100}, 5, MutableByteView{buf, 8});
    CHECK_FALSE(tooSmallWords.hasValue());
    CHECK(tooSmallWords.error().code == ErrorCode::BufferTooSmall);

    auto tooSmallBits = store.bits(Device{DeviceType::M, 0}, 5, MutableByteView{buf, 3});
    CHECK_FALSE(tooSmallBits.hasValue());
    CHECK(tooSmallBits.error().code == ErrorCode::BufferTooSmall);

    // count == 0 is a no-op success (0 bytes written), for both -- not an error path, but cheap
    // to confirm alongside everything else above.
    auto zeroWords = store.words(Device{DeviceType::D, 100}, 0, MutableByteView{buf, 16});
    REQUIRE(zeroWords.hasValue());
    CHECK(zeroWords.value() == 0);
    auto zeroBits = store.bits(Device{DeviceType::M, 0}, 0, MutableByteView{buf, 16});
    REQUIRE(zeroBits.hasValue());
    CHECK(zeroBits.value() == 0);
}

TEST_CASE("ValueStore::segment()/segmentCount(): SegmentView's own head/count/words/bits/states "
          "(Checkpoint C coverage gap, T-028: never called directly by any STO-xx test above, "
          "which all go through word()/bit()/words()/bits() instead)") {
    RangeSet subs;
    REQUIRE(subs.add(Device{DeviceType::D, 100}, 5).hasValue()); // D100..D104
    REQUIRE(subs.add(Device{DeviceType::D, 110}, 5).hasValue()); // D110..D114, gap D105..D109
    REQUIRE(subs.add(Device{DeviceType::M, 0}, 3).hasValue());

    FrameConfig cfg = FrameConfig::frame3E();
    auto planResult = ReadPlan::build(subs, cfg, PlanOptions{});
    REQUIRE(planResult.hasValue());
    ReadPlan& plan = planResult.value();
    REQUIRE(plan.size() == 2); // D's own single (gap-merged) chunk, plus M's.

    ValueStore store;
    store.rebuild(subs);

    REQUIRE(store.segmentCount(DeviceType::D) == 2); // The true, non-gap-merged footprint.
    SegmentView seg0 = store.segment(DeviceType::D, 0);
    CHECK(seg0.head == Device{DeviceType::D, 100});
    CHECK(seg0.count == 5);
    CHECK(seg0.words != nullptr);
    CHECK(seg0.bits == nullptr); // Word device: no bit storage.
    CHECK(seg0.states != nullptr);

    SegmentView seg1 = store.segment(DeviceType::D, 1);
    CHECK(seg1.head == Device{DeviceType::D, 110});
    CHECK(seg1.count == 5);

    REQUIRE(store.segmentCount(DeviceType::M) == 1);
    SegmentView mSeg = store.segment(DeviceType::M, 0);
    CHECK(mSeg.head == Device{DeviceType::M, 0});
    CHECK(mSeg.count == 3);
    CHECK(mSeg.words == nullptr); // Bit device: no word storage.
    CHECK(mSeg.bits != nullptr);

    // The pointers are live storage, not copies: apply()'s own write is visible through them.
    for (size_t i = 0; i < plan.size(); ++i) {
        const ChunkInfo& c = plan.chunk(i);
        if (c.request.head.type == DeviceType::D) {
            auto payload = wordsPayload(c.request.count, 1000);
            mc::Change changes[16];
            (void)store.apply(plan, i, view(payload), changes, 16);
        }
    }
    SegmentView seg0After = store.segment(DeviceType::D, 0);
    CHECK(seg0After.words[0] == 1000); // D100's own value, via the segment's own pointer.
    CHECK(static_cast<PointState>(seg0After.states[0]) == PointState::Valid);
}

TEST_CASE("ValueStore::rebuild(): extends a merge across two overlapping subscriptions added to "
          "the same RangeSet, and carries a bit-device baseline across a second rebuild() "
          "(Checkpoint C coverage gap, T-028: PLN-10 above only ever exercises a word device with "
          "one subscription per RangeSet, so rebuild()'s own multi-entry merge-extend branch and "
          "its bit-kind carry-over copy are never reached)") {
    RangeSet subs;
    // Two overlapping M subscriptions in the *same* RangeSet (unlike PLN-10's own two separate
    // RangeSets): entries sorted (head.number) put [0,8) before [4,16), so merging the second one
    // extends the first's own end (8 -> 16) rather than just leaving it alone.
    REQUIRE(subs.add(Device{DeviceType::M, 0}, 8).hasValue());  // M0..M7
    REQUIRE(subs.add(Device{DeviceType::M, 4}, 12).hasValue()); // M4..M15, overlaps and extends

    ValueStore store;
    store.rebuild(subs);
    REQUIRE(store.segmentCount(DeviceType::M) == 1); // Merged into exactly one run, M0..M15.
    CHECK(store.segment(DeviceType::M, 0).count == 16);

    // Give M0..M7 a baseline via a one-chunk ReadPlan covering exactly that range.
    RangeSet firstOnly;
    REQUIRE(firstOnly.add(Device{DeviceType::M, 0}, 8).hasValue());
    FrameConfig cfg = FrameConfig::frame3E();
    PlanOptions opt;
    opt.bitsAsWords = false;
    auto planResult = ReadPlan::build(firstOnly, cfg, opt);
    REQUIRE(planResult.hasValue());
    ReadPlan& plan = planResult.value();
    REQUIRE(plan.size() == 1);

    std::vector<uint8_t> payload(8);
    for (uint32_t i = 0; i < 8; ++i) {
        payload[i] = static_cast<uint8_t>(i % 2); // M0=0, M1=1, M2=0, ...
    }
    mc::Change changes[16];
    (void)store.apply(plan, 0, view(payload), changes, 16); // Silent baseline (NoValue -> Valid).
    REQUIRE(store.state(Device{DeviceType::M, 5}) == PointState::Valid);
    REQUIRE(store.bit(Device{DeviceType::M, 5}) == true);

    // Re-plan with the merged M0..M15 subscription (subs, from above): M0..M7's own bit values
    // and Valid states must survive into the new, larger segment -- exactly the
    // `newTs.bits[newIdx] = ts.bits[oldIdx]` carry-over line ValueStore::rebuild() has for a bit
    // device (STO/PLN-10's own carry-over check only ever runs this for a *word* device).
    store.rebuild(subs);
    CHECK(store.state(Device{DeviceType::M, 5}) == PointState::Valid);
    CHECK(store.bit(Device{DeviceType::M, 5}) == true);
    CHECK(store.state(Device{DeviceType::M, 4}) == PointState::Valid);
    CHECK(store.bit(Device{DeviceType::M, 4}) == false);
    CHECK(store.state(Device{DeviceType::M, 10}) == PointState::NoValue); // Never given a value.
}

TEST_CASE("ValueStore::markFailed(): skips gap points within a merged chunk (Checkpoint C "
          "coverage gap, T-028: STO-02's own markFailed() call has no gap in its chunk at all)") {
    RangeSet subs;
    REQUIRE(subs.add(Device{DeviceType::D, 100}, 5).hasValue()); // D100..D104
    REQUIRE(subs.add(Device{DeviceType::D, 110}, 5).hasValue()); // D110..D114, gap D105..D109
    FrameConfig cfg = FrameConfig::frame3E();
    auto planResult = ReadPlan::build(subs, cfg, PlanOptions{});
    REQUIRE(planResult.hasValue());
    ReadPlan& plan = planResult.value();
    REQUIRE(plan.size() == 1); // The 5-point gap merges (autoGap() is 16, PLN-03).
    CHECK(plan.chunk(0).request.count == 15);

    ValueStore store;
    store.rebuild(subs);

    // markFailed() only promotes a point to Failed when its current state is not NoValue (spec:
    // a point never reported at all stays silently NoValue, not Failed) -- give every subscribed
    // point a baseline first (STO-02's own NoValue -> Valid convention), otherwise this whole
    // chunk would still be NoValue and the gap-skip logic below would never be distinguishable
    // from "nothing happened yet".
    {
        auto payload = wordsPayload(15, 100);
        mc::Change changes[16];
        (void)store.apply(plan, 0, view(payload), changes, 16);
    }
    REQUIRE(store.state(Device{DeviceType::D, 100}) == PointState::Valid);

    store.markFailed(plan, 0);

    CHECK(store.state(Device{DeviceType::D, 100}) == PointState::Failed);
    CHECK(store.state(Device{DeviceType::D, 114}) == PointState::Failed);
    // The gap points were never subscribed (NoValue's own PointState default, not a segment
    // marker as such) and markFailed()'s own "not NoValue" guard leaves a never-subscribed point
    // untouched by design -- confirmed here as NotSubscribed specifically, not silently promoted
    // to Failed by an off-by-one in the gap-skip logic.
    for (uint32_t n = 105; n < 110; ++n) {
        CHECK(store.state(Device{DeviceType::D, n}) == PointState::NotSubscribed);
    }
}
