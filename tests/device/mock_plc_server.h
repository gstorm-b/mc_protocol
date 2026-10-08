// mock_plc_server.h -- a loopback PLC for the device tests: a QTcpServer on 127.0.0.1 (OS-chosen
// port) that hosts a fresh mc::MockPlc for every accepted connection. Test infrastructure, not
// library code: it links mc::mock, which mc_device itself never does.
#pragma once

#include "mc/core/frame_config.h"
#include "mc/mock/mock_plc.h"

#include <QObject>
#include <QTcpServer>
#include <QTimer>
#include <QVector>

#include <functional>
#include <memory>

class QTcpSocket;

class MockPlcServer : public QObject {
    Q_OBJECT
  public:
    explicit MockPlcServer(const mc::FrameConfig& frame = mc::FrameConfig::frame3E(),
                           QObject* parent = nullptr);
    ~MockPlcServer() override;

    // Starts listening on 127.0.0.1 with an OS-chosen port.
    bool listen();
    quint16 port() const;

    // Called for every new mock right after its creation (initial memory, faults). Applies to
    // the connections accepted after the call.
    void setInit(std::function<void(mc::MockPlc&)> init);

    // The mock of the most recent connection; null when nothing was accepted yet. The object
    // stays valid, and answers nothing, after its client disconnected.
    mc::MockPlc* plc() const;

    // Connections accepted so far, and connections whose socket is still open.
    int connectionCount() const;
    int openConnectionCount() const;

    // Closes the current client's socket from the server side.
    void closeClient();

    // Swallows every request (of the current and of every later mock) while on.
    void mute(bool on);

    // For connections accepted after the call: the bytes of each connection's first request (and
    // everything that arrives while they are held) reach its mock only after `ms`, in order, as a
    // PLC does that serves a new connection late. 0 = off. A connection closed during the hold
    // drops the held bytes.
    void holdFirstRequest(int ms);

  signals:
    void clientConnected();
    void clientDisconnected();

  private:
    struct Connection {
        QTcpSocket* socket{nullptr};
        std::unique_ptr<mc::MockPlc> plc;
        bool open{true};
        int holdMs{0};         // holdFirstRequest() value at accept time
        bool holding{false};   // the first request is being held
        bool firstSeen{false}; // the first bytes arrived
        QByteArray held;
        QTimer* holdTimer{nullptr}; // owned by the server (child), single shot
    };

    void onNewConnection();
    void onReadyRead(Connection* connection);
    void deliver(Connection* connection, const QByteArray& request);

    mc::FrameConfig m_frame;
    QTcpServer m_server;
    std::function<void(mc::MockPlc&)> m_init;
    bool m_muted{false};
    int m_holdFirstRequestMs{0};
    QVector<Connection*> m_connections; // owned; kept until the server is destroyed
};
