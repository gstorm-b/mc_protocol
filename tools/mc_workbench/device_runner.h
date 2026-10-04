/**
 * @file device_runner.h
 * @brief `DeviceRunner`: the object that owns one `McDevice` and its `RecordingTransport` on a
 * runner thread and reports to the GUI in batches of value copies.
 */
#pragma once

#include "hil_capture/recording_transport.h"
#include "mc/device/mc_device.h"
#include "mc/device/mc_device_config.h"
#include "mc_workbench/capture_controller.h"
#include "mc_workbench/capture_types.h"
#include "mc_workbench/flow_gate.h"
#include "mc_workbench/frame_decoder.h"
#include "mc_workbench/queue_log_sink.h"
#include "mc_workbench/runner_base.h"
#include "mc_workbench/runner_types.h"

#include <QString>
#include <QVector>

#include <functional>
#include <memory>
#include <optional>
#include <utility>

class QThread;
class QTimer;

namespace mc::workbench {

/**
 * @brief Drives one `McDevice` for a device tab.
 *
 * Created by a `RunnerThread` factory, so the device, its transport, its timers and this object
 * all live on the runner thread. Every public method below is a command: it runs on the runner
 * thread (the host posts it) and answers through signals. Nothing here is called from the GUI
 * thread.
 *
 * High-rate output (values, frames, log lines) is collected and emitted at most about 30 times a
 * second (kFlushIntervalMs); link state, faults and results are emitted at once.
 *
 * Containment: every command and every slot connected to the device runs through
 * RunnerBase::guarded. After a contained exception the device is disconnected and every command
 * is answered with a refusal.
 *
 * @note A configuration change builds a new `McDevice` (the transport is injected so that it can
 *       be recorded); runtime subscriptions are dropped, as `McDevice::setConfig` drops them.
 * @see DeviceHost, RunnerThread
 */
class DeviceRunner : public RunnerBase {
    Q_OBJECT
public:
    /// @brief Milliseconds between two emits of the batched signals (about 30 a second).
    static constexpr int kFlushIntervalMs = 34;

    /// @brief The most value changes held in one batch; further changes are counted and dropped.
    static constexpr int kMaxPendingChanges = 20000;

    /// @brief The most wire chunks held for the GUI between two emits; further chunks are counted
    ///        and dropped from the view (a running capture still records them).
    static constexpr int kMaxPendingFrames = 20000;

    /// @brief The most bytes of wire chunks held for the GUI between two emits.
    static constexpr qint64 kMaxPendingFrameBytes = 8 * 1024 * 1024;

    /// @brief The most chunks one `framesBatch` carries; the rest waits for the next emit.
    static constexpr int kMaxFramesPerBatch = 4000;

    /**
     * @brief Builds the device for @p cfg on the current thread.
     * @param[in] cfg The configuration; an unusable one is reported when connecting.
     * @param[in] parent Qt parent, normally none.
     */
    explicit DeviceRunner(const mc::McDeviceConfig& cfg, QObject* parent = nullptr);

    /// @brief Deletes the device on the runner thread.
    ~DeviceRunner() override;

    /**
     * @brief Replaces the configuration; only while Disconnected.
     * @param[in] token Answered in `commandDone`.
     * @param[in] cfg The new configuration.
     */
    void applyConfig(quint64 token, const mc::McDeviceConfig& cfg);

    /// @brief Opens the link (`McDevice::connectToPlc`).
    void connectToPlc();

    /// @brief Closes the link (`McDevice::disconnectFromPlc`).
    void disconnectFromPlc();

    /**
     * @brief Subscribes to @p count points from @p device.
     * @param[in] token Answered in `commandDone`; `value` is the subscription id.
     * @param[in] device Head device, e.g. "D100".
     * @param[in] count Number of points.
     */
    void subscribe(quint64 token, const QString& device, quint32 count);

    /**
     * @brief Removes a subscription.
     * @param[in] token Answered in `commandDone`.
     * @param[in] id Id from `subscribe`.
     */
    void unsubscribe(quint64 token, quint32 id);

    /// @brief Reads words; `commandDone.value` is the request id, `requestFinished` the result.
    /// @param[in] token Answered in `commandDone`.
    /// @param[in] head Head device, e.g. "D100".
    /// @param[in] count Number of words.
    void readWords(quint64 token, const QString& head, quint16 count);

    /// @brief Reads bits; see readWords().
    /// @param[in] token Answered in `commandDone`.
    /// @param[in] head Head device, e.g. "M100".
    /// @param[in] count Number of points.
    void readBits(quint64 token, const QString& head, quint16 count);

    /// @brief Writes words; see readWords().
    /// @param[in] token Answered in `commandDone`.
    /// @param[in] head Head device, e.g. "D100".
    /// @param[in] values The values, in device order.
    void writeWords(quint64 token, const QString& head, const QVector<quint16>& values);

    /// @brief Writes bits; see readWords().
    /// @param[in] token Answered in `commandDone`.
    /// @param[in] head Head device, e.g. "M100".
    /// @param[in] values The values, in device order.
    void writeBits(quint64 token, const QString& head, const QVector<bool>& values);

    /// @brief Sets the lowest level of the log lines that are kept.
    /// @param[in] level The new level.
    void setLogLevel(mc::LogLevel level);

    /// @brief Turns the frame decoder (frame boundaries and text of the trace) on or off.
    /// @param[in] on true to decode.
    void setTraceDecode(bool on);

    /// @brief Turns the back-pressure on: a batch is emitted only after the GUI acknowledged the
    ///        previous one (`ackFlush`). Off by default, so a runner without a host never stalls.
    /// @param[in] on true to enforce acknowledgements.
    void enableFlowControl(bool on);

    /// @brief The GUI consumed everything emitted up to the last `flushed()`.
    void ackFlush();

    /**
     * @brief Starts recording the traffic of the link.
     * @param[in] token Answered in `commandDone`.
     * @param[in] settings Profile id, source and limits; a real-PLC capture of a loopback address
     *            is refused.
     */
    void startCapture(quint64 token, const mc::workbench::CaptureSettings& settings);

    /// @brief Stops recording; the data stays for saving.
    /// @param[in] token Answered in `commandDone`.
    void stopCapture(quint64 token);

    /// @brief Drops the recorded data.
    /// @param[in] token Answered in `commandDone`.
    void discardCapture(quint64 token);

    /**
     * @brief Writes the recorded capture; the files are written on a worker thread.
     * @param[in] token Carried by the `captureSaved` that answers.
     * @param[in] request Target folder, the protected folder and the overwrite flag.
     */
    void saveCapture(quint64 token, const mc::workbench::CaptureSaveRequest& request);

    /// @brief Chunks held for the GUI right now (a test hook for the back-pressure bound).
    /// @return The number of pending chunks.
    int pendingFrameCount() const noexcept { return static_cast<int>(m_pendingFrames.size()); }

    /// @brief Chunks the transport has recorded and the runner has not taken yet (a test hook).
    /// @return The number of chunks in the `RecordingTransport`.
    int transportChunkCount() const noexcept;

    /// @brief Emits `threadReport` with the threads of the runner, device and transport.
    void reportThreads();

    /// @brief The device, for thread identity checks and tests; use on the runner thread only.
    /// @return Never null (replaced by applyConfig()).
    mc::McDevice* device() const noexcept { return m_device; }

    /// @brief The sink the library writes this runner's log lines to; a test seam that lets a test
    ///        write lines at full speed through the same bounded queue. Runner thread only.
    /// @return Never null.
    mc::LogSink* logSink() noexcept { return &m_log; }

    /// @brief Installs a function the device slot for value changes calls first; a test seam for
    ///        "a slot that throws". Use on the runner thread only.
    /// @param[in] hook The function; empty removes it.
    void setValuesSlotHook(std::function<void()> hook) { m_valuesHook = std::move(hook); }

    /// @brief Disconnects, emits what is pending and deletes the device.
    void shutdown() override;

signals:
    /// @brief The link state changed (see `McDevice::linkStateChanged`).
    /// @param[out] state The new state.
    /// @param[out] reason Why.
    /// @param[out] detail Human-readable text.
    void linkStateChanged(mc::LinkState state, mc::LinkReason reason, const QString& detail);

    /// @brief The Session reported a link fault.
    /// @param[out] fault What it said.
    void linkFault(const mc::workbench::FaultReport& fault);

    /// @brief Values since the last emit.
    /// @param[out] batch Snapshots (newest per type) and changes.
    void valuesBatch(const mc::workbench::ValueBatch& batch);

    /// @brief Wire chunks since the last emit.
    /// @param[out] frames The chunks, oldest first.
    void framesBatch(const QVector<mc::workbench::FrameRecord>& frames);

    /// @brief Log lines since the last emit.
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

    /// @brief The answer to reportThreads().
    /// @param[out] report The thread identities.
    void threadReport(const mc::workbench::ThreadReport& report);

    /// @brief The last signal of a batch: the GUI answers it with `ackFlush()`.
    void flushed();

    /// @brief Chunks left out of the trace because the GUI was behind (running total).
    /// @param[out] total Chunks dropped since the device was built.
    void framesDropped(quint64 total);

    /// @brief The capture's status changed.
    /// @param[out] status What the capture holds now.
    void captureStatusChanged(const mc::workbench::CaptureStatus& status);

    /// @brief A save finished or was refused.
    /// @param[out] result Token, success, files.
    void captureSaved(const mc::workbench::CaptureSaveResult& result);

protected:
    /// @brief Disconnects the device and emits what is pending.
    void stopAfterFailure() noexcept override;

private:
    void buildDevice(const mc::McDeviceConfig& cfg);
    void destroyDevice();
    void flushNow(bool force = false);
    void collectFrames();
    void emitCaptureStatus();
    void refuse(quint64 token, const QString& why);
    void answer(quint64 token, const mc::Error& error, quint64 value);
    bool ready(quint64 token);

    std::shared_ptr<mc::hil::RecordingClock> m_clock;
    QueueLogSink m_log;
    mc::McDevice* m_device{nullptr};            ///< Child of this object.
    mc::hil::RecordingTransport* m_transport{nullptr}; ///< Owned by m_device; same thread.
    QTimer* m_flushTimer;                       ///< Periodic while the link is up.
    ValueBatch m_values;                        ///< Collected, not yet emitted.
    QVector<FrameRecord> m_pendingFrames;       ///< Taken off the transport, not yet emitted.
    qint64 m_pendingFrameBytes{0};
    quint64 m_framesDropped{0};
    bool m_dropsChanged{false};
    std::unique_ptr<FrameDecoder> m_decoder;    ///< Null while decode is off.
    bool m_decode{true};
    FlowGate m_gate;
    CaptureController m_capture;
    QVector<QThread*> m_saveWorkers;            ///< Capture writers still running.
    std::optional<mc::CycleInfo> m_lastCycle;   ///< Newest finished round, not yet emitted.
    std::function<void()> m_valuesHook;
    mc::McDeviceConfig m_config;                ///< The configuration the device was built from.
};

} // namespace mc::workbench
