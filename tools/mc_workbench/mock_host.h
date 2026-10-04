/**
 * @file mock_host.h
 * @brief `MockHost`: the GUI-thread handle of one `MockRunner` thread.
 */
#pragma once

#include "mc/core/frame_config.h"
#include "mc/core/log.h"
#include "mc/mock/mock_plc.h"
#include "mc_workbench/capture_types.h"
#include "mc_workbench/mock_types.h"
#include "mc_workbench/runner_thread.h"
#include "mc_workbench/runner_types.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

namespace mc::workbench {

class MockRunner;

/**
 * @brief What a mock tab talks to: starts a runner thread with a `MockRunner` on it, forwards
 * commands as queued calls and re-emits the runner's signals on the GUI thread.
 *
 * The same rules as `DeviceHost`: every method returns at once, commands carry value copies,
 * answers come back as `commandDone` with the returned token.
 *
 * @note Lives on the GUI thread. Destroying it stops the runner thread within
 *       `RunnerThread::kDefaultStopTimeoutMs`.
 * @see MockRunner, DeviceHost
 */
class MockHost : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Starts a runner thread that builds a `MockRunner` for @p frame.
     * @param[in] name Name of the thread.
     * @param[in] frame Frame family and settings the mock answers; copied.
     * @param[in] parent Qt parent of this handle.
     */
    MockHost(const QString& name, const mc::FrameConfig& frame, QObject* parent = nullptr);

    /**
     * @brief Starts a runner thread that builds a `MockRunner` for @p frame and @p settings.
     * @param[in] name Name of the thread.
     * @param[in] frame Frame family and settings the mock answers; copied.
     * @param[in] settings The mock's error codes; copied.
     * @param[in] parent Qt parent of this handle.
     */
    MockHost(const QString& name, const mc::FrameConfig& frame, const MockSettings& settings,
             QObject* parent = nullptr);

    /// @brief Stops the thread (bounded) and releases the handle.
    ~MockHost() override;

    /// @brief Replaces the mock by a new one (empty memory, no faults); refused while serving.
    /// @param[in] frame Frame family and settings the new mock answers; copied.
    /// @param[in] settings The new mock's error codes; copied.
    /// @return The token of the `commandDone` that answers.
    quint64 reconfigure(const mc::FrameConfig& frame, const MockSettings& settings);

    /// @brief Opens a COM port and serves the mock on it.
    /// @param[in] line Port name and line settings; copied.
    /// @return The token of the `commandDone` that answers.
    quint64 openSerial(const SerialLine& line);

    /// @brief Copies consecutive points of the memory image; answered by `memoryRead`.
    /// @param[in] head Head device, e.g. "D100".
    /// @param[in] count Points, 1 to `MockRunner::kMaxMemoryPoints`.
    /// @param[in] bits true to read bits, false to read words.
    /// @return The token of the `commandDone` (and the `MemoryBlock`) that answers.
    quint64 readMemory(const QString& head, quint16 count, bool bits);

    /// @brief Swallows the next @p count requests.
    /// @param[in] count Requests to swallow.
    /// @return The token of the `commandDone` that answers.
    quint64 muteNext(quint32 count);

    /// @brief Corrupts the next @p count responses.
    /// @param[in] mode How the response is corrupted.
    /// @param[in] count Responses to corrupt.
    /// @return The token of the `commandDone` that answers.
    quint64 corruptNext(mc::Corruption mode, quint32 count = 1);

    /// @brief Makes access to a device range fail with a PLC error.
    /// @param[in] type Device type of the range.
    /// @param[in] first First device number.
    /// @param[in] last Last device number.
    /// @param[in] code PLC end code the mock answers with.
    /// @param[in] abnormal 1E abnormal code.
    /// @return The token of the `commandDone` that answers.
    quint64 failRange(mc::DeviceType type, quint32 first, quint32 last, quint16 code,
                      quint8 abnormal = 0);

    /// @brief Removes every failRange fault.
    /// @return The token of the `commandDone` that answers.
    quint64 clearFaults();

    /// @brief Sets the number of points of @p type the mock accepts.
    /// @param[in] type Device type.
    /// @param[in] limit Number of points; access beyond it is answered with the out-of-range code.
    /// @return The token of the `commandDone` that answers.
    quint64 setDeviceLimit(mc::DeviceType type, quint32 limit);

    /// @brief Starts listening on `127.0.0.1`.
    /// @param[in] port TCP port; 0 lets the system choose.
    /// @return The token; `commandDone.value` is the bound port.
    quint64 listen(quint16 port = 0);

    /// @brief Closes the client and the server.
    void stopListening();

    /// @brief Sets consecutive words of the memory image.
    /// @param[in] head Head device, e.g. "D100".
    /// @param[in] values The values; copied.
    /// @return The token of the `commandDone` that answers.
    quint64 setWords(const QString& head, const QVector<quint16>& values);

    /// @brief Sets consecutive bits of the memory image.
    /// @param[in] head Head device, e.g. "M100".
    /// @param[in] values The values; copied.
    /// @return The token of the `commandDone` that answers.
    quint64 setBits(const QString& head, const QVector<bool>& values);

    /// @brief Makes the mock swallow requests without answering.
    /// @param[in] on true to mute.
    void mute(bool on);

    /// @brief Sets the lowest level of the log lines the runner keeps.
    /// @param[in] level The new level.
    void setLogLevel(mc::LogLevel level);

    /// @brief Asks the runner for the threads of its objects; answered by `threadReport`.
    void requestThreadReport();

    /// @brief Turns the frame decoder of the trace on or off.
    /// @param[in] on true to decode.
    void setTraceDecode(bool on);

    /// @brief Starts recording what the mock receives and sends (tx = the client's requests).
    /// @param[in] settings Profile id, source and limits; a real-PLC capture is refused.
    /// @return The token of the `commandDone` that answers.
    quint64 startCapture(const mc::workbench::CaptureSettings& settings);

    /// @brief Stops recording; the data stays for saving.
    /// @return The token of the `commandDone` that answers.
    quint64 stopCapture();

    /// @brief Drops the recorded data.
    /// @return The token of the `commandDone` that answers.
    quint64 discardCapture();

    /// @brief Writes the recorded capture (on a worker thread of the runner).
    /// @param[in] request Target folder, protected folder, overwrite flag.
    /// @return The token carried by the `captureSaved` that answers.
    quint64 saveCapture(const mc::workbench::CaptureSaveRequest& request);

    /**
     * @brief Posts @p command to run on the runner thread (tests and advanced use).
     * @param[in] command Runs with the `MockRunner`; contained when it throws.
     */
    void post(std::function<void(MockRunner&)> command);

    /// @brief The handle of the thread, for stop(), isStuck() and the thread identity.
    /// @return Never null.
    RunnerThread* runnerThread() const noexcept { return m_thread; }

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
    /// @param[out] what Describes the endpoint; empty when not serving.
    void servingChanged(bool serving, const QString& what);

    /// @brief The COM port failed while open and was closed.
    /// @param[out] message The port's error text.
    void serialError(const QString& message);

    /// @brief Totals, when something changed.
    /// @param[out] stats The running totals.
    void statsChanged(const mc::workbench::MockStats& stats);

    /// @brief Request log entries since the last batch.
    /// @param[out] batch The entries, oldest first.
    void requestsLogged(const mc::workbench::MockRequestBatch& batch);

    /// @brief The answer to readMemory().
    /// @param[out] block The copied points.
    void memoryRead(const mc::workbench::MemoryBlock& block);

    /// @brief Log lines since the last batch.
    /// @param[out] lines The lines, oldest first.
    void logBatch(const QVector<mc::workbench::LogLine>& lines);

    /// @brief Wire chunks since the last batch: tx are the client's requests, rx the mock's answers.
    /// @param[out] frames The chunks, oldest first.
    void framesBatch(const QVector<mc::workbench::FrameRecord>& frames);

    /// @brief Chunks left out of the trace because the GUI was behind (running total).
    /// @param[out] total Chunks dropped since the mock was built.
    void framesDropped(quint64 total);

    /// @brief The capture's status changed.
    /// @param[out] status What the capture holds now.
    void captureStatusChanged(const mc::workbench::CaptureStatus& status);

    /// @brief A save finished or was refused.
    /// @param[out] result Token, success, files.
    void captureSaved(const mc::workbench::CaptureSaveResult& result);

    /// @brief The answer to a command.
    /// @param[out] result Token, success and details.
    void commandDone(const mc::workbench::CommandResult& result);

    /// @brief The answer to requestThreadReport().
    /// @param[out] report The thread identities.
    void threadReport(const mc::workbench::ThreadReport& report);

    /// @brief An exception was contained on the runner thread; the server is stopped.
    /// @param[out] message What failed and why.
    void failed(const QString& message);

private slots:
    void onFlushed();

private:
    quint64 nextToken() noexcept { return ++m_lastToken; }

    RunnerThread* m_thread{nullptr};
    quint64 m_lastToken{0};
};

} // namespace mc::workbench
