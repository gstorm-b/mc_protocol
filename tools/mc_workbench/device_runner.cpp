#include "mc_workbench/device_runner.h"

#include "mc/device/serial_transport.h"
#include "mc_workbench/capture_export.h"
#include "mc/device/tcp_transport.h"

#include <QMetaObject>
#include <QStringView>
#include <QPointer>
#include <QThread>
#include <QTimer>

#include <utility>

namespace mc::workbench {

namespace {
// How long a destructor waits for a file-writing thread (the stop bound of RunnerThread).
constexpr int kWorkerBoundMs = 3000;
} // namespace

namespace {

quintptr identity(const QThread* thread) {
    return reinterpret_cast<quintptr>(thread);
}

} // namespace

DeviceRunner::DeviceRunner(const mc::McDeviceConfig& cfg, QObject* parent)
    : RunnerBase(parent), m_clock(std::make_shared<mc::hil::RecordingClock>()), m_log(m_clock),
      m_flushTimer(new QTimer(this)) {
    registerRunnerMetaTypes();
    m_flushTimer->setInterval(kFlushIntervalMs);
    connect(m_flushTimer, &QTimer::timeout, this, [this]() {
        guarded("flush", [this]() { flushNow(); });
    });
    buildDevice(cfg);
}

DeviceRunner::~DeviceRunner() {
    destroyDevice();
    // A capture writer posts its result to this object: wait for it (it only writes files).
    const QVector<QThread*> workers = m_saveWorkers;
    m_saveWorkers.clear();
    for (QThread* worker : workers) {
        if (worker->wait(kWorkerBoundMs)) {
            delete worker;
        } else {
            // A write that does not end in time: a running QThread is never deleted. The thread object
            // deletes itself when the thread ends (and is leaked when its owner thread is gone).
            QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
        }
    }
}

void DeviceRunner::buildDevice(const mc::McDeviceConfig& cfg) {
    destroyDevice();
    m_config = cfg;
    m_pendingFrames.clear();
    m_pendingFrameBytes = 0;
    m_decoder = m_decode ? std::make_unique<FrameDecoder>(cfg.frame) : nullptr;
    m_capture.setTarget(cfg, cfg.transport == mc::TransportKind::Tcp &&
                                 isLoopbackHost(cfg.tcp.host));

    std::unique_ptr<mc::Transport> inner;
    if (cfg.transport == mc::TransportKind::Serial) {
        inner = std::make_unique<mc::SerialTransport>(cfg.serial);
    } else {
        inner = std::make_unique<mc::TcpTransport>(cfg.tcp);
    }
    auto recording = std::make_unique<mc::hil::RecordingTransport>(std::move(inner), m_clock);
    m_transport = recording.get();
    m_device = new mc::McDevice(cfg, std::move(recording), this);
    m_device->setLogSink(&m_log);

    connect(m_device, &mc::McDevice::linkStateChanged, this,
            [this](mc::LinkState state, mc::LinkReason reason, const QString& detail) {
                guarded("linkStateChanged", [&]() {
                    flushNow(true); // what led to the change goes first
                    if (state == mc::LinkState::Disconnected) {
                        m_flushTimer->stop();
                    }
                    emit linkStateChanged(state, reason, detail);
                });
            });
    connect(m_device, &mc::McDevice::linkFault, this, [this](const mc::LinkFaultInfo& fault) {
        guarded("linkFault", [&]() {
            FaultReport report;
            report.kind = static_cast<int>(fault.kind);
            report.errorCode = static_cast<int>(fault.error.code);
            report.message = QString::fromUtf8(fault.error.message);
            report.reopenTransport = fault.reopenTransport;
            emit linkFault(report);
        });
    });
    connect(m_device, &mc::McDevice::valuesChanged, this,
            [this](mc::DeviceType type, quint32 round, const QVector<mc::Change>& changes) {
                guarded("valuesChanged", [&]() {
                    if (m_valuesHook) {
                        m_valuesHook();
                    }
                    if (hasFailed()) {
                        return;
                    }
                    if (m_values.changes.size() >= kMaxPendingChanges) {
                        ++m_values.droppedChanges;
                        return;
                    }
                    m_values.changes.push_back(ValueUpdate{type, round, changes});
                });
            });
    connect(m_device, &mc::McDevice::snapshotReady, this,
            [this](const mc::DeviceSnapshot& snapshot) {
                guarded("snapshotReady", [&]() {
                    if (hasFailed()) {
                        return;
                    }
                    for (mc::DeviceSnapshot& kept : m_values.snapshots) {
                        if (kept.type == snapshot.type) {
                            kept = snapshot; // only the newest one of a type is worth sending
                            return;
                        }
                    }
                    m_values.snapshots.push_back(snapshot);
                });
            });
    connect(m_device, &mc::McDevice::cycleDone, this, [this](const mc::CycleInfo& cycle) {
        guarded("cycleDone", [&]() { m_lastCycle = cycle; });
    });
    connect(m_device, &mc::McDevice::requestFinished, this,
            [this](mc::RequestId id, const mc::Error& error, const QByteArray& payload) {
                guarded("requestFinished", [&]() {
                    RequestOutcome outcome;
                    outcome.id = id;
                    outcome.errorCode = static_cast<int>(error.code);
                    outcome.message = error.ok() ? QString() : QString::fromUtf8(error.message);
                    outcome.payload = payload;
                    emit requestFinished(outcome);
                });
            });
}

void DeviceRunner::destroyDevice() {
    if (m_device != nullptr) {
        // A plain delete: this runs from a command or the destructor, never from a slot that is
        // connected to the device's own signals.
        delete m_device;
        m_device = nullptr;
        m_transport = nullptr;
    }
}

void DeviceRunner::collectFrames() {
    if (m_transport == nullptr) {
        return;
    }
    // Take every chunk off the transport at each tick: the recording of RecordingTransport would
    // otherwise grow for as long as the link stays up (the memory bound of GUI-05).
    const QVector<mc::hil::WireChunk> chunks = m_transport->takeChunks();
    for (const mc::hil::WireChunk& chunk : chunks) {
        FrameRecord record;
        record.tNs = chunk.tNs;
        record.tx = chunk.dir == mc::hil::WireDirection::Tx;
        record.bytes = chunk.bytes;
        if (m_decoder) {
            m_decoder->annotate(record);
        }
        m_capture.add(record);
        if (m_pendingFrames.size() >= kMaxPendingFrames ||
            m_pendingFrameBytes + record.bytes.size() > kMaxPendingFrameBytes) {
            ++m_framesDropped;
            m_dropsChanged = true;
            continue;
        }
        m_pendingFrameBytes += record.bytes.size();
        m_pendingFrames.push_back(std::move(record));
    }
}

void DeviceRunner::emitCaptureStatus() {
    m_capture.takeChanged();
    emit captureStatusChanged(m_capture.status());
}

void DeviceRunner::flushNow(bool force) {
    collectFrames();
    if (!force && !m_gate.canEmit()) {
        return; // the GUI has not consumed the last batch: keep collecting, in bounded queues
    }
    bool any = false;
    if (!m_values.snapshots.isEmpty() || !m_values.changes.isEmpty() ||
        m_values.droppedChanges != 0) {
        ValueBatch batch;
        std::swap(batch, m_values);
        emit valuesBatch(batch);
        any = true;
    }
    if (!m_pendingFrames.isEmpty()) {
        QVector<FrameRecord> frames;
        if (force || m_pendingFrames.size() <= kMaxFramesPerBatch) {
            frames.swap(m_pendingFrames);
            m_pendingFrameBytes = 0;
        } else {
            frames = m_pendingFrames.mid(0, kMaxFramesPerBatch);
            m_pendingFrames.remove(0, kMaxFramesPerBatch);
            for (const FrameRecord& record : frames) {
                m_pendingFrameBytes -= record.bytes.size();
            }
        }
        emit framesBatch(frames);
        any = true;
    }
    if (m_dropsChanged) {
        m_dropsChanged = false;
        emit framesDropped(m_framesDropped);
        any = true;
    }
    if (m_log.hasPending()) {
        emit logBatch(m_log.take());
        any = true;
    }
    if (m_lastCycle) {
        const mc::CycleInfo cycle = *m_lastCycle;
        m_lastCycle.reset();
        emit cycleDone(cycle);
        any = true;
    }
    if (m_capture.takeChanged()) {
        emit captureStatusChanged(m_capture.status());
        any = true;
    }
    if (any && !force && m_gate.enabled()) {
        m_gate.sent();
        emit flushed();
    }
}

int DeviceRunner::transportChunkCount() const noexcept {
    return m_transport != nullptr ? m_transport->chunkCount() : 0;
}

void DeviceRunner::setTraceDecode(bool on) {
    m_decode = on;
    m_decoder = on ? std::make_unique<FrameDecoder>(m_config.frame) : nullptr;
}

void DeviceRunner::enableFlowControl(bool on) {
    m_gate.setEnabled(on);
}

void DeviceRunner::ackFlush() {
    m_gate.acked();
}

void DeviceRunner::startCapture(quint64 token, const CaptureSettings& settings) {
    if (!ready(token)) {
        return;
    }
    collectFrames(); // chunks before the start belong to no capture
    QString why;
    if (!m_capture.start(settings, &why)) {
        refuse(token, why);
        return;
    }
    answer(token, mc::Error{}, 0);
    emitCaptureStatus();
}

void DeviceRunner::stopCapture(quint64 token) {
    if (!ready(token)) {
        return;
    }
    collectFrames();
    m_capture.stop();
    answer(token, mc::Error{}, 0);
    emitCaptureStatus();
}

void DeviceRunner::discardCapture(quint64 token) {
    if (!ready(token)) {
        return;
    }
    m_capture.discard();
    answer(token, mc::Error{}, 0);
    emitCaptureStatus();
}

void DeviceRunner::saveCapture(quint64 token, const CaptureSaveRequest& request) {
    QString why;
    const CaptureController::SaveJob job = m_capture.prepareSave(token, request, &why);
    if (!job) {
        CaptureSaveResult result;
        result.token = token;
        result.ok = false;
        result.message = why;
        emit captureSaved(result);
        return;
    }
    // The files are written on a short-lived thread: the device on this thread keeps polling.
    QThread* worker = QThread::create([self = QPointer<DeviceRunner>(this), job]() {
        const CaptureSaveResult result = job();
        QMetaObject::invokeMethod(
            self.data(), [self, result]() { if (self) { emit self->captureSaved(result); } }, Qt::QueuedConnection);
    });
    m_saveWorkers.push_back(worker);
    connect(worker, &QThread::finished, this, [this, worker]() {
        m_saveWorkers.removeOne(worker);
        worker->deleteLater();
    });
    worker->start();
}

bool DeviceRunner::ready(quint64 token) {
    if (hasFailed()) {
        refuse(token, QStringLiteral("the runner stopped after an error"));
        return false;
    }
    return true;
}

void DeviceRunner::refuse(quint64 token, const QString& why) {
    CommandResult result;
    result.token = token;
    result.ok = false;
    result.message = why;
    emit commandDone(result);
}

void DeviceRunner::answer(quint64 token, const mc::Error& error, quint64 value) {
    CommandResult result;
    result.token = token;
    result.ok = error.ok();
    result.errorCode = static_cast<int>(error.code);
    result.message = error.ok() ? QString() : QString::fromUtf8(error.message);
    result.value = value;
    emit commandDone(result);
}

void DeviceRunner::applyConfig(quint64 token, const mc::McDeviceConfig& cfg) {
    if (!ready(token)) {
        return;
    }
    if (m_device->linkState() != mc::LinkState::Disconnected) {
        refuse(token, QStringLiteral("the configuration can only change while disconnected"));
        return;
    }
    QString where;
    const mc::Expected<void> valid = cfg.validate(&where);
    if (!valid) {
        CommandResult result;
        result.token = token;
        result.ok = false;
        result.errorCode = static_cast<int>(valid.error().code);
        result.message = QStringLiteral("%1 (%2)").arg(QString::fromUtf8(valid.error().message), where);
        emit commandDone(result);
        return;
    }
    flushNow(true);
    if (m_capture.active()) {
        // The frame settings may change: what was recorded so far stays, but is a capture of the old
        // configuration.
        collectFrames();
        m_capture.stop();
        emitCaptureStatus();
    }
    buildDevice(cfg);
    answer(token, mc::Error{}, 0);
}

void DeviceRunner::connectToPlc() {
    if (hasFailed()) {
        return;
    }
    m_flushTimer->start();
    m_device->connectToPlc();
}

void DeviceRunner::disconnectFromPlc() {
    if (hasFailed()) {
        return;
    }
    m_device->disconnectFromPlc();
}

void DeviceRunner::subscribe(quint64 token, const QString& device, quint32 count) {
    if (!ready(token)) {
        return;
    }
    const mc::Expected<mc::SubscriptionId> id = m_device->subscribe(QStringView(device), count);
    if (id) {
        answer(token, mc::Error{}, id.value());
    } else {
        answer(token, id.error(), 0);
    }
}

void DeviceRunner::unsubscribe(quint64 token, quint32 id) {
    if (!ready(token)) {
        return;
    }
    const mc::Expected<void> result = m_device->unsubscribe(id);
    answer(token, result ? mc::Error{} : result.error(), 0);
}

void DeviceRunner::readWords(quint64 token, const QString& head, quint16 count) {
    if (!ready(token)) {
        return;
    }
    const mc::Expected<mc::RequestId> id = m_device->readWords(QStringView(head), count);
    answer(token, id ? mc::Error{} : id.error(), id ? id.value() : 0);
}

void DeviceRunner::readBits(quint64 token, const QString& head, quint16 count) {
    if (!ready(token)) {
        return;
    }
    const mc::Expected<mc::RequestId> id = m_device->readBits(QStringView(head), count);
    answer(token, id ? mc::Error{} : id.error(), id ? id.value() : 0);
}

void DeviceRunner::writeWords(quint64 token, const QString& head, const QVector<quint16>& values) {
    if (!ready(token)) {
        return;
    }
    const mc::Expected<mc::RequestId> id = m_device->writeWords(QStringView(head), values);
    answer(token, id ? mc::Error{} : id.error(), id ? id.value() : 0);
}

void DeviceRunner::writeBits(quint64 token, const QString& head, const QVector<bool>& values) {
    if (!ready(token)) {
        return;
    }
    const mc::Expected<mc::RequestId> id = m_device->writeBits(QStringView(head), values);
    answer(token, id ? mc::Error{} : id.error(), id ? id.value() : 0);
}

void DeviceRunner::setLogLevel(mc::LogLevel level) {
    m_log.setLevel(level);
}

void DeviceRunner::reportThreads() {
    ThreadReport report;
    report.runnerThread = identity(QThread::currentThread());
    report.objectThread = identity(thread());
    report.deviceThread = identity(m_device != nullptr ? m_device->thread() : nullptr);
    report.transportThread = identity(m_transport != nullptr ? m_transport->thread() : nullptr);
    report.currentThread = identity(QThread::currentThread());
    emit threadReport(report);
}

void DeviceRunner::shutdown() {
    if (m_device != nullptr) {
        m_device->disconnectFromPlc();
        flushNow(true);
    }
    m_flushTimer->stop();
    destroyDevice();
}

void DeviceRunner::stopAfterFailure() noexcept {
    try {
        if (m_device != nullptr) {
            m_device->disconnectFromPlc();
        }
        m_flushTimer->stop();
        flushNow(true);
    } catch (...) {
        // The failure itself is reported by RunnerBase; there is nothing left to stop.
    }
}

} // namespace mc::workbench
