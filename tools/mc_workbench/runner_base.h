/**
 * @file runner_base.h
 * @brief `RunnerBase`: the common part of every object that lives on a runner thread (exception
 * containment, failure state, debug counters).
 */
#pragma once

#include <QObject>
#include <QString>

#include <exception>

namespace mc::workbench {

/**
 * @brief Base of `DeviceRunner` and `MockRunner`.
 *
 * A runner object is created inside its `RunnerThread` and never leaves it. Every entry point
 * into it (a queued command, a Qt signal of an object it owns, a timer) runs its body through
 * guarded(), so that an exception never escapes into the event loop: it is reported through
 * failed(), the runner stops what it owns and refuses further commands. The GUI thread keeps
 * running.
 *
 * @note Not thread safe by design: every method runs on the runner thread. The two static
 *       counters are the only members that may be read from any thread.
 * @see RunnerThread
 */
class RunnerBase : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Creates a runner object; counts it in liveRunners().
     * @param[in] parent Qt parent, normally none.
     */
    explicit RunnerBase(QObject* parent = nullptr);

    /// @brief Counts the deletion; a deletion from a thread other than the object's own is
    ///        counted in foreignDeletes().
    ~RunnerBase() override;

    /// @brief Number of runner objects that exist right now (debug hook of GUI-04).
    /// @return The count; any thread.
    static int liveRunners() noexcept;

    /// @brief Number of runner objects that were deleted on a thread other than their own.
    /// @return The count since the process started; any thread. Must stay 0.
    static int foreignDeletes() noexcept;

    /**
     * @brief Stops everything the runner owns (disconnect, close, delete children).
     *
     * Called on the runner thread by `RunnerThread` before the runner is deleted.
     */
    virtual void shutdown() = 0;

    /**
     * @brief Whether the runner is in the middle of a blocking job that must end before it may be
     * deleted.
     *
     * `RunnerThread` asks before it deletes the runner on shutdown and tries again later while this
     * returns true. The default is false. A job that spins a nested event loop (the HIL run) answers
     * true until it is done, so the shutdown never runs inside it.
     *
     * @return true while deleting the runner now would pull it out from under a running slot.
     */
    virtual bool shutdownDeferred() const noexcept { return false; }

    /// @brief Whether an exception stopped this runner.
    /// @return true after the first contained exception.
    bool hasFailed() const noexcept { return m_failed; }

    /**
     * @brief Runs @p body and contains any exception it throws.
     *
     * On an exception: marks the runner failed, calls stopAfterFailure() once, and emits
     * failed() with @p what and the exception text.
     *
     * @tparam F Callable with no arguments.
     * @param[in] what Short name of the entry point, for the message.
     * @param[in] body The code to run.
     * @return true when @p body completed.
     */
    template <class F> bool guarded(const char* what, F&& body) {
        try {
            body();
            return true;
        } catch (const std::exception& e) {
            fail(what, QString::fromUtf8(e.what()));
        } catch (...) {
            fail(what, QStringLiteral("unknown exception"));
        }
        return false;
    }

signals:
    /**
     * @brief An exception was contained; the runner has stopped what it owns.
     * @param[out] message "<entry point>: <exception text>".
     */
    void failed(const QString& message);

protected:
    /// @brief Stops the runner's device or server after a contained exception; must not throw.
    virtual void stopAfterFailure() noexcept = 0;

private:
    void fail(const char* what, const QString& text);

    bool m_failed{false};
};

} // namespace mc::workbench
