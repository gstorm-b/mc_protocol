/**
 * @file capture_writer.h
 * @brief The capture files of spec "Output": `steps.vec`, `session.vec`, `run.meta`, `bench.csv`,
 * written in formats the shared loader `tests/common/vectors.h` reads unchanged.
 *
 * **Folder.** `<output root>/<profile id>/`. `prepare()` replaces it: every file the tool writes is
 * removed first; `divergences.txt` is owner-maintained and is kept. The output root defaults to
 * `tests/vectors/captured/`, which is for hardware captures only: a capture made against
 * `virtual_plc` or the mock goes under a folder of the build tree (`--output-root`).
 *
 * **steps.vec.** One request record and (when bytes came back) one response record per frame
 * exchange of an api, mutate or raw step. Ids are `CAP-<profile>-<record id>` and
 * `CAP-<profile>-<record id>-R`; a step's second operation has the record id `<step>.2`, the
 * second frame of an operation that is split into several commands `<id>+2`.
 *
 *     # id: CAP-q03ude-eth-3e-bin-GV-01
 *     # source: plc  profile: q03ude-eth-3e-bin  step: GV-01  mirrors: V-3E-B-01
 *     # frame: 3E  code: Binary  op: ReadWords  device: D100  count: 3  via: api
 *     # kind: request
 *     50 00 00 FF FF 03 00 0C 00 10 00 01 04 00 00 64 00 00 A8 03 00
 *
 *     # id: CAP-q03ude-eth-3e-bin-GV-01-R
 *     # kind: response  of: CAP-q03ude-eth-3e-bin-GV-01
 *     # outcome: ok  expect: words 1995 1202 1130
 *     # ttfb_ms: 2.814  rx_ms: 0.041  rtt_ms: 2.855
 *     D0 00 00 FF FF 03 00 08 00 00 00 95 19 02 12 30 11
 *
 *  - `via` is `api` (a request McDevice sent), `mutate` (an edited frame; `op`, `device`, `count`
 *    describe the request it was built from) or `raw` (a literal frame; `op: Raw`, no device).
 *    Only `api` records are re-encodable from their metadata. A serial frame adds `format: <1-4>`.
 *  - `kind` is `request`, `response` or `response-partial` (a timeout that received part of a
 *    frame). `outcome` and `expect` are on the response record.
 *  - **A `.vec` record needs at least one byte**, so: when nothing came back (timeout, noResponse)
 *    there is no response record and the request record carries `outcome`, `expect` and
 *    `waited_ms`; when nothing was sent (`notSent`: the library refused the request) there is no
 *    record at all and `run.meta` lists it as `notsent.<record id>: <error>`.
 *  - `outcome` is `ok`, `plcError <code hex> [abnormal <hex>] [info net/pc/io/station/cmd/sub in
 *    hex]`, `timeout`, `noResponse`, `protocolError <ErrorCode name>`, `notSent <ErrorCode name>`;
 * a mutate or raw frame whose link broke adds `peerClosed` and `transportError`. A frame sent by
 * the library or the runner outside the operation's own requests (an EOT after a serial error) is
 * its own exchange, `via: raw`, `op: Raw`. `expect` is `ok`, `words <hex> ...`, `bits <0|1> ...`,
 * `bitsOn <index> ...`, `plcError`, `timeout`, `noResponse`, `notSent` or `record`.
 *  - Times are milliseconds with three decimals, from the `RecordingTransport` stamps: `ttfb_ms`
 *    (request written to first byte), `rx_ms` (first to last byte), `rtt_ms` (written to last
 * byte).
 *
 * **session.vec.** The transcript of a poll step: first its wire chunks, then its events; each
 * record carries `step`, `t_ns` (the run's clock, nanoseconds) and `seq` (one counter over chunks
 * and events, so the original order can be rebuilt).
 *
 *     # id: CAP-<profile>-G6-T0001      kind: tx | rx      t_ns: 123456   seq: 1     <the bytes>
 *     # id: CAP-<profile>-G6-E0001      kind: event        event: <name>  t_ns, seq, more keys
 *
 * An event's hex line is a tag byte, then little-endian data (a `.vec` record cannot be empty):
 *  - 01 `snapshot`: per segment head u32, count u32, values (2 bytes per word point, 1 per bit
 *    point), states (1 byte per point, `PointState`). Keys: `type`, `round`, `segments`, `chunks`
 *    (ok|failed|notRead per chunk).
 *  - 02 `valuesChanged`: per change device number u32, old u16, new u16. Keys `type`, `round`.
 *  - 03 `cycleDone`: round u32, startedAt u64, durationMs u32, requests u16, failedChunks u16,
 *    heartbeatOk u8. Keys: `round`, `duration_ms`, `requests`, `failed_chunks`, `heartbeat_ok`.
 *  - 04 `requestFinished`: the payload. Keys `request_id`, `outcome`, `bytes`.
 *  - 05 `linkState`: state u8, reason u8 (the `mc::LinkState` / `mc::LinkReason` numbers). Keys
 *    `state`, `reason`; the endpoint text is left out (it holds an address).
 *  - 06 `linkFault`: kind u8, error code u16, PLC code u16, reopenTransport u8. Keys `fault`
 *    (Timeout or ProtocolError), `error`, `reopen`.
 *
 * **Inputs in session.vec.** What the tool gave the session, so a replay can feed the same inputs:
 * records with `kind: input` (not `event`), the key `input: <name>`, and `t_ns` and `seq` on the
 * same counter as the chunks and events. The hex line is again a tag byte and little-endian data:
 *  - 07 `heartbeat`: enabled u8, device type u8 (`DeviceType`), number u32. Keys `enabled`,
 * `device`. Written once, before the subscriptions, for a poll with a heartbeat.
 *  - 08 `subscribe`: device type u8, head u32, count u32. Keys `name`, `device`, `count`. The
 * initial subscriptions come first (before the link is opened), later ones at their time.
 *  - 09 `unsubscribe`: the subscription name as text. Key `name`.
 *  - 0A `write`: an ad-hoc request submitted while polling: op u8 (`mc::Op`), device type u8, head
 * u32, count u16, then the normalized data bytes. Keys `op`, `device`, `count`.
 *
 * **Keys added to steps.vec records.** `override: key=value ...` on every record (request and
 * response) of a step that ran with a `frameOverride` (the same on its session.vec records); the
 * frame settings of `run.meta` are the profile's, so such a record must be re-encoded with the
 * override. `verdict: passed|failed|diverged|unsupported` on the response record (on the request
 * record when no response record exists), the runner's judgement of the operation against its
 * expectation; a verdict other than `passed` makes the step a finding, not a parse failure. An
 * exchange that got no byte takes the observed outcome (`peerClosed`, `notSent LinkDown`, ...) and
 * only otherwise `timeout` or `noResponse`.
 *
 * **run.meta.** `key: value` lines (no `#`), enough to replay without JSON: `format`, `tool`,
 * `tool_version`, `library_version`, `git_commit`, `date`, `operator_note`, `plc_state` (RUN or
 * STOP, set by the operator), `profile`, `plc`, `module`, `firmware`, `adapter`, `plc_state_note`,
 * `transport` (tcp or serial), `serial.*` line settings (serial only), `scratch`, `device_end`,
 * `supports`, then `frame.<key>` for EVERY FrameConfig field and `session.<key>` for EVERY
 * SessionConfig field, spelled as in the device JSON of `SPEC-qt-device.md` ("Binary", "Ascii",
 * "Format4", "FixedRate", "true", "0"; `session.heartbeat.enabled`, `session.heartbeat.device`).
 * It never holds an IP address, a TCP port or a COM port name: every value is scrubbed of the
 * profile's own host, `:port` and COM name (`<redacted>`, see scrubAddresses()), and a lost link is
 * described by the words of its `LinkReason` and `LinkState`, never by the transport's own text.
 * Then `skipped.<id>`, `notsent.<id>`, `recovery.<n>` (a lost link brought back: after which step,
 * why, how long), `stray.<id>` (bytes that arrived with no request before them), `polls: <ids>`
 * (the poll steps that have a transcript in session.vec, space separated; absent when the run had
 * no poll step) and any other keys the runner adds (e.g. `scan_time_raw`).
 *
 * **bench.csv.** One row per repetition (the warm-up is not written), columns `profile, plc_state,
 * step, op, device, count, req_bytes, resp_bytes, rep, ttfb_ms, rx_ms, rtt_ms, scan_ms`. `scan_ms`
 * is the raw word of the profile's `scanTimeDevice` read before the bench (empty when none is
 * declared). A bench of a poll set (`op` PollRound) has one row per round: `rtt_ms` is
 * `CycleInfo::durationMs`, `ttfb_ms` and `rx_ms` are empty. A pass with `plc_state` STOP
 * (`--plc-state STOP`) does not replace the folder: it removes the old STOP rows of bench.csv,
 * appends its own, appends a `stop_pass.*` block to run.meta and leaves every other file alone (the
 * RUN pass stays). The round-duration series of a polling step are the `duration_ms` keys of its
 * `cycleDone` events in session.vec.
 */
#pragma once

#include "hil_capture/plan.h"
#include "hil_capture/profile.h"
#include "mc/core/result.h"
#include "mc/device/mc_device.h"

#include <QByteArray>
#include <QPair>
#include <QString>
#include <QVector>

#include <limits>

namespace mc::hil {

/// @brief One frame exchange: the request that was sent and what came back.
struct StepRecord {
    QString recordId;                   ///< "G1-01", "G1-01.2", "G1-04+2".
    QString mirrors;                    ///< Appendix A vectors this mirrors; empty when none.
    QString via{QStringLiteral("api")}; ///< "api", "mutate" or "raw".
    QString frame;                      ///< "3E", "1E", "3C", "1C".
    QString code;                       ///< "Binary" or "Ascii".
    int format{0};                      ///< Serial format 1..4; 0 for an Ethernet frame.
    QString op;                         ///< "ReadWords", "WriteBits", ..., or "Raw".
    QString device;                     ///< Head device text; empty for a raw frame.
    int count{0};                       ///< Points or words; 0 when not applicable.
    QByteArray request;                 ///< The bytes written; empty when nothing was sent.
    QByteArray response;                ///< The bytes received; empty when nothing came back.
    bool partial{false};                ///< The response is a part of a frame (a timeout).
    QString outcome;                    ///< See `outcomeText()`.
    QString expect;                     ///< See `expectText()`; empty for none.
    double ttfbMs{std::numeric_limits<double>::quiet_NaN()};   ///< NaN = absent.
    double rxMs{std::numeric_limits<double>::quiet_NaN()};     ///< NaN = absent.
    double rttMs{std::numeric_limits<double>::quiet_NaN()};    ///< NaN = absent.
    double waitedMs{std::numeric_limits<double>::quiet_NaN()}; ///< Request-only records.
    QString verdict; ///< "passed", "failed", "diverged" or "unsupported" (the runner's judgement).
    QString overrideText; ///< The step's frameOverride as `key=value key=value`; empty when none.
};

/// @brief One wire chunk of a poll step.
struct SessionChunk {
    qint64 tNs{0};    ///< Run clock.
    int seq{0};       ///< Position in the transcript.
    bool tx{true};    ///< Written (true) or received (false).
    QByteArray bytes; ///< The bytes.
};

/// @brief One event of a poll step.
struct SessionEvent {
    qint64 tNs{0};     ///< Run clock.
    int seq{0};        ///< Position in the transcript.
    bool input{false}; ///< An input the tool gave the session (`kind: input`), not an event.
    QString name; ///< "snapshot", ..., or for an input "heartbeat", "subscribe", "unsubscribe",
                  ///< "write".
    QVector<QPair<QString, QString>> keys; ///< Readable keys of the event.
    QByteArray payload;                    ///< Tag byte and little-endian data (never empty).
};

/// @brief The transcript of one poll step.
struct SessionCapture {
    QString stepId;               ///< The poll step.
    QVector<SessionChunk> chunks; ///< Wire chunks.
    QVector<SessionEvent> events; ///< Events and inputs, in order of `seq`.
    QString overrideText;         ///< The step's frameOverride; empty when none.
};

/// @brief One repetition of a bench step.
struct BenchRow {
    QString plcState;    ///< RUN or STOP.
    QString step;        ///< Step id.
    QString op;          ///< "ReadWords", ..., or "PollRound".
    QString device;      ///< Head device text, or the poll step id.
    int count{0};        ///< Points or words; subscribed points for a poll set.
    qint64 reqBytes{0};  ///< Bytes written.
    qint64 respBytes{0}; ///< Bytes received.
    int rep{0};          ///< 1-based, warm-up excluded.
    double ttfbMs{std::numeric_limits<double>::quiet_NaN()}; ///< NaN = empty.
    double rxMs{std::numeric_limits<double>::quiet_NaN()};   ///< NaN = empty.
    double rttMs{std::numeric_limits<double>::quiet_NaN()};  ///< NaN = empty.
    QString scanMs;                                          ///< Raw scan time word, or empty.
};

/// @brief What `run.meta` records about one run.
struct RunMeta {
    QString gitCommit{QStringLiteral("unknown")}; ///< Commit of the working tree.
    QString date;                                 ///< ISO 8601 UTC.
    QString operatorNote; ///< Free text; "virtual_plc fixture, not hardware" for such captures.
    QString plcState{QStringLiteral("RUN")}; ///< RUN or STOP.
    Profile profile; ///< The profile (identity, scratch, frame and session settings).
    QVector<QPair<QString, QString>> skipped; ///< Step id and reason.
    QVector<QPair<QString, QString>> notSent; ///< Record id and the library's error.
    QVector<QPair<QString, QString>> extra;   ///< More `key: value` lines.
};

/// @brief The name of an ErrorCode ("PointCount", "FrameMismatch", ...).
QString errorCodeName(ErrorCode code);

/// @brief The `outcome` text of a failed operation: plcError (with abnormal and info) or
/// protocolError; other errors (timeout, link down) are spelled by the caller.
QString outcomeText(const Error& error);

/// @brief The `expect` text of an expectation.
/// @param[in] expect The expectation.
/// @param[in] bitUnit The operation reads bits (values are 0/1) rather than words.
QString expectText(const Expect& expect, bool bitUnit);

/// @brief Event of a snapshot signal.
SessionEvent snapshotEvent(const DeviceSnapshot& snapshot, qint64 tNs);
/// @brief Event of a valuesChanged signal.
SessionEvent changesEvent(DeviceType type, quint32 round, const QVector<Change>& changes,
                          qint64 tNs);
/// @brief Event of a cycleDone signal.
SessionEvent cycleEvent(const CycleInfo& cycle, qint64 tNs);
/// @brief Event of a requestFinished signal.
SessionEvent requestFinishedEvent(RequestId id, const Error& error, const QByteArray& payload,
                                  qint64 tNs);
/// @brief Event of a linkStateChanged signal.
SessionEvent linkStateEvent(LinkState state, LinkReason reason, qint64 tNs);
/// @brief Event of a linkFault signal.
SessionEvent linkFaultEvent(const LinkFaultInfo& fault, qint64 tNs);

/// @brief Input record: the heartbeat of the session (`enabled`, `device`).
SessionEvent heartbeatInput(bool enabled, const Device& device, qint64 tNs);
/// @brief Input record: a subscription made (at the start or between rounds).
SessionEvent subscribeInput(const QString& name, const Device& head, quint32 count, qint64 tNs);
/// @brief Input record: a subscription removed.
SessionEvent unsubscribeInput(const QString& name, qint64 tNs);
/// @brief Input record: an ad-hoc request submitted during the poll (its data in the payload).
SessionEvent writeInput(Op op, const Device& head, quint16 count, const ByteBuf& data, qint64 tNs);

/// @brief The text of steps.vec for @p records (records with no request bytes are skipped).
QString stepsText(const QString& profileId, const QVector<StepRecord>& records);
/// @brief The text of session.vec.
QString sessionText(const QString& profileId, const QVector<SessionCapture>& captures);
/// @brief The text of run.meta. Every value is scrubbed with scrubAddresses(): no host, TCP port
/// or COM name of the profile reaches the file, whichever field it was typed into.
QString runMetaText(const RunMeta& meta);
/// @brief @p text with the profile's own host ("host:port" as a whole, the host alone), ":port" and
/// serial port name each replaced by `<redacted>` (case-insensitive).
QString scrubAddresses(const QString& text, const Profile& profile);
/// @brief The name of a LinkState ("Disconnected", "Connecting", "Connected", "Faulted").
const char* linkStateName(LinkState state);
/// @brief The name of a LinkReason ("Requested", "OpenFailed", "PeerClosed", ...).
const char* linkReasonName(LinkReason reason);
/// @brief The header line of bench.csv (with its newline).
QString benchHeader();
/// @brief The text of bench.csv: the header and one line per row.
QString benchText(const QString& profileId, const QVector<BenchRow>& rows);

/**
 * @class CaptureWriter
 * @brief Writes the capture files of one profile into `<output root>/<profile id>/`.
 */
class CaptureWriter {
  public:
    /// @brief A writer for @p profileId under @p outputRoot (nothing is touched yet).
    CaptureWriter(QString outputRoot, QString profileId);

    /// @brief The folder the files go to.
    QString folder() const;

    /// @brief Replaces the folder: removes what the tool wrote earlier (keeping the owner's
    /// `divergences.txt`) and creates it.
    /// @param[out] error What went wrong.
    /// @param[out] error What went wrong.
    /// @param[in] keepExisting Do not remove anything (a STOP pass).
    bool prepare(QString* error, bool keepExisting = false);
    /// @brief Whether the folder already holds @p name.
    bool has(const QString& name) const;
    /// @brief Writes steps.vec.
    bool writeSteps(const QVector<StepRecord>& records, QString* error) const;
    /// @brief Writes session.vec.
    bool writeSession(const QVector<SessionCapture>& captures, QString* error) const;
    /// @brief Writes run.meta.
    /// @param[in] append Add a `stop_pass.*` block to an existing run.meta instead of replacing it.
    bool writeRunMeta(const RunMeta& meta, QString* error, bool append = false) const;
    /// @brief Writes bench.csv; with @p merge the rows of other states already in the file stay.
    bool writeBench(const QVector<BenchRow>& rows, bool merge, QString* error) const;
    /// @brief Writes bench.csv with its header only.
    bool writeBenchHeader(QString* error) const;

  private:
    bool writeFile(const QString& name, const QString& text, QString* error) const;

    QString m_root;
    QString m_profileId;
};

} // namespace mc::hil
