/**
 * @file serial_transport.h
 * @brief SerialSettings and SerialTransport: a Transport over a QSerialPort (COM port).
 */
#pragma once

#include "mc/device/transport.h"

#include <QSerialPort>
#include <QString>
#include <QtGlobal>

namespace mc {

/**
 * @struct SerialSettings
 * @brief Port name and line parameters of a serial link; the defaults are the old device's.
 *
 * The parameters must match the C24 module's settings: a mismatch shows up as timeouts or NAKs,
 * not as a clear error. The Qt enums are used as they are, so no mapping table exists between a
 * library type and the Qt one (the JSON form of McDeviceConfig spells them as strings).
 *
 * @see McDeviceConfig, SerialTransport
 */
struct SerialSettings {
    QString portName;      ///< "COM3", "/dev/ttyUSB0"; empty = invalid.
    qint32 baudRate{9600}; ///< Bits per second; must be positive.
    QSerialPort::DataBits dataBits{QSerialPort::Data7};               ///< Data bits per character.
    QSerialPort::Parity parity{QSerialPort::EvenParity};              ///< Parity check.
    QSerialPort::StopBits stopBits{QSerialPort::OneStop};             ///< Stop bits.
    QSerialPort::FlowControl flowControl{QSerialPort::NoFlowControl}; ///< Flow control.
};

/**
 * @class SerialTransport
 * @brief A Transport over one QSerialPort, for the 3C and 1C serial frames.
 *
 * The port is opened from the event loop, one turn after open(), so both transports behave alike
 * (open() never emits a signal itself). A port error while Open ends the link with lost(); the
 * cause is always a local I/O error (cable, port removed, permission), so lastLossWasPeerClose()
 * keeps the Transport default, false. A serial line has no peer that could close it, and a
 * parameter mismatch with the PLC is not an error here: it shows up as timeouts or NAKs in the
 * Session.
 *
 * Nothing blocks: every call returns at once and outcomes arrive as signals.
 *
 * @see Transport, SerialSettings
 */
class SerialTransport final : public Transport {
    Q_OBJECT
  public:
    /**
     * @brief Constructs a closed transport for @p s.
     * @param[in] s Port name and line parameters.
     * @param[in] parent Qt parent of this object.
     */
    explicit SerialTransport(SerialSettings s, QObject* parent = nullptr);

    /// @brief Closes the port silently; emits nothing.
    ~SerialTransport() override;

    /**
     * @brief Opens the port with the configured line parameters.
     *
     * Does nothing unless state() is State::Closed.
     *
     * @post Exactly one of opened() / openFailed() is emitted from the event loop, one turn
     *       later, unless close() comes first. openFailed() carries the port's error text.
     * @see Transport::open
     */
    void open() override;

    /**
     * @brief Closes the port and cancels an open() that has not completed, emitting nothing.
     * @post state() == State::Closed; no signal follows.
     * @see Transport::close
     */
    void close() override;

    /**
     * @brief Queues @p bytes on the port.
     * @param[in] bytes Bytes to send; copied into the port's buffer.
     * @return true when all bytes were queued; false when not Open or the port refused them.
     * @post After a false return caused by a port failure, lost() is emitted once from the
     *       event loop.
     * @see Transport::write
     */
    bool write(ByteView bytes) override;

    /**
     * @brief Copies received bytes out of the port without blocking.
     * @param[out] out Destination buffer.
     * @return Number of bytes copied; 0 when nothing is available or the transport is not Open.
     * @see Transport::read
     */
    size_t read(MutableByteView out) override;

    /// @brief Reports the current state.
    /// @return The current state.
    State state() const override;

    /// @brief Describes the line for logs and signals.
    /// @return Port, baud rate and framing, e.g. "COM3 9600 7E1" (data bits, parity letter
    ///         N/E/O/S/M, stop bits).
    QString describe() const override;

  private:
    void onReadyRead();
    void onError(QSerialPort::SerialPortError error);
    /// Opens the port now; runs from the event loop, only for the open() that scheduled it.
    void completeOpen(quint64 generation);
    /// Sets Closed and closes the port. Signals the port raises from inside close() find the
    /// state Closed and are ignored.
    void finishClosed();

    SerialSettings m_settings;
    QSerialPort* m_port; ///< Child of this object, so it follows a moveToThread().
    State m_state{State::Closed};
    /// Bumped by open() and close(); a deferred step captured under an older value is dropped,
    /// which is how "nothing is emitted after close()" holds for steps still queued.
    quint64 m_generation{0};
    bool m_inWrite{false}; ///< True while write() is calling into the port.
    QString m_writeError;  ///< Port error text recorded from inside write(); null when none.
};

} // namespace mc
