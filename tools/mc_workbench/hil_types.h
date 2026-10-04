/**
 * @file hil_types.h
 * @brief The value types of the HIL runner view that cross between the HIL runner thread and the
 * GUI thread: the check (gate and dry run) result, the run request and result, step outcomes and
 * the answers of the capture helpers.
 *
 * Plain values only; registered as Qt meta types (`registerHilMetaTypes()`).
 */
#pragma once

#include "mc_workbench/capture_types.h"

#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

namespace mc::workbench {

/// @brief What a check or a run is told to load: the same inputs as the `hil_capture` command line.
struct HilCheckInput {
    QString profilePath;                     ///< `--profile`.
    QString planPath;                        ///< `--plan`.
    QStringList only;                        ///< `--only`: step groups; empty keeps every step.
    QString plcState{QStringLiteral("RUN")}; ///< `--plc-state`: RUN or STOP, as the operator set it.
};

/// @brief One frame the gate could not decode and takes the plan's word for (declared read-only).
struct HilGateFrame {
    QString stepId; ///< Operation id.
    QString what;   ///< Description of the frame.
    QString hex;    ///< The bytes.
};

/// @brief What the gate and the dry run say about a profile and a plan.
struct HilCheckResult {
    quint64 token{0}; ///< The token of the check command.
    /// Same numbers as `mc::hil::ExitCode`: 0 the run may go on, 2 bad input (a file or the
    /// resolve step), 3 the safety gate refused the run.
    int exitCode{2};
    QString errorText;        ///< Bad input: what is wrong, one line per problem.
    QString refusalText;      ///< Gate refusal: every offending step, as `hil_capture` prints it.
    QString dryRunText;       ///< The frames the run would send (empty unless the gate passed).
    QString confirmationText; ///< PLC identity, transport, scratch area, read-only frames.
    QString profileId;        ///< The id the operator types to confirm.
    QString planId;           ///< The plan's id.
    int stepCount{0};         ///< Resolved steps (skipped ones included).
    QVector<HilGateFrame> readOnlyFrames; ///< Frames the gate could not decode.
    bool loopbackTcp{false};  ///< The profile talks TCP to this computer (a mock, not a PLC).
    QString digest;           ///< Fingerprint of the dry run and the confirmation text.

    /// @brief Whether the gate passed and the run may be confirmed.
    bool ok() const { return exitCode == 0; }
};

/// @brief A request to run a plan: what to load, what was confirmed, where the capture goes.
struct HilRunRequest {
    HilCheckInput input;                 ///< Profile, plan, groups, PLC state.
    QString expectedDigest;              ///< The digest of the check the operator looked at.
    QString typedId;                     ///< What the operator typed to confirm.
    bool skipTyping{false};              ///< `--yes`: confirm without typing (never for read-only frames).
    QString outputRoot;                  ///< The capture goes to `<outputRoot>/<profile id>/`.
    QString capturedRoot;                ///< `tests/vectors/captured/` of the repository (may be empty).
    CaptureSource source{CaptureSource::MockPlc}; ///< What the run talks to.
    QString note;                        ///< Operator note for `run.meta`.
    int benchReps{200};                  ///< `--bench-reps`.
    int reconnectSeconds{15};            ///< `--reconnect-timeout`.
    bool overwrite{false};               ///< Replace an existing capture folder of the same id.
};

/// @brief How a run request ended.
enum class HilRunStatus : quint8 {
    Finished,      ///< The plan ran to its end (see the counts; the capture is written).
    NotConfirmed,  ///< The confirmation did not hold; nothing was sent.
    GateRefused,   ///< The safety gate refused the run; nothing was sent.
    BadInput,      ///< A file did not load or resolve; nothing was sent.
    OutputRefused, ///< The capture folder is not allowed; nothing was sent.
    Cancelled,     ///< The operator stopped the run between two steps; no capture was written.
    Busy,          ///< A run is already in progress.
    Failed         ///< The run could not start or the capture could not be written.
};

/// @brief One line of the run's output that names a step outcome ("PASS", "FAIL", ...).
struct HilStepLine {
    QString category; ///< PASS, FAIL, DIVERGED, UNSUPPORTED or SKIP.
    QString stepId;   ///< The step id.
    QString text;     ///< What the step did and the note.
};

/// @brief The result of a run request.
struct HilRunResult {
    quint64 token{0};                         ///< The token of the run command.
    HilRunStatus status{HilRunStatus::Failed}; ///< How it ended.
    QString reason;                           ///< Why not Finished (the refusal text, the error).
    int passed{0};                            ///< Steps that matched their expectation.
    int failed{0};                            ///< Steps the tool or the link could not complete.
    int diverged{0};                          ///< Steps whose behaviour differs.
    int notSupported{0};                      ///< Steps answered with a PLC error where ok was expected.
    int skipped{0};                           ///< Steps not run.
    QStringList lines;                        ///< One line per step.
    QString folder;                           ///< The capture folder (Finished).
    QString threadName;                       ///< Name of the thread the run executed on.
    bool onGuiThread{false};                  ///< The run executed on the GUI thread (must stay false).

    /// @brief The process exit code `hil_capture` would end with for this result.
    int exitCode() const;
};

/// @brief The text of a capture file, read on the runner thread.
struct HilFileText {
    quint64 token{0}; ///< The token of the read command.
    QString path;     ///< The file.
    bool ok{false};   ///< The file was read.
    bool truncated{false}; ///< Only the first bytes are in `text`.
    QString text;     ///< The content (or the error message when not ok).
};

/// @brief The outcome of `mc_replay_tests` on a capture root.
struct HilReplayResult {
    quint64 token{0};   ///< The token of the replay command.
    bool started{false}; ///< The program was found and started.
    int exitCode{-1};   ///< Its exit code; 0 is green.
    QString output;     ///< Its console output (bounded).
};

/// @brief The bench report text of a capture root.
struct HilBenchText {
    quint64 token{0}; ///< The token of the command.
    bool ok{false};   ///< A report was built.
    QString text;     ///< The report, or the error message when not ok.
};

/// @brief Registers the types of this header as Qt meta types; safe to call repeatedly.
void registerHilMetaTypes();

} // namespace mc::workbench

Q_DECLARE_METATYPE(mc::workbench::HilCheckInput)
Q_DECLARE_METATYPE(mc::workbench::HilGateFrame)
Q_DECLARE_METATYPE(mc::workbench::HilCheckResult)
Q_DECLARE_METATYPE(mc::workbench::HilRunRequest)
Q_DECLARE_METATYPE(mc::workbench::HilStepLine)
Q_DECLARE_METATYPE(mc::workbench::HilRunResult)
Q_DECLARE_METATYPE(mc::workbench::HilFileText)
Q_DECLARE_METATYPE(mc::workbench::HilReplayResult)
Q_DECLARE_METATYPE(mc::workbench::HilBenchText)
