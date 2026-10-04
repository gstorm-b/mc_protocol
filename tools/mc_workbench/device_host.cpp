#include "mc_workbench/device_host.h"

#include "mc_workbench/device_runner.h"

#include <QPointer>

#include <utility>

namespace mc::workbench {

DeviceHost::DeviceHost(const QString& name, const mc::McDeviceConfig& cfg, QObject* parent)
    : QObject(parent) {
    registerRunnerMetaTypes();
    QPointer<DeviceHost> self(this);
    // Runs on the runner thread: the runner and the device are born there. Connecting from that
    // thread to a GUI-thread receiver is safe; the signals then travel as queued calls.
    m_thread = new RunnerThread(
        name,
        [cfg, self]() -> RunnerBase* {
            auto* runner = new DeviceRunner(cfg);
            if (!self.isNull()) {
                DeviceHost* host = self.data();
                const auto queued = Qt::QueuedConnection;
                QObject::connect(runner, &DeviceRunner::linkStateChanged, host,
                                 &DeviceHost::linkStateChanged, queued);
                QObject::connect(runner, &DeviceRunner::linkFault, host, &DeviceHost::linkFault,
                                 queued);
                QObject::connect(runner, &DeviceRunner::valuesBatch, host,
                                 &DeviceHost::valuesBatch, queued);
                QObject::connect(runner, &DeviceRunner::framesBatch, host,
                                 &DeviceHost::framesBatch, queued);
                QObject::connect(runner, &DeviceRunner::logBatch, host, &DeviceHost::logBatch,
                                 queued);
                QObject::connect(runner, &DeviceRunner::cycleDone, host, &DeviceHost::cycleDone,
                                 queued);
                QObject::connect(runner, &DeviceRunner::requestFinished, host,
                                 &DeviceHost::requestFinished, queued);
                QObject::connect(runner, &DeviceRunner::commandDone, host,
                                 &DeviceHost::commandDone, queued);
                QObject::connect(runner, &DeviceRunner::threadReport, host,
                                 &DeviceHost::threadReport, queued);
                QObject::connect(runner, &DeviceRunner::framesDropped, host,
                                 &DeviceHost::framesDropped, queued);
                QObject::connect(runner, &DeviceRunner::captureStatusChanged, host,
                                 &DeviceHost::captureStatusChanged, queued);
                QObject::connect(runner, &DeviceRunner::captureSaved, host,
                                 &DeviceHost::captureSaved, queued);
                // The last signal of a batch: once it arrives here every batch before it has been
                // consumed, and the acknowledgement lets the runner emit the next one.
                QObject::connect(runner, &DeviceRunner::flushed, host, &DeviceHost::onFlushed,
                                 queued);
                runner->enableFlowControl(true);
            }
            return runner;
        });
    connect(m_thread, &RunnerThread::failed, this, &DeviceHost::failed);
}

DeviceHost::~DeviceHost() {
    delete m_thread; // stops and joins within the bound
}

void DeviceHost::post(std::function<void(DeviceRunner&)> command) {
    m_thread->post([command = std::move(command)](RunnerBase& base) {
        command(static_cast<DeviceRunner&>(base));
    });
}

quint64 DeviceHost::applyConfig(const mc::McDeviceConfig& cfg) {
    const quint64 token = nextToken();
    post([token, cfg](DeviceRunner& r) { r.applyConfig(token, cfg); });
    return token;
}

void DeviceHost::connectToPlc() {
    post([](DeviceRunner& r) { r.connectToPlc(); });
}

void DeviceHost::disconnectFromPlc() {
    post([](DeviceRunner& r) { r.disconnectFromPlc(); });
}

quint64 DeviceHost::subscribe(const QString& device, quint32 count) {
    const quint64 token = nextToken();
    post([token, device, count](DeviceRunner& r) { r.subscribe(token, device, count); });
    return token;
}

quint64 DeviceHost::unsubscribe(quint32 id) {
    const quint64 token = nextToken();
    post([token, id](DeviceRunner& r) { r.unsubscribe(token, id); });
    return token;
}

quint64 DeviceHost::readWords(const QString& head, quint16 count) {
    const quint64 token = nextToken();
    post([token, head, count](DeviceRunner& r) { r.readWords(token, head, count); });
    return token;
}

quint64 DeviceHost::readBits(const QString& head, quint16 count) {
    const quint64 token = nextToken();
    post([token, head, count](DeviceRunner& r) { r.readBits(token, head, count); });
    return token;
}

quint64 DeviceHost::writeWords(const QString& head, const QVector<quint16>& values) {
    const quint64 token = nextToken();
    post([token, head, values](DeviceRunner& r) { r.writeWords(token, head, values); });
    return token;
}

quint64 DeviceHost::writeBits(const QString& head, const QVector<bool>& values) {
    const quint64 token = nextToken();
    post([token, head, values](DeviceRunner& r) { r.writeBits(token, head, values); });
    return token;
}

void DeviceHost::setLogLevel(mc::LogLevel level) {
    post([level](DeviceRunner& r) { r.setLogLevel(level); });
}

void DeviceHost::onFlushed() {
    post([](DeviceRunner& r) { r.ackFlush(); });
}

void DeviceHost::setTraceDecode(bool on) {
    post([on](DeviceRunner& r) { r.setTraceDecode(on); });
}

quint64 DeviceHost::startCapture(const CaptureSettings& settings) {
    const quint64 token = nextToken();
    post([token, settings](DeviceRunner& r) { r.startCapture(token, settings); });
    return token;
}

quint64 DeviceHost::stopCapture() {
    const quint64 token = nextToken();
    post([token](DeviceRunner& r) { r.stopCapture(token); });
    return token;
}

quint64 DeviceHost::discardCapture() {
    const quint64 token = nextToken();
    post([token](DeviceRunner& r) { r.discardCapture(token); });
    return token;
}

quint64 DeviceHost::saveCapture(const CaptureSaveRequest& request) {
    const quint64 token = nextToken();
    post([token, request](DeviceRunner& r) { r.saveCapture(token, request); });
    return token;
}

void DeviceHost::requestThreadReport() {
    post([](DeviceRunner& r) { r.reportThreads(); });
}

} // namespace mc::workbench
