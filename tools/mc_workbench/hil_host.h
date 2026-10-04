/**
 * @file hil_host.h
 * @brief `HilHost`: the GUI-thread handle of one `HilRunner` thread.
 */
#pragma once

#include "mc_workbench/hil_runner.h"
#include "mc_workbench/hil_types.h"
#include "mc_workbench/runner_thread.h"

#include <QObject>
#include <QString>

#include <memory>

namespace mc::workbench {

/**
 * @brief What the HIL runner view talks to: a runner thread with a `HilRunner` on it, commands as
 * queued calls with value copies, answers as queued signals.
 *
 * The same rules as `DeviceHost`: every method returns at once. `cancelRun()` and
 * `answerPrompt()` do not queue behind a running plan: they go through the shared `HilControl`.
 *
 * Destroying the host cancels a run, then stops the thread within
 * `RunnerThread::kDefaultStopTimeoutMs`. A run is only ever stopped at a step boundary, so a step
 * that outlasts the bound makes the thread stuck: it is abandoned and ends by itself.
 *
 * @note Lives on the GUI thread.
 */
class HilHost : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Starts the runner thread.
     * @param[in] name Name of the thread.
     * @param[in] parent Qt parent of this handle.
     */
    explicit HilHost(const QString& name, QObject* parent = nullptr);

    /// @brief Cancels a run, stops the thread (bounded) and releases the handle.
    ~HilHost() override;

    /// @brief Loads, resolves and gates on the runner thread.
    /// @param[in] input Profile, plan, groups and PLC state; copied.
    /// @return The token carried by the `checked` that answers.
    quint64 check(const HilCheckInput& input);

    /// @brief Runs a plan; the runner repeats the check and the confirmation before it starts.
    /// @param[in] request What to run and what was confirmed; copied.
    /// @return The token carried by the `runFinished` that answers.
    quint64 run(const HilRunRequest& request);

    /// @brief Asks the run to end at the next step boundary (and releases a waiting prompt).
    void cancelRun();

    /// @brief Answers the operator prompt the run is waiting for.
    void answerPrompt();

    /// @brief Reads a capture file on the runner thread.
    /// @param[in] path The file.
    /// @param[in] maxBytes The most bytes returned (0: 1 MiB).
    /// @return The token carried by the `fileRead` that answers.
    quint64 readFile(const QString& path, qint64 maxBytes = 0);

    /// @brief Runs `mc_replay_tests` on a capture root.
    /// @param[in] program The executable.
    /// @param[in] root The folder that holds the capture folders.
    /// @return The token carried by the `replayDone` that answers.
    quint64 runReplay(const QString& program, const QString& root);

    /// @brief Builds the bench report of a capture root.
    /// @param[in] root The folder that holds `<profile id>/bench.csv` files.
    /// @return The token carried by the `benchReady` that answers.
    quint64 benchReport(const QString& root);

    /// @brief The handle of the thread, for stop(), isStuck() and the thread identity.
    /// @return Never null.
    RunnerThread* runnerThread() const noexcept { return m_thread; }

signals:
    /// @brief A check ended.
    /// @param[out] result The gate's verdict and the texts.
    void checked(const mc::workbench::HilCheckResult& result);
    /// @brief A step is about to start.
    /// @param[out] stepId The step id.
    void stepStarted(const QString& stepId);
    /// @brief A step outcome line of the run.
    /// @param[out] line Category, step id, text.
    void stepOutcome(const mc::workbench::HilStepLine& line);
    /// @brief A line of the run's console output.
    /// @param[out] line The text.
    void outputLine(const QString& line);
    /// @brief The run waits for the operator.
    /// @param[out] text What the plan asks the operator to do.
    void promptRequested(const QString& text);
    /// @brief A run request ended or was refused.
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
    /// @brief The runner contained an exception.
    /// @param[out] message What failed.
    void failed(const QString& message);

private:
    quint64 nextToken() { return ++m_token; }

    std::shared_ptr<HilControl> m_control;
    RunnerThread* m_thread{nullptr};
    quint64 m_token{0};
};

} // namespace mc::workbench
