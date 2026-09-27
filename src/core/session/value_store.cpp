// ValueStore (spec SPEC-core-session.md, value_store.h): per-device-type segments over
// contiguous value/state arrays, apply()/markFailed() (silent baselines, decision S4),
// markStale()/resetBaselines(), rebuild() (carry-over on a re-plan), bulk words()/bits().
//
// markStale()/resetBaselines() are O(P) here, not the O(1) the module spec's own sketch comment
// suggests ("epoch counter"): see their own doc comments in value_store.h for why an epoch-lazy
// design is incompatible with segment()'s raw-pointer SegmentView::states (a caller reading that
// pointer directly, bypassing state(), would see stale bytes an epoch scheme never resolves).
// Flagged in this task's Dev notes.
#include "mc/core/value_store.h"

#include <algorithm>
#include <utility>

namespace mc {
namespace {

Error notSubscribedError(const char* message) noexcept {
    Error e{};
    e.category = ErrorCategory::Config;
    e.code = ErrorCode::NotSubscribed;
    e.message = message;
    return e;
}

Error bufferTooSmallError(const char* message) noexcept {
    Error e{};
    e.category = ErrorCategory::Encode;
    e.code = ErrorCode::BufferTooSmall;
    e.message = message;
    return e;
}

constexpr size_t kNotFound = static_cast<size_t>(-1);

} // namespace

const ValueStore::Segment* ValueStore::findSegment(const std::vector<Segment>& segs,
                                                    uint32_t number) noexcept {
    size_t lo = 0;
    size_t hi = segs.size();
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (segs[mid].head.number <= number) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo == 0) {
        return nullptr;
    }
    const Segment& s = segs[lo - 1];
    if (number < s.head.number + s.count) {
        return &s;
    }
    return nullptr;
}

size_t ValueStore::advanceAndFind(const std::vector<Segment>& segs, size_t& segIdx,
                                  uint32_t number) noexcept {
    while (segIdx < segs.size() && number >= segs[segIdx].head.number + segs[segIdx].count) {
        ++segIdx;
    }
    if (segIdx >= segs.size() || number < segs[segIdx].head.number) {
        return kNotFound;
    }
    return segs[segIdx].offset + (number - segs[segIdx].head.number);
}

PointState ValueStore::state(Device d) const noexcept {
    const TypeStore& ts = m_types[static_cast<size_t>(d.type)];
    const Segment* seg = findSegment(ts.segments, d.number);
    if (seg == nullptr) {
        return PointState::NotSubscribed;
    }
    size_t idx = seg->offset + (d.number - seg->head.number);
    return static_cast<PointState>(ts.states[idx]);
}

uint16_t ValueStore::word(Device d) const noexcept {
    if (deviceInfo(d.type).kind != DeviceKind::Word) {
        return 0;
    }
    const TypeStore& ts = m_types[static_cast<size_t>(d.type)];
    const Segment* seg = findSegment(ts.segments, d.number);
    if (seg == nullptr) {
        return 0;
    }
    size_t idx = seg->offset + (d.number - seg->head.number);
    PointState st = static_cast<PointState>(ts.states[idx]);
    if (st == PointState::NotSubscribed || st == PointState::NoValue) {
        return 0;
    }
    return ts.words[idx];
}

bool ValueStore::bit(Device d) const noexcept {
    if (deviceInfo(d.type).kind != DeviceKind::Bit) {
        return false;
    }
    const TypeStore& ts = m_types[static_cast<size_t>(d.type)];
    const Segment* seg = findSegment(ts.segments, d.number);
    if (seg == nullptr) {
        return false;
    }
    size_t idx = seg->offset + (d.number - seg->head.number);
    PointState st = static_cast<PointState>(ts.states[idx]);
    if (st == PointState::NotSubscribed || st == PointState::NoValue) {
        return false;
    }
    return ts.bits[idx] != 0;
}

Expected<size_t> ValueStore::words(Device head, uint32_t count,
                                    MutableByteView out) const noexcept {
    if (count == 0) {
        return Expected<size_t>(size_t{0});
    }
    if (deviceInfo(head.type).kind != DeviceKind::Word) {
        return Expected<size_t>(notSubscribedError("head.type is not a word device"));
    }
    const TypeStore& ts = m_types[static_cast<size_t>(head.type)];
    const Segment* seg = findSegment(ts.segments, head.number);
    uint64_t endNumber = static_cast<uint64_t>(head.number) + count;
    if (seg == nullptr || endNumber > static_cast<uint64_t>(seg->head.number) + seg->count) {
        return Expected<size_t>(
            notSubscribedError("[head, head + count) is not entirely inside one segment"));
    }

    size_t needed = static_cast<size_t>(count) * 2;
    if (out.size < needed) {
        return Expected<size_t>(bufferTooSmallError("out is smaller than count * 2 bytes"));
    }

    size_t startIdx = seg->offset + (head.number - seg->head.number);
    for (uint32_t i = 0; i < count; ++i) {
        size_t idx = startIdx + i;
        PointState st = static_cast<PointState>(ts.states[idx]);
        uint16_t v = (st == PointState::NoValue) ? uint16_t{0} : ts.words[idx];
        out.data[2 * i] = static_cast<uint8_t>(v & 0xFFu);
        out.data[2 * i + 1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    }
    return Expected<size_t>(needed);
}

Expected<size_t> ValueStore::bits(Device head, uint32_t count, MutableByteView out,
                                  BitLayout layout) const noexcept {
    if (count == 0) {
        return Expected<size_t>(size_t{0});
    }
    if (deviceInfo(head.type).kind != DeviceKind::Bit) {
        return Expected<size_t>(notSubscribedError("head.type is not a bit device"));
    }
    const TypeStore& ts = m_types[static_cast<size_t>(head.type)];
    const Segment* seg = findSegment(ts.segments, head.number);
    uint64_t endNumber = static_cast<uint64_t>(head.number) + count;
    if (seg == nullptr || endNumber > static_cast<uint64_t>(seg->head.number) + seg->count) {
        return Expected<size_t>(
            notSubscribedError("[head, head + count) is not entirely inside one segment"));
    }

    size_t needed =
        (layout == BitLayout::PackedLsbFirst) ? (static_cast<size_t>(count) + 7) / 8 : count;
    if (out.size < needed) {
        return Expected<size_t>(bufferTooSmallError("out is smaller than the layout's own size"));
    }

    size_t startIdx = seg->offset + (head.number - seg->head.number);
    if (layout == BitLayout::PackedLsbFirst) {
        for (size_t i = 0; i < needed; ++i) {
            out.data[i] = 0;
        }
        for (uint32_t i = 0; i < count; ++i) {
            size_t idx = startIdx + i;
            PointState st = static_cast<PointState>(ts.states[idx]);
            bool v = (st != PointState::NoValue) && (ts.bits[idx] != 0);
            if (v) {
                out.data[i / 8] = static_cast<uint8_t>(out.data[i / 8] | (1u << (i % 8)));
            }
        }
    } else {
        for (uint32_t i = 0; i < count; ++i) {
            size_t idx = startIdx + i;
            PointState st = static_cast<PointState>(ts.states[idx]);
            bool v = (st != PointState::NoValue) && (ts.bits[idx] != 0);
            out.data[i] = v ? uint8_t{1} : uint8_t{0};
        }
    }
    return Expected<size_t>(needed);
}

size_t ValueStore::segmentCount(DeviceType t) const noexcept {
    return m_types[static_cast<size_t>(t)].segments.size();
}

SegmentView ValueStore::segment(DeviceType t, size_t i) const noexcept {
    const TypeStore& ts = m_types[static_cast<size_t>(t)];
    const Segment& s = ts.segments[i];
    SegmentView v{};
    v.head = s.head;
    v.count = s.count;
    v.words = (deviceInfo(t).kind == DeviceKind::Word) ? ts.words.data() + s.offset : nullptr;
    v.bits = (deviceInfo(t).kind == DeviceKind::Bit) ? ts.bits.data() + s.offset : nullptr;
    v.states = ts.states.data() + s.offset;
    return v;
}

void ValueStore::rebuild(const RangeSet& subs) {
    size_t entryIndex = 0;
    for (uint16_t t = 0; t < static_cast<uint16_t>(DeviceType::Count); ++t) {
        DeviceType type = static_cast<DeviceType>(t);
        TypeStore& ts = m_types[t];
        bool isWord = deviceInfo(type).kind == DeviceKind::Word;

        // This type's subscriptions, unioned (no alignment, no gap merge: a segment is the true
        // subscribed footprint, spec "Storage"), mirroring ReadPlan::build()'s own per-type walk
        // over RangeSet::m_entries (sorted by (type, head), so a contiguous run here is exactly
        // one type).
        std::vector<std::pair<uint32_t, uint32_t>> raw;
        while (entryIndex < subs.m_entries.size() && subs.m_entries[entryIndex].head.type == type) {
            const RangeSet::Entry& e = subs.m_entries[entryIndex];
            raw.emplace_back(e.head.number, e.head.number + e.count);
            ++entryIndex;
        }
        std::sort(raw.begin(), raw.end());
        std::vector<std::pair<uint32_t, uint32_t>> merged;
        merged.reserve(raw.size());
        for (const auto& iv : raw) {
            if (!merged.empty() && iv.first <= merged.back().second) {
                if (iv.second > merged.back().second) {
                    merged.back().second = iv.second;
                }
            } else {
                merged.push_back(iv);
            }
        }

        size_t totalP = 0;
        for (const auto& iv : merged) {
            totalP += iv.second - iv.first;
        }

        TypeStore newTs;
        newTs.segments.reserve(merged.size());
        if (isWord) {
            newTs.words.assign(totalP, uint16_t{0});
        } else {
            newTs.bits.assign(totalP, uint8_t{0});
        }
        newTs.states.assign(totalP, static_cast<uint8_t>(PointState::NoValue));

        // Two-pointer carry-over: `n` runs strictly increasing across every new segment in turn,
        // and oldSegIdx only ever advances, so the whole type costs O(P_old + P_new), not
        // O(P_new * G_old).
        size_t oldSegIdx = 0;
        size_t offset = 0;
        for (const auto& iv : merged) {
            Segment seg{Device{type, iv.first}, iv.second - iv.first, offset};
            for (uint32_t n = iv.first; n < iv.second; ++n) {
                size_t newIdx = offset + (n - iv.first);
                size_t oldIdx = advanceAndFind(ts.segments, oldSegIdx, n);
                if (oldIdx != kNotFound) {
                    newTs.states[newIdx] = ts.states[oldIdx];
                    if (isWord) {
                        newTs.words[newIdx] = ts.words[oldIdx];
                    } else {
                        newTs.bits[newIdx] = ts.bits[oldIdx];
                    }
                }
            }
            newTs.segments.push_back(seg);
            offset += seg.count;
        }

        ts = std::move(newTs);
    }
}

size_t ValueStore::apply(const ReadPlan& plan, size_t chunk, ByteView payload, Change* out,
                          size_t capacity) noexcept {
    const Request& r = plan.chunk(chunk).request;
    DeviceType type = r.head.type;
    TypeStore& ts = m_types[static_cast<size_t>(type)];
    bool isBitKind = deviceInfo(type).kind == DeviceKind::Bit;
    bool bitsAsWordsChunk = isBitKind && r.op == Op::ReadWords;
    uint32_t nativeCount =
        bitsAsWordsChunk ? static_cast<uint32_t>(r.count) * 16u : static_cast<uint32_t>(r.count);

    size_t written = 0;
    size_t segIdx = 0;
    for (uint32_t p = 0; p < nativeCount; ++p) {
        uint32_t deviceNumber = r.head.number + p;
        size_t idx = advanceAndFind(ts.segments, segIdx, deviceNumber);
        if (idx == kNotFound) {
            continue; // Gap point: decoded and discarded, never stored (spec "Storage").
        }

        uint16_t newValue;
        if (bitsAsWordsChunk) {
            // Bit i of word k is device head + 16k + i (spec section 2.4, convert::wordsToBits's
            // own bit order: LSB of the word is bit 0).
            size_t wordIdx = p / 16;
            uint8_t lo = payload.data[2 * wordIdx];
            uint8_t hi = payload.data[2 * wordIdx + 1];
            uint16_t word =
                static_cast<uint16_t>(lo) | static_cast<uint16_t>(static_cast<uint16_t>(hi) << 8);
            newValue = static_cast<uint16_t>((word >> (p % 16)) & 0x01u);
        } else if (isBitKind) {
            // ReadBits, BytePerPoint (the layout Session's own polling chunks use): one byte per
            // point, any nonzero byte is 1.
            newValue = payload.data[p] != 0 ? uint16_t{1} : uint16_t{0};
        } else {
            newValue = static_cast<uint16_t>(payload.data[2 * p]) |
                       static_cast<uint16_t>(static_cast<uint16_t>(payload.data[2 * p + 1]) << 8);
        }

        PointState oldState = static_cast<PointState>(ts.states[idx]);
        bool hadBaseline = (oldState == PointState::Valid || oldState == PointState::Failed);
        uint16_t oldValue = isBitKind ? ts.bits[idx] : ts.words[idx];
        if (hadBaseline && oldValue != newValue && written < capacity) {
            out[written++] = Change{Device{type, deviceNumber}, oldValue, newValue};
        }

        ts.states[idx] = static_cast<uint8_t>(PointState::Valid);
        if (isBitKind) {
            ts.bits[idx] = static_cast<uint8_t>(newValue);
        } else {
            ts.words[idx] = newValue;
        }
    }
    return written;
}

void ValueStore::markFailed(const ReadPlan& plan, size_t chunk) noexcept {
    const Request& r = plan.chunk(chunk).request;
    DeviceType type = r.head.type;
    TypeStore& ts = m_types[static_cast<size_t>(type)];
    bool isBitKind = deviceInfo(type).kind == DeviceKind::Bit;
    bool bitsAsWordsChunk = isBitKind && r.op == Op::ReadWords;
    uint32_t nativeCount =
        bitsAsWordsChunk ? static_cast<uint32_t>(r.count) * 16u : static_cast<uint32_t>(r.count);

    size_t segIdx = 0;
    for (uint32_t p = 0; p < nativeCount; ++p) {
        uint32_t deviceNumber = r.head.number + p;
        size_t idx = advanceAndFind(ts.segments, segIdx, deviceNumber);
        if (idx == kNotFound) {
            continue;
        }
        if (static_cast<PointState>(ts.states[idx]) != PointState::NoValue) {
            ts.states[idx] = static_cast<uint8_t>(PointState::Failed);
        }
    }
}

void ValueStore::markStale() noexcept {
    for (TypeStore& ts : m_types) {
        for (uint8_t& s : ts.states) {
            PointState ps = static_cast<PointState>(s);
            if (ps == PointState::Valid || ps == PointState::Failed) {
                s = static_cast<uint8_t>(PointState::Stale);
            }
        }
    }
}

void ValueStore::resetBaselines() noexcept {
    for (TypeStore& ts : m_types) {
        for (uint8_t& s : ts.states) {
            s = static_cast<uint8_t>(PointState::NoValue);
        }
    }
}

} // namespace mc
