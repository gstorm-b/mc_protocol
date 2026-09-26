# Spec: core-session

- **Module id:** `core-session` (see `docs/spec/CAPABILITY-MAP.md`)
- **Status:** approved by the owner, 2026-09-26
- **Depends on:** `core-protocol` (and through it `core-model`)
- **Depended on by:** `qt-device`
- **Inputs:** owner interview 2026-09-25 (value-publishing contract, dynamic subscriptions; see "Decisions this spec implements"); ideas doc §4.3, §5, §9.3; reference spec `docs/mc_reference/mc-protocol-frame-spec.md` §4.4, §6.1, §6.3, §7.3, §8.5, §8.6, §9.9, §9.10; the old engine in `reference_source/device/plc/mc_protocol_device.cpp`, `mc_device_map_diff.h`, `mc_device_map.cpp`, and the old Modbus poller `reference_source/device/plc/modbus/modbus_tcp_client_device.cpp`, `modbus_register_map.cpp`; `SPEC-core-model.md`, `SPEC-core-protocol.md`.

## Objective

Provide the **polling engine** of the library as a sans-I/O state machine in standard C++: the application (or `qt-device`) feeds it received bytes, the current time, subscriptions and ad-hoc requests; it hands back bytes to send and events to publish. It never opens a socket, never sleeps, never reads a clock, never starts a thread.

This is the piece the old app could not test: *"nothing in this repository has ever executed a McProtocolDevice method"* (old `mc_device_map_diff.h`), because the engine was welded to its transport. Here the whole behaviour (rounds, retries, timeouts, change detection, request correlation, heartbeat) runs in a unit test with a fake clock and scripted bytes.

The module also ships the std-only building blocks the intent asks for ("chia nhỏ request, gom vùng nhớ liền nhau thành ít request nhất, lưu giá trị device đã đọc"), public so a consumer who writes their own engine can reuse them: `RangeSet`, `ReadPlan`, `ValueStore`.

**User stories**

- As an application, I subscribe `D2000×64` and `M2000×64`, connect, and after the first complete round I receive one snapshot per device type; from then on I receive a change event whenever a response shows a changed value, and a snapshot of each device type every round.
- As an application, I never see a value of 0 that the PLC did not send: a point that has not been read yet, or whose last read failed, says so.
- As an application, every ad-hoc request I submit returns an id and produces **exactly one** completion, even if the link drops before it is sent.
- As a v2 monitor widget, I add and remove subscriptions while polling runs; they take effect at the next round boundary.
- As a non-Qt integrator, I drive the engine from my own event loop with five input calls and one output loop.
- As a maintainer, I prove that a steady-state polling round performs zero heap allocations.

### Decisions this spec implements (owner interview, 2026-09-25)

| # | Decision |
|---|---|
| S1 | **Snapshot per device type** ("vùng memory" = a device type: M, X, Y, D, …). A snapshot covers every subscribed point of that type. |
| S2 | **Round 1 after every `linkUp`**: no change events; all snapshots are held back and published together when round 1 completes. |
| S3 | **From round 2**: a change event is published **per response** that shows changed values; the snapshot of a type is published as soon as the last chunk of that type in the round completes. |
| S4 | A point read for the first time (new subscription, or a chunk that failed until now) becomes a **silent baseline**: no change event for it. |
| S5 | A failed chunk marks its points *failed* (last known value kept, never zero-filled) and never blocks other chunks or the round. |
| S6 | **Subscriptions are dynamic**; changes are applied at the next round boundary. |
| S7 | (decision 7) On a link fault the engine **only reports**; reconnecting is the application's decision. |
| S8 | (decision 6) Heartbeat is optional, off by default. |

## Tech Stack

C++17, standard library only, doctest. Same rules as `core-model`: `noexcept` public functions where they cannot fail by allocation, no exceptions across the boundary, no Qt, no threads, no clock reads.

## Commands

```powershell
cmake --build build/cmake-debug --target mc_core_session_tests
ctest --test-dir build/cmake-debug -R core_session --output-on-failure
cmake -S . -B build/cmake-bench -DMC_BUILD_BENCH=ON -DCMAKE_BUILD_TYPE=Release; cmake --build build/cmake-bench --target mc_bench_session
```

## Project Structure

```text
include/mc/core/
├── poll_plan.h        SubscriptionId, RangeSet, PlanOptions, kAutoGap, ChunkState, ChunkInfo, ReadPlan
├── value_store.h      PointState, Change, SegmentView, ValueStore
└── session.h          TimeMs, kNoDeadline, RequestId, CycleMode, HeartbeatConfig, SessionConfig,
                       OutputKind, LinkFaultKind, CycleInfo, Output, SessionStats, Session
src/core/session/
├── range_set.cpp      subscription bookkeeping, union into segments
├── read_plan.cpp      alignment, coalescing, gap merge, chunking, autoGap()
├── value_store.cpp    segments, apply(), markFailed(), carry-over on re-plan
├── adhoc_queue.h/.cpp fixed-capacity ring of ad-hoc requests
├── output_ring.h      fixed-capacity ring of Output records + change arena
├── session.cpp        state machine, dispatch, rounds
└── session_rx.cpp     receive buffer, Parser driving, EOT and flush handling
tests/core/session/
├── harness.h/.cpp     FakeClock, ScriptedPeer (3E Binary/ASCII responder), OutputRecorder
├── test_range_set.cpp     PLN-xx
├── test_value_store.cpp   STO-xx
├── test_session_poll.cpp  SES-01…08, SES-22, SES-23, SES-26
├── test_session_adhoc.cpp SES-09…15
├── test_session_fault.cpp SES-16…20, SES-24
├── test_session_heartbeat.cpp SES-21
└── test_alloc.cpp         ALC-01, ALC-02
tests/bench/
└── bench_session.cpp      encode-free round cost vs subscribed points (regression tracking only)
examples/session_loop/
└── main.cpp           non-Qt usage: Session ↔ MockPlc through an in-memory pipe (links mc::mock)
```

## Public API

### Identifiers and time (`session.h`, `poll_plan.h`)

```cpp
namespace mc {
using TimeMs = uint64_t;                          ///< Monotonic milliseconds supplied by the caller.
inline constexpr TimeMs kNoDeadline = UINT64_MAX; ///< nextDeadline() when nothing is pending.
using RequestId = uint64_t;                       ///< Ad-hoc request id; never 0, never reused in a Session.
using SubscriptionId = uint32_t;                  ///< Never 0, never reused in a RangeSet.
}
```

The engine never reads a clock. Every input that can start a timer takes `now`; `now` must not decrease between calls (a decrease is clamped to the last value and logged at `Warn`).

### `poll_plan.h` — subscriptions and the read plan

```cpp
namespace mc {

/// The set of subscribed ranges. Order-independent; overlapping and adjacent subscriptions are
/// allowed and are unioned when a plan is built.
class RangeSet {
public:
    /// Adds `count` consecutive points from `head` (bits for bit devices, words for word
    /// devices). Checks count >= 1 and that head + count - 1 fits the device number (u32).
    /// Frame-specific checks happen in ReadPlan::build(). O(log S) search + O(S) insert.
    Expected<SubscriptionId> add(Device head, uint32_t count);
    /// Error{Config, NotSubscribed} for an unknown id. O(S).
    Expected<void> remove(SubscriptionId id);
    size_t size() const noexcept;
    bool empty() const noexcept;
};

inline constexpr uint32_t kAutoGap = UINT32_MAX;

struct PlanOptions {
    /// Poll bit subscriptions with word reads (16 points per word), head aligned down and end
    /// aligned up to a multiple of 16 (M9000–M9255 on 1E/1C: 9000 + 16k, spec §3.5). Default on:
    /// 4× fewer wire bytes than bit reads and a larger point limit per request.
    /// Risk: if a PLC's configured range of a bit device does not end on a multiple of 16, a
    /// subscription in that last group reads past the end and its chunk fails every round.
    /// Turn this off for such a PLC (INT-16 pins the behaviour).
    bool bitsAsWords{true};
    /// Merge two read intervals of the same device type when the unread gap between them is at
    /// most this many read units (words, or points for bit reads). kAutoGap = autoGap(cfg).
    uint32_t maxGap{kAutoGap};
};

/// The gap that costs no more wire bytes than a second request would:
///   floor((requestSize(1 unit) + responseSize(1 unit) - unitWireSize) / unitWireSize)
/// computed from McProtocol sizes for the frame. Examples: 3E Binary words 16, 3E ASCII
/// words 16, 1E Binary words 7. O(1).
uint32_t autoGap(const FrameConfig& cfg, Op readOp) noexcept;

enum class ChunkState : uint8_t { NotRead, Ok, Failed };

struct ChunkInfo {
    Request request;          ///< ReadWords or ReadBits; head/count after alignment and chunking.
    ChunkState state{ChunkState::NotRead};
    Error lastError{};        ///< Set when state == Failed.
    uint32_t lastOkRound{0};  ///< 0 = never read successfully since linkUp.
};

/// Coalesced, gap-merged, aligned and chunked read requests for a RangeSet on one FrameConfig.
/// Chunks are ordered by (DeviceType enum order, head number); the chunks of one device type are
/// contiguous.
class ReadPlan {
public:
    /// Algorithm, per device type:
    ///  1. each subscription → an interval in read units (bitsAsWords: aligned to 16 points);
    ///  2. sort, union overlapping/adjacent intervals, then merge neighbours whose gap <= maxGap;
    ///  3. split each interval with mc::chunk() using maxPoints() for the read op (spec §8.5);
    ///  4. validate() every chunk (device supported by the family, width, alignment).
    /// A subscription that fails step 4 makes build() fail with that error and names nothing
    /// else; the previous plan stays in force. O(S log S + C); allocates.
    static Expected<ReadPlan> build(const RangeSet& subs, const FrameConfig& cfg, const PlanOptions& opt);

    size_t size() const noexcept;
    const ChunkInfo& chunk(size_t i) const noexcept;
    /// [first, last) chunk indices of one device type; empty range if not subscribed. O(1).
    std::pair<size_t, size_t> chunksOf(DeviceType t) const noexcept;
    /// Largest normalized payload of any chunk, to size a decode buffer once.
    size_t maxPayloadSize() const noexcept;
    /// Largest number of subscribed points in one chunk, to size a change buffer once.
    size_t maxChunkPoints() const noexcept;
};
}
```

### `value_store.h` — values of subscribed points

```cpp
namespace mc {

enum class PointState : uint8_t {
    NotSubscribed,  ///< Not covered by any subscription (includes gap points read for merging).
    NoValue,        ///< Subscribed, not read successfully since the last linkUp.
    Valid,          ///< Last read succeeded.
    Failed,         ///< Last read failed; the value is the last known one (from this link session).
    Stale           ///< Link is down; the value is the last known one.
};

/// One changed point. Bit points carry 0/1.
struct Change { Device device; uint16_t oldValue; uint16_t newValue; };

/// A maximal run of subscribed points of one type (union of subscriptions; gaps excluded).
struct SegmentView {
    Device head;
    uint32_t count;
    const uint16_t* words;   ///< Word device: count values. Null for bit devices.
    const uint8_t* bits;     ///< Bit device: count values, 0/1. Null for word devices.
    const uint8_t* states;   ///< count PointState values.
};

class ValueStore {
public:
    /// O(log G), G = segments of the type. Never allocates.
    PointState state(Device d) const noexcept;
    /// Last known value, or 0 when state() is NotSubscribed or NoValue. Callers that care check
    /// state() first. O(log G).
    uint16_t word(Device d) const noexcept;
    bool bit(Device d) const noexcept;

    /// Copies `count` points starting at `head` in the normalized payload layout (words: 2 bytes
    /// LE; bits: per `layout`). Error{Config, NotSubscribed} if any point is not subscribed;
    /// BufferTooSmall if `out` is short. Values of NoValue points are copied as 0 — check
    /// state() or segment states when that matters. O(log G + count).
    Expected<size_t> words(Device head, uint32_t count, MutableByteView out) const noexcept;
    Expected<size_t> bits(Device head, uint32_t count, MutableByteView out,
                          BitLayout layout = BitLayout::BytePerPoint) const noexcept;

    size_t segmentCount(DeviceType t) const noexcept;
    SegmentView segment(DeviceType t, size_t i) const noexcept;

    // --- engine side (used by Session; public so a custom engine can reuse the store) ---

    /// Re-shapes the store for a new RangeSet: points covered before and after keep value and
    /// state; new points start NoValue; dropped points disappear. O(P_old + P_new); allocates.
    void rebuild(const RangeSet& subs);
    /// Decodes one chunk's normalized payload (as written by Parser::payload()) into the store.
    /// Converts raw words to points for bitsAsWords chunks (convert::wordsToBits semantics).
    /// Writes one Change per subscribed point whose state was Valid or Failed and whose value
    /// differs; points without a baseline (NoValue) become Valid silently (decision S4).
    /// Returns the number of changes written; never more than plan.maxChunkPoints().
    /// O(n) in the chunk's points; no allocation.
    size_t apply(const ReadPlan& plan, size_t chunk, ByteView payload,
                 Change* out, size_t capacity) noexcept;
    /// Marks the chunk's subscribed points Failed (NoValue points stay NoValue). O(n).
    void markFailed(const ReadPlan& plan, size_t chunk) noexcept;
    /// Link down: every Valid/Failed point becomes Stale; the next linkUp clears every baseline
    /// (all points NoValue), which is what makes round 1 silent. O(1) (epoch counter).
    void markStale() noexcept;
    void resetBaselines() noexcept;
};
}
```

**Storage.** Per device type, a sorted vector of segments over one contiguous value array (`uint16_t` per word point, `uint8_t` per bit point) and one contiguous `uint8_t` state array. Gap points read to save a round trip are decoded and discarded: they are never stored, never reported. Memory is O(P) with P the number of subscribed points.

### `session.h` — the engine

```cpp
namespace mc {

enum class CycleMode : uint8_t {
    FixedRate,   ///< Round k+1 starts at start(k) + interval, or immediately if round k ran longer. No catch-up bursts.
    FixedDelay   ///< Round k+1 starts at end(k) + interval.
};

struct HeartbeatConfig {
    bool enabled{false};                   ///< Decision 6: off by default.
    Device device{DeviceType::M, 2000};    ///< A bit device supported by the frame.
};

struct SessionConfig {
    uint32_t cycleIntervalMs{100};         ///< 0 = rounds back-to-back.
    CycleMode cycleMode{CycleMode::FixedRate};
    PlanOptions plan{};
    uint16_t adHocCapacity{64};            ///< Queued ad-hoc requests; submit() beyond it → QueueFull.
    uint32_t adHocArenaBytes{64 * 1024};   ///< FIFO ring holding every queued request's frames and read payload; allocated once in create().
    uint8_t maxAdHocBurst{4};              ///< During a round, at most this many ad-hoc frames between two polling chunks; 0 = strict priority.
    uint8_t maxConsecutiveLinkErrors{3};   ///< Serial only: timeouts/protocol errors in a row before LinkFault.
    uint16_t serialInterCharMs{100};       ///< Serial only: once a response's first byte has arrived, the deadline is last byte + this (spec §6.3).
    uint16_t serialFlushMs{50};            ///< Serial only: after EOT, discard bytes until the line has been silent this long (restarts on every byte; capped at effectiveTimeoutMs()).
    HeartbeatConfig heartbeat{};
    LogSink* log{nullptr};                 ///< Not owned; null = NullLogSink. Category "mc.session".
    Expected<void> validate(const FrameConfig& frame) const noexcept;
};

enum class OutputKind : uint8_t {
    Send,           ///< bytes: write them to the transport, in order.
    ValuesChanged,  ///< deviceType, round, changes[0..changeCount): one response's changes (round >= 2 only).
    Snapshot,       ///< deviceType, round, chunks[0..chunkCount): read values().segment(deviceType, …).
    CycleDone,      ///< cycle: one round finished.
    RequestDone,    ///< requestId, error, payload: exactly one per accepted submit().
    LinkFault       ///< fault, error, reopenTransport: the engine has stopped sending; see "Faults".
};

enum class LinkFaultKind : uint8_t { Timeout, ProtocolError };

struct CycleInfo {
    uint32_t round;
    TimeMs startedAt;
    uint32_t durationMs;
    uint16_t requests;       ///< Frames sent in the round, retries and ad-hoc included.
    uint16_t failedChunks;
    bool heartbeatOk;        ///< true when disabled.
};

/// One event. Only the fields of `kind` are meaningful. Every view (bytes, changes, chunks,
/// payload) stays valid until the next call to any Session input method.
struct Output {
    OutputKind kind{OutputKind::Send};
    ByteView bytes{};
    DeviceType deviceType{DeviceType::D};
    uint32_t round{0};
    const Change* changes{nullptr};
    size_t changeCount{0};
    const ChunkInfo* chunks{nullptr};
    size_t chunkCount{0};
    CycleInfo cycle{};
    RequestId requestId{0};
    Error error{};
    ByteView payload{};
    LinkFaultKind fault{LinkFaultKind::Timeout};
    bool reopenTransport{false};
};

struct SessionStats {
    uint64_t rounds, framesSent, bytesSent, bytesReceived;
    uint64_t timeouts, protocolErrors, plcErrors, retries, eotsSent;
};

class Session {
public:
    /// Validates both configs (FrameConfig::validate(), SessionConfig::validate()) and sizes every
    /// buffer the steady state needs. Allocates. The Session is movable, not copyable.
    static Expected<Session> create(const FrameConfig& frame, const SessionConfig& cfg = {});

    // ---- inputs -------------------------------------------------------------------------
    /// Takes effect at the next round boundary (decision S6). Errors: those of RangeSet::add and,
    /// checked immediately against the frame, InvalidDevice / PointCount.
    Expected<SubscriptionId> subscribe(Device head, uint32_t count);
    Expected<void> unsubscribe(SubscriptionId id);

    /// The transport is open. Clears every baseline (ValueStore::resetBaselines) and starts
    /// round 1 at `now` (the first Send is produced by this call).
    /// Ignored with a Warn log when already up or faulted (call linkDown first).
    void linkUp(TimeMs now) noexcept;
    /// The transport is closed or lost. Completes every in-flight and queued ad-hoc request with
    /// Error{Transport, LinkDown}, in submission order, marks values Stale, stops all timers.
    void linkDown(TimeMs now) noexcept;

    /// Queues an ad-hoc request (typically a write). Chunked with mc::chunk(): reads always,
    /// writes only with FrameConfig::splitWrites (else PointCount, nothing queued). Every chunk's
    /// frame and the read payload space are placed in the ad-hoc arena (r.data is not kept).
    /// Errors (no id, no RequestDone): validate() errors; PointCount (also when the request alone
    /// needs more than adHocArenaBytes); QueueFull (adHocCapacity reached or arena space
    /// exhausted); LinkDown when the link is not up. On success exactly one RequestDone with this
    /// id follows, ever. No allocation.
    Expected<RequestId> submit(const Request& r, TimeMs now);

    /// Bytes received from the transport, in order, any fragmentation.
    void bytesIn(ByteView bytes, TimeMs now) noexcept;
    /// Time passed. Call at or after nextDeadline(); extra calls are harmless.
    void tick(TimeMs now) noexcept;

    // ---- outputs ------------------------------------------------------------------------
    /// Pops the next output. Returns false when none is left. The caller MUST drain until false
    /// after every input call and before the next one (see "Drain contract").
    bool nextOutput(Output& out) noexcept;
    /// Earliest time the engine needs tick(): response timeout, flush end or next round start.
    TimeMs nextDeadline() const noexcept;

    // ---- observation --------------------------------------------------------------------
    const ValueStore& values() const noexcept;
    const ReadPlan& plan() const noexcept;       ///< The plan of the current round.
    const SessionStats& stats() const noexcept;
    bool isLinkUp() const noexcept;
    bool isFaulted() const noexcept;
};
}
```

### Non-Qt usage

```cpp
mc::SessionConfig cfg;
cfg.cycleIntervalMs = 100;
auto created = mc::Session::create(mc::FrameConfig::frame3E(), cfg);
if (!created) { report(created.error()); return; }
mc::Session s = std::move(created.value());
s.subscribe(mc::parseDevice("D2000").value(), 64);
s.subscribe(mc::parseDevice("M2000").value(), 64);

socket.open();
s.linkUp(clock.nowMs());
mc::Output out;
for (;;) {
    bool faulted = false;
    while (s.nextOutput(out)) {                       // drain completely before any input call
        switch (out.kind) {
        case mc::OutputKind::Send:          socket.send(out.bytes.data, out.bytes.size); break;
        case mc::OutputKind::ValuesChanged: onChanges(out.deviceType, out.changes, out.changeCount); break;
        case mc::OutputKind::Snapshot:      onSnapshot(out.deviceType, s.values()); break;
        case mc::OutputKind::RequestDone:   onDone(out.requestId, out.error, out.payload); break;
        case mc::OutputKind::LinkFault:     faulted = true; break;   // the app decides (decision S7)
        case mc::OutputKind::CycleDone:     break;
        }
    }
    if (faulted) {                                    // this app chooses to reconnect at once
        s.linkDown(clock.nowMs());
        while (s.nextOutput(out)) { /* RequestDone{LinkDown} for anything queued */ }
        socket.close(); socket.open();
        s.linkUp(clock.nowMs());
        continue;
    }
    size_t n = socket.receiveUntil(buf, sizeof buf, s.nextDeadline());   // the app's own wait
    if (n > 0) s.bytesIn({buf, n}, clock.nowMs()); else s.tick(clock.nowMs());
}
```

## Behaviour

### States

| State | Meaning | Leaves on |
|---|---|---|
| `Down` | No link. Nothing is sent; `bytesIn` is discarded. | `linkUp` → round 1 starts |
| `Idle` | Link up, nothing in flight, waiting for work or the next round. | work available (dispatch), `linkDown` |
| `Waiting` | One frame in flight (spec §8.6: one request on the wire). | response complete, response deadline, `linkDown` |
| `Flushing` | Serial only: EOT sent, discarding bytes until the line is silent for `serialFlushMs`. Every discarded byte restarts the silence timer; the whole window is capped at `effectiveTimeoutMs()`, and reaching the cap counts as one more consecutive link error. | silence reached → dispatch; cap reached → dispatch or fault; `linkDown` |
| `Faulted` | A link fault was reported. Nothing is sent; `bytesIn` is discarded. | `linkDown` only (decision S7) |

### Dispatch (entering `Idle`)

1. If the ad-hoc queue is not empty **and** either no round is in progress or fewer than `maxAdHocBurst` ad-hoc frames have been sent since the last polling chunk, send its head. (`maxAdHocBurst = 0` means no cap: strict priority, like the old engine. The heartbeat frame does not count toward the cap.)
2. Else, if a round is in progress, send its next chunk's pre-encoded frame and reset the burst counter.
3. Else, if `now >= nextRoundAt`, start a round: apply a pending re-plan (`ReadPlan::build` + `ValueStore::rebuild`, the only allocation point after `create`), increment the round number, record `startedAt`, put the heartbeat write (if enabled) in front, then go to 1.
4. Else stay `Idle` with `nextDeadline() == nextRoundAt`.

`nextRoundAt` after a round ends: `FixedRate` → `max(startedAt + interval, end)`; `FixedDelay` → `end + interval`. With interval 0 both are `end`. A round with no chunks still runs (it carries the heartbeat) and still emits `CycleDone`.

### Publishing values (decisions S1–S5)

When a polling chunk completes, in this order:

1. **Success:** `ValueStore::apply` writes the values. From round 2, if it produced changes, emit `ValuesChanged{deviceType, round, changes}` for this response only. In round 1 nothing is emitted (every point is `NoValue` after `linkUp`, so `apply` reports no change anyway; the rule is also enforced explicitly).
   **PLC error, or failure after retries:** `ValueStore::markFailed`; `ChunkInfo::state = Failed`, `lastError` set. No retry of PLC errors (the PLC would refuse again).
2. From round 2, if this was the last chunk of its device type, emit `Snapshot{deviceType, round, chunks of that type}`.
3. If this was the last chunk of the round: in round 1, emit one `Snapshot` per subscribed device type in `DeviceType` order; then emit `CycleDone`.

Timeline (3E, subscriptions `M0×128`, `D100×64`, `D2000×64`; the third chunk NAKs in round 1):

```text
linkUp(t0)                         → Send(M chunk)                       round 1 starts
bytesIn(M ok)                      → Send(D chunk#1)                     silent
bytesIn(D#1 ok)                    → Send(D chunk#2)                     silent
bytesIn(D#2 PLC C051)              → Snapshot{M, r1} → Snapshot{D, r1: #1 Ok, #2 Failed C051}
                                     → CycleDone{r1, failedChunks=1}
tick(t0+100)                       → Send(M chunk)                       round 2 starts
bytesIn(M, M5 0→1)                 → ValuesChanged{M, r2, [M5 0→1]} → Snapshot{M, r2} → Send(D#1)
bytesIn(D#1, D105 7→9)             → ValuesChanged{D, r2, [D105 7→9]} → Send(D#2)
bytesIn(D#2 ok, first time)        → Snapshot{D, r2: all Ok}  (D2000.. silent baseline, S4)
                                     → CycleDone{r2, failedChunks=0}
```

### Ad-hoc requests

- Chunked at `submit` time; each chunk's frame is encoded into the **ad-hoc arena**, followed by the space for the read payload. The arena is a byte ring sized by `adHocArenaBytes` and allocated once in `create()`: `submit` reserves at the tail, completion releases at the head. This works without fragmentation because ad-hoc requests complete strictly in submission order (one in flight, FIFO queue, `linkDown` completes them in order). Reads always split; writes split only with `splitWrites` (spec §8.5, non-atomic).
- Completion: after the last chunk, or at the first failed chunk (remaining chunks are not sent). `RequestDone.payload` holds the whole normalized read payload (`Request::bitLayout` applies); empty for writes.
- Ad-hoc reads do **not** update the `ValueStore` and never produce `ValuesChanged`; the store reflects polling only.
- The heartbeat write is internal: it produces no `RequestDone`; its outcome is in `CycleInfo::heartbeatOk` and the log.

### Faults, retries, EOT (spec §6.1, §6.3, §7.3)

| Event | Ethernet (3E, 1E) | Serial (3C, 1C) |
|---|---|---|
| Response deadline passes. Ethernet: `effectiveTimeoutMs()` after the send, for the whole response. Serial: `effectiveTimeoutMs()` after the send **until the first byte**; from then on, `serialInterCharMs` after the latest byte (spec §6.3), so a long response at low baud completes and a response cut mid-frame is detected quickly. A line that never stops sending is ended by the receive-buffer overflow row. | In-flight item fails with `Timeout`; queued ad-hoc complete with `LinkDown`; state `Faulted`; emit `LinkFault{Timeout, reopenTransport=true}`. No resend on this connection: a late response would be taken for the next request's (spec §6.1). | Send `EOT` (F4: `EOT CR LF`) when `sendEotOnError`; `Flushing` until the line is silent for `serialFlushMs`; then a **read** is resent up to `readRetries` times; a **write** fails with `Timeout`, never resent (spec §7.3). |
| Parser fails with a `Protocol` error | Same as timeout, `LinkFault{ProtocolError, reopenTransport=true}`: the byte stream cannot be trusted. | As timeout: EOT, flush, retry reads, fail writes. |
| Parser fails with a `Plc` error | Item fails with the PLC error; link is healthy; no retry. | Same. |
| Consecutive timeouts + protocol errors reach `maxConsecutiveLinkErrors` | (never reached: the first one faults) | State `Faulted`; `LinkFault{kind of the last one, reopenTransport=false}`. |
| Any well-formed response, including a PLC error | resets the consecutive counter | same |
| Bytes arrive while `Idle` | Unexpected on a one-request link: `LinkFault{ProtocolError, reopenTransport=true}`. | Discarded, logged at `Warn`. |
| Bytes arrive while `Flushing`, `Faulted`, `Down` | discarded, logged at `Trace` | same |
| Receive buffer would overflow (sized to the largest possible response at `create`) | `LinkFault{ProtocolError, reopenTransport=true}` | Discard, EOT, flush. |

`readRetries` therefore only acts on serial links. After any `LinkFault` the application chooses: reopen the transport and call `linkDown`/`linkUp`, or just `linkDown`. The engine never reconnects (decision S7).

### Exactly-once completion

Every `submit()` that returned an id produces exactly one `RequestDone`. The paths are: success; PLC error; `Timeout`; failure after retries; `LinkDown` (from `linkDown()` or from a fault that stops the engine with requests still queued). A request that vanishes is worse than one that fails; this rule is tested on every path (SES-15, SES-16).

### Drain contract

After every input call (`linkUp`, `linkDown`, `submit`, `bytesIn`, `tick`, `subscribe`, `unsubscribe`) the caller drains `nextOutput()` until it returns false, before the next input call. Views inside an `Output` are valid until the next input call. An input call made with outputs still pending is a precondition violation: debug builds assert; release builds discard the pending outputs and log at `Error`. The output ring is sized at `create()` for the worst single call (`adHocCapacity + 1` completions on `linkDown`, one snapshot per device type at the end of round 1, plus a constant), so it never grows.

### Logging (spec §8.6)

Category `mc.session`. `Trace`: TX/RX hex dumps via `hexDump` (control-code names for ASCII frames). `Debug`: round start/end, re-plan summary (types, chunks, merged gaps). `Info`: link up/down. `Warn`: retries, EOT, discarded bytes, clock going backwards. `Error`: faults, drain-contract violations. The pattern of `core-model` applies: `enabled()` is checked before any string is built.

## Complexity and allocation (binding; refines ideas §9.3)

S = subscriptions, G = segments of one type, C = chunks, P = subscribed points, n = points of one chunk, k = bytes in one `bytesIn`, Q = queued ad-hoc requests.

| Function | Time | Allocation |
|---|---|---|
| `RangeSet::add` / `remove` | O(log S) search + O(S) shift | vector growth only |
| `ReadPlan::build` | O(S log S + C) | yes (off the steady state) |
| `ValueStore::rebuild` | O(P_old + P_new) | yes (off the steady state) |
| `ValueStore::state`, `word`, `bit` | O(log G) | none |
| `ValueStore::words`, `bits` | O(log G + count) | none |
| `ValueStore::apply`, `markFailed` | O(n) | none |
| `ValueStore::markStale`, `resetBaselines` | O(1) | none |
| `Session::create` | O(1) + buffer sizing | yes |
| `subscribe`, `unsubscribe` | as `RangeSet`; re-plan deferred to the round boundary | vector growth only |
| `linkUp` | O(1) + first dispatch | none |
| `linkDown` | O(Q) | none |
| `submit` | O(n) validate + chunk + encode | none (ad-hoc arena) |
| `bytesIn` | O(k) parse; + O(n) decode and diff when a response completes | none |
| `tick` | O(1) amortized | none |
| `nextOutput`, `nextDeadline`, `values`, `plan`, `stats` | O(1) | none |
| **One polling round, steady state** | **O(P + wire bytes of the round)** | **none** (heartbeat included; frames pre-encoded at re-plan) |

## Code Style

Inherited from `SPEC-build-packaging.md`. The state machine is a `switch` over an `enum class State`; each input method is a short function that validates, mutates, and calls `dispatch()`. No virtual functions on the hot path; the only virtual call is `LogSink::enabled()`.

```cpp
// src/core/session/session.cpp
void Session::bytesIn(ByteView bytes, TimeMs now) noexcept {
    checkDrained();
    now = clampNow(now);
    m_stats.bytesReceived += bytes.size;
    switch (m_state) {
    case State::Waiting:  appendRx(bytes); pumpParser(now); break;
    case State::Idle:     onUnsolicited(bytes, now); break;  // Ethernet: fault; serial: discard
    case State::Flushing:
    case State::Faulted:
    case State::Down:     logDiscard(bytes); break;
    }
}
```

## Testing Strategy

doctest binary `mc_core_session_tests`, label `core_session`. The harness gives every test a `FakeClock` (an integer), a `ScriptedPeer` that answers 3E Binary/ASCII read and write frames from a word/bit image (built from the `core-protocol` sizes and the spec §5.1 layout, test-only), and an `OutputRecorder` that drains the session after every input and stores copies. Serial behaviour uses the golden vectors directly (e.g. subscribe `D100×3` on 3C F1: the session's request must equal `V-3C1-01`; the test replies with `V-3C1-02`, a truncated copy, or a copy with a wrong SUM). Serial tests set `readRetries = 2` unless stated (the `FrameConfig` default is 0). The cross-frame integration matrix runs in `mock-plc` (INT-xx) because it needs a full responder.

| ID | Covers |
|---|---|
| PLN-01 | Adjacent subscriptions (D100×10, D110×10) → one chunk D100×20 |
| PLN-02 | Overlapping subscriptions → union; removing one keeps the other's points |
| PLN-03 | `autoGap`: 3E Binary words = 16, 3E ASCII words = 16, 1E Binary words = 7; gap = autoGap merges, autoGap + 1 splits |
| PLN-04 | `bitsAsWords`: M5×3 → ReadWords M0×1; X1A×4 → ReadWords X10×1; M9005×4 on 1E → ReadWords M9000×1; M9020×4 on 1E → M9016 |
| PLN-05 | `bitsAsWords=false` → ReadBits with exact head/count |
| PLN-06 | Splitting: D0×2000 on 3E Binary → 960/960/80 (API-01); M0×8000 with bitsAsWords → one chunk of 500 words (vs 2 chunks as bits, API-02) |
| PLN-07 | Chunk order is (DeviceType, head); `chunksOf()` ranges are contiguous and correct |
| PLN-08 | Empty RangeSet → empty plan; round still runs, `CycleDone` emitted |
| PLN-09 | `build` rejects SM on 1E, width overflow on 1C, and keeps the previous plan |
| PLN-10 | Re-plan carries value and state for surviving points; new points `NoValue`; dropped points `NotSubscribed` |
| STO-01 | `word`, `bit`, bulk `words`/`bits` across two chunks of one segment; normalized layout |
| STO-02 | State transitions NoValue → Valid → Failed (value kept) → Valid; `markStale`; `resetBaselines` |
| STO-03 | Gap points are `NotSubscribed`, never stored, never in a `Change` |
| STO-04 | `apply` on a bitsAsWords chunk reports per-point bit changes with correct device numbers (hex X) |
| SES-01 | `linkUp` sends the first chunk at once; its bytes equal `McProtocol::encode` of `plan().chunk(0).request` |
| SES-02 | Round 1: no `ValuesChanged` although values are non-zero; snapshots in `DeviceType` order after the last response, then `CycleDone{1}` |
| SES-03 | Round 2: `ValuesChanged` with exactly the changed points, before that type's `Snapshot`; unchanged response → no `ValuesChanged`; one `Snapshot` per type per round |
| SES-04 | PLC error in round 1: the type's snapshot lists the failed chunk with the PLC code; other chunks `Ok`; round completes |
| SES-05 | Chunk failed in round 1, succeeds in round 2 → silent baseline, snapshot shows `Ok` |
| SES-06 | Chunk Ok in round 2, fails in round 3, succeeds in round 4 with a new value → `ValuesChanged` from the last known value |
| SES-07 | Scheduling: FixedRate 100 with 30 ms rounds → starts at 0, 100, 200; with 130 ms rounds → 0, 130, 260 (no burst); FixedDelay; interval 0 |
| SES-08 | `nextDeadline` equals the response deadline while waiting, `nextRoundAt` while idle, `kNoDeadline` when down or faulted |
| SES-09 | Ad-hoc write while idle is sent immediately; `RequestDone` carries its id and empty payload |
| SES-10 | Ad-hoc submitted mid-round goes out after the in-flight response, before the next chunk; with 9 queued and `maxAdHocBurst = 4` the wire order is W1–W4, C1, W5–W8, C2, W9, C3; with `maxAdHocBurst = 0` all nine precede C1; between rounds the queue drains without a cap |
| SES-11 | Ad-hoc read D0×2000 → 3 frames, one `RequestDone` with a 4000-byte payload; bit read honours `BitLayout` |
| SES-12 | Write over the limit without `splitWrites` → `PointCount`, nothing queued, no `RequestDone` |
| SES-13 | `QueueFull` at `adHocCapacity` and when the arena is full; a request larger than the whole arena → `PointCount`; the arena wraps correctly after many submit/complete cycles; `LinkDown` error from `submit` when down or faulted |
| SES-14 | Request ids are unique, increasing, never 0 |
| SES-15 | Exactly-once: `linkDown` with 1 in flight + 3 queued → 4 `RequestDone{LinkDown}` in submission order; no later output mentions them |
| SES-16 | Ethernet timeout: in-flight read → chunk Failed; in-flight write → `RequestDone{Timeout}`; queued → `LinkDown`; `LinkFault{Timeout, reopen}`; nothing sent until `linkDown` + `linkUp`; round 1 restarts |
| SES-17 | Serial timeout: `Send(04)` (F4: `04 0D 0A`); flushing discards bytes, a byte at +40 ms moves the end to +90 ms, bytes every 10 ms end it at the `effectiveTimeoutMs()` cap (counted as a link error); read resent `readRetries` times with identical bytes, then chunk Failed; `maxConsecutiveLinkErrors` → `LinkFault{Timeout, reopen=false}` |
| SES-27 | Serial deadlines: no first byte within `effectiveTimeoutMs()` → timeout; a response trickling one byte every 50 ms for 10 s completes; a response that stops after its first 20 bytes times out `serialInterCharMs` after the 20th byte; Ethernet keeps one overall deadline |
| SES-18 | Serial timeout on a write → EOT, `RequestDone{Timeout}`, never resent |
| SES-19 | Ethernet wrong subheader → `LinkFault{ProtocolError, reopen}`; serial wrong SUM → EOT, read retried and succeeds |
| SES-20 | A PLC error response resets the consecutive-error counter |
| SES-21 | Heartbeat: off by default (never sent); on → first frame of every round writes the heartbeat bit 1, 0, 1, …; no `RequestDone`; failure sets `heartbeatOk=false` only |
| SES-22 | `subscribe` mid-round: current round unchanged; next round includes it; its first read is silent; `unsubscribe` removes points from the next round |
| SES-23 | Every response delivered one byte at a time yields the same output sequence as delivered whole |
| SES-24 | Unsolicited bytes: Ethernet idle → fault; serial idle → discarded; flushing/faulted/down → discarded |
| SES-25 | Drain contract: in release, an input with pending outputs discards them and logs `Error` |
| SES-26 | Clock going backwards is clamped and logged; no deadline fires early |
| ALC-01 | Zero `operator new` calls across rounds 3–10 with changes on every round, heartbeat on, no ad-hoc |
| ALC-02 | Zero allocations across 1000 `submit` + completion cycles of writes and reads, including `linkDown` with a full queue |

Coverage: with `MC_COVERAGE=ON`, line coverage of `src/core/session` ≥ 95 %; every row of the fault table and every `OutputKind` appears in at least one test.

## Boundaries

**Always**

- Keep the engine free of I/O, clocks, sleeps and threads; time enters only through `now`.
- Complete every accepted ad-hoc request exactly once.
- Keep the steady-state round allocation-free (ALC-01 guards it).
- Record every behavioural difference from the old engine under "Divergences".

**Ask first**

- Changing the value-publishing contract (decisions S1–S5), the fault table, or the drain contract.
- Adding an `OutputKind`, a `SessionConfig` field or a `PointState` value (public API).
- Letting ad-hoc reads write into the `ValueStore`.
- Any automatic reconnect or resend on an Ethernet link.

**Never**

- Resend anything on an Ethernet link after a timeout (spec §6.1).
- Retry a write automatically (spec §7.3).
- Report a value the PLC did not send (no zero-fill, no value for a gap point).
- Include a Qt header or call a clock.

## Divergences from the old engine (recorded, intentional)

| Old `McProtocolDevice` | New `Session` | Why |
|---|---|---|
| Timeout retried the same frame up to 5 times on the same TCP socket | Ethernet timeout faults at once; the app reconnects | spec §6.1; decision 7 |
| Send failures and timeouts retried writes too | Writes are never retried | spec §7.3 |
| Timeouts checked only on the polling timer tick | Deadline-driven (`nextDeadline`) | precision; no idle ticks |
| An ad-hoc write after the round waited for the next timer tick | Sent as soon as the link is idle | latency |
| First round: M suppressed, D force-delivered (DR-0013), zero-filled maps | Round 1 silent for every type; one snapshot per type at its end; `NoValue` instead of zero | decisions S1–S5; the zero-fill caused the field fault recorded in DR-0013 |
| `pushRequest` had no completion; `pushTrackedRequest` did | Every `submit` is correlated | exactly-once |
| Bit reads over 8 points turned into word reads inside the 3E codec | `bitsAsWords` planner policy for every frame, aligned to 16 | one policy, in one place |
| Only exactly adjacent ranges merged | Gap merge with `autoGap` | fewer round trips at no byte cost |
| Heartbeat queued when the last read of a round was selected, first value 0 | First frame of every round, first value 1 | deterministic, testable |
| No EOT on serial errors | EOT + flush per spec §6.3 | C24 returns to command wait state |
| `std::map<int, T>` per device area, M and D only | Contiguous segments for every device type | ideas §9.2 lever 3 |

## Changes required in other specs (applied on 2026-09-26)

1. **`core-model`:** add `ErrorCode::NotSubscribed` (category `Config`); answer its open question 2 with "yes" (`operator==`, `operator<` on `Device`), which `RangeSet` and `ValueStore` need; state that `Expected<T>` supports move-only `T` (`Session::create` returns `Expected<Session>`); document that `FrameConfig::readRetries` only acts on serial links (Ethernet timeouts fault, spec §6.1); document that for serial frames `timeoutMs` / `effectiveTimeoutMs()` is the time to the **first byte** of the response, the rest being governed by `SessionConfig::serialInterCharMs`.
2. **`core-protocol`:** none structural. This spec assumes open question 2 of `SPEC-core-protocol.md` is answered as proposed ("raw words"): `Parser::payload()` returns raw words for `ReadWords` on a bit device and `ValueStore::apply` converts them.
3. **`CAPABILITY-MAP.md`:** the `core-session` row gains "snapshot per device type" and the note that `examples/session_loop` links `mc::mock` (built only when both exist).

## Success Criteria

1. Every PLN, STO, SES and ALC test passes on MSVC and on GCC or Clang.
2. The timeline in "Publishing values" is reproduced exactly by SES-02…SES-05.
3. ALC-01 and ALC-02 report zero allocations.
4. No test needs a socket, a thread, a sleep or a real clock.
5. `examples/session_loop` runs against `MockPlc` and prints round-1 snapshots, then changes, with no Qt on the include path.
6. `include/mc/core/{poll_plan,value_store,session}.h` expose no type from `mc::detail` and no template other than `Expected`.

## Open Questions

1. **Ad-hoc allocation.** One allocation per `submit` or a pre-sized arena so writes are allocation-free too. *Decision:* pre-sized FIFO ring arena, `adHocArenaBytes` default 64 KiB; `submit` never allocates (2026-09-26). Spec body updated.
2. **Ad-hoc priority.** Strict priority over polling chunks (as the old engine) or a cap of N ad-hoc frames between two chunks so a write storm cannot starve polling. *Decision:* capped priority, `maxAdHocBurst` default 4, 0 = strict priority; no cap between rounds (2026-09-26). Spec body updated.
3. **`serialFlushMs` default.** The right value depends on baud rate; the spec gives no number. *Decision:* silence-based window: flushing ends after `serialFlushMs` (default 50 ms) without a received byte, every byte restarts it, capped at `effectiveTimeoutMs()` (2026-09-26). Spec body updated.
4. **`bitsAsWords` at the top of a device range.** Aligning the end up to 16 can reach past a PLC's configured range and NAK forever. Proposed: document it and let the app turn `bitsAsWords` off; no automatic fallback in v1. *Decision:* as proposed: document the risk in README and the `PlanOptions::bitsAsWords` Doxygen; global switch only in v1. A per-subscription `ReadMode` override may come in v1.x as an additive overload (2026-09-26).
5. **Defaults** `cycleIntervalMs = 100` (the old `refreshInterval`), `FixedRate`, `maxConsecutiveLinkErrors = 3` (old engine: 5 retries). *Decision:* as proposed: 100 ms, `FixedRate`, 3 (2026-09-26).
6. **Inter-character timeout** (spec §6.3 "recommended"). *Decision:* yes, serial only: `effectiveTimeoutMs()` to the first byte, then `serialInterCharMs` (default 100 ms) after each byte; Ethernet keeps one overall deadline (2026-09-26). Reason: a 960-word 3C ASCII response takes ~4 s at 9600 baud, longer than the 3 s overall default, so the original proposal would time out healthy responses. Spec body updated (SES-27).
