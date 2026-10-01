// virtual_plc: a PLC for demos and manual tests. A QTcpServer on 127.0.0.1, or one COM port, in
// front of mc::MockPlc.
//
//   virtual_plc --frame 3E --code Binary --port 5000 --set D100=1234 --wiggle D105
//   virtual_plc --frame 3C --format 4 --serial COM50 --set D100=1234 --wiggle D105
//
// What it does:
//   * Over TCP, every accepted connection gets its own mc::MockPlc, built from the same frame
//     settings and the same starting image (the --set values). The connections do not share
//     memory: a write from one client is not visible to another. That is enough for a demo with
//     one client.
//   * With --serial PORT (and --baud N, default 9600; the line is 7E1, the default of the old
//     device and the usual C24 setting) one mc::MockPlc answers on that COM port instead, and no
//     TCP port is opened. Pair it with a virtual COM pair (the other end of PORT) and
//     `qt_console_poller --serial OTHER`.
//   * --set DEV=VALUE (repeatable) sets a word, or a bit when DEV is a bit device.
//   * --wiggle DEV (repeatable) changes DEV once a second (a word counts up, a bit toggles), in
//     every live connection, so a poller sees a change each second. It starts from the value
//     --set gave the same device (0 when there is none), so --set keeps its meaning.
//   * The 3E and 1E frames (--code Binary or ASCII) and the 3C and 1C frames (ASCII, --format 1 to
//     4) are answered by the mock; without --serial the serial frames travel over the TCP socket,
//     as through a serial-to-Ethernet converter.
//
// The mock is a sans-I/O object: this file is the only place that touches a socket or a port.
// Bytes that arrive go in with bytesIn(); whatever nextResponse() hands back is written back.
#include "mc/core/device.h"
#include "mc/core/frame_config.h"
#include "mc/device/serial_transport.h"
#include "mc/mock/mock_plc.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QHostAddress>
#include <QSerialPort>
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

// What arrives on `io` goes into the mock; what the mock answers goes back on `io`.
void feed(QIODevice& io, mc::MockPlc& plc) {
    const QByteArray request = io.readAll();
    plc.bytesIn(mc::ByteView{reinterpret_cast<const uint8_t*>(request.constData()),
                             static_cast<size_t>(request.size())});
    mc::ByteView response;
    while (plc.nextResponse(response)) {
        io.write(reinterpret_cast<const char*>(response.data), static_cast<qint64>(response.size));
    }
}

class VirtualPlc {
  public:
    VirtualPlc(const mc::FrameConfig& frame, std::vector<Point> image, std::vector<Point> wiggles)
        : m_frame(frame), m_image(std::move(image)), m_wiggles(std::move(wiggles)) {}

    bool listen(quint16 port) {
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this]() { accept(); });
        startWiggle();
        return m_server.listen(QHostAddress::LocalHost, port);
    }

    // One mock on one COM port; false with errorText() set when the port cannot be opened.
    bool listenSerial(const mc::SerialSettings& line) {
        m_serialPlc = std::make_unique<mc::MockPlc>(m_frame);
        for (const Point& p : m_image) {
            apply(*m_serialPlc, p);
        }
        for (const Point& p : m_wiggles) {
            apply(*m_serialPlc, p);
        }
        m_port.setPortName(line.portName);
        m_port.setBaudRate(line.baudRate);
        m_port.setDataBits(line.dataBits);
        m_port.setParity(line.parity);
        m_port.setStopBits(line.stopBits);
        m_port.setFlowControl(line.flowControl);
        QObject::connect(&m_port, &QSerialPort::readyRead, &m_port,
                         [this]() { feed(m_port, *m_serialPlc); });
        QObject::connect(
            &m_port, &QSerialPort::errorOccurred, &m_port, [this](QSerialPort::SerialPortError e) {
                if (e != QSerialPort::NoError && e != QSerialPort::TimeoutError) {
                    say(QStringLiteral("serial port error: %1").arg(m_port.errorString()));
                    QCoreApplication::exit(1);
                }
            });
        if (!m_port.open(QIODevice::ReadWrite)) {
            m_portError = m_port.errorString();
            return false;
        }
        startWiggle();
        return true;
    }

    QString errorText() const {
        return m_portError.isEmpty() ? m_server.errorString() : m_portError;
    }
    quint16 port() const { return m_server.serverPort(); }

  private:
    struct Connection {
        QTcpSocket* socket;
        std::unique_ptr<mc::MockPlc> plc;
    };

    // One timer changes the wiggled devices; every live mock gets the same new value.
    void startWiggle() {
        m_timer.setInterval(1000);
        QObject::connect(&m_timer, &QTimer::timeout, &m_timer, [this]() { wiggle(); });
        if (!m_wiggles.empty()) {
            m_timer.start();
        }
    }

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
            QObject::connect(socket, &QTcpSocket::readyRead, socket,
                             [socket, raw]() { feed(*socket, *raw); });
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
            if (m_serialPlc) {
                apply(*m_serialPlc, p);
            }
            say(QStringLiteral("wiggle %1 = %2").arg(nameOf(p.device)).arg(p.value));
        }
    }

    mc::FrameConfig m_frame;
    std::vector<Point> m_image;
    std::vector<Point> m_wiggles;
    QTcpServer m_server;
    QSerialPort m_port;
    std::unique_ptr<mc::MockPlc> m_serialPlc; // the one mock of --serial mode
    QString m_portError;
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
        QStringLiteral("A virtual PLC: a TCP server or a COM port in front of mc::MockPlc (3E, "
                       "1E, 3C or 1C frame)."));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("frame"), QStringLiteral("Frame family: 3E, 1E, 3C or 1C."),
                      QStringLiteral("3E|1E|3C|1C"), QStringLiteral("3E")});
    parser.addOption({QStringLiteral("code"), QStringLiteral("Binary or ASCII (3E and 1E)."),
                      QStringLiteral("Binary|ASCII"), QStringLiteral("Binary")});
    parser.addOption({QStringLiteral("format"), QStringLiteral("Serial format 1 to 4 (3C and 1C)."),
                      QStringLiteral("1|2|3|4"), QStringLiteral("1")});
    parser.addOption({QStringLiteral("port"), QStringLiteral("TCP port on 127.0.0.1."),
                      QStringLiteral("N"), QStringLiteral("5000")});
    parser.addOption({QStringLiteral("set"), QStringLiteral("Initial value, e.g. D100=1234."),
                      QStringLiteral("DEV=VALUE")});
    parser.addOption({QStringLiteral("wiggle"), QStringLiteral("Change DEV once a second, from its --set value (0 if none)."),
                      QStringLiteral("DEV")});
    parser.addOption({QStringLiteral("serial"),
                      QStringLiteral("Answer on this COM port (7E1) instead of a TCP port."),
                      QStringLiteral("PORT")});
    parser.addOption({QStringLiteral("baud"), QStringLiteral("Baud rate with --serial."),
                      QStringLiteral("N"), QStringLiteral("9600")});
    if (!parser.parse(app.arguments())) {
        std::fprintf(stderr, "%s\n", qPrintable(parser.errorText()));
        return 2;
    }
    if (parser.isSet(QStringLiteral("help"))) {
        std::printf("%s", qPrintable(parser.helpText()));
        return 0;
    }
    const bool onComPort = parser.isSet(QStringLiteral("serial"));
    if (parser.isSet(QStringLiteral("baud")) && !onComPort) {
        std::fprintf(stderr, "virtual_plc: --baud needs --serial.\n");
        return 2;
    }
    mc::SerialSettings line; // 9600 7E1, no flow control: the defaults of the old device
    line.portName = parser.value(QStringLiteral("serial"));
    bool baudOk = false;
    const int baud = parser.value(QStringLiteral("baud")).toInt(&baudOk);
    if (!baudOk || baud <= 0) {
        std::fprintf(stderr, "virtual_plc: --baud is a positive number.\n");
        return 2;
    }
    line.baudRate = baud;
    if (onComPort && line.portName.isEmpty()) {
        std::fprintf(stderr, "virtual_plc: --serial needs a port name, e.g. COM50.\n");
        return 2;
    }

    const QString frameText = parser.value(QStringLiteral("frame"));
    const bool is1e = frameText.compare(QLatin1String("1E"), Qt::CaseInsensitive) == 0;
    const bool is3c = frameText.compare(QLatin1String("3C"), Qt::CaseInsensitive) == 0;
    const bool is1c = frameText.compare(QLatin1String("1C"), Qt::CaseInsensitive) == 0;
    const bool serialFrame = is3c || is1c;
    if (!is1e && !serialFrame &&
        frameText.compare(QLatin1String("3E"), Qt::CaseInsensitive) != 0) {
        std::fprintf(stderr, "virtual_plc: --frame is 3E, 1E, 3C or 1C.\n");
        return 2;
    }
    bool formatOk = false;
    const int formatNumber = parser.value(QStringLiteral("format")).toInt(&formatOk);
    if (!formatOk || formatNumber < 1 || formatNumber > 4) {
        std::fprintf(stderr, "virtual_plc: --format is 1, 2, 3 or 4.\n");
        return 2;
    }
    if (serialFrame && parser.isSet(QStringLiteral("code")) &&
        parser.value(QStringLiteral("code")).compare(QLatin1String("ASCII"),
                                                     Qt::CaseInsensitive) != 0) {
        std::fprintf(stderr, "virtual_plc: 3C and 1C frames are ASCII only.\n");
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

    const auto format = static_cast<mc::SerialFormat>(formatNumber);
    mc::FrameConfig frame = mc::FrameConfig::frame3E(code);
    if (is1e) {
        frame = mc::FrameConfig::frame1E(code);
    } else if (is3c) {
        frame = mc::FrameConfig::frame3C(format);
    } else if (is1c) {
        frame = mc::FrameConfig::frame1C(format);
    }
    VirtualPlc plc(frame, image, wiggles);
    const QString shape = serialFrame ? QStringLiteral("format %1").arg(formatNumber)
                          : code == mc::DataCode::Binary ? QStringLiteral("Binary")
                                                         : QStringLiteral("ASCII");
    if (onComPort) {
        if (!plc.listenSerial(line)) {
            std::fprintf(stderr, "virtual_plc: cannot open %s: %s\n", qPrintable(line.portName),
                         qPrintable(plc.errorText()));
            return 1;
        }
        say(QStringLiteral("virtual_plc listening on %1 %2 7E1 (%3 %4)")
                .arg(line.portName)
                .arg(line.baudRate)
                .arg(frameText.toUpper(), shape));
        return app.exec();
    }
    if (!plc.listen(static_cast<quint16>(port))) {
        std::fprintf(stderr, "virtual_plc: cannot listen on 127.0.0.1:%d: %s\n", port,
                     qPrintable(plc.errorText()));
        return 1;
    }
    say(QStringLiteral("virtual_plc listening on 127.0.0.1:%1 (%2 %3)")
            .arg(plc.port())
            .arg(frameText.toUpper(), shape));
    return app.exec();
}
