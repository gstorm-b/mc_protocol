// tst_tcp_transport.cpp -- T-035: TcpTransport against a local QTcpServer on 127.0.0.1 (an
// OS-chosen port), and registerMetaTypes(). Test ids: QDV-06 and QDV-12 are the spec's
// (SPEC-qt-device.md, transport-level parts); TRN-nn cover the Transport contract of transport.h
// and MTA-nn the meta-type registration. No test gates a positive outcome on a fixed sleep: they
// wait on signals with a bounded timeout. The few negative checks ("nothing more is emitted")
// let the events that are already queued run, and TRN_04 additionally waits a short fixed time
// to prove that a cancelled connect timer stays cancelled.
#include "mc/device/meta_types.h"
#include "mc/device/tcp_transport.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

#include <cstdint>
#include <memory>

namespace {

constexpr int kWaitMs = 5000;

mc::TcpSettings settingsFor(quint16 port, int connectTimeoutMs = 2000) {
    mc::TcpSettings s;
    s.host = QStringLiteral("127.0.0.1");
    s.port = port;
    s.connectTimeoutMs = connectTimeoutMs;
    return s;
}

// A port on which nobody listens: bind an OS-chosen one, then release it.
quint16 unusedPort() {
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) {
        return 0;
    }
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}

// Lets every event that is already queued run.
void settle() {
    for (int i = 0; i < 3; ++i) {
        QCoreApplication::processEvents();
    }
}

mc::ByteView view(const QByteArray& b) {
    return mc::ByteView{reinterpret_cast<const uint8_t*>(b.constData()),
                        static_cast<size_t>(b.size())};
}

bool listenOnLoopback(QTcpServer& server) { return server.listen(QHostAddress::LocalHost, 0); }

// Waits for one accepted connection; nullptr on timeout.
QTcpSocket* accept(QTcpServer& server) {
    if (!server.hasPendingConnections()) {
        QSignalSpy spy(&server, &QTcpServer::newConnection);
        if (!spy.wait(kWaitMs)) {
            return nullptr;
        }
    }
    return server.nextPendingConnection();
}

// Emits the types the device layer sends through signals, so the test can queue them.
class Emitter : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
  signals:
    void changes(mc::DeviceType type, quint32 round, const QVector<mc::Change>& list);
    void failed(const mc::Error& error);
    void cycle(const mc::CycleInfo& info);
};

} // namespace

class TstTcpTransport : public QObject {
    Q_OBJECT

  private slots:
    void QDV_12_socketOptionsLowDelayAndKeepAliveAreSet() {
        QTcpServer server;
        QVERIFY(listenOnLoopback(server));
        mc::TcpTransport transport(settingsFor(server.serverPort()));
        QSignalSpy opened(&transport, &mc::Transport::opened);
        transport.open();
        QVERIFY(opened.wait(kWaitMs));

        // The socket is the transport's only QTcpSocket child; the options are read from it.
        QTcpSocket* socket = transport.findChild<QTcpSocket*>();
        QVERIFY(socket != nullptr);
        QCOMPARE(socket->socketOption(QAbstractSocket::LowDelayOption).toInt(), 1);
        QCOMPARE(socket->socketOption(QAbstractSocket::KeepAliveOption).toInt(), 1);
    }

    void QDV_06_openFailsWhenNobodyListensWithinTimeout() {
        const quint16 port = unusedPort();
        QVERIFY(port != 0);
        // Windows reports a refused loopback connect only after several seconds, so on that
        // platform the connect timer is what ends this open; on others the refusal does. Both
        // must yield exactly one openFailed within the bound.
        const int timeoutMs = 2000;
        mc::TcpTransport transport(settingsFor(port, timeoutMs));
        QSignalSpy opened(&transport, &mc::Transport::opened);
        QSignalSpy failed(&transport, &mc::Transport::openFailed);

        QElapsedTimer clock;
        clock.start();
        transport.open();
        QCOMPARE(transport.state(), mc::Transport::State::Opening);
        QVERIFY(failed.wait(timeoutMs + 500));
        QVERIFY2(clock.elapsed() <= timeoutMs + 500, qPrintable(QString::number(clock.elapsed())));

        QCOMPARE(failed.count(), 1);
        QVERIFY(!failed.first().first().toString().isEmpty());
        QCOMPARE(transport.state(), mc::Transport::State::Closed);
        settle();
        QCOMPARE(failed.count(), 1);
        QCOMPARE(opened.count(), 0);
    }

    void QDV_06_connectTimerBoundsAnUnroutableHost() {
        // 10.255.255.1 is normally a black hole; on a machine that answers at once (network
        // unreachable) the same bound holds. Either way exactly one openFailed arrives in time.
        mc::TcpSettings s;
        s.host = QStringLiteral("10.255.255.1");
        s.port = 5000;
        s.connectTimeoutMs = 300;
        mc::TcpTransport transport(s);
        QSignalSpy opened(&transport, &mc::Transport::opened);
        QSignalSpy failed(&transport, &mc::Transport::openFailed);

        QElapsedTimer clock;
        clock.start();
        transport.open();
        QVERIFY(failed.wait(s.connectTimeoutMs + 500));
        QVERIFY2(clock.elapsed() <= s.connectTimeoutMs + 500,
                 qPrintable(QString::number(clock.elapsed())));
        QVERIFY(!failed.first().first().toString().isEmpty());
        QCOMPARE(transport.state(), mc::Transport::State::Closed);
        settle();
        QCOMPARE(failed.count(), 1);
        QCOMPARE(opened.count(), 0);
    }

    void TRN_01_openedIsEmittedOnceAndOpenWhileBusyIsIgnored() {
        QTcpServer server;
        QVERIFY(listenOnLoopback(server));
        QSignalSpy connections(&server, &QTcpServer::newConnection);
        mc::TcpTransport transport(settingsFor(server.serverPort()));
        QSignalSpy opened(&transport, &mc::Transport::opened);
        QSignalSpy failed(&transport, &mc::Transport::openFailed);

        QCOMPARE(transport.state(), mc::Transport::State::Closed);
        QCOMPARE(transport.describe(), QStringLiteral("127.0.0.1:%1").arg(server.serverPort()));
        transport.open();
        transport.open(); // Opening: ignored
        QCOMPARE(transport.state(), mc::Transport::State::Opening);
        QVERIFY(opened.wait(kWaitMs));
        transport.open(); // Open: ignored
        settle();

        QCOMPARE(transport.state(), mc::Transport::State::Open);
        QCOMPARE(opened.count(), 1);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(connections.count(), 1);
    }

    void TRN_02_bytesTravelBothWaysAndReadDoesNotBlock() {
        QTcpServer server;
        QVERIFY(listenOnLoopback(server));
        mc::TcpTransport transport(settingsFor(server.serverPort()));
        QSignalSpy opened(&transport, &mc::Transport::opened);
        QSignalSpy readyRead(&transport, &mc::Transport::readyRead);
        transport.open();
        QVERIFY(opened.wait(kWaitMs));
        QTcpSocket* peer = accept(server);
        QVERIFY(peer != nullptr);

        // Transport -> server.
        const QByteArray request("\x50\x00\xFF\xFF\x03\x00", 6);
        QVERIFY(transport.write(view(request)));
        QVERIFY(transport.write(mc::ByteView{})); // an empty write is a successful no-op
        QByteArray received;
        QTRY_VERIFY_WITH_TIMEOUT((received += peer->readAll(), received.size() >= request.size()),
                                 kWaitMs);
        QCOMPARE(received, request);

        // Server -> transport, drained through a buffer smaller than the data.
        uint8_t buffer[4];
        QCOMPARE(transport.read(mc::MutableByteView{buffer, sizeof buffer}), size_t{0});
        QCOMPARE(readyRead.count(), 0);
        const QByteArray reply("abcdefghij");
        QCOMPARE(peer->write(reply), qint64(reply.size()));
        peer->flush();
        QTRY_VERIFY_WITH_TIMEOUT(readyRead.count() >= 1, kWaitMs);

        // QTRY_* evaluates its condition again after it first holds, so the condition must be
        // idempotent: drain() appends whatever is available (possibly nothing) and reports.
        QByteArray got;
        auto drain = [&]() {
            for (size_t n = transport.read(mc::MutableByteView{buffer, sizeof buffer}); n > 0;
                 n = transport.read(mc::MutableByteView{buffer, sizeof buffer})) {
                got.append(reinterpret_cast<const char*>(buffer), static_cast<int>(n));
            }
            return got.size() >= reply.size();
        };
        QTRY_VERIFY_WITH_TIMEOUT(drain(), kWaitMs);
        QCOMPARE(got, reply);
        QCOMPARE(transport.read(mc::MutableByteView{buffer, sizeof buffer}), size_t{0});
        QCOMPARE(transport.read(mc::MutableByteView{}), size_t{0});
    }

    void TRN_02_writeAndReadWhenNotOpenDoNothing() {
        mc::TcpTransport transport(settingsFor(unusedPort()));
        const QByteArray data("x");
        uint8_t buffer[4];
        QVERIFY(!transport.write(view(data)));
        QCOMPARE(transport.read(mc::MutableByteView{buffer, sizeof buffer}), size_t{0});
    }

    void TRN_03_peerCloseEmitsOneLost() {
        QTcpServer server;
        QVERIFY(listenOnLoopback(server));
        mc::TcpTransport transport(settingsFor(server.serverPort()));
        QSignalSpy opened(&transport, &mc::Transport::opened);
        QSignalSpy failed(&transport, &mc::Transport::openFailed);
        QSignalSpy lost(&transport, &mc::Transport::lost);
        transport.open();
        QVERIFY(opened.wait(kWaitMs));
        QTcpSocket* peer = accept(server);
        QVERIFY(peer != nullptr);

        peer->disconnectFromHost();
        QTRY_COMPARE_WITH_TIMEOUT(lost.count(), 1, kWaitMs);
        QVERIFY(!lost.first().first().toString().isEmpty());
        QCOMPARE(transport.state(), mc::Transport::State::Closed);
        settle();
        QCOMPARE(lost.count(), 1);
        QCOMPARE(opened.count(), 1);
        QCOMPARE(failed.count(), 0);
        QVERIFY(!transport.write(view(QByteArray("x"))));
    }

    void TRN_04_closeEmitsNothingAndTheServerSeesIt() {
        QTcpServer server;
        QVERIFY(listenOnLoopback(server));
        mc::TcpTransport transport(settingsFor(server.serverPort()));
        QSignalSpy opened(&transport, &mc::Transport::opened);
        QSignalSpy failed(&transport, &mc::Transport::openFailed);
        QSignalSpy lost(&transport, &mc::Transport::lost);
        transport.open();
        QVERIFY(opened.wait(kWaitMs));
        QTcpSocket* peer = accept(server);
        QVERIFY(peer != nullptr);
        QSignalSpy peerGone(peer, &QTcpSocket::disconnected);

        transport.close();
        QCOMPARE(transport.state(), mc::Transport::State::Closed);
        QVERIFY(peerGone.wait(kWaitMs));
        settle();
        QCOMPARE(lost.count(), 0);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(opened.count(), 1);

        transport.close(); // idempotent
        QCOMPARE(transport.state(), mc::Transport::State::Closed);
    }

    void TRN_04_closeWhileOpeningCancelsTheOpen() {
        QTcpServer server;
        QVERIFY(listenOnLoopback(server));
        mc::TcpTransport transport(settingsFor(server.serverPort(), 50));
        QSignalSpy opened(&transport, &mc::Transport::opened);
        QSignalSpy failed(&transport, &mc::Transport::openFailed);
        QSignalSpy lost(&transport, &mc::Transport::lost);

        transport.open();
        transport.close();
        QCOMPARE(transport.state(), mc::Transport::State::Closed);
        // A cancelled open must stay cancelled, connect timer included: wait past the 50 ms.
        QTest::qWait(150);
        QCOMPARE(opened.count(), 0);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(lost.count(), 0);
    }

    void TRN_05_theTransportCanBeReopenedAfterCloseAndAfterLoss() {
        QTcpServer server;
        QVERIFY(listenOnLoopback(server));
        QSignalSpy connections(&server, &QTcpServer::newConnection);
        mc::TcpTransport transport(settingsFor(server.serverPort()));
        QSignalSpy opened(&transport, &mc::Transport::opened);
        QSignalSpy lost(&transport, &mc::Transport::lost);

        transport.open();
        QVERIFY(opened.wait(kWaitMs));
        transport.close();

        transport.open();
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 2, kWaitMs);
        QCOMPARE(transport.state(), mc::Transport::State::Open);

        // The server holds both connections; drop them, and the live one is the loss.
        QTRY_COMPARE_WITH_TIMEOUT(connections.count(), 2, kWaitMs);
        while (QTcpSocket* peer = server.nextPendingConnection()) {
            peer->disconnectFromHost();
        }
        QTRY_COMPARE_WITH_TIMEOUT(lost.count(), 1, kWaitMs);
        QCOMPARE(transport.state(), mc::Transport::State::Closed);

        transport.open();
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 3, kWaitMs);
        QCOMPARE(transport.state(), mc::Transport::State::Open);
    }

    void TRN_06_destroyingAnOpenTransportIsSilentAndClosesTheSocket() {
        QTcpServer server;
        QVERIFY(listenOnLoopback(server));
        auto transport = std::make_unique<mc::TcpTransport>(settingsFor(server.serverPort()));
        QSignalSpy opened(transport.get(), &mc::Transport::opened);
        QSignalSpy lost(transport.get(), &mc::Transport::lost);
        transport->open();
        QVERIFY(opened.wait(kWaitMs));
        QTcpSocket* peer = accept(server);
        QVERIFY(peer != nullptr);
        QSignalSpy peerGone(peer, &QTcpSocket::disconnected);

        transport.reset();
        QVERIFY(peerGone.wait(kWaitMs));
        QCOMPARE(lost.count(), 0);
    }

    void TRN_07_aFailedOpenEndsInExactlyOneOpenFailedAfterOpenReturned() {
        // An empty host fails at once. What this proves: exactly one openFailed(), none before
        // open() has returned, none after. What it cannot prove: on Windows Qt reports this error
        // asynchronously anyway, so the m_inOpen deferral in TcpTransport (for platforms that fail
        // inside connectToHost(), e.g. Linux with an unreachable network) passes with or without it.
        mc::TcpSettings s;
        s.host = QString();
        s.port = 5000;
        s.connectTimeoutMs = 2000;
        mc::TcpTransport transport(s);
        QSignalSpy opened(&transport, &mc::Transport::opened);
        QSignalSpy failed(&transport, &mc::Transport::openFailed);

        transport.open();
        QCOMPARE(failed.count(), 0);
        QVERIFY(failed.wait(s.connectTimeoutMs + 500));
        QCOMPARE(failed.count(), 1);
        QCOMPARE(transport.state(), mc::Transport::State::Closed);
        settle();
        QCOMPARE(failed.count(), 1);
        QCOMPARE(opened.count(), 0);
    }

    // A write that fails while the transport believes it is Open: the socket is aborted behind
    // its back with its signals blocked, so no disconnected()/error reaches the transport, and
    // QAbstractSocket::write() then fails with "Socket is not connected".
    void TRN_08_aFailedWriteClosesAndReportsLostOnceFromTheEventLoop() {
        QTcpServer server;
        QVERIFY(listenOnLoopback(server));
        mc::TcpTransport transport(settingsFor(server.serverPort()));
        QSignalSpy opened(&transport, &mc::Transport::opened);
        QSignalSpy lost(&transport, &mc::Transport::lost);
        transport.open();
        QVERIFY(opened.wait(kWaitMs));
        QTcpSocket* peer = accept(server);
        QVERIFY(peer != nullptr);

        QTcpSocket* socket = transport.findChild<QTcpSocket*>();
        QVERIFY(socket != nullptr);
        socket->blockSignals(true);
        socket->abort();
        socket->blockSignals(false);
        QCOMPARE(transport.state(), mc::Transport::State::Open);

        const QByteArray bytes("abc");
        QVERIFY(!transport.write(view(bytes)));
        QCOMPARE(transport.state(), mc::Transport::State::Closed);
        QCOMPARE(lost.count(), 0); // never from inside write()
        QVERIFY(lost.wait(kWaitMs));
        QCOMPARE(lost.count(), 1);
        QVERIFY(!transport.lastLossWasPeerClose()); // a local failure, not a peer close
        settle();
        QCOMPARE(lost.count(), 1);
    }

    void TRN_09_closeAfterAFailedWriteCancelsTheDeferredLost() {
        QTcpServer server;
        QVERIFY(listenOnLoopback(server));
        mc::TcpTransport transport(settingsFor(server.serverPort()));
        QSignalSpy opened(&transport, &mc::Transport::opened);
        QSignalSpy lost(&transport, &mc::Transport::lost);
        transport.open();
        QVERIFY(opened.wait(kWaitMs));
        QTcpSocket* peer = accept(server);
        QVERIFY(peer != nullptr);
        QTcpSocket* socket = transport.findChild<QTcpSocket*>();
        QVERIFY(socket != nullptr);
        socket->blockSignals(true);
        socket->abort();
        socket->blockSignals(false);

        const QByteArray bytes("abc");
        QVERIFY(!transport.write(view(bytes)));
        transport.close(); // the owner reacted before the event loop ran: nothing more is emitted
        settle();
        QTest::qWait(50); // negative check: a queued lost() would have been delivered by now
        QCOMPARE(lost.count(), 0);
    }

    void MTA_01_registerMetaTypesIsIdempotentAndNamesTheTypes() {
        mc::registerMetaTypes();
        const int changeId = qMetaTypeId<mc::Change>();
        mc::registerMetaTypes();
        QCOMPARE(qMetaTypeId<mc::Change>(), changeId);

        QVERIFY(QMetaType::fromName("mc::DeviceType").isValid());
        QVERIFY(QMetaType::fromName("mc::Error").isValid());
        QVERIFY(QMetaType::fromName("mc::Change").isValid());
        QVERIFY(QMetaType::fromName("mc::CycleInfo").isValid());
        QVERIFY(QMetaType::fromType<QVector<mc::Change>>().isValid());
    }

    void MTA_02_signalValueTypesArriveIntactThroughAQueuedConnection() {
        // Checks the values, not the registration: Qt 6 queues functor connections without
        // qRegisterMetaType(); MTA_01 is the check that registerMetaTypes() registers.
        mc::registerMetaTypes();
        Emitter emitter;
        QObject receiver;

        mc::DeviceType gotType = mc::DeviceType::D;
        quint32 gotRound = 0;
        QVector<mc::Change> gotChanges;
        QObject::connect(
            &emitter, &Emitter::changes, &receiver,
            [&](mc::DeviceType t, quint32 round, const QVector<mc::Change>& list) {
                gotType = t;
                gotRound = round;
                gotChanges = list;
            },
            Qt::QueuedConnection);
        mc::Error gotError;
        QObject::connect(
            &emitter, &Emitter::failed, &receiver, [&](const mc::Error& e) { gotError = e; },
            Qt::QueuedConnection);
        mc::CycleInfo gotCycle{};
        QObject::connect(
            &emitter, &Emitter::cycle, &receiver, [&](const mc::CycleInfo& c) { gotCycle = c; },
            Qt::QueuedConnection);

        mc::Change change{};
        change.device = mc::Device{mc::DeviceType::M, 17};
        change.oldValue = 1;
        change.newValue = 0;
        QVector<mc::Change> sent;
        sent.push_back(change);
        mc::Error error{};
        error.category = mc::ErrorCategory::Transport;
        error.code = mc::ErrorCode::Timeout;
        error.message = "timed out";
        mc::CycleInfo info{};
        info.round = 7;
        info.failedChunks = 2;

        emit emitter.changes(mc::DeviceType::M, 3, sent);
        emit emitter.failed(error);
        emit emitter.cycle(info);
        QCOMPARE(gotRound, 0u); // queued: nothing is delivered before the event loop runs
        settle();

        QCOMPARE(gotType, mc::DeviceType::M);
        QCOMPARE(gotRound, 3u);
        QCOMPARE(gotChanges.size(), 1);
        QVERIFY(gotChanges.first().device == change.device);
        QCOMPARE(gotChanges.first().oldValue, uint16_t{1});
        QCOMPARE(gotChanges.first().newValue, uint16_t{0});
        QCOMPARE(gotError.code, mc::ErrorCode::Timeout);
        QCOMPARE(QString::fromLatin1(gotError.message), QStringLiteral("timed out"));
        QCOMPARE(gotCycle.round, 7u);
        QCOMPARE(gotCycle.failedChunks, uint16_t{2});
    }
};

QTEST_GUILESS_MAIN(TstTcpTransport)

#include "tst_tcp_transport.moc"
