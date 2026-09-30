// virtual_plc: a PLC for demos and manual tests. A QTcpServer on 127.0.0.1 in front of mc::MockPlc.
//
//   virtual_plc --frame 3E --code Binary --port 5000 --set D100=1234 --wiggle D105
//
// What it does:
//   * Every accepted connection gets its own mc::MockPlc, built from the same frame settings and
//     the same starting image (the --set values). The connections do not share memory: a write
//     from one client is not visible to another. That is enough for a demo with one client.
//   * --set DEV=VALUE (repeatable) sets a word, or a bit when DEV is a bit device.
//   * --wiggle DEV (repeatable) changes DEV once a second (a word counts up, a bit toggles), in
//     every live connection, so a poller sees a change each second. It starts from the value
//     --set gave the same device (0 when there is none), so --set keeps its meaning.
//   * The 3E and 1E frames are answered by the mock in this version (--frame 3E, --frame 1E, each
//     with --code Binary or ASCII). Serial options are refused.
//
// The mock is a sans-I/O object: this file is the only place that touches a socket. Bytes that
// arrive go in with bytesIn(); whatever nextResponse() hands back is written to the socket.
#include "mc/core/device.h"
#include "mc/core/frame_config.h"
#include "mc/mock/mock_plc.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <cstdint>
#include <cstdio>
#include <cstddef>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void say(const QString& line) {
    std::printf("%s\n", qPrintable(line));
    std::fflush(stdout);
}

QString nameOf(mc::Device d) {
    char text[16];
    mc::formatDevice(d, text, sizeof text);
    return QString::fromLatin1(text);
}

// A device with the value it currently has in the starting image.
struct Point {
    mc::Device device;
    uint16_t value;
};

void apply(mc::MockPlc& plc, const Point& p) {
    if (mc::deviceInfo(p.device.type).kind == mc::DeviceKind::Bit) {
        plc.setBit(p.device, p.value != 0);
    } else {
        plc.setWord(p.device, p.value);
    }
}

class VirtualPlc {
  public:
    VirtualPlc(const mc::FrameConfig& frame, std::vector<Point> image, std::vector<Point> wiggles)
        : m_frame(frame), m_image(std::move(image)), m_wiggles(std::move(wiggles)) {}

    bool listen(quint16 port) {
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this]() { accept(); });
        // One timer changes the wiggled devices; every live mock gets the same new value.
        m_timer.setInterval(1000);
        QObject::connect(&m_timer, &QTimer::timeout, &m_server, [this]() { wiggle(); });
        if (!m_wiggles.empty()) {
            m_timer.start();
        }
        return m_server.listen(QHostAddress::LocalHost, port);
    }

    QString errorText() const { return m_server.errorString(); }
    quint16 port() const { return m_server.serverPort(); }

  private:
    struct Connection {
        QTcpSocket* socket;
        std::unique_ptr<mc::MockPlc> plc;
    };

    void accept() {
        while (m_server.hasPendingConnections()) {
            QTcpSocket* socket = m_server.nextPendingConnection();
            auto plc = std::make_unique<mc::MockPlc>(m_frame);
            for (const Point& p : m_image) {
                apply(*plc, p);
            }
            for (const Point& p : m_wiggles) {
                apply(*plc, p);
            }
            mc::MockPlc* raw = plc.get();
            m_connections.push_back({socket, std::move(plc)});
            say(QStringLiteral("client connected: %1:%2")
                    .arg(socket->peerAddress().toString())
                    .arg(socket->peerPort()));

            // The request bytes go into the mock; its responses go back on the socket.
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, raw]() {
                const QByteArray request = socket->readAll();
                raw->bytesIn(mc::ByteView{reinterpret_cast<const uint8_t*>(request.constData()),
                                          static_cast<size_t>(request.size())});
                mc::ByteView response;
                while (raw->nextResponse(response)) {
                    socket->write(reinterpret_cast<const char*>(response.data),
                                  static_cast<qint64>(response.size));
                }
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, [this, socket]() {
                say(QStringLiteral("client disconnected"));
                for (size_t i = 0; i < m_connections.size(); ++i) {
                    if (m_connections[i].socket == socket) {
                        m_connections.erase(m_connections.begin() + static_cast<std::ptrdiff_t>(i));
                        break;
                    }
                }
                socket->deleteLater();
            });
        }
    }

    void wiggle() {
        for (Point& p : m_wiggles) {
            const bool isBit = mc::deviceInfo(p.device.type).kind == mc::DeviceKind::Bit;
            p.value = isBit ? static_cast<uint16_t>(p.value == 0 ? 1 : 0)
                            : static_cast<uint16_t>(p.value + 1);
            for (Connection& c : m_connections) {
                apply(*c.plc, p);
            }
            say(QStringLiteral("wiggle %1 = %2").arg(nameOf(p.device)).arg(p.value));
        }
    }

    mc::FrameConfig m_frame;
    std::vector<Point> m_image;
    std::vector<Point> m_wiggles;
    QTcpServer m_server;
    QTimer m_timer;
    std::vector<Connection> m_connections;
};

// "D100=1234" -> a point; false (with a message) when it is not.
bool parseSet(const QString& text, Point& out, QString& why) {
    const qsizetype eq = text.indexOf(QLatin1Char('='));
    if (eq <= 0) {
        why = QStringLiteral("--set expects DEVICE=VALUE, got \"%1\"").arg(text);
        return false;
    }
    const QByteArray name = text.left(eq).toLatin1();
    const auto device = mc::parseDevice(std::string_view(name.constData(), name.size()));
    bool ok = false;
    const int value = text.mid(eq + 1).toInt(&ok, 0);
    if (!device || !ok || value < 0 || value > 0xFFFF) {
        why = QStringLiteral("--set: \"%1\" is not a device and a value 0..65535").arg(text);
        return false;
    }
    out = Point{device.value(), static_cast<uint16_t>(value)};
    return true;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("A virtual PLC: a TCP server in front of mc::MockPlc (3E or 1E frame)."));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("frame"), QStringLiteral("Frame family: 3E or 1E."),
                      QStringLiteral("3E|1E"), QStringLiteral("3E")});
    parser.addOption({QStringLiteral("code"), QStringLiteral("Binary or ASCII."),
                      QStringLiteral("Binary|ASCII"), QStringLiteral("Binary")});
    parser.addOption({QStringLiteral("port"), QStringLiteral("TCP port on 127.0.0.1."),
                      QStringLiteral("N"), QStringLiteral("5000")});
    parser.addOption({QStringLiteral("set"), QStringLiteral("Initial value, e.g. D100=1234."),
                      QStringLiteral("DEV=VALUE")});
    parser.addOption({QStringLiteral("wiggle"), QStringLiteral("Change DEV once a second, from its --set value (0 if none)."),
                      QStringLiteral("DEV")});
    // Recognised only to say clearly that they are not there yet.
    parser.addOption({QStringLiteral("serial"), QStringLiteral("Not available yet."),
                      QStringLiteral("PORT")});
    parser.addOption({QStringLiteral("baud"), QStringLiteral("Not available yet."),
                      QStringLiteral("N")});
    if (!parser.parse(app.arguments())) {
        std::fprintf(stderr, "%s\n", qPrintable(parser.errorText()));
        return 2;
    }
    if (parser.isSet(QStringLiteral("help"))) {
        std::printf("%s", qPrintable(parser.helpText()));
        return 0;
    }
    if (parser.isSet(QStringLiteral("serial")) || parser.isSet(QStringLiteral("baud"))) {
        std::fprintf(stderr, "virtual_plc: the serial (COM port) side is not available yet; "
                             "this build serves TCP only.\n");
        return 2;
    }

    const QString frameText = parser.value(QStringLiteral("frame"));
    const bool is1e = frameText.compare(QLatin1String("1E"), Qt::CaseInsensitive) == 0;
    if (!is1e && frameText.compare(QLatin1String("3E"), Qt::CaseInsensitive) != 0) {
        std::fprintf(stderr,
                     "virtual_plc: only --frame 3E and --frame 1E are answered by the mock in this "
                     "version.\n");
        return 2;
    }
    const QString codeText = parser.value(QStringLiteral("code"));
    mc::DataCode code = mc::DataCode::Binary;
    if (codeText.compare(QLatin1String("ASCII"), Qt::CaseInsensitive) == 0) {
        code = mc::DataCode::Ascii;
    } else if (codeText.compare(QLatin1String("Binary"), Qt::CaseInsensitive) != 0) {
        std::fprintf(stderr, "virtual_plc: --code is Binary or ASCII, got \"%s\".\n", qPrintable(codeText));
        return 2;
    }
    bool portOk = false;
    const int port = parser.value(QStringLiteral("port")).toInt(&portOk);
    if (!portOk || port < 0 || port > 65535) {
        std::fprintf(stderr, "virtual_plc: --port is 0..65535.\n");
        return 2;
    }

    std::vector<Point> image;
    for (const QString& text : parser.values(QStringLiteral("set"))) {
        Point p{};
        QString why;
        if (!parseSet(text, p, why)) {
            std::fprintf(stderr, "virtual_plc: %s\n", qPrintable(why));
            return 2;
        }
        image.push_back(p);
    }
    std::vector<Point> wiggles;
    for (const QString& text : parser.values(QStringLiteral("wiggle"))) {
        const QByteArray name = text.toLatin1();
        const auto device = mc::parseDevice(std::string_view(name.constData(), name.size()));
        if (!device) {
            std::fprintf(stderr, "virtual_plc: --wiggle: \"%s\" is not a device.\n", qPrintable(text));
            return 2;
        }
        // Start from what --set put there, so --set D105=5 --wiggle D105 counts on from 5.
        uint16_t start = 0;
        for (const Point& p : image) {
            if (p.device.type == device.value().type && p.device.number == device.value().number) {
                start = p.value; // the last --set of that device wins, as it does in the mock
            }
        }
        wiggles.push_back(Point{device.value(), start});
    }

    VirtualPlc plc(is1e ? mc::FrameConfig::frame1E(code) : mc::FrameConfig::frame3E(code), image,
                   wiggles);
    if (!plc.listen(static_cast<quint16>(port))) {
        std::fprintf(stderr, "virtual_plc: cannot listen on 127.0.0.1:%d: %s\n", port,
                     qPrintable(plc.errorText()));
        return 1;
    }
    say(QStringLiteral("virtual_plc listening on 127.0.0.1:%1 (%2 %3)")
            .arg(plc.port())
            .arg(is1e ? QLatin1String("1E") : QLatin1String("3E"))
            .arg(code == mc::DataCode::Binary ? QLatin1String("Binary") : QLatin1String("ASCII")));
    return app.exec();
}
