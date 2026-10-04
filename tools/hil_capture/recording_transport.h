/**
 * @file recording_transport.h
 * @brief `RecordingTransport`: a `Transport` decorator that stamps every chunk written and every
 * chunk received with one monotonic clock (spec "The capture tool").
 *
 * It uses the public `Transport` interface only. It wraps the real `TcpTransport`
 * or `SerialTransport`, forwards every call and signal unchanged (including
 * `lastLossWasPeerClose()`), and keeps the recording in memory for the capture writer.
 *
 * **Raw mode.** `mutate` and `raw` steps send frames `McDevice` would refuse to build, on the same
 * transport and the same clock. While raw mode is on, bytes that arrive are still recorded and
 * kept, but `readyRead()` is NOT emitted, so `McDevice` and its `Session` never see them (an
 * unsolicited response in the idle `Session` would fault the link); the step runner is told by
 * `rawReadyRead()` and reads them with `read()` (or `takeRaw()`). Writes go through `write()` as
 * usual, so the recording has them. `setRawMode(false)` drops whatever the runner left unread.
 */
#pragma once

#include "mc/device/transport.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QVector>

#include <cstdint>
#include <memory>

namespace mc::hil {

/// @brief The one monotonic clock of a run; shared by every `RecordingTransport` and the runner.
class RecordingClock {
  public:
    /// @brief Starts the clock now.
    RecordingClock() { m_timer.start(); }
    /// @brief Nanoseconds since the clock started (`QElapsedTimer::nsecsElapsed()`).
    qint64 nowNs() const { return m_timer.nsecsElapsed(); }

  private:
    QElapsedTimer m_timer;
};

/// @brief Which way a recorded chunk travelled.
enum class WireDirection : uint8_t { Tx, Rx };

/// @brief One chunk on the wire: what was handed to the transport, or what arrived in one go.
struct WireChunk {
    qint64 tNs{0};                        ///< Clock reading when it was written or received.
    WireDirection dir{WireDirection::Tx}; ///< Written (Tx) or received (Rx).
    QByteArray bytes;                     ///< The bytes.
};

/**
 * @class RecordingTransport
 * @brief Decorates a Transport and records its traffic.
 */
class RecordingTransport final : public Transport {
    Q_OBJECT
  public:
    /**
     * @brief Wraps @p inner.
     * @param[in] inner The real transport; becomes a child of this object.
     * @param[in] clock The run's clock.
     * @param[in] parent Qt parent of this object.
     */
    RecordingTransport(std::unique_ptr<Transport> inner, std::shared_ptr<RecordingClock> clock,
                       QObject* parent = nullptr);
    /// @brief Closes the inner transport silently.
    ~RecordingTransport() override;

    /// @brief Opens the inner transport; its signals are forwarded.
    void open() override;
    /// @brief Closes the inner transport and drops unread bytes; emits nothing.
    void close() override;
    /// @brief Records the chunk (stamped before the call) and writes it to the inner transport.
    /// @return What the inner transport returned; a chunk it refused is not recorded.
    bool write(ByteView bytes) override;
    /// @brief Copies bytes that arrived and were not read yet.
    size_t read(MutableByteView out) override;
    /// @brief The inner transport's state.
    State state() const override;
    /// @brief The inner transport's description.
    QString describe() const override;
    /// @brief The inner transport's answer: the peer closed, or a local I/O error.
    bool lastLossWasPeerClose() const override;

    /// @brief Every chunk recorded so far, in the order it happened.
    const QVector<WireChunk>& chunks() const { return m_chunks; }
    /// @brief Number of chunks recorded so far.
    int chunkCount() const { return static_cast<int>(m_chunks.size()); }
    /// @brief Moves every recorded chunk out and starts the recording empty again.
    ///
    /// For a long-lived consumer that must not keep the whole recording in memory (the GUI's
    /// device runner). The capture tool never calls it: `chunks()` and `chunkCount()` keep
    /// their meaning for it.
    /// @return The chunks recorded since the last call (or since construction), oldest first.
    QVector<WireChunk> takeChunks() {
        QVector<WireChunk> out;
        out.swap(m_chunks);
        return out;
    }
    /// @brief The shared clock.
    const std::shared_ptr<RecordingClock>& clock() const { return m_clock; }

    /// @brief Turns raw mode on or off (see the file comment); turning it off drops unread bytes.
    void setRawMode(bool on);
    /// @brief Whether raw mode is on.
    bool rawMode() const { return m_raw; }
    /// @brief Takes everything that arrived and was not read yet.
    QByteArray takeRaw();

  signals:
    /// @brief Bytes arrived while raw mode is on (`readyRead()` is not emitted then).
    void rawReadyRead();

  private:
    void onInnerReadyRead();

    Transport* m_inner; ///< The wrapped transport; a Qt child of this object.
    std::shared_ptr<RecordingClock> m_clock;
    QVector<WireChunk> m_chunks;
    QByteArray m_unread; ///< Received and recorded, not yet read by the consumer.
    bool m_raw{false};
};

} // namespace mc::hil
