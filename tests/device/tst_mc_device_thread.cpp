// tst_mc_device_thread.cpp -- QDV-09: an McDevice moved to a QThread. The test creates and owns
// the QThread; the library never starts one. The device runs against a MockPlcServer on
// 127.0.0.1 (the server lives in the main thread); the recorder lives in the main thread, so
// every signal reaches it through a queued connection.
#include "device_recorder.h"
#include "device_test_support.h"
#include "fake_transport.h"
#include "mock_plc_server.h"

#include "mc/device/mc_device.h"

#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QtTest>

#include <atomic>

using namespace testing;

namespace {

// Stops the thread on every exit path, so a failed QVERIFY cannot destroy a running QThread.
struct ThreadGuard {
    explicit ThreadGuard(QThread& t) : thread(t) {}
    ~ThreadGuard() {
        thread.quit();
        thread.wait(kWaitMs);
    }
    QThread& thread;
};

} // namespace

class TstMcDeviceThread : public QObject {
    Q_OBJECT

  private slots:
    void QDV_09_deviceOnAThreadSignalsArriveInTheMainThread() {
        QThread* const mainThread = QThread::currentThread();
        MockPlcServer server;
        server.setInit(seedMemory);
        QVERIFY(server.listen());

        QThread worker;
        ThreadGuard guard(worker);
        auto* device = new mc::McDevice(configFor(server.port()));
        QPointer<mc::McDevice> alive(device);
        // The device dies in its own thread when the thread ends.
        connect(&worker, &QThread::finished, device, &QObject::deleteLater);

        DeviceRecorder recorder(*device, this); // main thread: queued delivery
        std::atomic<QThread*> emittedIn{nullptr};
        connect(
            device, &mc::McDevice::snapshotReady, device,
            [&emittedIn](const mc::DeviceSnapshot&) { emittedIn = QThread::currentThread(); },
            Qt::DirectConnection);

        device->moveToThread(&worker);
        worker.start();
        QMetaObject::invokeMethod(device, &mc::McDevice::connectToPlc, Qt::QueuedConnection);
        QTRY_VERIFY_WITH_TIMEOUT(recorder.hasCycle(1), kWaitMs);

        // Emitted in the worker thread, received in the main thread.
        QCOMPARE(emittedIn.load(), &worker);
        for (const Event& e : recorder.events) {
            QCOMPARE(e.thread, mainThread);
        }
        QCOMPARE(recorder.trace().mid(0, 5),
                 (QStringList{"link(Connecting,Requested)", "link(Connected,Requested)",
                              "snapshot(M,1)", "snapshot(D,1)", "cycle(1)"}));
        const Event snapshotD = recorder.of(Event::Kind::Snapshot).at(1);
        QCOMPARE(snapshotD.snapshot.segments.at(0).values, wordsLe({10, 20, 30, 40}));

        // Values changed in the mock arrive as a queued valuesChanged.
        server.plc()->setWord(dev("D101"), 555);
        QTRY_VERIFY_WITH_TIMEOUT(recorder.count(Event::Kind::Changed) >= 1, kWaitMs);
        const Event changed = recorder.of(Event::Kind::Changed).at(0);
        QCOMPARE(changed.changes.at(0).device.number, 101u);
        QCOMPARE(changed.changes.at(0).newValue, uint16_t{555});

        // values() is read on the device's thread (blocking call, no lock in the library).
        uint16_t seen = 0;
        QMetaObject::invokeMethod(
            device, [&]() { seen = device->values().word(dev("D101")); },
            Qt::BlockingQueuedConnection);
        QCOMPARE(seen, uint16_t{555});

        // A request submitted on the device's thread completes with a queued requestFinished.
        mc::RequestId id = 0;
        QMetaObject::invokeMethod(
            device,
            [&]() {
                const mc::Expected<mc::RequestId> r = device->writeWords(u"D200", {42});
                id = r ? r.value() : 0;
            },
            Qt::BlockingQueuedConnection);
        QVERIFY(id != 0);
        QTRY_VERIFY_WITH_TIMEOUT(recorder.count(Event::Kind::Finished) == 1, kWaitMs);
        QCOMPARE(recorder.of(Event::Kind::Finished).at(0).id, id);
        QVERIFY(recorder.of(Event::Kind::Finished).at(0).error.ok());
        QCOMPARE(server.plc()->word(dev("D200")), uint16_t{42});

        QMetaObject::invokeMethod(device, &mc::McDevice::disconnectFromPlc,
                                  Qt::BlockingQueuedConnection);
        QTRY_VERIFY_WITH_TIMEOUT(recorder.trace().last() == "link(Disconnected,Requested)", kWaitMs);
        worker.quit();
        QVERIFY(worker.wait(kWaitMs));
        QVERIFY(alive.isNull()); // deleted in its thread when the thread finished
    }

    void THR_01_theWholeObjectTreeMovesWithTheDevice() {
        QThread worker;
        ThreadGuard guard(worker);
        auto fake = std::make_unique<FakeTransport>();
        FakeTransport* transport = fake.get();
        auto device = std::make_unique<mc::McDevice>(configFor(0, mc::FrameConfig::frame3E(), false),
                                                     std::move(fake));
        QCOMPARE(transport->thread(), QThread::currentThread());
        device->moveToThread(&worker);

        // The injected transport and the deadline timer are children, so they moved as well.
        QCOMPARE(transport->thread(), &worker);
        QTimer* timer = device->findChild<QTimer*>();
        QVERIFY(timer != nullptr);
        QCOMPARE(timer->thread(), &worker);

        // Give the object back before its owner (a unique_ptr on this thread) destroys it.
        worker.start();
        QMetaObject::invokeMethod(
            device.get(), [&]() { device->moveToThread(QCoreApplication::instance()->thread()); },
            Qt::BlockingQueuedConnection);
        QCOMPARE(device->thread(), QCoreApplication::instance()->thread());
        QCOMPARE(transport->thread(), QCoreApplication::instance()->thread());
    }
};

QTEST_GUILESS_MAIN(TstMcDeviceThread)

#include "tst_mc_device_thread.moc"
