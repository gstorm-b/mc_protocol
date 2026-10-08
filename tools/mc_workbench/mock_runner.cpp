#include "mc_workbench/mock_runner.h"

#include "mc/core/device.h"
#include "mc_workbench/capture_export.h"

#include <QHostAddress>
#include <QMetaObject>
#include <QSerialPort>
#include <QTcpServer>
#include <QTcpSocket>
#include <QPointer>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <string_view>

namespace mc::workbench {

namespace {
// How long a destructor waits for a file-writing thread (the stop bound of RunnerThread).
constexpr int kWorkerBoundMs = 3000;
} // namespace

namespace {

quintptr identity(const QThread* thread) {
    return reinterpret_cast<quintptr>(thread);
}

const char* opName(mc::Op op) {
    switch (op) {
    case mc::Op::ReadBits:
        return "ReadBits";
    case mc::Op::ReadWords:
        return "ReadWords";
    case mc::Op::WriteBits:
        return "WriteBits";
    case mc::Op::WriteWords:
        return "WriteWords";
    }
    return "?";
}

QString deviceText(const mc::Device& device, mc::XyNumbering xy) {
    char buffer[16];
    const size_t size = mc::formatDevice(device, buffer, sizeof(buffer), xy);
    return QString::fromLatin1(buffer, static_cast<QString::size_type>(size));
}

QChar parityLetter(int parity) {
    switch (parity) {
    case QSerialPort::NoParity:
        return QLatin1Char('N');
    case QSerialPort::EvenParity:
        return QLatin1Char('E');
    case QSerialPort::OddParity:
        return QLatin1Char('O');
    case QSerialPort::SpaceParity:
        return QLatin1Char('S');
    case QSerialPort::MarkParity:
        return QLatin1Char('M');
    default:
        return QLatin1Char('?');
    }
}

QString lineText(const SerialLine& line) {
    const QString stop = line.stopBits == QSerialPort::OneAndHalfStop
                             ? QStringLiteral("1.5")
                             : QString::number(line.stopBits == QSerialPort::TwoStop ? 2 : 1);
    return QStringLiteral("%1 %2 %3%4%5")
        .arg(line.portName)
        .arg(line.baudRate)
        .arg(line.dataBits)
        .arg(parityLetter(line.parity))
        .arg(stop);
}

} // namespace

MockRunner::MockRunner(const mc::FrameConfig& frame, QObject* parent)
    : MockRunner(frame, MockSettings{}, parent) {}

MockRunner::MockRunner(const mc::FrameConfig& frame, const MockSettings& settings, QObject* parent)
    : RunnerBase(parent), m_clock(std::make_shared<mc::hil::RecordingClock>()), m_log(m_clock),
      m_frame(frame), m_settings(settings),
      m_plc(std::make_unique<mc::MockPlc>(frame, settings.toOptions(&m_log))),
      m_plcThread(QThread::currentThread()), m_server(new QTcpServer(this)),
      m_flushTimer(new QTimer(this)) {
    registerMockMetaTypes();
    m_flushTimer->setSingleShot(true);
    m_flushTimer->setInterval(kFlushIntervalMs);
    connect(m_flushTimer, &QTimer::timeout, this, [this]() {
        guarded("flush", [this]() { flushNow(); });
    });
    connect(m_server, &QTcpServer::newConnection, this, [this]() {
        guarded("newConnection", [this]() { onNewConnection(); });
    });
    m_decoder = std::make_unique<FrameDecoder>(m_frame);
    configureCapture();
}

MockRunner::~MockRunner() {
    closeServer();
    closeSerial(QString());
    m_plc.reset();
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

void MockRunner::configureCapture() {
    mc::McDeviceConfig target;
    target.frame = m_frame;
    target.transport = m_frame.isSerial() ? mc::TransportKind::Serial : mc::TransportKind::Tcp;
    m_capture.setTarget(target, true); // a mock is never a real PLC
}

void MockRunner::buildPlc() {
    m_plc = std::make_unique<mc::MockPlc>(m_frame, m_settings.toOptions(&m_log));
    m_requests = 0;
    m_skippedBytes = 0;
    m_statsDirty = true;
    m_pendingFrames.clear();
    m_pendingFrameBytes = 0;
    m_pendingRequestsDropped = 0;
    m_links.clear(); // no client is connected while the configuration changes
    m_decoder = m_decode ? std::make_unique<FrameDecoder>(m_frame) : nullptr;
    configureCapture();
}

bool MockRunner::isServing() const {
    return m_server->isListening() || m_serial != nullptr;
}

void MockRunner::announceServing(bool serving, const QString& what) {
    emit servingChanged(serving, what);
}

void MockRunner::onNewConnection() {
    while (m_server->hasPendingConnections()) {
        QTcpSocket* socket = m_server->nextPendingConnection();
        if (m_clients.size() >= kMaxClients) {
            socket->abort();
            socket->deleteLater();
            continue;
        }
        m_clients.push_back(socket);
        ClientLink& link = m_links[socket];
        link.stream = m_plc->openStream();
        if (m_decode) {
            link.decoder = std::make_unique<FrameDecoder>(m_frame);
        }
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            guarded("readyRead", [this, socket]() { onReadyRead(socket); });
        });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            guarded("disconnected", [this, socket]() {
                if (m_clients.removeOne(socket)) {
                    releaseLink(socket);
                    m_statsDirty = true;
                    emit clientDisconnected();
                    flushNow(true);
                }
                socket->deleteLater();
            });
        });
        m_statsDirty = true;
        emit clientConnected(QStringLiteral("%1:%2")
                                 .arg(socket->peerAddress().toString())
                                 .arg(socket->peerPort()));
        flushNow(true);
    }
}

void MockRunner::onReadyRead(QIODevice* io) {
    if (io == nullptr) {
        return;
    }
    // A TCP client has its own stream and decoder; the COM port is stream 0.
    mc::MockStreamId stream = 0;
    FrameDecoder* decoder = m_decoder.get();
    if (io != m_serial) {
        const auto link = m_links.find(static_cast<QTcpSocket*>(io));
        if (link == m_links.end()) {
            return;
        }
        stream = link->second.stream;
        decoder = link->second.decoder.get();
    }
    const QByteArray request = io->readAll();
    collectFrame(m_clock->nowNs(), true, request, decoder);
    m_plc->bytesIn(stream, mc::ByteView{reinterpret_cast<const uint8_t*>(request.constData()),
                                        static_cast<size_t>(request.size())});
    mc::ByteView response;
    while (m_plc->nextResponse(stream, response)) {
        io->write(reinterpret_cast<const char*>(response.data), static_cast<qint64>(response.size));
        collectFrame(m_clock->nowNs(), false,
                     QByteArray(reinterpret_cast<const char*>(response.data),
                                static_cast<QByteArray::size_type>(response.size)),
                     decoder);
    }
    m_statsDirty = true;
    if (!m_flushTimer->isActive()) {
        m_flushTimer->start();
    }
}

// Closes the input stream of a client that is going away and forgets its decoder.
void MockRunner::releaseLink(QTcpSocket* socket) {
    const auto it = m_links.find(socket);
    if (it != m_links.end()) {
        m_plc->closeStream(it->second.stream);
        m_links.erase(it);
    }
}

void MockRunner::dropClients() {
    const QVector<QTcpSocket*> old = m_clients;
    m_clients.clear(); // a disconnected() that still arrives finds nothing to remove
    for (QTcpSocket* socket : old) {
        releaseLink(socket);
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }
}

void MockRunner::closeServer() {
    dropClients();
    if (m_server->isListening()) {
        m_server->close();
    }
}

void MockRunner::closeSerial(const QString& reason) {
    if (m_serial == nullptr) {
        return;
    }
    QSerialPort* port = m_serial;
    m_serial = nullptr;
    port->disconnect(this);
    if (port->isOpen()) {
        port->close();
    }
    delete port; // a child of this object, on its own thread
    if (!reason.isEmpty()) {
        emit serialError(reason);
    }
}

void MockRunner::collectFrame(qint64 tNs, bool tx, const QByteArray& bytes,
                              FrameDecoder* decoder) {
    FrameRecord record;
    record.tNs = tNs;
    record.tx = tx;
    record.bytes = bytes;
    if (decoder != nullptr) {
        decoder->annotate(record);
    }
    m_capture.add(record);
    if (m_pendingFrames.size() >= kMaxPendingFrames ||
        m_pendingFrameBytes + record.bytes.size() > kMaxPendingFrameBytes) {
        ++m_framesDropped;
        m_dropsChanged = true;
        return;
    }
    m_pendingFrameBytes += record.bytes.size();
    m_pendingFrames.push_back(std::move(record));
}

void MockRunner::flushNow(bool force) {
    m_flushTimer->stop();
    // The request log is cleared here so that it stays bounded; the totals are kept in
    // m_requests and m_skippedBytes, and the entries go out in a bounded batch.
    const std::vector<mc::MockRequestRecord>& records = m_plc->requests();
    if (!force && !m_gate.canEmit()) {
        // The GUI has not consumed the last batch: keep collecting, in bounded queues. The
        // request log of the mock is bounded here (older entries are only counted).
        m_flushWanted = true;
        const size_t seen = records.size();
        if (seen > static_cast<size_t>(kMaxPendingRequests)) {
            m_requests += seen;
            m_pendingRequestsDropped += static_cast<quint32>(seen);
            m_skippedBytes += m_plc->skippedBytes();
            m_plc->clearLog();
            m_statsDirty = true;
        }
        return;
    }
    m_flushWanted = false;
    bool any = false;
    const uint64_t skipped = m_plc->skippedBytes();
    if (!records.empty() || skipped != 0 || m_pendingRequestsDropped != 0) {
        MockRequestBatch batch;
        const size_t seen = records.size();
        const size_t kept = std::min<size_t>(seen, kMaxRequestsPerBatch);
        batch.dropped = static_cast<quint32>(seen - kept) + m_pendingRequestsDropped;
        m_pendingRequestsDropped = 0;
        batch.entries.reserve(static_cast<QVector<MockRequestEntry>::size_type>(kept));
        const qint64 now = m_clock->nowNs();
        for (size_t i = seen - kept; i < seen; ++i) {
            const mc::MockRequestRecord& record = records[i];
            MockRequestEntry entry;
            entry.seq = m_requests + i + 1;
            entry.tNs = now;
            entry.op = QString::fromLatin1(opName(record.op));
            entry.head = deviceText(record.head, m_frame.xyNotation);
            entry.count = record.count;
            entry.answered = record.answered;
            entry.errorCode = static_cast<int>(record.answeredWith.code);
            entry.plcCode = record.answeredWith.plcCode;
            entry.message = record.answeredWith.ok()
                                ? QString()
                                : QString::fromUtf8(record.answeredWith.message);
            batch.entries.push_back(std::move(entry));
        }
        m_requests += seen;
        m_skippedBytes += skipped;
        m_plc->clearLog();
        m_statsDirty = true;
        if (!batch.entries.isEmpty() || batch.dropped != 0) {
            emit requestsLogged(batch);
            any = true;
        }
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
            m_flushWanted = true; // the rest goes out at the next tick
        }
        emit framesBatch(frames);
        any = true;
    }
    if (m_dropsChanged) {
        m_dropsChanged = false;
        emit framesDropped(m_framesDropped);
        any = true;
    }
    if (m_statsDirty) {
        m_statsDirty = false;
        MockStats stats;
        stats.requests = m_requests;
        stats.eotCount = m_plc->eotCount();
        stats.skippedBytes = m_skippedBytes;
        stats.clients = static_cast<int>(m_clients.size());
        emit statsChanged(stats);
        any = true;
    }
    if (m_log.hasPending()) {
        emit logBatch(m_log.take());
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
    if (m_flushWanted && !m_flushTimer->isActive()) {
        m_flushTimer->start();
    }
}

void MockRunner::setTraceDecode(bool on) {
    m_decode = on;
    m_decoder = on ? std::make_unique<FrameDecoder>(m_frame) : nullptr;
    for (auto& entry : m_links) {
        entry.second.decoder = on ? std::make_unique<FrameDecoder>(m_frame) : nullptr;
    }
}

void MockRunner::enableFlowControl(bool on) {
    m_gate.setEnabled(on);
}

void MockRunner::ackFlush() {
    m_gate.acked();
    if (m_flushWanted && !m_flushTimer->isActive()) {
        m_flushTimer->start();
    }
}

void MockRunner::emitCaptureStatus() {
    m_capture.takeChanged();
    emit captureStatusChanged(m_capture.status());
}

void MockRunner::startCapture(quint64 token, const CaptureSettings& settings) {
    if (!ready(token)) {
        return;
    }
    QString why;
    if (!m_capture.start(settings, &why)) {
        answer(token, false, why, 0);
        return;
    }
    answer(token, true, QString(), 0);
    emitCaptureStatus();
}

void MockRunner::stopCapture(quint64 token) {
    if (!ready(token)) {
        return;
    }
    m_capture.stop();
    answer(token, true, QString(), 0);
    emitCaptureStatus();
}

void MockRunner::discardCapture(quint64 token) {
    if (!ready(token)) {
        return;
    }
    m_capture.discard();
    answer(token, true, QString(), 0);
    emitCaptureStatus();
}

void MockRunner::saveCapture(quint64 token, const CaptureSaveRequest& request) {
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
    // The files are written on a short-lived thread: the mock on this thread keeps serving.
    QThread* worker = QThread::create([self = QPointer<MockRunner>(this), job]() {
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

bool MockRunner::ready(quint64 token) {
    if (hasFailed()) {
        answer(token, false, QStringLiteral("the runner stopped after an error"), 0);
        return false;
    }
    return true;
}

void MockRunner::answer(quint64 token, bool ok, const QString& message, quint64 value) {
    CommandResult result;
    result.token = token;
    result.ok = ok;
    result.message = message;
    result.value = value;
    emit commandDone(result);
}

void MockRunner::reconfigure(quint64 token, const mc::FrameConfig& frame,
                             const MockSettings& settings) {
    if (!ready(token)) {
        return;
    }
    if (isServing()) {
        answer(token, false, QStringLiteral("stop serving before changing the configuration"), 0);
        return;
    }
    const auto valid = frame.validate();
    if (!valid) {
        answer(token, false, QString::fromUtf8(valid.error().message),
               static_cast<quint64>(valid.error().code));
        return;
    }
    if (m_capture.active()) {
        m_capture.stop(); // the frame settings may change: it stays a capture of the old mock
        emitCaptureStatus();
    }
    m_frame = frame;
    m_settings = settings;
    buildPlc();
    flushNow(true);
    answer(token, true, QString(), 0);
}

void MockRunner::listen(quint64 token, quint16 port) {
    if (!ready(token)) {
        return;
    }
    if (isServing()) {
        answer(token, false, QStringLiteral("already serving"), 0);
        return;
    }
    if (!m_server->listen(QHostAddress::LocalHost, port)) {
        answer(token, false, m_server->errorString(), 0);
        return;
    }
    answer(token, true, QString(), m_server->serverPort());
    emit listening(m_server->serverPort());
    announceServing(true, QStringLiteral("tcp 127.0.0.1:%1").arg(m_server->serverPort()));
}

void MockRunner::openSerial(quint64 token, const SerialLine& line) {
    if (!ready(token)) {
        return;
    }
    if (isServing()) {
        answer(token, false, QStringLiteral("already serving"), 0);
        return;
    }
    if (line.portName.isEmpty() || line.baudRate <= 0) {
        answer(token, false, QStringLiteral("a COM port name and a positive baud rate are needed"), 0);
        return;
    }
    auto* port = new QSerialPort(this);
    port->setPortName(line.portName);
    port->setBaudRate(line.baudRate);
    port->setDataBits(static_cast<QSerialPort::DataBits>(line.dataBits));
    port->setParity(static_cast<QSerialPort::Parity>(line.parity));
    port->setStopBits(static_cast<QSerialPort::StopBits>(line.stopBits));
    port->setFlowControl(QSerialPort::NoFlowControl);
    if (!port->open(QIODevice::ReadWrite)) {
        const QString why = port->errorString();
        delete port;
        answer(token, false, why, 0);
        return;
    }
    port->clear();
    m_serial = port;
    connect(port, &QSerialPort::readyRead, this, [this, port]() {
        guarded("serialReadyRead", [this, port]() { onReadyRead(port); });
    });
    connect(port, &QSerialPort::errorOccurred, this, [this, port](QSerialPort::SerialPortError error) {
        guarded("serialError", [this, port, error]() {
            if (port != m_serial) {
                return;
            }
            if (error == QSerialPort::ResourceError || error == QSerialPort::PermissionError ||
                error == QSerialPort::DeviceNotFoundError) {
                const QString message = port->errorString();
                closeSerial(message);
                announceServing(false, QString());
            }
        });
    });
    answer(token, true, QString(), 0);
    announceServing(true, lineText(line));
}

void MockRunner::stopListening() {
    if (hasFailed()) {
        return;
    }
    const bool wasServing = isServing();
    closeServer();
    closeSerial(QString());
    m_statsDirty = true;
    flushNow(true);
    if (wasServing) {
        announceServing(false, QString());
    }
}

void MockRunner::setWords(quint64 token, const QString& head, const QVector<quint16>& values) {
    if (!ready(token)) {
        return;
    }
    const QByteArray text = head.toLatin1();
    const auto parsed = mc::parseDevice(std::string_view(text.constData(), static_cast<size_t>(text.size())),
                                        m_frame.xyNotation);
    if (!parsed) {
        answer(token, false, QString::fromUtf8(parsed.error().message), 0);
        return;
    }
    mc::Device device = parsed.value();
    for (quint16 value : values) {
        m_plc->setWord(device, value);
        ++device.number;
    }
    answer(token, true, QString(), 0);
}

void MockRunner::setBits(quint64 token, const QString& head, const QVector<bool>& values) {
    if (!ready(token)) {
        return;
    }
    const QByteArray text = head.toLatin1();
    const auto parsed = mc::parseDevice(std::string_view(text.constData(), static_cast<size_t>(text.size())),
                                        m_frame.xyNotation);
    if (!parsed) {
        answer(token, false, QString::fromUtf8(parsed.error().message), 0);
        return;
    }
    mc::Device device = parsed.value();
    for (bool value : values) {
        m_plc->setBit(device, value);
        ++device.number;
    }
    answer(token, true, QString(), 0);
}

void MockRunner::readMemory(quint64 token, const QString& head, quint16 count, bool bits) {
    if (!ready(token)) {
        return;
    }
    if (count == 0 || count > kMaxMemoryPoints) {
        answer(token, false, QStringLiteral("count must be 1 to %1").arg(kMaxMemoryPoints), 0);
        return;
    }
    const QByteArray text = head.toLatin1();
    const auto parsed = mc::parseDevice(std::string_view(text.constData(), static_cast<size_t>(text.size())),
                                        m_frame.xyNotation);
    if (!parsed) {
        answer(token, false, QString::fromUtf8(parsed.error().message), 0);
        return;
    }
    MemoryBlock block;
    block.token = token;
    block.head = head;
    block.bits = bits;
    block.values.reserve(count);
    mc::Device device = parsed.value();
    for (quint16 i = 0; i < count; ++i) {
        block.values.push_back(bits ? static_cast<quint16>(m_plc->bit(device) ? 1 : 0)
                                    : m_plc->word(device));
        ++device.number;
    }
    emit memoryRead(block);
    answer(token, true, QString(), count);
}

void MockRunner::mute(bool on) {
    if (!hasFailed()) {
        m_plc->mute(on);
    }
}

void MockRunner::muteNext(quint64 token, quint32 count) {
    if (!ready(token)) {
        return;
    }
    m_plc->muteNext(count);
    answer(token, true, QString(), count);
}

void MockRunner::corruptNext(quint64 token, mc::Corruption mode, quint32 count) {
    if (!ready(token)) {
        return;
    }
    m_plc->corruptNext(mode, count);
    answer(token, true, QString(), count);
}

void MockRunner::failRange(quint64 token, mc::DeviceType type, quint32 first, quint32 last,
                           quint16 code, quint8 abnormal) {
    if (!ready(token)) {
        return;
    }
    if (first > last) {
        answer(token, false, QStringLiteral("the first device number is above the last"), 0);
        return;
    }
    m_plc->failRange(type, first, last, code, abnormal);
    answer(token, true, QString(), 0);
}

void MockRunner::clearFaults(quint64 token) {
    if (!ready(token)) {
        return;
    }
    m_plc->clearFaults();
    answer(token, true, QString(), 0);
}

void MockRunner::setDeviceLimit(quint64 token, mc::DeviceType type, quint32 limit) {
    if (!ready(token)) {
        return;
    }
    m_plc->setDeviceLimit(type, limit);
    answer(token, true, QString(), 0);
}

void MockRunner::setLogLevel(mc::LogLevel level) {
    m_log.setLevel(level);
}

void MockRunner::reportThreads() {
    ThreadReport report;
    report.runnerThread = identity(QThread::currentThread());
    report.objectThread = identity(thread());
    report.deviceThread = identity(m_plcThread);
    report.transportThread = identity(m_server->thread());
    report.currentThread = identity(QThread::currentThread());
    emit threadReport(report);
}

void MockRunner::shutdown() {
    closeServer();
    closeSerial(QString());
    flushNow(true);
}

void MockRunner::stopAfterFailure() noexcept {
    try {
        closeServer();
        closeSerial(QString());
        flushNow(true);
    } catch (...) {
        // The failure itself is reported by RunnerBase; there is nothing left to stop.
    }
}

} // namespace mc::workbench
