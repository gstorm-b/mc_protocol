/**
 * @file queue_log_sink.h
 * @brief `QueueLogSink`: a `mc::LogSink` that collects lines for a runner to emit in batches.
 */
#pragma once

#include "mc/core/log.h"
#include "hil_capture/recording_transport.h"
#include "mc_workbench/runner_types.h"

#include <QVector>

#include <cstddef>
#include <memory>
#include <string_view>
#include <utility>

namespace mc::workbench {

/**
 * @brief Collects the log lines of one runner's objects with a bounded queue.
 *
 * Used on the runner thread only (the library calls a sink from the thread its object runs on),
 * so it takes no lock. The runner drains it with take() when it emits a batch. A queue that is
 * full drops the new lines and counts them; take() reports the count as one synthetic Warn line.
 */
class QueueLogSink final : public mc::LogSink {
public:
    /// @brief The most lines held between two take() calls.
    static constexpr int kMaxPending = 5000;

    /**
     * @brief Creates a sink at Info level.
     * @param[in] clock The runner's monotonic clock; stamps every line.
     */
    explicit QueueLogSink(std::shared_ptr<mc::hil::RecordingClock> clock)
        : m_clock(std::move(clock)) {}

    /// @brief Whether @p level is at or above the sink's level.
    /// @param[in] level The level the caller is about to log at.
    /// @return true when the line is wanted.
    bool enabled(mc::LogLevel level) const noexcept override { return level >= m_level; }

    /**
     * @brief Copies one line into the queue.
     * @param[in] level Level of the line.
     * @param[in] category Subsystem name.
     * @param[in] message The text; copied.
     */
    void write(mc::LogLevel level, std::string_view category,
               std::string_view message) noexcept override;

    /// @brief Sets the lowest level that is kept.
    /// @param[in] level The new level; `Off` keeps nothing.
    void setLevel(mc::LogLevel level) noexcept { m_level = level; }

    /// @brief Whether lines are waiting.
    /// @return true when take() would return something.
    bool hasPending() const noexcept { return !m_lines.isEmpty() || m_dropped != 0; }

    /**
     * @brief Moves the collected lines out.
     * @return The lines, oldest first, with one Warn line at the end when lines were dropped.
     */
    QVector<LogLine> take();

private:
    mc::LogLevel m_level{mc::LogLevel::Info};
    std::shared_ptr<mc::hil::RecordingClock> m_clock;
    QVector<LogLine> m_lines;
    quint32 m_dropped{0};
};

} // namespace mc::workbench
