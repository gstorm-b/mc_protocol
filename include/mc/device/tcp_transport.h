/**
 * @file tcp_transport.h
 * @brief TcpSettings and TcpTransport: a Transport over a QTcpSocket.
 */
#pragma once

#include "mc/device/transport.h"

#include <QString>
#include <QtGlobal>

class QTcpSocket;
class QTimer;

namespace mc {

/**
 * @struct TcpSettings
 * @brief Where a TcpTransport connects, how long it waits, and its socket options and close style.
 */
struct TcpSettings {
    QString host{QStringLiteral("192.168.0.1")}; ///< Host name or address (old default).
    quint16 port{5000};                          ///< TCP port (old default).
    int connectTimeoutMs{2000};                  ///< Connect timeout in ms; 0 or less = no timer.
    bool lowDelay{true};  ///< Set QAbstractSocket::LowDelayOption (no Nagle delay) once connected;
                          ///< false leaves the OS default.
    bool keepAlive{true}; ///< Set QAbstractSocket::KeepAliveOption (OS keep-alive timing) once
                          ///< connected; false leaves the OS default.
    int closeGraceMs{0};  ///< close() of a connected socket: 0 = abort (RST) at once; > 0 =
                          ///< disconnectFromHost() (FIN), abort after this many ms at most.
};

/**
 * @class TcpTransport
 * @brief A Transport over one QTcpSocket, for Ethernet frames or a serial-device server.
 *
 * Small request frames are sent without Nagle delay (QAbstractSocket::LowDelayOption) and
 * keep-alive probes are enabled (QAbstractSocket::KeepAliveOption), both set once the connection
 * is up; TcpSettings::lowDelay and TcpSettings::keepAlive switch each off (the OS default then
 * holds). A deliberate close() ends the connection with a reset by default, or gracefully when
 * TcpSettings::closeGraceMs is positive.
 *
 * The connect timeout is a single-shot QTimer because QAbstractSocket has none of its own.
 *
 * Failures that Qt reports from inside open() are delivered from the event loop, so open() never
 * emits a signal itself.
 *
 * @see Transport, TcpSettings
 */
class TcpTransport final : public Transport {
    Q_OBJECT
  public:
    /**
     * @brief Constructs a closed transport for @p s.
     * @param[in] s Host, port and connect timeout.
     * @param[in] parent Qt parent of this object.
     */
    explicit TcpTransport(TcpSettings s, QObject* parent = nullptr);

    /// @brief Aborts the connection and any socket still closing, silently; emits nothing.
    ~TcpTransport() override;

    /**
     * @brief Connects to the configured host and port.
     *
     * Starts the connect timer when TcpSettings::connectTimeoutMs is positive. Does nothing
     * unless state() is State::Closed.
     *
     * @post Exactly one of opened() / openFailed() is emitted later from the event loop, unless
     *       close() comes first. openFailed() carries the socket's error text, or a timeout text
     *       when the connect timer fires.
     * @see Transport::open
     */
    void open() override;

    /**
     * @brief Closes the connection and cancels a pending connect, emitting nothing.
     *
     * A connected socket is aborted (reset) when TcpSettings::closeGraceMs is 0. When it is
     * positive, the socket is handed over to a graceful close: unsent bytes are flushed, then FIN
     * is sent, and the socket is aborted if the peer has not finished within that many ms. The
     * transport continues with a new socket, so open() may follow at once. A socket that is
     * still connecting is always aborted.
     *
     * @post state() == State::Closed; no signal follows, not even from a socket that is still
     *       closing.
     * @see Transport::close
     */
    void close() override;

    /**
     * @brief Queues @p bytes on the socket.
     * @param[in] bytes Bytes to send; copied into the socket's buffer.
     * @return true when all bytes were queued; false when not Open or the socket refused them.
     * @post After a false return caused by a socket failure, lost() is emitted once from the
     *       event loop.
     * @see Transport::write
     */
    bool write(ByteView bytes) override;

    /**
     * @brief Copies received bytes out of the socket without blocking.
     * @param[out] out Destination buffer.
     * @return Number of bytes copied; 0 when nothing is available or the transport is not Open.
     * @see Transport::read
     */
    size_t read(MutableByteView out) override;

    /// @brief Reports the current state.
    /// @return The current state.
    State state() const override;

    /// @brief Describes the endpoint for logs and signals.
    /// @return "host:port".
    QString describe() const override;

    /// @brief Tells why the last lost() happened.
    /// @return true when the remote host closed the connection, false after a local socket error
    ///         or a failed write().
    bool lastLossWasPeerClose() const override;

  private:
    void onConnected();
    void onDisconnected();
    void onError();
    void onReadyRead();
    void onConnectTimeout();

    /// Creates the socket (a child of this object) and connects its signals to the slots above.
    void createSocket();
    /// Hands the connected socket to a graceful close and continues with a new one.
    void closeGracefully();

    /// Stops the timer, sets Closed and aborts the socket (a loss, a failed open or a failed write;
    /// only close() may end gracefully). Signals the socket raises from inside
    /// abort() find the state Closed and are ignored.
    void finishClosed();
    /// Emits openFailed() now, or from the event loop while open() is still on the stack.
    void reportOpenFailed(const QString& reason);

    TcpSettings m_settings;
    /// Child of this object, so it follows a moveToThread(). Sockets in a graceful close are
    /// further children, detached from the slots above.
    QTcpSocket* m_socket{nullptr};
    QTimer* m_connectTimer; ///< Child of this object; single-shot.
    State m_state{State::Closed};
    /// Bumped by open() and close(); a deferred signal captured under an older value is dropped,
    /// which is how "nothing is emitted after close()" holds for signals still queued.
    quint64 m_generation{0};
    bool m_inOpen{false}; ///< True while open() is calling into the socket.
    bool m_lostByPeer{false}; ///< Cause of the last lost(); see lastLossWasPeerClose().
};

} // namespace mc
