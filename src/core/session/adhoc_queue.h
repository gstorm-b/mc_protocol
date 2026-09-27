// src/core/session/adhoc_queue.h -- Session's ad-hoc request queue and FIFO byte arena (spec
// SPEC-core-session.md, "Ad-hoc requests", "Exactly-once completion"). Private implementation
// detail: not part of mc_core's public surface; only session.cpp/session_rx.cpp include it
// (mirrors output_ring.h's own PIMPL split from session.h).
#pragma once

#include "mc/core/frame_config.h"
#include "mc/core/protocol.h"
#include "mc/core/request.h"
#include "mc/core/result.h"
#include "mc/core/session.h" // RequestId, TimeMs

#include <cstdint>
#include <optional>
#include <vector>

namespace mc::detail {

/// One queued/in-flight/awaiting-drain ad-hoc request. Chunked lazily -- only a handful of
/// counters are stored, no per-chunk array -- so `AdHocQueue::submit()` and every completion
/// step stay allocation-free no matter how many chunks a request eventually splits into (the
/// next chunk's own head/count is recomputed on demand from `pointsCompleted`, the same
/// head-offset/step-per-unit arithmetic `mc::chunk()` uses, spec section 8.5).
struct AdHocJob {
    RequestId id{0};
    Op op{Op::ReadWords};
    Device head{};
    uint16_t count{0}; ///< Total points across every chunk (`Request::count`'s own width).
    BitLayout bitLayout{BitLayout::BytePerPoint};
    bool isWrite{false};
    bool completed{false}; ///< Outcome known (success or failure); its `RequestDone` was pushed.

    /// Arena reservation, in absolute (ever-increasing) byte offsets -- never modular -- so a
    /// reservation that has to skip a too-small gap at the physical end of the buffer can fold
    /// that gap into *this* job's own `reserveLength` (see `AdHocQueue`'s class doc): the byte
    /// range physically used is always `[start % arena.size(), ...)` for exactly `length` bytes,
    /// never split across the wrap point.
    uint64_t reserveStart{0};  ///< Start of the whole reservation (may include a leading gap).
    uint64_t reserveLength{0}; ///< Its span, gap included.
    /// Start of the first pre-encoded chunk frame (== `reserveStart` plus any leading gap).
    uint64_t framesStart{0};
    uint64_t framesLength{0}; ///< Total bytes of every chunk's frame, back-to-back.
    /// Bytes of `[framesStart, framesStart + framesLength)` already sent -- i.e. the offset of
    /// the next not-yet-sent chunk's own frame.
    uint64_t framesSent{0};
    /// Start of the write-data copy / read-payload space (== `framesStart + framesLength`).
    uint64_t dataStart{0};
    /// Its size: the write's own `data.size`, or `payloadSize(original request)` for a read.
    uint64_t dataLength{0};

    uint32_t pointsCompleted{0};     ///< Points whose outcome (success or failure) is known.
    uint16_t inFlightChunkPoints{0}; ///< The in-flight chunk's own point count; 0 when none.
};

/// One job finishing (its last chunk done, or any chunk failed): what `Session` turns into one
/// `RequestDone` output.
struct AdHocCompletion {
    RequestId id{0};
    Error error{};
    ByteView payload{}; ///< Empty for a write, or on failure.
};

/**
 * @class AdHocQueue
 * @brief Ad-hoc request queue and byte arena (spec "Ad-hoc requests"): `capacity` jobs,
 * `arenaBytes` of pre-encoded frames and read/write payload storage, both sized once at
 * construction and never grown.
 *
 * A job's whole arena reservation (frames + data) is released only once its own `RequestDone`
 * has been popped through `Session::nextOutput()` (`confirmDrained()`), not the moment it
 * completes: `AdHocCompletion::payload` is a view into that reservation, and a later `submit()`
 * reusing the space before the caller drains it would be exactly the dangling-pointer hazard
 * T-024 fixed for `Snapshot`/`ValuesChanged` (see this module's own test coverage for it).
 */
class AdHocQueue {
public:
    /// @param[in] frame Frame every request is validated, chunked and encoded against; copied
    /// (small, trivially-copyable) and never changes for this queue's own lifetime -- one
    /// `AdHocQueue` per `Session`, one `Session` per `FrameConfig`.
    AdHocQueue(const FrameConfig& frame, uint16_t capacity, uint32_t arenaBytes);

    /// @return Whether some job has a chunk still waiting to be sent (not yet completed).
    /// @par Complexity
    /// O(1); no allocation.
    bool hasWork() const noexcept;

    /**
     * @brief Validates, chunks, and reserves arena space for `r`: encodes every chunk's frame
     * into it (a write's own data is copied in; a read's response payload space is left for
     * later chunks to fill in as they complete).
     * @param[in] r Request to submit.
     * @return The new request's id.
     * @retval ErrorCode::PointCount / ErrorCode::InvalidDevice `mc::chunkCount(r, frame)`'s own
     * errors (`frame`: the one this queue was constructed with), or `r` needs more bytes than
     * the whole arena.
     * @retval ErrorCode::QueueFull `capacity` jobs are already queued, or the arena has no room
     * for `r` right now (transient: freed as earlier jobs drain).
     * @par Complexity
     * O(n) validate + chunk, n the chunk count; no allocation.
     */
    Expected<RequestId> submit(const Request& r) noexcept;

    /// The oldest not-yet-completed job's next not-yet-sent chunk, as a `Request` recomputed
    /// from that job's own progress (never stored as a separate object) and its pre-encoded
    /// frame bytes.
    /// @pre `hasWork()`.
    /// @par Complexity
    /// O(1); no allocation.
    Request nextChunkRequest() const noexcept;

    /// @copydoc nextChunkRequest
    ByteView nextChunkFrame() const noexcept;

    /// Marks the chunk `nextChunkRequest()` last described as sent (in flight).
    /// @param[in] chunkPoints That chunk's own point count (`nextChunkRequest().count`).
    void markChunkSent(uint16_t chunkPoints) noexcept;

    /**
     * @brief The in-flight chunk's response is known.
     * @param[in] ok Whether it succeeded.
     * @param[in] err Set when `!ok`.
     * @return The job's own completion when this was its last chunk or it failed (the job is
     * then `completed`, awaiting `confirmDrained()`); `std::nullopt` when more chunks remain.
     * @par Complexity
     * O(1); no allocation.
     * @see chunkPayloadDest
     */
    std::optional<AdHocCompletion> completeInFlightChunk(bool ok, Error err) noexcept;

    /// Destination for `Session` to decode the in-flight chunk's own response payload into (a
    /// read only; an empty view for a write, which has no response data to decode).
    /// @pre A chunk is in flight (`markChunkSent()` called, not yet completed).
    MutableByteView chunkPayloadDest() const noexcept;

    /**
     * @brief Marks every not-yet-completed job (queued or in flight) completed with `err`, one
     * job per call, oldest first (spec `linkDown()`: exactly-once completion, submission order).
     * @param[in] err The error every such job completes with (`Transport`/`LinkDown`).
     * @param[out] out Set when this returns `true`.
     * @return `false` once nothing is left to complete.
     * @par Complexity
     * O(1) amortized per call; no allocation.
     */
    bool completeAllForLinkDown(Error err, AdHocCompletion& out) noexcept;

    /// The oldest job's own `RequestDone` has just been popped by `Session::nextOutput()`:
    /// releases its arena reservation and its job slot.
    /// @pre The oldest job is `completed`.
    /// @par Complexity
    /// O(1); no allocation.
    void confirmDrained() noexcept;

private:
    AdHocJob& frontJob() noexcept;
    const AdHocJob& frontJob() const noexcept;
    AdHocJob* frontUnfinished() noexcept;
    const AdHocJob* frontUnfinished() const noexcept;

    /// Fixed capacity; ring `[m_jobHead, m_jobHead + m_jobCount)`.
    std::vector<AdHocJob> m_jobs;
    size_t m_jobHead{0};
    size_t m_jobCount{0};

    std::vector<uint8_t> m_arena; ///< Fixed capacity.
    uint64_t m_arenaHead{0};      ///< Absolute (ever-increasing); see `AdHocJob`'s own doc for why.
    uint64_t m_arenaTail{0};

    FrameConfig m_frame;
    RequestId m_nextId{1};
};

} // namespace mc::detail
