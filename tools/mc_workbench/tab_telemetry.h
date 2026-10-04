/**
 * @file tab_telemetry.h
 * @brief `TabTelemetry` and `TelemetryRegistry`: what one tab offers the Frame trace, Debug log and
 * Capture views, and the list of such tabs a window keeps.
 */
#pragma once

#include "mc/core/log.h"
#include "mc_workbench/capture_types.h"
#include "mc_workbench/log_model.h"
#include "mc_workbench/runner_types.h"
#include "mc_workbench/trace_model.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

namespace mc::workbench {

/**
 * @brief The trace ring, the capture state and the controls of one tab (a device or a mock).
 *
 * A tab creates one and calls attachHost() with its `DeviceHost` or `MockHost`: the host's batches
 * then flow into the trace model and out as log lines, and the controls (decode, capture) are sent
 * to the host's runner. Everything here runs on the GUI thread and holds value copies only.
 *
 * @note GUI thread only.
 */
class TabTelemetry : public QObject {
    Q_OBJECT
public:
    /// @brief What kind of tab this is.
    enum class Kind { Device, Mock };

    /**
     * @brief Creates the telemetry of a tab.
     * @param[in] name Name of the tab; shown in the views and in the log's tab column.
     * @param[in] kind Device or mock.
     * @param[in] parent Qt parent, normally the tab.
     */
    TabTelemetry(const QString& name, Kind kind, QObject* parent = nullptr);

    /// @brief The tab's name.
    /// @return The name given at construction.
    QString name() const { return m_name; }

    /// @brief The kind of tab.
    /// @return Device or mock.
    Kind kind() const noexcept { return m_kind; }

    /// @brief The trace ring.
    /// @return Never null.
    TraceModel* trace() const noexcept { return m_trace; }

    /**
     * @brief Connects @p host's batches and controls to this telemetry.
     *
     * @tparam Host `DeviceHost` or `MockHost` (they share `framesBatch`, `framesDropped`,
     *         `logBatch`, `captureStatusChanged`, `captureSaved`, `setTraceDecode` and the capture
     *         commands).
     * @param[in] host The tab's host; must outlive this object or be destroyed with it.
     */
    template <class Host> void attachHost(Host* host) {
        connect(host, &Host::framesBatch, this, &TabTelemetry::onFrames);
        connect(host, &Host::framesDropped, this, &TabTelemetry::onFramesDropped);
        connect(host, &Host::logBatch, this, &TabTelemetry::onLog);
        connect(host, &Host::captureStatusChanged, this, &TabTelemetry::onCaptureStatus);
        connect(host, &Host::captureSaved, this, &TabTelemetry::onCaptureSaved);
        m_setDecode = [host](bool on) { host->setTraceDecode(on); };
        m_start = [host](const CaptureSettings& s) { return host->startCapture(s); };
        m_stop = [host]() { return host->stopCapture(); };
        m_discard = [host]() { return host->discardCapture(); };
        m_save = [host](const CaptureSaveRequest& r) { return host->saveCapture(r); };
    }

    /// @brief Turns the runner's frame decoder on or off (it also clears nothing).
    /// @param[in] on true to decode.
    void setDecode(bool on);

    /// @brief Whether decode was asked for.
    /// @return true when on (the default).
    bool decode() const noexcept { return m_decode; }

    /// @brief Starts recording the tab's traffic.
    /// @param[in] settings Profile id, source and limits.
    /// @return The token of the runner's answer.
    quint64 startCapture(const CaptureSettings& settings);

    /// @brief Stops recording.
    /// @return The token of the runner's answer.
    quint64 stopCapture();

    /// @brief Drops the recorded data.
    /// @return The token of the runner's answer.
    quint64 discardCapture();

    /// @brief Writes the recorded capture.
    /// @param[in] request Target folder, protected folder, overwrite flag.
    /// @return The token carried by `captureSaved()`.
    quint64 saveCapture(const CaptureSaveRequest& request);

    /// @brief What the runner's capture holds, as last reported.
    /// @return The status.
    CaptureStatus captureStatus() const { return m_capture; }

    /// @brief The source a capture of this tab most likely has.
    /// @return The last hint of the tab (loopback host: mock; otherwise a real PLC).
    CaptureSource suggestedSource() const noexcept { return m_suggested; }

    /// @brief Tells the telemetry what the tab is connected to.
    /// @param[in] source The likely source of a capture.
    void setSuggestedSource(CaptureSource source) { m_suggested = source; }

    /// @brief Adds a line written by the GUI itself (link state change, fault) to the debug log.
    /// @param[in] level Level of the line.
    /// @param[in] category Subsystem name.
    /// @param[in] message The text.
    void note(mc::LogLevel level, const QString& category, const QString& message);

public slots:
    /// @brief Feeds a batch of chunks into the trace ring.
    /// @param[in] frames The chunks.
    void onFrames(const QVector<mc::workbench::FrameRecord>& frames);

    /// @brief Records how many chunks the runner left out.
    /// @param[in] total Running total.
    void onFramesDropped(quint64 total);

    /// @brief Forwards log lines to `logLines()`.
    /// @param[in] lines The lines.
    void onLog(const QVector<mc::workbench::LogLine>& lines);

    /// @brief Remembers the capture status and announces it.
    /// @param[in] status The status.
    void onCaptureStatus(const mc::workbench::CaptureStatus& status);

    /// @brief Announces the end of a save.
    /// @param[in] result The result.
    void onCaptureSaved(const mc::workbench::CaptureSaveResult& result);

signals:
    /// @brief Log lines of this tab (the registry feeds them into the shared log).
    /// @param[out] tab The tab's name.
    /// @param[out] lines The lines.
    void logLines(const QString& tab, const QVector<mc::workbench::LogLine>& lines);

    /// @brief The capture status changed.
    /// @param[out] status The new status.
    void captureStatusChanged(const mc::workbench::CaptureStatus& status);

    /// @brief A save finished or was refused.
    /// @param[out] result The result.
    void captureSaved(const mc::workbench::CaptureSaveResult& result);

private:
    QString m_name;
    Kind m_kind;
    TraceModel* m_trace;
    bool m_decode{true};
    CaptureStatus m_capture;
    CaptureSource m_suggested{CaptureSource::RealPlc};
    QElapsedTimer m_clock;
    std::function<void(bool)> m_setDecode;
    std::function<quint64(const CaptureSettings&)> m_start;
    std::function<quint64()> m_stop;
    std::function<quint64()> m_discard;
    std::function<quint64(const CaptureSaveRequest&)> m_save;
};

/**
 * @brief The telemetries of the tabs of one window, and the shared debug log they write to.
 *
 * Tabs register when they are created and are dropped from the list when they are destroyed.
 *
 * @note GUI thread only.
 */
class TelemetryRegistry : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Creates an empty registry with an empty log.
     * @param[in] parent Qt parent.
     */
    explicit TelemetryRegistry(QObject* parent = nullptr);

    /// @brief The shared debug log of every registered tab.
    /// @return Never null.
    LogModel* log() const noexcept { return m_log; }

    /// @brief Registers a tab's telemetry; its log lines flow into the shared log.
    /// @param[in] telemetry The telemetry; not owned, removed when destroyed.
    void add(TabTelemetry* telemetry);

    /// @brief The registered telemetries, in registration order.
    /// @return The list.
    QVector<TabTelemetry*> tabs() const { return m_tabs; }

signals:
    /// @brief A tab was registered.
    /// @param[out] telemetry The new telemetry.
    void tabAdded(mc::workbench::TabTelemetry* telemetry);

    /// @brief A registered tab was destroyed.
    /// @param[out] telemetry The telemetry (about to be destroyed; do not keep it).
    void tabRemoved(mc::workbench::TabTelemetry* telemetry);

private:
    LogModel* m_log;
    QVector<TabTelemetry*> m_tabs;
};

} // namespace mc::workbench
