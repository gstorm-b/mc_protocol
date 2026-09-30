#include "mock_plc_server.h"

#include <QHostAddress>
#include <QTcpSocket>

MockPlcServer::MockPlcServer(const mc::FrameConfig& frame, QObject* parent)
    : QObject(parent), m_frame(frame) {
    connect(&m_server, &QTcpServer::newConnection, this, &MockPlcServer::onNewConnection);
}

MockPlcServer::~MockPlcServer() {
    for (Connection* connection : m_connections) {
        if (connection->socket != nullptr) {
            connection->socket->disconnect(this);
            connection->socket->abort();
        }
        delete connection;
    }
}

bool MockPlcServer::listen() { return m_server.listen(QHostAddress::LocalHost, 0); }

quint16 MockPlcServer::port() const { return m_server.serverPort(); }

void MockPlcServer::setInit(std::function<void(mc::MockPlc&)> init) { m_init = std::move(init); }

mc::MockPlc* MockPlcServer::plc() const {
    return m_connections.isEmpty() ? nullptr : m_connections.last()->plc.get();
}

int MockPlcServer::connectionCount() const { return static_cast<int>(m_connections.size()); }

int MockPlcServer::openConnectionCount() const {
    int n = 0;
    for (const Connection* connection : m_connections) {
        n += connection->open ? 1 : 0;
    }
    return n;
}

void MockPlcServer::closeClient() {
    if (!m_connections.isEmpty() && m_connections.last()->open) {
        m_connections.last()->socket->disconnectFromHost();
    }
}

void MockPlcServer::mute(bool on) {
    m_muted = on;
    if (plc() != nullptr) {
        plc()->mute(on);
    }
}

void MockPlcServer::onNewConnection() {
    while (m_server.hasPendingConnections()) {
        auto* connection = new Connection;
        connection->socket = m_server.nextPendingConnection();
        connection->socket->setParent(this);
        connection->plc = std::make_unique<mc::MockPlc>(m_frame);
        if (m_init) {
            m_init(*connection->plc);
        }
        if (m_muted) {
            connection->plc->mute(true);
        }
        m_connections.append(connection);

        connect(connection->socket, &QTcpSocket::readyRead, this,
                [this, connection]() { onReadyRead(connection); });
        connect(connection->socket, &QTcpSocket::disconnected, this, [this, connection]() {
            connection->open = false;
            emit clientDisconnected();
        });
        emit clientConnected();
    }
}

void MockPlcServer::onReadyRead(Connection* connection) {
    const QByteArray request = connection->socket->readAll();
    connection->plc->bytesIn(mc::ByteView{reinterpret_cast<const uint8_t*>(request.constData()),
                                          static_cast<size_t>(request.size())});
    mc::ByteView response;
    while (connection->plc->nextResponse(response)) {
        connection->socket->write(reinterpret_cast<const char*>(response.data),
                                  static_cast<qint64>(response.size));
    }
}
