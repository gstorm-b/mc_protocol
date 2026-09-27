// ReadPlan::build() and autoGap() (spec `SPEC-core-session.md`, poll_plan.h): per device type,
// each subscription becomes an aligned interval (bitsAsWords: 16-point/word boundary), the
// type's intervals are unioned and gap-merged, then split into chunks with mc::chunk(). All
// interval arithmetic below is done in uint64_t native device-number units (bits for bit devices,
// words for word devices) so a subscription that legally reaches the top of a 32-bit device
// number (RangeSet::add's own widest case) never overflows while it is being aligned or merged;
// values are narrowed back to uint32_t only once they become a Device/Request, which by then have
// already passed RangeSet::add()'s own range check.
#include "mc/core/poll_plan.h"

#include "mc/core/limits.h"
#include "mc/core/protocol.h"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace mc {
namespace {

/// Half-open interval [start, end) in native device-number units.
struct Interval {
    uint64_t start;
    uint64_t end;
};

bool intervalStartLess(const Interval& a, const Interval& b) noexcept { return a.start < b.start; }

/// Whether word access to a bit device on `cfg` must land on a 16-point boundary (spec §3.5).
/// Mirrors the condition src/core/model/validate.cpp checks before rejecting an unaligned
/// request (that file's own needsWordAlignmentCheck(), private to it) -- kept in sync by hand
/// since neither module exposes it to the other; SPEC-core-session.md ties the two together
/// explicitly ("M9000-M9255 on 1E/1C").
bool needsWordAlignment(const FrameConfig& cfg) noexcept {
    return cfg.frame == FrameType::F1E || cfg.frame == FrameType::F1C || cfg.aSeriesTarget;
}

/// Aligns one subscription's native point range down/up to a 16-point (word) boundary for
/// `PlanOptions::bitsAsWords` planning. The M9000-M9255 special case (spec §10 Q8: boundaries at
/// 9000 + 16k, since 9000 itself is not a multiple of 16) applies only where `needsWordAlignment`
/// would also enforce it on send; every other bit device, and M outside that range, aligns to a
/// plain multiple of 16.
Interval alignToWordBoundary(DeviceType type, uint32_t start, uint32_t count,
                              const FrameConfig& cfg) noexcept {
    uint64_t origin = 0;
    if (type == DeviceType::M && needsWordAlignment(cfg) && start >= 9000 && start <= 9255) {
        origin = 9000;
    }
    uint64_t s = start;
    uint64_t last = s + count - 1; // count >= 1: RangeSet::add() already rejects count == 0.
    uint64_t alignedStart = origin + ((s - origin) / 16) * 16;
    uint64_t alignedEnd = origin + (((last - origin) / 16) + 1) * 16;
    return Interval{alignedStart, alignedEnd};
}

/// Sorts `intervals` and merges overlapping, adjacent, or gap-separated-by-at-most-`gapNative`
/// neighbours into the smallest possible set of disjoint intervals (spec "Algorithm", the union
/// and gap-merge steps -- one pass covers both, since "overlapping/adjacent" is exactly the
/// gap-<=0 case that a `gapNative >= 0` threshold already includes).
std::vector<Interval> unionAndMergeGaps(std::vector<Interval> intervals,
                                         uint64_t gapNative) noexcept {
    std::sort(intervals.begin(), intervals.end(), intervalStartLess);
    std::vector<Interval> merged;
    merged.reserve(intervals.size());
    for (const Interval& iv : intervals) {
        if (!merged.empty() && iv.start <= merged.back().end + gapNative) {
            if (iv.end > merged.back().end) {
                merged.back().end = iv.end;
            }
        } else {
            merged.push_back(iv);
        }
    }
    return merged;
}

/// Sum of the overlap between `[start, end)` and the sorted, disjoint intervals of `raw`, advancing
/// the shared `rawIndex` forward only past entries fully consumed by this call. Both `raw` and the
/// sequence of `[start, end)` ranges this is called with (one call per chunk, in increasing chunk
/// order) are already sorted and disjoint, so a single shared index never revisits an entry.
size_t overlapWithRaw(const std::vector<Interval>& raw, size_t& rawIndex, uint64_t start,
                       uint64_t end) noexcept {
    size_t total = 0;
    while (rawIndex < raw.size() && raw[rawIndex].start < end) {
        uint64_t s = raw[rawIndex].start > start ? raw[rawIndex].start : start;
        uint64_t e = raw[rawIndex].end < end ? raw[rawIndex].end : end;
        if (e > s) {
            total += static_cast<size_t>(e - s);
        }
        if (raw[rawIndex].end <= end) {
            ++rawIndex;
        } else {
            break;
        }
    }
    return total;
}

} // namespace

uint32_t autoGap(const FrameConfig& cfg, Op readOp) noexcept {
    bool isBitOp = (readOp == Op::ReadBits);
    // Device symbol/number never affects a QnA/1E/1C read's own wire size (only its kind, already
    // fixed by isBitOp, does), so any always-supported device stands in here: M for bit reads, D
    // for word reads.
    Device device = isBitOp ? Device{DeviceType::M, 0} : Device{DeviceType::D, 0};

    constexpr uint16_t kSmallCount = 1;
    // Large enough that its response wire data dominates maxResponseSize()'s own max(wireData,
    // errorInfo) for every implemented frame, so the difference between kBigCount and
    // kBigCount + 1 below isolates the marginal per-unit wire cost rather than being swamped by
    // the (fixed-size) PLC error-information block.
    constexpr uint16_t kBigCount = 100;

    Request small = isBitOp ? Request::readBits(device, kSmallCount)
                             : Request::readWords(device, kSmallCount);
    Request big = isBitOp ? Request::readBits(device, kBigCount)
                           : Request::readWords(device, kBigCount);
    Request bigPlusOne = isBitOp
                              ? Request::readBits(device, static_cast<uint16_t>(kBigCount + 1))
                              : Request::readWords(device, static_cast<uint16_t>(kBigCount + 1));

    McProtocol proto(cfg);
    auto requestSizeResult = proto.encodedSize(small);
    if (!requestSizeResult.hasValue()) {
        return 0; // Frame not implemented yet, or this device kind unsupported by it.
    }

    // maxResponseSize() is header + end code + max(wire data size, PLC error-information size)
    // (protocol.h); at kBigCount/kBigCount + 1 the wire data term dominates for every implemented
    // frame, so the envelope (header + end code, constant across count) cancels out of the
    // difference, leaving exactly the marginal wire bytes one more read unit costs.
    size_t respBig = proto.maxResponseSize(big);
    size_t respBigPlusOne = proto.maxResponseSize(bigPlusOne);
    if (respBig == 0 || respBigPlusOne <= respBig) {
        return 0;
    }
    size_t unitWireSize = respBigPlusOne - respBig;

    // Extrapolated back down to kSmallCount units, still with the envelope included (respBig
    // already is envelope + wireData(kBigCount); removing (kBigCount - kSmallCount) marginal
    // units' worth of wire data leaves envelope + wireData(kSmallCount)).
    size_t successResponseSmall =
        respBig - static_cast<size_t>(kBigCount - kSmallCount) * unitWireSize;
    size_t requestSizeSmall = requestSizeResult.value();

    size_t numerator = requestSizeSmall + successResponseSmall;
    if (numerator < unitWireSize) {
        return 0;
    }
    return static_cast<uint32_t>((numerator - unitWireSize) / unitWireSize);
}

Expected<ReadPlan> ReadPlan::build(const RangeSet& subs, const FrameConfig& cfg,
                                    const PlanOptions& opt) {
    ReadPlan plan;
    McProtocol proto(cfg);

    size_t entryIndex = 0;
    for (uint16_t t = 0; t < static_cast<uint16_t>(DeviceType::Count); ++t) {
        DeviceType type = static_cast<DeviceType>(t);
        size_t typeStart = plan.m_chunks.size();

        const DeviceInfo& info = deviceInfo(type);
        bool bitsAsWords = opt.bitsAsWords && info.kind == DeviceKind::Bit;
        uint64_t unitSize = bitsAsWords ? 16u : 1u;
        Op readOp = (info.kind == DeviceKind::Bit && !bitsAsWords) ? Op::ReadBits : Op::ReadWords;

        std::vector<Interval> raw;
        std::vector<Interval> aligned;
        while (entryIndex < subs.m_entries.size() && subs.m_entries[entryIndex].head.type == type) {
            const RangeSet::Entry& e = subs.m_entries[entryIndex];
            Interval rawIv{e.head.number, static_cast<uint64_t>(e.head.number) + e.count};
            raw.push_back(rawIv);
            aligned.push_back(bitsAsWords ? alignToWordBoundary(type, e.head.number, e.count, cfg)
                                          : rawIv);
            ++entryIndex;
        }

        if (raw.empty()) {
            plan.m_typeRanges[t] = {typeStart, typeStart};
            continue;
        }

        std::vector<Interval> rawUnion = unionAndMergeGaps(std::move(raw), 0);

        uint32_t gapReadUnits = (opt.maxGap == kAutoGap) ? autoGap(cfg, readOp) : opt.maxGap;
        uint64_t gapNative = static_cast<uint64_t>(gapReadUnits) * unitSize;
        std::vector<Interval> planIntervals = unionAndMergeGaps(std::move(aligned), gapNative);

        size_t rawIndex = 0;
        for (const Interval& iv : planIntervals) {
            uint64_t remainingUnits = (iv.end - iv.start) / unitSize;
            uint64_t sliceStart = iv.start;

            while (remainingUnits > 0) {
                uint16_t sliceCount =
                    static_cast<uint16_t>(remainingUnits > 0xFFFFu ? 0xFFFFu : remainingUnits);
                Device sliceHead{type, static_cast<uint32_t>(sliceStart)};
                Request r = (readOp == Op::ReadWords) ? Request::readWords(sliceHead, sliceCount)
                                                       : Request::readBits(sliceHead, sliceCount);

                auto countResult = chunkCount(r, cfg);
                if (!countResult.hasValue()) {
                    return Expected<ReadPlan>(countResult.error());
                }
                size_t n = countResult.value();
                std::vector<Chunk> rawChunks(n);
                // Qualified as ::mc::chunk: ReadPlan's own chunk(size_t) accessor (poll_plan.h)
                // is a member of this same class, so unqualified lookup from inside
                // ReadPlan::build() would find it first and hide mc::chunk() (limits.h) instead
                // of overload-resolving between them.
                auto chunkResult = ::mc::chunk(r, cfg, rawChunks.data(), n);
                if (!chunkResult.hasValue()) {
                    return Expected<ReadPlan>(chunkResult.error());
                }

                for (const Chunk& c : rawChunks) {
                    ChunkInfo ci{};
                    ci.request = (readOp == Op::ReadWords)
                                     ? Request::readWords(Device{type, c.headNumber}, c.count)
                                     : Request::readBits(Device{type, c.headNumber}, c.count);
                    plan.m_chunks.push_back(ci);

                    uint64_t chunkNativeEnd = static_cast<uint64_t>(c.headNumber) +
                                              static_cast<uint64_t>(c.count) * unitSize;
                    size_t subscribed =
                        overlapWithRaw(rawUnion, rawIndex, c.headNumber, chunkNativeEnd);
                    if (subscribed > plan.m_maxChunkPoints) {
                        plan.m_maxChunkPoints = subscribed;
                    }

                    size_t payload = proto.payloadSize(ci.request);
                    if (payload > plan.m_maxPayloadSize) {
                        plan.m_maxPayloadSize = payload;
                    }
                }

                sliceStart += static_cast<uint64_t>(sliceCount) * unitSize;
                remainingUnits -= sliceCount;
            }
        }

        plan.m_typeRanges[t] = {typeStart, plan.m_chunks.size()};
    }

    return Expected<ReadPlan>(std::move(plan));
}

size_t ReadPlan::size() const noexcept { return m_chunks.size(); }

const ChunkInfo& ReadPlan::chunk(size_t i) const noexcept { return m_chunks[i]; }

std::pair<size_t, size_t> ReadPlan::chunksOf(DeviceType t) const noexcept {
    return m_typeRanges[static_cast<size_t>(t)];
}

size_t ReadPlan::maxPayloadSize() const noexcept { return m_maxPayloadSize; }

size_t ReadPlan::maxChunkPoints() const noexcept { return m_maxChunkPoints; }

} // namespace mc
