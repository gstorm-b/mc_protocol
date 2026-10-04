/**
 * @file capture_controller.h
 * @brief `CaptureController`: the capture state of one runner (recording, limits, save), used on
 * the runner thread. The files are written by a worker thread the runner starts, never by the GUI
 * thread.
 */
#pragma once

#include "mc/device/mc_device_config.h"
#include "mc_workbench/capture_types.h"
#include "mc_workbench/runner_types.h"

#include <QString>
#include <QVector>

#include <functional>

namespace mc::workbench {

/**
 * @brief Records the chunks of one link and prepares them for saving.
 *
 * The runner calls add() for every chunk it takes off its transport (or, for a mock, sees on its
 * server) and the commands start(), stop() and prepareSave() when the GUI asks. Recording is
 * bounded by `CaptureSettings::maxChunks` and `maxBytes`: at the limit it stops and the capture is
 * marked full, still saveable.
 *
 * A capture of a real PLC is refused when the link cannot be one: a loopback TCP address, or a
 * mock (`setLocalTarget`).
 *
 * @note Runner thread only; no locking. prepareSave() returns a job that touches no controller
 *       state and may run on any thread.
 */
class CaptureController {
public:
    /**
     * @brief Tells the controller what the runner is connected to.
     * @param[in] device The link's configuration; kept for `run.meta`.
     * @param[in] localTarget true when the peer cannot be a real PLC (a loopback address, or the
     *            runner is a mock).
     */
    void setTarget(const mc::McDeviceConfig& device, bool localTarget);

    /**
     * @brief Starts a new recording, dropping any earlier one.
     * @param[in] settings Profile id, source and limits.
     * @param[out] error Why it was refused.
     * @return true when recording started.
     */
    bool start(const CaptureSettings& settings, QString* error);

    /// @brief Stops recording; the data stays for saving.
    void stop();

    /// @brief Drops the recorded data.
    void discard();

    /// @brief Whether chunks are being recorded.
    /// @return true while recording.
    bool active() const noexcept { return m_status.active; }

    /**
     * @brief Records one chunk (a no-op unless recording).
     * @param[in] record The chunk; copied.
     */
    void add(const FrameRecord& record);

    /// @brief What the controller holds.
    /// @return A copy of the current status.
    CaptureStatus status() const { return m_status; }

    /// @brief Bumps when the status changed since the last call of takeChanged().
    /// @return true once per change.
    bool takeChanged() noexcept {
        const bool changed = m_changed;
        m_changed = false;
        return changed;
    }

    /// @brief The work of a save: builds the records and writes the files; runs on any thread.
    using SaveJob = std::function<CaptureSaveResult()>;

    /**
     * @brief Checks a save request and, when it is allowed, returns the job that carries it out.
     * @param[in] token Token of the save command; carried by the job's result.
     * @param[in] request Where to write.
     * @param[out] refusal Why nothing will be written (the target, an existing folder, no data).
     * @return The job, or an empty function when refused.
     */
    SaveJob prepareSave(quint64 token, const CaptureSaveRequest& request, QString* refusal);

private:
    void touch() noexcept { m_changed = true; }

    mc::McDeviceConfig m_device;        ///< The link as last configured.
    mc::McDeviceConfig m_captureDevice; ///< The link as it was when the recording started.
    bool m_local{false};
    CaptureSettings m_settings;
    CaptureStatus m_status;
    QVector<FrameRecord> m_chunks;
    bool m_changed{false};
};

} // namespace mc::workbench
