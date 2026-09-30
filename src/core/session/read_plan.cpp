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

#include "core/session/word_align.h"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace mc {
namespace {

using detail::alignToWordBoundary;
using detail::Interval;

bool intervalStartLess(const Interval& a, const Interval& b) noexcept { return a.start < b.start; }

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

    McProtocol proto(cfg);
    auto sizeFor = [&](uint16_t count) -> size_t {
        Request r = isBitOp ? Request::readBits(device, count) : Request::readWords(device, count);
        return proto.maxResponseSize(r);
    };
    auto requestSizeResult = proto.encodedSize(isBitOp ? Request::readBits(device, 1)
                                                        : Request::readWords(device, 1));
    if (!requestSizeResult.hasValue()) {
        return 0; // Frame not implemented yet, or this device kind unsupported by it.
    }

    // maxResponseSize() is header + end code + max(wire data size, PLC error-information size)
    // (protocol.h). At 100 and 200 units the wire data term dominates for every implemented frame
    // (no error-information block is as large), so the envelope (header + end code) cancels out of
    // r200 - r100, which is the wire size of exactly 100 more units: `hundredUnits` bytes or
    // characters. A bit read is not a whole number of bytes per unit (Binary packs two points per
    // byte), so the per-unit cost is the fraction hundredUnits / 100, never rounded.
    const size_t r100 = sizeFor(100);
    const size_t r200 = sizeFor(200);
    const size_t r101 = sizeFor(101); // odd count: includes the pad nibble / dummy character
    if (r100 == 0 || r200 <= r100 || r101 == 0) {
        return 0;
    }
    const size_t hundredUnits = r200 - r100;
    // The real success response to one unit: the 101-unit response minus 100 units of data.
    if (r101 <= hundredUnits) {
        return 0;
    }
    const size_t responseSmall = r101 - hundredUnits;
    const size_t requestSmall = requestSizeResult.value();

    // Spec formula: floor((request(1) + response(1) - unit) / unit) with unit = hundredUnits / 100,
    // in integers: floor((100 * (request(1) + response(1)) - hundredUnits) / hundredUnits).
    const size_t scaled = 100 * (requestSmall + responseSmall);
    if (scaled < hundredUnits) {
        return 0;
    }
    return static_cast<uint32_t>((scaled - hundredUnits) / hundredUnits);
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
