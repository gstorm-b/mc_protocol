/**
 * @file device_host.h
 * @brief `DeviceHost`: the GUI-thread handle of one `DeviceRunner` thread.
 */
#pragma once

#include "mc/core/log.h"
#include "mc/device/mc_device.h"
#include "mc/device/mc_device_config.h"
#include "mc_workbench/capture_types.h"
#include "mc_workbench/runner_thread.h"
#include "mc_workbench/runner_types.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

namespace mc::workbench {

class DeviceRunner;

/**
 * @brief What a device tab talks to: starts a runner thread with a `DeviceRunner` on it, forwards
 * commands as queued calls and re-emits the runner's signals on the GUI thread.
 *
 * Every method returns at once and never touches the device: a command is a functor with value
 * copies, posted to the runner thread. Commands that can fail return a token; the answer is one
 * `commandDone` with the same token. Values, frames and log lines arrive in batches (about 30 a
 * second at most) as value copies.
 *
 * @note Lives on the GUI thread. Destroying it stops the runner thread within
 *       `RunnerThread::kDefaultStopTimeoutMs`.
 * @see DeviceRunner, RunnerThread
 */
class DeviceHost : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Starts a runner thread that builds a `DeviceRunner` for @p cfg.
     * @param[in] name Name of the thread.
     * @param[in] cfg The initial configuration; copied.
     * @param[in] parent Qt parent of this handle.
     */
    explicit DeviceHost(const QString& name, const mc::McDeviceConfig& cfg = {},
                        QObject* parent = nullptr);

    /// @brief Stops the thread (bounded) and releases the handle.
    ~DeviceHost() override;

    /// @brief Replaces the configuration while Disconnected.
    /// @param[in] cfg The new configuration; copied.
    /// @return The token of the `commandDone` that answers.
    quint64 applyConfig(const mc::McDeviceConfig& cfg);

    /// @brief Opens the link; progress arrives as `linkStateChanged`.
    void connectToPlc();

    /// @brief Closes the link.
    void disconnectFromPlc();

    /// @brief Subscribes to @p count points from @p device.
    /// @param[in] device Head device, e.g. "D100".
    /// @param[in] count Number of points.
    /// @return The token; `commandDone.value` is the subscription id.
    quint64 subscribe(const QString& device, quint32 count);

    /// @brief Removes a subscription.
    /// @param[in] id Id from `subscribe`.
    /// @return The token of the `commandDone` that answers.
    quint64 unsubscribe(quint32 id);

    /// @brief Reads words; the result arrives as `requestFinished`.
    /// @param[in] head Head device.
    /// @param[in] count Number of words.
    /// @return The token; `commandDone.value` is the request id.
    quint64 readWords(const QString& head, quint16 count);

    /// @brief Reads bits; see readWords().
    /// @param[in] head Head device.
    /// @param[in] count Number of points.
    /// @return The token.
    quint64 readBits(const QString& head, quint16 count);

    /// @brief Writes words; see readWords().
    /// @param[in] head Head device.
    /// @param[in] values The values; copied.
    /// @return The token.
    quint64 writeWords(const QString& head, const QVector<quint16>& values);

    /// @brief Writes bits; see readWords().
    /// @param[in] head Head device.
    /// @param[in] values The values; copied.
    /// @return The token.
    quint64 writeBits(const QString& head, const QVector<bool>& values);

    /// @brief Sets the lowest level of the log lines the runner keeps.
    /// @param[in] level The new level.
    void setLogLevel(mc::LogLevel level);

    /// @brief Turns the frame decoder of the trace on or off.
    /// @param[in] on true to decode.
    void setTraceDecode(bool on);

    /// @brief Starts recording the link's traffic.
    /// @param[in] settings Profile id, source and limits.
    /// @return The token of the `commandDone` that answers.
    quint64 startCapture(const mc::workbench::CaptureSettings& settings);

    /// @brief Stops recording; the data stays for saving.
    /// @return The token of the `commandDone` that answers.
    quint64 stopCapture();

    /// @brief Drops the recorded data.
    /// @return The token of the `commandDone` that answers.
    quint64 discardCapture();

    /// @brief Writes the recorded capture (on a worker thread of the runner).
    /// @param[in] request Target folder, protected folder, overwrite flag.
    /// @return The token carried by the `captureSaved` that answers.
    quint64 saveCapture(const mc::workbench::CaptureSaveRequest& request);

    /// @brief Asks the runner for the threads of its objects; answered by `threadReport`.
    void requestThreadReport();

    /**
     * @brief Posts @p command to run on the runner thread (tests and advanced use).
     * @param[in] command Runs with the `DeviceRunner`; contained when it throws.
     */
    void post(std::function<void(DeviceRunner&)> command);

    /// @brief The handle of the thread, for stop(), isStuck() and the thread identity.
    /// @return Never null.
    RunnerThread* runnerThread() const noexcept { return m_thread; }

signals:
    /// @brief The link state changed.
    /// @param[out] state The new state.
    /// @param[out] reason Why.
    /// @param[out] detail Human-readable text.
    void linkStateChanged(mc::LinkState state, mc::LinkReason reason, const QString& detail);

    /// @brief The Session reported a link fault.
    /// @param[out] fault What it said.
    void linkFault(const mc::workbench::FaultReport& fault);

    /// @brief Values since the last batch.
    /// @param[out] batch Snapshots and changes.
    void valuesBatch(const mc::workbench::ValueBatch& batch);

    /// @brief Wire chunks since the last batch.
    /// @param[out] frames The chunks, oldest first.
    void framesBatch(const QVector<mc::workbench::FrameRecord>& frames);

    /// @brief Log lines since the last batch.
    /// @param[out] lines The lines, oldest first.
    void logBatch(const QVector<mc::workbench::LogLine>& lines);

    /// @brief A polling round finished.
    /// @param[out] cycle Summary of the round.
    void cycleDone(const mc::CycleInfo& cycle);

    /// @brief An ad-hoc request ended.
    /// @param[out] outcome The result.
    void requestFinished(const mc::workbench::RequestOutcome& outcome);

    /// @brief The answer to a command.
    /// @param[out] result Token, success and details.
    void commandDone(const mc::workbench::CommandResult& result);

    /// @brief The answer to requestThreadReport().
    /// @param[out] report The thread identities.
    void threadReport(const mc::workbench::ThreadReport& report);

    /// @brief Chunks left out of the trace because the GUI was behind (running total).
    /// @param[out] total Chunks dropped since the device was built.
    void framesDropped(quint64 total);

    /// @brief The capture's status changed.
    /// @param[out] status What the capture holds now.
    void captureStatusChanged(const mc::workbench::CaptureStatus& status);

    /// @brief A save finished or was refused.
    /// @param[out] result Token, success, files.
    void captureSaved(const mc::workbench::CaptureSaveResult& result);

    /// @brief An exception was contained on the runner thread; the device is stopped.
    /// @param[out] message What failed and why.
    void failed(const QString& message);

private slots:
    void onFlushed();

private:
    quint64 nextToken() noexcept { return ++m_lastToken; }

    RunnerThread* m_thread{nullptr};
    quint64 m_lastToken{0};
};

} // namespace mc::workbench
