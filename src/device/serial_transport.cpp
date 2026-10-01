#include "mc/device/serial_transport.h"

#include <QIODevice>
#include <QTimer>

#include <utility>

namespace mc {

namespace {

char parityLetter(QSerialPort::Parity parity) {
    switch (parity) {
    case QSerialPort::NoParity:
        return 'N';
    case QSerialPort::EvenParity:
        return 'E';
    case QSerialPort::OddParity:
        return 'O';
    case QSerialPort::SpaceParity:
        return 'S';
    case QSerialPort::MarkParity:
        return 'M';
    default:
        return '?';
    }
}

QString stopBitsText(QSerialPort::StopBits stopBits) {
    switch (stopBits) {
    case QSerialPort::OneStop:
        return QStringLiteral("1");
    case QSerialPort::OneAndHalfStop:
        return QStringLiteral("1.5");
    case QSerialPort::TwoStop:
        return QStringLiteral("2");
    default:
        return QStringLiteral("?");
    }
}

} // namespace

SerialTransport::SerialTransport(SerialSettings s, QObject* parent)
    : Transport(parent), m_settings(std::move(s)), m_port(new QSerialPort(this)) {
    connect(m_port, &QIODevice::readyRead, this, &SerialTransport::onReadyRead);
    connect(m_port, &QSerialPort::errorOccurred, this, &SerialTransport::onError);
}

SerialTransport::~SerialTransport() {
    // The port is a child and is destroyed after this body; make its last signals inert first.
    m_state = State::Closed;
    if (m_port->isOpen()) {
        m_port->close();
    }
}

void SerialTransport::open() {
    if (m_state != State::Closed) {
        return;
    }
    ++m_generation;
    m_state = State::Opening;
    const quint64 generation = m_generation;
    QTimer::singleShot(0, this, [this, generation]() { completeOpen(generation); });
}

void SerialTransport::completeOpen(quint64 generation) {
    if (generation != m_generation || m_state != State::Opening) {
        return; // close() (or a newer open()) came first
    }
    m_port->setPortName(m_settings.portName);
    m_port->setBaudRate(m_settings.baudRate);
    m_port->setDataBits(m_settings.dataBits);
    m_port->setParity(m_settings.parity);
    m_port->setStopBits(m_settings.stopBits);
    m_port->setFlowControl(m_settings.flowControl);
    if (!m_port->open(QIODevice::ReadWrite)) {
        const QString reason = m_settings.portName + QStringLiteral(": ") + m_port->errorString();
        finishClosed();
        emit openFailed(reason);
        return;
    }
    m_state = State::Open;
    emit opened();
}

void SerialTransport::close() {
    ++m_generation;
    finishClosed();
}

bool SerialTransport::write(ByteView bytes) {
    if (m_state != State::Open) {
        return false;
    }
    if (bytes.size == 0) {
        return true;
    }
    const auto wanted = static_cast<qint64>(bytes.size);
    // The port may report its error from inside write(); onError() then only records it, so
    // lost() is never emitted from this call (Transport::write contract).
    m_inWrite = true;
    m_writeError.clear();
    const qint64 written = m_port->write(reinterpret_cast<const char*>(bytes.data), wanted);
    m_inWrite = false;
    if (written == wanted && m_writeError.isNull()) {
        return true;
    }

    const QString reason = m_writeError.isNull() ? m_port->errorString() : m_writeError;
    finishClosed();
    const quint64 generation = m_generation;
    QTimer::singleShot(0, this, [this, generation, reason]() {
        if (generation == m_generation) {
            emit lost(reason);
        }
    });
    return false;
}

size_t SerialTransport::read(MutableByteView out) {
    if (m_state != State::Open || out.size == 0) {
        return 0;
    }
    const qint64 n = m_port->read(reinterpret_cast<char*>(out.data), static_cast<qint64>(out.size));
    return n > 0 ? static_cast<size_t>(n) : 0;
}

Transport::State SerialTransport::state() const { return m_state; }

QString SerialTransport::describe() const {
    return QStringLiteral("%1 %2 %3%4%5")
        .arg(m_settings.portName)
        .arg(m_settings.baudRate)
        .arg(static_cast<int>(m_settings.dataBits))
        .arg(QLatin1Char(parityLetter(m_settings.parity)))
        .arg(stopBitsText(m_settings.stopBits));
}

void SerialTransport::onReadyRead() {
    if (m_state == State::Open) {
        emit readyRead();
    }
}

void SerialTransport::onError(QSerialPort::SerialPortError error) {
    // NoError is the reset value; TimeoutError belongs to the blocking waits this class never
    // calls. Everything else is an I/O failure of the port.
    if (m_state != State::Open || error == QSerialPort::NoError ||
        error == QSerialPort::TimeoutError) {
        return;
    }
    const QString reason = m_port->errorString();
    if (m_inWrite) {
        m_writeError = reason.isEmpty() ? QStringLiteral("serial write failed") : reason;
        return;
    }
    finishClosed();
    emit lost(reason);
}

void SerialTransport::finishClosed() {
    m_state = State::Closed;
    if (m_port->isOpen()) {
        m_port->close();
    }
}

} // namespace mc
