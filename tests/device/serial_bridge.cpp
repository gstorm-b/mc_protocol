#include "serial_bridge.h"

#include <QByteArray>
#include <QStringList>

std::optional<SerialPairNames> serialPairFromEnvironment() {
    const QString value = qEnvironmentVariable("MC_TEST_SERIAL_PAIR");
    const QStringList parts = value.split(QLatin1Char(','));
    if (parts.size() != 2 || parts.at(0).trimmed().isEmpty() || parts.at(1).trimmed().isEmpty()) {
        return std::nullopt;
    }
    return SerialPairNames{parts.at(0).trimmed(), parts.at(1).trimmed()};
}

SerialBridge::SerialBridge(const mc::FrameConfig& frame, const mc::SerialSettings& line,
                           QObject* parent)
    : QObject(parent), m_line(line), m_port(this), m_plc(frame) {
    connect(&m_port, &QSerialPort::readyRead, this, &SerialBridge::onReadyRead);
}

SerialBridge::~SerialBridge() {
    m_port.disconnect(this);
    if (m_port.isOpen()) {
        m_port.close();
    }
}

bool SerialBridge::open() {
    m_port.setPortName(m_line.portName);
    m_port.setBaudRate(m_line.baudRate);
    m_port.setDataBits(m_line.dataBits);
    m_port.setParity(m_line.parity);
    m_port.setStopBits(m_line.stopBits);
    m_port.setFlowControl(m_line.flowControl);
    if (!m_port.open(QIODevice::ReadWrite)) {
        m_error = m_line.portName + QStringLiteral(": ") + m_port.errorString();
        return false;
    }
    return true;
}

QString SerialBridge::errorString() const { return m_error; }

mc::MockPlc& SerialBridge::plc() { return m_plc; }

void SerialBridge::onReadyRead() {
    const QByteArray request = m_port.readAll();
    m_plc.bytesIn(mc::ByteView{reinterpret_cast<const uint8_t*>(request.constData()),
                               static_cast<size_t>(request.size())});
    mc::ByteView response;
    while (m_plc.nextResponse(response)) {
        m_port.write(reinterpret_cast<const char*>(response.data),
                     static_cast<qint64>(response.size));
    }
}
