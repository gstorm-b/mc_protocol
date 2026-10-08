// session_rx.cpp (spec SPEC-core-session.md, "Publishing values", decisions S1-S5; "Faults,
// retries, EOT", T-026 and T-051): the receive path proper -- bytesIn() drives the current
// chunk's std::optional<Parser> over the caller-fed bytes, and once a response completes, decodes
// it into the ValueStore and emits ValuesChanged/Snapshot exactly per the spec's own worked
// timeline. A failure of the exchange itself -- a response deadline, a Protocol-category parser
// failure, unsolicited bytes while Idle, a receive buffer overflow -- is a link error: on Ethernet
// an immediate LinkFault via Session::faultLink() (session.cpp); on serial an EOT, a Flushing
// state until the line is silent, then a resend of a read or the failure of a write, and a
// LinkFault only after maxConsecutiveLinkErrors in a row (serialAttemptFailed() and the functions
// below it).
#include "mc/core/session.h"

#include "adhoc_queue.h"
#include "output_ring.h"

namespace mc {
namespace {

Error protocolError(ErrorCode code, const char* message) noexcept {
    Error e{};
    e.category = ErrorCategory::Protocol;
    e.code = code;
    e.message = message;
    return e;
}

Error timeoutError(const char* message) noexcept {
    Error e{};
    e.category = ErrorCategory::Transport;
    e.code = ErrorCode::Timeout;
    e.message = message;
    return e;
}

} // namespace

void Session::bytesIn(ByteView bytes, TimeMs now) noexcept {
    checkDrained();
    now = clampNow(now);
    m_stats.bytesReceived += bytes.size;
    const bool serial = m_frameConfig.isSerial();

    if (m_state != State::Waiting) {
        if (m_state == State::Idle) {
            if (serial) {
                // Serial: nothing is in flight, so these are stray bytes (noise, a late tail):
                // "discarded, logged at Warn" (spec fault table, SES-24). Not a link error.
                if (log().enabled(LogLevel::Warn)) {
                    log().write(LogLevel::Warn, "mc.session",
                                "bytesIn discarded: serial line is idle");
                }
                return;
            }
            // Ethernet: "unexpected on a one-request link" (spec fault table, SES-24).
            ++m_stats.protocolErrors;
            faultLink(LinkFaultKind::ProtocolError,
                      protocolError(ErrorCode::FrameMismatch, "unsolicited bytes while idle"), now);
            return;
        }
        if (m_state == State::Flushing && bytes.size > 0) {
            m_flushLastActivityAt = now; // Every discarded byte restarts the silence window.
        }
        // Down/Faulted/Flushing: discarded, logged Trace (spec fault table, SES-24).
        if (log().enabled(LogLevel::Trace)) {
            log().write(LogLevel::Trace, "mc.session", "bytesIn discarded: not Waiting");
        }
        return;
    }

    m_rxBuffer.insert(m_rxBuffer.end(), bytes.data, bytes.data + bytes.size);

    // Receive-buffer overflow (spec fault table): bounded by the largest possible response to
    // whichever request is actually in flight, so a line that never settles into a valid frame
    // cannot grow this buffer forever. bit is a structural stand-in only (Parser/maxResponseSize()
    // never read a Request's data bytes, only op/count/bitLayout -- see Parser's own m_* fields in
    // protocol.cpp), reused for the heartbeat case exactly as sendHeartbeat() itself builds one.
    uint8_t bit = (m_round % 2 == 1) ? 1 : 0;
    Request inFlight = m_currentIsHeartbeat
        ? Request::writeBits(m_config.heartbeat.device, ByteView{&bit, 1})
        : (m_currentIsAdHoc ? m_adHocQueue->nextChunkRequest() : m_plan.chunk(m_currentChunk).request);
    const size_t maxResponse = m_proto.maxResponseSize(inFlight);
    if (!serial && maxResponse > 0 && m_rxBuffer.size() > maxResponse) {
        ++m_stats.protocolErrors;
        faultLink(LinkFaultKind::ProtocolError,
                  protocolError(ErrorCode::LengthMismatch, "receive buffer overflow"), now);
        return;
    }

    ParseStatus status = m_parser->feed(ByteView{m_rxBuffer.data(), m_rxBuffer.size()});
    if (status == ParseStatus::NeedMore) {
        if (serial) {
            // Serial (spec §6.3, 4C-15): bytes before the frame's start byte are junk the parser
            // skips; they never count toward the overflow bound, and while no start byte has
            // been seen they are dropped here so a noisy line cannot grow the buffer. Past the
            // start byte, more than the largest response is the overflow row: discard, EOT, flush.
            const size_t junk = m_parser->skipped();
            if (junk == m_rxBuffer.size()) {
                m_rxBuffer.clear();
                m_parser->reset();
            } else if (maxResponse > 0 && m_rxBuffer.size() - junk > maxResponse) {
                ++m_stats.protocolErrors;
                serialAttemptFailed(
                    LinkFaultKind::ProtocolError,
                    protocolError(ErrorCode::LengthMismatch, "receive buffer overflow"), now);
            } else if (bytes.size > 0) {
                // Spec §6.3 / SES-27: once a response has started (a start byte has been seen;
                // junk alone is not a start), only the gap between bytes matters, so a long
                // response at a low baud rate completes and one cut mid-frame is detected
                // quickly. Until then the first-byte deadline set at the send stays.
                m_responseDeadline = now + m_config.serialInterCharMs;
            }
        }
        return; // Byte-at-a-time delivery (SES-23): every partial call stops here, silently.
    }

    bool ok = (status == ParseStatus::Done);
    Error err = ok ? Error{} : m_parser->error();

    if (!ok && err.category == ErrorCategory::Protocol) {
        // SES-19: e.g. a wrong subheader or SUM. Same treatment as a timeout (spec fault table):
        // "the byte stream cannot be trusted" -- Ethernet always faults, serial sends EOT and
        // flushes (reads are then resent, writes fail). Never routed to any of the three
        // completion functions below.
        ++m_stats.protocolErrors;
        if (serial) {
            serialAttemptFailed(LinkFaultKind::ProtocolError, err, now);
        } else {
            faultLink(LinkFaultKind::ProtocolError, err, now);
        }
        return;
    }
    if (!ok) {
        // The only other Failed cause (ErrorCategory::Plc): "link is healthy; no retry" (spec
        // fault table) -- SES-20 exercises this repeatedly and confirms no LinkFault ever follows.
        ++m_stats.plcErrors;
    }
    // Any well-formed response, a PLC error included, resets the consecutive link error count
    // (spec fault table, SES-20).
    m_consecutiveLinkErrors = 0;

    if (m_currentIsHeartbeat) {
        completeHeartbeatResponse(now, ok);
    } else if (m_currentIsAdHoc) {
        completeAdHocResponse(now, ok, err);
    } else {
        completeCurrentChunk(now, ok, err);
    }
}

void Session::onResponseDeadline(TimeMs now) noexcept {
    ++m_stats.timeouts;
    if (!m_frameConfig.isSerial()) {
        // Ethernet response deadline (spec "Faults, retries, EOT"): no resend on this connection
        // (a late response would be taken for the next request's), so this always faults --
        // never a retry (spec §6.1).
        faultLink(LinkFaultKind::Timeout, timeoutError("response deadline passed"), now);
        return;
    }
    // Serial (spec §6.3): either no byte arrived within effectiveTimeoutMs() of the send, or the
    // response stopped serialInterCharMs after its latest byte (bytesIn() moved the deadline).
    serialAttemptFailed(LinkFaultKind::Timeout,
                        timeoutError(m_rxBuffer.empty()
                                         ? "no response within the timeout"
                                         : "response stopped mid-frame (inter-character timeout)"),
                        now);
}

void Session::serialAttemptFailed(LinkFaultKind kind, Error err, TimeMs now) noexcept {
    ++m_consecutiveLinkErrors;
    // The EOT goes out on every such error, the one that faults included: C24 returns to its
    // command wait state whatever the application does next (spec §6.3).
    sendEot();
    if (m_consecutiveLinkErrors >= m_config.maxConsecutiveLinkErrors) {
        faultLink(kind, err, now);
        return;
    }
    // The in-flight item stays in flight through the flush; endFlush() resolves it (spec: "then a
    // read is resent ...; a write fails with Timeout, never resent").
    m_attemptError = err;
    m_parser.reset();
    m_rxBuffer.clear();
    m_responseDeadline = kNoDeadline;
    m_state = State::Flushing;
    m_flushLastActivityAt = now;
    m_flushCapAt = now + m_frameConfig.effectiveTimeoutMs();
    if (log().enabled(LogLevel::Warn)) {
        log().write(LogLevel::Warn, "mc.session", "serial link error; flushing the line");
    }
}

void Session::onFlushDeadline(TimeMs now) noexcept {
    if (now >= m_flushLastActivityAt + m_config.serialFlushMs) {
        endFlush(now); // The line has been silent for serialFlushMs.
        return;
    }
    if (now >= m_flushCapAt) {
        // The line never went quiet: the window is capped, and that counts as one more link
        // error (spec "States", Flushing row).
        ++m_consecutiveLinkErrors;
        if (m_consecutiveLinkErrors >= m_config.maxConsecutiveLinkErrors) {
            faultLink(LinkFaultKind::Timeout,
                      timeoutError("serial line did not fall silent after EOT"), now);
            return;
        }
        if (log().enabled(LogLevel::Warn)) {
            log().write(LogLevel::Warn, "mc.session", "serial flush reached its cap");
        }
        endFlush(now);
    }
    // Else: a tick before the earlier of the two deadlines; nothing to do.
}

void Session::endFlush(TimeMs now) noexcept {
    // Only reads are resent (spec §7.3): a poll chunk or an ad-hoc read chunk. An ad-hoc write and
    // the heartbeat write fail instead.
    const bool isRead =
        !m_currentIsHeartbeat && (!m_currentIsAdHoc || !m_adHocQueue->nextChunkRequest().isWrite());
    if (isRead && m_retriesUsed < m_frameConfig.readRetries) {
        ++m_retriesUsed;
        ++m_stats.retries;
        if (log().enabled(LogLevel::Warn)) {
            log().write(LogLevel::Warn, "mc.session", "serial read resent");
        }
        resendInFlight(now);
        return;
    }
    const Error err = m_attemptError;
    m_attemptError = Error{};
    if (m_currentIsHeartbeat) {
        completeHeartbeatResponse(now, false);
    } else if (m_currentIsAdHoc) {
        completeAdHocResponse(now, false, err);
    } else {
        completeCurrentChunk(now, false, err);
    }
}

void Session::resendInFlight(TimeMs now) noexcept {
    ByteView frame{};
    Request req{};
    if (m_currentIsAdHoc) {
        frame = m_adHocQueue->inFlightChunkFrame();
        req = m_adHocQueue->nextChunkRequest();
    } else {
        const ByteBuf& encoded = m_encodedChunks[m_currentChunk];
        frame = ByteView{encoded.data(), encoded.size()};
        req = m_plan.chunk(m_currentChunk).request;
    }

    Output out{};
    out.kind = OutputKind::Send;
    out.bytes = frame;
    m_outputRing->push(out);

    m_parser = m_proto.parser(req);
    m_rxBuffer.clear();
    m_responseDeadline = now + m_frameConfig.effectiveTimeoutMs();
    m_state = State::Waiting;

    ++m_stats.framesSent;
    m_stats.bytesSent += frame.size;
    ++m_requestsThisRound;
}

void Session::sendEot() noexcept {
    if (!m_frameConfig.sendEotOnError) {
        return;
    }
    static constexpr uint8_t kEot[] = {0x04};
    static constexpr uint8_t kEotCrLf[] = {0x04, 0x0D, 0x0A};
    const bool crLf = (m_frameConfig.format == SerialFormat::Format4);

    Output out{};
    out.kind = OutputKind::Send;
    out.bytes = crLf ? ByteView{kEotCrLf, sizeof(kEotCrLf)} : ByteView{kEot, sizeof(kEot)};
    m_outputRing->push(out);

    ++m_stats.eotsSent;
    m_stats.bytesSent += out.bytes.size; // framesSent counts request frames; the EOT has eotsSent.
    if (log().enabled(LogLevel::Warn)) {
        log().write(LogLevel::Warn, "mc.session", "EOT sent after a serial link error");
    }
}

void Session::sendAdHocChunk(TimeMs now) noexcept {
    ByteView frame = m_adHocQueue->nextChunkFrame();
    Request chunkReq = m_adHocQueue->nextChunkRequest();

    Output out{};
    out.kind = OutputKind::Send;
    out.bytes = frame;
    m_outputRing->push(out);

    m_currentIsAdHoc = true;
    m_parser = m_proto.parser(chunkReq);
    m_rxBuffer.clear();
    m_responseDeadline = newSendDeadline(now);
    m_state = State::Waiting;
    m_retriesUsed = 0;
    m_adHocQueue->markChunkSent(chunkReq.count);
    ++m_adHocBurstSinceLastPollChunk;

    ++m_stats.framesSent;
    m_stats.bytesSent += frame.size;
    ++m_requestsThisRound;
}

void Session::completeAdHocResponse(TimeMs now, bool ok, Error err) noexcept {
    if (ok) {
        // Ad-hoc reads never touch the ValueStore (spec: "the store reflects polling only"); the
        // decoded bytes go straight into this job's own arena payload slice instead.
        MutableByteView dest = m_adHocQueue->chunkPayloadDest();
        if (dest.size > 0) {
            auto payloadResult = m_parser->payload(ByteView{m_rxBuffer.data(), m_rxBuffer.size()},
                                                    dest);
            if (!payloadResult.hasValue()) {
                ok = false;
                err = payloadResult.error(); // Never expected; see completeCurrentChunk()'s own
                                              // comment on the analogous polling-side defensive
                                              // fallback.
            }
        }
    }

    auto completion = m_adHocQueue->completeInFlightChunk(ok, err);
    m_parser.reset();
    m_rxBuffer.clear();
    m_responseDeadline = kNoDeadline;

    if (completion.has_value()) {
        Output out{};
        out.kind = OutputKind::RequestDone;
        out.requestId = completion->id;
        out.error = completion->error;
        out.payload = completion->payload;
        m_outputRing->push(out);
    }
    // Else: more chunks remain for this same job; dispatch() below sends the next one.

    m_currentIsAdHoc = false;
    m_state = State::Idle;
    dispatch(now);
}

void Session::sendHeartbeat(TimeMs now) noexcept {
    // Round 1, 3, 5, ... writes 1; round 2, 4, ... writes 0 (spec SES-21); tied to m_round's own
    // parity rather than a separate toggle, so linkDown()'s "round 1 restarts" (m_round reset to
    // 0) naturally restarts the heartbeat sequence at 1 too, with no extra reset logic needed.
    bool bitIsOn = (m_round % 2 == 1);
    const ByteBuf& frame = bitIsOn ? m_heartbeatFrameOn : m_heartbeatFrameOff;

    Output out{};
    out.kind = OutputKind::Send;
    out.bytes = ByteView{frame.data(), frame.size()};
    m_outputRing->push(out);

    uint8_t bit = bitIsOn ? 1 : 0;
    Request hbReq = Request::writeBits(m_config.heartbeat.device, ByteView{&bit, 1});
    m_currentIsHeartbeat = true;
    m_parser = m_proto.parser(hbReq); // Parser never reads r.data (protocol.cpp); the local `bit`
                                       // need not outlive this call.
    m_rxBuffer.clear();
    m_responseDeadline = newSendDeadline(now);
    m_state = State::Waiting;
    m_retriesUsed = 0;
    // Deliberately does not touch m_adHocBurstSinceLastPollChunk (dispatch()'s own doc comment:
    // "the heartbeat frame does not count toward this cap either way").

    ++m_stats.framesSent;
    m_stats.bytesSent += frame.size();
    ++m_requestsThisRound;
}

void Session::completeHeartbeatResponse(TimeMs now, bool ok) noexcept {
    // Spec "Ad-hoc requests": "internal ... produces no RequestDone"; its outcome is
    // CycleInfo::heartbeatOk (endRound(), session.cpp) and the log only (SES-21). Reached with
    // a clean response, with a Plc-error response (the PLC refused the write), and on serial from
    // endFlush() after a timeout or protocol error (a write is never resent). On Ethernet such an
    // error faults the link instead (bytesIn()'s Protocol branch, tick()'s deadline check).
    if (!ok) {
        m_heartbeatOkThisRound = false;
        if (log().enabled(LogLevel::Warn)) {
            log().write(LogLevel::Warn, "mc.session", "heartbeat write failed");
        }
    }
    m_parser.reset();
    m_rxBuffer.clear();
    m_responseDeadline = kNoDeadline;
    m_currentIsHeartbeat = false;
    m_state = State::Idle;
    dispatch(now); // Spec "Dispatch" step 3's own "then go to 1": resumes at step 1 (ad-hoc
                    // priority) normally.
}

void Session::emitSnapshot(DeviceType type) noexcept {
    auto range = m_plan.chunksOf(type);
    Output out{};
    out.kind = OutputKind::Snapshot;
    out.deviceType = type;
    out.round = m_round;
    // ReadPlan::chunk() indexes one contiguous std::vector<ChunkInfo> (poll_plan.h), and
    // chunksOf(type) is exactly one contiguous run of it, so this type's whole chunk range is a
    // valid pointer + count pair without copying anything. m_plan is only ever replaced
    // (never touched in place) at a round-boundary re-plan (applyRePlan()); dispatch() defers
    // that specifically while this Snapshot is still queued (T-024's m_pendingPlanRefs), so the
    // pointer stays valid until the caller pops it, as Output's own contract requires.
    out.chunks = &m_plan.chunk(range.first);
    out.chunkCount = range.second - range.first;
    ++m_pendingPlanRefs;
    m_outputRing->push(out);
}

void Session::completeCurrentChunk(TimeMs now, bool ok, Error err) noexcept {
    size_t completedIndex = m_currentChunk;
    ChunkInfo& ci = m_plan.chunkForUpdate(completedIndex);
    DeviceType type = ci.request.head.type;
    size_t changeCount = 0;

    if (ok) {
        ci.state = ChunkState::Ok;
        ci.lastOkRound = m_round;
        ci.lastError = Error{};

        auto payloadResult =
            m_parser->payload(ByteView{m_rxBuffer.data(), m_rxBuffer.size()},
                               MutableByteView{m_payloadBuf.data(), m_payloadBuf.size()});
        if (payloadResult.hasValue()) {
            changeCount = m_values.apply(m_plan, completedIndex,
                                          ByteView{m_payloadBuf.data(), payloadResult.value()},
                                          m_changeBuf.data(), m_changeBuf.size());
        }
        // payloadResult failing here would mean m_payloadBuf (sized from plan().maxPayloadSize()
        // at the last re-plan) disagrees with this exact chunk's own request -- never expected;
        // treated as "no changes" rather than risking a stale/partial decode.
    } else {
        ci.state = ChunkState::Failed;
        ci.lastError = err;
        ++m_failedChunksThisRound;
        m_values.markFailed(m_plan, completedIndex);
        // No retry of a PLC error (spec: "the PLC would refuse again"; link is healthy). `err` is a
        // Plc error from bytesIn(), or on serial the Timeout / Protocol error of the last attempt
        // when endFlush() gives up on a read; on Ethernet bytesIn() routes a Protocol failure to
        // faultLink() instead.
    }

    // Rule 1 (spec "Publishing values"): ValuesChanged is per completing response, from round 2
    // only -- decision S4's silent baseline already makes round 1 report zero changes on its
    // own (every point starts NoValue after linkUp()/resetBaselines()), and this m_round >= 2
    // guard is the spec's own "the rule is also enforced explicitly".
    if (m_round >= 2 && ok && changeCount > 0) {
        Output out{};
        out.kind = OutputKind::ValuesChanged;
        out.deviceType = type;
        out.round = m_round;
        out.changes = m_changeBuf.data();
        out.changeCount = changeCount;
        ++m_pendingPlanRefs; // m_changeBuf is replaced at a re-plan too; see emitSnapshot()'s
                              // own comment (T-024) and dispatch()'s doc comment (session.h).
        m_outputRing->push(out);
    }

    // Rule 2: from round 2, this type's own Snapshot as soon as its last chunk completes. Round
    // 1's snapshots are all held back to endRound() (decision S2; session.cpp).
    auto typeRange = m_plan.chunksOf(type);
    bool lastChunkOfType = (completedIndex + 1 == typeRange.second);
    if (m_round >= 2 && lastChunkOfType) {
        emitSnapshot(type);
    }

    m_parser.reset();
    m_rxBuffer.clear();
    m_responseDeadline = kNoDeadline;

    ++m_currentChunk;
    if (m_currentChunk >= m_plan.size()) {
        endRound(now); // Rule 3: round 1's held-back snapshots, then CycleDone (session.cpp).
    }
    m_state = State::Idle;
    dispatch(now);
}

} // namespace mc
