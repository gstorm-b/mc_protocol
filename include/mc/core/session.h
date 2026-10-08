/**
 * @file session.h
 * @brief The sans-I/O polling engine (spec `mc-protocol-frame-spec.md` §4.4, §6.1, §6.3, §7.3,
 * §8.5, §8.6, §9.9, §9.10): `TimeMs`, `RequestId`, `CycleMode`, `HeartbeatConfig`,
 * `SessionConfig`, `OutputKind`, `LinkFaultKind`, `CycleInfo`, `Output`, `SessionStats`,
 * `Session`.
 */
#pragma once

#include "mc/core/device.h"
#include "mc/core/frame_config.h"
#include "mc/core/log.h"
#include "mc/core/poll_plan.h"
#include "mc/core/protocol.h"
#include "mc/core/request.h"
#include "mc/core/result.h"
#include "mc/core/types.h"
#include "mc/core/value_store.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace mc {

namespace detail {
class OutputRing; // src/core/session/output_ring.h; Session's own private implementation
                   // detail, never part of mc_core's public surface (kept out of this header so
                   // a consumer never needs a src/-only path).
class AdHocQueue;  // src/core/session/adhoc_queue.h; same reasoning (T-025).
} // namespace detail

/// Monotonic milliseconds supplied by the caller. The engine never reads a clock itself; every
/// input that can start a timer takes `now`, which must not decrease between calls (a decrease
/// is clamped to the last value and logged at `Warn`).
using TimeMs = uint64_t;

/// `nextDeadline()` value meaning "nothing is pending".
inline constexpr TimeMs kNoDeadline = UINT64_MAX;

/// Identifies one ad-hoc request submitted with `Session::submit()`. Never 0, never reused
/// within one `Session`.
using RequestId = uint64_t;

/// Round scheduling policy.
enum class CycleMode : uint8_t {
    FixedRate, ///< Round k+1 starts at `start(k) + interval`, or immediately if round k ran
               ///< longer. No catch-up bursts.
    FixedDelay ///< Round k+1 starts at `end(k) + interval`.
};

/**
 * @struct HeartbeatConfig
 * @brief Optional heartbeat write, off by default (decision 6, `SPEC-core-session.md`).
 */
struct HeartbeatConfig {
    bool enabled{false};                    ///< Off by default.
    Device device{DeviceType::M, 2000};     ///< A bit device supported by the frame.
};

/**
 * @struct SessionConfig
 * @brief Tuning knobs for `Session::create()`.
 */
struct SessionConfig {
    uint32_t cycleIntervalMs{100};        ///< 0 = rounds back-to-back.
    CycleMode cycleMode{CycleMode::FixedRate}; ///< Round scheduling policy.
    PlanOptions plan{};                    ///< Forwarded to every `ReadPlan::build()` re-plan.
    uint16_t adHocCapacity{64};            ///< Queued ad-hoc requests; `submit()` beyond it fails
                                            ///< with `QueueFull`.
    uint32_t adHocArenaBytes{64 * 1024};   ///< FIFO ring holding every queued request's frames
                                            ///< and read payload; allocated once in `create()`.
    uint8_t maxAdHocBurst{4};              ///< During a round, at most this many ad-hoc frames
                                            ///< between two polling chunks; 0 = strict priority.
    uint8_t maxConsecutiveLinkErrors{3};   ///< Serial only: timeouts/protocol errors in a row
                                            ///< before `LinkFault`; at least 1 (0 is rejected by
                                            ///< `validate()` on every frame).
    uint16_t serialInterCharMs{100};       ///< Serial only: once a response's first byte has
                                            ///< arrived, the deadline is last byte + this.
    uint16_t serialFlushMs{50};            ///< Serial only: after EOT, discard bytes until the
                                            ///< line has been silent this long.
    uint32_t firstResponseTimeoutMs{0}; ///< Response deadline of the first frame sent after each
                                        ///< `linkUp()`: 0 = `FrameConfig::effectiveTimeoutMs()`.
                                        ///< Never a resend. Serial: bounds the time to the first
                                        ///< byte only. Nonzero and below `effectiveTimeoutMs()`
                                        ///< is rejected by `validate()`.
    HeartbeatConfig heartbeat{};        ///< Optional heartbeat write.
    LogSink* log{nullptr};                  ///< Not owned; null = `NullLogSink`. Category
                                             ///< `"mc.session"`.

    /**
     * @brief Checks this configuration for values that are wrong before `Session::create()`
     * builds anything.
     * @param[in] frame Frame this session would run on; `heartbeat.device` is checked against it
     * when `heartbeat.enabled`.
     * @return Success when every check passes.
     * @retval ErrorCode::InvalidConfig `maxConsecutiveLinkErrors` is 0, `firstResponseTimeoutMs`
     * is nonzero and below `frame.effectiveTimeoutMs()`, or `heartbeat.enabled` and
     * `heartbeat.device` is not a bit device supported by `frame`.
     * @par Complexity
     * O(1); no allocation.
     */
    Expected<void> validate(const FrameConfig& frame) const noexcept;
};

/// Which fields of an `Output` are meaningful; see `Output`'s own member docs.
enum class OutputKind : uint8_t {
    Send,          ///< `bytes`: write them to the transport, in order.
    ValuesChanged, ///< `deviceType`, `round`, `changes[0..changeCount)`: one response's changes
                   ///< (round >= 2 only).
    Snapshot,      ///< `deviceType`, `round`, `chunks[0..chunkCount)`: read
                   ///< `values().segment(deviceType, ...)`.
    CycleDone,     ///< `cycle`: one round finished.
    RequestDone,   ///< `requestId`, `error`, `payload`: exactly one per accepted `submit()`.
    LinkFault      ///< `fault`, `error`, `reopenTransport`: the engine has stopped sending.
};

/// Why a `LinkFault` was reported.
enum class LinkFaultKind : uint8_t {
    Timeout,       ///< No response within the frame's effective timeout.
    ProtocolError  ///< The byte stream could not be trusted as a valid frame.
};

/**
 * @struct CycleInfo
 * @brief Summary of one finished polling round; carried by `Output::cycle`.
 */
struct CycleInfo {
    uint32_t round;        ///< Round number (1 = the first round after `linkUp`).
    TimeMs startedAt;       ///< `now` when this round started.
    uint32_t durationMs;    ///< Wall time (in caller `now` units) this round took.
    uint16_t requests;      ///< Frames sent in the round, retries and ad-hoc included.
    uint16_t failedChunks;  ///< Number of this round's chunks that ended `Failed`.
    bool heartbeatOk;       ///< `true` when disabled.
};

/**
 * @struct Output
 * @brief One event popped by `Session::nextOutput()`. Only the fields `kind` documents are
 * meaningful.
 *
 * Every view (`bytes`, `changes`, `chunks`, `payload`) stays valid until the next call to any
 * `Session` input method.
 *
 * @see OutputKind, Session::nextOutput
 */
struct Output {
    OutputKind kind{OutputKind::Send}; ///< Which fields below are meaningful.
    ByteView bytes{};                  ///< `Send`: bytes to write to the transport.
    DeviceType deviceType{DeviceType::D}; ///< `ValuesChanged`/`Snapshot`: which device type.
    uint32_t round{0};                 ///< `ValuesChanged`/`Snapshot`: which round.
    const Change* changes{nullptr};    ///< `ValuesChanged`: the changed points.
    size_t changeCount{0};             ///< `ValuesChanged`: number of `changes`.
    const ChunkInfo* chunks{nullptr};  ///< `Snapshot`: this type's own chunk range of the plan.
    size_t chunkCount{0};              ///< `Snapshot`: number of `chunks`.
    CycleInfo cycle{};                 ///< `CycleDone`: the round that just finished.
    RequestId requestId{0};            ///< `RequestDone`: which `submit()` call this answers.
    Error error{};                     ///< `RequestDone`/`LinkFault`: the outcome.
    ByteView payload{};                ///< `RequestDone`: the normalized read payload (empty for
                                        ///< a write, or on failure).
    LinkFaultKind fault{LinkFaultKind::Timeout}; ///< `LinkFault`: which kind.
    bool reopenTransport{false};       ///< `LinkFault`: whether the transport itself is
                                        ///< considered unusable (Ethernet: always `true`;
                                        ///< serial: always `false`).
};

/**
 * @struct SessionStats
 * @brief Running counters since `create()`.
 */
struct SessionStats {
    uint64_t rounds{0};         ///< Completed rounds (`CycleDone` count).
    uint64_t framesSent{0};     ///< Frames written to `Send` outputs.
    uint64_t bytesSent{0};      ///< Bytes written to `Send` outputs.
    uint64_t bytesReceived{0};  ///< Bytes passed to `bytesIn()`.
    uint64_t timeouts{0};       ///< Response deadlines reached.
    uint64_t protocolErrors{0}; ///< `Parser` failures with a `Protocol` error.
    uint64_t plcErrors{0};      ///< `Parser` failures with a `Plc` error.
    uint64_t retries{0};        ///< Serial reads resent after a timeout/error.
    uint64_t eotsSent{0};       ///< Serial `EOT`s sent after an error.
};

/**
 * @class Session
 * @brief Sans-I/O polling engine: the application feeds it received bytes, the current time,
 * subscriptions and ad-hoc requests; it hands back bytes to send and events to publish.
 *
 * Never opens a socket, never sleeps, never reads a clock, never starts a thread, never throws.
 * Movable, not copyable.
 *
 * @see SessionConfig, Output, RangeSet, ReadPlan, ValueStore
 */
class Session {
public:
    /**
     * @brief Validates both configs (`FrameConfig::validate()`, `SessionConfig::validate()`) and
     * sizes every buffer the steady state needs.
     * @param[in] frame Frame this session runs on.
     * @param[in] cfg Session tuning knobs.
     * @return The created session.
     * @retval ErrorCode::InvalidConfig Either `validate()` failed.
     * @par Complexity
     * O(1) plus buffer sizing; allocates.
     * @see SessionConfig
     */
    static Expected<Session> create(const FrameConfig& frame, const SessionConfig& cfg = {});

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) noexcept;
    Session& operator=(Session&&) noexcept;
    ~Session();

    /**
     * @brief Adds a subscription. Takes effect at the next round boundary (decision S6).
     * @param[in] head First point to subscribe.
     * @param[in] count Number of consecutive points, starting at `head`.
     * @return The new subscription's id.
     * @retval ErrorCode::PointCount `RangeSet::add()`'s own `count == 0` check, or no spec §4.4
     * limit applies to this device kind/operation.
     * @retval ErrorCode::InvalidDevice `RangeSet::add()`'s own overflow check, or `head`/`count`
     * fails an immediate check against this session's frame (device unsupported, field width).
     * @post Reflected in `plan()`/`values()` from the start of the next round onward.
     * @par Complexity
     * As `RangeSet::add()`, plus one `chunkCount()` check; re-plan itself deferred to the round
     * boundary. Allocates when the underlying subscription storage grows.
     * @see unsubscribe
     */
    Expected<SubscriptionId> subscribe(Device head, uint32_t count);

    /**
     * @brief Removes a subscription. Takes effect at the next round boundary (decision S6).
     * @param[in] id Subscription id to remove.
     * @retval ErrorCode::NotSubscribed `id` names no current subscription.
     * @post Reflected in `plan()`/`values()` from the start of the next round onward.
     * @par Complexity
     * As `RangeSet::remove()`; re-plan itself deferred to the round boundary.
     * @see subscribe
     */
    Expected<void> unsubscribe(SubscriptionId id);

    /**
     * @brief The transport is open. Clears every baseline (`ValueStore::resetBaselines`) and
     * starts round 1 at `now` (the first `Send` is produced by this call). The first frame sent
     * after this call (heartbeat, polling chunk or ad-hoc request) waits
     * `SessionConfig::firstResponseTimeoutMs` for its response when that is nonzero; it is never
     * resent for it.
     * @param[in] now Current time.
     * @pre `nextOutput()` has drained every output of the previous input call.
     * @post At least one `Send` (or, for an empty plan, a `CycleDone`) is available from
     * `nextOutput()`.
     * @par Complexity
     * O(1) plus one dispatch step; no allocation unless a subscription made before this call is
     * still pending re-plan, in which case as `ReadPlan::build()`/`ValueStore::rebuild()`.
     * @see linkDown
     */
    void linkUp(TimeMs now) noexcept;

    /**
     * @brief The transport is closed or lost. Completes every in-flight and queued ad-hoc
     * request with `Error{Transport, LinkDown}`, in submission order, marks every value `Stale`,
     * stops all timers, and clears `Faulted` (spec "States": `Faulted` leaves only on
     * `linkDown()`). The next `linkUp()` starts round numbering over at 1 (spec SES-16: "round 1
     * restarts").
     * @param[in] now Current time.
     * @pre `nextOutput()` has drained every output of the previous input call.
     * @par Complexity
     * O(Q + P), Q the queued ad-hoc request count, P the total subscribed point count
     * (`ValueStore::markStale()`); no allocation.
     * @see linkUp
     */
    void linkDown(TimeMs now) noexcept;

    /**
     * @brief Queues an ad-hoc request (typically a write).
     *
     * Chunked with `mc::chunk()`: reads always split; writes only with `FrameConfig::splitWrites`
     * (else `PointCount`, nothing queued). Every chunk's frame and the read payload space are
     * placed in the ad-hoc arena (`r.data` is not kept).
     *
     * @param[in] r Request to submit.
     * @param[in] now Current time.
     * @return The request's id.
     * @retval ErrorCode::PointCount / ErrorCode::InvalidDevice `mc::chunkCount(r, frame)`'s own
     * errors (also when the request alone needs more than `adHocArenaBytes`).
     * @retval ErrorCode::QueueFull `adHocCapacity` reached, or the arena has no room for `r`
     * right now (transient: retry once earlier requests complete and are drained).
     * @retval ErrorCode::LinkDown The link is not up.
     * @post On success, exactly one `RequestDone` with this id follows, ever.
     * @par Complexity
     * O(n) validate + chunk; no allocation (ad-hoc arena).
     */
    Expected<RequestId> submit(const Request& r, TimeMs now);

    /**
     * @brief Bytes received from the transport, in order, any fragmentation.
     *
     * Ethernet faults (spec "Faults, retries, EOT"): a parser failure whose `Error::category` is
     * `Protocol` (not `Plc`), bytes that would overflow the receive buffer, or bytes that arrive
     * while `Idle` (nothing was sent) all report `LinkFault{ProtocolError, reopenTransport=true}`
     * via `faultLink()` and stop the engine (state `Faulted`) instead of completing anything
     * normally. A `Plc`-category failure (the PLC answered with a non-zero end code) is not a
     * link fault: the in-flight item simply fails, same as always.
     *
     * Serial (spec "Faults, retries, EOT", serial column): once a response's first byte has
     * arrived its deadline becomes `SessionConfig::serialInterCharMs` after the latest byte. A
     * `Protocol`-category parse failure or a receive-buffer overflow is a link error, handled like
     * a timeout (`tick()`); bytes while `Idle` are discarded and logged at `Warn`; bytes while
     * `Flushing` are discarded and restart the flush's silence window.
     *
     * @param[in] bytes Bytes received since the last call.
     * @param[in] now Current time.
     * @pre `nextOutput()` has drained every output of the previous input call.
     * @par Complexity
     * O(k) parse, k = `bytes.size`; + O(n) when a response completes; no allocation.
     */
    void bytesIn(ByteView bytes, TimeMs now) noexcept;

    /**
     * @brief Time passed. Call at or after `nextDeadline()`; extra calls are harmless.
     *
     * Ethernet timeout (spec "Faults, retries, EOT"): once `now >= ` the response deadline while
     * `Waiting`, reports `LinkFault{Timeout, reopenTransport=true}` via `faultLink()` instead of
     * continuing to wait.
     *
     * Serial: a response deadline is a link error. `EOT` is sent (when
     * `FrameConfig::sendEotOnError`) and the session stays `Flushing` until the line has been
     * silent for `SessionConfig::serialFlushMs` (capped at `FrameConfig::effectiveTimeoutMs()`,
     * which counts as one more link error); then a read is resent up to
     * `FrameConfig::readRetries` times and a write fails with `Timeout`, never resent.
     * `SessionConfig::maxConsecutiveLinkErrors` link errors in a row report
     * `LinkFault{..., reopenTransport=false}`.
     *
     * @note Call this whenever `nextDeadline()` passes, even while bytes keep arriving: a driver
     * loop that calls `tick()` only when no data came never ends a serial flush on a line that
     * keeps chattering (the flush cap is evaluated here).
     *
     * @param[in] now Current time.
     * @pre `nextOutput()` has drained every output of the previous input call.
     * @par Complexity
     * O(1) amortized; no allocation unless a pending re-plan applies (as `linkUp()`).
     */
    void tick(TimeMs now) noexcept;

    /**
     * @brief Pops the next output.
     * @param[out] out Set when this returns `true`.
     * @return `false` when none is left. The caller MUST drain until `false` after every input
     * call and before the next one.
     * @par Complexity
     * O(1); no allocation.
     */
    bool nextOutput(Output& out) noexcept;

    /**
     * @brief Earliest time the engine needs `tick()`.
     * @return The response deadline while `Waiting`; the end of the flush (silence window or cap,
     * whichever is earlier) while `Flushing`; the next round start while `Idle`; `kNoDeadline`
     * when down or faulted.
     * @par Complexity
     * O(1); no allocation.
     */
    TimeMs nextDeadline() const noexcept;

    /// @return The values of every currently subscribed point.
    const ValueStore& values() const noexcept;

    /// @return The plan of the current round.
    const ReadPlan& plan() const noexcept;

    /// @return Running counters since `create()`.
    const SessionStats& stats() const noexcept;

    /// @return `true` when the link is up (not `Down`, not `Faulted`).
    bool isLinkUp() const noexcept;

    /// @return `true` when a `LinkFault` has been reported and not yet cleared by `linkDown()`.
    bool isFaulted() const noexcept;

private:
    /// The state machine's own states (spec "States"): a `switch` over this drives every input
    /// method. `Flushing` is serial only: an `EOT` was sent after a link error and received bytes
    /// are discarded until the line is silent.
    enum class State : uint8_t { Down, Idle, Waiting, Flushing, Faulted };

    Session(const FrameConfig& frame, const SessionConfig& cfg, ReadPlan plan);

    TimeMs clampNow(TimeMs now) noexcept;
    LogSink& log() const noexcept;

    /// Entered whenever the state machine reaches `Idle`. Step 1 (spec "Dispatch"): if the
    /// ad-hoc queue has work and either no round is in progress or fewer than
    /// `maxAdHocBurst` ad-hoc frames have been sent since the last polling chunk, sends the
    /// ad-hoc queue's head (T-025). Else, if a round is in progress, sends its next chunk when
    /// one remains or ends it (a heartbeat-only round can reach here with none left, T-026: see
    /// `sendHeartbeat()`). Else starts a new round when due -- the heartbeat write, if enabled,
    /// unconditionally goes out first (spec "Dispatch" step 3: "put the heartbeat write in
    /// front, then go to 1"; its own completion re-enters `dispatch()`, which is that "go to 1")
    /// -- or does nothing (stays `Idle` until `nextDeadline()`). Never recurses — a round with no
    /// chunks and no heartbeat emits its own `CycleDone` and returns; the next round (even at
    /// `cycleIntervalMs == 0`) starts on the next input call, keeping every call's own output
    /// count bounded (see Dev notes, T-022). When a re-plan is pending, starting a round is
    /// additionally deferred while `m_pendingPlanRefs > 0` (T-024: a `Snapshot`/`ValuesChanged`
    /// queued earlier in this same call still points into the plan/buffers a re-plan would
    /// replace; the caller draining it first, on this same or the very next call, is what
    /// actually applies the re-plan).
    void dispatch(TimeMs now) noexcept;

    /// Applies a pending re-plan (if any), then starts a new round's bookkeeping. The only
    /// allocation point after `create()` when a re-plan is actually pending.
    void startRound(TimeMs now);

    /// Sends `plan().chunk(index).request`'s pre-encoded frame and enters `Waiting` for it.
    void sendChunk(size_t index, TimeMs now) noexcept;

    /// Response deadline for a frame being sent now (not a resend): `now +
    /// firstResponseTimeoutMs` for the first send after `linkUp()` when that is nonzero, else
    /// `now + effectiveTimeoutMs()`. Clears `m_firstSendPending`.
    TimeMs newSendDeadline(TimeMs now) noexcept;

    /// Sends the ad-hoc queue's head chunk (`m_adHocQueue->nextChunkFrame()`/
    /// `nextChunkRequest()`) and enters `Waiting` for it; sets `m_currentIsAdHoc` so `bytesIn()`
    /// routes the response to `completeAdHocResponse()` instead of `completeCurrentChunk()`;
    /// increments the burst counter. Defined in session_rx.cpp (T-025).
    void sendAdHocChunk(TimeMs now) noexcept;

    /// The current chunk's response parsed `Done` (`ok`) or `Failed` (`!ok`, `err` set): decodes
    /// the payload into `m_values` (or marks the chunk `Failed`), emits `ValuesChanged` from
    /// round 2 (decision S4's silent baseline via `ValueStore::apply()` itself), emits this
    /// type's `Snapshot` when this was its last chunk (round 2 onward; round 1's snapshots are
    /// all held back to `endRound()`), then advances to the next chunk or ends the round.
    /// Defined in session_rx.cpp (spec "Publishing values", decisions S1-S5).
    void completeCurrentChunk(TimeMs now, bool ok, Error err) noexcept;

    /// The ad-hoc queue's in-flight chunk's response parsed `Done` (`ok`) or `Failed` (`!ok`,
    /// `err` set): decodes a read's own chunk payload into the arena, tells `m_adHocQueue`, and
    /// -- when that was the job's last chunk or it failed -- emits `RequestDone`. Defined in
    /// session_rx.cpp (T-025).
    void completeAdHocResponse(TimeMs now, bool ok, Error err) noexcept;

    /// Sends this round's heartbeat write (`m_heartbeatFrameOn`/`m_heartbeatFrameOff`, whichever
    /// `m_round`'s own parity selects -- spec SES-21: round 1, 3, 5, ... writes 1; round 2, 4, ...
    /// writes 0) and enters `Waiting` for it; sets `m_currentIsHeartbeat` so `bytesIn()` routes
    /// the response to `completeHeartbeatResponse()`. Does not touch
    /// `m_adHocBurstSinceLastPollChunk` either way (T-025's own dispatch()-step-1 doc comment:
    /// "the heartbeat frame does not count toward this cap"). Defined in session_rx.cpp (T-026).
    void sendHeartbeat(TimeMs now) noexcept;

    /// The heartbeat's own response parsed `Done` (`ok`) or `Failed` (`!ok`; a PLC error only --
    /// a protocol error/timeout on it faults the link the same as any other in-flight item, via
    /// `faultLink()`, before this is ever called). Sets `m_heartbeatOkThisRound = false` on `!ok`;
    /// never emits `RequestDone` (spec "Ad-hoc requests": "internal ... produces no
    /// RequestDone" -- its outcome is `CycleInfo::heartbeatOk` and the log only). Defined in
    /// session_rx.cpp (T-026).
    void completeHeartbeatResponse(TimeMs now, bool ok) noexcept;

    /// Pre-encodes both heartbeat write frames (bit 1 and bit 0) once, from
    /// `m_config.heartbeat.device`. Called once from the constructor only (unlike
    /// `encodeAllChunks()`, a re-plan never changes the heartbeat device, so there is nothing to
    /// redo at a re-plan boundary). A no-op when `!m_config.heartbeat.enabled`. Defined in
    /// session.cpp (T-026).
    void encodeHeartbeatFrames();

    /// Spec "Faults, retries, EOT": the in-flight item (if any) fails -- a poll chunk's own
    /// `ChunkInfo`/`ValueStore` marking (no `Snapshot`/`CycleDone`: this round is abandoned, not
    /// completed), an ad-hoc job's `RequestDone`, or (heartbeat) just
    /// `m_heartbeatOkThisRound = false` -- every other still-queued ad-hoc job then completes
    /// with `LinkDown` (T-025's own exactly-once rule, same as `linkDown()`'s loop), timers stop,
    /// the state becomes `Faulted`, and `LinkFault{kind, err, reopenTransport}` is emitted
    /// (`reopenTransport` is `true` on Ethernet and `false` on serial, decision S7). The in-flight
    /// item fails with `err`, except while `Flushing`, where it fails with the error of the
    /// attempt that started the flush (`m_attemptError`) and `err` describes the flush itself.
    /// Only `linkDown()` leaves `Faulted` (spec "States"). Defined in session.cpp.
    void faultLink(LinkFaultKind kind, Error err, TimeMs now) noexcept;

    /// Response deadline of the `Waiting` state passed (`tick()`). Ethernet: `faultLink()`. Serial:
    /// `serialAttemptFailed()` with a `Timeout`. Defined in session_rx.cpp.
    void onResponseDeadline(TimeMs now) noexcept;

    /// Serial only (spec "Faults, retries, EOT", serial column): one attempt of the in-flight
    /// item failed with `err` (`kind` `Timeout`: response deadline; `ProtocolError`: a `Protocol`
    /// parse failure or receive-buffer overflow). Counts one consecutive link error, sends `EOT`
    /// when `FrameConfig::sendEotOnError`, then either faults the link (the count reached
    /// `maxConsecutiveLinkErrors`) or enters `Flushing`, remembering `err`; the item is resolved
    /// (resent or failed) by `endFlush()`. Defined in session_rx.cpp.
    void serialAttemptFailed(LinkFaultKind kind, Error err, TimeMs now) noexcept;

    /// `tick()` while `Flushing`: the silence window ended (`endFlush()`), or the whole flush
    /// reached its cap (`effectiveTimeoutMs()` after it began) and counts as one more consecutive
    /// link error. Defined in session_rx.cpp.
    void onFlushDeadline(TimeMs now) noexcept;

    /// The flush is over: a read with retries left is resent (identical bytes, `m_retriesUsed`),
    /// anything else fails with `m_attemptError` through its usual completion path
    /// (`completeCurrentChunk()` / `completeAdHocResponse()` / `completeHeartbeatResponse()`),
    /// which then dispatches. Writes (ad-hoc and heartbeat) are never resent (spec §7.3). Defined
    /// in session_rx.cpp.
    void endFlush(TimeMs now) noexcept;

    /// Resends the in-flight item's frame (the pre-encoded chunk frame, or the ad-hoc queue's
    /// in-flight chunk frame) and enters `Waiting` again. Defined in session_rx.cpp.
    void resendInFlight(TimeMs now) noexcept;

    /// Emits the serial `EOT` (`EOT` alone; `EOT CR LF` for Format 4) as a `Send` when
    /// `FrameConfig::sendEotOnError`. Defined in session_rx.cpp.
    void sendEot() noexcept;

    /// Spec "Drain contract": a precondition violation (an input call made while the previous
    /// one's outputs are still pending). Keeps the spec's own debug/release split (owner
    /// decision, rework after T-027): `detail::notifyDrainViolation()`
    /// (`src/core/session/drain_violation.h`, a private header, not part of this public API)
    /// invokes a replaceable handler in debug builds only (the default asserts); in both builds,
    /// the pending outputs are then discarded and logged at `Error` regardless, right after that
    /// call returns -- so a test that installs a non-aborting handler observes both halves in the
    /// very same build (see that header's own doc comment for the full rationale). Discards
    /// through `nextOutput()` itself, not the output ring directly (phase-review rework: a raw
    /// ring pop here used to bypass `nextOutput()`'s own per-`OutputKind` release bookkeeping --
    /// `m_pendingPlanRefs` for a `Snapshot`/`ValuesChanged`, T-024; `AdHocQueue::confirmDrained()`
    /// for a `RequestDone`, T-025 -- leaving both stuck exactly as if the discarded output were
    /// still queued). Called first by every input method (`subscribe`, `unsubscribe`, `linkUp`,
    /// `linkDown`, `submit`, `bytesIn`, `tick`). Defined in session.cpp (T-026; reworked after
    /// T-027 and again after the Checkpoint C phase review).
    void checkDrained() noexcept;

    /// Emits `Snapshot{type, round, chunksOf(type)}`. Called from `completeCurrentChunk()` (round
    /// 2 onward, this type's last chunk) and from `endRound()`'s own round-1 catch-up loop.
    /// Defined in session_rx.cpp.
    void emitSnapshot(DeviceType type) noexcept;

    /// Emits `CycleDone` and computes `nextRoundAt` for `cycleMode`. In round 1, first emits one
    /// `Snapshot` per subscribed device type, `DeviceType` order (decision S2: every round-1
    /// snapshot is held back to here, none emitted per-type as `completeCurrentChunk()` goes).
    void endRound(TimeMs now) noexcept;

    /// (Re)builds `m_plan`/`m_values` from `m_subs`, pre-encodes every chunk's frame, and resizes
    /// `m_payloadBuf`/`m_changeBuf` to the new plan's own `maxPayloadSize()`/`maxChunkPoints()`.
    void applyRePlan();

    /// Re-encodes every chunk of `m_plan` into `m_encodedChunks` and resizes the scratch buffers
    /// `completeCurrentChunk()` decodes into (allocates).
    void encodeAllChunks();

    FrameConfig m_frameConfig;
    SessionConfig m_config;
    McProtocol m_proto;
    State m_state{State::Down};

    RangeSet m_subs;
    bool m_rePlanPending{false};
    ReadPlan m_plan;
    ValueStore m_values;
    std::vector<ByteBuf> m_encodedChunks; ///< Parallel to `m_plan`'s own chunk indices.

    /// Number of currently-queued (pushed, not yet popped by `nextOutput()`) `Snapshot`/
    /// `ValuesChanged` outputs -- both point into `m_plan`/`m_changeBuf`, which `applyRePlan()`
    /// replaces. `dispatch()` will not start a round that would apply a pending re-plan while
    /// this is nonzero (T-024; see `dispatch()`'s own doc comment above). Incremented in
    /// `emitSnapshot()` and `completeCurrentChunk()`'s own `ValuesChanged` push
    /// (`session_rx.cpp`); decremented in `nextOutput()`.
    size_t m_pendingPlanRefs{0};

    bool m_roundInProgress{false};
    size_t m_currentChunk{0};
    uint32_t m_round{0};
    TimeMs m_roundStartedAt{0};
    TimeMs m_nextRoundAt{0};
    uint16_t m_requestsThisRound{0};
    uint16_t m_failedChunksThisRound{0};

    std::optional<Parser> m_parser;
    std::vector<uint8_t> m_rxBuffer;
    /// While `Waiting`: Ethernet, `effectiveTimeoutMs()` after the send; serial, the same until
    /// the first byte arrives, then `serialInterCharMs` after the latest byte.
    TimeMs m_responseDeadline{kNoDeadline};
    /// Set by `linkUp()`, cleared by the first new send and by `linkDown()`: that send's deadline
    /// is `firstResponseTimeoutMs` (when nonzero).
    bool m_firstSendPending{false};

    /// Serial: link errors in a row (timeouts, protocol errors, flushes that reached their
    /// cap); any well-formed response and `linkDown()` reset it.
    uint32_t m_consecutiveLinkErrors{0};
    /// Resends already made of the in-flight item; reset whenever a new item is sent.
    uint8_t m_retriesUsed{0};
    /// The error of the attempt that started the current flush (what the item fails with when it
    /// is not resent).
    Error m_attemptError{};
    /// While `Flushing`: when the latest discarded byte (or the `EOT`) went by; the silence
    /// window ends `serialFlushMs` later.
    TimeMs m_flushLastActivityAt{0};
    /// While `Flushing`: end of the whole flush (`effectiveTimeoutMs()` after it began).
    TimeMs m_flushCapAt{0};

    /// Scratch storage `completeCurrentChunk()` decodes into, resized to the current plan's own
    /// `maxPayloadSize()`/`maxChunkPoints()` whenever the plan changes (`Session`'s own "change
    /// arena": `output_ring.h`'s identically-named part of the module spec's Project Structure is
    /// not used for this, see T-022/T-023 Dev notes). `Output::payload`/`Output::changes` point
    /// directly into these, valid until the next input call, per `Output`'s own contract.
    std::vector<uint8_t> m_payloadBuf;
    std::vector<Change> m_changeBuf;

    TimeMs m_lastNow{0};
    SessionStats m_stats{};

    std::unique_ptr<detail::OutputRing> m_outputRing;

    /// T-025 (ad-hoc requests): `m_adHocQueue` owns the FIFO job ring and byte arena
    /// (`adHocCapacity`/`adHocArenaBytes`); everything else here is bookkeeping only `Session`
    /// itself needs, since one `Waiting`/`Parser`/`rxBuffer` is shared between polling and ad-hoc
    /// (spec §8.6: one request on the wire).
    std::unique_ptr<detail::AdHocQueue> m_adHocQueue;
    /// Routes `bytesIn()`'s completion to `completeAdHocResponse()` instead of
    /// `completeCurrentChunk()`.
    bool m_currentIsAdHoc{false};
    /// Reset whenever a polling chunk is sent (`dispatch()`'s own step 2); `maxAdHocBurst == 0`
    /// means no cap (checked separately).
    uint8_t m_adHocBurstSinceLastPollChunk{0};

    /// T-026 (heartbeat): `m_heartbeatFrameOn`/`Off` are pre-encoded once
    /// (`encodeHeartbeatFrames()`, constructor only -- the heartbeat device never changes at a
    /// re-plan) so `sendHeartbeat()` at every round start stays allocation-free, mirroring
    /// `m_encodedChunks`. `m_currentIsHeartbeat` routes `bytesIn()`'s completion to
    /// `completeHeartbeatResponse()` instead of `completeCurrentChunk()`/
    /// `completeAdHocResponse()`; `m_heartbeatOkThisRound` is reset `true` in `startRound()` and
    /// read (only when `heartbeat.enabled`) into `CycleInfo::heartbeatOk` in `endRound()`.
    ByteBuf m_heartbeatFrameOn;
    ByteBuf m_heartbeatFrameOff;
    bool m_currentIsHeartbeat{false};
    bool m_heartbeatOkThisRound{true};
};

} // namespace mc
