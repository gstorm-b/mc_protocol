/**
 * @file serial_transport.h
 * @brief SerialSettings: the line parameters of a serial (COM port) link.
 */
#pragma once

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
 * @see McDeviceConfig
 */
struct SerialSettings {
    QString portName;      ///< "COM3", "/dev/ttyUSB0"; empty = invalid.
    qint32 baudRate{9600}; ///< Bits per second; must be positive.
    QSerialPort::DataBits dataBits{QSerialPort::Data7};               ///< Data bits per character.
    QSerialPort::Parity parity{QSerialPort::EvenParity};              ///< Parity check.
    QSerialPort::StopBits stopBits{QSerialPort::OneStop};             ///< Stop bits.
    QSerialPort::FlowControl flowControl{QSerialPort::NoFlowControl}; ///< Flow control.
};

} // namespace mc
