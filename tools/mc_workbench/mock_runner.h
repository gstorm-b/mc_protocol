/**
 * @file mock_runner.h
 * @brief `MockRunner`: a `MockPlc` served over loopback TCP or a COM port on a runner thread (the
 * GUI's own small serving code; `examples/virtual_plc` is unchanged).
 */
#pragma once

#include "hil_capture/recording_transport.h"
#include "mc/core/frame_config.h"
#include "mc/mock/mock_plc.h"
#include "mc_workbench/capture_controller.h"
#include "mc_workbench/capture_types.h"
#include "mc_workbench/flow_gate.h"
#include "mc_workbench/frame_decoder.h"
#include "mc_workbench/mock_types.h"
#include "mc_workbench/queue_log_sink.h"
#include "mc_workbench/runner_base.h"
#include "mc_workbench/runner_types.h"

#include <QString>
#include <QVector>

#include <cstdint>
#include <map>
#include <memory>

class QIODevice;
class QSerialPort;
class QTcpServer;
class QThread;
class QTcpSocket;
class QTimer;

namespace mc::workbench {

/**
 * @brief Serves one `MockPlc` to TCP clients on `127.0.0.1`, or over one COM port.
 *
 * Created by a `RunnerThread` factory: the mock, the `QTcpServer`, the `QSerialPort` and the
 * sockets live on the runner thread. Bytes that arrive go into `MockPlc::bytesIn()`; what
 * `nextResponse()` hands back is written to the same socket or port. One `MockPlc` serves every
 * connection (up to `kMaxClients` TCP clients at once), so the memory image the GUI edits is the
 * one every client sees. Each TCP client has its own `MockPlc` input stream (opened when it
 * connects, closed when it disconnects) and its own trace decoder, so requests split across
 * segments by two clients never mix and a request a client abandons dies with its connection. COM
 * serving uses stream 0. TCP and COM serving exclude each other: stopListening() ends either.
 *
 * Statistics, request log entries and log lines are emitted at most about 30 times a second. The
 * request log of the `MockPlc` is cleared on every emit, so it stays bounded; `MockStats` carries
 * running totals and `requestsLogged` the entries themselves (a batch holds at most
 * `kMaxRequestsPerBatch`; the rest is counted as dropped).
 *
 * @note Every public method is a command that runs on the runner thread.
 * @see MockHost, RunnerThread
 */
class MockRunner : public RunnerBase {
    Q_OBJECT
public:
    /// @brief Milliseconds between two emits of the batched signals (about 30 a second).
    static constexpr int kFlushIntervalMs = 34;

    /// @brief The most request log entries one batch carries.
    static constexpr int kMaxRequestsPerBatch = 2000;

    /// @brief The most request log entries held while the GUI has not consumed the last batch;
    ///        older ones are counted as dropped.
    static constexpr int kMaxPendingRequests = 20000;

    /// @brief The most wire chunks held for the GUI between two emits; further chunks are counted
    ///        and dropped from the view (a running capture still records them).
    static constexpr int kMaxPendingFrames = 20000;

    /// @brief The most bytes of wire chunks held for the GUI between two emits.
    static constexpr qint64 kMaxPendingFrameBytes = 8 * 1024 * 1024;

    /// @brief The most chunks one `framesBatch` carries; the rest waits for the next emit.
    static constexpr int kMaxFramesPerBatch = 4000;

    /// @brief The most TCP clients served at the same time; further connections are closed.
    static constexpr int kMaxClients = 32;

    /// @brief The most points one readMemory() returns.
    static constexpr int kMaxMemoryPoints = 4096;

    /**
     * @brief Builds the mock for @p frame on the current thread.
     * @param[in] frame Frame family and settings the mock answers.
     * @param[in] parent Qt parent, normally none.
     */
    explicit MockRunner(const mc::FrameConfig& frame, QObject* parent = nullptr);

    /**
     * @brief Builds the mock for @p frame with the error codes of @p settings.
     * @param[in] frame Frame family and settings the mock answers.
     * @param[in] settings The mock's error codes.
     * @param[in] parent Qt parent, normally none.
     */
    MockRunner(const mc::FrameConfig& frame, const MockSettings& settings, QObject* parent = nullptr);

    /// @brief Closes the server and deletes the mock on the runner thread.
    ~MockRunner() override;

    /**
     * @brief Replaces the mock by a new one for @p frame and @p settings.
     *
     * The memory image, the faults and the counters start empty again. Refused while serving.
     *
     * @param[in] token Answered in `commandDone`.
     * @param[in] frame Frame family and settings the new mock answers.
     * @param[in] settings The new mock's error codes.
     */
    void reconfigure(quint64 token, const mc::FrameConfig& frame, const MockSettings& settings);

    /**
     * @brief Starts listening on `127.0.0.1`.
     * @param[in] token Answered in `commandDone`; `value` is the bound port.
     * @param[in] port TCP port; 0 lets the system choose.
     */
    void listen(quint64 token, quint16 port);

    /**
     * @brief Opens a COM port and serves the mock on it.
     * @param[in] token Answered in `commandDone`.
     * @param[in] line Port name and line settings.
     */
    void openSerial(quint64 token, const SerialLine& line);

    /// @brief Closes the client, the server and the COM port; the memory image stays.
    void stopListening();

    /**
     * @brief Sets consecutive words of the memory image.
     * @param[in] token Answered in `commandDone`.
     * @param[in] head Head device, e.g. "D100".
     * @param[in] values The values, in device order.
     */
    void setWords(quint64 token, const QString& head, const QVector<quint16>& values);

    /**
     * @brief Sets consecutive bits of the memory image.
     * @param[in] token Answered in `commandDone`.
     * @param[in] head Head device, e.g. "M100".
     * @param[in] values The values, in device order.
     */
    void setBits(quint64 token, const QString& head, const QVector<bool>& values);

    /**
     * @brief Copies consecutive points of the memory image and emits them as `memoryRead`.
     * @param[in] token Answered in `commandDone`; also carried by the `MemoryBlock`.
     * @param[in] head Head device, e.g. "D100".
     * @param[in] count Points, 1 to `kMaxMemoryPoints`.
     * @param[in] bits true to read bits (0 or 1 each), false to read words.
     */
    void readMemory(quint64 token, const QString& head, quint16 count, bool bits);

    /// @brief Makes the mock swallow requests without answering (`MockPlc::mute`).
    /// @param[in] on true to mute.
    void mute(bool on);

    /**
     * @brief Swallows the next @p count requests (`MockPlc::muteNext`).
     * @param[in] token Answered in `commandDone`.
     * @param[in] count Requests to swallow.
     */
    void muteNext(quint64 token, quint32 count);

    /**
     * @brief Corrupts the next @p count responses (`MockPlc::corruptNext`).
     * @param[in] token Answered in `commandDone`.
     * @param[in] mode How the response is corrupted.
     * @param[in] count Responses to corrupt.
     */
    void corruptNext(quint64 token, mc::Corruption mode, quint32 count);

    /**
     * @brief Makes access to a device range fail with a PLC error (`MockPlc::failRange`).
     * @param[in] token Answered in `commandDone`; refused when @p first is above @p last.
     * @param[in] type Device type of the range.
     * @param[in] first First device number.
     * @param[in] last Last device number.
     * @param[in] code PLC end code the mock answers with.
     * @param[in] abnormal 1E abnormal code.
     */
    void failRange(quint64 token, mc::DeviceType type, quint32 first, quint32 last, quint16 code,
                   quint8 abnormal);

    /**
     * @brief Removes every failRange fault (`MockPlc::clearFaults`).
     * @param[in] token Answered in `commandDone`.
     */
    void clearFaults(quint64 token);

    /**
     * @brief Sets the highest device number the mock accepts (`MockPlc::setDeviceLimit`).
     * @param[in] token Answered in `commandDone`.
     * @param[in] type Device type.
     * @param[in] limit Number of points; access beyond it is answered with the out-of-range code.
     */
    void setDeviceLimit(quint64 token, mc::DeviceType type, quint32 limit);

    /// @brief Sets the lowest level of the log lines that are kept.
    /// @param[in] level The new level.
    void setLogLevel(mc::LogLevel level);

    /// @brief Turns the frame decoder (frame boundaries and text of the trace) on or off.
    /// @param[in] on true to decode.
    void setTraceDecode(bool on);

    /// @brief Turns the back-pressure on: a batch is emitted only after the GUI acknowledged the
    ///        previous one (`ackFlush`). Off by default.
    /// @param[in] on true to enforce acknowledgements.
    void enableFlowControl(bool on);

    /// @brief The GUI consumed everything emitted up to the last `flushed()`.
    void ackFlush();

    /// @brief Starts recording what the mock receives and sends (tx = the client's requests).
    /// @param[in] token Answered in `commandDone`.
    /// @param[in] settings Profile id, source and limits; a real-PLC capture is refused.
    void startCapture(quint64 token, const mc::workbench::CaptureSettings& settings);

    /// @brief Stops recording; the data stays for saving.
    /// @param[in] token Answered in `commandDone`.
    void stopCapture(quint64 token);

    /// @brief Drops the recorded data.
    /// @param[in] token Answered in `commandDone`.
    void discardCapture(quint64 token);

    /// @brief Writes the recorded capture; the files are written on a worker thread.
    /// @param[in] token Carried by the `captureSaved` that answers.
    /// @param[in] request Target folder, the protected folder and the overwrite flag.
    void saveCapture(quint64 token, const mc::workbench::CaptureSaveRequest& request);

    /// @brief Chunks held for the GUI right now (a test hook for the back-pressure bound).
    /// @return The number of pending chunks.
    int pendingFrameCount() const noexcept { return static_cast<int>(m_pendingFrames.size()); }

    /// @brief Emits `threadReport` with the threads of the runner, the server and the mock.
    void reportThreads();

    /// @brief The mock, for thread identity checks and tests; use on the runner thread only.
    /// @return Never null.
    mc::MockPlc* plc() const noexcept { return m_plc.get(); }

    /// @brief Closes the client, the server and the COM port and emits what is pending.
    void shutdown() override;

signals:
    /// @brief The server is listening.
    /// @param[out] port The bound port.
    void listening(quint16 port);

    /// @brief A client connected.
    /// @param[out] peer "address:port" of the client.
    void clientConnected(const QString& peer);

    /// @brief The client went away.
    void clientDisconnected();

    /// @brief The mock started or stopped serving (TCP or COM).
    /// @param[out] serving true while a server or a COM port is open.
    /// @param[out] what "tcp 127.0.0.1:<port>" or "COM54 9600 7E1"; empty when not serving.
    void servingChanged(bool serving, const QString& what);

    /// @brief The COM port failed while open (removed, access lost); it has been closed.
    /// @param[out] message The port's error text.
    void serialError(const QString& message);

    /// @brief Totals since the last emit, when something changed.
    /// @param[out] stats The running totals.
    void statsChanged(const mc::workbench::MockStats& stats);

    /// @brief Request log entries since the last emit.
    /// @param[out] batch The entries, oldest first.
    void requestsLogged(const mc::workbench::MockRequestBatch& batch);

    /// @brief Log lines since the last emit.
    /// @param[out] lines The lines, oldest first.
    void logBatch(const QVector<mc::workbench::LogLine>& lines);

    /// @brief The answer to readMemory().
    /// @param[out] block The copied points.
    void memoryRead(const mc::workbench::MemoryBlock& block);

    /// @brief The answer to a command.
    /// @param[out] result Token, success and details.
    void commandDone(const mc::workbench::CommandResult& result);

    /// @brief The answer to reportThreads().
    /// @param[out] report The thread identities.
    void threadReport(const mc::workbench::ThreadReport& report);

    /// @brief Wire chunks since the last emit: tx are the client's requests, rx the mock's answers.
    /// @param[out] frames The chunks, oldest first.
    void framesBatch(const QVector<mc::workbench::FrameRecord>& frames);

    /// @brief Chunks left out of the trace because the GUI was behind (running total).
    /// @param[out] total Chunks dropped since the mock was built.
    void framesDropped(quint64 total);

    /// @brief The last signal of a batch: the GUI answers it with `ackFlush()`.
    void flushed();

    /// @brief The capture's status changed.
    /// @param[out] status What the capture holds now.
    void captureStatusChanged(const mc::workbench::CaptureStatus& status);

    /// @brief A save finished or was refused.
    /// @param[out] result Token, success, files.
    void captureSaved(const mc::workbench::CaptureSaveResult& result);

protected:
    /// @brief Closes the client, the server and the COM port.
    void stopAfterFailure() noexcept override;

private:
    void buildPlc();
    void onNewConnection();
    void onReadyRead(QIODevice* io);
    void releaseLink(QTcpSocket* socket);
    void dropClients();
    void closeServer();
    void closeSerial(const QString& reason);
    void announceServing(bool serving, const QString& what);
    bool isServing() const;
    void flushNow(bool force = false);
    void collectFrame(qint64 tNs, bool tx, const QByteArray& bytes, FrameDecoder* decoder);
    void configureCapture();
    void emitCaptureStatus();
    void answer(quint64 token, bool ok, const QString& message, quint64 value);
    bool ready(quint64 token);

    std::shared_ptr<mc::hil::RecordingClock> m_clock;
    QueueLogSink m_log;
    mc::FrameConfig m_frame;
    MockSettings m_settings;
    std::unique_ptr<mc::MockPlc> m_plc;
    QThread* m_plcThread{nullptr}; ///< The thread the mock was created on.
    QTcpServer* m_server;          ///< Child of this object.
    QVector<QTcpSocket*> m_clients; ///< Children of the server, in connection order.
    /// What one TCP client owns on the mock: its input stream and its trace decoder.
    struct ClientLink {
        mc::MockStreamId stream{0};
        std::unique_ptr<FrameDecoder> decoder; /// Null while decode is off.
    };
    std::map<QTcpSocket*, ClientLink> m_links; ///< One entry per element of m_clients.
    QSerialPort* m_serial{nullptr}; ///< Child of this object while a COM port is served.
    QTimer* m_flushTimer;          ///< Single shot; armed when traffic is pending.
    quint64 m_requests{0};         ///< Requests seen, from the cleared request logs.
    quint64 m_skippedBytes{0};     ///< Skipped bytes, from the cleared counters.
    bool m_statsDirty{false};
    QVector<FrameRecord> m_pendingFrames; ///< Seen on the wire, not yet emitted.
    qint64 m_pendingFrameBytes{0};
    quint64 m_framesDropped{0};
    bool m_dropsChanged{false};
    quint32 m_pendingRequestsDropped{0}; ///< Request entries discarded while the GUI was behind.
    std::unique_ptr<FrameDecoder> m_decoder; ///< COM link (stream 0); null while decode is off.
    bool m_decode{true};
    FlowGate m_gate;
    bool m_flushWanted{false}; ///< A flush was held back by the gate.
    CaptureController m_capture;
    QVector<QThread*> m_saveWorkers; ///< Capture writers still running.
};

} // namespace mc::workbench
