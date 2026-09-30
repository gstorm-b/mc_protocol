// Negative control for QDV-HYG: never compiled. A blocking wait is forbidden in the device layer.
#include <QTcpSocket>

bool blockingConnect(QTcpSocket& socket) {
    socket.connectToHost("127.0.0.1", 5000);
    return socket.waitForConnected(1000);
}
