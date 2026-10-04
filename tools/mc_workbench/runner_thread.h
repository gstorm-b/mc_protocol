/**
 * @file runner_thread.h
 * @brief `RunnerThread`: one `QThread` that owns one runner object, created inside the thread,
 * with queued commands and a bounded shutdown.
 */
#pragma once

#include "mc_workbench/runner_base.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>

class QThread;

namespace mc::workbench {

/**
 * @brief The GUI-side handle of one runner thread (SPEC-gui-tool.md, "Architecture").
 *
 * The constructor starts a `QThread`; the thread's first act is to call the factory, so the
 * runner object, and with it every `McDevice`, transport and mock it creates, is born on the
 * runner thread. The GUI thread never touches the runner object: it posts commands (queued, value
 * copies captured by the functor) and receives results through signals of the host that wraps
 * this class.
 *
 * Containment: a command that throws is caught at the slot boundary (RunnerBase::guarded); the
 * runner stops what it owns and reports through failed().
 *
 * Shutdown: stop() asks the runner to shut down on its own thread, deletes it there, quits the
 * loop and waits up to a bound. A thread that does not finish in time is reported through
 * stuck() and left to finish by itself; the GUI thread is never blocked longer than the bound.
 *
 * @note Lives on the GUI thread. Methods other than post() and the accessors must be called
 *       from the thread that created the object.
 * @see RunnerBase
 */
class RunnerThread : public QObject {
    Q_OBJECT
public:
    /// @brief Creates the runner object; runs on the runner thread. May return null on failure.
    using Factory = std::function<RunnerBase*()>;

    /// @brief A command: runs on the runner thread with the runner object.
    using Command = std::function<void(RunnerBase&)>;

    /// @brief The default bound of stop() and of the destructor, in milliseconds.
    static constexpr int kDefaultStopTimeoutMs = 3000;

    /**
     * @brief Starts the thread and, on it, the factory.
     * @param[in] name Name of the thread (`QObject::objectName()`), for debuggers and logs.
     * @param[in] factory Builds the runner object on the runner thread; the thread owns the
     *            result and deletes it on shutdown.
     * @param[in] parent Qt parent of this handle.
     */
    RunnerThread(const QString& name, Factory factory, QObject* parent = nullptr);

    /**
     * @brief Stops the thread within kDefaultStopTimeoutMs.
     *
     * A thread that is still busy after the bound is abandoned: it finishes and deletes itself
     * later, and its objects are deleted on it.
     */
    ~RunnerThread() override;

    /**
     * @brief Queues @p command for the runner thread.
     *
     * Commands run in the order posted. Ignored after requestStop(). An exception thrown by
     * @p command is contained.
     *
     * @param[in] command The code to run on the runner thread; capture value copies only.
     */
    void post(Command command);

    /// @brief Asks the runner to shut down and the thread to end; returns at once.
    void requestStop();

    /**
     * @brief Waits for the thread to end.
     * @param[in] timeoutMs Longest wait, in milliseconds.
     * @return true when the thread has finished.
     */
    bool waitFinished(int timeoutMs);

    /**
     * @brief requestStop() and waitFinished(); reports a stuck thread instead of blocking on it.
     * @param[in] timeoutMs Longest wait, in milliseconds.
     * @return true when the thread finished in time; false after emitting stuck().
     */
    bool stop(int timeoutMs = kDefaultStopTimeoutMs);

    /// @brief The runner thread.
    /// @return Never null. Use it to compare thread identities; do not call into it.
    QThread* workerThread() const noexcept { return m_thread; }

    /// @brief Whether the thread is running.
    /// @return true until it has finished.
    bool isRunning() const;

    /// @brief Whether the last stop() timed out and the thread has not finished since.
    /// @return true while the thread is stuck.
    bool isStuck() const noexcept { return m_stuck; }

signals:
    /// @brief The runner contained an exception (queued from the runner thread).
    /// @param[out] message What failed and why.
    void failed(const QString& message);

    /// @brief stop() waited the whole bound and the thread had not finished.
    /// @param[out] message Names the thread and the bound.
    void stuck(const QString& message);

    /// @brief The thread finished; every runner object is already deleted.
    void finished();

private:
    struct State;

    static void shutdownOnThread(const std::shared_ptr<State>& state);

    QThread* m_thread;
    std::shared_ptr<State> m_state;
    bool m_stopRequested{false};
    bool m_stuck{false};
};

} // namespace mc::workbench
