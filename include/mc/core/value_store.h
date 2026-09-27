/**
 * @file value_store.h
 * @brief Values of subscribed points (spec `mc-protocol-frame-spec.md` §2.3, §2.4): `PointState`,
 * `Change`, `SegmentView`, `ValueStore`.
 */
#pragma once

#include "mc/core/device.h"
#include "mc/core/poll_plan.h"
#include "mc/core/request.h"
#include "mc/core/result.h"
#include "mc/core/types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mc {

/**
 * @enum PointState
 * @brief What a subscribed point's stored value currently means.
 */
enum class PointState : uint8_t {
    NotSubscribed, ///< Not covered by any subscription (includes gap points read only to merge
                   ///< two subscriptions into one chunk).
    NoValue,       ///< Subscribed, not read successfully since the last `linkUp` (or ever).
    Valid,         ///< Last read succeeded; the value is current.
    Failed,        ///< Last read failed; the value is the last known one (from this link
                   ///< session).
    Stale          ///< Link is down; the value is the last known one.
};

/**
 * @struct Change
 * @brief One changed point, as `ValueStore::apply()` reports it.
 *
 * Bit points carry 0/1 in `oldValue`/`newValue`.
 */
struct Change {
    Device device;      ///< The point that changed.
    uint16_t oldValue;  ///< Value before this response.
    uint16_t newValue;  ///< Value after this response.
};

/**
 * @struct SegmentView
 * @brief A maximal run of subscribed points of one device type (the union of subscriptions of
 * that type; gaps excluded).
 *
 * `words`/`bits` and `states` point into `ValueStore`'s own storage; valid until the next
 * `ValueStore::rebuild()`.
 *
 * @see ValueStore::segment
 */
struct SegmentView {
    Device head;            ///< First point of this segment.
    uint32_t count;         ///< Number of consecutive points, starting at `head`.
    const uint16_t* words;  ///< Word device: `count` values. Null for a bit device.
    const uint8_t* bits;    ///< Bit device: `count` values, 0/1. Null for a word device.
    const uint8_t* states;  ///< `count` `PointState` values (stored as `uint8_t`).
};

/**
 * @class ValueStore
 * @brief Last known value and state of every subscribed point, kept in per-device-type segments.
 *
 * Storage: per device type, a sorted vector of segments over one contiguous value array
 * (`uint16_t` per word point, `uint8_t` per bit point) and one contiguous `uint8_t` state array.
 * A gap point read only to merge two subscriptions into one chunk (`ReadPlan`) is decoded and
 * discarded: it is never stored, never reported, and `state()` reports it `NotSubscribed`.
 * Memory is O(P), P the number of subscribed points.
 *
 * @see PointState, Change, SegmentView, RangeSet, ReadPlan
 */
class ValueStore {
public:
    /**
     * @brief Current state of one point.
     * @param[in] d Point to look up.
     * @return `PointState::NotSubscribed` when `d` is not covered by any current subscription;
     * otherwise the point's own state.
     * @par Complexity
     * O(log G), G the segment count of `d.type`; no allocation.
     * @see word, bit
     */
    PointState state(Device d) const noexcept;

    /**
     * @brief Last known value of a word point.
     *
     * Callers that care whether the value is current, stale, or has never been read check
     * `state()` first; this always returns a number.
     *
     * @param[in] d Point to look up; `d.type` must be a word device (`DeviceKind::Word`).
     * @return The last known value, or 0 when `state(d)` is `NotSubscribed` or `NoValue`, or
     * `d.type` is not a word device.
     * @par Complexity
     * O(log G); no allocation.
     * @see state, bit
     */
    uint16_t word(Device d) const noexcept;

    /**
     * @brief Last known value of a bit point.
     * @param[in] d Point to look up; `d.type` must be a bit device (`DeviceKind::Bit`).
     * @return The last known value, or `false` when `state(d)` is `NotSubscribed` or `NoValue`,
     * or `d.type` is not a bit device.
     * @par Complexity
     * O(log G); no allocation.
     * @see state, word
     */
    bool bit(Device d) const noexcept;

    /**
     * @brief Copies `count` word points starting at `head`, in the normalized payload layout
     * (2 bytes per word, little-endian).
     *
     * `[head, head + count)` must fall entirely inside one segment; a range that reaches into a
     * gap or a neighbouring segment fails, even if every individual point in it happens to be
     * subscribed some other way. A `NoValue` point's own two bytes are 0 — check `state()` or
     * `segment()`'s own state array when that matters.
     *
     * @param[in] head First point to copy.
     * @param[in] count Number of consecutive points, starting at `head`.
     * @param[out] out Destination; must hold at least `count * 2` bytes.
     * @return Bytes written (`count * 2`).
     * @retval ErrorCode::NotSubscribed `head.type` is not a word device, or
     * `[head, head + count)` is not entirely inside one segment.
     * @retval ErrorCode::BufferTooSmall `out` is smaller than `count * 2` bytes.
     * @par Complexity
     * O(log G + count); no allocation.
     * @see bits, segment
     */
    Expected<size_t> words(Device head, uint32_t count, MutableByteView out) const noexcept;

    /**
     * @brief Copies `count` bit points starting at `head`, in `layout`.
     *
     * Same "entirely inside one segment" precondition as `words()`. A `NoValue` point's own bit
     * is 0.
     *
     * @param[in] head First point to copy.
     * @param[in] count Number of consecutive points, starting at `head`.
     * @param[out] out Destination; must hold at least `count` bytes (`BytePerPoint`) or
     * `(count + 7) / 8` bytes (`PackedLsbFirst`).
     * @param[in] layout Layout to write `out` in.
     * @return Bytes written.
     * @retval ErrorCode::NotSubscribed `head.type` is not a bit device, or
     * `[head, head + count)` is not entirely inside one segment.
     * @retval ErrorCode::BufferTooSmall `out` is smaller than the layout's own size.
     * @par Complexity
     * O(log G + count); no allocation.
     * @see words, segment
     */
    Expected<size_t> bits(Device head, uint32_t count, MutableByteView out,
                          BitLayout layout = BitLayout::BytePerPoint) const noexcept;

    /**
     * @brief Number of segments currently held for `t`.
     * @param[in] t Device type to look up.
     * @return The segment count.
     * @par Complexity
     * O(1); no allocation.
     * @see segment
     */
    size_t segmentCount(DeviceType t) const noexcept;

    /**
     * @brief Segment `i` of `t`.
     * @param[in] t Device type to look up.
     * @param[in] i Segment index; `i < segmentCount(t)`.
     * @return The segment.
     * @par Complexity
     * O(1); no allocation.
     * @see segmentCount
     */
    SegmentView segment(DeviceType t, size_t i) const noexcept;

    /**
     * @brief Re-shapes the store for a new `RangeSet` (a re-plan).
     *
     * Points covered before and after keep their value and state; new points start `NoValue`;
     * dropped points disappear (`state()` then reports them `NotSubscribed`).
     *
     * @param[in] subs Subscriptions to re-shape the store for.
     * @par Complexity
     * O(P_old + P_new); allocates.
     * @see state
     */
    void rebuild(const RangeSet& subs);

    /**
     * @brief Decodes one chunk's normalized payload into the store.
     *
     * Converts raw words to points for a `bitsAsWords` chunk (`convert::wordsToBits` semantics:
     * bit `i` of word `k` is device `chunk.request.head + 16k + i`). Writes one `Change` per
     * subscribed point whose state was `Valid` or `Failed` and whose value differs from `payload`
     * (a point without a baseline, i.e. `NoValue`, becomes `Valid` silently instead). A point of
     * `chunk`'s own range that is not covered by any current subscription (a gap point) is
     * decoded and discarded: never stored, never reported.
     *
     * @param[in] plan The plan `chunk` is an index into.
     * @param[in] chunk Index of the chunk this payload answers; `chunk < plan.size()`.
     * @param[in] payload Normalized payload, as `Parser::payload()` decoded it for
     * `plan.chunk(chunk).request`.
     * @param[out] out Destination array for the changes.
     * @param[in] capacity Number of `Change` slots available at `out`.
     * @return The number of changes written; never more than `capacity`, and never more than
     * `plan.maxChunkPoints()`.
     * @par Complexity
     * O(n), n the chunk's own point count; no allocation.
     * @see markFailed
     */
    size_t apply(const ReadPlan& plan, size_t chunk, ByteView payload, Change* out,
                 size_t capacity) noexcept;

    /**
     * @brief Marks the chunk's subscribed points `Failed` (a `NoValue` point stays `NoValue`: it
     * never had a value to keep).
     * @param[in] plan The plan `chunk` is an index into.
     * @param[in] chunk Index of the chunk that failed; `chunk < plan.size()`.
     * @par Complexity
     * O(n), n the chunk's own point count; no allocation.
     * @see apply
     */
    void markFailed(const ReadPlan& plan, size_t chunk) noexcept;

    /**
     * @brief Link down: every `Valid`/`Failed` point becomes `Stale` (the value is kept).
     *
     * @par Complexity
     * O(P), P the total subscribed point count; no allocation. The module spec's own sketch
     * proposes O(1) via an epoch counter; not implemented that way here because `segment()`
     * exposes each point's state through a raw pointer (`SegmentView::states`) that a caller may
     * read directly, without going through `state()` — an epoch-lazy state would have to be
     * resolved on read, which a raw pointer cannot do, so it would make `segment()`'s own states
     * eagerly wrong until the next call that happens to touch each point. Recorded as a deviation
     * (Dev notes, T-021) for the leader/owner: revisiting it would mean changing `segment()`'s
     * contract, which is outside this task.
     * @see resetBaselines
     */
    void markStale() noexcept;

    /**
     * @brief Clears every baseline: every point becomes `NoValue`. What makes round 1 silent
     * after a `linkUp`.
     * @par Complexity
     * O(P); no allocation. Same deviation from the spec's O(1) hint as `markStale()`, and for the
     * same reason.
     * @see markStale
     */
    void resetBaselines() noexcept;

private:
    /// One segment: a maximal run of subscribed points, over a slice of this type's own
    /// contiguous value/state arrays.
    struct Segment {
        Device head;    ///< First point of this segment.
        uint32_t count; ///< Number of consecutive points, starting at `head`.
        size_t offset;  ///< Index of `head` in this type's own value/state arrays.
    };

    /// One device type's own segments and contiguous storage.
    struct TypeStore {
        std::vector<Segment> segments; ///< Sorted by `head.number`; never overlapping/adjacent
                                        ///< (adjacent runs are always one segment).
        std::vector<uint16_t> words;   ///< Populated for a word device; empty for a bit device.
        std::vector<uint8_t> bits;     ///< Populated for a bit device; empty for a word device.
        std::vector<uint8_t> states;   ///< One `PointState` (as `uint8_t`) per point; same length
                                        ///< as `words` or `bits`, whichever is populated.
    };

    /// Binary search for the segment covering `number`, or nullptr when none does.
    static const Segment* findSegment(const std::vector<Segment>& segs, uint32_t number) noexcept;

    /// Advances `segIdx` forward past every segment ending at or before `number`, then returns
    /// the storage index of `number`, or `SIZE_MAX` when it falls in a gap. `segIdx` must only
    /// ever be called with a non-decreasing `number` between two resets to 0 (apply()/
    /// markFailed()'s own point-by-point walk over one chunk's range, in increasing order).
    static size_t advanceAndFind(const std::vector<Segment>& segs, size_t& segIdx,
                                 uint32_t number) noexcept;

    std::array<TypeStore, static_cast<size_t>(DeviceType::Count)> m_types{};
};

} // namespace mc
