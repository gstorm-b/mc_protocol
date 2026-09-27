/**
 * @file poll_plan.h
 * @brief Subscription bookkeeping and the read planner (spec `mc-protocol-frame-spec.md` §2.4,
 * §3.5, §8.5): `SubscriptionId`, `RangeSet`, `PlanOptions`, `kAutoGap`, `autoGap()`,
 * `ChunkState`, `ChunkInfo`, `ReadPlan`.
 */
#pragma once

#include "mc/core/device.h"
#include "mc/core/frame_config.h"
#include "mc/core/request.h"
#include "mc/core/result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace mc {

/// Identifies one subscription inside a `RangeSet`. Never 0, never reused within one `RangeSet`.
using SubscriptionId = uint32_t;

/**
 * @class RangeSet
 * @brief The set of subscribed device ranges a `ReadPlan` is built from.
 *
 * Order-independent: overlapping and adjacent subscriptions are allowed and are unioned when a
 * plan is built (`ReadPlan::build`). Not thread-safe; the engine (`core-session`) is the only
 * intended caller of `add()`/`remove()`, serialized the same way every other `Session` input is.
 *
 * @see ReadPlan
 */
class RangeSet {
public:
    /**
     * @brief Adds `count` consecutive points starting at `head` (bits for bit devices, words for
     * word devices) as a new subscription.
     *
     * Only checks that can be decided without a `FrameConfig`: `count >= 1` and that
     * `head.number + count - 1` fits a 32-bit device number. Frame-specific checks (device
     * supported by the family, field width, alignment) happen later, in `ReadPlan::build()`.
     *
     * @param[in] head First point of the new subscription.
     * @param[in] count Number of consecutive points, starting at `head`.
     * @return The new subscription's id.
     * @retval ErrorCode::PointCount `count == 0`.
     * @retval ErrorCode::InvalidDevice `head.number + count - 1` does not fit a 32-bit device
     * number.
     * @par Complexity
     * O(log S) search + O(S) insert, S the current subscription count; allocates when the
     * underlying storage grows.
     * @see remove
     */
    Expected<SubscriptionId> add(Device head, uint32_t count);

    /**
     * @brief Removes a subscription previously returned by `add()`.
     * @param[in] id Subscription id to remove.
     * @retval ErrorCode::NotSubscribed `id` names no current subscription
     * (`ErrorCategory::Config`).
     * @par Complexity
     * O(S); no allocation.
     * @see add
     */
    Expected<void> remove(SubscriptionId id);

    /**
     * @brief Number of subscriptions currently held.
     * @return The subscription count.
     * @par Complexity
     * O(1); no allocation.
     */
    size_t size() const noexcept;

    /**
     * @brief Whether no subscription is currently held.
     * @return `size() == 0`.
     * @par Complexity
     * O(1); no allocation.
     */
    bool empty() const noexcept;

private:
    friend class ReadPlan;
    friend class ValueStore; // ValueStore::rebuild() (value_store.h, T-021) needs the same
                              // per-type subscription list ReadPlan::build() walks.

    /// One subscribed range, as `add()` recorded it (pre-alignment, pre-union).
    struct Entry {
        SubscriptionId id;  ///< This entry's own subscription id.
        Device head;        ///< First point of the subscription.
        uint32_t count;     ///< Number of consecutive points starting at `head`.
    };

    /// Sorted by `(head.type, head.number)`: groups subscriptions by device type in `DeviceType`'s
    /// own declaration order, which is exactly the order `ReadPlan::build()` needs to walk them in.
    std::vector<Entry> m_entries;
    SubscriptionId m_nextId{1}; ///< Never 0, never reused: only ever incremented.
};

/// Sentinel for `PlanOptions::maxGap`: resolve the merge gap from `autoGap()` instead of a fixed
/// value.
inline constexpr uint32_t kAutoGap = UINT32_MAX;

/**
 * @struct PlanOptions
 * @brief Tuning knobs for `ReadPlan::build()`.
 */
struct PlanOptions {
    /// Poll bit subscriptions with word reads (16 points per word), head aligned down and end
    /// aligned up to a multiple of 16 (M9000-M9255 on 1E/1C, or any frame with
    /// `FrameConfig::aSeriesTarget` set: 9000 + 16k, spec §3.5/§10 Q8). Default on: 4x fewer wire
    /// bytes than bit reads and a larger point limit per request.
    /// Risk: if a PLC's configured range of a bit device does not end on a multiple of 16, a
    /// subscription in that last group reads past the end and its chunk fails every round. Turn
    /// this off for such a PLC.
    bool bitsAsWords{true};

    /// Merge two read intervals of the same device type when the unread gap between them is at
    /// most this many read units (words for a word read, points for a bit read).
    /// `kAutoGap` resolves to `autoGap(cfg, readOp)` for the frame/operation being planned.
    uint32_t maxGap{kAutoGap};
};

/**
 * @brief The gap that costs no more wire bytes than a second request would.
 *
 * `floor((requestSize(1 unit) + responseSize(1 unit) - unitWireSize) / unitWireSize)`, computed
 * from `McProtocol`'s own sizing functions for `cfg`/`readOp` (`requestSize`/`responseSize` are
 * whole-frame sizes for a single-unit request/successful response; `unitWireSize` is the marginal
 * response bytes one more read unit costs). Merging across a gap this wide or narrower reads no
 * more wire bytes, in the worst case, than a second, separate request for the same continuation
 * would.
 *
 * @param[in] cfg Frame this gap is computed for.
 * @param[in] readOp `Op::ReadBits` or `Op::ReadWords`; any other value is treated as `ReadWords`.
 * @return The gap, in the same read units `readOp` reads (words or points); 0 when `cfg`'s frame
 * is not implemented yet (`McProtocol` reports `ErrorCode::UnsupportedCommand`).
 * @par Complexity
 * O(1); no allocation.
 * @see PlanOptions::maxGap
 */
uint32_t autoGap(const FrameConfig& cfg, Op readOp) noexcept;

/// Outcome of the last attempt to read one chunk.
enum class ChunkState : uint8_t {
    NotRead, ///< Never attempted since this plan was built (or since the last `linkUp`).
    Ok,      ///< The last attempt succeeded.
    Failed   ///< The last attempt failed; `ChunkInfo::lastError` holds why.
};

/**
 * @struct ChunkInfo
 * @brief One request-sized slice of a `ReadPlan`: its wire request, and the outcome of the last
 * attempt to read it.
 *
 * @see ReadPlan
 */
struct ChunkInfo {
    Request request;                     ///< `ReadWords` or `ReadBits`; head/count already
                                          ///< aligned and chunked to fit one command.
    ChunkState state{ChunkState::NotRead}; ///< Outcome of the last attempt.
    Error lastError{};                   ///< Set when `state == ChunkState::Failed`.
    uint32_t lastOkRound{0};             ///< Round this chunk last completed in; 0 = never, since
                                          ///< the last `linkUp`.
};

/**
 * @class ReadPlan
 * @brief Coalesced, gap-merged, aligned and chunked read requests for a `RangeSet`, on one
 * `FrameConfig`.
 *
 * Built once by `build()`; every accessor after that is a plain, allocation-free read of the
 * result. Chunks are ordered by `(DeviceType, head number)`, `DeviceType` in its own declaration
 * order; the chunks of one device type are contiguous (`chunksOf()`).
 *
 * @see RangeSet, PlanOptions
 */
class ReadPlan {
public:
    /**
     * @brief Builds a new plan for `subs` on `cfg`.
     *
     * Per device type, in `DeviceType` declaration order:
     * -# each subscription becomes an interval in read units (`opt.bitsAsWords`: a bit device's
     *    interval is aligned to a 16-point/word boundary first, spec §3.5);
     * -# the type's intervals are sorted, overlapping/adjacent ones are unioned, then neighbours
     *    whose gap is at most `opt.maxGap` (`kAutoGap` resolves via `autoGap()`) are merged;
     * -# each merged interval is split into chunks with `mc::chunk()` (spec §8.5), which validates
     *    every chunk it produces (device supported by the family, field width, alignment).
     *
     * A subscription that fails validation makes this call fail with that subscription's own
     * error and nothing else; the caller's previous `ReadPlan` (if any) is a separate object and
     * is therefore untouched by a failed `build()`.
     *
     * @param[in] subs Subscriptions to plan for.
     * @param[in] cfg Frame every chunk is validated and sized against.
     * @param[in] opt Planning options.
     * @return The new plan.
     * @retval ErrorCode::InvalidDevice A subscription's device is unsupported by `cfg`'s frame
     * family, exceeds its field width, or (word access to a bit device) is not aligned as `cfg`
     * requires.
     * @retval ErrorCode::PointCount No spec §4.4 limit applies to a subscription's device
     * kind/operation, or a chunk would need more points than a `Request::count` can hold and
     * `mc::chunk()` itself cannot split it further.
     * @par Complexity
     * O(S log S + C), S the subscription count, C the chunk count; allocates.
     * @see RangeSet, PlanOptions
     */
    static Expected<ReadPlan> build(const RangeSet& subs, const FrameConfig& cfg,
                                     const PlanOptions& opt);

    /**
     * @brief Number of chunks in this plan.
     * @return The chunk count.
     * @par Complexity
     * O(1); no allocation.
     */
    size_t size() const noexcept;

    /**
     * @brief Chunk at index `i`.
     * @param[in] i Chunk index; `i < size()`.
     * @return The chunk.
     * @par Complexity
     * O(1); no allocation.
     */
    const ChunkInfo& chunk(size_t i) const noexcept;

    /**
     * @brief Index range of `t`'s own chunks.
     * @param[in] t Device type to look up.
     * @return `[first, last)` chunk indices of `t`; an empty range (`first == last`) when `t` is
     * not subscribed.
     * @par Complexity
     * O(1); no allocation.
     */
    std::pair<size_t, size_t> chunksOf(DeviceType t) const noexcept;

    /**
     * @brief Largest normalized payload of any chunk in this plan.
     * @return The size, in bytes; 0 when `size() == 0`. A caller sizes its decode buffer once from
     * this value.
     * @par Complexity
     * O(1); no allocation.
     */
    size_t maxPayloadSize() const noexcept;

    /**
     * @brief Largest number of subscribed points read by any one chunk in this plan (never counts
     * a gap point read only to merge two subscriptions, nor `bitsAsWords` alignment padding).
     * @return The count; 0 when `size() == 0`. A caller sizes its change buffer once from this
     * value.
     * @par Complexity
     * O(1); no allocation.
     */
    size_t maxChunkPoints() const noexcept;

private:
    friend class Session; // Session (session.h, T-022) is the only writer of a chunk's own
                           // ChunkState/lastError/lastOkRound as rounds run; every other caller
                           // sees them through the const chunk() accessor above only.

    /// Mutable access to chunk `i`, for `Session` to record the outcome of reading it. Not
    /// exposed publicly: `chunk()` (above) is the read-only accessor every other caller uses.
    ChunkInfo& chunkForUpdate(size_t i) noexcept { return m_chunks[i]; }

    std::vector<ChunkInfo> m_chunks;
    std::array<std::pair<size_t, size_t>, static_cast<size_t>(DeviceType::Count)> m_typeRanges{};
    size_t m_maxPayloadSize{0};
    size_t m_maxChunkPoints{0};
};

} // namespace mc
