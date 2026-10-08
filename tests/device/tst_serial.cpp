// tst_serial.cpp -- SerialTransport and McDevice over a serial port. QDV-14 (3C F4 and 1C F1, a
// round and a write each, over a virtual COM pair) needs a pair of ports wired to each other:
// the tests that need one QSKIP unless MC_TEST_SERIAL_PAIR names it, e.g. "COM50,COM51" (the first
// port is the one McDevice or the transport opens, the second one belongs to the test peer).
// The SER-nn tests without a pair prove the open/close contract of the transport on a port that
// does not exist. No positive outcome is gated on a fixed sleep: tests wait on state with QTRY_*
// and a bounded timeout.
#include "device_recorder.h"
#include "device_test_support.h"
#include "serial_bridge.h"
#include "smoke_check.h"

#include "mc/device/mc_device.h"
#include "mc/device/mc_device_config.h"
#include "mc/device/serial_transport.h"

#include <QSerialPort>
#include <QSignalSpy>
#include <QtTest>

using namespace testing;

namespace {

// A port name no machine has, so that opening it fails the same way everywhere.
const QString kMissingPort = QStringLiteral("COM_MC_TEST_MISSING");

const char* const kNoPair =
    "MC_TEST_SERIAL_PAIR is not set (e.g. \"COM50,COM51\"): no virtual COM pair";

mc::SerialSettings lineFor(const QString& portName) {
    mc::SerialSettings line; // the defaults of the old device: 9600 7E1, no flow control
    line.portName = portName;
    return line;
}

} // namespace

class TstSerial : public QObject {
    Q_OBJECT

  private:
    // QDV-14: a McDevice on the device end of the pair, a MockPlc behind the bridge end.
    void smokeOverComPair(const mc::FrameConfig& frame, const SerialPairNames& pair) {
        SerialBridge bridge(frame, lineFor(pair.bridgePort));
        seedMemory(bridge.plc());
        QVERIFY2(bridge.open(), qPrintable(bridge.errorString()));

        mc::McDeviceConfig cfg = configFor(0, frame);
        cfg.transport = mc::TransportKind::Serial;
        cfg.serial = lineFor(pair.devicePort);
        mc::McDevice device(cfg);
        auto* recorder = new DeviceRecorder(device);

        device.connectToPlc();
        // A serial round trip is slower than loopback TCP, so the wait is generous.
        verifySmoke(*recorder, device, [&bridge]() { return &bridge.plc(); }, 15000);
        QCOMPARE(recorder->events.first().kind, Event::Kind::Link);
        QCOMPARE(recorder->count(Event::Kind::Fault), 0);
        QVERIFY(bridge.plc().requests().size() >= 3);

        device.disconnectFromPlc();
        QCOMPARE(device.linkState(), mc::LinkState::Disconnected);
    }

  private slots:
    void SER_01_describeNamesPortBaudAndFraming() {
        mc::SerialTransport defaults(lineFor(QStringLiteral("COM3")));
        QCOMPARE(defaults.describe(), QStringLiteral("COM3 9600 7E1"));

        mc::SerialSettings other = lineFor(QStringLiteral("/dev/ttyUSB0"));
        other.baudRate = 115200;
        other.dataBits = QSerialPort::Data8;
        other.parity = QSerialPort::NoParity;
        other.stopBits = QSerialPort::TwoStop;
        QCOMPARE(mc::SerialTransport(other).describe(), QStringLiteral("/dev/ttyUSB0 115200 8N2"));
        other.parity = QSerialPort::OddParity;
        other.stopBits = QSerialPort::OneAndHalfStop;
        QCOMPARE(mc::SerialTransport(other).describe(),
                 QStringLiteral("/dev/ttyUSB0 115200 8O1.5"));
        other.parity = QSerialPort::SpaceParity;
        QVERIFY(mc::SerialTransport(other).describe().contains(QLatin1String(" 8S1.5")));
        other.parity = QSerialPort::MarkParity;
        QVERIFY(mc::SerialTransport(other).describe().contains(QLatin1String(" 8M1.5")));
    }

    void SER_02_aClosedTransportWritesAndReadsNothing() {
        mc::SerialTransport transport(lineFor(kMissingPort));
        QCOMPARE(transport.state(), mc::Transport::State::Closed);
        const uint8_t bytes[] = {1, 2, 3};
        QVERIFY(!transport.write(mc::ByteView{bytes, sizeof bytes}));
        uint8_t buffer[8];
        QCOMPARE(transport.read(mc::MutableByteView{buffer, sizeof buffer}), size_t{0});
        QVERIFY(!transport.lastLossWasPeerClose());
    }

    void SER_03_openOfAMissingPortFailsFromTheEventLoopExactlyOnce() {
        mc::SerialTransport transport(lineFor(kMissingPort));
        QSignalSpy opened(&transport, &mc::Transport::opened);
        QSignalSpy failed(&transport, &mc::Transport::openFailed);
        QSignalSpy lost(&transport, &mc::Transport::lost);

        transport.open();
        // Nothing is emitted from inside open(); the transport reports Opening until the turn.
        QCOMPARE(failed.count(), 0);
        QCOMPARE(transport.state(), mc::Transport::State::Opening);
        transport.open(); // a second open() while Opening changes nothing

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, kWaitMs);
        QCOMPARE(transport.state(), mc::Transport::State::Closed);
        QVERIFY2(failed.at(0).at(0).toString().contains(kMissingPort),
                 qPrintable(failed.at(0).at(0).toString()));
        QTest::qWait(100); // a bounded negative wait: no second signal follows
        QCOMPARE(failed.count(), 1);
        QCOMPARE(opened.count(), 0);
        QCOMPARE(lost.count(), 0);
    }

    void SER_04_closeBeforeTheOpenCompletesEmitsNothing() {
        mc::SerialTransport transport(lineFor(kMissingPort));
        QSignalSpy any(&transport, &mc::Transport::openFailed);
        QSignalSpy opened(&transport, &mc::Transport::opened);
        QSignalSpy lost(&transport, &mc::Transport::lost);

        transport.open();
        transport.close();
        QCOMPARE(transport.state(), mc::Transport::State::Closed);
        QTest::qWait(100); // bounded negative wait: the deferred open found a newer generation
        QCOMPARE(any.count(), 0);
        QCOMPARE(opened.count(), 0);
        QCOMPARE(lost.count(), 0);

        // The transport is reusable: the next open() runs and fails once for the missing port.
        transport.open();
        QTRY_COMPARE_WITH_TIMEOUT(any.count(), 1, kWaitMs);
    }

    void SER_05_mcDeviceOnAMissingPortPublishesOpenFailedWithTheCause() {
        mc::McDeviceConfig cfg;
        cfg.transport = mc::TransportKind::Serial;
        cfg.serial = lineFor(kMissingPort);
        mc::McDevice device(cfg);
        auto* recorder = new DeviceRecorder(device);

        device.connectToPlc();
        QTRY_COMPARE_WITH_TIMEOUT(device.linkState(), mc::LinkState::Disconnected, kWaitMs);
        QCOMPARE(recorder->trace(), (QStringList{"link(Connecting,Requested)",
                                                 "link(Disconnected,OpenFailed)"}));
        QCOMPARE(recorder->events.at(0).detail, kMissingPort + QStringLiteral(" 9600 7E1"));
        QVERIFY2(recorder->events.at(1).detail.contains(kMissingPort),
                 qPrintable(recorder->events.at(1).detail));
    }

    void SER_06_bytesTravelBothWaysAndThePortReopens() {
        const std::optional<SerialPairNames> pair = serialPairFromEnvironment();
        if (!pair) {
            QSKIP(kNoPair);
        }
        const mc::SerialSettings line = lineFor(pair->devicePort);
        QSerialPort peer; // the other end of the pair, a plain Qt port
        peer.setPortName(pair->bridgePort);
        peer.setBaudRate(line.baudRate);
        peer.setDataBits(line.dataBits);
        peer.setParity(line.parity);
        peer.setStopBits(line.stopBits);
        QVERIFY2(peer.open(QIODevice::ReadWrite), qPrintable(peer.errorString()));

        mc::SerialTransport transport(line);
        QSignalSpy opened(&transport, &mc::Transport::opened);
        QSignalSpy readable(&transport, &mc::Transport::readyRead);
        QSignalSpy lost(&transport, &mc::Transport::lost);

        // Reads everything the transport has so far into `into`.
        auto pumpTransport = [&transport](QByteArray& into) {
            uint8_t buffer[16];
            for (;;) {
                const size_t n = transport.read(mc::MutableByteView{buffer, sizeof buffer});
                if (n == 0) {
                    return;
                }
                into.append(reinterpret_cast<const char*>(buffer),
                            static_cast<QByteArray::size_type>(n));
            }
        };

        for (int round = 0; round < 2; ++round) { // the second round reopens after close()
            opened.clear();
            transport.open();
            QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, kWaitMs);
            QCOMPARE(transport.state(), mc::Transport::State::Open);

            const uint8_t out[] = {'a', 'b', 'c'};
            QVERIFY(transport.write(mc::ByteView{out, sizeof out}));
            QByteArray atPeer;
            QTRY_VERIFY_WITH_TIMEOUT((atPeer += peer.readAll(), atPeer.size() >= 3), kWaitMs);
            QCOMPARE(atPeer, QByteArray("abc"));

            readable.clear();
            peer.write("xyz");
            QTRY_VERIFY_WITH_TIMEOUT(readable.count() >= 1, kWaitMs);
            QByteArray atTransport;
            QTRY_VERIFY_WITH_TIMEOUT((pumpTransport(atTransport), atTransport.size() >= 3),
                                     kWaitMs);
            QCOMPARE(atTransport, QByteArray("xyz"));

            transport.close();
            QCOMPARE(transport.state(), mc::Transport::State::Closed);
        }
        QCOMPARE(lost.count(), 0); // close() never reports a loss
    }

    void QDV_14_threeCFormat4OverAComPair() {
        const std::optional<SerialPairNames> pair = serialPairFromEnvironment();
        if (!pair) {
            QSKIP(kNoPair);
        }
        smokeOverComPair(mc::FrameConfig::frame3C(mc::SerialFormat::Format4), *pair);
    }

    void QDV_14_oneCFormat1OverAComPair() {
        const std::optional<SerialPairNames> pair = serialPairFromEnvironment();
        if (!pair) {
            QSKIP(kNoPair);
        }
        smokeOverComPair(mc::FrameConfig::frame1C(mc::SerialFormat::Format1), *pair);
    }
};

QTEST_GUILESS_MAIN(TstSerial)
#include "tst_serial.moc"
