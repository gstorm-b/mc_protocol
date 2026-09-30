/**
 * @file transport.h
 * @brief Abstract byte transport under McDevice: open, close, write, read and its signals.
 */
#pragma once

#include "mc/core/types.h"

#include <QObject>
#include <QString>

#include <cstddef>
#include <cstdint>

namespace mc {

/**
 * @class Transport
 * @brief Moves bytes and knows nothing about the MC protocol.
 *
 * The FrameConfig, not the transport, decides protocol behaviour, so a 3C frame over a TCP
 * serial-device server is allowed and behaves as serial. A transport never reconnects by itself
 * and never blocks: every operation returns at once and outcomes arrive as signals.
 *
 * A transport and its helper objects form one object tree that lives on one thread.
 *
 * @see TcpTransport
 */
class Transport : public QObject {
    Q_OBJECT
  public:
    /**
     * @enum State
     * @brief Where the transport is between open() and close().
     */
    enum class State : uint8_t {
        Closed,  ///< Not open: never opened, closed, failed to open, or lost.
        Opening, ///< open() was called and neither opened() nor openFailed() was emitted yet.
        Open     ///< opened() was emitted and lost() has not been.
    };

    /**
     * @brief Constructs a closed transport.
     * @param[in] parent Qt parent of this object.
     */
    using QObject::QObject;

    /// @brief Destroys the transport, closing it silently.
    ~Transport() override = default;

    /**
     * @brief Starts opening the link.
     *
     * Asynchronous: returns at once. Does nothing unless state() is State::Closed.
     *
     * @post Exactly one of opened() / openFailed() is emitted later, unless close() (or the
     *       destructor) comes first, in which case neither is.
     * @see close
     */
    virtual void open() = 0;

    /**
     * @brief Closes the link at once and cancels an open() in progress.
     *
     * Idempotent. Emits nothing, before or after the call returns: the caller asked for it.
     *
     * @post state() == State::Closed; no opened(), openFailed() or lost() follows.
     * @see open
     */
    virtual void close() = 0;

    /**
     * @brief Queues bytes for sending.
     *
     * @param[in] bytes Bytes to send, in order; copied before the call returns.
     * @return true when every byte was queued; false when the transport is not open or an
     *         immediate failure occurred.
     * @post After a false return caused by a failure while Open, state() == State::Closed and
     *       lost() is emitted once from the event loop (never from inside this call).
     */
    virtual bool write(ByteView bytes) = 0;

    /**
     * @brief Copies what has already arrived, without blocking.
     *
     * @param[out] out Destination buffer.
     * @return Number of bytes copied; 0 means nothing more is available now.
     */
    virtual size_t read(MutableByteView out) = 0;

    /// @brief Reports the current state.
    /// @return The current state.
    virtual State state() const = 0;

    /// @brief Describes the endpoint for logs and signals.
    /// @return A short text, e.g. "192.168.0.10:5000" or "COM3 9600 7E1".
    virtual QString describe() const = 0;

    /**
     * @brief Tells why the last lost() happened, so McDevice can pick its LinkReason.
     *
     * The default implementation reports a local I/O error; a transport that can tell overrides
     * it. Read it from a slot of lost(); the value is meaningless before the first loss.
     *
     * @return true when the peer closed (or reset) the link, false when a local I/O error ended it.
     * @see lost
     */
    virtual bool lastLossWasPeerClose() const { return false; }

  signals:
    /**
     * @brief The link is open.
     * @see open
     */
    void opened();

    /**
     * @brief The link could not be opened.
     * @param[out] reason Human-readable cause.
     * @see open
     */
    void openFailed(const QString& reason);

    /// @brief Bytes have arrived; call read() until it returns 0.
    void readyRead();

    /**
     * @brief The peer closed the link or an I/O error happened while Open.
     *
     * Not emitted after close().
     *
     * @param[out] reason Human-readable cause.
     */
    void lost(const QString& reason);
};

} // namespace mc
