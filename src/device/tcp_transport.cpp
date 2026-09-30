#include "mc/device/tcp_transport.h"

#include <QAbstractSocket>
#include <QTcpSocket>
#include <QTimer>

#include <utility>

namespace mc {

TcpTransport::TcpTransport(TcpSettings s, QObject* parent)
    : Transport(parent), m_settings(std::move(s)), m_socket(new QTcpSocket(this)),
      m_connectTimer(new QTimer(this)) {
    m_connectTimer->setSingleShot(true);

    connect(m_socket, &QAbstractSocket::connected, this, &TcpTransport::onConnected);
    connect(m_socket, &QAbstractSocket::disconnected, this, &TcpTransport::onDisconnected);
    connect(m_socket, &QAbstractSocket::errorOccurred, this, &TcpTransport::onError);
    connect(m_socket, &QIODevice::readyRead, this, &TcpTransport::onReadyRead);
    connect(m_connectTimer, &QTimer::timeout, this, &TcpTransport::onConnectTimeout);
}

TcpTransport::~TcpTransport() {
    // The socket is a child and is destroyed after this body; make its last signals inert first.
    m_state = State::Closed;
    m_socket->abort();
}

void TcpTransport::open() {
    if (m_state != State::Closed) {
        return;
    }
    ++m_generation;
    m_state = State::Opening;
    if (m_settings.connectTimeoutMs > 0) {
        m_connectTimer->start(m_settings.connectTimeoutMs);
    }
    // connectToHost() can fail synchronously (e.g. an unreachable network); m_inOpen makes
    // reportOpenFailed() defer that signal to the event loop.
    m_inOpen = true;
    m_socket->connectToHost(m_settings.host, m_settings.port);
    m_inOpen = false;
}

void TcpTransport::close() {
    ++m_generation;
    finishClosed();
}

bool TcpTransport::write(ByteView bytes) {
    if (m_state != State::Open) {
        return false;
    }
    if (bytes.size == 0) {
        return true;
    }
    const auto wanted = static_cast<qint64>(bytes.size);
    if (m_socket->write(reinterpret_cast<const char*>(bytes.data), wanted) == wanted) {
        return true;
    }

    m_lostByPeer = false;
    // Immediate failure: close now, tell the owner from the event loop so a caller that is in
    // the middle of writing is never re-entered.
    const QString reason = m_socket->errorString();
    finishClosed();
    const quint64 generation = m_generation;
    QTimer::singleShot(0, this, [this, generation, reason]() {
        if (generation == m_generation) {
            emit lost(reason);
        }
    });
    return false;
}

size_t TcpTransport::read(MutableByteView out) {
    if (m_state != State::Open || out.size == 0) {
        return 0;
    }
    const qint64 n =
        m_socket->read(reinterpret_cast<char*>(out.data), static_cast<qint64>(out.size));
    return n > 0 ? static_cast<size_t>(n) : 0;
}

Transport::State TcpTransport::state() const { return m_state; }

bool TcpTransport::lastLossWasPeerClose() const { return m_lostByPeer; }

QString TcpTransport::describe() const {
    return m_settings.host + QLatin1Char(':') + QString::number(m_settings.port);
}

void TcpTransport::onConnected() {
    if (m_state != State::Opening) {
        return;
    }
    m_connectTimer->stop();
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    m_socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
    m_state = State::Open;
    emit opened();
}

void TcpTransport::onDisconnected() {
    if (m_state != State::Open) {
        return;
    }
    m_lostByPeer = true;
    finishClosed();
    emit lost(QStringLiteral("connection closed by the peer"));
}

void TcpTransport::onError() {
    // Read the text before abort() resets the socket.
    const bool remoteClosed = m_socket->error() == QAbstractSocket::RemoteHostClosedError;
    const QString reason = m_socket->errorString();
    if (m_state == State::Opening) {
        finishClosed();
        reportOpenFailed(reason);
    } else if (m_state == State::Open) {
        finishClosed();
        m_lostByPeer = remoteClosed;
        emit lost(reason);
    }
}

void TcpTransport::onReadyRead() {
    if (m_state == State::Open) {
        emit readyRead();
    }
}

void TcpTransport::onConnectTimeout() {
    if (m_state != State::Opening) {
        return;
    }
    finishClosed();
    reportOpenFailed(
        QStringLiteral("connect timed out after %1 ms").arg(m_settings.connectTimeoutMs));
}

void TcpTransport::finishClosed() {
    m_connectTimer->stop();
    m_state = State::Closed;
    m_socket->abort();
}

void TcpTransport::reportOpenFailed(const QString& reason) {
    if (!m_inOpen) {
        emit openFailed(reason);
        return;
    }
    const quint64 generation = m_generation;
    QTimer::singleShot(0, this, [this, generation, reason]() {
        if (generation == m_generation) {
            emit openFailed(reason);
        }
    });
}

} // namespace mc
