/**
 * @file hil_runner.h
 * @brief `HilRunner` (lives on a runner thread): checks a profile and a plan through the
 * `mc_hil_tool` gate, runs the plan with the `mc_hil_tool` `Runner`, and serves the capture
 * helpers (files, replay, bench report). `HilControl` is the thread-safe side channel of a run:
 * cancel and the operator's answers.
 */
#pragma once

#include "mc_workbench/hil_types.h"
#include "mc_workbench/runner_base.h"

#include <QProcess>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace mc::workbench {

/**
 * @brief What the GUI thread may do to a run in progress without waiting for the runner thread.
 *
 * The runner thread is busy inside `Runner::run()` while a plan runs, so a stop request or an
 * answer to an operator prompt cannot queue behind it: both go through this object, which holds
 * a mutex and a flag, nothing else. One object per `HilHost`.
 *
 * @note Thread safe.
 */
class HilControl {
public:
    /// @brief Clears the flags; the host calls it before it posts a run.
    void reset();

    /// @brief Asks the run to end at the next step boundary and releases a waiting prompt.
    void requestCancel();

    /// @brief Whether a cancel was requested since reset().
    /// @return true after requestCancel().
    bool cancelRequested() const { return m_cancel.load(); }

    /// @brief Answers the operator prompt the run is waiting for ("press Enter").
    void answerPrompt();

    /**
     * @brief Blocks the calling thread until the prompt is answered or the run is cancelled.
     * @return true when answered; false when cancelled.
     */
    bool waitForAnswer();

private:
    std::atomic<bool> m_cancel{false};
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_answered{false};
};

/**
 * @brief The HIL run, on its runner thread.
 *
 * Every entry point runs through `RunnerBase::guarded` (the `RunnerThread` does that for posted
 * commands). The run itself blocks in a nested event loop of `mc::hil::Runner`, so the runner
 * answers further commands (`readFile`, `benchReport`, `runReplay`) while a plan runs but refuses
 * a second `run` or `check`.
 *
 * run() repeats the whole pipeline of the check on this thread (load, resolve, gate), compares
 * its fingerprint with the one the operator confirmed and applies the confirmation rule before
 * anything is opened. There is no way to start the runner without that pipeline.
 *
 * @note Created inside the runner thread; never touched from another thread.
 */
class HilRunner : public RunnerBase {
    Q_OBJECT
public:
    /**
     * @brief Creates the runner.
     * @param[in] control The side channel shared with the host.
     */
    explicit HilRunner(std::shared_ptr<HilControl> control);

    /// @brief Cancels a run in progress and kills a replay in progress.
    void shutdown() override;

    /// @brief true while a plan runs: the thread must not delete this object inside the run.
    bool shutdownDeferred() const noexcept override { return m_running; }

    /// @brief Loads, resolves and gates; answered by `checked`.
    /// @param[in] token The token of the command.
    /// @param[in] input Profile, plan, groups and PLC state.
    void check(quint64 token, const HilCheckInput& input);

    /// @brief Runs the plan after the confirmation rule; answered by `runFinished`.
    /// @param[in] token The token of the command.
    /// @param[in] request What to run and what was confirmed.
    void run(quint64 token, const HilRunRequest& request);

    /// @brief Reads a capture file (at most @p maxBytes); answered by `fileRead`.
    /// @param[in] token The token of the command.
    /// @param[in] path The file.
    /// @param[in] maxBytes The most bytes returned.
    void readFile(quint64 token, const QString& path, qint64 maxBytes);

    /// @brief Runs `mc_replay_tests` on a capture root; answered by `replayDone`.
    /// @param[in] token The token of the command.
    /// @param[in] program The `mc_replay_tests` executable.
    /// @param[in] root The folder that holds `<profile id>/` capture folders.
    void runReplay(quint64 token, const QString& program, const QString& root);

    /// @brief Builds the bench report of a capture root; answered by `benchReady`.
    /// @param[in] token The token of the command.
    /// @param[in] root The folder that holds `<profile id>/bench.csv` files.
    void benchReport(quint64 token, const QString& root);

signals:
    /// @brief A check ended.
    /// @param[out] result The gate's verdict and the texts.
    void checked(const mc::workbench::HilCheckResult& result);

    /// @brief A step is about to start.
    /// @param[out] stepId The step id.
    void stepStarted(const QString& stepId);

    /// @brief The step line of the run's output that names an outcome.
    /// @param[out] line Category, step id and text.
    void stepOutcome(const mc::workbench::HilStepLine& line);

    /// @brief One line of the run's console output.
    /// @param[out] line The text.
    void outputLine(const QString& line);

    /// @brief The run waits for the operator ("press Enter to continue").
    /// @param[out] text What the plan asks the operator to do.
    void promptRequested(const QString& text);

    /// @brief A run request ended (or was refused).
    /// @param[out] result Status, counts, folder.
    void runFinished(const mc::workbench::HilRunResult& result);

    /// @brief A capture file was read.
    /// @param[out] file Path and text.
    void fileRead(const mc::workbench::HilFileText& file);

    /// @brief The replay program ended.
    /// @param[out] result Exit code and output.
    void replayDone(const mc::workbench::HilReplayResult& result);

    /// @brief The bench report is ready.
    /// @param[out] result The text.
    void benchReady(const mc::workbench::HilBenchText& result);

protected:
    void stopAfterFailure() noexcept override;

private:
    std::shared_ptr<HilControl> m_control;
    bool m_running{false};
    QProcess* m_replay{nullptr};
};

} // namespace mc::workbench
