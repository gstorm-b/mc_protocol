#include "mc_workbench/mock_host.h"

#include "mc_workbench/mock_runner.h"

#include <QPointer>

#include <utility>

namespace mc::workbench {

MockHost::MockHost(const QString& name, const mc::FrameConfig& frame, QObject* parent)
    : MockHost(name, frame, MockSettings{}, parent) {}

MockHost::MockHost(const QString& name, const mc::FrameConfig& frame, const MockSettings& settings,
                   QObject* parent)
    : QObject(parent) {
    registerMockMetaTypes();
    QPointer<MockHost> self(this);
    // Runs on the runner thread: the runner, the mock and the server are born there.
    m_thread = new RunnerThread(name, [frame, settings, self]() -> RunnerBase* {
        auto* runner = new MockRunner(frame, settings);
        if (!self.isNull()) {
            MockHost* host = self.data();
            const auto queued = Qt::QueuedConnection;
            QObject::connect(runner, &MockRunner::listening, host, &MockHost::listening, queued);
            QObject::connect(runner, &MockRunner::clientConnected, host,
                             &MockHost::clientConnected, queued);
            QObject::connect(runner, &MockRunner::clientDisconnected, host,
                             &MockHost::clientDisconnected, queued);
            QObject::connect(runner, &MockRunner::servingChanged, host, &MockHost::servingChanged,
                             queued);
            QObject::connect(runner, &MockRunner::serialError, host, &MockHost::serialError,
                             queued);
            QObject::connect(runner, &MockRunner::statsChanged, host, &MockHost::statsChanged,
                             queued);
            QObject::connect(runner, &MockRunner::requestsLogged, host, &MockHost::requestsLogged,
                             queued);
            QObject::connect(runner, &MockRunner::logBatch, host, &MockHost::logBatch, queued);
            QObject::connect(runner, &MockRunner::memoryRead, host, &MockHost::memoryRead, queued);
            QObject::connect(runner, &MockRunner::commandDone, host, &MockHost::commandDone,
                             queued);
            QObject::connect(runner, &MockRunner::threadReport, host, &MockHost::threadReport,
                             queued);
            QObject::connect(runner, &MockRunner::framesBatch, host, &MockHost::framesBatch,
                             queued);
            QObject::connect(runner, &MockRunner::framesDropped, host, &MockHost::framesDropped,
                             queued);
            QObject::connect(runner, &MockRunner::captureStatusChanged, host,
                             &MockHost::captureStatusChanged, queued);
            QObject::connect(runner, &MockRunner::captureSaved, host, &MockHost::captureSaved,
                             queued);
            // The last signal of a batch: once it arrives here every batch before it has been
            // consumed, and the acknowledgement lets the runner emit the next one.
            QObject::connect(runner, &MockRunner::flushed, host, &MockHost::onFlushed, queued);
            runner->enableFlowControl(true);
        }
        return runner;
    });
    connect(m_thread, &RunnerThread::failed, this, &MockHost::failed);
}

MockHost::~MockHost() {
    delete m_thread; // stops and joins within the bound
}

void MockHost::post(std::function<void(MockRunner&)> command) {
    m_thread->post([command = std::move(command)](RunnerBase& base) {
        command(static_cast<MockRunner&>(base));
    });
}

quint64 MockHost::reconfigure(const mc::FrameConfig& frame, const MockSettings& settings) {
    const quint64 token = nextToken();
    post([token, frame, settings](MockRunner& r) { r.reconfigure(token, frame, settings); });
    return token;
}

quint64 MockHost::listen(quint16 port) {
    const quint64 token = nextToken();
    post([token, port](MockRunner& r) { r.listen(token, port); });
    return token;
}

quint64 MockHost::openSerial(const SerialLine& line) {
    const quint64 token = nextToken();
    post([token, line](MockRunner& r) { r.openSerial(token, line); });
    return token;
}

void MockHost::stopListening() {
    post([](MockRunner& r) { r.stopListening(); });
}

quint64 MockHost::setWords(const QString& head, const QVector<quint16>& values) {
    const quint64 token = nextToken();
    post([token, head, values](MockRunner& r) { r.setWords(token, head, values); });
    return token;
}

quint64 MockHost::setBits(const QString& head, const QVector<bool>& values) {
    const quint64 token = nextToken();
    post([token, head, values](MockRunner& r) { r.setBits(token, head, values); });
    return token;
}

quint64 MockHost::readMemory(const QString& head, quint16 count, bool bits) {
    const quint64 token = nextToken();
    post([token, head, count, bits](MockRunner& r) { r.readMemory(token, head, count, bits); });
    return token;
}

void MockHost::mute(bool on) {
    post([on](MockRunner& r) { r.mute(on); });
}

quint64 MockHost::muteNext(quint32 count) {
    const quint64 token = nextToken();
    post([token, count](MockRunner& r) { r.muteNext(token, count); });
    return token;
}

quint64 MockHost::corruptNext(mc::Corruption mode, quint32 count) {
    const quint64 token = nextToken();
    post([token, mode, count](MockRunner& r) { r.corruptNext(token, mode, count); });
    return token;
}

quint64 MockHost::failRange(mc::DeviceType type, quint32 first, quint32 last, quint16 code,
                            quint8 abnormal) {
    const quint64 token = nextToken();
    post([=](MockRunner& r) { r.failRange(token, type, first, last, code, abnormal); });
    return token;
}

quint64 MockHost::clearFaults() {
    const quint64 token = nextToken();
    post([token](MockRunner& r) { r.clearFaults(token); });
    return token;
}

quint64 MockHost::setDeviceLimit(mc::DeviceType type, quint32 limit) {
    const quint64 token = nextToken();
    post([token, type, limit](MockRunner& r) { r.setDeviceLimit(token, type, limit); });
    return token;
}

void MockHost::setLogLevel(mc::LogLevel level) {
    post([level](MockRunner& r) { r.setLogLevel(level); });
}

void MockHost::onFlushed() {
    post([](MockRunner& r) { r.ackFlush(); });
}

void MockHost::setTraceDecode(bool on) {
    post([on](MockRunner& r) { r.setTraceDecode(on); });
}

quint64 MockHost::startCapture(const CaptureSettings& settings) {
    const quint64 token = nextToken();
    post([token, settings](MockRunner& r) { r.startCapture(token, settings); });
    return token;
}

quint64 MockHost::stopCapture() {
    const quint64 token = nextToken();
    post([token](MockRunner& r) { r.stopCapture(token); });
    return token;
}

quint64 MockHost::discardCapture() {
    const quint64 token = nextToken();
    post([token](MockRunner& r) { r.discardCapture(token); });
    return token;
}

quint64 MockHost::saveCapture(const CaptureSaveRequest& request) {
    const quint64 token = nextToken();
    post([token, request](MockRunner& r) { r.saveCapture(token, request); });
    return token;
}

void MockHost::requestThreadReport() {
    post([](MockRunner& r) { r.reportThreads(); });
}

} // namespace mc::workbench
