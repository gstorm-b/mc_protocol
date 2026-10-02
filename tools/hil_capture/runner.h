/**
 * @file runner.h
 * @brief The step runner: executes a resolved plan against one PLC (or `virtual_plc`), judges every
 * outcome against its expectation, recovers from link faults and writes the capture set.
 *
 * - `write` / `read` go through `McDevice::submit` (the full library path); a `then` operation
 *   follows its step, an implicit read-back follows a write that asked for one.
 * - `poll` subscribes its ranges before connecting, runs the rounds, applies its actions between
 *   rounds and records every wire chunk and every `McDevice` signal into `session.vec`.
 * - `mutate` and `raw` write their final bytes on the `RecordingTransport` in raw mode (see
 *   `recording_transport.h`) and read what comes back there; `recover` brings the link back.
 * - `bench` repeats one request (or one round of a poll's subscription set) after a warm-up and
 *   writes one `bench.csv` row per repetition.
 * - A step whose `frameOverride`, poll heartbeat or `bitsAsWords` differs from the configuration
 *   in force makes the runner disconnect, `setConfig()` and reconnect, so every step runs with
 *   exactly its frame.
 * - A link that is not Connected between steps (an `McDevice` `LinkFault`, a peer close) is
 *   reconnected with `connectToPlc()` for up to 15 s; the recovery is recorded in `run.meta` as
 *   `recovery.<n>` and the faulted step keeps its own outcome.
 *
 * Outcomes are counted per step: **passed** (matches the expectation), **failed** (the tool or the
 * link could not do the step: values differ, an unexpected timeout, link loss, a refused request),
 * **diverged** (a valid but different behaviour: an error expected and a normal answer received,
 * silence expected and an answer received, a refusal expected and a frame sent),
 * **not supported** (a PLC error where `ok` was expected) and **skipped**. The exit code is 1 only
 * when a step failed; divergences and unsupported commands are findings, not tool errors.
 *
 * The operator prompt of a poll action is read on a thread, so rounds and deadlines go on
 * meanwhile.
 */
#pragma once

#include "hil_capture/capture_writer.h"
#include "hil_capture/profile.h"
#include "hil_capture/recording_transport.h"
#include "hil_capture/resolve.h"
#include "hil_capture/tool.h"
#include "mc/device/mc_device.h"

#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>

namespace mc::hil {

/// @brief What the runner needs besides the profile and the plan.
struct RunnerSettings {
    QString outputRoot;                      ///< Folder that receives `<profile id>/`.
    QString plcState{QStringLiteral("RUN")}; ///< RUN or STOP, as the operator set it.
    QString operatorNote;                    ///< Recorded in run.meta.
    int benchReps{200};                      ///< Repetitions of every bench step (`--bench-reps`).
    QString gitCommit;                       ///< Empty: found with `git rev-parse` (or "unknown").
    int reconnectBudgetMs{15000}; ///< How long a lost link is retried (`--reconnect-timeout`).
    int pollSlackMs{
        10000}; ///< Added to a poll's budget of rounds x (interval + timeout); tests shorten it.
};

/// @brief The outcome counts of a run.
struct RunSummary {
    int passed{0};       ///< Steps that matched their expectation.
    int failed{0};       ///< Steps the tool or the link could not complete as expected.
    int diverged{0};     ///< Steps whose behaviour differs from the expectation.
    int notSupported{0}; ///< Steps answered with a PLC error where `ok` was expected.
    int skipped{0};      ///< Steps not run (requirements, scratch too small, bench).
    QStringList lines;   ///< One line per step.
    QString folder;      ///< The capture folder.
    QString error;       ///< Set when the run could not start or the capture could not be written.

    /// @brief Whether the run counts as clean (exit code 0).
    bool clean() const { return failed == 0 && error.isEmpty(); }
};

/**
 * @class Runner
 * @brief Runs a resolved plan. One Runner runs once.
 */
class Runner {
  public:
    /// @brief Prepares a run; nothing is connected or written yet.
    Runner(const Profile& profile, const ResolveResult& resolved, RunnerSettings settings,
           const ToolIo& io);
    ~Runner();
    Runner(const Runner&) = delete;
    Runner& operator=(const Runner&) = delete;

    /// @brief Runs every step, writes the capture set and returns the counts. Blocks, spinning
    /// the event loop while it waits for the PLC.
    RunSummary run();

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mc::hil
