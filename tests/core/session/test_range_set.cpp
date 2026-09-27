// RangeSet and ReadPlan (spec SPEC-core-session.md, poll_plan.h): PLN-01, 02, 04..09.
//
// PLN-10 ("Re-plan carries value and state for surviving points; new points NoValue; dropped
// points NotSubscribed") exercises PointState::NoValue/NotSubscribed and value carry-over, which
// is ValueStore::rebuild()'s own contract (value_store.h, T-021) -- RangeSet/ReadPlan (this
// task's own files) have no notion of a point's "value" at all. Flagged to the leader in this
// task's Dev notes; the actual PLN-10 test case is added in T-021's test_value_store.cpp against
// ValueStore::rebuild(), and both are verified together by the phase's `ctest -L core_session`
// (rules.md, phase-batched verification).
#include "doctest/doctest.h"

#include "mc/core/poll_plan.h"

#include <cstdint>
#include <utility>

using mc::ChunkInfo;
using mc::DataCode;
using mc::Device;
using mc::DeviceType;
using mc::Error;
using mc::ErrorCode;
using mc::FrameConfig;
using mc::Op;
using mc::PlanOptions;
using mc::RangeSet;
using mc::ReadPlan;
using mc::SubscriptionId;

TEST_CASE("PLN-01 ReadPlan::build(): adjacent subscriptions merge into one chunk") {
    RangeSet subs;
    REQUIRE(subs.add(Device{DeviceType::D, 100}, 10).hasValue());
    REQUIRE(subs.add(Device{DeviceType::D, 110}, 10).hasValue());

    FrameConfig cfg = FrameConfig::frame3E();
    auto planResult = ReadPlan::build(subs, cfg, PlanOptions{});
    REQUIRE(planResult.hasValue());
    ReadPlan& plan = planResult.value();

    auto range = plan.chunksOf(DeviceType::D);
    REQUIRE(range.second - range.first == 1);
    const ChunkInfo& c = plan.chunk(range.first);
    CHECK(c.request.op == Op::ReadWords);
    CHECK(c.request.head == Device{DeviceType::D, 100});
    CHECK(c.request.count == 20);
}

TEST_CASE("PLN-02 ReadPlan::build(): overlapping subscriptions union; remove() keeps the other") {
    RangeSet subs;
    auto id1 = subs.add(Device{DeviceType::D, 100}, 20); // D100..D119
    auto id2 = subs.add(Device{DeviceType::D, 110}, 20); // D110..D129, overlaps id1
    REQUIRE(id1.hasValue());
    REQUIRE(id2.hasValue());

    FrameConfig cfg = FrameConfig::frame3E();
    auto plan1Result = ReadPlan::build(subs, cfg, PlanOptions{});
    REQUIRE(plan1Result.hasValue());
    {
        ReadPlan& plan1 = plan1Result.value();
        auto range = plan1.chunksOf(DeviceType::D);
        REQUIRE(range.second - range.first == 1);
        CHECK(plan1.chunk(range.first).request.head == Device{DeviceType::D, 100});
        CHECK(plan1.chunk(range.first).request.count == 30); // union D100..D129
    }

    REQUIRE(subs.remove(id1.value()).hasValue());
    auto plan2Result = ReadPlan::build(subs, cfg, PlanOptions{});
    REQUIRE(plan2Result.hasValue());
    ReadPlan& plan2 = plan2Result.value();
    auto range2 = plan2.chunksOf(DeviceType::D);
    REQUIRE(range2.second - range2.first == 1);
    CHECK(plan2.chunk(range2.first).request.head == Device{DeviceType::D, 110});
    CHECK(plan2.chunk(range2.first).request.count == 20);
}

TEST_CASE("PLN-03 autoGap(): 3E Binary and 3E ASCII words = 16 (1E value added in T37)") {
    CHECK(mc::autoGap(FrameConfig::frame3E(DataCode::Binary), Op::ReadWords) == 16);
    CHECK(mc::autoGap(FrameConfig::frame3E(DataCode::Ascii), Op::ReadWords) == 16);
}

TEST_CASE("PLN-03 ReadPlan::build(): a gap of autoGap() merges, autoGap() + 1 splits") {
    FrameConfig cfg = FrameConfig::frame3E();
    uint32_t gap = mc::autoGap(cfg, Op::ReadWords);
    REQUIRE(gap == 16);

    {
        RangeSet subs;
        REQUIRE(subs.add(Device{DeviceType::D, 100}, 10).hasValue());       // D100..D109
        REQUIRE(subs.add(Device{DeviceType::D, 110 + gap}, 10).hasValue()); // gap units after
        auto planResult = ReadPlan::build(subs, cfg, PlanOptions{});
        REQUIRE(planResult.hasValue());
        auto range = planResult.value().chunksOf(DeviceType::D);
        CHECK(range.second - range.first == 1);
    }
    {
        RangeSet subs;
        REQUIRE(subs.add(Device{DeviceType::D, 100}, 10).hasValue());
        REQUIRE(subs.add(Device{DeviceType::D, 110 + gap + 1}, 10).hasValue()); // one unit wider
        auto planResult = ReadPlan::build(subs, cfg, PlanOptions{});
        REQUIRE(planResult.hasValue());
        auto range = planResult.value().chunksOf(DeviceType::D);
        CHECK(range.second - range.first == 2);
    }
}

TEST_CASE("PLN-04 ReadPlan::build(): bitsAsWords aligns a bit subscription to a 16-point "
          "boundary") {
    FrameConfig cfg = FrameConfig::frame3E();

    {
        RangeSet subs;
        REQUIRE(subs.add(Device{DeviceType::M, 5}, 3).hasValue());
        auto planResult = ReadPlan::build(subs, cfg, PlanOptions{});
        REQUIRE(planResult.hasValue());
        auto range = planResult.value().chunksOf(DeviceType::M);
        REQUIRE(range.second - range.first == 1);
        const ChunkInfo& c = planResult.value().chunk(range.first);
        CHECK(c.request.op == Op::ReadWords);
        CHECK(c.request.head == Device{DeviceType::M, 0});
        CHECK(c.request.count == 1);
    }
    {
        RangeSet subs;
        REQUIRE(subs.add(Device{DeviceType::X, 0x1A}, 4).hasValue());
        auto planResult = ReadPlan::build(subs, cfg, PlanOptions{});
        REQUIRE(planResult.hasValue());
        auto range = planResult.value().chunksOf(DeviceType::X);
        REQUIRE(range.second - range.first == 1);
        const ChunkInfo& c = planResult.value().chunk(range.first);
        CHECK(c.request.op == Op::ReadWords);
        CHECK(c.request.head == Device{DeviceType::X, 0x10});
        CHECK(c.request.count == 1);
    }
}

TEST_CASE("PLN-04 ReadPlan::build(): bitsAsWords honours the M9000-M9255 special case on 1E") {
    FrameConfig cfg = FrameConfig::frame1E();

    {
        RangeSet subs;
        REQUIRE(subs.add(Device{DeviceType::M, 9005}, 4).hasValue());
        auto planResult = ReadPlan::build(subs, cfg, PlanOptions{});
        REQUIRE(planResult.hasValue());
        auto range = planResult.value().chunksOf(DeviceType::M);
        REQUIRE(range.second - range.first == 1);
        const ChunkInfo& c = planResult.value().chunk(range.first);
        CHECK(c.request.head == Device{DeviceType::M, 9000});
        CHECK(c.request.count == 1);
    }
    {
        RangeSet subs;
        REQUIRE(subs.add(Device{DeviceType::M, 9020}, 4).hasValue());
        auto planResult = ReadPlan::build(subs, cfg, PlanOptions{});
        REQUIRE(planResult.hasValue());
        auto range = planResult.value().chunksOf(DeviceType::M);
        REQUIRE(range.second - range.first == 1);
        const ChunkInfo& c = planResult.value().chunk(range.first);
        CHECK(c.request.head == Device{DeviceType::M, 9016});
        CHECK(c.request.count == 1);
    }
}

TEST_CASE("PLN-05 ReadPlan::build(): bitsAsWords = false reads bits directly, unaligned") {
    RangeSet subs;
    REQUIRE(subs.add(Device{DeviceType::M, 5}, 3).hasValue());

    PlanOptions opt;
    opt.bitsAsWords = false;
    auto planResult = ReadPlan::build(subs, FrameConfig::frame3E(), opt);
    REQUIRE(planResult.hasValue());
    auto range = planResult.value().chunksOf(DeviceType::M);
    REQUIRE(range.second - range.first == 1);
    const ChunkInfo& c = planResult.value().chunk(range.first);
    CHECK(c.request.op == Op::ReadBits);
    CHECK(c.request.head == Device{DeviceType::M, 5});
    CHECK(c.request.count == 3);
}

TEST_CASE("PLN-06 ReadPlan::build(): D0x2000 on 3E Binary splits 960/960/80") {
    RangeSet subs;
    REQUIRE(subs.add(Device{DeviceType::D, 0}, 2000).hasValue());

    auto planResult = ReadPlan::build(subs, FrameConfig::frame3E(), PlanOptions{});
    REQUIRE(planResult.hasValue());
    ReadPlan& plan = planResult.value();
    auto range = plan.chunksOf(DeviceType::D);
    REQUIRE(range.second - range.first == 3);

    CHECK(plan.chunk(range.first + 0).request.head == Device{DeviceType::D, 0});
    CHECK(plan.chunk(range.first + 0).request.count == 960);
    CHECK(plan.chunk(range.first + 1).request.head == Device{DeviceType::D, 960});
    CHECK(plan.chunk(range.first + 1).request.count == 960);
    CHECK(plan.chunk(range.first + 2).request.head == Device{DeviceType::D, 1920});
    CHECK(plan.chunk(range.first + 2).request.count == 80);
}

TEST_CASE("PLN-06 ReadPlan::build(): M0x8000 bitsAsWords is one 500-word chunk, vs two as bits") {
    RangeSet subs;
    REQUIRE(subs.add(Device{DeviceType::M, 0}, 8000).hasValue());
    FrameConfig cfg = FrameConfig::frame3E();

    {
        auto planResult = ReadPlan::build(subs, cfg, PlanOptions{}); // bitsAsWords true (default)
        REQUIRE(planResult.hasValue());
        auto range = planResult.value().chunksOf(DeviceType::M);
        REQUIRE(range.second - range.first == 1);
        const ChunkInfo& c = planResult.value().chunk(range.first);
        CHECK(c.request.op == Op::ReadWords);
        CHECK(c.request.head == Device{DeviceType::M, 0});
        CHECK(c.request.count == 500);
    }
    {
        PlanOptions opt;
        opt.bitsAsWords = false;
        auto planResult = ReadPlan::build(subs, cfg, opt);
        REQUIRE(planResult.hasValue());
        ReadPlan& plan = planResult.value();
        auto range = plan.chunksOf(DeviceType::M);
        REQUIRE(range.second - range.first == 2);
        CHECK(plan.chunk(range.first + 0).request.op == Op::ReadBits);
        CHECK(plan.chunk(range.first + 0).request.count == 7168);
        CHECK(plan.chunk(range.first + 1).request.count == 832);
    }
}

TEST_CASE("PLN-07 ReadPlan::build(): chunk order is (DeviceType, head); chunksOf() ranges are "
          "contiguous") {
    RangeSet subs;
    REQUIRE(subs.add(Device{DeviceType::D, 200}, 5).hasValue());
    REQUIRE(subs.add(Device{DeviceType::D, 0}, 5).hasValue());
    REQUIRE(subs.add(Device{DeviceType::X, 0}, 5).hasValue());
    REQUIRE(subs.add(Device{DeviceType::M, 0}, 5).hasValue());

    PlanOptions opt;
    opt.bitsAsWords = false; // exact head/count, no alignment, to keep this test's math simple.
    auto planResult = ReadPlan::build(subs, FrameConfig::frame3E(), opt);
    REQUIRE(planResult.hasValue());
    ReadPlan& plan = planResult.value();

    auto xRange = plan.chunksOf(DeviceType::X);
    auto mRange = plan.chunksOf(DeviceType::M);
    auto dRange = plan.chunksOf(DeviceType::D);

    REQUIRE(xRange.second - xRange.first == 1);
    REQUIRE(mRange.second - mRange.first == 1);
    REQUIRE(dRange.second - dRange.first == 2); // D0x5 and D200x5 are far apart: no merge.

    // X (DeviceType declaration order 2) before M (order 4) before D (order 9); every type in
    // between (SM, SD, Y, then L, F, V, B) has no subscription, so each range starts exactly
    // where the previous non-empty type's own range ends.
    CHECK(xRange.first == 0);
    CHECK(xRange.second == mRange.first);
    CHECK(mRange.second == dRange.first);
    CHECK(dRange.second == plan.size());

    CHECK(plan.chunk(dRange.first).request.head == Device{DeviceType::D, 0});
    CHECK(plan.chunk(dRange.first + 1).request.head == Device{DeviceType::D, 200});
}

TEST_CASE("PLN-08 ReadPlan::build(): an empty RangeSet produces an empty plan") {
    // The rest of PLN-08 ("round still runs, CycleDone emitted") is Session behaviour, tested at
    // T-022/T-023 (SES-xx) once Session exists; this task covers only the planner half.
    RangeSet subs;
    auto planResult = ReadPlan::build(subs, FrameConfig::frame3E(), PlanOptions{});
    REQUIRE(planResult.hasValue());
    ReadPlan& plan = planResult.value();

    CHECK(plan.size() == 0);
    CHECK(plan.maxPayloadSize() == 0);
    CHECK(plan.maxChunkPoints() == 0);
    for (uint16_t t = 0; t < static_cast<uint16_t>(DeviceType::Count); ++t) {
        auto range = plan.chunksOf(static_cast<DeviceType>(t));
        CHECK(range.first == range.second);
    }
}

TEST_CASE("PLN-09 ReadPlan::build(): rejects an unsupported device and keeps the previous plan") {
    RangeSet goodSubs;
    REQUIRE(goodSubs.add(Device{DeviceType::D, 0}, 10).hasValue());
    auto plan1Result = ReadPlan::build(goodSubs, FrameConfig::frame3E(), PlanOptions{});
    REQUIRE(plan1Result.hasValue());
    ReadPlan plan1 = std::move(plan1Result.value());
    CHECK(plan1.size() == 1);

    RangeSet badSubs;
    // SM has no 1E code at all (device_table.cpp, spec section 3.2 footnote 1): rejected purely
    // by validate() (core-model), which chunkCount()/chunk() call before anything frame_1e.cpp
    // would need to exist for.
    REQUIRE(badSubs.add(Device{DeviceType::SM, 0}, 1).hasValue());
    auto plan2Result = ReadPlan::build(badSubs, FrameConfig::frame1E(), PlanOptions{});
    CHECK_FALSE(plan2Result.hasValue());
    CHECK(plan2Result.error().code == ErrorCode::InvalidDevice);

    // plan1 is a separate object: a failed build() above never touched it.
    CHECK(plan1.size() == 1);
    CHECK(plan1.chunk(0).request.head == Device{DeviceType::D, 0});
}

TEST_CASE("RangeSet::add()/remove(): own error paths, and size()/empty() (Checkpoint C coverage "
          "gap, T-028: never directly exercised by the PLN-xx tests above, which only ever call "
          "ReadPlan::build() on top of an already-valid RangeSet)") {
    RangeSet subs;
    CHECK(subs.empty());
    CHECK(subs.size() == 0);

    auto zeroCount = subs.add(Device{DeviceType::D, 0}, 0);
    CHECK_FALSE(zeroCount.hasValue());
    CHECK(zeroCount.error().code == ErrorCode::PointCount);
    CHECK(subs.empty()); // A rejected add() adds nothing.

    // head.number (2) + count (UINT32_MAX) - 1 overflows past the 32-bit device-number range
    // add()'s own uint64_t check exists for (head.number (1) would land exactly on 0xFFFFFFFF,
    // still in range -- the check is "> ", not ">=").
    auto overflow = subs.add(Device{DeviceType::D, 2}, 0xFFFFFFFFu);
    CHECK_FALSE(overflow.hasValue());
    CHECK(overflow.error().code == ErrorCode::InvalidDevice);
    CHECK(subs.empty());

    auto added = subs.add(Device{DeviceType::D, 100}, 10);
    REQUIRE(added.hasValue());
    CHECK_FALSE(subs.empty());
    CHECK(subs.size() == 1);

    auto missing = subs.remove(static_cast<SubscriptionId>(added.value() + 1000)); // Never issued.
    CHECK_FALSE(missing.hasValue());
    CHECK(missing.error().code == ErrorCode::NotSubscribed);
    CHECK(subs.size() == 1); // A rejected remove() removes nothing.

    REQUIRE(subs.remove(added.value()).hasValue());
    CHECK(subs.empty());
    CHECK(subs.size() == 0);
}

TEST_CASE("PLN-09 ReadPlan::build(): rejects a device number past the frame's field width (1C)") {
    RangeSet subs;
    // 1C ACPU's D field is 4 decimal digits (device_encode.h, validate.cpp deviceNumberLimit()):
    // 10000 does not fit, unlike the 9999 ceiling.
    REQUIRE(subs.add(Device{DeviceType::D, 10000}, 1).hasValue());
    auto planResult = ReadPlan::build(subs, FrameConfig::frame1C(), PlanOptions{});
    CHECK_FALSE(planResult.hasValue());
    CHECK(planResult.error().code == ErrorCode::InvalidDevice);
}
