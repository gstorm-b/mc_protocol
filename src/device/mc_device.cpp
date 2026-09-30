#include "mc/device/mc_device.h"

#include "mc/device/meta_types.h"
#include "mc/device/tcp_transport.h"

#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <limits>
#include <string_view>
#include <utility>

namespace mc {

namespace {

constexpr size_t kReadBufferBytes = 4096; // spec "Pump and signal order": a fixed 4 KiB buffer

Error makeConfigError(const char* message) {
    Error e;
    e.category = ErrorCategory::Config;
    e.code = ErrorCode::InvalidConfig;
    e.message = message;
    return e;
}

QString textOf(const char* message) { return QString::fromUtf8(message); }

ByteView viewOf(const QByteArray& b) {
    return ByteView{reinterpret_cast<const uint8_t*>(b.constData()), static_cast<size_t>(b.size())};
}

QByteArray bytesOf(ByteView v) {
    if (v.size == 0) {
        return QByteArray();
    }
    return QByteArray(reinterpret_cast<const char*>(v.data), static_cast<qsizetype>(v.size));
}

} // namespace

// Forwards the Session's and the device's log lines to whatever setLogSink() installed, so the
// Session can be created once and the sink swapped later.
class McDevice::SinkForwarder final : public LogSink {
  public:
    void setTarget(LogSink* target) { m_target = target; }

    bool enabled(LogLevel level) const noexcept override {
        return m_target != nullptr && m_target->enabled(level);
    }

    void write(LogLevel level, std::string_view category,
               std::string_view message) noexcept override {
        if (m_target != nullptr) {
            m_target->write(level, category, message);
        }
    }

  private:
    LogSink* m_target{nullptr};
};

// Marks one entry into the device (a public call, a transport slot, a timer slot). The outermost
// scope flushes the signal queue when it ends; a nested one only drains and re-arms.
class McDevice::CallScope {
  public:
    explicit CallScope(McDevice& device) : m_device(device) { ++m_device.m_depth; }
    ~CallScope() {
        m_device.finishCall();
        --m_device.m_depth;
    }
    CallScope(const CallScope&) = delete;
    CallScope& operator=(const CallScope&) = delete;

  private:
    McDevice& m_device;
};

McDevice::McDevice(QObject* parent) : McDevice(McDeviceConfig{}, nullptr, parent) {}

McDevice::McDevice(const McDeviceConfig& cfg, QObject* parent) : McDevice(cfg, nullptr, parent) {}

McDevice::McDevice(const McDeviceConfig& cfg, std::unique_ptr<Transport> transport, QObject* parent)
    : QObject(parent), m_config(cfg), m_sink(std::make_unique<SinkForwarder>()),
      m_timer(new QTimer(this)), m_transport(std::move(transport)) {
    registerMetaTypes();
    m_clock.start();
    m_timer->setSingleShot(true);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &McDevice::onTimer);
    if (m_transport) {
        m_transportInjected = true;
        attachTransport();
    }
    rebuildSession();
}

McDevice::~McDevice() {
    // Nothing is emitted from here on: a slot of a queued signal must not run against a device
    // that is being destroyed, and the transport and timer children die silently.
    m_destroying = true;
    m_timer->stop();
    m_signals.clear();
    if (!m_outstanding.isEmpty()) { // a forgotten disconnectFromPlc() leaves this trace
        QStringList ids;
        for (const RequestId id : m_outstanding) {
            ids << QString::number(id);
        }
        logLine(LogLevel::Warn, QStringLiteral("destroyed with %1 outstanding request(s), ids: %2")
                                    .arg(m_outstanding.size())
                                    .arg(ids.join(QStringLiteral(", "))));
    }
    if (m_transport) {
        m_transport->disconnect(this);
        m_transport->close();
    }
}

Expected<void> McDevice::setConfig(const McDeviceConfig& cfg, QString* where) {
    if (m_state != LinkState::Disconnected) {
        if (where != nullptr) {
            where->clear();
        }
        return makeConfigError("McDevice::setConfig requires the Disconnected state");
    }
    McDeviceConfig probe = cfg;
    if (m_transportInjected) {
        probe.transport = TransportKind::Tcp;
        probe.tcp = TcpSettings{};
    }
    const Expected<void> valid = probe.validate(where);
    if (!valid) {
        return valid;
    }
    m_config = cfg;
    if (!m_transportInjected) {
        // Rebuilt from the new settings at the next connectToPlc(). A slot of linkStateChanged()
        // may be calling us from inside a signal of this very transport (OpenFailed, PeerClosed),
        // so it is detached and deleted from the event loop, never here.
        if (m_transport) {
            m_transport->disconnect(this);
            m_transport.release()->deleteLater();
        }
    }
    rebuildSession();
    if (m_configError.code != ErrorCode::Ok) { // Session::create() or a subscription refused it
        if (where != nullptr) {
            *where = m_configWhere;
        }
        return m_configError;
    }
    return {};
}

const McDeviceConfig& McDevice::config() const noexcept { return m_config; }

Expected<void> McDevice::configStatus() const {
    if (m_configError.code == ErrorCode::Ok) {
        return {};
    }
    return m_configError;
}

void McDevice::setLogSink(LogSink* sink) { m_sink->setTarget(sink); }

Expected<Device> McDevice::parseHead(QStringView text) {
    const QByteArray latin1 = text.toLatin1();
    return parseDevice(std::string_view(latin1.constData(), static_cast<size_t>(latin1.size())));
}

Expected<SubscriptionId> McDevice::subscribe(Device head, quint32 count) {
    if (m_destroying || !m_session) {
        return this->configError();
    }
    CallScope scope(*this);
    return m_session->subscribe(head, count);
}

Expected<SubscriptionId> McDevice::subscribe(QStringView device, quint32 count) {
    const Expected<Device> head = parseHead(device);
    if (!head) {
        return head.error();
    }
    return subscribe(head.value(), count);
}

Expected<void> McDevice::unsubscribe(SubscriptionId id) {
    if (m_destroying || !m_session) {
        return this->configError();
    }
    CallScope scope(*this);
    return m_session->unsubscribe(id);
}

Expected<RequestId> McDevice::submit(const Request& r) {
    if (m_destroying || !m_session) {
        return this->configError();
    }
    CallScope scope(*this);
    const Expected<RequestId> id = m_session->submit(r, nowMs());
    if (id) {
        m_outstanding.append(id.value());
    }
    return id;
}

Expected<RequestId> McDevice::writeWords(QStringView head, const QVector<quint16>& values) {
    const Expected<Device> device = parseHead(head);
    if (!device) {
        return device.error();
    }
    if (values.size() > std::numeric_limits<quint16>::max()) {
        Error e;
        e.category = ErrorCategory::Encode;
        e.code = ErrorCode::PointCount;
        e.message = "a write carries at most 65535 points";
        return e;
    }
    QByteArray bytes;
    bytes.resize(values.size() * 2);
    for (qsizetype i = 0; i < values.size(); ++i) {
        bytes[i * 2] = static_cast<char>(values[i] & 0xFF);
        bytes[i * 2 + 1] = static_cast<char>((values[i] >> 8) & 0xFF);
    }
    return submit(Request::writeWords(device.value(), viewOf(bytes)));
}

Expected<RequestId> McDevice::writeBits(QStringView head, const QVector<bool>& values) {
    const Expected<Device> device = parseHead(head);
    if (!device) {
        return device.error();
    }
    if (values.size() > std::numeric_limits<quint16>::max()) {
        Error e;
        e.category = ErrorCategory::Encode;
        e.code = ErrorCode::PointCount;
        e.message = "a write carries at most 65535 points";
        return e;
    }
    QByteArray bytes;
    bytes.resize(values.size());
    for (qsizetype i = 0; i < values.size(); ++i) {
        bytes[i] = static_cast<char>(values[i] ? 1 : 0);
    }
    return submit(Request::writeBits(device.value(), viewOf(bytes)));
}

Expected<RequestId> McDevice::readWords(QStringView head, quint16 count) {
    const Expected<Device> device = parseHead(head);
    if (!device) {
        return device.error();
    }
    return submit(Request::readWords(device.value(), count));
}

Expected<RequestId> McDevice::readBits(QStringView head, quint16 count) {
    const Expected<Device> device = parseHead(head);
    if (!device) {
        return device.error();
    }
    return submit(Request::readBits(device.value(), count));
}

LinkState McDevice::linkState() const noexcept { return m_state; }

const ValueStore& McDevice::values() const noexcept {
    static const ValueStore kEmpty;
    return m_session ? m_session->values() : kEmpty;
}

const SessionStats& McDevice::stats() const noexcept {
    static const SessionStats kZero;
    return m_session ? m_session->stats() : kZero;
}

void McDevice::connectToPlc() {
    if (m_destroying) {
        return;
    }
    CallScope scope(*this);
    switch (m_state) {
    case LinkState::Disconnected:
        startOpen();
        break;
    case LinkState::Connecting:
        setState(LinkState::Connecting, LinkReason::Requested,
                 m_transport ? m_transport->describe() : QString());
        break;
    case LinkState::Connected:
        setState(LinkState::Connected, LinkReason::Requested, m_transport->describe());
        break;
    case LinkState::Faulted:
        // The transport stayed open; a fresh connection is the application's decision, made now.
        m_session->linkDown(nowMs());
        drain();
        m_transport->close();
        startOpen();
        break;
    }
}

void McDevice::disconnectFromPlc() {
    if (m_destroying || m_state == LinkState::Disconnected) {
        return;
    }
    CallScope scope(*this);
    if (m_session) {
        m_session->linkDown(nowMs());
        drain(); // the requestFinished() of the outstanding ids come before the state signal
    }
    if (m_transport) {
        m_transport->close();
    }
    setState(LinkState::Disconnected, LinkReason::Requested, QString());
}

void McDevice::rebuildSession() {
    m_session.reset();
    m_outstanding.clear();
    m_configError = Error{};
    m_configWhere.clear();

    McDeviceConfig probe = m_config;
    if (m_transportInjected) { // the injected transport replaces cfg.transport/tcp/serial
        probe.transport = TransportKind::Tcp;
        probe.tcp = TcpSettings{};
    }
    QString where;
    const Expected<void> valid = probe.validate(&where);
    if (!valid) {
        m_configError = valid.error();
        m_configWhere = where;
        return;
    }

    SessionConfig sessionConfig = m_config.session;
    sessionConfig.log = m_sink.get();
    Expected<Session> created = Session::create(m_config.frame, sessionConfig);
    if (!created) {
        m_configError = created.error();
        m_configWhere = QStringLiteral("session");
        return;
    }
    m_session.emplace(std::move(created.value()));

    for (qsizetype i = 0; i < m_config.subscriptions.size(); ++i) {
        const SubscriptionSpec& spec = m_config.subscriptions[i];
        const Expected<Device> head = parseHead(spec.device);
        const Expected<SubscriptionId> id = head ? m_session->subscribe(head.value(), spec.count)
                                                 : Expected<SubscriptionId>(head.error());
        if (!id) { // validate() accepted it, so this is not expected; keep the device unusable
            m_configError = id.error();
            m_configWhere = QStringLiteral("subscriptions[%1].device").arg(i);
            m_session.reset();
            return;
        }
    }
}

void McDevice::attachTransport() {
    m_transport->setParent(this); // one object tree: it follows a moveToThread()
    connect(m_transport.get(), &Transport::opened, this, &McDevice::onTransportOpened);
    connect(m_transport.get(), &Transport::openFailed, this, &McDevice::onTransportOpenFailed);
    connect(m_transport.get(), &Transport::readyRead, this, &McDevice::onTransportReadyRead);
    connect(m_transport.get(), &Transport::lost, this, &McDevice::onTransportLost);
}

bool McDevice::ensureTransport() {
    if (m_transport) {
        return true;
    }
    if (m_config.transport == TransportKind::Tcp) {
        m_transport = std::make_unique<TcpTransport>(m_config.tcp);
        attachTransport();
        return true;
    }
    failOpen(QStringLiteral("the serial transport is not available in this version"));
    return false;
}

void McDevice::startOpen() {
    if (!m_session) {
        failOpen(m_configWhere.isEmpty()
                     ? textOf(m_configError.message)
                     : m_configWhere + QStringLiteral(": ") + textOf(m_configError.message));
        return;
    }
    if (!m_transportInjected) {
        const bool serialFrame = m_config.frame.isSerial();
        if (serialFrame && m_config.transport == TransportKind::Tcp) {
            logLine(LogLevel::Info,
                    QStringLiteral("serial frame over a TCP transport (serial-device server)"));
        } else if (!serialFrame && m_config.transport == TransportKind::Serial) {
            logLine(LogLevel::Info, QStringLiteral("Ethernet frame over a serial transport"));
        }
    }
    if (!ensureTransport()) {
        return;
    }
    setState(LinkState::Connecting, LinkReason::Requested, m_transport->describe());
    m_transport->open(); // opened() / openFailed() arrive later, from the event loop
}

void McDevice::setState(LinkState state, LinkReason reason, const QString& detail) {
    m_state = state;
    m_signals.push_back(LinkStateSignal{state, reason, detail});
}

void McDevice::failOpen(const QString& detail) {
    setState(LinkState::Disconnected, LinkReason::OpenFailed, detail);
}

TimeMs McDevice::nowMs() const { return static_cast<TimeMs>(m_clock.elapsed()); }

Error McDevice::configError() const {
    if (m_configError.code != ErrorCode::Ok) {
        return m_configError;
    }
    return makeConfigError("the device has no usable configuration");
}

void McDevice::logLine(LogLevel level, const QString& text) const {
    if (!m_sink->enabled(level)) {
        return;
    }
    const QByteArray utf8 = text.toUtf8();
    m_sink->write(level, "mc.device",
                  std::string_view(utf8.constData(), static_cast<size_t>(utf8.size())));
}

void McDevice::drain() {
    if (!m_session) {
        return;
    }
    bool writeFailed = false;
    drainOnce(writeFailed);
    if (writeFailed) {
        // The transport closed itself; it reports lost() from the event loop. The Session must
        // not go on believing the frame went out.
        logLine(LogLevel::Warn, QStringLiteral("write failed; stopping the session"));
        m_session->linkDown(nowMs());
        bool ignored = false;
        drainOnce(ignored);
    }
}

void McDevice::drainOnce(bool& writeFailed) {
    Output out;
    while (m_session->nextOutput(out)) {
        switch (out.kind) {
        case OutputKind::Send:
            if (!m_transport || !m_transport->write(out.bytes)) {
                writeFailed = true;
            }
            break;
        case OutputKind::ValuesChanged: {
            ValuesChangedSignal s{out.deviceType, out.round, {}};
            s.changes.reserve(static_cast<qsizetype>(out.changeCount));
            for (size_t i = 0; i < out.changeCount; ++i) {
                s.changes.append(out.changes[i]);
            }
            m_signals.push_back(std::move(s));
            break;
        }
        case OutputKind::Snapshot:
            m_signals.push_back(buildSnapshot(out));
            break;
        case OutputKind::CycleDone:
            m_signals.push_back(out.cycle);
            break;
        case OutputKind::RequestDone:
            m_outstanding.removeOne(out.requestId);
            m_signals.push_back(
                RequestFinishedSignal{out.requestId, out.error, bytesOf(out.payload)});
            break;
        case OutputKind::LinkFault: {
            const LinkFaultInfo info{out.fault, out.error, out.reopenTransport};
            const QString text =
                QString::fromLatin1(out.fault == LinkFaultKind::Timeout ? "timeout"
                                                                        : "protocol error") +
                QStringLiteral(": ") + textOf(out.error.message);
            m_signals.push_back(info);
            setState(LinkState::Faulted, LinkReason::Fault, text);
            break;
        }
        }
    }
}

DeviceSnapshot McDevice::buildSnapshot(const Output& out) const {
    DeviceSnapshot snapshot;
    snapshot.type = out.deviceType;
    snapshot.round = out.round;

    const ValueStore& store = m_session->values();
    const size_t segmentCount = store.segmentCount(out.deviceType);
    snapshot.segments.reserve(static_cast<qsizetype>(segmentCount));
    for (size_t i = 0; i < segmentCount; ++i) {
        const SegmentView view = store.segment(out.deviceType, i);
        SnapshotSegment segment;
        segment.head = view.head;
        segment.count = view.count;
        if (view.words != nullptr) { // normalized: two bytes little-endian per word
            segment.values.resize(static_cast<qsizetype>(view.count) * 2);
            for (quint32 k = 0; k < view.count; ++k) {
                segment.values[static_cast<qsizetype>(k) * 2] =
                    static_cast<char>(view.words[k] & 0xFF);
                segment.values[static_cast<qsizetype>(k) * 2 + 1] =
                    static_cast<char>((view.words[k] >> 8) & 0xFF);
            }
        } else if (view.bits != nullptr) { // normalized: one byte per point, 0 or 1
            segment.values = QByteArray(reinterpret_cast<const char*>(view.bits),
                                        static_cast<qsizetype>(view.count));
        }
        segment.states = QByteArray(reinterpret_cast<const char*>(view.states),
                                    static_cast<qsizetype>(view.count));
        snapshot.segments.append(std::move(segment));
    }

    snapshot.chunks.reserve(static_cast<qsizetype>(out.chunkCount));
    for (size_t i = 0; i < out.chunkCount; ++i) {
        snapshot.chunks.append(
            ChunkStatus{out.chunks[i].request, out.chunks[i].state, out.chunks[i].lastError});
    }
    return snapshot;
}

void McDevice::arm() {
    const TimeMs deadline = m_session ? m_session->nextDeadline() : kNoDeadline;
    if (deadline == kNoDeadline) {
        m_timer->stop();
        return;
    }
    const TimeMs now = nowMs();
    const TimeMs wait = deadline > now ? deadline - now : 0;
    const TimeMs cap = static_cast<TimeMs>(std::numeric_limits<int>::max());
    m_timer->start(static_cast<int>(std::min(wait, cap)));
}

void McDevice::flush() {
    while (!m_signals.empty() && !m_destroying) {
        QueuedSignal next = std::move(m_signals.front());
        m_signals.pop_front();
        std::visit([this](const auto& signal) { emitOne(signal); }, next);
    }
}

void McDevice::finishCall() {
    if (m_destroying) {
        return;
    }
    drain();
    arm();
    if (m_depth == 1) {
        flush();
    }
}

void McDevice::onTransportOpened() {
    if (m_destroying || m_state != LinkState::Connecting) {
        return;
    }
    CallScope scope(*this);
    setState(LinkState::Connected, LinkReason::Requested, m_transport->describe());
    m_session->linkUp(nowMs()); // its first Send is drained by the scope
}

void McDevice::onTransportOpenFailed(const QString& reason) {
    if (m_destroying || m_state != LinkState::Connecting) {
        return;
    }
    CallScope scope(*this);
    failOpen(reason);
}

void McDevice::onTransportReadyRead() {
    if (m_destroying || !m_transport) {
        return;
    }
    CallScope scope(*this);
    uint8_t buffer[kReadBufferBytes];
    for (;;) {
        const size_t n = m_transport->read(MutableByteView{buffer, sizeof buffer});
        if (n == 0) {
            break;
        }
        if (m_session) {
            m_session->bytesIn(ByteView{buffer, n}, nowMs());
            drain();
        }
    }
}

void McDevice::onTransportLost(const QString& reason) {
    if (m_destroying || (m_state != LinkState::Connected && m_state != LinkState::Faulted)) {
        return;
    }
    CallScope scope(*this);
    const LinkReason why =
        m_transport->lastLossWasPeerClose() ? LinkReason::PeerClosed : LinkReason::TransportError;
    if (m_session) {
        m_session->linkDown(nowMs());
        drain(); // requestFinished(LinkDown) for whatever was outstanding, before the state
    }
    setState(LinkState::Disconnected, why, reason);
}

void McDevice::onTimer() {
    if (m_destroying || !m_session) {
        return;
    }
    CallScope scope(*this);
    m_session->tick(nowMs());
}

void McDevice::emitOne(const LinkStateSignal& s) {
    emit linkStateChanged(s.state, s.reason, s.detail);
}

void McDevice::emitOne(const LinkFaultInfo& s) { emit linkFault(s); }

void McDevice::emitOne(const ValuesChangedSignal& s) {
    emit valuesChanged(s.type, s.round, s.changes);
}

void McDevice::emitOne(const DeviceSnapshot& s) { emit snapshotReady(s); }

void McDevice::emitOne(const CycleInfo& s) { emit cycleDone(s); }

void McDevice::emitOne(const RequestFinishedSignal& s) {
    emit requestFinished(s.id, s.error, s.payload);
}

} // namespace mc
