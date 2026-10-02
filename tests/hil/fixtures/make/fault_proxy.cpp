// fault_proxy: a TCP proxy that makes a link fault on purpose, used once to produce the fixtures
// vplc-3e-bin-fault and vplc-3e-bin-drop (see ../README.md). Not part of any build: it is a few
// lines of Qt that a person compiles by hand when a fixture has to be made again.
//
//   hil_fault_proxy <listen port> <upstream port> <mode> <ms> [<connection>]
//
// It listens on 127.0.0.1 and forwards every connection to 127.0.0.1:<upstream port> (a
// virtual_plc). The fault is made on one connection only, the <connection>-th accepted (default 1);
// every other connection passes through untouched, so the tool's reconnect works.
//
//   stall         From <ms> milliseconds after the connection was accepted, everything the upstream
//                 sends is dropped; the link stays open and the client times out.
//   close         <ms> milliseconds after the connection was accepted, both sides are closed.
//   closeonwrite  The connection is closed when the client sends a 3E binary word write (command
//                 1401, subcommand 0000): the write is outstanding when the link goes down.
//
// Build: the CMake target hil_fault_proxy (tests/CMakeLists.txt, with MC_BUILD_TESTS and
// MC_BUILD_TOOLS).
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <cstdio>

namespace {

void say(const char* text) {
    std::printf("fault_proxy: %s\n", text);
    std::fflush(stdout);
}

bool isWordWrite(const QByteArray& data) {
    return data.size() >= 15 && data[11] == 0x01 && data[12] == 0x14 && data[13] == 0x00 &&
           data[14] == 0x00;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc < 5) {
        say("usage: fault_proxy <listen port> <upstream port> <stall|close|closeonwrite> <ms> "
            "[<connection>]");
        return 2;
    }
    const quint16 listenPort = QString(argv[1]).toUShort();
    const quint16 upstreamPort = QString(argv[2]).toUShort();
    const QString mode = argv[3];
    const int ms = QString(argv[4]).toInt();
    const int faulty = argc > 5 ? QString(argv[5]).toInt() : 1;

    QTcpServer server;
    int accepted = 0;
    QObject::connect(&server, &QTcpServer::newConnection, [&]() {
        while (QTcpSocket* client = server.nextPendingConnection()) {
            const bool fault = ++accepted == faulty;
            auto* upstream = new QTcpSocket(client);
            auto* age = new QElapsedTimer;
            age->start();
            upstream->connectToHost(QHostAddress::LocalHost, upstreamPort);
            QObject::connect(client, &QTcpSocket::readyRead, upstream, [=]() {
                const QByteArray data = client->readAll();
                if (fault && mode == "closeonwrite" && isWordWrite(data)) {
                    say("closing on a word write");
                    client->abort();
                    upstream->abort();
                    return;
                }
                upstream->write(data);
            });
            QObject::connect(upstream, &QTcpSocket::readyRead, client, [=]() {
                const QByteArray data = upstream->readAll();
                if (fault && mode == "stall" && age->elapsed() >= ms) {
                    say("dropping what the upstream sent");
                    return;
                }
                client->write(data);
            });
            QObject::connect(client, &QTcpSocket::disconnected, upstream,
                             [upstream]() { upstream->close(); });
            QObject::connect(upstream, &QTcpSocket::disconnected, client,
                             [client]() { client->close(); });
            if (fault && mode == "close") {
                QTimer::singleShot(ms, client, [client, upstream]() {
                    say("closing");
                    client->abort();
                    upstream->abort();
                });
            }
        }
    });
    if (!server.listen(QHostAddress::LocalHost, listenPort)) {
        say("cannot listen");
        return 1;
    }
    say("listening");
    return app.exec();
}
