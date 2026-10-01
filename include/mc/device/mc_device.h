/**
 * @file mc_device.h
 * @brief McDevice: the Qt object that drives a Session over a Transport and reports through
 * signals; LinkState, LinkReason, LinkFaultInfo and the snapshot value types.
 */
#pragma once

#include "mc/core/device.h"
#include "mc/core/frame_config.h"
#include "mc/core/log.h"
#include "mc/core/poll_plan.h"
#include "mc/core/request.h"
#include "mc/core/result.h"
#include "mc/core/session.h"
#include "mc/core/value_store.h"
#include "mc/device/mc_device_config.h"
#include "mc/device/transport.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringView>
#include <QVector>
#include <QtGlobal>

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <variant>

class QTimer;

namespace mc {

/**
 * @enum LinkState
 * @brief The link state an application sees.
 */
enum class LinkState : uint8_t {
    Disconnected, ///< No transport is open and none is being opened.
    Connecting,   ///< The transport is opening.
    Connected,    ///< The transport is open and the Session is polling.
    Faulted       ///< The Session reported a LinkFault; the transport is still open.
};

/**
 * @enum LinkReason
 * @brief Why a linkStateChanged() was published.
 */
enum class LinkReason : uint8_t {
    Requested,      ///< connectToPlc() or disconnectFromPlc().
    OpenFailed,     ///< The transport could not open, or the configuration is unusable.
    PeerClosed,     ///< The other side closed the connection.
    TransportError, ///< An I/O error while open (cable, port removed).
    Fault           ///< The Session reported a LinkFault; see McDevice::linkFault().
};

/**
 * @struct LinkFaultInfo
 * @brief What the Session reported when it stopped sending.
 *
 * @see McDevice::linkFault
 */
struct LinkFaultInfo {
    LinkFaultKind kind{
        LinkFaultKind::Timeout}; ///< Timeout, or a byte stream that cannot be trusted.
    Error error{};               ///< The error behind the fault.
    bool reopenTransport{false}; ///< Whether the Session wants a fresh connection.
};

/**
 * @struct SnapshotSegment
 * @brief A run of consecutive subscribed points of one device type, with values and states.
 *
 * `values` uses the normalized payload layout: two bytes little-endian per word point, one byte
 * (0 or 1) per bit point, so the accessors of mc::convert work on it directly. `states` holds one
 * PointState per point.
 *
 * @see DeviceSnapshot
 */
struct SnapshotSegment {
    Device head{};     ///< First point of the segment.
    quint32 count{0};  ///< Number of points, starting at `head`.
    QByteArray values; ///< Normalized values of the `count` points.
    QByteArray states; ///< One PointState (as a byte) per point.
};

/**
 * @struct ChunkStatus
 * @brief One read request of the plan and how its last attempt went.
 *
 * @see DeviceSnapshot
 */
struct ChunkStatus {
    Request request{};                     ///< The read the plan sends; its `data` view is empty.
    ChunkState state{ChunkState::NotRead}; ///< Outcome of the last attempt.
    Error error{};                         ///< Why the chunk failed, when `state` is Failed.
};

/**
 * @struct DeviceSnapshot
 * @brief The subscribed points of one device type after a round.
 *
 * @see McDevice::snapshotReady
 */
struct DeviceSnapshot {
    DeviceType type{DeviceType::D};    ///< Device type of every segment.
    quint32 round{0};                  ///< Round the snapshot belongs to.
    QVector<SnapshotSegment> segments; ///< Subscribed runs of `type`, sorted by device number.
    QVector<ChunkStatus> chunks;       ///< The read chunks of `type` and how the last round went.
};

/**
 * @class McDevice
 * @brief Talks to one PLC through a Transport, polls the subscribed devices and reports through
 * signals.
 *
 * A thin adapter: rounds, timeouts, retries, change detection and request correlation live in
 * mc::Session. McDevice owns a Transport, moves bytes between it and the Session, turns time into
 * Session input with one single-shot timer armed at Session::nextDeadline(), turns Session
 * outputs into signals and keeps the LinkState. It never blocks, never starts a thread and never
 * reconnects by itself: after a fault or a lost connection the application decides.
 *
 * Every public method, transport slot and timer slot ends the same way. It drains every Session
 * output (bytes to send are written at once, everything else is converted to its Qt value and
 * appended to a signal queue), re-arms the timer, and then, only in the outermost call, emits the
 * queued signals in FIFO order. A slot connected directly to a signal may call any McDevice
 * method: the nested call drains into the same queue and returns, and its signals follow the ones
 * already queued. Signal order is therefore causal, and the Session's drain contract is never
 * violated.
 *
 * @note The device, its transport and its timer form one object tree that lives on one thread.
 * The application may moveToThread() a Disconnected device; every later call must come from that
 * thread or through queued connections. Nothing in the class locks a mutex. Do not delete the
 * device from a slot connected to one of its own signals; use deleteLater().
 *
 * @see Transport, McDeviceConfig, Session
 */
class McDevice : public QObject {
    Q_OBJECT
  public:
    /**
     * @brief Constructs a device with the default configuration.
     * @param[in] parent Qt parent of this object.
     */
    explicit McDevice(QObject* parent = nullptr);

    /**
     * @brief Constructs a device that builds its transport from @p cfg.
     *
     * An unusable @p cfg does not fail the constructor: configStatus() reports the error and
     * connectToPlc() publishes it.
     *
     * @param[in] cfg Frame, session, transport and subscriptions; `cfg.session.log` is ignored.
     * @param[in] parent Qt parent of this object.
     */
    explicit McDevice(const McDeviceConfig& cfg, QObject* parent = nullptr);

    /**
     * @brief Constructs a device around a transport the caller supplies (tests, other media).
     *
     * `cfg.transport`, `cfg.tcp` and `cfg.serial` are ignored. The device takes ownership and
     * makes the transport its child.
     *
     * @param[in] cfg Frame, session and subscriptions.
     * @param[in] transport The transport to use; must be closed and live on the calling thread.
     * @param[in] parent Qt parent of this object.
     */
    McDevice(const McDeviceConfig& cfg, std::unique_ptr<Transport> transport,
             QObject* parent = nullptr);

    /**
     * @brief Closes the transport and destroys the device.
     *
     * Emits nothing: call disconnectFromPlc() first to receive the completions of outstanding
     * requests. Requests still outstanding are listed (count and ids) in one Warn log line, so a
     * forgotten disconnect leaves a trace.
     */
    ~McDevice() override;

    /**
     * @brief Replaces the configuration while Disconnected.
     *
     * Replaces the Session: values and subscriptions made at run time are dropped and
     * `cfg.subscriptions` are applied. A configuration that fails validation is refused and the
     * previous one stays in force. An injected transport is kept; a transport built from the old
     * configuration is discarded.
     *
     * @param[in] cfg The new configuration.
     * @param[out] where Optional. Receives the JSON-style path of the offending field on failure.
     * @return Success when the configuration was applied.
     * @retval ErrorCode::InvalidConfig The device is not Disconnected, or `cfg` failed
     *         McDeviceConfig::validate() with this code.
     * @retval ErrorCode::InvalidDevice A subscription of @p cfg does not parse or fit the frame.
     * @retval ErrorCode::PointCount A subscription of @p cfg has an unusable count.
     * @see configStatus
     */
    Expected<void> setConfig(const McDeviceConfig& cfg, QString* where = nullptr);

    /// @brief Reports the configuration in force.
    /// @return What the application supplied; never changed by subscribe() or unsubscribe().
    const McDeviceConfig& config() const noexcept;

    /**
     * @brief Reports whether the configuration in force is usable.
     * @return Success, or the error connectToPlc() publishes through linkStateChanged().
     */
    Expected<void> configStatus() const;

    /**
     * @brief Sets the log sink of the device and of its Session.
     * @param[in] sink Receives the log lines; not owned, must outlive the device or be reset to
     *            null first. Null disables logging. Category "mc.device" for the device's own
     *            lines.
     */
    void setLogSink(LogSink* sink);

    /// @name Subscriptions
    /// Dynamic; they take effect at the next round boundary and are not written back to config().
    /// @{

    /**
     * @brief Subscribes to @p count consecutive points starting at @p head.
     * @param[in] head First point.
     * @param[in] count Number of points.
     * @return The new subscription's id.
     * @retval ErrorCode::InvalidConfig The configuration is unusable (see configStatus()).
     * @retval ErrorCode::PointCount Zero count, or a count no command can carry.
     * @retval ErrorCode::InvalidDevice The device is not usable on this frame.
     * @see unsubscribe, Session::subscribe
     */
    Expected<SubscriptionId> subscribe(Device head, quint32 count);

    /**
     * @brief Subscribes using the text form of the head device.
     * @param[in] device Head device such as "D2000".
     * @param[in] count Number of points.
     * @return The new subscription's id.
     * @retval ErrorCode::InvalidDevice The text is not a device, or the device is not usable.
     * @retval ErrorCode::InvalidConfig The configuration is unusable (see configStatus()).
     * @retval ErrorCode::PointCount Zero count, or a count no command can carry.
     * @see unsubscribe
     */
    Expected<SubscriptionId> subscribe(QStringView device, quint32 count);

    /**
     * @brief Removes a subscription.
     * @param[in] id Id returned by subscribe().
     * @return Success when the subscription existed.
     * @retval ErrorCode::NotSubscribed No such subscription.
     * @retval ErrorCode::InvalidConfig The configuration is unusable (see configStatus()).
     * @see subscribe
     */
    Expected<void> unsubscribe(SubscriptionId id);
    /// @}

    /// @name Ad-hoc requests
    /// Exactly one requestFinished() follows every id returned.
    /// @{

    /**
     * @brief Queues an ad-hoc request.
     * @param[in] r The request; its `data` is copied before the call returns.
     * @return The request's id.
     * @retval ErrorCode::LinkDown The link is not up.
     * @retval ErrorCode::QueueFull The Session's ad-hoc queue or arena is full.
     * @retval ErrorCode::PointCount The request does not fit the frame.
     * @retval ErrorCode::InvalidDevice The request's device is not usable on this frame.
     * @retval ErrorCode::InvalidConfig The configuration is unusable (see configStatus()).
     * @post On success requestFinished() fires exactly once with the returned id, with the
     *       result, the PLC's error, a timeout or LinkDown.
     */
    Expected<RequestId> submit(const Request& r);

    /**
     * @brief Writes consecutive words.
     * @param[in] head First word device, e.g. "D100".
     * @param[in] values Values to write, in device order; at most 65535.
     * @return The request's id; errors as submit(), plus ErrorCode::InvalidDevice for a text that
     *         is not a device.
     * @post requestFinished() fires once with the returned id.
     */
    Expected<RequestId> writeWords(QStringView head, const QVector<quint16>& values);

    /**
     * @brief Writes consecutive bit points.
     * @param[in] head First bit device, e.g. "M100".
     * @param[in] values Values to write, in device order; at most 65535.
     * @return The request's id; errors as submit(), plus ErrorCode::InvalidDevice for a text that
     *         is not a device.
     * @post requestFinished() fires once with the returned id.
     */
    Expected<RequestId> writeBits(QStringView head, const QVector<bool>& values);

    /**
     * @brief Reads consecutive words.
     * @param[in] head First word device, e.g. "D100".
     * @param[in] count Number of words.
     * @return The request's id; errors as submit(), plus ErrorCode::InvalidDevice for a text that
     *         is not a device.
     * @post requestFinished() fires once with the returned id; its payload holds two bytes
     *       little-endian per word.
     */
    Expected<RequestId> readWords(QStringView head, quint16 count);

    /**
     * @brief Reads consecutive bit points.
     * @param[in] head First bit device, e.g. "M100".
     * @param[in] count Number of points.
     * @return The request's id; errors as submit(), plus ErrorCode::InvalidDevice for a text that
     *         is not a device.
     * @post requestFinished() fires once with the returned id; its payload holds one byte (0 or
     *       1) per point.
     */
    Expected<RequestId> readBits(QStringView head, quint16 count);
    /// @}

    /// @brief Reports the link state; updated as soon as it changes, before its signal is emitted.
    /// @return The current state.
    LinkState linkState() const noexcept;

    /// @brief Reads the values of every subscribed point.
    /// @return The Session's value store (empty when the configuration is unusable); read it in
    ///         a slot, or at any time on the device's own thread.
    const ValueStore& values() const noexcept;

    /// @brief Reads the Session's running counters.
    /// @return The counters (all zero when the configuration is unusable).
    const SessionStats& stats() const noexcept;

  public slots:
    /**
     * @brief Opens the link, or re-publishes the state when it is already open or opening.
     *
     * Disconnected: builds the transport unless one was injected (a TcpTransport or, for
     * `TransportKind::Serial`, a SerialTransport) and opens it (Connecting). Connected:
     * republishes Connected and opens no second socket. Connecting: opens nothing more and
     * republishes Connecting. Faulted: stops the Session, closes the transport and opens it
     * again. An unusable configuration publishes (Disconnected, OpenFailed) without opening
     * anything; so does a transport that cannot be opened (a COM port that does not exist).
     *
     * @post At least one linkStateChanged() is emitted before the call returns.
     * @see disconnectFromPlc
     */
    void connectToPlc();

    /**
     * @brief Completes outstanding requests with LinkDown, closes the transport and publishes
     * Disconnected.
     *
     * Does nothing when already Disconnected.
     *
     * @post requestFinished() for every outstanding id, then
     *       linkStateChanged(Disconnected, Requested).
     * @see connectToPlc
     */
    void disconnectFromPlc();

  signals:
    /**
     * @brief The link state changed, or connectToPlc() re-published it.
     * @param[out] state The new state.
     * @param[out] reason Why.
     * @param[out] detail Human-readable text: the endpoint, or the cause of a failure.
     */
    void linkStateChanged(mc::LinkState state, mc::LinkReason reason, const QString& detail);

    /**
     * @brief The Session stopped sending; emitted before the matching linkStateChanged(Faulted).
     * @param[out] fault What the Session reported.
     */
    void linkFault(const mc::LinkFaultInfo& fault);

    /**
     * @brief One response's changes; round 2 and later only.
     * @param[out] type Device type of the changed points.
     * @param[out] round Round the response belongs to.
     * @param[out] changes The changed points with old and new value.
     */
    void valuesChanged(mc::DeviceType type, quint32 round, const QVector<mc::Change>& changes);

    /**
     * @brief One device type after a round; after round 1 all types arrive together.
     * @param[out] snapshot The subscribed points of the type and the outcome of its chunks.
     */
    void snapshotReady(const mc::DeviceSnapshot& snapshot);

    /**
     * @brief A polling round finished.
     * @param[out] cycle Summary of the round.
     */
    void cycleDone(const mc::CycleInfo& cycle);

    /**
     * @brief An ad-hoc request completed; emitted exactly once per id returned by submit().
     * @param[out] id The id submit() returned.
     * @param[out] error Success, the PLC's error, a timeout or LinkDown.
     * @param[out] payload The normalized read payload; empty for a write or a failure.
     */
    void requestFinished(mc::RequestId id, const mc::Error& error, const QByteArray& payload);

  private:
    class SinkForwarder;

    struct LinkStateSignal {
        LinkState state;
        LinkReason reason;
        QString detail;
    };
    struct ValuesChangedSignal {
        DeviceType type;
        quint32 round;
        QVector<Change> changes;
    };
    struct RequestFinishedSignal {
        RequestId id;
        Error error;
        QByteArray payload;
    };
    /// One entry of the signal queue: what is emitted, in the order it was produced.
    using QueuedSignal = std::variant<LinkStateSignal, LinkFaultInfo, ValuesChangedSignal,
                                      DeviceSnapshot, CycleInfo, RequestFinishedSignal>;

    class CallScope;

    void rebuildSession();
    void attachTransport();
    void ensureTransport();
    void startOpen();
    void setState(LinkState state, LinkReason reason, const QString& detail);
    void failOpen(const QString& detail);
    TimeMs nowMs() const;
    void drain();
    /// Pops every Session output; `writeFailed` is set when a Send could not be written.
    void drainOnce(bool& writeFailed);
    DeviceSnapshot buildSnapshot(const Output& out) const;
    void arm();
    void flush();
    void finishCall();
    void logLine(LogLevel level, const QString& text) const;
    Error configError() const;
    static Expected<Device> parseHead(QStringView text);

    void onTransportOpened();
    void onTransportOpenFailed(const QString& reason);
    void onTransportReadyRead();
    void onTransportLost(const QString& reason);
    void onTimer();

    void emitOne(const LinkStateSignal& s);
    void emitOne(const LinkFaultInfo& s);
    void emitOne(const ValuesChangedSignal& s);
    void emitOne(const DeviceSnapshot& s);
    void emitOne(const CycleInfo& s);
    void emitOne(const RequestFinishedSignal& s);

    McDeviceConfig m_config;                ///< What the application supplied.
    std::unique_ptr<SinkForwarder> m_sink;  ///< Handed to the Session; forwards to setLogSink().
    QTimer* m_timer;                        ///< Child; single-shot, armed at the Session deadline.
    QElapsedTimer m_clock;                  ///< Started in the constructor; the Session's time.
    std::unique_ptr<Transport> m_transport; ///< Child once set; null until first connect.
    bool m_transportInjected{false};        ///< True when the application supplied it.
    std::optional<Session> m_session;       ///< Empty while the configuration is unusable.
    Error m_configError{};                  ///< Why the configuration is unusable, if it is.
    QString m_configWhere;                  ///< JSON-style path that goes with m_configError.
    LinkState m_state{LinkState::Disconnected}; ///< Updated at once, signalled through the queue.
    std::deque<QueuedSignal> m_signals;         ///< Signals produced but not yet emitted, FIFO.
    int m_depth{0};                             ///< Nesting of public calls, transport slots and
                                                ///< timer slots; only depth 1 flushes the queue.
    QVector<RequestId> m_outstanding;           ///< Ids returned by submit() and not completed.
    bool m_destroying{false};                   ///< Set first in the destructor.
};

} // namespace mc
