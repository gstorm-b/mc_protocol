// Session (spec SPEC-core-session.md): create(), subscribe()/unsubscribe() (deferred re-plan at
// the next round boundary, decision S6/decision 10), linkUp()/linkDown(), round scheduling
// (FixedRate/FixedDelay/interval 0), pre-encoded frames, nextDeadline()/nextOutput(). The receive
// path itself (bytesIn(), completeCurrentChunk(), emitSnapshot()) is session_rx.cpp (T-023): this
// file only keeps endRound(), which also owns round 1's own held-back snapshot loop (decision
// S2) since it is scheduling bookkeeping as much as it is publishing.
//
// dispatch()'s re-plan-deferral guard (T-024, m_pendingPlanRefs) fixes a real dangling-pointer
// bug T-023's own Dev notes flagged: a round ending with a re-plan pending, immediately followed
// by the next round starting in the *same* input call (back-to-back scheduling), used to apply
// the re-plan -- replacing m_plan/m_changeBuf -- after that same call had already queued a
// Snapshot/ValuesChanged pointing into them. Regression test: test_session_poll.cpp, "T-024 2a
// regression"; SES-22 and "T-024 2" cover subscribe()/unsubscribe() itself.
//
// T-025 adds the ad-hoc queue/arena (adhoc_queue.h/.cpp): dispatch()'s own step 1
// (maxAdHocBurst priority), submit(), and linkDown()'s "complete every in-flight and queued
// ad-hoc request" loop. Its own dangling-pointer-lifetime rule (a RequestDone's payload stays
// valid only until confirmDrained() runs, itself only called from nextOutput() when popping
// that exact RequestDone) mirrors T-024's m_pendingPlanRefs fix for Snapshot/ValuesChanged --
// see AdHocQueue's own class doc.
//
// T-026 adds the Ethernet column of the fault table (faultLink(), called from tick()'s deadline
// check and session_rx.cpp's bytesIn()), the drain contract (checkDrained(), called first by
// every input method), and the optional heartbeat write (encodeHeartbeatFrames(), dispatch()'s
// own new heartbeat-first branch in its step 3, session_rx.cpp's sendHeartbeat()/
// completeHeartbeatResponse()).
//
// Rework (owner decision, after T-027, before T-028): checkDrained() now keeps the spec's own
// debug/release split literally (debug asserts; release discards + logs at Error) rather than
// always taking the release branch -- reachable through drain_violation.h's replaceable handler
// (notifyDrainViolation(), debug builds only) instead of a bare assert() inline here, so a test
// can install a non-aborting handler and observe both the handler call and the discard+log in the
// very same (debug) build it runs in. See drain_violation.h's own doc comment for the full
// rationale; test_session_fault.cpp's own SES-25 proves both halves.
//
// T-051 adds the serial column of the fault table (EOT, the Flushing state, readRetries,
// serialInterCharMs / serialFlushMs, maxConsecutiveLinkErrors): the logic lives in
// session_rx.cpp (onResponseDeadline(), serialAttemptFailed(), onFlushDeadline(), endFlush());
// this file keeps the state-machine plumbing it touches -- tick()'s dispatch to them, faultLink()
// (the one place a link fault is reported, now for both families), nextDeadline() and linkDown()'s
// reset.
#include "mc/core/session.h"

#include "mc/core/limits.h"
#include "adhoc_queue.h"
#include "drain_violation.h"
#include "output_ring.h"
#include "word_align.h"

#include <algorithm>
#include <utility>

namespace mc {
namespace {

Error invalidConfigError(const char* message) noexcept {
    Error e{};
    e.category = ErrorCategory::Config;
    e.code = ErrorCode::InvalidConfig;
    e.message = message;
    return e;
}

Error linkDownError(const char* message) noexcept {
    Error e{};
    e.category = ErrorCategory::Transport;
    e.code = ErrorCode::LinkDown;
    e.message = message;
    return e;
}

// Spec "Drain contract": sized for the worst single input call -- adHocCapacity + 1 completions
// on linkDown(), one Snapshot per device type at the end of round 1, plus a constant.
size_t ringCapacityFor(const SessionConfig& cfg) noexcept {
    return static_cast<size_t>(cfg.adHocCapacity) + 1 + static_cast<size_t>(DeviceType::Count) + 1;
}

} // namespace

Expected<void> SessionConfig::validate(const FrameConfig& frame) const noexcept {
    if (maxConsecutiveLinkErrors == 0) {
        return Expected<void>(invalidConfigError("maxConsecutiveLinkErrors must be at least 1"));
    }
    if (firstResponseTimeoutMs != 0 && firstResponseTimeoutMs < frame.effectiveTimeoutMs()) {
        return Expected<void>(invalidConfigError(
            "firstResponseTimeoutMs must be 0 or at least effectiveTimeoutMs()"));
    }
    if (heartbeat.enabled) {
        if (deviceInfo(heartbeat.device.type).kind != DeviceKind::Bit) {
            return Expected<void>(
                invalidConfigError("heartbeat.device must be a bit device"));
        }
        uint8_t one[1] = {1};
        Request hb = Request::writeBits(heartbeat.device, ByteView{one, 1});
        auto v = mc::validate(hb, frame);
        if (!v.hasValue()) {
            return v;
        }
    }
    return Expected<void>();
}

Session::Session(const FrameConfig& frame, const SessionConfig& cfg, ReadPlan plan)
    : m_frameConfig(frame), m_config(cfg), m_proto(frame), m_plan(std::move(plan)),
      m_outputRing(std::make_unique<detail::OutputRing>(ringCapacityFor(cfg))),
      m_adHocQueue(std::make_unique<detail::AdHocQueue>(frame, cfg.adHocCapacity,
                                                         cfg.adHocArenaBytes)) {
    m_values.rebuild(m_subs); // m_subs is empty at construction: an empty, matching store.
    encodeAllChunks();        // Trivial (0 chunks) for the initial empty plan.
    encodeHeartbeatFrames();  // Once only: the heartbeat device never changes at a re-plan.
}

Session::Session(Session&&) noexcept = default;
Session& Session::operator=(Session&&) noexcept = default;
Session::~Session() = default;

Expected<Session> Session::create(const FrameConfig& frame, const SessionConfig& cfg) {
    auto frameOk = frame.validate();
    if (!frameOk.hasValue()) {
        return Expected<Session>(frameOk.error());
    }
    auto cfgOk = cfg.validate(frame);
    if (!cfgOk.hasValue()) {
        return Expected<Session>(cfgOk.error());
    }

    RangeSet emptySubs;
    auto planResult = ReadPlan::build(emptySubs, frame, cfg.plan);
    if (!planResult.hasValue()) {
        return Expected<Session>(planResult.error());
    }

    return Expected<Session>(Session(frame, cfg, std::move(planResult.value())));
}

TimeMs Session::clampNow(TimeMs now) noexcept {
    if (now < m_lastNow) {
        if (log().enabled(LogLevel::Warn)) {
            log().write(LogLevel::Warn, "mc.session", "now went backwards; clamped");
        }
        now = m_lastNow;
    }
    m_lastNow = now;
    return now;
}

LogSink& Session::log() const noexcept {
    static NullLogSink nullSink;
    return m_config.log != nullptr ? *m_config.log : static_cast<LogSink&>(nullSink);
}

void Session::checkDrained() noexcept {
    if (m_outputRing->empty()) {
        return;
    }
    // Precondition violation (spec "Drain contract"). notifyDrainViolation() (drain_violation.h)
    // is debug-only (a no-op when NDEBUG is defined) and calls whichever handler is currently
    // installed -- the default asserts; a test can install a recording one instead. Either way,
    // discarding the pending outputs and logging at Error below always runs afterward, in both
    // builds (the spec's own "release" behaviour) -- see this method's own doc comment in
    // session.h and drain_violation.h's own doc comment for the full rationale.
    detail::notifyDrainViolation();
    if (log().enabled(LogLevel::Error)) {
        log().write(LogLevel::Error, "mc.session",
                     "input call with outputs still pending; discarded (drain contract)");
    }
    // Rework (phase review blocker): every discarded Output must still release whatever
    // per-kind bookkeeping nextOutput() would have (m_pendingPlanRefs for a Snapshot/
    // ValuesChanged, T-024; AdHocQueue::confirmDrained() for a RequestDone, T-025) -- a raw
    // m_outputRing->pop() loop here bypassed both, permanently: the former hangs dispatch()'s own
    // round-start guard forever once a re-plan is pending; the latter leaks that job's own
    // arena/job-ring slot, cumulatively, until adHocCapacity is exhausted. nextOutput() is the one
    // routine that already does this release correctly for every OutputKind -- discarding through
    // it, rather than the ring directly, keeps there being exactly one place that logic lives.
    Output discarded;
    while (nextOutput(discarded)) {
        // Discarded; views inside it (if any) are not read again.
    }
}

Expected<SubscriptionId> Session::subscribe(Device head, uint32_t count) {
    checkDrained();
    auto added = m_subs.add(head, count);
    if (!added.hasValue()) {
        return Expected<SubscriptionId>(added.error());
    }

    // Immediate frame-level check (spec: "checked immediately against the frame, InvalidDevice /
    // PointCount"), independent of the deferred re-plan (decision S6): the same read op
    // ReadPlan::build() would use for this device kind, validated right away so a bad
    // subscription is rejected here rather than only surfacing indirectly at the next round
    // boundary.
    const DeviceInfo& info = deviceInfo(head.type);
    bool bitsAsWords = m_config.plan.bitsAsWords && info.kind == DeviceKind::Bit;
    Op op = (info.kind == DeviceKind::Bit && !bitsAsWords) ? Op::ReadBits : Op::ReadWords;
    // With bitsAsWords the plan reads the 16-point-aligned word range that covers the subscription
    // (ReadPlan::build), so that is what is checked, not the raw bit head and count.
    uint64_t checkUnits = count;
    if (bitsAsWords) {
        const detail::Interval aligned =
            detail::alignToWordBoundary(head.type, head.number, count, m_frameConfig);
        head.number = static_cast<uint32_t>(aligned.start);
        checkUnits = (aligned.end - aligned.start) / 16;
    }
    uint16_t checkCount = static_cast<uint16_t>(checkUnits > 0xFFFFu ? 0xFFFFu : checkUnits);
    Request checkReq = (op == Op::ReadWords) ? Request::readWords(head, checkCount)
                                              : Request::readBits(head, checkCount);
    auto checked = chunkCount(checkReq, m_frameConfig);
    if (!checked.hasValue()) {
        (void)m_subs.remove(added.value()); // subscribe() fails atomically: undo the add().
        return Expected<SubscriptionId>(checked.error());
    }

    m_rePlanPending = true;
    return Expected<SubscriptionId>(added.value());
}

Expected<void> Session::unsubscribe(SubscriptionId id) {
    checkDrained();
    auto removed = m_subs.remove(id);
    if (!removed.hasValue()) {
        return removed;
    }
    m_rePlanPending = true;
    return Expected<void>();
}

void Session::linkUp(TimeMs now) noexcept {
    checkDrained();
    now = clampNow(now);
    if (m_state != State::Down) {
        if (log().enabled(LogLevel::Warn)) {
            log().write(LogLevel::Warn, "mc.session", "linkUp ignored: already up or faulted");
        }
        return;
    }
    if (log().enabled(LogLevel::Info)) {
        log().write(LogLevel::Info, "mc.session", "link up");
    }
    m_values.resetBaselines();
    m_state = State::Idle;
    m_firstSendPending = true;
    m_nextRoundAt = now; // Round 1 starts immediately.
    dispatch(now);
}

void Session::linkDown(TimeMs now) noexcept {
    checkDrained();
    now = clampNow(now);
    if (log().enabled(LogLevel::Info)) {
        log().write(LogLevel::Info, "mc.session", "link down");
    }

    // "Completes every in-flight and queued ad-hoc request with Error{Transport, LinkDown}, in
    // submission order" (spec): one RequestDone per not-yet-completed job, oldest first.
    Error down = linkDownError("link is down");
    detail::AdHocCompletion completion{};
    while (m_adHocQueue->completeAllForLinkDown(down, completion)) {
        Output out{};
        out.kind = OutputKind::RequestDone;
        out.requestId = completion.id;
        out.error = completion.error;
        out.payload = completion.payload; // Always empty here: LinkDown is always a failure.
        m_outputRing->push(out);
    }

    m_values.markStale();
    m_state = State::Down; // Also clears Faulted (spec "States": Faulted leaves on linkDown()
                            // only).
    m_round = 0;            // SES-16: "round 1 restarts" -- the next linkUp() begins a fresh
                             // round 1, not a continuation of whatever round was running (or
                             // aborted by a fault) before this linkDown().
    m_roundInProgress = false;
    m_currentIsAdHoc = false;
    m_currentIsHeartbeat = false;
    m_adHocBurstSinceLastPollChunk = 0;
    m_firstSendPending = false;
    m_parser.reset();
    m_rxBuffer.clear();
    m_responseDeadline = kNoDeadline;
    m_consecutiveLinkErrors = 0;
    m_retriesUsed = 0;
    m_attemptError = Error{};
}

void Session::faultLink(LinkFaultKind kind, Error err, TimeMs /*now*/) noexcept {
    // now is unused: a fault stops all timers (m_responseDeadline -> kNoDeadline below) rather
    // than scheduling one. Kept as a parameter for symmetry with every other dispatch-adjacent
    // function here (sendChunk, sendAdHocChunk, ...), all of which take the caller's current time.

    // Serial only: a fault while Flushing (the flush reached its cap) fails the in-flight item
    // with the error of the attempt that began the flush, not with the flush's own error.
    const bool flushing = (m_state == State::Flushing);
    const Error itemError = flushing ? m_attemptError : err;
    if (m_state == State::Waiting || flushing) {
        if (m_currentIsHeartbeat) {
            // Internal (spec "Ad-hoc requests": no RequestDone, ever): this round is abandoned
            // by the fault below, not completed, so heartbeatOkThisRound has no CycleDone left to
            // report to -- set anyway, for the same "consistent bookkeeping" reason
            // completeHeartbeatResponse() does on an ordinary Plc-error failure.
            m_heartbeatOkThisRound = false;
        } else if (m_currentIsAdHoc) {
            // SES-16: "in-flight write -> RequestDone{Timeout}" (a read behaves the same: any
            // chunk failing completes the whole job, per "Ad-hoc requests"' own "first failed
            // chunk" rule). completeInFlightChunk() marks the job completed; it is always the
            // FIFO-oldest one, so the completeAllForLinkDown() loop below picks up correctly from
            // the next-oldest queued job, preserving submission order end to end.
            auto completion = m_adHocQueue->completeInFlightChunk(false, itemError);
            if (completion.has_value()) {
                Output out{};
                out.kind = OutputKind::RequestDone;
                out.requestId = completion->id;
                out.error = completion->error;
                out.payload = completion->payload; // Always empty: a failure never carries one.
                m_outputRing->push(out);
            }
        } else {
            // SES-16: "in-flight read -> chunk Failed" -- same ChunkInfo/ValueStore marking
            // completeCurrentChunk()'s own ok=false branch uses, but no Snapshot/CycleDone: the
            // round is abandoned by this fault, not completed (nothing is sent again until
            // linkDown() + linkUp(); the application learns about it from LinkFault itself, not
            // from a synthesized partial round summary).
            ChunkInfo& ci = m_plan.chunkForUpdate(m_currentChunk);
            ci.state = ChunkState::Failed;
            ci.lastError = itemError;
            m_values.markFailed(m_plan, m_currentChunk);
        }
    }

    // Every OTHER still-queued ad-hoc job (the in-flight one, if any, is already completed above
    // and so already skipped by frontUnfinished()) completes with LinkDown, submission order --
    // same as linkDown()'s own loop above, and for the same reason (T-025's exactly-once rule).
    Error down = linkDownError("link is down");
    detail::AdHocCompletion completion{};
    while (m_adHocQueue->completeAllForLinkDown(down, completion)) {
        Output out{};
        out.kind = OutputKind::RequestDone;
        out.requestId = completion.id;
        out.error = completion.error;
        out.payload = completion.payload;
        m_outputRing->push(out);
    }

    m_parser.reset();
    m_rxBuffer.clear();
    m_responseDeadline = kNoDeadline;
    m_state = State::Faulted;
    m_roundInProgress = false;
    m_currentIsAdHoc = false;
    m_currentIsHeartbeat = false;
    m_adHocBurstSinceLastPollChunk = 0;
    m_retriesUsed = 0;
    m_attemptError = Error{};

    if (log().enabled(LogLevel::Error)) {
        log().write(LogLevel::Error, "mc.session", "link fault");
    }

    Output out{};
    out.kind = OutputKind::LinkFault;
    out.fault = kind;
    out.error = err;
    // Spec fault table: Ethernet always reopens (a late response would be taken for the next
    // request's); serial never does -- the line itself is not broken, only this exchange. Decision
    // S7 leaves the choice of what to actually do about it to the application either way.
    out.reopenTransport = !m_frameConfig.isSerial();
    m_outputRing->push(out);
}

Expected<RequestId> Session::submit(const Request& r, TimeMs now) {
    checkDrained();
    now = clampNow(now);
    auto checked = chunkCount(r, m_frameConfig);
    if (!checked.hasValue()) {
        return Expected<RequestId>(checked.error());
    }
    if (!isLinkUp()) {
        return Expected<RequestId>(linkDownError("link is not up"));
    }
    auto submitted = m_adHocQueue->submit(r);
    if (!submitted.hasValue()) {
        return submitted;
    }
    // SES-09: "ad-hoc write while idle is sent immediately" -- if nothing was already in flight,
    // this new job may be sendable right now. If something IS in flight (Waiting), it is picked
    // up naturally once that response completes; nextDeadline() is unaffected either way
    // (submit() is not itself a scheduling event).
    if (m_state == State::Idle) {
        dispatch(now);
    }
    return submitted;
}

void Session::tick(TimeMs now) noexcept {
    checkDrained();
    now = clampNow(now);
    if (m_state == State::Waiting && now >= m_responseDeadline) {
        onResponseDeadline(now);
        return;
    }
    if (m_state == State::Flushing) {
        onFlushDeadline(now);
        return;
    }
    if (m_state == State::Idle) {
        dispatch(now);
    }
    // Faulted/Down: nothing to do.
}

void Session::dispatch(TimeMs now) noexcept {
    if (m_state != State::Idle) {
        return;
    }
    // Step 1 (spec "Dispatch"): ad-hoc has priority over polling, capped by maxAdHocBurst (0 =
    // no cap, strict priority) while a round is in progress; uncapped between rounds. The
    // heartbeat write does not count toward this cap either way (T-026: sendHeartbeat() never
    // touches m_adHocBurstSinceLastPollChunk); it also always goes out before this check ever
    // runs for a round it starts (see step 3 below), so it is never itself subject to the cap.
    bool burstAllows = !m_roundInProgress || m_config.maxAdHocBurst == 0 ||
                        m_adHocBurstSinceLastPollChunk < m_config.maxAdHocBurst;
    if (m_adHocQueue->hasWork() && burstAllows) {
        sendAdHocChunk(now);
        return;
    }
    if (m_roundInProgress) {
        if (m_currentChunk < m_plan.size()) {
            // completeCurrentChunk() only leaves m_roundInProgress true when more chunks remain.
            sendChunk(m_currentChunk, now);
        } else {
            // A heartbeat-only round (T-026): its own completion re-enters dispatch() here with
            // no polling chunk ever having been the "current" one (m_currentChunk is still the 0
            // startRound() reset it to, but m_plan is empty). Every other path already ends the
            // round itself (completeCurrentChunk() calls endRound() once m_currentChunk reaches
            // m_plan.size(), which clears m_roundInProgress before returning here), so this branch
            // is unreachable except for that one case; the guard keeps it total regardless.
            endRound(now);
        }
        return;
    }
    if (now >= m_nextRoundAt) {
        if (m_rePlanPending && m_pendingPlanRefs > 0) {
            // T-024 (dangling-pointer finding from T-023): applying the pending re-plan right
            // now would replace m_plan/m_changeBuf out from under a Snapshot/ValuesChanged this
            // same call already queued (back-to-back scheduling, e.g. cycleIntervalMs == 0, can
            // reach this in the very call that just ended the previous round). Stay Idle;
            // m_nextRoundAt is already <= now, so nextDeadline() still reports "due", and the
            // caller's own next call -- after draining, per the drain contract -- retries this
            // with m_pendingPlanRefs back at 0.
            return;
        }
        startRound(now);
        if (m_config.heartbeat.enabled) {
            // Spec "Dispatch" step 3: "put the heartbeat write in front, then go to 1" -- sent
            // unconditionally ahead of even ad-hoc priority; completeHeartbeatResponse() is what
            // actually "goes to 1" (calls dispatch() again, re-entering step 1 normally).
            sendHeartbeat(now);
            return;
        }
        if (m_plan.size() > 0) {
            sendChunk(0, now);
        } else {
            // "A round with no chunks still runs ... and still emits CycleDone" (spec
            // "Dispatch"). Not recursed into another dispatch() call: see this function's own
            // doc comment in session.h for why (bounded output per input call).
            endRound(now);
        }
    }
    // Else: stay Idle; nextDeadline() reports m_nextRoundAt.
}

void Session::startRound(TimeMs now) {
    if (m_rePlanPending) {
        applyRePlan();
        m_rePlanPending = false;
    }
    ++m_round;
    m_roundStartedAt = now;
    m_roundInProgress = true;
    m_currentChunk = 0;
    m_requestsThisRound = 0;
    m_failedChunksThisRound = 0;
    m_heartbeatOkThisRound = true; // Reset regardless of m_config.heartbeat.enabled; endRound()
                                    // only reads it when enabled (disabled always reports true).
}

void Session::applyRePlan() {
    auto planResult = ReadPlan::build(m_subs, m_frameConfig, m_config.plan);
    if (!planResult.hasValue()) {
        // ReadPlan::build()'s own contract: a failure keeps the previous plan untouched. There
        // is still no defined recovery/reporting path for this back to the application (e.g. an
        // Output naming the bad subscription) -- T-024 fixed the dangling-pointer hazard around
        // re-plan timing but did not add one; the previous m_plan/m_values/m_encodedChunks simply
        // keep running unchanged, silently, until whoever removes the offending subscription.
        if (log().enabled(LogLevel::Error)) {
            log().write(LogLevel::Error, "mc.session", "re-plan failed; keeping the previous plan");
        }
        return;
    }
    m_plan = std::move(planResult.value());
    m_values.rebuild(m_subs);
    encodeAllChunks();
}

void Session::encodeAllChunks() {
    m_encodedChunks.clear();
    m_encodedChunks.reserve(m_plan.size());
    for (size_t i = 0; i < m_plan.size(); ++i) {
        auto encoded = m_proto.encode(m_plan.chunk(i).request);
        // ReadPlan::build() already validated every chunk against this exact frame; encode()
        // failing here would mean the two disagree. Defensive fallback (never expected to be
        // exercised): an empty frame rather than leaving m_encodedChunks short, since sendChunk()
        // indexes it positionally alongside m_plan's own chunks.
        m_encodedChunks.push_back(encoded.hasValue() ? std::move(encoded.value()) : ByteBuf{});
    }

    // completeCurrentChunk() (session_rx.cpp) decodes into these; both are sized from the plan
    // that is about to become current, never the one it replaces.
    m_payloadBuf.assign(m_plan.maxPayloadSize(), uint8_t{0});
    m_changeBuf.assign(m_plan.maxChunkPoints(), Change{});
}

void Session::encodeHeartbeatFrames() {
    if (!m_config.heartbeat.enabled) {
        return;
    }
    uint8_t one = 1;
    uint8_t zero = 0;
    Request hbOn = Request::writeBits(m_config.heartbeat.device, ByteView{&one, 1});
    Request hbOff = Request::writeBits(m_config.heartbeat.device, ByteView{&zero, 1});
    auto on = m_proto.encode(hbOn);
    auto off = m_proto.encode(hbOff);
    // SessionConfig::validate() already confirmed this device/frame combination validates at
    // create() time; encode() failing here would mean the two disagree (never expected -- same
    // defensive fallback as encodeAllChunks() above).
    m_heartbeatFrameOn = on.hasValue() ? std::move(on.value()) : ByteBuf{};
    m_heartbeatFrameOff = off.hasValue() ? std::move(off.value()) : ByteBuf{};
}

TimeMs Session::newSendDeadline(TimeMs now) noexcept {
    uint32_t wait = m_frameConfig.effectiveTimeoutMs();
    if (m_firstSendPending) {
        m_firstSendPending = false;
        if (m_config.firstResponseTimeoutMs != 0) {
            wait = m_config.firstResponseTimeoutMs;
        }
    }
    return now + wait;
}

void Session::sendChunk(size_t index, TimeMs now) noexcept {
    const ByteBuf& frame = m_encodedChunks[index];
    Output out{};
    out.kind = OutputKind::Send;
    out.bytes = ByteView{frame.data(), frame.size()};
    m_outputRing->push(out);

    m_currentChunk = index;
    m_currentIsAdHoc = false;
    m_parser = m_proto.parser(m_plan.chunk(index).request);
    m_rxBuffer.clear();
    m_responseDeadline = newSendDeadline(now);
    m_state = State::Waiting;
    m_retriesUsed = 0;
    m_adHocBurstSinceLastPollChunk = 0; // Spec dispatch step 2: sending a polling chunk resets it.

    ++m_stats.framesSent;
    m_stats.bytesSent += frame.size();
    ++m_requestsThisRound;
}

void Session::endRound(TimeMs now) noexcept {
    if (m_round == 1) {
        // Decision S2: round 1's snapshots are all held back and published together here,
        // rather than per-type as completeCurrentChunk() (session_rx.cpp) goes -- that function
        // only emits a type's Snapshot "from round 2" (spec "Publishing values" rule 2).
        for (uint16_t t = 0; t < static_cast<uint16_t>(DeviceType::Count); ++t) {
            DeviceType type = static_cast<DeviceType>(t);
            auto range = m_plan.chunksOf(type);
            if (range.second > range.first) {
                emitSnapshot(type);
            }
        }
    }

    CycleInfo cycle{};
    cycle.round = m_round;
    cycle.startedAt = m_roundStartedAt;
    cycle.durationMs = static_cast<uint32_t>(now - m_roundStartedAt);
    cycle.requests = m_requestsThisRound;
    cycle.failedChunks = m_failedChunksThisRound;
    // Spec CycleInfo::heartbeatOk: "true when disabled" -- m_heartbeatOkThisRound is otherwise
    // meaningless (startRound() resets it every round regardless, but nothing ever sets it false
    // unless a heartbeat write actually failed, which cannot happen when heartbeat is off).
    cycle.heartbeatOk = m_config.heartbeat.enabled ? m_heartbeatOkThisRound : true;

    Output out{};
    out.kind = OutputKind::CycleDone;
    out.cycle = cycle;
    m_outputRing->push(out);
    ++m_stats.rounds;

    TimeMs end = now;
    if (m_config.cycleMode == CycleMode::FixedRate) {
        TimeMs candidate = m_roundStartedAt + m_config.cycleIntervalMs;
        m_nextRoundAt = candidate > end ? candidate : end;
    } else {
        m_nextRoundAt = end + m_config.cycleIntervalMs;
    }
    m_roundInProgress = false;
}

bool Session::nextOutput(Output& out) noexcept {
    bool got = m_outputRing->pop(out);
    if (got && (out.kind == OutputKind::Snapshot || out.kind == OutputKind::ValuesChanged)) {
        --m_pendingPlanRefs; // See dispatch()'s own doc comment (T-024).
    }
    if (got && out.kind == OutputKind::RequestDone) {
        // AdHocQueue's own T-025 lifetime rule (mirrors T-024's m_pendingPlanRefs): a
        // RequestDone's payload points into the arena reservation confirmDrained() now frees,
        // so it must not run any earlier than this -- exactly the point the caller has this
        // Output's own copy of `payload` in hand, per Output's own "valid until the next input
        // call" contract (a following nextOutput() call is not an input call).
        m_adHocQueue->confirmDrained();
    }
    return got;
}

TimeMs Session::nextDeadline() const noexcept {
    switch (m_state) {
    case State::Down:
    case State::Faulted:
        return kNoDeadline;
    case State::Waiting:
        return m_responseDeadline;
    case State::Idle:
        return m_nextRoundAt;
    case State::Flushing: {
        // Serial: the silence window (restarted by every discarded byte), but never past the cap.
        const TimeMs silentAt = m_flushLastActivityAt + m_config.serialFlushMs;
        return silentAt < m_flushCapAt ? silentAt : m_flushCapAt;
    }
    }
    return kNoDeadline;
}

const ValueStore& Session::values() const noexcept { return m_values; }

const ReadPlan& Session::plan() const noexcept { return m_plan; }

const SessionStats& Session::stats() const noexcept { return m_stats; }

bool Session::isLinkUp() const noexcept {
    return m_state == State::Idle || m_state == State::Waiting || m_state == State::Flushing;
}

bool Session::isFaulted() const noexcept { return m_state == State::Faulted; }

} // namespace mc
