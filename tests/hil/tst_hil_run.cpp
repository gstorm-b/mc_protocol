// HIL-04, capture half (SPEC-hil-capture.md): the tool runs a plan end to end against
// examples/virtual_plc and writes a full capture set. Everything is written under the build tree
// and no real PLC is involved. The serial suite needs the virtual COM pair (MC_TEST_SERIAL_PAIR)
// and runs as its own ctest entry with RESOURCE_LOCK mc_serial_pair.
#include "hil_suites.h"
#include "hil_test_support.h"

#include "common/vectors.h"
#include "hil_capture/options.h"
#include "hil_capture/plan.h"
#include "hil_capture/profile.h"
#include "hil_capture/resolve.h"
#include "hil_capture/runner.h"
#include "hil_capture/tool.h"
#include "mc/core/protocol.h"
#include "mc/mock/mock_plc.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QHostAddress>
#include <QIODevice>
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>
#include <QSemaphore>
#include <QSerialPort>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtTest>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

#include <functional>
#include <memory>

namespace mc::hil::test {

namespace {

std::string toStd(const QString& s) { return s.toStdString(); }

QString planPath(const char* name) {
    return testsDir() + QStringLiteral("/hil/e2e/") + QLatin1String(name);
}

QString readAll(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

// An input device whose read blocks until the test lets the operator "answer".
class GatedInput : public QIODevice {
  public:
    GatedInput() { open(QIODevice::ReadOnly); }
    void answer() { m_gate.release(); }
    /// @brief Called, on the reading thread, when the reader starts to wait for the answer.
    void setOnRead(std::function<void()> f) { m_onRead = std::move(f); }
    bool isSequential() const override { return true; }

  protected:
    qint64 readData(char* data, qint64 maxSize) override {
        if (m_done || maxSize < 1) {
            return -1;
        }
        if (m_onRead) {
            m_onRead();
        }
        m_gate.acquire();
        m_done = true;
        data[0] = '\n';
        return 1;
    }
    qint64 writeData(const char*, qint64) override { return -1; }

  private:
    QSemaphore m_gate;
    bool m_done{false};
    std::function<void()> m_onRead;
};

// Ends the process with a clear message when a test hangs (a busy or stuck virtual COM port can
// block a driver call for good), well before ctest's own 120 s timeout.
class Watchdog {
  public:
    Watchdog(int seconds, const char* what)
        : m_thread([this, seconds, what]() {
              std::unique_lock<std::mutex> lock(m_mutex);
              if (!m_cv.wait_for(lock, std::chrono::seconds(seconds),
                                 [this]() { return m_done; })) {
                  std::fprintf(
                      stderr,
                      "FAIL: %s did not finish in %d s: the virtual COM pair is busy or stuck "
                      "(another process holds a port?)\n",
                      what, seconds);
                  std::fflush(stderr);
                  std::_Exit(3);
              }
          }) {}
    ~Watchdog() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_done = true;
        }
        m_cv.notify_all();
        m_thread.join();
    }

  private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_done{false};
    std::thread m_thread;
};

// Runs mc_replay_tests on the capture folders under `root` (HIL-04, replay half): exit 0 means
// green.
QString runReplay(const QString& root, int* exitCode) {
    const QString path = QStringLiteral(MC_REPLAY_TESTS_PATH);
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        *exitCode = -1;
        return QStringLiteral("mc_replay_tests is not built here (%1)").arg(path);
    }
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(path,
            {QStringLiteral("--replay-root=") + root, QStringLiteral("-tc=RPL-sweep: every*")});
    if (!p.waitForFinished(60000)) {
        p.kill();
        *exitCode = -2;
        return QStringLiteral("mc_replay_tests did not finish");
    }
    *exitCode = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -3;
    return QString::fromUtf8(p.readAll());
}

// A loopback TCP server that gives every accepted connection its own mc::MockPlc (as a PLC module
// does: what one connection sent never meets what another sends) and keeps the bytes of each.
class PerConnectionPlc : public QObject {
  public:
    struct Connection {
        std::unique_ptr<MockPlc> plc;
        QByteArray bytes;
        int requests{0}; ///< Chunks received on this connection (one per request in these tests).
        bool refused{false}; ///< Accepted near a close of the fake: it will not answer, it closes.
    };

    explicit PerConnectionPlc(const FrameConfig& frame) : m_frame(frame) {
        QObject::connect(&m_server, &QTcpServer::newConnection, this, [this]() { accept(); });
    }

    bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }
    quint16 port() const { return m_server.serverPort(); }
    /// @brief Every response is written this long after its request arrived.
    void setDelayMs(int ms) { m_delayMs = ms; }
    /// @brief While silent, a response that falls due is dropped (the cable is out). Thread safe.
    void setSilent(bool on) { m_silent = on; }
    /// @brief The @p request-th chunk received on the @p connection-th accepted connection (both
    /// counted from 1) is not answered: the fake closes that connection (FIN) instead.
    void closeOnRequest(int connection, int request) {
        m_closeConnection = connection;
        m_closeRequest = request;
    }
    /// @brief A connection accepted less than @p windowMs after the fake last closed one is closed
    /// @p afterMs after it was accepted, or at its first request, whichever comes first, and
    /// never answers (a module that drops a link opened right after a close).
    void refuseNearLastClose(int windowMs, int afterMs) {
        m_refuseWindowMs = windowMs;
        m_refuseAfterMs = afterMs;
    }
    /// @brief Every accepted connection is closed @p afterMs after it was accepted (a link that
    /// never stays up).
    void closeEveryConnection(int afterMs) { m_closeAllAfterMs = afterMs; }
    /// @brief What the fake writes back to a lone EOT (`04` or `04 0D 0A`); empty: nothing, as the
    /// MockPlc does.
    void setEotReply(const QByteArray& reply) { m_eotReply = reply; }
    const std::vector<std::unique_ptr<Connection>>& connections() const { return m_connections; }

  private:
    void closeSocket(QTcpSocket* socket) {
        if (socket->state() != QAbstractSocket::ConnectedState) {
            return;
        }
        m_sinceClose.restart();
        m_closedOnce = true;
        socket->disconnectFromHost();
    }

    void accept() {
        while (QTcpSocket* socket = m_server.nextPendingConnection()) {
            m_connections.push_back(std::make_unique<Connection>());
            Connection* c = m_connections.back().get();
            const int index = static_cast<int>(m_connections.size()); // 1-based
            c->plc = std::make_unique<MockPlc>(m_frame);
            if (m_closeAllAfterMs >= 0) {
                QTimer::singleShot(m_closeAllAfterMs, socket, [this, socket]() { closeSocket(socket); });
            } else if (m_refuseWindowMs > 0 && m_closedOnce &&
                       m_sinceClose.elapsed() < m_refuseWindowMs) {
                c->refused = true;
                QTimer::singleShot(m_refuseAfterMs, socket, [this, socket]() { closeSocket(socket); });
            }
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, c, index]() {
                const QByteArray in = socket->readAll();
                c->bytes += in;
                ++c->requests;
                if (c->refused ||
                    (index == m_closeConnection && c->requests == m_closeRequest)) {
                    closeSocket(socket); // no answer: the module drops the link
                    return;
                }
                c->plc->bytesIn(ByteView{reinterpret_cast<const uint8_t*>(in.constData()),
                                         static_cast<size_t>(in.size())});
                ByteView out;
                while (c->plc->nextResponse(out)) {
                    const QByteArray response(reinterpret_cast<const char*>(out.data),
                                              static_cast<QByteArray::size_type>(out.size));
                    QTimer::singleShot(m_delayMs, socket, [this, socket, response]() {
                        if (!m_silent) {
                            socket->write(response);
                        }
                    });
                }
                const bool eot = in == QByteArray::fromHex("04") || in == QByteArray::fromHex("040D0A");
                if (eot && !m_eotReply.isEmpty()) {
                    QTimer::singleShot(m_delayMs, socket, [this, socket]() {
                        if (!m_silent) {
                            socket->write(m_eotReply);
                        }
                    });
                }
            });
        }
    }

    FrameConfig m_frame;
    int m_delayMs{0};
    std::atomic<bool> m_silent{false};
    int m_closeConnection{0};
    int m_closeRequest{0};
    int m_refuseWindowMs{0};
    int m_refuseAfterMs{0};
    int m_closeAllAfterMs{-1};
    QByteArray m_eotReply;
    QElapsedTimer m_sinceClose;
    bool m_closedOnce{false};
    QTcpServer m_server;
    std::vector<std::unique_ptr<Connection>> m_connections;
};

// A virtual_plc process (examples/virtual_plc) that the test starts, restarts and stops.
class VirtualPlc {
  public:
    ~VirtualPlc() { stop(); }

    /// @return empty on success, else why it could not start (the test then skips).
    QString start(const QStringList& args) {
        const QString path = QStringLiteral(MC_VIRTUAL_PLC_PATH);
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            return QStringLiteral("examples/virtual_plc is not built here (%1)").arg(path);
        }
        m_process = std::make_unique<QProcess>();
        m_process->setProcessChannelMode(QProcess::MergedChannels);
        QObject::connect(m_process.get(), &QProcess::readyReadStandardOutput, m_process.get(),
                         [this]() { m_output += m_process->readAllStandardOutput(); });
        m_process->start(path, args);
        if (!m_process->waitForStarted(5000)) {
            return QStringLiteral("virtual_plc did not start: %1").arg(m_process->errorString());
        }
        static const QRegularExpression listening(
            QStringLiteral("listening on (?:127\\.0\\.0\\.1:(\\d+)|(\\S+))"));
        for (int i = 0; i < 100; ++i) {
            m_process->waitForReadyRead(100);
            QCoreApplication::processEvents();
            const QRegularExpressionMatch m = listening.match(QString::fromUtf8(m_output));
            if (m.hasMatch()) {
                m_port = static_cast<quint16>(m.captured(1).toUInt());
                return QString();
            }
            if (m_process->state() != QProcess::Running) {
                break;
            }
        }
        return QStringLiteral("virtual_plc did not report that it listens: %1")
            .arg(QString::fromUtf8(m_output));
    }

    void stop() {
        if (m_process && m_process->state() != QProcess::NotRunning) {
            m_process->kill();
            m_process->waitForFinished(5000);
        }
    }

    quint16 port() const { return m_port; }

  private:
    std::unique_ptr<QProcess> m_process;
    QByteArray m_output;
    quint16 m_port{0};
};

// A copy of an example profile, aimed at virtual_plc, with short timeouts and rounds.
QString writeProfile(const QString& dir, const QString& exampleId, const QString& newId,
                     quint16 tcpPort, const QString& comName, int timeoutMs, int baud = 0) {
    QJsonObject root = readJsonFile(exampleProfilePath(exampleId));
    QJsonObject profile = root.value(QStringLiteral("profile")).toObject();
    profile.insert(QStringLiteral("id"), newId);
    root.insert(QStringLiteral("profile"), profile);
    QJsonObject device = root.value(QStringLiteral("device")).toObject();
    QJsonObject frame = device.value(QStringLiteral("frame")).toObject();
    frame.insert(QStringLiteral("timeoutMs"), timeoutMs);
    device.insert(QStringLiteral("frame"), frame);
    QJsonObject session = device.value(QStringLiteral("session")).toObject();
    session.insert(QStringLiteral("cycleIntervalMs"), 20);
    device.insert(QStringLiteral("session"), session);
    QJsonObject transport = device.value(QStringLiteral("transport")).toObject();
    QJsonObject tcp = transport.value(QStringLiteral("tcp")).toObject();
    tcp.insert(QStringLiteral("host"), QStringLiteral("127.0.0.1"));
    tcp.insert(QStringLiteral("port"), static_cast<int>(tcpPort == 0 ? 1 : tcpPort));
    tcp.insert(QStringLiteral("connectTimeoutMs"), 1000);
    transport.insert(QStringLiteral("tcp"), tcp);
    QJsonObject serial = transport.value(QStringLiteral("serial")).toObject();
    serial.insert(QStringLiteral("portName"), comName.isEmpty() ? QStringLiteral("COM1") : comName);
    if (baud > 0) {
        serial.insert(QStringLiteral("baudRate"), baud);
    }
    transport.insert(QStringLiteral("serial"), serial);
    device.insert(QStringLiteral("transport"), transport);
    root.insert(QStringLiteral("device"), device);
    const QString path = dir + QStringLiteral("/") + newId + QStringLiteral(".json");
    writeJsonFile(path, root);
    return path;
}

struct Counts {
    int passed{-1}, failed{-1}, diverged{-1}, notSupported{-1}, skipped{-1};
};

Counts countsOf(const QString& out) {
    static const QRegularExpression re(QStringLiteral(
        R"(passed (\d+), failed (\d+), diverged (\d+), not supported (\d+), skipped (\d+))"));
    Counts c;
    const QRegularExpressionMatch m = re.match(out);
    if (m.hasMatch()) {
        c.passed = m.captured(1).toInt();
        c.failed = m.captured(2).toInt();
        c.diverged = m.captured(3).toInt();
        c.notSupported = m.captured(4).toInt();
        c.skipped = m.captured(5).toInt();
    }
    return c;
}

std::vector<mc::test::Vector> load(const QString& path) {
    try {
        return mc::test::loadVectors(toStd(path));
    } catch (const std::exception& e) {
        qWarning("cannot load %s: %s", qPrintable(path), e.what());
        return {};
    }
}

const mc::test::Vector* find(const std::vector<mc::test::Vector>& v, const QString& id) {
    for (const mc::test::Vector& x : v) {
        if (x.id == toStd(id)) {
            return &x;
        }
    }
    return nullptr;
}

QString field(const std::vector<mc::test::Vector>& v, const QString& id, const char* key) {
    const mc::test::Vector* x = find(v, id);
    return x == nullptr ? QStringLiteral("<no record %1>").arg(id)
                        : QString::fromStdString(x->field(key));
}

QMap<QString, QString> metaOf(const QString& path) {
    QMap<QString, QString> map;
    for (const QString& line : readAll(path).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const qsizetype colon = line.indexOf(QStringLiteral(": "));
        if (colon > 0) {
            map.insert(line.left(colon), line.mid(colon + 2));
        }
    }
    return map;
}

// Checks one capture set written for plan_3e.json / plan_3c.json; `ethernet` selects the details.
void checkCaptureSet(const QString& folder, const QString& profile, bool ethernet,
                     const QString& forbiddenA, const QString& forbiddenB) {
    for (const char* name : {"run.meta", "steps.vec", "session.vec", "bench.csv"}) {
        QVERIFY2(QFileInfo::exists(folder + QStringLiteral("/") + QLatin1String(name)), name);
    }
    // E-14 is a bench of 3 repetitions after one warm-up: header and three rows.
    const QStringList csv =
        readAll(folder + QStringLiteral("/bench.csv")).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(csv.size(), 4);
    QCOMPARE(csv[0],
             QStringLiteral(
                 "profile,plc_state,step,op,device,count,req_bytes,resp_bytes,rep,ttfb_ms,rx_ms,"
                 "rtt_ms,scan_ms"));
    QVERIFY(csv[1].startsWith(profile + QStringLiteral(",RUN,E-14,ReadWords,D100,1,")));

    const std::vector<mc::test::Vector> steps = load(folder + QStringLiteral("/steps.vec"));
    QVERIFY(!steps.empty());
    const QString p = QStringLiteral("CAP-") + profile + QLatin1Char('-');
    // Every step of the plan that sent something has its records, and every response its outcome.
    for (const char* id : {"E-01", "E-02", "E-02.2", "E-03", "E-03.2", "E-04", "E-05", "E-05.2",
                           "E-06", "E-06+2", "E-07", "E-08", "E-10", "E-10.2", "E-11", "E-13"}) {
        QVERIFY2(find(steps, p + QLatin1String(id)) != nullptr, id);
    }
    for (const mc::test::Vector& v : steps) {
        const QString id = QString::fromStdString(v.id);
        if (id.endsWith(QStringLiteral("-R"))) {
            QVERIFY2(!v.field("outcome").empty(), qPrintable(id));
            QVERIFY2(!v.field("of").empty(), qPrintable(id));
        }
    }
    QCOMPARE(field(steps, p + QStringLiteral("E-01"), "via"), QStringLiteral("api"));
    QCOMPARE(field(steps, p + QStringLiteral("E-01"), "frame"),
             ethernet ? QStringLiteral("3E") : QStringLiteral("3C"));
    QCOMPARE(field(steps, p + QStringLiteral("E-01"), "op"), QStringLiteral("ReadWords"));
    QCOMPARE(field(steps, p + QStringLiteral("E-01"), "device"), QStringLiteral("D100"));
    QCOMPARE(field(steps, p + QStringLiteral("E-01-R"), "outcome"), QStringLiteral("ok"));
    QCOMPARE(field(steps, p + QStringLiteral("E-01-R"), "expect"), QStringLiteral("words 4D2"));
    QCOMPARE(field(steps, p + QStringLiteral("E-02"), "mirrors"),
             ethernet ? QStringLiteral("V-3E-B-05/06") : QStringLiteral("V-3E-A-05/06"));
    QCOMPARE(field(steps, p + QStringLiteral("E-02"), "op"), QStringLiteral("WriteWords"));
    QCOMPARE(field(steps, p + QStringLiteral("E-02.2"), "op"),
             QStringLiteral("ReadWords")); // the read-back
    QCOMPARE(field(steps, p + QStringLiteral("E-02.2-R"), "expect"), QStringLiteral("words 1 2 3"));
    QCOMPARE(field(steps, p + QStringLiteral("E-03"), "op"), QStringLiteral("WriteBits"));
    QCOMPARE(field(steps, p + QStringLiteral("E-03.2-R"), "expect"),
             QStringLiteral("bits 1 1 0 0 1 1 0 0"));
    QCOMPARE(field(steps, p + QStringLiteral("E-05"), "count"),
             QStringLiteral("4")); // 1.5 as four words
    // E-06 reads 1000 words: two commands, one record pair each (the second named +2).
    QCOMPARE(field(steps, p + QStringLiteral("E-06"), "count"), QStringLiteral("960"));
    QCOMPARE(field(steps, p + QStringLiteral("E-06+2"), "count"), QStringLiteral("40"));
    QCOMPARE(field(steps, p + QStringLiteral("E-06+2"), "device"), QStringLiteral("D1060"));
    QCOMPARE(field(steps, p + QStringLiteral("E-06+2-R"), "outcome"), QStringLiteral("ok"));
    QCOMPARE(field(steps, p + QStringLiteral("E-08"), "op"), QStringLiteral("ReadWords"));
    QCOMPARE(field(steps, p + QStringLiteral("E-08"), "device"), QStringLiteral("M115"));
    if (ethernet) {
        // The mock does not know 0403: it answers a PLC error, with the 3E error information.
        QVERIFY2(field(steps, p + QStringLiteral("E-09-R"), "outcome")
                     .startsWith(QStringLiteral("plcError C059 info")),
                 qPrintable(field(steps, p + QStringLiteral("E-09-R"), "outcome")));
        QCOMPARE(field(steps, p + QStringLiteral("E-09"), "via"), QStringLiteral("mutate"));
    }
    // A truncated request: silence. No response record exists, the request record carries the
    // outcome.
    QCOMPARE(field(steps, p + QStringLiteral("E-10"), "via"), QStringLiteral("mutate"));
    QCOMPARE(field(steps, p + QStringLiteral("E-10"), "outcome"), QStringLiteral("timeout"));
    QVERIFY(!field(steps, p + QStringLiteral("E-10"), "waited_ms").isEmpty());
    QVERIFY(find(steps, p + QStringLiteral("E-10-R")) == nullptr);
    // ... and the next read, sent after the recovery, succeeds.
    QCOMPARE(field(steps, p + QStringLiteral("E-10.2-R"), "outcome"), QStringLiteral("ok"));
    QCOMPARE(field(steps, p + QStringLiteral("E-10.2-R"), "expect"), QStringLiteral("words 4D2"));
    // Two requests in one write: one raw exchange.
    QCOMPARE(field(steps, p + QStringLiteral("E-11"), "via"), QStringLiteral("raw"));
    QCOMPARE(field(steps, p + QStringLiteral("E-11"), "op"), QStringLiteral("Raw"));
    QVERIFY(find(steps, p + QStringLiteral("E-11-R")) != nullptr);
    // The poll step has no record here; the bench step and the skipped step have none either.
    for (const char* id : {"E-12", "E-14", "E-15"}) {
        QVERIFY2(find(steps, p + QLatin1String(id)) == nullptr, id);
    }

    // The poll transcript.
    const std::vector<mc::test::Vector> session = load(folder + QStringLiteral("/session.vec"));
    QVERIFY(!session.empty());
    int tx = 0, rx = 0, cycles = 0, snapshots = 0, finished = 0, changedD = 0, changedM = 0;
    qint64 lastSeq = 0;
    bool chunksFirst = true;
    bool sawEvent = false;
    for (const mc::test::Vector& v : session) {
        const QString kind = QString::fromStdString(v.field("kind"));
        const QString event = QString::fromStdString(v.field("event"));
        QVERIFY2(QString::fromStdString(v.field("step")) == QStringLiteral("E-12"),
                 "session record of another step");
        QVERIFY(!v.field("t_ns").empty());
        const qint64 seq = QString::fromStdString(v.field("seq")).toLongLong();
        if (kind == QStringLiteral("event")) {
            sawEvent = true;
        } else if (kind == QStringLiteral("input")) {
            QVERIFY(!v.field("input").empty());
        } else if (sawEvent) {
            chunksFirst = false;
        }
        QVERIFY2(seq > 0, "seq");
        lastSeq = std::max(lastSeq, seq);
        tx += kind == QStringLiteral("tx") ? 1 : 0;
        rx += kind == QStringLiteral("rx") ? 1 : 0;
        cycles += event == QStringLiteral("cycleDone") ? 1 : 0;
        snapshots += event == QStringLiteral("snapshot") ? 1 : 0;
        finished += event == QStringLiteral("requestFinished") ? 1 : 0;
        if (event == QStringLiteral("valuesChanged")) {
            changedD += QString::fromStdString(v.field("type")) == QStringLiteral("D") ? 1 : 0;
            changedM += QString::fromStdString(v.field("type")) == QStringLiteral("M") ? 1 : 0;
        }
    }
    QVERIFY2(chunksFirst, "session.vec lists wire chunks before events");
    QVERIFY(tx > 0 && rx > 0);
    QVERIFY2(cycles >= 5, qPrintable(QString::number(cycles)));
    QVERIFY(snapshots >= 2);
    QVERIFY2(finished >= 1, "the poll's ad-hoc write finished");
    QVERIFY2(changedD >= 1, "D101 changed after the write of the poll");
    QVERIFY2(changedM >= 1, "the heartbeat bit toggles");
    QVERIFY(lastSeq >= tx + rx + cycles);

    // run.meta: identity, settings, the recovery, the skipped steps; no address, port or COM name.
    const QMap<QString, QString> meta = metaOf(folder + QStringLiteral("/run.meta"));
    // The commit and the date are digits and hex: leave them out of the substring checks below.
    QString metaText;
    const QStringList metaLines =
        readAll(folder + QStringLiteral("/run.meta")).split(QLatin1Char('\n'));
    for (const QString& line : metaLines) {
        if (!line.startsWith(QStringLiteral("git_commit:")) &&
            !line.startsWith(QStringLiteral("date:"))) {
            metaText += line + QLatin1Char('\n');
        }
    }
    QCOMPARE(meta.value(QStringLiteral("profile")), profile);
    QCOMPARE(meta.value(QStringLiteral("operator_note")),
             QStringLiteral("virtual_plc fixture, not hardware"));
    QCOMPARE(meta.value(QStringLiteral("plc_state")), QStringLiteral("RUN"));
    QVERIFY(meta.contains(QStringLiteral("git_commit")) &&
            !meta.value(QStringLiteral("date")).isEmpty());
    QVERIFY(meta.contains(QStringLiteral("frame.timeoutMs")) &&
            meta.contains(QStringLiteral("session.cycleIntervalMs")));
    QVERIFY(
        meta.value(QStringLiteral("skipped.E-15")).contains(QStringLiteral("requires scanTime")));
    // The poll step E-12 has a transcript in session.vec, and run.meta says so (the bench E-14
    // keeps none).
    QCOMPARE(meta.value(QStringLiteral("polls")), QStringLiteral("E-12"));
    if (ethernet) {
        // E-09 and E-10 both declare "recover": "reconnect": the link is brought back after each.
        QVERIFY2(meta.value(QStringLiteral("recovery.1")).contains(QStringLiteral("after E-09")),
                 qPrintable(metaText));
        QVERIFY2(meta.value(QStringLiteral("recovery.2")).contains(QStringLiteral("after E-10")),
                 qPrintable(metaText));
    }
    QVERIFY(!metaText.contains(forbiddenA));
    QVERIFY(!metaText.contains(forbiddenB));
    QVERIFY(!metaText.contains(QStringLiteral("127.0.0.1")));
}

// ---- HIL-07 / HIL-08 helpers ---------------------------------------------------------------------

// The module of F-012: it closes the first connection at its first request (a request it dislikes),
// and it closes a link opened less than kRefuseWindowMs after one of its own closes, kRefuseAfterMs
// after the connection was accepted.
constexpr int kRefuseWindowMs = 150;
constexpr int kRefuseAfterMs = 25;

struct PlanRun {
    ToolRun run;
    QString folder; ///< The capture folder of the run.
};

// H-01 makes the fake close the link (no answer); its `then` read follows on the reconnected link;
// H-02 is a plain read after that. @p settleMs is Options::settleMs (-1: the tool's default).
PlanRun runSettlePlan(const QString& name, int settleMs, PerConnectionPlc& server) {
    PlanRun result;
    server.closeOnRequest(1, 1);
    server.refuseNearLastClose(kRefuseWindowMs, kRefuseAfterMs);
    const QString dir = scratchDir(name);
    QFile planFile(dir + QStringLiteral("/plan.json"));
    if (!planFile.open(QIODevice::WriteOnly)) {
        return result;
    }
    planFile.write(R"JSON({"schema":1,"plan":{"id":"settle"},"steps":[
        {"id":"H-01","kind":"read","device":"D@s","expect":"record",
         "then":[{"kind":"read","device":"D@s","expect":"ok"}]},
        {"id":"H-02","kind":"read","device":"D@s","expect":"ok"}
    ]})JSON");
    planFile.close();
    Options options;
    options.profilePath = writeProfile(dir, QStringLiteral("q03ude-eth-3e-bin"), name,
                                       server.port(), QString(), 400);
    options.planPath = dir + QStringLiteral("/plan.json");
    options.outputRoot = dir + QStringLiteral("/captured");
    options.yes = true;
    options.settleMs = settleMs;
    result.run = runToolWith(options);
    result.folder = options.outputRoot + QLatin1Char('/') + name;
    return result;
}

// A copy of the 3C Format 4 example profile whose transport is TCP to 127.0.0.1:@p port (a serial
// device server): the in-process fake serves the 3C frames of a `recover: eot` step.
QString writeProfile3cOverTcp(const QString& dir, const QString& newId, quint16 port,
                              int timeoutMs) {
    const QString path =
        writeProfile(dir, QStringLiteral("q03ude-c24-3c-f4"), newId, port, QString(), timeoutMs);
    QJsonObject root = readJsonFile(path);
    QJsonObject device = root.value(QStringLiteral("device")).toObject();
    QJsonObject transport = device.value(QStringLiteral("transport")).toObject();
    transport.insert(QStringLiteral("kind"), QStringLiteral("Tcp"));
    device.insert(QStringLiteral("transport"), transport);
    root.insert(QStringLiteral("device"), device);
    writeJsonFile(path, root);
    return path;
}

// N-01: a truncated read (no answer, the C24 waits for the rest), then `recover: eot`, then a
// normal read. The EOT exchange is "N-01+2". @p eotReply is what the fake answers to the EOT.
PlanRun runEotPlan(const QString& name, const QByteArray& eotReply) {
    PlanRun result;
    const Profile q = loadExample(QStringLiteral("q03ude-c24-3c-f4"));
    PerConnectionPlc server(q.device.frame);
    server.setEotReply(eotReply);
    if (!server.listen()) {
        return result;
    }
    const QString dir = scratchDir(name);
    QFile planFile(dir + QStringLiteral("/plan.json"));
    if (!planFile.open(QIODevice::WriteOnly)) {
        return result;
    }
    planFile.write(R"JSON({"schema":1,"plan":{"id":"eot"},"steps":[
        {"id":"N-01","kind":"mutate","request":{"kind":"read","device":"D@s"},
         "edits":[{"truncate":1}],"readOnly":true,"recover":"eot","expect":"record",
         "then":[{"kind":"read","device":"D@s","expect":"ok"}]}
    ]})JSON");
    planFile.close();
    Options options;
    options.profilePath = writeProfile3cOverTcp(dir, name, server.port(), 400);
    options.planPath = dir + QStringLiteral("/plan.json");
    options.outputRoot = dir + QStringLiteral("/captured");
    options.yes = true;
    result.run = runToolWith(options, name + QLatin1Char('\n'));
    result.folder = options.outputRoot + QLatin1Char('/') + name;
    return result;
}

class HilRunTests : public QObject {
    Q_OBJECT

  private slots:
    void HIL_04_endToEndOverLoopbackTcp() {
        VirtualPlc plc;
        const QString why =
            plc.start({QStringLiteral("--frame"), QStringLiteral("3E"), QStringLiteral("--code"),
                       QStringLiteral("Binary"), QStringLiteral("--port"), QStringLiteral("0"),
                       QStringLiteral("--set"), QStringLiteral("D100=1234")});
        if (!why.isEmpty()) {
            QSKIP(qPrintable(why));
        }
        const QString dir = scratchDir(QStringLiteral("run_tcp"));
        Options options;
        options.profilePath =
            writeProfile(dir, QStringLiteral("q03ude-eth-3e-bin"), QStringLiteral("e2e-3e-bin"),
                         plc.port(), QString(), 400);
        options.planPath = planPath("plan_3e.json");
        options.outputRoot = dir + QStringLiteral("/captured");
        options.yes = true;
        options.note = QStringLiteral("virtual_plc fixture, not hardware");
        const ToolRun run = runToolWith(options, QStringLiteral("e2e-3e-bin\n"));
        QVERIFY2(static_cast<int>(run.code) == static_cast<int>(ExitCode::Ok),
                 qPrintable(run.out + run.err));
        const Counts c = countsOf(run.out);
        QVERIFY2(c.passed == 14 && c.failed == 0 && c.diverged == 0 && c.notSupported == 0 &&
                     c.skipped == 1,
                 qPrintable(run.out));
        QVERIFY(run.out.contains(QStringLiteral("capture written to")));
        checkCaptureSet(options.outputRoot + QStringLiteral("/e2e-3e-bin"),
                        QStringLiteral("e2e-3e-bin"), true, QString::number(plc.port()),
                        QStringLiteral("COM1"));
        // Nothing virtual goes to the hardware-only folder.
        QVERIFY(!QDir(testsDir() + QStringLiteral("/vectors/captured/e2e-3e-bin")).exists());

        // HIL-04, replay half: the capture this run wrote replays green (RPL-01..06).
        int replayExit = 0;
        const QString replayOut = runReplay(options.outputRoot, &replayExit);
        if (replayExit == -1) {
            QSKIP(qPrintable(replayOut));
        }
        QVERIFY2(replayExit == 0, qPrintable(replayOut));

        // A second run replaces the folder.
        QFile stale(options.outputRoot + QStringLiteral("/e2e-3e-bin/stale.txt"));
        QVERIFY(stale.open(QIODevice::WriteOnly));
        stale.close();
        const ToolRun again = runToolWith(options, QStringLiteral("e2e-3e-bin\n"));
        QCOMPARE(static_cast<int>(again.code), static_cast<int>(ExitCode::Ok));
        QVERIFY(!QFileInfo::exists(options.outputRoot + QStringLiteral("/e2e-3e-bin/stale.txt")));
    }

    void HIL_04_aLostLinkIsRecordedTheToolReconnectsAndTheRunGoesOn() {
        VirtualPlc plc;
        const QStringList args{QStringLiteral("--frame"), QStringLiteral("3E"),
                               QStringLiteral("--code"),  QStringLiteral("Binary"),
                               QStringLiteral("--set"),   QStringLiteral("D100=1234")};
        QString why = plc.start(args + QStringList{QStringLiteral("--port"), QStringLiteral("0")});
        if (!why.isEmpty()) {
            QSKIP(qPrintable(why));
        }
        const quint16 port = plc.port();
        const QString dir = scratchDir(QStringLiteral("run_fault"));
        Options options;
        options.profilePath = writeProfile(dir, QStringLiteral("q03ude-eth-3e-bin"),
                                           QStringLiteral("e2e-fault"), port, QString(), 400);
        options.planPath = planPath("plan_fault.json");
        options.outputRoot = dir + QStringLiteral("/captured");
        options.yes = true;

        // virtual_plc dies before F-03 and comes back 1.5 s later, on the same port.
        QTimer restart;
        restart.setSingleShot(true);
        QObject::connect(&restart, &QTimer::timeout, &restart, [&]() {
            const QString again =
                plc.start(args + QStringList{QStringLiteral("--port"), QString::number(port)});
            QVERIFY2(again.isEmpty(), qPrintable(again));
        });
        ToolIo hook;
        QString outText;
        QString errText;
        QString inText;
        QTextStream out(&outText);
        QTextStream err(&errText);
        QTextStream in(&inText);
        hook.out = &out;
        hook.err = &err;
        hook.in = &in;
        hook.stepStarted = [&](const QString& id) {
            if (id == QStringLiteral("F-03")) {
                plc.stop();
                restart.start(1500);
            }
        };
        const ExitCode code = runTool(options, hook);
        out.flush();
        // F-03 fails (the link died under it); every other step passes, including the ones after
        // the restart.
        QCOMPARE(static_cast<int>(code), static_cast<int>(ExitCode::RunFailures));
        const Counts c = countsOf(outText);
        QVERIFY2(c.passed == 4 && c.failed == 1 && c.diverged == 0 && c.skipped == 0,
                 qPrintable(outText));
        QVERIFY2(outText.contains(QRegularExpression(QStringLiteral(R"(FAIL\s+F-03)"))),
                 qPrintable(outText));
        QVERIFY(outText.contains(QStringLiteral("recovery after F-03: reconnected")));

        const QString folder = options.outputRoot + QStringLiteral("/e2e-fault");
        const QMap<QString, QString> meta = metaOf(folder + QStringLiteral("/run.meta"));
        QVERIFY2(meta.value(QStringLiteral("recovery.1")).contains(QStringLiteral("after F-03")),
                 qPrintable(readAll(folder + QStringLiteral("/run.meta"))));
        QVERIFY(
            meta.value(QStringLiteral("recovery.1")).contains(QStringLiteral("reconnected in")));
        const std::vector<mc::test::Vector> steps = load(folder + QStringLiteral("/steps.vec"));
        const QString p = QStringLiteral("CAP-e2e-fault-");
        QVERIFY(find(steps, p + QStringLiteral("F-05")) !=
                nullptr); // the steps after the fault ran
        QCOMPARE(field(steps, p + QStringLiteral("F-04-R"), "expect"), QStringLiteral("words 4D2"));
        QCOMPARE(field(steps, p + QStringLiteral("F-05.2-R"), "outcome"), QStringLiteral("ok"));
        // The faulted step keeps its own outcome: its request has no usable answer.
        const mc::test::Vector* f3 = find(steps, p + QStringLiteral("F-03"));
        if (f3 != nullptr) {
            const QString outcome = field(steps, p + QStringLiteral("F-03-R"), "outcome");
            const QString requestOutcome = QString::fromStdString(f3->field("outcome"));
            QVERIFY2(outcome != QStringLiteral("ok") && requestOutcome != QStringLiteral("ok"),
                     qPrintable(outcome + requestOutcome));
        }
    }

    void HIL_04_outcomesAreClassifiedAndTheExitCodeSaysSo() {
        VirtualPlc plc;
        const QString why =
            plc.start({QStringLiteral("--frame"), QStringLiteral("3E"), QStringLiteral("--code"),
                       QStringLiteral("Binary"), QStringLiteral("--port"), QStringLiteral("0"),
                       QStringLiteral("--set"), QStringLiteral("D100=1234")});
        if (!why.isEmpty()) {
            QSKIP(qPrintable(why));
        }
        const QString dir = scratchDir(QStringLiteral("run_classes"));
        QFile planFile(dir + QStringLiteral("/plan.json"));
        QVERIFY(planFile.open(QIODevice::WriteOnly));
        planFile.write(R"JSON({"schema":1,"plan":{"id":"classes"},"steps":[
            {"id":"N-01","kind":"read","device":"D@s","expect":{"kind":"ok","values":[1234]}},
            {"id":"N-02","kind":"mutate","request":{"kind":"read","device":"D@s"},
                "edits":[{"setByte":{"at":11,"value":"0x03"}}],"readOnly":true,
                "recover":"reconnect","expect":"ok"},
            {"id":"N-03","kind":"read","device":"D@s","expect":"plcError"},
            {"id":"N-04","kind":"read","device":"D@s","expect":{"kind":"ok","values":[999]}},
            {"id":"N-05","kind":"read","device":"M@s16+3","unit":"word","expect":"notSent"},
            {"id":"N-06","kind":"read","device":"D@s","expect":"notSent"},
            {"id":"N-07","kind":"read","device":"D@s","expect":"timeout"}
        ]})JSON");
        planFile.close();
        Options options;
        options.profilePath =
            writeProfile(dir, QStringLiteral("q03ude-eth-3e-bin"), QStringLiteral("e2e-classes"),
                         plc.port(), QString(), 400);
        options.planPath = dir + QStringLiteral("/plan.json");
        options.outputRoot = dir + QStringLiteral("/captured");
        options.yes = true;
        const ToolRun run = runToolWith(options, QStringLiteral("e2e-classes\n"));
        // N-01 pass; N-02 not supported (PLC error where ok was expected); N-03 diverged (ok where
        // an error was expected); N-04 failed (value differs); N-05 diverged (3E accepts what the
        // plan expected the library to refuse); N-06 diverged; N-07 diverged (an answer where
        // silence was expected).
        QCOMPARE(static_cast<int>(run.code), static_cast<int>(ExitCode::RunFailures));
        const Counts c = countsOf(run.out);
        QVERIFY2(c.passed == 1 && c.failed == 1 && c.diverged == 4 && c.notSupported == 1,
                 qPrintable(run.out));
        QVERIFY(run.out.contains(QRegularExpression(QStringLiteral(R"(UNSUPPORTED\s+N-02)"))));
        QVERIFY(run.out.contains(QRegularExpression(QStringLiteral(R"(DIVERGED\s+N-03)"))));
        QVERIFY(run.out.contains(
            QRegularExpression(QStringLiteral(R"(FAIL\s+N-04.*value 0 is 1234, expected 999)"))));
    }

    void HIL_04_aRefusedRequestIsNotSentAndListedInRunMeta() {
        VirtualPlc plc;
        const QString why =
            plc.start({QStringLiteral("--frame"), QStringLiteral("1E"), QStringLiteral("--code"),
                       QStringLiteral("Binary"), QStringLiteral("--port"), QStringLiteral("0")});
        if (!why.isEmpty()) {
            QSKIP(qPrintable(why));
        }
        const QString dir = scratchDir(QStringLiteral("run_notsent"));
        QFile planFile(dir + QStringLiteral("/plan.json"));
        QVERIFY(planFile.open(QIODevice::WriteOnly));
        // 1E refuses word access to a bit device whose head is not a multiple of 16 (DEV-12).
        planFile.write(R"JSON({"schema":1,"plan":{"id":"notsent"},"steps":[
            {"id":"R-01","kind":"read","device":"M@s16+3","unit":"word","expect":"notSent"},
            {"id":"R-02","kind":"read","device":"D@s","expect":"ok"}
        ]})JSON");
        planFile.close();
        Options options;
        options.profilePath = writeProfile(dir, QStringLiteral("fx3-eth-1e-bin"),
                                           QStringLiteral("e2e-1e"), plc.port(), QString(), 400);
        options.planPath = dir + QStringLiteral("/plan.json");
        options.outputRoot = dir + QStringLiteral("/captured");
        options.yes = true;
        const ToolRun run = runToolWith(options);
        QVERIFY2(static_cast<int>(run.code) == static_cast<int>(ExitCode::Ok),
                 qPrintable(run.out + run.err));
        QVERIFY2(countsOf(run.out).passed == 2, qPrintable(run.out));
        const QString folder = options.outputRoot + QStringLiteral("/e2e-1e");
        const QMap<QString, QString> meta = metaOf(folder + QStringLiteral("/run.meta"));
        QCOMPARE(meta.value(QStringLiteral("notsent.R-01")),
                 QStringLiteral("notSent InvalidDevice"));
        QVERIFY2(!meta.contains(QStringLiteral("polls")), "no poll step ran");
        const std::vector<mc::test::Vector> steps = load(folder + QStringLiteral("/steps.vec"));
        QVERIFY(find(steps, QStringLiteral("CAP-e2e-1e-R-01")) ==
                nullptr); // nothing was sent, no record
        QVERIFY(find(steps, QStringLiteral("CAP-e2e-1e-R-02-R")) != nullptr);
        QCOMPARE(field(steps, QStringLiteral("CAP-e2e-1e-R-02"), "frame"), QStringLiteral("1E"));
    }

    void HIL_04_aPollPromptWaitsForTheOperator() {
        VirtualPlc plc;
        const QString why =
            plc.start({QStringLiteral("--frame"), QStringLiteral("3E"), QStringLiteral("--code"),
                       QStringLiteral("Binary"), QStringLiteral("--port"), QStringLiteral("0")});
        if (!why.isEmpty()) {
            QSKIP(qPrintable(why));
        }
        const QString dir = scratchDir(QStringLiteral("run_prompt"));
        QFile planFile(dir + QStringLiteral("/plan.json"));
        QVERIFY(planFile.open(QIODevice::WriteOnly));
        planFile.write(R"JSON({"schema":1,"plan":{"id":"prompt"},"steps":[
            {"id":"P-01","kind":"poll","rounds":3,"subscribe":[{"name":"d","device":"D@s",
                "count":2}],
             "actions":[{"after":1,"prompt":"Pull the cable now"}]},
            {"id":"P-02","kind":"read","device":"D@s"}
        ]})JSON");
        planFile.close();
        Options options;
        options.profilePath =
            writeProfile(dir, QStringLiteral("q03ude-eth-3e-bin"), QStringLiteral("e2e-prompt"),
                         plc.port(), QString(), 400);
        options.planPath = dir + QStringLiteral("/plan.json");
        options.outputRoot = dir + QStringLiteral("/captured");
        options.yes = true;
        const ToolRun run = runToolWith(options, QStringLiteral("\n"));
        QVERIFY2(static_cast<int>(run.code) == static_cast<int>(ExitCode::Ok),
                 qPrintable(run.out + run.err));
        QVERIFY(run.out.contains(QStringLiteral("Pull the cable now (press Enter to continue)")));
        QCOMPARE(countsOf(run.out).passed, 2);
    }

    void HIL_04_aTimeoutFaultsTheLinkAndTheToolReconnects() {
        // A server that accepts and never answers: every request times out, McDevice reports a
        // LinkFault and the tool reconnects before the next step.
        QTcpServer silent;
        QVERIFY(silent.listen(QHostAddress::LocalHost, 0));
        const QString dir = scratchDir(QStringLiteral("run_silent"));
        QFile planFile(dir + QStringLiteral("/plan.json"));
        QVERIFY(planFile.open(QIODevice::WriteOnly));
        planFile.write(R"JSON({"schema":1,"plan":{"id":"silent"},"steps":[
            {"id":"T-01","kind":"read","device":"D@s","expect":"timeout"},
            {"id":"T-02","kind":"read","device":"D@s","expect":"noResponse"},
            {"id":"T-03","kind":"read","device":"D@s","expect":"ok"}
        ]})JSON");
        planFile.close();
        Options options;
        options.profilePath =
            writeProfile(dir, QStringLiteral("q03ude-eth-3e-bin"), QStringLiteral("e2e-silent"),
                         silent.serverPort(), QString(), 300);
        options.planPath = dir + QStringLiteral("/plan.json");
        options.outputRoot = dir + QStringLiteral("/captured");
        options.yes = true;
        const ToolRun run = runToolWith(options);
        // T-01 and T-02 expected silence and got it; T-03 expected an answer and did not get one.
        QCOMPARE(static_cast<int>(run.code), static_cast<int>(ExitCode::RunFailures));
        const Counts c = countsOf(run.out);
        QVERIFY2(c.passed == 2 && c.failed == 1, qPrintable(run.out));
        const QString folder = options.outputRoot + QStringLiteral("/e2e-silent");
        const QMap<QString, QString> meta = metaOf(folder + QStringLiteral("/run.meta"));
        QVERIFY2(meta.value(QStringLiteral("recovery.1")).contains(QStringLiteral("after T-01")),
                 qPrintable(readAll(folder + QStringLiteral("/run.meta"))));
        QVERIFY(meta.value(QStringLiteral("recovery.2")).contains(QStringLiteral("after T-02")));
        QVERIFY(meta.value(QStringLiteral("recovery.3")).contains(QStringLiteral("after T-03")));
        QVERIFY(meta.value(QStringLiteral("recovery.1")).contains(QStringLiteral("reconnected")));
        // The requests are on record, with the outcome on the request (no byte ever came back).
        const std::vector<mc::test::Vector> steps = load(folder + QStringLiteral("/steps.vec"));
        const QString p = QStringLiteral("CAP-e2e-silent-");
        QCOMPARE(field(steps, p + QStringLiteral("T-01"), "outcome"), QStringLiteral("timeout"));
        QCOMPARE(field(steps, p + QStringLiteral("T-02"), "outcome"), QStringLiteral("noResponse"));
        QVERIFY(find(steps, p + QStringLiteral("T-01-R")) == nullptr);
    }

    void HIL_04_aPlcThatIsNotThereAbortsTheRunAfterOneBudget() {
        quint16 closedPort = 0;
        {
            QTcpServer probe;
            QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
            closedPort = probe.serverPort();
        }
        const QString dir = scratchDir(QStringLiteral("run_nobody"));
        QFile planFile(dir + QStringLiteral("/plan.json"));
        QVERIFY(planFile.open(QIODevice::WriteOnly));
        planFile.write(R"JSON({"schema":1,"plan":{"id":"nobody"},"steps":[
            {"id":"X-01","kind":"read","device":"D@s"},
            {"id":"X-02","kind":"read","device":"D@s"},
            {"id":"X-03","kind":"poll","rounds":2,"subscribe":[{"name":"d","device":"D@s",
                "count":1}]}
        ]})JSON");
        planFile.close();
        Options options;
        options.profilePath =
            writeProfile(dir, QStringLiteral("q03ude-eth-3e-bin"), QStringLiteral("e2e-nobody"),
                         closedPort, QString(), 400);
        options.planPath = dir + QStringLiteral("/plan.json");
        options.outputRoot = dir + QStringLiteral("/captured");
        options.yes = true;
        options.reconnectSeconds = 2;
        QElapsedTimer timer;
        timer.start();
        const ToolRun run = runToolWith(options);
        // One budget is spent on the first step; the others are not even tried.
        QVERIFY2(timer.elapsed() < 12000, qPrintable(QString::number(timer.elapsed())));
        QCOMPARE(static_cast<int>(run.code), static_cast<int>(ExitCode::RunFailures));
        QCOMPARE(countsOf(run.out).failed, 3);
        QVERIFY(run.out.contains(QStringLiteral("aborted, the link could not be restored")));
        // The capture set is still written, so the failure is on record.
        const QString folder = options.outputRoot + QStringLiteral("/e2e-nobody");
        QVERIFY(QFileInfo::exists(folder + QStringLiteral("/run.meta")));
        QVERIFY(QFileInfo::exists(folder + QStringLiteral("/steps.vec")));
        QVERIFY(QFileInfo::exists(folder + QStringLiteral("/session.vec")));
    }

    void HIL_04_roundsAndDeadlinesKeepRunningWhileAPromptIsOpen() {
        VirtualPlc plc;
        const QString why =
            plc.start({QStringLiteral("--frame"), QStringLiteral("3E"), QStringLiteral("--code"),
                       QStringLiteral("Binary"), QStringLiteral("--port"), QStringLiteral("0")});
        if (!why.isEmpty()) {
            QSKIP(qPrintable(why));
        }
        const QString dir = scratchDir(QStringLiteral("run_prompt_open"));
        QFile planFile(dir + QStringLiteral("/plan.json"));
        QVERIFY(planFile.open(QIODevice::WriteOnly));
        planFile.write(R"JSON({"schema":1,"plan":{"id":"promptopen"},"steps":[
            {"id":"P-01","kind":"poll","rounds":3,"subscribe":[{"name":"d","device":"D@s",
                "count":2}],
             "actions":[{"after":1,"prompt":"Pull the cable"}]}
        ]})JSON");
        planFile.close();
        Options options;
        options.profilePath =
            writeProfile(dir, QStringLiteral("q03ude-eth-3e-bin"), QStringLiteral("e2e-promptopen"),
                         plc.port(), QString(), 400);
        options.planPath = dir + QStringLiteral("/plan.json");
        options.outputRoot = dir + QStringLiteral("/captured");
        options.yes = true;
        // The operator answers after one second; reading the answer blocks a thread, not the tool.
        GatedInput input;
        QTimer answer;
        answer.setSingleShot(true);
        QObject::connect(&answer, &QTimer::timeout, &answer, [&]() { input.answer(); });
        answer.start(1000);
        QString outText;
        QString errText;
        QTextStream out(&outText);
        QTextStream err(&errText);
        QTextStream in(&input);
        QElapsedTimer timer;
        timer.start();
        const ExitCode code = runTool(options, ToolIo{&out, &err, &in});
        out.flush();
        QVERIFY2(static_cast<int>(code) == static_cast<int>(ExitCode::Ok),
                 qPrintable(outText + errText));
        QVERIFY2(timer.elapsed() >= 900, qPrintable(QString::number(timer.elapsed())));
        // A 20 ms round that cannot run while the prompt is open leaves a gap of about a second in
        // the transcript; here the rounds go on, the stamps show it.
        const std::vector<mc::test::Vector> session =
            load(options.outputRoot + QStringLiteral("/e2e-promptopen/session.vec"));
        QVector<qint64> stamps;
        for (const mc::test::Vector& v : session) {
            if (v.field("event") == "cycleDone") {
                stamps.push_back(QString::fromStdString(v.field("t_ns")).toLongLong());
            }
        }
        QVERIFY2(stamps.size() >= 25, qPrintable(QString::number(stamps.size())));
        qint64 widest = 0;
        for (int i = 1; i < stamps.size(); ++i) {
            widest = std::max(widest, stamps[i] - stamps[i - 1]);
        }
        QVERIFY2(widest < 300000000LL, qPrintable(QString::number(widest)));
    }

    void HIL_06_benchRunsWriteRowsAStopPassAddsRowsAndTheReportShowsBoth() {
        VirtualPlc plc;
        const QString why =
            plc.start({QStringLiteral("--frame"), QStringLiteral("3E"), QStringLiteral("--code"),
                       QStringLiteral("Binary"), QStringLiteral("--port"), QStringLiteral("0")});
        if (!why.isEmpty()) {
            QSKIP(qPrintable(why));
        }
        const QString dir = scratchDir(QStringLiteral("run_bench"));
        Options options;
        options.profilePath = writeProfile(dir, QStringLiteral("q03ude-eth-3e-bin"),
                                           QStringLiteral("e2e-bench"), plc.port(), QString(), 400);
        options.planPath = planPath("plan_bench.json");
        options.outputRoot = dir + QStringLiteral("/captured");
        options.only = QStringList{QStringLiteral("GB")};
        options.benchReps = 4;
        options.note = QStringLiteral("virtual_plc fixture, not hardware");

        // The state the operator set is part of what they confirm.
        options.plcState = QStringLiteral("STOP");
        ToolRun asked = runToolWith(options, QStringLiteral("no\n"));
        QVERIFY(asked.out.contains(QStringLiteral("Operator-set state: STOP")));
        QCOMPARE(static_cast<int>(asked.code), static_cast<int>(ExitCode::BadInput));

        options.yes = true;
        options.plcState = QStringLiteral("RUN");
        const ToolRun first = runToolWith(options);
        QVERIFY2(static_cast<int>(first.code) == static_cast<int>(ExitCode::Ok),
                 qPrintable(first.out + first.err));
        QCOMPARE(countsOf(first.out).passed, 4);
        const QString folder = options.outputRoot + QStringLiteral("/e2e-bench");
        const auto rows = [&]() {
            QVector<QStringList> r;
            const QStringList lines = readAll(folder + QStringLiteral("/bench.csv"))
                                          .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (int i = 1; i < lines.size(); ++i) {
                r.push_back(lines[i].split(QLatin1Char(',')));
            }
            return r;
        };
        QVector<QStringList> r = rows();
        QCOMPARE(r.size(), 16); // 4 steps x 4 repetitions, the warm-up is not written
        for (const QStringList& c : r) {
            QCOMPARE(c.size(), 13);
            QCOMPARE(c[0], QStringLiteral("e2e-bench"));
            QCOMPARE(c[1], QStringLiteral("RUN"));
        }
        const auto rowsOf = [&](const QString& step, const QString& state) {
            QVector<QStringList> out;
            for (const QStringList& c : rows()) {
                if (c[2] == step && c[1] == state) {
                    out.push_back(c);
                }
            }
            return out;
        };
        QCOMPARE(rowsOf(QStringLiteral("GB-01"), QStringLiteral("RUN")).size(), 4);
        const QStringList gb02 = rowsOf(QStringLiteral("GB-02"), QStringLiteral("RUN"))[3];
        QCOMPARE(gb02[3], QStringLiteral("ReadWords"));
        QCOMPARE(gb02[4], QStringLiteral("D100"));
        QCOMPARE(gb02[5], QStringLiteral("64"));
        QCOMPARE(gb02[6], QStringLiteral("21"));  // a 3E Binary read request
        QCOMPARE(gb02[7], QStringLiteral("139")); // 11 header bytes + 64 words
        QCOMPARE(gb02[8], QStringLiteral("4"));
        QVERIFY(gb02[9].toDouble() > 0 && gb02[11].toDouble() >= gb02[9].toDouble());
        QCOMPARE(gb02[12], QString()); // no scanTimeDevice in this profile
        const QStringList poll = rowsOf(QStringLiteral("GB-10"), QStringLiteral("RUN"))[0];
        QCOMPARE(poll[3], QStringLiteral("PollRound"));
        QCOMPARE(poll[4], QStringLiteral("G6"));
        QVERIFY(poll[9].isEmpty() && poll[10].isEmpty()); // a round has no ttfb or rx
        QVERIFY(!poll[11].isEmpty()); // its duration, from CycleInfo::durationMs
        QVERIFY(poll[6].toInt() > 0 && poll[7].toInt() > 0); // bytes of the round

        // A STOP pass adds STOP rows, keeps the RUN rows and the RUN pass's files.
        options.plcState = QStringLiteral("STOP");
        options.benchReps = 3;
        const QString runMetaBefore = readAll(folder + QStringLiteral("/run.meta"));
        const ToolRun stop = runToolWith(options);
        QVERIFY2(static_cast<int>(stop.code) == static_cast<int>(ExitCode::Ok),
                 qPrintable(stop.out + stop.err));
        QCOMPARE(rowsOf(QStringLiteral("GB-01"), QStringLiteral("RUN")).size(), 4);
        QCOMPARE(rowsOf(QStringLiteral("GB-01"), QStringLiteral("STOP")).size(), 3);
        QCOMPARE(rowsOf(QStringLiteral("GB-10"), QStringLiteral("STOP")).size(), 3);
        const QString runMetaAfter = readAll(folder + QStringLiteral("/run.meta"));
        QVERIFY(runMetaAfter.startsWith(runMetaBefore.trimmed()));
        QVERIFY(runMetaAfter.contains(QStringLiteral("plc_state: RUN")));
        QVERIFY(runMetaAfter.contains(QStringLiteral("stop_pass.plc_state: STOP")));
        // A second STOP pass replaces the STOP rows instead of piling them up.
        options.benchReps = 2;
        QVERIFY(static_cast<int>(runToolWith(options).code) == static_cast<int>(ExitCode::Ok));
        QCOMPARE(rowsOf(QStringLiteral("GB-01"), QStringLiteral("STOP")).size(), 2);
        QCOMPARE(rowsOf(QStringLiteral("GB-01"), QStringLiteral("RUN")).size(), 4);

        // --report: both columns.
        Options report;
        report.report = true;
        report.outputRoot = options.outputRoot;
        report.reportOut = dir + QStringLiteral("/BENCH.md");
        const ToolRun made = runToolWith(report);
        QCOMPARE(static_cast<int>(made.code), static_cast<int>(ExitCode::Ok));
        const QString text = readAll(report.reportOut);
        QVERIFY(text.contains(QStringLiteral("## e2e-bench")));
        QVERIFY(text.contains(QStringLiteral("(n=4)")) && text.contains(QStringLiteral("(n=2)")));
        QVERIFY(text.contains(QStringLiteral("| GB-10 | PollRound G6 x8 |")));
    }

    void HIL_04_theCatalogue3EPlanRunsAgainstVirtualPlcWithoutAToolError() {
        VirtualPlc plc;
        const QString why =
            plc.start({QStringLiteral("--frame"), QStringLiteral("3E"), QStringLiteral("--code"),
                       QStringLiteral("Binary"), QStringLiteral("--port"), QStringLiteral("0")});
        if (!why.isEmpty()) {
            QSKIP(qPrintable(why));
        }
        const QString dir = scratchDir(QStringLiteral("run_catalogue_3e"));
        Options options;
        options.profilePath =
            writeProfile(dir, QStringLiteral("q03ude-eth-3e-bin"), QStringLiteral("cat-3e-bin"),
                         plc.port(), QString(), 400);
        options.planPath = testsDir() + QStringLiteral("/hil/plans/qna_ethernet.json");
        options.outputRoot = dir + QStringLiteral("/captured");
        options.yes = true;
        options.benchReps = 3;
        options.note = QStringLiteral("virtual_plc fixture, not hardware");
        const ToolRun run = runToolWith(options, QStringLiteral("cat-3e-bin\n\n\n\n"));
        QVERIFY2(static_cast<int>(run.code) == static_cast<int>(ExitCode::Ok),
                 qPrintable(run.out + run.err));
        const Counts c = countsOf(run.out);
        QCOMPARE(c.failed, 0);
        QVERIFY(c.passed > 40);
        QVERIFY(QFileInfo::exists(options.outputRoot + QStringLiteral("/cat-3e-bin/steps.vec")));
    }

    void HIL_04_recordsCarryOverrideVerdictAndThePollInputs() {
        VirtualPlc plc;
        const QString why =
            plc.start({QStringLiteral("--frame"), QStringLiteral("3E"), QStringLiteral("--code"),
                       QStringLiteral("Binary"), QStringLiteral("--port"), QStringLiteral("0")});
        if (!why.isEmpty()) {
            QSKIP(qPrintable(why));
        }
        const QString dir = scratchDir(QStringLiteral("run_keys"));
        QFile planFile(dir + QStringLiteral("/plan.json"));
        QVERIFY(planFile.open(QIODevice::WriteOnly));
        planFile.write(R"JSON({"schema":1,"plan":{"id":"keys"},"steps":[
            {"id":"K-01","kind":"read","device":"D@s","frameOverride":{"station":1}},
            {"id":"K-02","kind":"read","device":"D@s","expect":"plcError"},
            {"id":"K-03","kind":"mutate","request":{"kind":"read","device":"D@s"},
                "edits":[{"setByte":{"at":11,"value":"0x03"}}],"readOnly":true,
                "recover":"reconnect","expect":"ok"},
            {"id":"K-04","kind":"read","device":"D@s","expect":{"kind":"ok","values":[999]}},
            {"id":"K-05","kind":"mutate","request":{"kind":"read","device":"D@s"},
                "edits":[{"truncate":1}],"readOnly":true,"recover":"reconnect","expect":"record"},
            {"id":"K-06","kind":"poll","heartbeat":"M@s+200","rounds":4,
             "subscribe":[{"name":"d","device":"D@s","count":4},{"name":"x","device":"X0","count":8,
                 "input":true}],
             "actions":[{"after":1,"write":{"device":"D@s+1","values":[7]}},
                        {"after":2,"subscribe":{"name":"w","device":"W@s","count":2}},
                        {"after":2,"unsubscribe":"d"}]}
        ]})JSON");
        planFile.close();
        Options options;
        options.profilePath = writeProfile(dir, QStringLiteral("q03ude-eth-3e-bin"),
                                           QStringLiteral("e2e-keys"), plc.port(), QString(), 400);
        options.planPath = dir + QStringLiteral("/plan.json");
        options.outputRoot = dir + QStringLiteral("/captured");
        options.yes = true;
        const ToolRun run = runToolWith(options, QStringLiteral("e2e-keys\n"));
        QCOMPARE(static_cast<int>(run.code), static_cast<int>(ExitCode::RunFailures)); // K-04 fails
        const QString folder = options.outputRoot + QStringLiteral("/e2e-keys");
        const std::vector<mc::test::Vector> steps = load(folder + QStringLiteral("/steps.vec"));
        const QString p = QStringLiteral("CAP-e2e-keys-");
        // override: on every record of the overridden step, and only there.
        QCOMPARE(field(steps, p + QStringLiteral("K-01"), "override"), QStringLiteral("station=1"));
        QCOMPARE(field(steps, p + QStringLiteral("K-01-R"), "override"),
                 QStringLiteral("station=1"));
        QCOMPARE(field(steps, p + QStringLiteral("K-02"), "override"), QString());
        // verdict: on the response record, from the runner's judgement.
        QCOMPARE(field(steps, p + QStringLiteral("K-01-R"), "verdict"), QStringLiteral("passed"));
        QCOMPARE(field(steps, p + QStringLiteral("K-02-R"), "verdict"), QStringLiteral("diverged"));
        QCOMPARE(field(steps, p + QStringLiteral("K-03-R"), "verdict"),
                 QStringLiteral("unsupported"));
        QCOMPARE(field(steps, p + QStringLiteral("K-04-R"), "verdict"), QStringLiteral("failed"));
        // ... and on the request record when no response record exists.
        QVERIFY(find(steps, p + QStringLiteral("K-05-R")) == nullptr);
        QCOMPARE(field(steps, p + QStringLiteral("K-05"), "verdict"), QStringLiteral("passed"));

        // The poll inputs, in order, on the same counter as the chunks, before the link is opened.
        const std::vector<mc::test::Vector> session = load(folder + QStringLiteral("/session.vec"));
        QStringList inputs;
        qint64 firstChunkSeq = INT_MAX;
        qint64 lastInitialInput = 0;
        for (const mc::test::Vector& v : session) {
            const qint64 seq = QString::fromStdString(v.field("seq")).toLongLong();
            if (v.field("kind") == "input") {
                const QString name = QString::fromStdString(v.field("input"));
                inputs << name + QLatin1Char(':') +
                              QString::fromStdString(v.field("name").empty() ? v.field("device")
                                                                             : v.field("name"));
                if (inputs.size() <= 3) {
                    lastInitialInput = seq;
                }
                if (name == QLatin1String("subscribe") &&
                    QString::fromStdString(v.field("name")) == QLatin1String("d")) {
                    QCOMPARE(QString::fromStdString(v.field("count")), QStringLiteral("4"));
                    QCOMPARE(v.bytes[0], uint8_t(0x08));
                    QCOMPARE(v.bytes[1], static_cast<uint8_t>(DeviceType::D));
                    QCOMPARE(int(v.bytes[2]) | (int(v.bytes[3]) << 8), 100); // D100, little endian
                    QCOMPARE(int(v.bytes[6]), 4);
                }
                if (name == QLatin1String("write")) {
                    QCOMPARE(v.bytes[0], uint8_t(0x0A));
                    QCOMPARE(QString::fromStdString(v.field("op")), QStringLiteral("WriteWords"));
                    QCOMPARE(QString::fromStdString(v.field("device")), QStringLiteral("D101"));
                    QCOMPARE(int(v.bytes[v.bytes.size() - 2]),
                             7); // the written word, little endian
                }
            } else if (v.field("kind") == "tx" || v.field("kind") == "rx") {
                firstChunkSeq = std::min(firstChunkSeq, seq);
            }
        }
        QCOMPARE(inputs, (QStringList{"heartbeat:M300", "subscribe:d", "subscribe:x", "write:D101",
                                      "subscribe:w", "unsubscribe:d"}));
        QVERIFY2(lastInitialInput < firstChunkSeq,
                 "the initial inputs come before the first byte on the wire");
    }

    void HIL_02_aReadOnlyFragmentNeverMeetsTheFrameThatFollowsIt() {
        // The gate admits a fragment that shows no command only when the frame is declared readOnly
        // and a reconnect follows it. Here the runner does what the plan says, and a server that
        // gives every connection its own PLC (as a real module does) shows where every byte went:
        // the head of a write (the header and the monitoring timer, the command is not there yet)
        // on one connection, the tail that would complete it, a write to D50 outside scratch, on
        // another.
        const Profile q = loadExample(QStringLiteral("q03ude-eth-3e-bin"));
        const McProtocol codec(q.device.frame);
        const ByteBuf one = {0x01, 0x00};
        ByteBuf head = codec
                           .encode(Request::writeWords(Device{DeviceType::D, 100},
                                                       ByteView{one.data(), one.size()}))
                           .value();
        head.resize(11); // the header and the monitoring timer: no command yet
        const QByteArray tail = QByteArray::fromHex("01140000320000a801000700"); // 1401 D50 = 7
        PerConnectionPlc server(q.device.frame);
        QVERIFY(server.listen());
        const QString dir = scratchDir(QStringLiteral("run_fragments"));
        QFile planFile(dir + QStringLiteral("/plan.json"));
        QVERIFY(planFile.open(QIODevice::WriteOnly));
        planFile.write(QStringLiteral(R"JSON({"schema":1,"plan":{"id":"fragments"},"steps":[
            {"id":"X-01","kind":"raw","hex":"%1","readOnly":true,"recover":"reconnect"},
            {"id":"X-02","kind":"raw","hex":"%2","readOnly":true,"recover":"reconnect"},
            {"id":"X-03","kind":"read","device":"D50","expect":{"kind":"ok","values":[0]}}
        ]})JSON")
                           .arg(hexText(head), QString::fromLatin1(tail.toHex(' ').toUpper()))
                           .toUtf8());
        planFile.close();
        Options options;
        options.profilePath =
            writeProfile(dir, QStringLiteral("q03ude-eth-3e-bin"), QStringLiteral("e2e-fragments"),
                         server.port(), QString(), 300);
        options.planPath = dir + QStringLiteral("/plan.json");
        options.outputRoot = dir + QStringLiteral("/captured");
        options.yes = true;
        const ToolRun run = runToolWith(options, QStringLiteral("e2e-fragments\n"));
        QVERIFY2(static_cast<int>(run.code) == static_cast<int>(ExitCode::Ok),
                 qPrintable(run.out + run.err));

        const std::vector<std::unique_ptr<PerConnectionPlc::Connection>>& connections =
            server.connections();
        QVERIFY2(connections.size() >= 3, qPrintable(QString::number(connections.size())));
        int headOn = -1;
        int tailOn = -1;
        for (size_t i = 0; i < connections.size(); ++i) {
            const QByteArray& bytes = connections[i]->bytes;
            if (bytes.startsWith(QByteArray::fromRawData(reinterpret_cast<const char*>(head.data()),
                                                         static_cast<int>(head.size())))) {
                headOn = static_cast<int>(i);
            }
            if (bytes.startsWith(tail)) {
                tailOn = static_cast<int>(i);
            }
            // No connection ever saw a write executed, and D50 is untouched on every one of them.
            for (const MockRequestRecord& r : connections[i]->plc->requests()) {
                QVERIFY2(!(r.count > 0 && (r.op == Op::WriteWords || r.op == Op::WriteBits)),
                         "a write was executed");
            }
            QCOMPARE(connections[i]->plc->word(Device{DeviceType::D, 50}), uint16_t(0));
        }
        QVERIFY2(headOn >= 0 && tailOn >= 0, "both fragments reached the server");
        QVERIFY2(headOn != tailOn, "the fragments arrived on one connection");
    }

    void HIL_04_aFailedConnectionLeavesNoAddressOrPortNameInRunMeta() {
        quint16 closedPort = 0;
        {
            QTcpServer probe;
            QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
            closedPort = probe.serverPort();
        }
        const QString dir = scratchDir(QStringLiteral("run_meta_tcp"));
        QFile planFile(dir + QStringLiteral("/plan.json"));
        QVERIFY(planFile.open(QIODevice::WriteOnly));
        planFile.write(R"JSON({"schema":1,"plan":{"id":"refused"},"steps":[
            {"id":"X-01","kind":"read","device":"D@s"},
            {"id":"X-02","kind":"read","device":"D@s"}
        ]})JSON");
        planFile.close();
        // The profile's free-text fields name the host, the port and a COM port: they are scrubbed
        // too.
        const QString path =
            writeProfile(dir, QStringLiteral("q03ude-eth-3e-bin"), QStringLiteral("e2e-refused"),
                         closedPort, QStringLiteral("COM77"), 400);
        QJsonObject root = readJsonFile(path);
        QJsonObject profile = root.value(QStringLiteral("profile")).toObject();
        const QString where = QStringLiteral("127.0.0.1:%1").arg(closedPort);
        profile.insert(QStringLiteral("adapter"),
                       QStringLiteral("gateway at %1 (or COM77)").arg(where));
        profile.insert(QStringLiteral("module"), QStringLiteral("module on 127.0.0.1"));
        root.insert(QStringLiteral("profile"), profile);
        QVERIFY(writeJsonFile(path, root));
        Options options;
        options.profilePath = path;
        options.planPath = dir + QStringLiteral("/plan.json");
        options.outputRoot = dir + QStringLiteral("/captured");
        options.yes = true;
        options.reconnectSeconds = 1;
        options.note = QStringLiteral("run against %1").arg(where);
        const ToolRun run = runToolWith(options);
        QCOMPARE(static_cast<int>(run.code), static_cast<int>(ExitCode::RunFailures));
        const QString text = readAll(options.outputRoot + QStringLiteral("/e2e-refused/run.meta"));
        const QMap<QString, QString> meta =
            metaOf(options.outputRoot + QStringLiteral("/e2e-refused/run.meta"));
        // The link state is on record in words, not in the transport's text.
        QCOMPARE(meta.value(QStringLiteral("notsent.X-01")),
                 QStringLiteral("link down: OpenFailed (Disconnected)"));
        QVERIFY2(meta.value(QStringLiteral("recovery.1"))
                     .contains(QStringLiteral("NOT reconnected: OpenFailed (Disconnected)")),
                 qPrintable(text));
        QCOMPARE(meta.value(QStringLiteral("adapter")),
                 QStringLiteral("gateway at <redacted> (or <redacted>)"));
        QCOMPARE(meta.value(QStringLiteral("module")), QStringLiteral("module on <redacted>"));
        QCOMPARE(meta.value(QStringLiteral("operator_note")),
                 QStringLiteral("run against <redacted>"));
        QVERIFY(!text.contains(QStringLiteral("127.0.0.1")));
        QVERIFY(!text.contains(QStringLiteral(":%1").arg(closedPort)));
        QVERIFY(!text.contains(QStringLiteral("COM77"), Qt::CaseInsensitive));
        // The console still says what the transport said.
        QVERIFY(run.out.contains(QStringLiteral("could not connect")));
    }

    void HIL_04_aPortThatCannotBeOpenedLeavesNoComNameInRunMeta() {
        // COM77 does not exist: the transport's own message names the port ("COM77: The system
        // cannot find the file specified"). It must not reach run.meta.
        const QString dir = scratchDir(QStringLiteral("run_meta_com"));
        QFile planFile(dir + QStringLiteral("/plan.json"));
        QVERIFY(planFile.open(QIODevice::WriteOnly));
        planFile.write(R"JSON({"schema":1,"plan":{"id":"nocom"},"steps":[
            {"id":"X-01","kind":"read","device":"D@s"},
            {"id":"X-02","kind":"read","device":"D@s"}
        ]})JSON");
        planFile.close();
        Options options;
        options.profilePath =
            writeProfile(dir, QStringLiteral("q03ude-c24-3c-f4"), QStringLiteral("e2e-nocom"), 0,
                         QStringLiteral("COM77"), 400);
        options.planPath = dir + QStringLiteral("/plan.json");
        options.outputRoot = dir + QStringLiteral("/captured");
        options.yes = true;
        options.reconnectSeconds = 1;
        const ToolRun run = runToolWith(options);
        QCOMPARE(static_cast<int>(run.code), static_cast<int>(ExitCode::RunFailures));
        const QString file = options.outputRoot + QStringLiteral("/e2e-nocom/run.meta");
        const QString text = readAll(file);
        QVERIFY2(!text.isEmpty(), qPrintable(run.out + run.err));
        QVERIFY2(!text.contains(QStringLiteral("COM77"), Qt::CaseInsensitive), qPrintable(text));
        const QMap<QString, QString> meta = metaOf(file);
        QCOMPARE(meta.value(QStringLiteral("notsent.X-01")),
                 QStringLiteral("link down: OpenFailed (Disconnected)"));
        QVERIFY(meta.value(QStringLiteral("recovery.1"))
                    .contains(QStringLiteral("OpenFailed (Disconnected)")));
        // The transport's text is for the operator at the console.
        QVERIFY2(run.out.contains(QStringLiteral("COM77")), qPrintable(run.out));
    }

    void HIL_04_aPromptLongerThanThePollBudgetDoesNotFailTheStep() {
        VirtualPlc plc;
        const QString why =
            plc.start({QStringLiteral("--frame"), QStringLiteral("3E"), QStringLiteral("--code"),
                       QStringLiteral("Binary"), QStringLiteral("--port"), QStringLiteral("0")});
        if (!why.isEmpty()) {
            QSKIP(qPrintable(why));
        }
        const QString dir = scratchDir(QStringLiteral("run_prompt_budget"));
        QFile planFile(dir + QStringLiteral("/plan.json"));
        QVERIFY(planFile.open(QIODevice::WriteOnly));
        planFile.write(R"JSON({"schema":1,"plan":{"id":"budget"},"steps":[
            {"id":"P-01","kind":"poll","rounds":4,"subscribe":[{"name":"d","device":"D@s",
                "count":2}],
             "actions":[{"after":1,"prompt":"Pull the cable"},{"after":2,"write":{"device":"D@s+1",
                 "values":[7]}}]}
        ]})JSON");
        planFile.close();
        const ProfileLoad profile =
            loadProfileFile(writeProfile(dir, QStringLiteral("q03ude-eth-3e-bin"),
                                         QStringLiteral("e2e-budget"), plc.port(), QString(), 400));
        const PlanLoad plan = loadPlanFile(dir + QStringLiteral("/plan.json"));
        QVERIFY(profile.ok() && plan.ok());
        const ResolveResult resolved = resolvePlan(*plan.plan, *profile.profile);
        QVERIFY(resolved.ok());
        // The budget of the poll is 4 x (20 + 400) ms + 200 ms; the operator takes 2.5 s to answer.
        RunnerSettings settings;
        settings.outputRoot = dir + QStringLiteral("/captured");
        settings.pollSlackMs = 200;
        GatedInput input;
        QTimer answer;
        answer.setSingleShot(true);
        QObject::connect(&answer, &QTimer::timeout, &answer, [&]() { input.answer(); });
        answer.start(2500);
        QString outText;
        QString errText;
        QTextStream out(&outText);
        QTextStream err(&errText);
        QTextStream in(&input);
        Runner runner(*profile.profile, resolved, settings, ToolIo{&out, &err, &in});
        QElapsedTimer timer;
        timer.start();
        const RunSummary summary = runner.run();
        QVERIFY2(timer.elapsed() >= 2400, qPrintable(QString::number(timer.elapsed())));
        QVERIFY2(summary.failed == 0 && summary.passed == 1,
                 qPrintable(summary.lines.join(QLatin1Char('\n'))));
        // The rounds went on while the prompt was open, and the write that was due after round 2
        // was made once the prompt was answered (it is on record as an input).
        const std::vector<mc::test::Vector> session =
            load(settings.outputRoot + QStringLiteral("/e2e-budget/session.vec"));
        bool wrote = false;
        for (const mc::test::Vector& v : session) {
            wrote = wrote || (v.field("kind") == "input" && v.field("input") == "write");
        }
        QVERIFY(wrote);
    }

    void HIL_04_aPromptDoesNotUseUpThePollBudgetWhileTheLinkIsDown() {
        // The cable-pull case: the operator is asked to pull the cable, the answers stop, the
        // rounds fault, and the operator types Enter well after the poll's own budget would have
        // run out. The time the prompt was open is not the poll's time: once the cable is back the
        // rounds finish.
        const Profile q = loadExample(QStringLiteral("q03ude-eth-3e-bin"));
        PerConnectionPlc server(q.device.frame);
        server.setDelayMs(150);
        QVERIFY(server.listen());
        const QString dir = scratchDir(QStringLiteral("run_prompt_stall"));
        QFile planFile(dir + QStringLiteral("/plan.json"));
        QVERIFY(planFile.open(QIODevice::WriteOnly));
        planFile.write(R"JSON({"schema":1,"plan":{"id":"stall"},"steps":[
            {"id":"P-01","kind":"poll","rounds":3,
             "subscribe":[{"name":"d","device":"D@s","count":2}],
             "actions":[{"after":1,"prompt":"Pull the cable"}]}
        ]})JSON");
        planFile.close();
        const ProfileLoad profile = loadProfileFile(
            writeProfile(dir, QStringLiteral("q03ude-eth-3e-bin"), QStringLiteral("e2e-stall"),
                         server.port(), QString(), 400));
        const PlanLoad plan = loadPlanFile(dir + QStringLiteral("/plan.json"));
        QVERIFY(profile.ok() && plan.ok());
        const ResolveResult resolved = resolvePlan(*plan.plan, *profile.profile);
        QVERIFY(resolved.ok());
        // The budget is 3 x (20 + 400) ms = 1260 ms; the prompt stays open for about 1.8 s.
        RunnerSettings settings;
        settings.outputRoot = dir + QStringLiteral("/captured");
        settings.pollSlackMs = 0;
        GatedInput input;
        input.setOnRead(
            [&server]() { server.setSilent(true); }); // the prompt is open: the cable is out
        QTimer heal;
        heal.setSingleShot(true);
        QObject::connect(&heal, &QTimer::timeout, &heal, [&server]() { server.setSilent(false); });
        heal.start(1500);
        QTimer answer;
        answer.setSingleShot(true);
        QObject::connect(&answer, &QTimer::timeout, &answer, [&]() { input.answer(); });
        answer.start(2000);
        QString outText;
        QString errText;
        QTextStream out(&outText);
        QTextStream err(&errText);
        QTextStream in(&input);
        Runner runner(*profile.profile, resolved, settings, ToolIo{&out, &err, &in});
        const RunSummary summary = runner.run();
        QVERIFY2(summary.failed == 0 && summary.passed == 1,
                 qPrintable(summary.lines.join(QLatin1Char('\n'))));
    }

    // specialFrom in a run: the read in the special range passes with expectation ok and the
    // summary line says why; the read spanning both ranges is reported skipped with its reason and
    // nothing of it goes on the wire. (A 3E profile served by MockPlc, which has no device limit
    // there, stands in for the FX3.)
    void HIL_04_theSummaryShowsTheSpecialFromExpectationAndSkip() {
        QJsonObject root = readJsonFile(exampleProfilePath(QStringLiteral("q03ude-eth-3e-bin")));
        const Profile q = *loadProfile(root).profile;
        PerConnectionPlc server(q.device.frame);
        QVERIFY(server.listen());
        const QString dir = scratchDir(QStringLiteral("run_special_from"));
        const QString path =
            writeProfile(dir, QStringLiteral("q03ude-eth-3e-bin"), QStringLiteral("e2e-special"),
                         server.port(), QString(), 400);
        root = readJsonFile(path);
        QJsonObject profileObj = root.value(QStringLiteral("profile")).toObject();
        profileObj.insert(QStringLiteral("specialFrom"), QJsonObject{{"D", 12288}});
        root.insert(QStringLiteral("profile"), profileObj);
        const ProfileLoad profile = loadProfile(root);
        QVERIFY2(profile.ok(), qPrintable(profile.error.text()));
        const PlanLoad plan = loadPlan(QJsonDocument::fromJson(R"JSON({"schema":1,
            "plan":{"id":"special"},"steps":[
            {"id":"G5-02","kind":"read","device":"D@end+1","expect":"plcError"},
            {"id":"G5-03","kind":"read","device":"D@end","count":2,"expect":"plcError"}
        ]})JSON")
                                           .object());
        QVERIFY2(plan.ok(), qPrintable(plan.error.text()));
        const ResolveResult resolved = resolvePlan(*plan.plan, *profile.profile);
        QVERIFY(resolved.ok());
        RunnerSettings settings;
        settings.outputRoot = dir + QStringLiteral("/captured");
        QString outText;
        QString errText;
        QString inText;
        QTextStream out(&outText);
        QTextStream err(&errText);
        QTextStream in(&inText);
        Runner runner(*profile.profile, resolved, settings, ToolIo{&out, &err, &in});
        const RunSummary summary = runner.run();
        const QString lines = summary.lines.join(QLatin1Char('\n'));
        QVERIFY2(summary.passed == 1 && summary.skipped == 1 && summary.failed == 0,
                 qPrintable(lines));
        QVERIFY2(lines.contains(QStringLiteral("ReadWords D12288 x1 (G5-02: expect ok, not "
                                               "plcError: D12288 is in the special range from "
                                               "D12288 (specialFrom))")),
                 qPrintable(lines));
        QVERIFY2(lines.contains(QStringLiteral("skipped: D12287-D12288 spans the general and the "
                                               "special range (specialFrom D12288)")),
                 qPrintable(lines));
        // One request reached the PLC: G5-02's. G5-03 was not sent.
        QCOMPARE(static_cast<int>(server.connections().size()), 1);
        McProtocol codec(q.device.frame);
        const Expected<ByteBuf> g502 =
            codec.encode(Request::readWords(Device{DeviceType::D, 12288}, 1));
        QVERIFY(g502);
        QCOMPARE(server.connections()[0]->bytes,
                 QByteArray(reinterpret_cast<const char*>(g502.value().data()),
                            static_cast<int>(g502.value().size())));
    }

    void HIL_04_theOnlyOptionRunsTheNamedGroupsOnly() {
        VirtualPlc plc;
        const QString why =
            plc.start({QStringLiteral("--frame"), QStringLiteral("3E"), QStringLiteral("--code"),
                       QStringLiteral("Binary"), QStringLiteral("--port"), QStringLiteral("0"),
                       QStringLiteral("--set"), QStringLiteral("D100=1234")});
        if (!why.isEmpty()) {
            QSKIP(qPrintable(why));
        }
        const QString dir = scratchDir(QStringLiteral("run_only"));
        QFile planFile(dir + QStringLiteral("/plan.json"));
        QVERIFY(planFile.open(QIODevice::WriteOnly));
        planFile.write(R"JSON({"schema":1,"plan":{"id":"only"},"steps":[
            {"id":"G1-01","kind":"read","device":"D@s","expect":{"kind":"ok","values":[1234]}},
            {"id":"G2-01","kind":"read","device":"D@s","expect":{"kind":"ok","values":[5]}}
        ]})JSON");
        planFile.close();
        Options options;
        options.profilePath = writeProfile(dir, QStringLiteral("q03ude-eth-3e-bin"),
                                           QStringLiteral("e2e-only"), plc.port(), QString(), 400);
        options.planPath = dir + QStringLiteral("/plan.json");
        options.outputRoot = dir + QStringLiteral("/captured");
        options.yes = true;
        options.only = QStringList{QStringLiteral("G1")};
        const ToolRun run = runToolWith(options);
        QVERIFY2(static_cast<int>(run.code) == static_cast<int>(ExitCode::Ok),
                 qPrintable(run.out + run.err));
        QCOMPARE(countsOf(run.out).passed, 1);
        QVERIFY(!run.out.contains(QStringLiteral("G2-01")));
    }

    // The program itself (tools/hil_capture/main.cpp), not runTool(): its console streams are
    // UTF-8 with Qt 5 and Qt 6 alike, so a non-ASCII path in an error message keeps every
    // character (Qt 5's default codec, the ANSI code page, turned it into '?').
    void HIL_01_theProgramWritesNonAsciiConsoleTextAsUtf8() {
        const QString program = QStringLiteral(MC_HIL_CAPTURE_PATH);
        if (program.isEmpty() || !QFileInfo::exists(program)) {
            QSKIP(qPrintable(
                QStringLiteral("tools/hil_capture is not built here (%1)").arg(program)));
        }
        // U+03A9 U+00FC U+1EC7, escaped so the literal does not depend on the source encoding.
        const QByteArray name("missing-\xCE\xA9\xC3\xBC\xE1\xBB\x87.json");
        const QString profile =
            QStringLiteral(MC_HIL_OUTPUT_DIR) + QLatin1Char('/') + QString::fromUtf8(name);
        QVERIFY(!QFileInfo::exists(profile));

        QProcess p;
        p.start(program, {QStringLiteral("--profile"), profile, QStringLiteral("--plan"),
                          QStringLiteral(MC_TESTS_SOURCE_DIR "/hil/e2e/plan_3e.json"),
                          QStringLiteral("--dry-run")});
        QVERIFY(p.waitForFinished(30000));
        QCOMPARE(p.exitStatus(), QProcess::NormalExit);
        QCOMPARE(p.exitCode(), static_cast<int>(ExitCode::BadInput));
        const QByteArray err = p.readAllStandardError();
        QVERIFY2(err.contains(name), err.toHex(' ').constData());
    }

    // ---- HIL-07: settle after every (re)connect (F-012) ---------------------------------------

    void HIL_07_aSettledReconnectSurvivesAModuleThatClosesAFreshLink() {
        const Watchdog dog(60, "the settled reconnect run");
        const Profile q = loadExample(QStringLiteral("q03ude-eth-3e-bin"));
        PerConnectionPlc server(q.device.frame);
        QVERIFY(server.listen());
        // Settle 300 ms: longer than the 25 ms the fake keeps a link it dislikes. The reconnect made
        // right after the close is dropped during the settle, counts as a failed attempt and is
        // repeated after the pause, when the fake has stopped refusing.
        const PlanRun r = runSettlePlan(QStringLiteral("e2e-settle"), 300, server);
        const ToolRun& run = r.run;
        QVERIFY2(static_cast<int>(run.code) == static_cast<int>(ExitCode::Ok),
                 qPrintable(run.out + run.err));
        const Counts c = countsOf(run.out);
        QVERIFY2(c.passed == 2 && c.failed == 0, qPrintable(run.out));
        const std::vector<mc::test::Vector> steps = load(r.folder + QStringLiteral("/steps.vec"));
        const QString p = QStringLiteral("CAP-e2e-settle-");
        // The `then` read after the close is answered, and so is the step after it.
        QCOMPARE(field(steps, p + QStringLiteral("H-01.2-R"), "outcome"), QStringLiteral("ok"));
        QCOMPARE(field(steps, p + QStringLiteral("H-02-R"), "outcome"), QStringLiteral("ok"));
        // Only H-01 itself (the request the fake dropped) is lost; no request after it died on a link
        // the tool had just opened.
        for (const mc::test::Vector& v : steps) {
            if (v.id != toStd(p + QStringLiteral("H-01"))) {
                QVERIFY2(QString::fromStdString(v.field("outcome")).contains(QStringLiteral("LinkDown")) ==
                             false,
                         v.id.c_str());
            }
        }
        // The recoveries are on record, with the settle retries.
        const QMap<QString, QString> meta = metaOf(r.folder + QStringLiteral("/run.meta"));
        QVERIFY2(meta.value(QStringLiteral("recovery.1")).contains(QStringLiteral("after H-01")),
                 qPrintable(readAll(r.folder + QStringLiteral("/run.meta"))));
        bool settleRecorded = false;
        for (auto it = meta.constBegin(); it != meta.constEnd(); ++it) {
            settleRecorded = settleRecorded ||
                             (it.key().startsWith(QStringLiteral("recovery.")) &&
                              it.value().contains(QStringLiteral("settle"), Qt::CaseInsensitive));
        }
        QVERIFY2(settleRecorded, qPrintable(readAll(r.folder + QStringLiteral("/run.meta"))));
        // At least the first link, the one dropped during the settle and the one that stayed.
        QVERIFY2(server.connections().size() >= 3,
                 qPrintable(QString::number(server.connections().size())));
        QVERIFY(!server.connections().back()->bytes.isEmpty());
    }

    // Format 1 of the C24 sends the EOT alone (04) and answers it with a NAK frame without CR LF
    // (the NAK of CAP-q03ude-c24-3c-f1-sum-G7-01+2-R, error 7F24).
    void HIL_08_aFormat1NakAnsweringTheEotIsRecordedAsThePlcError() {
        const Watchdog dog(60, "the format 1 EOT run");
        QJsonObject proot = readJsonFile(exampleProfilePath(QStringLiteral("q03ude-c24-3c-f4")));
        const auto withFormat1 = [](QJsonObject root) {
            QJsonObject device = root.value(QStringLiteral("device")).toObject();
            QJsonObject frame = device.value(QStringLiteral("frame")).toObject();
            frame.insert(QStringLiteral("format"), QStringLiteral("Format1"));
            device.insert(QStringLiteral("frame"), frame);
            root.insert(QStringLiteral("device"), device);
            return root;
        };
        const ProfileLoad format1 = loadProfile(withFormat1(proot));
        QVERIFY(format1.ok());
        PerConnectionPlc server(format1.profile->device.frame);
        const QByteArray nak = QByteArray::fromHex("154639303030304646303037463234");
        QCOMPARE(nak.size(), 15);
        server.setEotReply(nak);
        QVERIFY(server.listen());
        const QString name = QStringLiteral("e2e-eotf1");
        const QString dir = scratchDir(name);
        QFile planFile(dir + QStringLiteral("/plan.json"));
        QVERIFY(planFile.open(QIODevice::WriteOnly));
        planFile.write(R"JSON({"schema":1,"plan":{"id":"eot"},"steps":[
            {"id":"N-01","kind":"mutate","request":{"kind":"read","device":"D@s"},
             "edits":[{"truncate":1}],"readOnly":true,"recover":"eot","expect":"record",
             "then":[{"kind":"read","device":"D@s","expect":"ok"}]}
        ]})JSON");
        planFile.close();
        const QString path = writeProfile3cOverTcp(dir, name, server.port(), 400);
        writeJsonFile(path, withFormat1(readJsonFile(path)));
        Options options;
        options.profilePath = path;
        options.planPath = dir + QStringLiteral("/plan.json");
        options.outputRoot = dir + QStringLiteral("/captured");
        options.yes = true;
        const ToolRun run = runToolWith(options, name + QLatin1Char('\n'));
        QVERIFY2(static_cast<int>(run.code) == static_cast<int>(ExitCode::Ok),
                 qPrintable(run.out + run.err));
        const std::vector<mc::test::Vector> steps =
            load(options.outputRoot + QLatin1Char('/') + name + QStringLiteral("/steps.vec"));
        const QString p = QStringLiteral("CAP-e2e-eotf1-");
        const mc::test::Vector* eot = find(steps, p + QStringLiteral("N-01+2"));
        QVERIFY(eot != nullptr);
        QCOMPARE(eot->bytes.size(), size_t(1)); // the EOT alone, no CR LF
        const QString outcome = field(steps, p + QStringLiteral("N-01+2-R"), "outcome");
        QVERIFY2(outcome.startsWith(QStringLiteral("plcError 7F24")), qPrintable(outcome));
        QCOMPARE(field(steps, p + QStringLiteral("N-01+2-R"), "verdict"), QStringLiteral("passed"));
        QCOMPARE(field(steps, p + QStringLiteral("N-01.2-R"), "outcome"), QStringLiteral("ok"));
    }

    // Added by the GREEN-verify pass: a link the tool closes itself (a frame override changes the
    // configuration, the runner disconnects and connects again) is not a recovery: run.meta keeps
    // no `recovery.N` line for it, with the default settle and with a settle.
    void HIL_07_aDeliberateDisconnectIsNotRecordedAsARecovery() {
        const Watchdog dog(60, "the deliberate disconnect run");
        const Profile q = loadExample(QStringLiteral("q03ude-eth-3e-bin"));
        for (const int settleMs : {0, 50}) {
            PerConnectionPlc server(q.device.frame);
            QVERIFY(server.listen());
            const QString name = QStringLiteral("e2e-deliberate%1").arg(settleMs);
            const QString dir = scratchDir(name);
            QFile planFile(dir + QStringLiteral("/plan.json"));
            QVERIFY(planFile.open(QIODevice::WriteOnly));
            planFile.write(R"JSON({"schema":1,"plan":{"id":"deliberate"},"steps":[
                {"id":"D-01","kind":"read","device":"D@s","expect":"record"},
                {"id":"D-02","kind":"read","device":"D@s","frameOverride":{"station":1},
                 "expect":"record"},
                {"id":"D-03","kind":"read","device":"D@s","expect":"record"}
            ]})JSON");
            planFile.close();
            Options options;
            options.profilePath = writeProfile(dir, QStringLiteral("q03ude-eth-3e-bin"), name,
                                               server.port(), QString(), 400);
            options.planPath = dir + QStringLiteral("/plan.json");
            options.outputRoot = dir + QStringLiteral("/captured");
            options.yes = true;
            options.settleMs = settleMs;
            const ToolRun run = runToolWith(options);
            QVERIFY2(static_cast<int>(run.code) == static_cast<int>(ExitCode::Ok),
                     qPrintable(run.out + run.err));
            const QString folder = options.outputRoot + QLatin1Char('/') + name;
            // The override really made the runner connect again.
            QVERIFY2(server.connections().size() >= 3,
                     qPrintable(QString::number(server.connections().size())));
            const QMap<QString, QString> meta = metaOf(folder + QStringLiteral("/run.meta"));
            for (auto it = meta.constBegin(); it != meta.constEnd(); ++it) {
                QVERIFY2(!it.key().startsWith(QStringLiteral("recovery.")),
                         qPrintable(it.key() + QLatin1Char('=') + it.value()));
            }
        }
    }

    // Pins F-012: without a settle the same plan sends the `then` read on the link the module is
    // about to drop. (This one may already pass before the change; it must keep passing.)
    void HIL_07_withoutASettleTheNextRequestDiesAsLinkDown() {
        const Watchdog dog(60, "the unsettled reconnect run");
        const Profile q = loadExample(QStringLiteral("q03ude-eth-3e-bin"));
        PerConnectionPlc server(q.device.frame);
        QVERIFY(server.listen());
        const PlanRun r = runSettlePlan(QStringLiteral("e2e-nosettle"), 0, server);
        const ToolRun& run = r.run;
        QCOMPARE(static_cast<int>(run.code), static_cast<int>(ExitCode::RunFailures));
        const std::vector<mc::test::Vector> steps = load(r.folder + QStringLiteral("/steps.vec"));
        const QString p = QStringLiteral("CAP-e2e-nosettle-");
        // No answer came back, so the outcome is on the request record.
        QVERIFY(find(steps, p + QStringLiteral("H-01.2-R")) == nullptr);
        QCOMPARE(field(steps, p + QStringLiteral("H-01.2"), "outcome"),
                 QStringLiteral("transportError LinkDown"));
    }

    void HIL_07_aLinkThatNeverStaysUpAbortsTheRunAfterOneBudget() {
        const Watchdog dog(60, "the never-up run");
        const Profile q = loadExample(QStringLiteral("q03ude-eth-3e-bin"));
        PerConnectionPlc server(q.device.frame);
        server.closeEveryConnection(20);
        QVERIFY(server.listen());
        const QString dir = scratchDir(QStringLiteral("e2e-neverup"));
        QFile planFile(dir + QStringLiteral("/plan.json"));
        QVERIFY(planFile.open(QIODevice::WriteOnly));
        planFile.write(R"JSON({"schema":1,"plan":{"id":"neverup"},"steps":[
            {"id":"X-01","kind":"read","device":"D@s"},
            {"id":"X-02","kind":"read","device":"D@s"},
            {"id":"X-03","kind":"read","device":"D@s"}
        ]})JSON");
        planFile.close();
        Options options;
        options.profilePath = writeProfile(dir, QStringLiteral("q03ude-eth-3e-bin"),
                                           QStringLiteral("e2e-neverup"), server.port(), QString(),
                                           400);
        options.planPath = dir + QStringLiteral("/plan.json");
        options.outputRoot = dir + QStringLiteral("/captured");
        options.yes = true;
        options.settleMs = 200;
        options.reconnectSeconds = 2;
        QElapsedTimer timer;
        timer.start();
        const ToolRun run = runToolWith(options);
        // One budget (2 s) is spent on the first step; the others are not even tried.
        QVERIFY2(timer.elapsed() >= 1500 && timer.elapsed() < 8000,
                 qPrintable(QString::number(timer.elapsed())));
        QCOMPARE(static_cast<int>(run.code), static_cast<int>(ExitCode::RunFailures));
        QCOMPARE(countsOf(run.out).failed, 3);
        QVERIFY2(run.out.contains(QStringLiteral("aborted, the link could not be restored")),
                 qPrintable(run.out));
        // The tool kept trying inside the budget: a link that closes during the settle is retried.
        QVERIFY2(server.connections().size() >= 2,
                 qPrintable(QString::number(server.connections().size())));
        QVERIFY(QFileInfo::exists(options.outputRoot + QStringLiteral("/e2e-neverup/run.meta")));
    }

    // The default is resolved from the profile: 0 on loopback and on a serial line, 500 ms for TCP to
    // another host; an explicit value (0..10000) wins. The function is the one declaration the
    // runner header must add: `int settleMsFor(const Profile& profile, int settleMs)`.
    void HIL_07_theSettleDefaultFollowsTheTransportAndAnExplicitValueWins() {
        QJsonObject root = readJsonFile(exampleProfilePath(QStringLiteral("q03ude-eth-3e-bin")));
        const auto withHost = [&](const QString& host) {
            QJsonObject r = root;
            QJsonObject device = r.value(QStringLiteral("device")).toObject();
            QJsonObject transport = device.value(QStringLiteral("transport")).toObject();
            QJsonObject tcp = transport.value(QStringLiteral("tcp")).toObject();
            tcp.insert(QStringLiteral("host"), host);
            transport.insert(QStringLiteral("tcp"), tcp);
            device.insert(QStringLiteral("transport"), transport);
            r.insert(QStringLiteral("device"), device);
            const ProfileLoad load = loadProfile(r);
            return load.ok() ? *load.profile : Profile();
        };
        const Profile remote = withHost(QStringLiteral("192.0.2.10"));
        QCOMPARE(static_cast<int>(remote.device.transport), static_cast<int>(TransportKind::Tcp));
        QCOMPARE(settleMsFor(remote, -1), 500);
        QCOMPARE(settleMsFor(withHost(QStringLiteral("127.0.0.1")), -1), 0);
        QCOMPARE(settleMsFor(withHost(QStringLiteral("localhost")), -1), 0);
        QCOMPARE(settleMsFor(withHost(QStringLiteral("::1")), -1), 0);
        // A serial line: 0.
        const Profile serial = loadExample(QStringLiteral("q03ude-c24-3c-f4"));
        QCOMPARE(static_cast<int>(serial.device.transport),
                 static_cast<int>(TransportKind::Serial));
        QCOMPARE(settleMsFor(serial, -1), 0);
        // An explicit value is used as given, whatever the transport.
        QCOMPARE(settleMsFor(remote, 0), 0);
        QCOMPARE(settleMsFor(remote, 1234), 1234);
        QCOMPARE(settleMsFor(withHost(QStringLiteral("127.0.0.1")), 800), 800);
        QCOMPARE(settleMsFor(serial, 10000), 10000);
        // The settings struct defaults to "auto" and the options struct too.
        QCOMPARE(RunnerSettings().settleAfterConnectMs, -1);
        QCOMPARE(Options().settleMs, -1);
    }

    // ---- HIL-08: the answer to the EOT of a `recover: eot` step is on record (F-017) -----------

    void HIL_08_aNakAnsweringTheEotIsRecordedAsThePlcErrorAndTheVerdictIsUnchanged() {
        const Watchdog dog(60, "the EOT NAK run");
        // The C24 answer of the capture (CAP-q03ude-c24-3c-f4-G7-01+2-R): NAK, F9, 0000, FF, 00,
        // 7F23, CR LF.
        const QByteArray nak = QByteArray::fromHex("1546393030303046463030374632330D0A");
        QCOMPARE(nak.size(), 17);
        const PlanRun r = runEotPlan(QStringLiteral("e2e-eotnak"), nak);
        const ToolRun& run = r.run;
        QVERIFY2(static_cast<int>(run.code) == static_cast<int>(ExitCode::Ok),
                 qPrintable(run.out + run.err));
        const Counts c = countsOf(run.out);
        QVERIFY2(c.passed == 1 && c.failed == 0 && c.diverged == 0, qPrintable(run.out));
        const std::vector<mc::test::Vector> steps = load(r.folder + QStringLiteral("/steps.vec"));
        const QString p = QStringLiteral("CAP-e2e-eotnak-");
        // The truncated request got no answer; the EOT exchange got the NAK.
        QCOMPARE(field(steps, p + QStringLiteral("N-01"), "outcome"), QStringLiteral("timeout"));
        const mc::test::Vector* eotResponse = find(steps, p + QStringLiteral("N-01+2-R"));
        QVERIFY2(eotResponse != nullptr, "no response record for the EOT exchange");
        QVERIFY2(field(steps, p + QStringLiteral("N-01+2-R"), "outcome")
                     .startsWith(QStringLiteral("plcError 7F23")),
                 qPrintable(field(steps, p + QStringLiteral("N-01+2-R"), "outcome")));
        QCOMPARE(eotResponse->bytes.size(), size_t(17));
        // expect: record -> the verdict is passed whatever the EOT answered.
        QCOMPARE(field(steps, p + QStringLiteral("N-01+2-R"), "verdict"), QStringLiteral("passed"));
        // The line is usable again: the `then` read is answered.
        QCOMPARE(field(steps, p + QStringLiteral("N-01.2-R"), "outcome"), QStringLiteral("ok"));
    }

    // Silence after the EOT stays `noResponse` (this one may already pass before the change).
    void HIL_08_noAnswerToTheEotIsRecordedAsNoResponse() {
        const Watchdog dog(60, "the EOT silence run");
        const PlanRun r = runEotPlan(QStringLiteral("e2e-eotsilent"), QByteArray());
        const ToolRun& run = r.run;
        QVERIFY2(static_cast<int>(run.code) == static_cast<int>(ExitCode::Ok),
                 qPrintable(run.out + run.err));
        QVERIFY2(countsOf(run.out).passed == 1 && countsOf(run.out).failed == 0,
                 qPrintable(run.out));
        const std::vector<mc::test::Vector> steps = load(r.folder + QStringLiteral("/steps.vec"));
        const QString p = QStringLiteral("CAP-e2e-eotsilent-");
        QCOMPARE(field(steps, p + QStringLiteral("N-01+2"), "outcome"),
                 QStringLiteral("noResponse"));
        QVERIFY(find(steps, p + QStringLiteral("N-01+2-R")) == nullptr);
        QCOMPARE(field(steps, p + QStringLiteral("N-01.2-R"), "outcome"), QStringLiteral("ok"));
    }

    // The EOT the library sends on its own after a bad answer in an api operation is not the EOT of
    // `recover: eot`: its record keeps the blanket `ok` of the recorder (pins the unchanged half).
    void HIL_08_anEotSentByTheLibraryInAnApiOperationKeepsItsOkRecord() {
        const Watchdog dog(60, "the api EOT run");
        const Profile q = loadExample(QStringLiteral("q03ude-c24-3c-f4"));
        const QByteArray nak = QByteArray::fromHex("1546393030303046463030374632330D0A");
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        std::vector<std::unique_ptr<MockPlc>> plcs;
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&]() {
            while (QTcpSocket* socket = server.nextPendingConnection()) {
                plcs.push_back(std::make_unique<MockPlc>(q.device.frame));
                MockPlc* plc = plcs.back().get();
                auto requests = std::make_shared<int>(0);
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [=]() {
                    const QByteArray in = socket->readAll();
                    if (in == QByteArray::fromHex("04") || in == QByteArray::fromHex("040D0A")) {
                        socket->write(nak);
                        return;
                    }
                    if (++*requests == 1) {
                        socket->write(QByteArray("ZZ\r\n")); // not a frame: the library sends an EOT
                        return;
                    }
                    plc->bytesIn(ByteView{reinterpret_cast<const uint8_t*>(in.constData()),
                                          static_cast<size_t>(in.size())});
                    ByteView out;
                    while (plc->nextResponse(out)) {
                        socket->write(QByteArray(reinterpret_cast<const char*>(out.data),
                                                 static_cast<QByteArray::size_type>(out.size)));
                    }
                });
            }
        });
        const QString name = QStringLiteral("e2e-apieot");
        const QString dir = scratchDir(name);
        QFile planFile(dir + QStringLiteral("/plan.json"));
        QVERIFY(planFile.open(QIODevice::WriteOnly));
        planFile.write(R"JSON({"schema":1,"plan":{"id":"apieot"},"steps":[
            {"id":"A-01","kind":"read","device":"D@s","expect":"record"}
        ]})JSON");
        planFile.close();
        Options options;
        options.profilePath = writeProfile3cOverTcp(dir, name, server.serverPort(), 400);
        options.planPath = dir + QStringLiteral("/plan.json");
        options.outputRoot = dir + QStringLiteral("/captured");
        options.yes = true;
        const ToolRun run = runToolWith(options, name + QLatin1Char('\n'));
        QVERIFY2(static_cast<int>(run.code) == static_cast<int>(ExitCode::Ok),
                 qPrintable(run.out + run.err));
        const std::vector<mc::test::Vector> steps =
            load(options.outputRoot + QLatin1Char('/') + name + QStringLiteral("/steps.vec"));
        // Every request record that is a lone EOT: its response record says `ok`, not the NAK's
        // PLC error.
        int eotAnswers = 0;
        for (const mc::test::Vector& v : steps) {
            if (v.bytes == std::vector<uint8_t>{0x04} ||
                v.bytes == std::vector<uint8_t>{0x04, 0x0D, 0x0A}) {
                const QString response = QString::fromStdString(v.id) + QStringLiteral("-R");
                const mc::test::Vector* answer = find(steps, response);
                if (answer != nullptr) {
                    ++eotAnswers;
                    QCOMPARE(QString::fromStdString(answer->field("outcome")), QStringLiteral("ok"));
                }
            }
        }
        QVERIFY2(eotAnswers >= 1, qPrintable(run.out));
    }

    // The recovery text carries the settle in force and how many links closed during it.
    void HIL_07_theRecoveryRecordsTheNumberOfSettleRetries() {
        const Watchdog dog(60, "the settle retries run");
        const Profile q = loadExample(QStringLiteral("q03ude-eth-3e-bin"));
        PerConnectionPlc server(q.device.frame);
        QVERIFY(server.listen());
        const PlanRun r = runSettlePlan(QStringLiteral("e2e-settleretries"), 300, server);
        QVERIFY2(static_cast<int>(r.run.code) == static_cast<int>(ExitCode::Ok),
                 qPrintable(r.run.out + r.run.err));
        const QMap<QString, QString> meta = metaOf(r.folder + QStringLiteral("/run.meta"));
        const QString recovery = meta.value(QStringLiteral("recovery.1"));
        QVERIFY2(recovery.contains(QStringLiteral("settle 300 ms")), qPrintable(recovery));
        QVERIFY2(recovery.contains(QStringLiteral("settle retries: 1")), qPrintable(recovery));
    }

    // The rounds a poll runs while its settle lasts are not the plan's rounds: "after 3" means after
    // the third round that completes once the link has settled, and the poll then goes on for the
    // rest of its rounds.
    void HIL_07_aPollCountsItsActionRoundsFromTheFirstRoundAfterTheSettle() {
        const Watchdog dog(60, "the settled poll run");
        const Profile q = loadExample(QStringLiteral("q03ude-eth-3e-bin"));
        PerConnectionPlc server(q.device.frame);
        QVERIFY(server.listen());
        const QString name = QStringLiteral("e2e-settlepoll");
        const QString dir = scratchDir(name);
        QFile planFile(dir + QStringLiteral("/plan.json"));
        QVERIFY(planFile.open(QIODevice::WriteOnly));
        planFile.write(R"JSON({"schema":1,"plan":{"id":"settlepoll"},"steps":[
            {"id":"P-01","kind":"poll","rounds":6,"subscribe":[{"name":"d","device":"D@s","count":2}],
             "actions":[{"after":3,"write":{"device":"D@s+1","values":[7]}}]}
        ]})JSON");
        planFile.close();
        Options options;
        options.profilePath =
            writeProfile(dir, QStringLiteral("q03ude-eth-3e-bin"), name, server.port(), QString(), 400);
        options.planPath = dir + QStringLiteral("/plan.json");
        options.outputRoot = dir + QStringLiteral("/captured");
        options.yes = true;
        options.settleMs = 500; // loopback, but the settle is asked for
        const ToolRun run = runToolWith(options, name + QLatin1Char('\n'));
        QVERIFY2(static_cast<int>(run.code) == static_cast<int>(ExitCode::Ok),
                 qPrintable(run.out + run.err));
        const std::vector<mc::test::Vector> session =
            load(options.outputRoot + QLatin1Char('/') + name + QStringLiteral("/session.vec"));
        qint64 writeSeq = -1;
        qint64 roundsBefore = 0;
        qint64 roundsAfter = 0;
        for (const mc::test::Vector& v : session) {
            const qint64 seq = QString::fromStdString(v.field("seq")).toLongLong();
            if (v.field("kind") == "input" && v.field("input") == "write") {
                writeSeq = seq;
            } else if (v.field("event") == "cycleDone") {
                (writeSeq < 0 ? roundsBefore : roundsAfter) += 1;
            }
        }
        QVERIFY2(writeSeq > 0, "the action was never made");
        // Rounds ran during the 500 ms settle (20 ms interval) and are not counted: the write comes
        // after at least 3 more rounds and the poll then runs the 3 that remain.
        QVERIFY2(roundsBefore >= 3 + 5, qPrintable(QString::number(roundsBefore)));
        QVERIFY2(roundsAfter >= 2, qPrintable(QString::number(roundsAfter)));
    }

    // One loopback rule for the runner (the settle default) and the workbench (mock or PLC).
    void HIL_07_theLoopbackRuleIsSharedAndAcceptsTheSpellingsOfTheWorkbench() {
        for (const char* host : {"127.0.0.1", "127.0.0.2", "127.5.5.5", "localhost", "LocalHost.",
                                 "localhost.", "::1", "[::1]", " [::1] ", "::ffff:127.0.0.1"}) {
            QVERIFY2(isLoopbackHost(QString::fromLatin1(host)), host);
        }
        for (const char* host : {"192.0.2.10", "192.168.1.10", "[::2]", "notlocalhost.",
                                 "plc.example.org", "", "128.0.0.1"}) {
            QVERIFY2(!isLoopbackHost(QString::fromLatin1(host)), host);
        }
    }
};

// The serial flow, over the virtual COM pair; the tool opens the first port, virtual_plc the
// second.
class HilSerialRunTests : public QObject {
    Q_OBJECT

  private:
    // MC_TEST_SERIAL_PAIR says the pair exists: a virtual_plc that cannot open its port is a busy
    // or broken port and fails the test at once with the reason; only a missing program skips.
    static void failOrSkipSerial(const QString& why) {
        if (why.isEmpty()) {
            return;
        }
        if (why.contains(QStringLiteral("is not built"))) {
            QSKIP(qPrintable(why));
        }
        QFAIL(qPrintable(
            QStringLiteral(
                "virtual_plc cannot use its COM port (busy? another process holds it): ") +
            why));
    }

  private slots:
    void HIL_04_theCatalogue3CPlanRunsAgainstVirtualPlcWithoutAToolError() {
        const QStringList pair =
            qEnvironmentVariable("MC_TEST_SERIAL_PAIR").split(QLatin1Char(','));
        if (pair.size() != 2) {
            QSKIP("MC_TEST_SERIAL_PAIR is not set (e.g. COM50,COM51)");
        }
        const Watchdog dog(90, "the 3C catalogue run");
        VirtualPlc plc;
        const QString why =
            plc.start({QStringLiteral("--frame"), QStringLiteral("3C"), QStringLiteral("--format"),
                       QStringLiteral("4"), QStringLiteral("--serial"), pair[1].trimmed(),
                       QStringLiteral("--baud"), QStringLiteral("115200")});
        failOrSkipSerial(why);
        // 115200 baud: the virtual ports keep the line speed, and a 960-word frame is 4 kB of
        // ASCII.
        const QString dir = scratchDir(QStringLiteral("run_catalogue_3c"));
        Options options;
        options.profilePath =
            writeProfile(dir, QStringLiteral("q03ude-c24-3c-f4"), QStringLiteral("cat-3c-f4"), 0,
                         pair[0].trimmed(), 3000, 115200);
        options.planPath = testsDir() + QStringLiteral("/hil/plans/qna_serial.json");
        options.outputRoot = dir + QStringLiteral("/captured");
        options.yes = true;
        options.benchReps = 3;
        const ToolRun run = runToolWith(options, QStringLiteral("cat-3c-f4\n\n\n\n"));
        QVERIFY2(static_cast<int>(run.code) == static_cast<int>(ExitCode::Ok),
                 qPrintable(run.out + run.err));
        QCOMPARE(countsOf(run.out).failed, 0);
    }

    void HIL_04_aBusyComPortLeavesNoComNameInRunMeta() {
        const QStringList pair =
            qEnvironmentVariable("MC_TEST_SERIAL_PAIR").split(QLatin1Char(','));
        if (pair.size() != 2) {
            QSKIP("MC_TEST_SERIAL_PAIR is not set (e.g. COM50,COM51)");
        }
        const Watchdog dog(60, "the busy COM port run");
        // Somebody else holds the first port of the pair: the tool's open is refused ("Access is
        // denied") and the transport's text names the port.
        QSerialPort holder(pair[0].trimmed());
        if (!holder.open(QIODevice::ReadWrite)) {
            QFAIL(qPrintable(
                QStringLiteral(
                    "the test cannot open the pair itself (busy? another process holds it): ") +
                holder.errorString()));
        }
        const QString dir = scratchDir(QStringLiteral("run_meta_busy"));
        QFile planFile(dir + QStringLiteral("/plan.json"));
        QVERIFY(planFile.open(QIODevice::WriteOnly));
        planFile.write(R"JSON({"schema":1,"plan":{"id":"busy"},"steps":[
            {"id":"X-01","kind":"read","device":"D@s"},
            {"id":"X-02","kind":"read","device":"D@s"}
        ]})JSON");
        planFile.close();
        Options options;
        options.profilePath = writeProfile(dir, QStringLiteral("q03ude-c24-3c-f4"),
                                           QStringLiteral("e2e-busy"), 0, pair[0].trimmed(), 400);
        options.planPath = dir + QStringLiteral("/plan.json");
        options.outputRoot = dir + QStringLiteral("/captured");
        options.yes = true;
        options.reconnectSeconds = 1;
        const ToolRun run = runToolWith(options);
        holder.close();
        QCOMPARE(static_cast<int>(run.code), static_cast<int>(ExitCode::RunFailures));
        const QString file = options.outputRoot + QStringLiteral("/e2e-busy/run.meta");
        const QString text = readAll(file);
        QVERIFY2(!text.isEmpty(), qPrintable(run.out + run.err));
        QVERIFY2(!text.contains(pair[0].trimmed(), Qt::CaseInsensitive), qPrintable(text));
        QCOMPARE(metaOf(file).value(QStringLiteral("notsent.X-01")),
                 QStringLiteral("link down: OpenFailed (Disconnected)"));
    }

    void HIL_04_endToEndOverTheVirtualComPair() {
        const QStringList pair =
            qEnvironmentVariable("MC_TEST_SERIAL_PAIR").split(QLatin1Char(','));
        if (pair.size() != 2) {
            QSKIP("MC_TEST_SERIAL_PAIR is not set (e.g. COM50,COM51)");
        }
        const Watchdog dog(90, "the 3C end-to-end run");
        VirtualPlc plc;
        const QString why =
            plc.start({QStringLiteral("--frame"), QStringLiteral("3C"), QStringLiteral("--format"),
                       QStringLiteral("4"), QStringLiteral("--serial"), pair[1].trimmed(),
                       QStringLiteral("--set"), QStringLiteral("D100=1234")});
        failOrSkipSerial(why);
        const QString dir = scratchDir(QStringLiteral("run_serial"));
        Options options;
        options.profilePath = writeProfile(dir, QStringLiteral("q03ude-c24-3c-f4"),
                                           QStringLiteral("e2e-3c-f4"), 0, pair[0].trimmed(), 1500);
        options.planPath = planPath("plan_3c.json");
        options.outputRoot = dir + QStringLiteral("/captured");
        options.yes = true;
        options.note = QStringLiteral("virtual_plc fixture, not hardware");
        const ToolRun run = runToolWith(options, QStringLiteral("e2e-3c-f4\n"));
        QVERIFY2(static_cast<int>(run.code) == static_cast<int>(ExitCode::Ok),
                 qPrintable(run.out + run.err));
        const Counts c = countsOf(run.out);
        QVERIFY2(c.passed == 13 && c.failed == 0 && c.diverged == 0 && c.notSupported == 0 &&
                     c.skipped == 1,
                 qPrintable(run.out));
        // The serial recovery of E-10 is an EOT, not a reconnect: run.meta has no recovery entry,
        // the wire has the EOT as its own exchange.
        const QString folder = options.outputRoot + QStringLiteral("/e2e-3c-f4");
        const std::vector<mc::test::Vector> steps = load(folder + QStringLiteral("/steps.vec"));
        const QString p = QStringLiteral("CAP-e2e-3c-f4-");
        QCOMPARE(field(steps, p + QStringLiteral("E-01"), "frame"), QStringLiteral("3C"));
        QCOMPARE(field(steps, p + QStringLiteral("E-01"), "format"), QStringLiteral("4"));
        QCOMPARE(field(steps, p + QStringLiteral("E-01"), "code"), QStringLiteral("Ascii"));
        QCOMPARE(field(steps, p + QStringLiteral("E-01-R"), "outcome"), QStringLiteral("ok"));
        QCOMPARE(field(steps, p + QStringLiteral("E-10"), "outcome"), QStringLiteral("timeout"));
        QCOMPARE(field(steps, p + QStringLiteral("E-10.2-R"), "outcome"), QStringLiteral("ok"));
        // One COM port, one mock for the whole run: D100 still holds what E-02 wrote.
        QCOMPARE(field(steps, p + QStringLiteral("E-10.2-R"), "expect"), QStringLiteral("words 1"));
        const QString metaText = readAll(folder + QStringLiteral("/run.meta"));
        QVERIFY(metaText.contains(QStringLiteral("serial.baudRate: 9600")));
        QVERIFY(!metaText.contains(pair[0].trimmed()));
        QVERIFY(!metaText.contains(pair[1].trimmed()));
        QVERIFY(metaText.contains(QStringLiteral("frame.format: Format4")));
        const std::vector<mc::test::Vector> session = load(folder + QStringLiteral("/session.vec"));
        int cycles = 0;
        for (const mc::test::Vector& v : session) {
            cycles += v.field("event") == "cycleDone" ? 1 : 0;
        }
        QVERIFY2(cycles >= 5, qPrintable(QString::number(cycles)));
    }
};

} // namespace

QObject* makeRunSuite() { return new HilRunTests; }
QObject* makeSerialRunSuite() { return new HilSerialRunTests; }

} // namespace mc::hil::test

#include "tst_hil_run.moc"
