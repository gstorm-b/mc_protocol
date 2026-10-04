// GUI-05 (SPEC-gui-tool.md "Testing"): back-pressure. A mock answers as fast as it can; a device
// polls it back to back with the trace, the log, a capture and real widgets attached. The memory
// stays bounded and the GUI thread keeps processing events. A second case stops the GUI thread for
// a while: the runner must keep collecting in bounded queues and must not pile events up for the
// GUI thread.
//
// The numbers (memory bound, largest GUI event gap, throughput) are printed with qInfo so that a
// run documents them.
#include "gui_suites.h"
#include "gui_test_support.h"

#include "mc_workbench/capture_types.h"
#include "mc_workbench/device_host.h"
#include "mc_workbench/device_runner.h"
#include "mc_workbench/log_model.h"
#include "mc_workbench/log_view.h"
#include "mc_workbench/mock_host.h"
#include "mc_workbench/mock_runner.h"
#include "mc_workbench/tab_telemetry.h"
#include "mc_workbench/trace_model.h"
#include "mc_workbench/trace_view.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <QTimer>

#include <atomic>

using namespace mc::workbench;
using namespace mc::workbench::test;

namespace {

// The run of the flood case, in seconds ("N seconds" of the spec).
constexpr int kFloodSeconds = 6;
// Rows and bytes the views keep during the test (the defaults are 100 000 rows / 32 MiB).
constexpr int kTraceRows = 5000;
constexpr qint64 kTraceBytes = 1ll * 1024 * 1024;
constexpr int kLogRows = 5000;
// The limit of "GUI event latency": the longest gap between two ticks of a 10 ms timer.
constexpr qint64 kGapLimitMs = 250;
// Growth of the process working set allowed between the end of the warm-up and the end of the run.
constexpr qint64 kGrowthLimit = 48ll * 1024 * 1024;

mc::McDeviceConfig floodConfig(quint16 port) {
    mc::McDeviceConfig cfg = deviceConfig(port, 0); // rounds back to back
    cfg.subscriptions = {{QStringLiteral("D0"), 480}, {QStringLiteral("D1000"), 480},
                         {QStringLiteral("M0"), 512}};
    return cfg;
}

} // namespace

class BackPressureTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { registerRunnerMetaTypes(); }

    void GUI_05_aMockAnsweringAsFastAsItCanKeepsMemoryBoundedAndTheGuiResponsive() {
        MockRig mock(QStringLiteral("t071-flood-mock"));
        QVERIFY(mock.ok);
        TabTelemetry mockTele(QStringLiteral("Mock 1"), TabTelemetry::Kind::Mock);
        mockTele.attachHost(&mock.host);
        mockTele.trace()->setCapacity(kTraceRows, kTraceBytes);

        DeviceHost dev(QStringLiteral("t071-flood-dev"), floodConfig(mock.port));
        TabTelemetry tele(QStringLiteral("PLC 1"), TabTelemetry::Kind::Device);
        tele.attachHost(&dev);
        TraceModel* trace = tele.trace();
        trace->setCapacity(kTraceRows, kTraceBytes);
        LogModel log;
        log.setCapacity(kLogRows);
        connect(&tele, &TabTelemetry::logLines, &log, &LogModel::append);
        connect(&mockTele, &TabTelemetry::logLines, &log, &LogModel::append);

        // Real widgets over the models, so that painting and layout count in the gap.
        TraceView view;
        view.resize(1000, 500);
        view.show();
        view.setTelemetry(&tele);
        LogView logView(&log);
        logView.resize(1000, 300);
        logView.show();

        QSignalSpy link(&dev, &DeviceHost::linkStateChanged);
        QSignalSpy done(&dev, &DeviceHost::commandDone);
        QSignalSpy stats(&mock.host, &MockHost::statsChanged);
        dev.setLogLevel(mc::LogLevel::Debug); // a log line per frame, at the highest rate
        CaptureSettings settings;
        settings.profileId = QStringLiteral("gui-t071-flood");
        settings.maxChunks = 5000; // a capture stays bounded too
        QVERIFY(resultOf(done, tele.startCapture(settings)).ok);

        // The library logs only at events, so a healthy link never floods the log: write lines at full
        // speed through the runner's own sink, the queue the library writes to (100 000 lines a second).
        QTimer logFlood;
        logFlood.setInterval(20);
        connect(&logFlood, &QTimer::timeout, this, [&dev]() {
            dev.post([](DeviceRunner& r) {
                for (int i = 0; i < 2000; ++i) {
                    r.logSink()->write(mc::LogLevel::Info, "test.flood", "a log line of some length, number 1234567890");
                }
            });
        });

        Ticker ticker;
        logFlood.start();
        dev.connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(sawLink(link, mc::LinkState::Connected), kWait);

        // The flood lasts until it has proved the bounds, not for a fixed time: the rings must have
        // seen more than twice what they keep (warm-up), then it goes on for kFloodSeconds more.
        // A generous wall-clock cap ends a run that never gets there.
        constexpr qint64 kCapMs = 120000;
        QElapsedTimer run;
        run.start();
        qint64 warm = 0;
        qint64 warmAt = 0;
        qint64 peak = 0;
        qint64 maxGap = 0;
        while (run.elapsed() < kCapMs && (warm == 0 || run.elapsed() - warmAt < kFloodSeconds * 1000)) {
            QTest::qWait(100);
            const qint64 now = workingSetBytes();
            peak = qMax(peak, now);
            if (warm == 0 && trace->totalSeen() > quint64(kTraceRows) * 2 &&
                log.totalSeen() > quint64(kLogRows) * 2) {
                warm = now; // the rings are full by now
                warmAt = run.elapsed();
            }
            maxGap = qMax(maxGap, ticker.maxGapMs()); // over the whole flood, warm-up included
        }
        const qint64 end = workingSetBytes();

        // The flood really happened: a lot more chunks than the views keep.
        const quint64 seen = trace->totalSeen();
        QVERIFY2(seen > quint64(kTraceRows) * 2, qPrintable(QStringLiteral("only %1 chunks seen").arg(seen)));
        QVERIFY(trace->evicted() > 0);

        // The views are bounded: rows, bytes and memory.
        QVERIFY(trace->rowCount() <= kTraceRows);
        QVERIFY(trace->heldBytes() <= kTraceBytes);
        QVERIFY(mockTele.trace()->rowCount() <= kTraceRows);
        QVERIFY(log.rowCount() <= kLogRows);
        QVERIFY2(log.totalSeen() > quint64(kLogRows) * 2, qPrintable(QStringLiteral("only %1 log lines seen").arg(log.totalSeen())));
        QVERIFY(log.evicted() > 0);
        // memoryBytes() is what the model holds: capped by the byte limit plus the rows' own cost.
        QVERIFY(trace->memoryBytes() <= kTraceBytes + qint64(kTraceRows) * 200);

        // The runner side is bounded: the transport's recording is taken every tick, the queues
        // of the runner never pass their caps.
        std::atomic<int> transportChunks{-1};
        std::atomic<int> pending{-1};
        dev.post([&](DeviceRunner& r) {
            transportChunks = r.transportChunkCount();
            pending = r.pendingFrameCount();
        });
        QTRY_VERIFY_WITH_TIMEOUT(transportChunks.load() >= 0, kWait);
        QVERIFY2(transportChunks.load() < DeviceRunner::kMaxPendingFrames,
                 qPrintable(QStringLiteral("transport holds %1").arg(transportChunks.load())));
        QVERIFY(pending.load() <= DeviceRunner::kMaxPendingFrames);
        QVERIFY(tele.captureStatus().chunks <= settings.maxChunks);
        QVERIFY(tele.captureStatus().full); // the flood passed the capture limit
        QVERIFY(!tele.captureStatus().active);

        // The process: no growth once the rings are full.
        const qint64 growth = end - warm;
        const quint64 requests = stats.isEmpty() ? 0 : stats.last().at(0).value<MockStats>().requests;
        qInfo().noquote()
            << QStringLiteral("GUI-05 flood %1 s: %2 chunks seen (%3 rows kept, %4 evicted), %5 requests served; "
                              "trace ring %6 KiB held (cap %7 rows / %8 MiB), log %9 rows; working set "
                              "after warm-up %10 MiB, peak %11 MiB, end %12 MiB, growth %13 MiB; "
                              "largest GUI event gap %14 ms (limit %15 ms); chunks left out of the view by the runner %16; log lines seen %17")
                   .arg(kFloodSeconds)
                   .arg(seen)
                   .arg(trace->rowCount())
                   .arg(trace->evicted())
                   .arg(requests)
                   .arg(trace->memoryBytes() / 1024)
                   .arg(kTraceRows)
                   .arg(kTraceBytes >> 20)
                   .arg(log.rowCount())
                   .arg(warm >> 20)
                   .arg(peak >> 20)
                   .arg(end >> 20)
                   .arg(growth >> 20)
                   .arg(maxGap)
                   .arg(kGapLimitMs)
                   .arg(trace->droppedByRunner())
                   .arg(log.totalSeen());
        QVERIFY2(warm != 0, "the flood never filled the rings within the wall-clock cap");
        {
            QVERIFY2(growth < kGrowthLimit,
                     qPrintable(QStringLiteral("working set grew %1 MiB after warm-up").arg(growth >> 20)));
        }
        QVERIFY2(maxGap < kGapLimitMs,
                 qPrintable(QStringLiteral("the GUI thread went %1 ms without processing events").arg(maxGap)));

        logFlood.stop();
        dev.disconnectFromPlc();
    }

    void GUI_05_aStalledGuiThreadMakesTheRunnerQueueInBoundedMemoryNotEvents() {
        MockRig mock(QStringLiteral("t071-stall-mock"));
        QVERIFY(mock.ok);
        DeviceHost dev(QStringLiteral("t071-stall-dev"), floodConfig(mock.port));
        TabTelemetry tele(QStringLiteral("PLC 1"), TabTelemetry::Kind::Device);
        tele.attachHost(&dev);
        tele.trace()->setCapacity(kTraceRows, kTraceBytes);
        QSignalSpy link(&dev, &DeviceHost::linkStateChanged);
        dev.setLogLevel(mc::LogLevel::Debug);
        dev.connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(sawLink(link, mc::LinkState::Connected), kWait);
        QTRY_VERIFY_WITH_TIMEOUT(tele.trace()->totalSeen() > 500, kWait);

        // Block the GUI thread: the runner keeps polling and collecting.
        QTest::qSleep(1500);

        // What waits for the GUI thread is one batch, not one per tick: 1500 ms at about 30 emits
        // a second would be about 45 queued batches without the acknowledgement.
        int batches = 0;
        int logBatches = 0;
        const auto countFrames = connect(&dev, &DeviceHost::framesBatch, this,
                                         [&batches](const QVector<FrameRecord>&) { ++batches; });
        const auto countLog = connect(&dev, &DeviceHost::logBatch, this,
                                      [&logBatches](const QVector<LogLine>&) { ++logBatches; });
        QCoreApplication::processEvents();
        disconnect(countFrames);
        disconnect(countLog);
        qInfo().noquote() << QStringLiteral("GUI-05 stall 1500 ms: %1 frame batches and %2 log batches were waiting "
                                            "for the GUI thread (about 45 without the acknowledgement)")
                                 .arg(batches)
                                 .arg(logBatches);
        QVERIFY2(batches <= 3, qPrintable(QStringLiteral("%1 frame batches queued up").arg(batches)));
        QVERIFY2(logBatches <= 3, qPrintable(QStringLiteral("%1 log batches queued up").arg(logBatches)));

        // The runner's own queues stayed within their caps while the GUI was away.
        std::atomic<int> transportChunks{-1};
        std::atomic<int> pending{-1};
        dev.post([&](DeviceRunner& r) {
            transportChunks = r.transportChunkCount();
            pending = r.pendingFrameCount();
        });
        QTRY_VERIFY_WITH_TIMEOUT(transportChunks.load() >= 0, kWait);
        QVERIFY(pending.load() <= DeviceRunner::kMaxPendingFrames);
        QVERIFY(transportChunks.load() < DeviceRunner::kMaxPendingFrames);

        // And it all resumes once the GUI thread is back: the chunks go on arriving.
        const quint64 mark = tele.trace()->totalSeen();
        QTRY_VERIFY_WITH_TIMEOUT(tele.trace()->totalSeen() > mark + 200, kWait);
        dev.disconnectFromPlc();
    }

    // The mock side has the same gate: its request log, trace and statistics wait for the GUI.
    void GUI_05_aStalledGuiThreadAlsoHoldsBackTheMockRunner() {
        MockRig mock(QStringLiteral("t071-mstall-mock"));
        QVERIFY(mock.ok);
        TabTelemetry mockTele(QStringLiteral("Mock 1"), TabTelemetry::Kind::Mock);
        mockTele.attachHost(&mock.host);
        DeviceHost dev(QStringLiteral("t071-mstall-dev"), floodConfig(mock.port));
        QSignalSpy link(&dev, &DeviceHost::linkStateChanged);
        dev.connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(sawLink(link, mc::LinkState::Connected), kWait);
        QTRY_VERIFY_WITH_TIMEOUT(mockTele.trace()->totalSeen() > 500, kWait);

        QTest::qSleep(1500);

        int frameBatches = 0;
        int requestBatches = 0;
        const auto countFrames = connect(&mock.host, &MockHost::framesBatch, this,
                                         [&frameBatches](const QVector<FrameRecord>&) { ++frameBatches; });
        const auto countRequests = connect(&mock.host, &MockHost::requestsLogged, this,
                                           [&requestBatches](const MockRequestBatch&) { ++requestBatches; });
        QCoreApplication::processEvents();
        disconnect(countFrames);
        disconnect(countRequests);
        qInfo().noquote() << QStringLiteral("GUI-05 mock stall 1500 ms: %1 frame batches and %2 request batches "
                                            "were waiting for the GUI thread")
                                 .arg(frameBatches)
                                 .arg(requestBatches);
        QVERIFY(frameBatches <= 3);
        QVERIFY(requestBatches <= 3);

        std::atomic<int> pending{-1};
        mock.host.post([&](MockRunner& r) { pending = r.pendingFrameCount(); });
        QTRY_VERIFY_WITH_TIMEOUT(pending.load() >= 0, kWait);
        QVERIFY(pending.load() <= MockRunner::kMaxPendingFrames);
        dev.disconnectFromPlc();
    }
};

namespace mc::workbench::test {
QObject* makeBackPressureSuite() {
    return new BackPressureTest;
}
} // namespace mc::workbench::test

#include "tst_gui_backpressure.moc"
