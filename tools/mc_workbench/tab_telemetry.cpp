#include "mc_workbench/tab_telemetry.h"

namespace mc::workbench {

TabTelemetry::TabTelemetry(const QString& name, Kind kind, QObject* parent)
    : QObject(parent), m_name(name), m_kind(kind), m_trace(new TraceModel(this)) {
    registerRunnerMetaTypes();
    m_clock.start();
}

void TabTelemetry::setDecode(bool on) {
    m_decode = on;
    if (m_setDecode) {
        m_setDecode(on);
    }
}

quint64 TabTelemetry::startCapture(const CaptureSettings& settings) {
    return m_start ? m_start(settings) : 0;
}

quint64 TabTelemetry::stopCapture() {
    return m_stop ? m_stop() : 0;
}

quint64 TabTelemetry::discardCapture() {
    return m_discard ? m_discard() : 0;
}

quint64 TabTelemetry::saveCapture(const CaptureSaveRequest& request) {
    return m_save ? m_save(request) : 0;
}

void TabTelemetry::note(mc::LogLevel level, const QString& category, const QString& message) {
    LogLine line;
    line.tNs = m_clock.nsecsElapsed();
    line.level = level;
    line.category = category;
    line.message = message;
    emit logLines(m_name, {line});
}

void TabTelemetry::onFrames(const QVector<FrameRecord>& frames) {
    m_trace->append(frames);
}

void TabTelemetry::onFramesDropped(quint64 total) {
    m_trace->setDroppedByRunner(total);
}

void TabTelemetry::onLog(const QVector<LogLine>& lines) {
    emit logLines(m_name, lines);
}

void TabTelemetry::onCaptureStatus(const CaptureStatus& status) {
    m_capture = status;
    emit captureStatusChanged(status);
}

void TabTelemetry::onCaptureSaved(const CaptureSaveResult& result) {
    emit captureSaved(result);
}

TelemetryRegistry::TelemetryRegistry(QObject* parent)
    : QObject(parent), m_log(new LogModel(this)) {}

void TelemetryRegistry::add(TabTelemetry* telemetry) {
    if (telemetry == nullptr || m_tabs.contains(telemetry)) {
        return;
    }
    m_tabs.push_back(telemetry);
    connect(telemetry, &TabTelemetry::logLines, m_log, &LogModel::append);
    connect(telemetry, &QObject::destroyed, this, [this, telemetry]() {
        m_tabs.removeOne(telemetry);
        emit tabRemoved(telemetry);
    });
    emit tabAdded(telemetry);
}

} // namespace mc::workbench
