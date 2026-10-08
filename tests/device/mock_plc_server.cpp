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
        delete connection->holdTimer;
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

void MockPlcServer::holdFirstRequest(int ms) { m_holdFirstRequestMs = ms; }

void MockPlcServer::onNewConnection() {
    while (m_server.hasPendingConnections()) {
        auto* connection = new Connection;
        connection->socket = m_server.nextPendingConnection();
        connection->socket->setParent(this);
        connection->plc = std::make_unique<mc::MockPlc>(m_frame);
        connection->holdMs = m_holdFirstRequestMs;
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
            connection->holding = false;
            connection->held.clear();
            if (connection->holdTimer != nullptr) {
                connection->holdTimer->stop();
            }
            emit clientDisconnected();
        });
        emit clientConnected();
    }
}

void MockPlcServer::onReadyRead(Connection* connection) {
    const QByteArray request = connection->socket->readAll();
    if (connection->holding) {
        connection->held.append(request); // queue behind the held first request
        return;
    }
    if (connection->holdMs > 0 && !connection->firstSeen) {
        connection->firstSeen = true;
        connection->holding = true;
        connection->held = request;
        if (connection->holdTimer == nullptr) {
            connection->holdTimer = new QTimer(this);
            connection->holdTimer->setSingleShot(true);
            connect(connection->holdTimer, &QTimer::timeout, this, [this, connection]() {
                connection->holding = false;
                const QByteArray bytes = std::move(connection->held);
                connection->held.clear();
                if (connection->open && !bytes.isEmpty()) {
                    deliver(connection, bytes);
                }
            });
        }
        connection->holdTimer->start(connection->holdMs);
        return;
    }
    connection->firstSeen = true;
    deliver(connection, request);
}

void MockPlcServer::deliver(Connection* connection, const QByteArray& request) {
    connection->plc->bytesIn(mc::ByteView{reinterpret_cast<const uint8_t*>(request.constData()),
                                          static_cast<size_t>(request.size())});
    mc::ByteView response;
    while (connection->plc->nextResponse(response)) {
        connection->socket->write(reinterpret_cast<const char*>(response.data),
                                  static_cast<qint64>(response.size));
    }
}
