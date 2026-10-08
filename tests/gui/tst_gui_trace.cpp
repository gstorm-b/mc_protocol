// T-071 (SPEC-gui-tool.md, "Frame trace", "Debug log", "Capture and export"): the trace ring, the
// frame decoder, the debug log, the telemetry of a tab, the capture of a tab's traffic and its
// export. Every device and mock is a DeviceHost / MockHost on a runner thread; files are written
// under the build tree (MC_GUI_OUTPUT_DIR), never to tests/vectors/captured/.
#include "gui_suites.h"
#include "gui_test_support.h"

#include "mc/core/protocol.h"
#include "mc/mock/mock_plc.h"
#include "mc_workbench/capture_builder.h"
#include "mc_workbench/capture_controller.h"
#include "mc_workbench/capture_export.h"
#include "mc_workbench/capture_panel.h"
#include "mc_workbench/device_host.h"
#include "mc_workbench/device_runner.h"
#include "mc_workbench/device_tab.h"
#include "mc_workbench/frame_decoder.h"
#include "mc_workbench/log_model.h"
#include "mc_workbench/log_view.h"
#include "mc_workbench/main_window.h"
#include "mc_workbench/mock_host.h"
#include "mc_workbench/mock_tab.h"
#include "mc_workbench/tab_telemetry.h"
#include "mc_workbench/trace_model.h"
#include "mc_workbench/trace_pane.h"
#include "mc_workbench/trace_view.h"

#include <QClipboard>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTableView>
#include <QTest>

#include <atomic>

using namespace mc::workbench;
using namespace mc::workbench::test;

namespace {

FrameRecord chunk(qint64 tNs, bool tx, const QByteArray& bytes) {
    FrameRecord record;
    record.tNs = tNs;
    record.tx = tx;
    record.bytes = bytes;
    return record;
}

QVector<FrameRecord> chunks(int count, int size, qint64 firstNs = 0) {
    QVector<FrameRecord> out;
    for (int i = 0; i < count; ++i) {
        out.push_back(chunk(firstNs + i, i % 2 == 0, QByteArray(size, static_cast<char>(i & 0x7F))));
    }
    return out;
}

QString slurp(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

// A stand-in for the repository's tests/vectors/captured in the build tree: no test, and no mutant of the
// export rule, can ever write into the source tree (T-074 rework).
QString repoCapturedRoot() {
    QDir out(QStringLiteral(MC_GUI_OUTPUT_DIR));
    const QString relative = QStringLiteral("fake-repo/tests/vectors/captured");
    out.mkpath(relative);
    return out.absoluteFilePath(relative);
}

#ifdef Q_OS_WIN
// A junction (/J) or a directory symbolic link (/D) made with cmd, inside MC_GUI_OUTPUT_DIR only.
bool makeLink(const QString& link, const QString& target, bool junction) {
    QProcess p;
    p.start(QStringLiteral("cmd"), {QStringLiteral("/c"), QStringLiteral("mklink"),
                                     junction ? QStringLiteral("/J") : QStringLiteral("/D"),
                                     QDir::toNativeSeparators(link), QDir::toNativeSeparators(target)});
    return p.waitForFinished(10000) && p.exitCode() == 0;
}

// rmdir removes the link itself, never what it points at.
void removeLink(const QString& link) {
    QProcess p;
    p.start(QStringLiteral("cmd"), {QStringLiteral("/c"), QStringLiteral("rmdir"), QDir::toNativeSeparators(link)});
    p.waitForFinished(10000);
}
#endif

QByteArray encodeRequest(const mc::FrameConfig& frame, const mc::Request& request) {
    const mc::McProtocol protocol(frame);
    uint8_t buffer[512];
    const mc::Expected<size_t> size = protocol.encode(request, mc::MutableByteView{buffer, sizeof(buffer)});
    return size ? QByteArray(reinterpret_cast<const char*>(buffer),
                             static_cast<QByteArray::size_type>(size.value()))
                : QByteArray();
}

// What a mock for @p frame answers to @p request.
QByteArray answerOf(mc::MockPlc& mock, const QByteArray& request) {
    mock.bytesIn(mc::ByteView{reinterpret_cast<const uint8_t*>(request.constData()),
                              static_cast<size_t>(request.size())});
    QByteArray answer;
    mc::ByteView view;
    while (mock.nextResponse(view)) {
        answer.append(reinterpret_cast<const char*>(view.data),
                      static_cast<QByteArray::size_type>(view.size));
    }
    return answer;
}

// Collects the chunks a host delivers.
template <class Host> void collectFrames(Host& host, QVector<FrameRecord>* sink) {
    QObject::connect(&host, &Host::framesBatch, &host,
                     [sink](const QVector<FrameRecord>& frames) { sink->append(frames); });
}

} // namespace

class TraceCaptureTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { registerRunnerMetaTypes(); }

    // ---- the trace ring ------------------------------------------------------------------------

    void TraceModel_ringIsBoundedByRowsAndBytes() {
        TraceModel model;
        model.setCapacity(10, 1000);
        model.append(chunks(25, 10));
        QCOMPARE(model.rowCount(), 10);
        QCOMPARE(model.evicted(), quint64(15));
        QCOMPARE(model.totalSeen(), quint64(25));
        // The newest rows stay and keep their running numbers.
        QCOMPARE(model.index(0, TraceModel::ColSeq).data().toULongLong(), qulonglong(15));
        QCOMPARE(model.index(9, TraceModel::ColSeq).data().toULongLong(), qulonglong(24));

        // A byte limit counts too: 95 bytes hold nine chunks of ten.
        model.setCapacity(1000, 95);
        QCOMPARE(model.rowCount(), 9);
        QVERIFY(model.heldBytes() <= 95);
        model.append(chunks(4, 10, 100));
        QCOMPARE(model.rowCount(), 9);
        QCOMPARE(model.index(8, TraceModel::ColSeq).data().toULongLong(), qulonglong(28));

        // A batch bigger than the whole ring keeps only its newest rows.
        model.setCapacity(100, 1 << 20);
        model.clear();
        model.append(chunks(5000, 4));
        QCOMPARE(model.rowCount(), 100);
        QCOMPARE(model.index(0, TraceModel::ColSeq).data().toULongLong(), qulonglong(4900));
        QCOMPARE(model.evicted(), quint64(4900));

        // memoryBytes() follows what is held, not what has passed through.
        const qint64 held = model.memoryBytes();
        model.append(chunks(100000, 4));
        QCOMPARE(model.rowCount(), 100);
        QCOMPARE(model.memoryBytes(), held);

        // Shrinking the limit drops the oldest rows at once.
        model.setCapacity(10, 1 << 20);
        QCOMPARE(model.rowCount(), 10);
    }

    void TraceModel_columnsShowNanosecondsHexAndAscii() {
        TraceModel model;
        FrameRecord tx = chunk(123456789, true, QByteArray::fromHex("5000ff03414243"));
        tx.edge = FrameEdge::Complete;
        tx.note = QStringLiteral("ReadWords D100 x4");
        FrameRecord rx = chunk(123999999, false, QByteArray(100, 'z'));
        rx.edge = FrameEdge::Partial;
        model.append({tx, rx});

        QCOMPARE(model.index(0, TraceModel::ColTime).data().toLongLong(), qlonglong(123456789));
        QCOMPARE(model.index(0, TraceModel::ColDir).data().toString(), QStringLiteral("TX"));
        QCOMPARE(model.index(1, TraceModel::ColDir).data().toString(), QStringLiteral("RX"));
        QCOMPARE(model.index(0, TraceModel::ColLen).data().toInt(), 7);
        QCOMPARE(model.index(0, TraceModel::ColHex).data().toString(), QStringLiteral("50 00 FF 03 41 42 43"));
        QCOMPARE(model.index(0, TraceModel::ColAscii).data().toString(), QStringLiteral("P...ABC"));
        QCOMPARE(model.index(0, TraceModel::ColFrame).data().toString(), QStringLiteral("frame"));
        QCOMPARE(model.index(1, TraceModel::ColFrame).data().toString(), QStringLiteral("part"));
        QCOMPARE(model.index(0, TraceModel::ColNote).data().toString(), QStringLiteral("ReadWords D100 x4"));
        // A long chunk is cut in the cell and whole in the tooltip.
        QVERIFY(model.index(1, TraceModel::ColHex).data().toString().endsWith(QStringLiteral("...")));
        QVERIFY(model.index(1, TraceModel::ColHex).data(Qt::ToolTipRole).toString().size() >
                model.index(1, TraceModel::ColHex).data().toString().size());
        QCOMPARE(model.index(1, TraceModel::ColHex).data(TraceModel::RawBytesRole).toByteArray().size(), 100);
        QVERIFY(model.textOf(0, 1).contains(QStringLiteral("123456789\tTX\t7\t50 00 FF 03 41 42 43")));
    }

    void TraceModel_pauseCountsAndClearRestarts() {
        TraceModel model;
        model.append(chunks(3, 4));
        model.setPaused(true);
        model.append(chunks(7, 4));
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(model.skippedWhilePaused(), quint64(7));
        model.setPaused(false);
        model.append(chunks(2, 4));
        QCOMPARE(model.rowCount(), 5);
        model.clear();
        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(model.totalSeen(), quint64(0));
        QCOMPARE(model.heldBytes(), qint64(0));
    }

    // ---- the decoder ---------------------------------------------------------------------------

    void FrameDecoder_describesRequestsAndAnswers_data() {
        QTest::addColumn<int>("family"); // 0 = 3E binary, 1 = 3C format 1
        QTest::newRow("3E binary") << 0;
        QTest::newRow("3C format 1") << 1;
    }

    void FrameDecoder_describesRequestsAndAnswers() {
        QFETCH(int, family);
        const mc::FrameConfig frame =
            family == 0 ? mc::FrameConfig::frame3E() : mc::FrameConfig::frame3C();
        mc::MockPlc mock(frame);
        mock.setWord(mc::Device{mc::DeviceType::D, 100}, 1995);
        FrameDecoder decoder(frame);

        const QByteArray request = encodeRequest(frame, mc::Request::readWords(mc::Device{mc::DeviceType::D, 100}, 4));
        QVERIFY(!request.isEmpty());
        const QByteArray answer = answerOf(mock, request);
        QVERIFY(answer.size() > 4);

        FrameRecord tx = chunk(1, true, request);
        decoder.annotate(tx);
        QCOMPARE(tx.edge, FrameEdge::Complete);
        QCOMPARE(tx.note, QStringLiteral("ReadWords D100 x4"));

        // The answer in one chunk.
        FrameRecord rx = chunk(2, false, answer);
        decoder.annotate(rx);
        QCOMPARE(rx.edge, FrameEdge::Complete);
        QCOMPARE(rx.note, QStringLiteral("ok, 4 words"));

        // The same answer in two chunks: a fragment, then the end of the frame.
        FrameRecord tx2 = chunk(3, true, request);
        decoder.annotate(tx2);
        FrameRecord first = chunk(4, false, answer.left(answer.size() / 2));
        FrameRecord second = chunk(5, false, answer.mid(answer.size() / 2));
        decoder.annotate(first);
        decoder.annotate(second);
        QCOMPARE(first.edge, FrameEdge::Partial);
        QVERIFY(first.note.startsWith(QStringLiteral("partial answer")));
        QCOMPARE(second.edge, FrameEdge::Complete);
        QCOMPARE(second.note, QStringLiteral("ok, 4 words"));

        // A PLC error end code.
        mock.failRange(mc::DeviceType::D, 100, 103, 0xC051, 0);
        FrameRecord tx3 = chunk(6, true, request);
        decoder.annotate(tx3);
        FrameRecord bad = chunk(7, false, answerOf(mock, request));
        decoder.annotate(bad);
        QCOMPARE(bad.edge, FrameEdge::Complete);
        QVERIFY2(bad.note.startsWith(QStringLiteral("PLC error 0x")), qPrintable(bad.note));

        // Bytes that answer no request.
        FrameRecord stray = chunk(8, false, QByteArray(3, '\x01'));
        decoder.annotate(stray);
        QCOMPARE(stray.note, QStringLiteral("no request to match"));
    }

    // ---- the debug log -------------------------------------------------------------------------

    void LogModel_ringAndFilters() {
        LogModel log;
        log.setCapacity(50);
        const auto line = [](mc::LogLevel level, const QString& category, const QString& message) {
            LogLine l;
            l.tNs = 1000;
            l.level = level;
            l.category = category;
            l.message = message;
            return l;
        };
        log.append(QStringLiteral("PLC 1"), {line(mc::LogLevel::Debug, QStringLiteral("mc.session"), QStringLiteral("tx 21 bytes")),
                                             line(mc::LogLevel::Warn, QStringLiteral("mc.session"), QStringLiteral("retry")),
                                             line(mc::LogLevel::Error, QStringLiteral("workbench.link"), QStringLiteral("Faulted"))});
        log.append(QStringLiteral("Mock 1"), {line(mc::LogLevel::Info, QStringLiteral("mc.mock"), QStringLiteral("muted")),
                                              line(mc::LogLevel::Debug, QStringLiteral("mc.mock"), QStringLiteral("rx 21 bytes"))});
        QCOMPARE(log.rowCount(), 5);
        QCOMPARE(log.tabs(), (QStringList{QStringLiteral("Mock 1"), QStringLiteral("PLC 1")}));

        LogFilterModel filter;
        filter.setSourceModel(&log);
        QCOMPARE(filter.rowCount(), 5);
        filter.setMinLevel(mc::LogLevel::Warn);
        QCOMPARE(filter.rowCount(), 2);
        filter.setMinLevel(mc::LogLevel::Debug);
        filter.setTab(QStringLiteral("Mock 1"));
        QCOMPARE(filter.rowCount(), 2);
        filter.setTab(QString());
        filter.setSearch(QStringLiteral("BYTES"));
        QCOMPARE(filter.rowCount(), 2); // case-insensitive, message
        filter.setSearch(QStringLiteral("workbench"));
        QCOMPARE(filter.rowCount(), 1); // category
        filter.setSearch(QString());
        QVERIFY(filter.shownText().contains(QStringLiteral("Faulted")));

        // The ring: a thousand lines into fifty keep the newest fifty.
        QVector<LogLine> many;
        for (int i = 0; i < 1000; ++i) {
            many.push_back(line(mc::LogLevel::Info, QStringLiteral("x"), QString::number(i)));
        }
        log.append(QStringLiteral("PLC 1"), many);
        QCOMPARE(log.rowCount(), 50);
        QCOMPARE(log.index(49, LogModel::ColMessage).data().toString(), QStringLiteral("999"));
        QCOMPARE(log.index(0, LogModel::ColMessage).data().toString(), QStringLiteral("950"));
        QCOMPARE(log.evicted(), quint64(1005 - 50));
    }

    void LogView_filtersCopyAndSave() {
        LogModel log;
        LogView view(&log);
        view.resize(800, 300);
        view.show();
        LogLine a;
        a.level = mc::LogLevel::Debug;
        a.category = QStringLiteral("mc.session");
        a.message = QStringLiteral("alpha");
        LogLine b = a;
        b.level = mc::LogLevel::Error;
        b.message = QStringLiteral("beta");
        log.append(QStringLiteral("PLC 1"), {a, b});
        log.append(QStringLiteral("PLC 2"), {a});
        QCOMPARE(view.filter()->rowCount(), 3);

        view.setLevelFilter(QStringLiteral("Error"));
        QCOMPARE(view.filter()->rowCount(), 1);
        QVERIFY(view.copyText().contains(QStringLiteral("beta")));
        view.setLevelFilter(QStringLiteral("Trace"));
        view.setTabFilter(QStringLiteral("PLC 2"));
        QCOMPARE(view.filter()->rowCount(), 1);
        view.setTabFilter(QString());
        view.setSearchText(QStringLiteral("alp"));
        QCOMPARE(view.filter()->rowCount(), 2);

        view.copyToClipboard();
        QVERIFY(QGuiApplication::clipboard()->text().contains(QStringLiteral("alpha")));

        const QString out = freshOutputDir(QStringLiteral("t071-log-save"));
        const QString path = QDir(out).absoluteFilePath(QStringLiteral("log.txt"));
        QSignalSpy saved(&view, &LogView::saved);
        view.saveTo(path);
        QTRY_VERIFY_WITH_TIMEOUT(saved.count() == 1, kWait);
        QVERIFY(saved.first().at(1).toBool());
        const QString text = slurp(path);
        QVERIFY(text.contains(QStringLiteral("alpha")));
        QVERIFY(!text.contains(QStringLiteral("beta"))); // only the shown lines
    }

    void TraceView_copyAndSave() {
        TabTelemetry tele(QStringLiteral("PLC 1"), TabTelemetry::Kind::Device);
        FrameRecord tx = chunk(10, true, QByteArray::fromHex("0102"));
        tx.note = QStringLiteral("hello");
        tele.onFrames({tx, chunk(20, false, QByteArray::fromHex("0304"))});
        TraceView view;
        view.resize(900, 300);
        view.show();
        view.setTelemetry(&tele);
        QCOMPARE(view.table()->model()->rowCount(), 2);
        QVERIFY(view.copyText().contains(QStringLiteral("01 02")));
        view.copyToClipboard();
        QVERIFY(QGuiApplication::clipboard()->text().contains(QStringLiteral("03 04")));

        const QString out = freshOutputDir(QStringLiteral("t071-trace-save"));
        const QString path = QDir(out).absoluteFilePath(QStringLiteral("trace.txt"));
        QSignalSpy saved(&view, &TraceView::saved);
        view.saveTo(path);
        QTRY_VERIFY_WITH_TIMEOUT(saved.count() == 1, kWait);
        QVERIFY(saved.first().at(1).toBool());
        QVERIFY(slurp(path).contains(QStringLiteral("hello")));

        // Without a telemetry the view is empty and says so.
        view.setTelemetry(nullptr);
        QVERIFY(view.statusText().contains(QStringLiteral("No tab")));
    }

    // ---- a tab's trace, fed by its runner ------------------------------------------------------

    void Telemetry_deviceTraceIsFedByBatchesAndDecoded() {
        MockRig mock(QStringLiteral("t071-mock"));
        QVERIFY(mock.ok);
        DeviceHost dev(QStringLiteral("t071-dev"), deviceConfig(mock.port, 5));
        TabTelemetry tele(QStringLiteral("PLC 1"), TabTelemetry::Kind::Device);
        tele.attachHost(&dev);
        QSignalSpy link(&dev, &DeviceHost::linkStateChanged);
        QSignalSpy logs(&tele, &TabTelemetry::logLines);

        dev.setLogLevel(mc::LogLevel::Debug);
        dev.connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(sawLink(link, mc::LinkState::Connected), kWait);
        TraceModel* trace = tele.trace();
        QTRY_VERIFY_WITH_TIMEOUT(trace->rowCount() >= 8, kWait);

        // Every chunk is decoded: requests are TX, answers RX, each ends a frame.
        bool sawRequest = false;
        bool sawAnswer = false;
        qint64 lastNs = -1;
        for (int row = 0; row < trace->rowCount(); ++row) {
            const QString dir = trace->index(row, TraceModel::ColDir).data().toString();
            const QString note = trace->index(row, TraceModel::ColNote).data().toString();
            const qint64 ns = trace->index(row, TraceModel::ColTime).data().toLongLong();
            QVERIFY2(ns >= lastNs, "the stamps of one link never go back");
            lastNs = ns;
            if (dir == QStringLiteral("TX") && note.startsWith(QStringLiteral("Read"))) {
                sawRequest = true;
            }
            if (dir == QStringLiteral("RX") && note.startsWith(QStringLiteral("ok"))) {
                sawAnswer = true;
            }
            QVERIFY2(!note.isEmpty(), qPrintable(QStringLiteral("row %1 is not decoded").arg(row)));
        }
        QVERIFY(sawRequest);
        QVERIFY(sawAnswer);
        QVERIFY(!trace->bytesAt(0).isEmpty());

        // The log of the session arrives as value copies, tagged with the tab.
        QTRY_VERIFY_WITH_TIMEOUT(logs.count() > 0, kWait);
        QCOMPARE(logs.first().at(0).toString(), QStringLiteral("PLC 1"));

        // The runner takes the recording off the transport: it never holds more than a tick's worth.
        std::atomic<int> held{-1};
        dev.post([&held](DeviceRunner& r) { held = r.transportChunkCount(); });
        QTRY_VERIFY_WITH_TIMEOUT(held.load() >= 0, kWait);
        QVERIFY2(held.load() < 500, "RecordingTransport keeps its chunks: the runner must take them");

        // Decode off: the chunks that arrive afterwards carry no text.
        tele.setDecode(false);
        // (the chunks already taken or in flight were decoded: wait for the first one that was not)
        const auto lastIsPlain = [&]() {
            const int last = trace->rowCount() - 1;
            return last >= 0 && trace->index(last, TraceModel::ColNote).data().toString().isEmpty() &&
                   trace->index(last, TraceModel::ColFrame).data().toString().isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(lastIsPlain(), kWait);
        tele.setDecode(true);
    }

    // The builder alone: which exchanges become api records, which stay literal, how the ends of a
    // recording are treated.
    void CaptureBuilder_pairsExchangesAndTreatsTheEndsOfARecording() {
        const mc::FrameConfig frame = mc::FrameConfig::frame3E();
        mc::MockPlc mock(frame);
        mock.setWord(mc::Device{mc::DeviceType::D, 100}, 1995);
        const mc::Device d100{mc::DeviceType::D, 100};
        const QByteArray readD = encodeRequest(frame, mc::Request::readWords(d100, 4));
        const QByteArray readM = encodeRequest(frame, mc::Request::readBits(mc::Device{mc::DeviceType::M, 0}, 16));
        const uint8_t words[] = {1, 0, 2, 0};
        const QByteArray writeD = encodeRequest(
            frame, mc::Request::writeWords(mc::Device{mc::DeviceType::D, 200}, mc::ByteView{words, 4}));
        const QByteArray answerD = answerOf(mock, readD);
        const QByteArray answerM = answerOf(mock, readM);
        const QByteArray answerW = answerOf(mock, writeD);
        mock.failRange(mc::DeviceType::W, 0, 10, 0xC051, 0);
        const QByteArray readW = encodeRequest(frame, mc::Request::readWords(mc::Device{mc::DeviceType::W, 0}, 2));
        const QByteArray answerErr = answerOf(mock, readW);

        QVector<FrameRecord> in;
        in.push_back(chunk(5, false, QByteArray(3, 'x')));          // stray rx before any request
        in.push_back(chunk(10, true, readD));                       // T1 ok, 1.0 ms
        in.push_back(chunk(1000010, false, answerD));
        in.push_back(chunk(2000000, true, readM));                  // T2 no answer, the next request came: timeout
        in.push_back(chunk(3000000, true, writeD));                 // T3 write: literal
        in.push_back(chunk(3500000, false, answerW));
        in.push_back(chunk(4000000, true, readW));                  // T4 PLC error
        in.push_back(chunk(4500000, false, answerErr));
        in.push_back(chunk(5000000, true, readD));                  // T5 cut in the middle of its answer
        in.push_back(chunk(5500000, false, answerD.left(5)));
        in.push_back(chunk(6000000, true, readM));                  // T6 the recording ends: left out
        CaptureSettings settings;
        settings.profileId = QStringLiteral("unit");
        settings.source = CaptureSource::MockPlc;
        mc::McDeviceConfig cfg;
        cfg.frame = frame;
        const BuiltCapture built = buildCapture(in, settings, cfg, false);

        QCOMPARE(built.steps.size(), 5);
        QCOMPARE(built.steps[0].via, QStringLiteral("api"));
        QCOMPARE(built.steps[0].op, QStringLiteral("ReadWords"));
        QCOMPARE(built.steps[0].device, QStringLiteral("D100"));
        QCOMPARE(built.steps[0].count, 4);
        QCOMPARE(built.steps[0].outcome, QStringLiteral("ok"));
        QCOMPARE(built.steps[0].source, QStringLiteral("mock"));
        QVERIFY(qAbs(built.steps[0].rttMs - 1.0) < 0.001);
        QCOMPARE(built.steps[1].op, QStringLiteral("ReadBits"));
        QCOMPARE(built.steps[1].outcome, QStringLiteral("timeout"));
        QVERIFY(built.steps[1].response.isEmpty());
        QCOMPARE(built.steps[2].via, QStringLiteral("raw")); // a write is kept as the literal frame
        QCOMPARE(built.steps[2].op, QStringLiteral("Raw"));
        QCOMPARE(built.steps[2].outcome, QStringLiteral("ok"));
        QVERIFY(built.steps[3].outcome.startsWith(QStringLiteral("plcError")));
        QVERIFY2(built.steps[3].outcome.contains(QStringLiteral("C051")), qPrintable(built.steps[3].outcome));
        QVERIFY(built.steps[4].partial);
        QCOMPARE(built.apiRecords, 4);
        QCOMPARE(built.rawRecords, 1);

        // device_end is the highest point accessed successfully: D200..D201 (the write) and D103;
        // the failed read of W does not count.
        QCOMPARE(built.meta.profile.end(mc::DeviceType::D).value_or(0), quint32(201));
        QVERIFY(!built.meta.profile.end(mc::DeviceType::W).has_value());
        QCOMPARE(built.meta.profile.end(mc::DeviceType::M).value_or(99), quint32(99)); // only a timeout

        QString extras;
        for (const auto& pair : built.meta.extra) {
            extras += pair.first + QLatin1Char('=') + pair.second + QLatin1Char(' ');
        }
        QVERIFY2(extras.contains(QStringLiteral("stray_rx=1")), qPrintable(extras));
        QVERIFY2(extras.contains(QStringLiteral("unanswered_last_request")), qPrintable(extras));
        QVERIFY(!extras.contains(QStringLiteral("truncated")));
        QVERIFY(buildCapture(in, settings, cfg, true).meta.extra.last().first == QStringLiteral("truncated"));
    }

    // ---- where a capture may go ----------------------------------------------------------------

    void ExportPolicy_onlyARealPlcMayGoUnderTheCapturedRoot() {
        const QString captured = repoCapturedRoot();
        QVERIFY(!checkOutputTarget(captured, CaptureSource::MockPlc, captured).allowed);
        QVERIFY(!checkOutputTarget(captured, CaptureSource::VirtualPlc, captured).allowed);
        QVERIFY(checkOutputTarget(captured, CaptureSource::RealPlc, captured).allowed);

        // Another spelling of the same folder: `..`, upper case, a trailing slash, a sub-folder.
        QVERIFY(!checkOutputTarget(captured + QStringLiteral("/../captured/"), CaptureSource::MockPlc, captured).allowed);
        QVERIFY(!checkOutputTarget(captured.toUpper(), CaptureSource::MockPlc, captured).allowed);
        QVERIFY(!checkOutputTarget(captured + QStringLiteral("/sub/deeper"), CaptureSource::MockPlc, captured).allowed);

        // The reason names the folder.
        const ExportDecision refused = checkOutputTarget(captured, CaptureSource::MockPlc, captured);
        QVERIFY(refused.reason.contains(QStringLiteral("tests/vectors/captured")));
        QVERIFY(refused.reason.contains(QStringLiteral("mock")));

        // An unknown repository root: the path itself still gives it away.
        QVERIFY(!checkOutputTarget(captured, CaptureSource::MockPlc, QString()).allowed);
        QVERIFY(!checkOutputTarget(QStringLiteral("D:/elsewhere/tests/vectors/captured"), CaptureSource::MockPlc, QString()).allowed);

        // A user folder takes every source; the folder above the protected one is not protected.
        const QString user = freshOutputDir(QStringLiteral("t071-policy"));
        QVERIFY(checkOutputTarget(user, CaptureSource::MockPlc, captured).allowed);
        QVERIFY(checkOutputTarget(user, CaptureSource::VirtualPlc, captured).allowed);
        QVERIFY(checkOutputTarget(QFileInfo(captured).absolutePath(), CaptureSource::MockPlc, captured).allowed);
        QVERIFY(!checkOutputTarget(QString(), CaptureSource::RealPlc, captured).allowed);

        QVERIFY(isLoopbackHost(QStringLiteral("127.0.0.1")));
        QVERIFY(isLoopbackHost(QStringLiteral("localhost")));
        QVERIFY(isLoopbackHost(QStringLiteral("127.5.5.5")));
        QVERIFY(isLoopbackHost(QStringLiteral("::1")));
        QVERIFY(isLoopbackHost(QStringLiteral("localhost.")));
        QVERIFY(isLoopbackHost(QStringLiteral("LocalHost.")));
        QVERIFY(isLoopbackHost(QStringLiteral("[::1]")));
        QVERIFY(isLoopbackHost(QStringLiteral(" [::1] ")));
        QVERIFY(!isLoopbackHost(QStringLiteral("[::2]")));
        QVERIFY(!isLoopbackHost(QStringLiteral("notlocalhost.")));
        QVERIFY(!isLoopbackHost(QStringLiteral("192.168.1.10")));
        QVERIFY(!isLoopbackHost(QStringLiteral("plc.example.org")));
    }

    // T-074 rework 2: the rule asks the operating system and refuses when unsure. Every case below
    // lives in a repository stand-in under MC_GUI_OUTPUT_DIR.
    void ExportPolicy_namesWindowsWouldChangeOrRefuseAreRefused() {
        const QString base = freshOutputDir(QStringLiteral("t074-names"));
        const QString repo = QDir(base).absoluteFilePath(QStringLiteral("repo"));
        const QString vectors = repo + QStringLiteral("/tests/vectors");
        const QString captured = vectors + QStringLiteral("/captured");
        QVERIFY(QDir().mkpath(captured));
        const auto mockRefused = [&](const QString& folder, const QString& root) {
            return !checkOutputTarget(folder, CaptureSource::MockPlc, root).allowed;
        };
        for (const QString& root : {captured, QString()}) {
            QVERIFY(mockRefused(vectors + QStringLiteral("/captured."), root));
            QVERIFY(mockRefused(vectors + QStringLiteral("/captured.. "), root));
            QVERIFY(mockRefused(vectors + QStringLiteral("/captured ."), root));
            QVERIFY(mockRefused(repo + QStringLiteral("/tests/vectors./captured"), root));
            QVERIFY(mockRefused(repo + QStringLiteral("/tests/vectors./captured./deeper"), root));
            QVERIFY(mockRefused(repo + QStringLiteral("/tests/x/../vectors/Captured"), root));
        }
        // Names that have no plain meaning are refused for every source, with a reason.
        for (const char* name : {"a:b", "a~1", "a*", "a?", "a|b", "a<b", "con", "NUL.txt", "Com1", "lpt9", "AUX", "...",
                                 "  "}) {
            const QString folder = base + QStringLiteral("/new/") + QLatin1String(name);
            for (const CaptureSource source : {CaptureSource::MockPlc, CaptureSource::VirtualPlc, CaptureSource::RealPlc}) {
                const ExportDecision d = checkOutputTarget(folder, source, captured);
                QVERIFY2(!d.allowed, name);
                QVERIFY2(!d.reason.isEmpty(), name);
            }
        }
        // A plain new name is fine, also below an existing folder with a dot in its name.
        QVERIFY(checkOutputTarget(base + QStringLiteral("/ok-new/sub"), CaptureSource::MockPlc, captured).allowed);
        QVERIFY(checkOutputTarget(base + QStringLiteral("/COM10x/sub"), CaptureSource::MockPlc, captured).allowed);
        // Device and extended-length forms are refused outright.
        QVERIFY(!checkOutputTarget(QStringLiteral("\\\\?\\") + QDir::toNativeSeparators(base), CaptureSource::MockPlc, captured).allowed);
        QVERIFY(!checkOutputTarget(QStringLiteral("\\\\.\\") + QDir::toNativeSeparators(base), CaptureSource::MockPlc, captured).allowed);
        // A folder that cannot be resolved is refused (never allowed), whatever the source.
        QString unused;
        for (char letter = 'Z'; letter >= 'D'; --letter) {
            const QString drive = QString(QLatin1Char(letter)) + QStringLiteral(":/");
            if (!QDir(drive).exists()) {
                unused = drive;
                break;
            }
        }
        if (unused.isEmpty()) {
            QSKIP("every drive letter is in use here");
        }
        for (const CaptureSource source : {CaptureSource::MockPlc, CaptureSource::RealPlc}) {
            const ExportDecision d = checkOutputTarget(unused + QStringLiteral("x/y"), source, captured);
            QVERIFY(!d.allowed);
            QVERIFY2(d.reason.contains(QStringLiteral("cannot be checked")), qPrintable(d.reason));
        }
        // A protected root that cannot be resolved refuses too.
        const ExportDecision noRoot =
            checkOutputTarget(base + QStringLiteral("/ok-new"), CaptureSource::MockPlc, unused + QStringLiteral("repo/tests/vectors/captured"));
        QVERIFY(!noRoot.allowed);

        // The inputs: an id or a root ending in a dot or a space is refused at once.
        QVERIFY(!checkOutputInputs(base, QStringLiteral("captured.")).isEmpty());
        QVERIFY(!checkOutputInputs(base, QStringLiteral("id ")).isEmpty());
        QVERIFY(!checkOutputInputs(base + QStringLiteral("/vectors."), QStringLiteral("id")).isEmpty());
        QVERIFY(!checkOutputInputs(base + QStringLiteral("/vectors. /"), QStringLiteral("id")).isEmpty());
        QVERIFY(checkOutputInputs(base + QStringLiteral("/vectors/"), QStringLiteral("id")).isEmpty());
        QVERIFY(checkOutputInputs(base + QStringLiteral("/vectors/."), QStringLiteral("id")).isEmpty());
        QVERIFY(checkOutputInputs(base + QStringLiteral("/a/.."), QStringLiteral("id")).isEmpty());
    }

    void ExportPolicy_aJunctionOrSymlinkAsTheRootIsResolved() {
#ifdef Q_OS_WIN
        const QString base = freshOutputDir(QStringLiteral("t074-links"));
        const QString repo = QDir(base).absoluteFilePath(QStringLiteral("repo"));
        const QString vectors = repo + QStringLiteral("/tests/vectors");
        const QString captured = vectors + QStringLiteral("/captured");
        QVERIFY(QDir().mkpath(captured));
        bool madeAny = false;
        for (const bool junction : {true, false}) {
            const QString link = base + (junction ? QStringLiteral("/junction") : QStringLiteral("/symlink"));
            if (!makeLink(link, vectors, junction)) {
                qInfo("%s not available here (needs a privilege or a file system that has it)",
                      junction ? "junction" : "symbolic link");
                continue;
            }
            madeAny = true;
            // link -> tests/vectors: link/captured IS the protected folder, with or without the known root.
            for (const QString& root : {captured, QString()}) {
                QVERIFY(!checkOutputTarget(link + QStringLiteral("/captured"), CaptureSource::MockPlc, root).allowed);
                QVERIFY(!checkOutputTarget(link + QStringLiteral("/Captured"), CaptureSource::VirtualPlc, root).allowed);
                QVERIFY(!checkOutputTarget(link + QStringLiteral("/captured/sub"), CaptureSource::MockPlc, root).allowed);
                QVERIFY(!checkOutputTarget(link + QStringLiteral("/captured."), CaptureSource::MockPlc, root).allowed);
            }
            // The same link is fine for a real PLC capture and for a folder beside `captured`.
            QVERIFY(checkOutputTarget(link + QStringLiteral("/captured"), CaptureSource::RealPlc, captured).allowed);
            QVERIFY(checkOutputTarget(link + QStringLiteral("/other"), CaptureSource::MockPlc, captured).allowed);
            removeLink(link);
            QVERIFY(!QFileInfo::exists(link));
        }
        if (!madeAny) {
            QSKIP("neither a junction nor a symbolic link can be created here");
        }
        QVERIFY(QFileInfo(captured).isDir()); // the targets were never touched
#else
        QSKIP("links are exercised on Windows only (v1)");
#endif
    }

    void ExportPolicy_anEightDotThreeNameIsResolved() {
#ifdef Q_OS_WIN
        const QString base = freshOutputDir(QStringLiteral("t074-short"));
        const QString repo = QDir(base).absoluteFilePath(QStringLiteral("longrepositoryname"));
        const QString captured = repo + QStringLiteral("/tests/vectors/captured");
        QVERIFY(QDir().mkpath(captured));
        const QString native = QDir::toNativeSeparators(repo);
        QVector<wchar_t> buffer(1024);
        const DWORD n = GetShortPathNameW(reinterpret_cast<LPCWSTR>(native.utf16()), buffer.data(),
                                          static_cast<DWORD>(buffer.size()));
        const QString shortRepo = QDir::fromNativeSeparators(QString::fromWCharArray(buffer.data(), static_cast<int>(n)));
        if (n == 0 || shortRepo.compare(repo, Qt::CaseInsensitive) == 0 || !shortRepo.contains(QLatin1Char('~'))) {
            QSKIP("short (8.3) names are not enabled on this volume");
        }
        const QString viaShort = shortRepo + QStringLiteral("/tests/vectors/captured");
        QVERIFY(!checkOutputTarget(viaShort, CaptureSource::MockPlc, captured).allowed);
        QVERIFY(!checkOutputTarget(viaShort + QStringLiteral("/id"), CaptureSource::MockPlc, captured).allowed);
        QVERIFY(!checkOutputTarget(viaShort + QStringLiteral("/id"), CaptureSource::MockPlc, QString()).allowed);
        QVERIFY(!checkOutputTarget(viaShort, CaptureSource::MockPlc, shortRepo + QStringLiteral("/tests/vectors/captured")).allowed);
        QVERIFY(checkOutputTarget(shortRepo + QStringLiteral("/tests/other"), CaptureSource::MockPlc, captured).allowed);
#else
        QSKIP("8.3 names exist on Windows only");
#endif
    }

    // ---- capture: a device tab against a mock replays green ------------------------------------

    void Capture_deviceTabAgainstMockReplaysGreen() {
        MockRig mock(QStringLiteral("t071-cap-mock"));
        QVERIFY(mock.ok);
        DeviceHost dev(QStringLiteral("t071-cap-dev"), deviceConfig(mock.port, 10));
        TabTelemetry tele(QStringLiteral("PLC 1"), TabTelemetry::Kind::Device);
        tele.attachHost(&dev);
        QSignalSpy done(&dev, &DeviceHost::commandDone);
        QSignalSpy link(&dev, &DeviceHost::linkStateChanged);
        QSignalSpy saved(&tele, &TabTelemetry::captureSaved);

        CaptureSettings settings;
        settings.profileId = QStringLiteral("gui-t071-dev");
        settings.source = CaptureSource::MockPlc;
        settings.note = QStringLiteral("T-071 test");
        QVERIFY(resultOf(done, tele.startCapture(settings)).ok);
        QTRY_VERIFY_WITH_TIMEOUT(tele.captureStatus().active, kWait);

        dev.connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(sawLink(link, mc::LinkState::Connected), kWait);
        QVERIFY(resultOf(done, dev.writeWords(QStringLiteral("D200"), {7, 8, 9})).ok);
        QVERIFY(resultOf(done, dev.readWords(QStringLiteral("D200"), 3)).ok);
        QVERIFY(resultOf(done, dev.writeBits(QStringLiteral("M100"), {true, false, true})).ok);
        QVERIFY(resultOf(done, dev.readBits(QStringLiteral("M100"), 3)).ok);
        QTest::qWait(300); // a few polling rounds

        QVERIFY(resultOf(done, tele.stopCapture()).ok);
        QTRY_VERIFY_WITH_TIMEOUT(tele.captureStatus().hasData && !tele.captureStatus().active, kWait);
        QVERIFY(tele.captureStatus().chunks >= 12);
        dev.disconnectFromPlc();

        const QString out = freshOutputDir(QStringLiteral("t071-device-capture"));
        CaptureSaveRequest request;
        request.outputRoot = out;
        request.capturedRoot = repoCapturedRoot();
        const quint64 token = tele.saveCapture(request);
        QTRY_VERIFY_WITH_TIMEOUT(saved.count() == 1, kWait);
        const auto result = saved.first().at(0).value<CaptureSaveResult>();
        QVERIFY2(result.ok, qPrintable(result.message));
        QCOMPARE(result.token, token);
        QVERIFY(result.records >= 6);

        const QString folder = QDir(out).absoluteFilePath(QStringLiteral("gui-t071-dev"));
        const QString steps = slurp(QDir(folder).filePath(QStringLiteral("steps.vec")));
        const QString meta = slurp(QDir(folder).filePath(QStringLiteral("run.meta")));
        QVERIFY(QFileInfo::exists(QDir(folder).filePath(QStringLiteral("session.vec"))));
        QVERIFY(steps.contains(QStringLiteral("# source: mock")));
        QVERIFY(!steps.contains(QStringLiteral("# source: plc")));
        QVERIFY(steps.contains(QStringLiteral("via: api")));
        QVERIFY(steps.contains(QStringLiteral("op: ReadWords")));
        QVERIFY(steps.contains(QStringLiteral("via: raw"))); // the writes
        QVERIFY(meta.contains(QStringLiteral("profile: gui-t071-dev")));
        QVERIFY(meta.contains(QStringLiteral("capture_source: mock")));
        QVERIFY(meta.contains(QStringLiteral("not hardware")));
        QVERIFY2(!meta.contains(QStringLiteral("127.0.0.1")) && !meta.contains(QString::number(mock.port)),
                 "run.meta must not hold the address or the port");

        int exitCode = 0;
        const QString output = runReplay(out, &exitCode);
        QVERIFY2(exitCode == 0, qPrintable(output));
        QVERIFY2(output.contains(QStringLiteral("replayed gui-t071-dev")), qPrintable(output));
        QVERIFY2(output.contains(QStringLiteral(" 0 failures")), qPrintable(output));
    }

    void Capture_mockTabTrafficReplaysGreen() {
        MockRig mock(QStringLiteral("t071-capmock"));
        QVERIFY(mock.ok);
        TabTelemetry tele(QStringLiteral("Mock 1"), TabTelemetry::Kind::Mock);
        tele.attachHost(&mock.host);
        QSignalSpy saved(&tele, &TabTelemetry::captureSaved);
        QSignalSpy mockDone(&mock.host, &MockHost::commandDone);

        CaptureSettings settings;
        settings.profileId = QStringLiteral("gui-t071-mock");
        settings.source = CaptureSource::MockPlc;
        QVERIFY(resultOf(mockDone, tele.startCapture(settings)).ok);

        DeviceHost dev(QStringLiteral("t071-capmock-dev"), deviceConfig(mock.port, 10));
        QSignalSpy link(&dev, &DeviceHost::linkStateChanged);
        QSignalSpy done(&dev, &DeviceHost::commandDone);
        dev.connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(sawLink(link, mc::LinkState::Connected), kWait);
        QVERIFY(resultOf(done, dev.writeWords(QStringLiteral("D300"), {1, 2})).ok);
        QVERIFY(resultOf(done, dev.readWords(QStringLiteral("D300"), 2)).ok);
        QTest::qWait(200);

        // The mock tab's trace holds what the mock saw: requests as TX, its answers as RX.
        QTRY_VERIFY_WITH_TIMEOUT(tele.trace()->rowCount() >= 8, kWait);
        QCOMPARE(tele.trace()->index(0, TraceModel::ColDir).data().toString(), QStringLiteral("TX"));

        QVERIFY(resultOf(mockDone, tele.stopCapture()).ok);
        QTRY_VERIFY_WITH_TIMEOUT(tele.captureStatus().hasData && !tele.captureStatus().active, kWait);
        dev.disconnectFromPlc();

        const QString out = freshOutputDir(QStringLiteral("t071-mock-capture"));
        CaptureSaveRequest request;
        request.outputRoot = out;
        request.capturedRoot = repoCapturedRoot();
        tele.saveCapture(request);
        QTRY_VERIFY_WITH_TIMEOUT(saved.count() == 1, kWait);
        const auto result = saved.first().at(0).value<CaptureSaveResult>();
        QVERIFY2(result.ok, qPrintable(result.message));

        int exitCode = 0;
        const QString output = runReplay(out, &exitCode);
        QVERIFY2(exitCode == 0, qPrintable(output));
        QVERIFY2(output.contains(QStringLiteral("replayed gui-t071-mock")), qPrintable(output));
    }

    void Capture_refusals() {
        // Output of an earlier failed or mutated run must not make this case fail.
        QDir(QDir(QStringLiteral(MC_GUI_OUTPUT_DIR)).absoluteFilePath(QStringLiteral("fake-repo"))).removeRecursively();
        MockRig mock(QStringLiteral("t071-ref-mock"));
        QVERIFY(mock.ok);
        DeviceHost dev(QStringLiteral("t071-ref-dev"), deviceConfig(mock.port, 10));
        TabTelemetry tele(QStringLiteral("PLC 1"), TabTelemetry::Kind::Device);
        tele.attachHost(&dev);
        QSignalSpy done(&dev, &DeviceHost::commandDone);
        QSignalSpy link(&dev, &DeviceHost::linkStateChanged);
        QSignalSpy saved(&tele, &TabTelemetry::captureSaved);

        // A loopback address is no real PLC: that source is refused at the start.
        CaptureSettings settings;
        settings.profileId = QStringLiteral("gui-t071-ref");
        settings.source = CaptureSource::RealPlc;
        const CommandResult notReal = resultOf(done, tele.startCapture(settings));
        QVERIFY(!notReal.ok);
        QVERIFY2(notReal.message.contains(QStringLiteral("real PLC")), qPrintable(notReal.message));
        // A name that is no folder is refused too.
        settings.source = CaptureSource::MockPlc;
        settings.profileId = QStringLiteral("../escape");
        QVERIFY(!resultOf(done, tele.startCapture(settings)).ok);

        // Nothing recorded: nothing to save.
        const QString out = freshOutputDir(QStringLiteral("t071-refusals"));
        CaptureSaveRequest request;
        request.outputRoot = out;
        request.capturedRoot = repoCapturedRoot();
        tele.saveCapture(request);
        QTRY_VERIFY_WITH_TIMEOUT(saved.count() == 1, kWait);
        QVERIFY(!saved.last().at(0).value<CaptureSaveResult>().ok);
        QVERIFY(saved.last().at(0).value<CaptureSaveResult>().message.contains(QStringLiteral("nothing is captured")));

        // Record a mock capture.
        settings.profileId = QStringLiteral("gui-t071-ref");
        QVERIFY(resultOf(done, tele.startCapture(settings)).ok);
        dev.connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(sawLink(link, mc::LinkState::Connected), kWait);
        QTRY_VERIFY_WITH_TIMEOUT(tele.captureStatus().chunks >= 6, kWait);

        // Saving while recording is refused.
        tele.saveCapture(request);
        QTRY_VERIFY_WITH_TIMEOUT(saved.count() == 2, kWait);
        QVERIFY(saved.last().at(0).value<CaptureSaveResult>().message.contains(QStringLiteral("stop the capture")));

        QVERIFY(resultOf(done, tele.stopCapture()).ok);
        QTRY_VERIFY_WITH_TIMEOUT(tele.captureStatus().hasData && !tele.captureStatus().active, kWait);

        // The export of a mock capture under tests/vectors/captured/ is refused, in every spelling,
        // and nothing is written there.
        const QString captured = repoCapturedRoot();
        const QString wouldBe = QDir(captured).absoluteFilePath(QStringLiteral("gui-t071-ref"));
        const QStringList targets = {captured, captured + QStringLiteral("/../captured"),
                                     captured.toUpper()};
        int expected = 2;
        for (const QString& target : targets) {
            CaptureSaveRequest bad = request;
            bad.outputRoot = target;
            bad.overwrite = true;
            tele.saveCapture(bad);
            ++expected;
        QTRY_VERIFY_WITH_TIMEOUT(saved.count() == expected, kWait);
            const auto refused = saved.last().at(0).value<CaptureSaveResult>();
            QVERIFY2(!refused.ok, qPrintable(target));
            QVERIFY2(refused.message.contains(QStringLiteral("tests/vectors/captured")), qPrintable(refused.message));
            QVERIFY(refused.files.isEmpty());
            QVERIFY2(!QFileInfo::exists(wouldBe), "a refused export must write nothing");
        }
        // The path gives it away even when the panel does not know the repository.
        CaptureSaveRequest unknown = request;
        unknown.outputRoot = QDir(out).absoluteFilePath(QStringLiteral("vectors/captured"));
        unknown.capturedRoot.clear();
        tele.saveCapture(unknown);
        ++expected;
        QTRY_VERIFY_WITH_TIMEOUT(saved.count() == expected, kWait);
        QVERIFY(!saved.last().at(0).value<CaptureSaveResult>().ok);
        QVERIFY(!QFileInfo::exists(QDir(unknown.outputRoot).filePath(QStringLiteral("gui-t071-ref"))));

        // A user folder works; a second save does not replace without the flag.
        tele.saveCapture(request);
        ++expected;
        QTRY_VERIFY_WITH_TIMEOUT(saved.count() == expected, kWait);
        QVERIFY2(saved.last().at(0).value<CaptureSaveResult>().ok,
                 qPrintable(saved.last().at(0).value<CaptureSaveResult>().message));
        tele.saveCapture(request);
        ++expected;
        QTRY_VERIFY_WITH_TIMEOUT(saved.count() == expected, kWait);
        QVERIFY(saved.last().at(0).value<CaptureSaveResult>().message.contains(QStringLiteral("already exists")));
        request.overwrite = true;
        tele.saveCapture(request);
        ++expected;
        QTRY_VERIFY_WITH_TIMEOUT(saved.count() == expected, kWait);
        QVERIFY(saved.last().at(0).value<CaptureSaveResult>().ok);

        // The recording is bounded: a small limit stops it and marks it full, still saveable.
        QVERIFY(resultOf(done, tele.discardCapture()).ok);
        settings.maxChunks = 10;
        QVERIFY(resultOf(done, tele.startCapture(settings)).ok);
        QTRY_VERIFY_WITH_TIMEOUT(tele.captureStatus().full, kWait);
        QVERIFY(!tele.captureStatus().active);
        QVERIFY(tele.captureStatus().chunks <= 10);
        QVERIFY(tele.captureStatus().hasData);
        dev.disconnectFromPlc();
    }

    // A capture of a real PLC may be exported under the captured root: the controller with a
    // target that is not local (a documentation address), files under a fake root of the build tree.
    void Capture_realPlcMayBeExportedUnderTheCapturedRoot() {
        MockRig mock(QStringLiteral("t071-real-mock"));
        QVERIFY(mock.ok);
        DeviceHost dev(QStringLiteral("t071-real-dev"), deviceConfig(mock.port, 10));
        QVector<FrameRecord> seen;
        collectFrames(dev, &seen);
        QSignalSpy link(&dev, &DeviceHost::linkStateChanged);
        dev.connectToPlc();
        QTRY_VERIFY_WITH_TIMEOUT(sawLink(link, mc::LinkState::Connected), kWait);
        QTRY_VERIFY_WITH_TIMEOUT(seen.size() >= 12, kWait);
        dev.disconnectFromPlc();
        QTRY_VERIFY_WITH_TIMEOUT(sawLink(link, mc::LinkState::Disconnected), kWait);

        mc::McDeviceConfig cfg = deviceConfig(1280);
        cfg.tcp.host = QStringLiteral("192.0.2.10");
        const QString out = freshOutputDir(QStringLiteral("t071-real-export"));
        const QString fakeRoot = QDir(out).absoluteFilePath(QStringLiteral("vectors/captured"));

        CaptureController real;
        real.setTarget(cfg, false);
        CaptureSettings settings;
        settings.profileId = QStringLiteral("gui-t071-plc");
        settings.source = CaptureSource::RealPlc;
        QString why;
        QVERIFY2(real.start(settings, &why), qPrintable(why));
        for (const FrameRecord& record : seen) {
            real.add(record);
        }
        real.stop();
        QVERIFY(real.status().hasData);

        CaptureSaveRequest request;
        request.outputRoot = fakeRoot;
        request.capturedRoot = fakeRoot;
        const CaptureController::SaveJob job = real.prepareSave(7, request, &why);
        QVERIFY2(static_cast<bool>(job), qPrintable(why));
        const CaptureSaveResult result = job();
        QVERIFY2(result.ok, qPrintable(result.message));
        QCOMPARE(result.token, quint64(7));
        QCOMPARE(result.files.size(), 3);

        const QString folder = QDir(fakeRoot).absoluteFilePath(QStringLiteral("gui-t071-plc"));
        const QString steps = slurp(QDir(folder).filePath(QStringLiteral("steps.vec")));
        const QString meta = slurp(QDir(folder).filePath(QStringLiteral("run.meta")));
        QVERIFY(steps.contains(QStringLiteral("# source: plc")));
        QVERIFY(meta.contains(QStringLiteral("capture_source: plc")));
        QVERIFY2(!meta.contains(QStringLiteral("192.0.2.10")), "run.meta must not hold the address");
        int exitCode = 0;
        const QString output = runReplay(fakeRoot, &exitCode);
        QVERIFY2(exitCode == 0, qPrintable(output));
        QVERIFY2(output.contains(QStringLiteral("replayed gui-t071-plc")), qPrintable(output));

        // The same data as a mock capture is refused under that root; so is a real-PLC capture of
        // a link that is local.
        CaptureController asMock;
        asMock.setTarget(cfg, false);
        settings.source = CaptureSource::MockPlc;
        QVERIFY(asMock.start(settings, &why));
        for (const FrameRecord& record : seen) {
            asMock.add(record);
        }
        asMock.stop();
        QVERIFY(!asMock.prepareSave(8, request, &why));
        QVERIFY(why.contains(QStringLiteral("may not be written")));
        CaptureController local;
        local.setTarget(cfg, true);
        settings.source = CaptureSource::RealPlc;
        QVERIFY(!local.start(settings, &why));
        QVERIFY(why.contains(QStringLiteral("real PLC")));
    }

    // ---- the window ----------------------------------------------------------------------------

    void MainWindow_traceAndLogDocksFollowTheTabs() {
        MainWindow window;
        window.show();
        DeviceTab* device = window.devicePane()->addDevice();
        MockTab* mockTab = window.mockPane()->addMock();
        TelemetryRegistry* registry = window.telemetry();
        QCOMPARE(registry->tabs().size(), 2);
        TracePane* pane = window.tracePane();
        QCOMPARE(pane->current(), device->telemetry());

        pane->select(mockTab->telemetry());
        QCOMPARE(pane->current(), mockTab->telemetry());
        QCOMPARE(pane->capturePanel()->settings().source, CaptureSource::MockPlc);
        pane->select(device->telemetry());
        QCOMPARE(pane->capturePanel()->settings().source, CaptureSource::RealPlc); // no host yet

        // A line a tab writes shows up in the shared log, under the tab's name.
        device->telemetry()->note(mc::LogLevel::Warn, QStringLiteral("workbench.test"), QStringLiteral("hello"));
        QCOMPARE(registry->log()->rowCount(), 1);
        QCOMPARE(registry->log()->tabs(), QStringList{QStringLiteral("PLC 1")});
        window.logView()->setTabFilter(QStringLiteral("PLC 1"));
        QCOMPARE(window.logView()->filter()->rowCount(), 1);

        // Closing the tab that is shown moves the pane to the other one; closing both empties it.
        window.devicePane()->closeDevice(0);
        QCOMPARE(registry->tabs().size(), 1);
        QCOMPARE(pane->current(), mockTab->telemetry());
        window.mockPane()->closeMock(0);
        QCOMPARE(registry->tabs().size(), 0);
        QVERIFY(pane->current() == nullptr);
        QCOMPARE(pane->capturePanel()->exportAsReplayData(), quint64(0));
    }

    // The panel without a tab, and the export button's refusal when the source is a mock.
    void CapturePanel_exportNeedsARealSourceAndTheRepository() {
        TabTelemetry tele(QStringLiteral("Mock 1"), TabTelemetry::Kind::Mock);
        CapturePanel panel;
        panel.setCapturedRoot(repoCapturedRoot());
        QCOMPARE(panel.start(), quint64(0)); // no tab yet
        panel.setTelemetry(&tele);
        QCOMPARE(panel.settings().source, CaptureSource::RealPlc); // the telemetry's default hint
        panel.setSource(CaptureSource::MockPlc);
        panel.setProfileId(QStringLiteral("x"));
        QCOMPARE(panel.settings().profileId, QStringLiteral("x"));
        panel.setCapturedRoot(QString());
        QCOMPARE(panel.exportAsReplayData(), quint64(0)); // the repository is unknown
    }
};

namespace mc::workbench::test {
QObject* makeTraceSuite() {
    return new TraceCaptureTest;
}
} // namespace mc::workbench::test

#include "tst_gui_trace.moc"
