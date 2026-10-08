/**
 * @file resolve.h
 * @brief Resolves a plan against a profile: device references become devices, limit names
 * become counts, values become bytes, and every request is encoded into the frames the run
 * would put on the wire. The safety gate, `--dry-run` and the runner all work on this result.
 */
#pragma once

#include "hil_capture/plan.h"
#include "hil_capture/profile.h"
#include "mc/core/frame_config.h"
#include "mc/core/request.h"
#include "mc/core/types.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>
#include <optional>

namespace mc::hil {

/// @brief A read or write after resolution.
struct ResolvedRequest {
    Op op{Op::ReadWords}; ///< Operation.
    Device head{};        ///< First point.
    uint16_t count{0};    ///< Points for bit operations, words for word operations.
    ByteBuf data;         ///< Write payload in the normalized layout (one byte per bit point).
    bool scratchRelative{false}; ///< The head came from a scratch-relative reference.

    /// @brief Whether the request writes.
    bool isWrite() const { return op == Op::WriteBits || op == Op::WriteWords; }
    /// @brief Number of device numbers it covers (a word of a bit device covers 16).
    uint64_t numbersCovered() const;
};

/// @brief How an operation reaches the wire.
enum class Via : uint8_t { Api, Mutate, Raw };

/// @brief What one frame of an operation asks for (an operation may be split into several).
struct FrameMeta {
    Op op{Op::ReadWords}; ///< Operation of the frame.
    Device head{};        ///< First point.
    uint16_t count{0};    ///< Points or words.
};

/// @brief One operation of a step after resolution.
struct ResolvedOp {
    QString recordId;        ///< "G1-01", "G1-01.2" for the second operation of a step.
    Via via{Via::Api};       ///< Through McDevice, an edited frame, or a literal frame.
    FrameConfig frame;       ///< The effective frame configuration.
    ResolvedRequest request; ///< Api and Mutate; empty for Raw.
    QVector<ByteBuf> frames; ///< Every frame the operation sends, in order.
    QVector<FrameMeta>
        frameMeta;        ///< What each Api or Mutate frame asks for (same order as `frames`).
    QString notSent;      ///< Api: why the library cannot send it (the encode error).
    Expect expect;        ///< The judged outcome.
    QString expectNote;   ///< Why `expect` differs from the plan's (specialFrom); or empty.
    QString mirrors;      ///< Appendix A vectors this mirrors, placeholders substituted.
    QString metaKey;      ///< run.meta key for the first response word.
    bool readOnly{false}; ///< Mutate/Raw: declared read-only in the plan.
    Recover recover{Recover::None}; ///< Mutate/Raw recovery.
    bool scratchMisfit{false};      ///< A scratch-relative request that does not fit one range.
    bool readBack{false};           ///< This operation is the implicit read-back of the one before.
    QString description;            ///< One line for dry-run and error messages.
    QString overrideText; ///< The step's frameOverride as "key=value ..."; empty when none.
};

/// @brief One subscription after resolution.
struct ResolvedSub {
    QString name;                ///< Plan name.
    Device head{};               ///< First point.
    uint32_t count{0};           ///< Points.
    bool input{false};           ///< Read-only points outside scratch.
    bool scratchRelative{false}; ///< The head came from a scratch-relative reference.
};

/// @brief One poll action after resolution.
struct ResolvedAction {
    PollAction::Kind kind{PollAction::Kind::Write}; ///< Which action.
    int after{1};                                   ///< Runs after this round.
    ResolvedOp write;                               ///< Write.
    ResolvedSub sub;                                ///< Subscribe.
    QString name;                                   ///< Unsubscribe.
    QString text;                                   ///< Prompt.
};

/// @brief A poll step after resolution.
struct ResolvedPoll {
    std::optional<Device> heartbeat; ///< Heartbeat bit.
    QVector<ResolvedSub> subs;       ///< Initial subscriptions.
    int rounds{1};                   ///< Rounds.
    std::optional<bool> bitsAsWords; ///< Overrides the session's plan option for this step.
    QVector<ResolvedAction> actions; ///< Between-round actions.
};

/// @brief A bench step after resolution.
struct ResolvedBench {
    bool isPollSet{false}; ///< One repetition is one round of `poll`.
    ResolvedOp request;    ///< The repeated request.
    ResolvedPoll poll;     ///< The subscription set of `pollSet`.
    QString pollSetId;     ///< Id of the poll step.
    int reps{-1};          ///< Repetitions; -1 = command line.
    int warmup{1};         ///< Warm-up repetitions.
};

/// @brief One step after resolution.
struct ResolvedStep {
    QString id;                    ///< Step id (expanded: "<id>-<symbol>").
    QString group;                 ///< Group of the id.
    QString title;                 ///< Free text.
    StepKind kind{StepKind::Read}; ///< Kind.
    QString skipReason;            ///< Non-empty: the step is skipped, with this reason.
    FrameConfig frame;             ///< The effective frame configuration of the step.
    QJsonObject frameOverride;     ///< The step's override, as written.
    QVector<ResolvedOp> ops;       ///< Operations (write, read, mutate, raw; bench keeps its own).
    ResolvedPoll poll;             ///< Poll.
    ResolvedBench bench;           ///< Bench.
    Expect expect;                 ///< Step-level expectation (poll, bench).

    /// @brief Whether the step is skipped.
    bool skipped() const { return !skipReason.isEmpty(); }
};

/// @brief A resolution problem, naming the step.
struct StepError {
    QString stepId;  ///< Step id.
    QString message; ///< What is wrong.
    /// @brief "step: message".
    QString text() const { return stepId + QStringLiteral(": ") + message; }
};

/// @brief The resolved plan, or the errors that stopped it.
struct ResolveResult {
    QString planId;              ///< Plan id.
    QVector<ResolvedStep> steps; ///< Steps in plan order (foreach expanded, groups filtered).
    QVector<StepError> errors;   ///< Empty on success.
    /// @brief Whether every step resolved.
    bool ok() const { return errors.isEmpty(); }
};

/// @brief Resolves @p plan against @p profile.
/// @param[in] plan The loaded plan.
/// @param[in] profile The loaded profile.
/// @param[in] onlyGroups Group ids to keep ("G1", "G2"); empty keeps every step.
ResolveResult resolvePlan(const Plan& plan, const Profile& profile,
                          const QStringList& onlyGroups = QStringList());

/// @brief The FrameConfig of the profile with @p overrideObj applied (the step's override).
/// @param[in] profile The profile.
/// @param[in] overrideObj FrameConfig keys; "+N"/"-N" strings are relative to the profile's value.
/// @param[out] why What is wrong.
std::optional<FrameConfig> applyFrameOverride(const Profile& profile,
                                              const QJsonObject& overrideObj, QString& why);

/// @brief A frameOverride as "key=value key=value" (keys in alphabetical order).
QString overrideTextOf(const QJsonObject& overrideObj);

/// @brief The text of an Error: its message and, for a PLC error, the end code.
QString describeError(const Error& e);

/// @brief The short frame name of a configuration: "3E", "1E", "3C", "1C".
QString frameName(const FrameConfig& f);

/// @brief Whether the frame family has a device code for @p t.
bool frameHasCode(const FrameConfig& f, DeviceType t);

/// @brief Applies the edits of a mutate step to @p frame.
/// @param[in] frame The encoded request.
/// @param[in] cfg The frame configuration it was encoded with.
/// @param[in] edits The edits, in order.
/// @param[out] out The edited bytes.
/// @param[out] why What is wrong.
bool applyEdits(const ByteBuf& frame, const FrameConfig& cfg, const QVector<Edit>& edits,
                ByteBuf& out, QString& why);

/// @brief Lower-case hexadecimal of a frame with spaces ("50 00 ...").
QString hexText(const ByteBuf& bytes);

/// @brief The name of an operation: "ReadBits", "ReadWords", "WriteBits" or "WriteWords".
const char* opName(Op op);

} // namespace mc::hil
