/**
 * @file runner_types.h
 * @brief The value types that cross between a runner thread and the GUI thread.
 *
 * Everything here is a plain value (no pointers into runner-owned memory) and a registered Qt
 * meta type, so it travels through queued signals and queued calls as a copy (SPEC-gui-tool.md,
 * "Architecture").
 */
#pragma once

#include "mc/core/device.h"
#include "mc/core/log.h"
#include "mc/core/value_store.h"
#include "mc/device/mc_device.h"

#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace mc::workbench {

/**
 * @brief The outcome of one command sent to a runner.
 *
 * Every command that can fail answers with exactly one CommandResult carrying the token the
 * host returned when the command was sent.
 */
struct CommandResult {
    quint64 token{0};   ///< The token the host returned for the command.
    bool ok{true};      ///< Whether the command was carried out.
    int errorCode{0};   ///< `mc::ErrorCode` as an integer when the library refused; 0 otherwise.
    QString message;    ///< Why it failed; empty on success.
    quint64 value{0};   ///< Command specific: a subscription id, a request id or a TCP port.
};

/// @brief The changed points of one response (`McDevice::valuesChanged`), as a value copy.
struct ValueUpdate {
    mc::DeviceType type{mc::DeviceType::D}; ///< Device type of the changed points.
    quint32 round{0};                       ///< Polling round the response belongs to.
    QVector<mc::Change> changes;            ///< The changed points with old and new value.
};

/**
 * @brief What a device runner collected since its last emit (at most about 30 emits a second).
 *
 * Snapshots are coalesced: only the newest snapshot of each device type is kept between two
 * emits. Changes are kept in order.
 */
struct ValueBatch {
    QVector<mc::DeviceSnapshot> snapshots; ///< Newest snapshot per device type.
    QVector<ValueUpdate> changes;          ///< Every value change, oldest first.
    quint32 droppedChanges{0};             ///< Changes discarded because the batch was full.
};

/// @brief What the optional frame decoder made of one chunk.
enum class FrameEdge : quint8 {
    Unknown = 0,  ///< Not decoded (decode is off) or not recognised.
    Complete = 1, ///< The chunk ends a request or a response frame.
    Partial = 2   ///< A fragment: more bytes of the same frame follow.
};

/// @brief One chunk on the wire, copied out of the `RecordingTransport` (or seen by a mock).
struct FrameRecord {
    qint64 tNs{0};     ///< Nanoseconds on the runner's clock.
    bool tx{true};     ///< true: written to the PLC (a mock: received from the client); false: the answer.
    QByteArray bytes;  ///< The bytes of the chunk.
    FrameEdge edge{FrameEdge::Unknown}; ///< Frame boundary, when decoded.
    QString note;      ///< Decoded text ("ReadWords D100 x4", "ok", "PLC error 0xC051"); may be empty.
};

/// @brief One line of a `LogSink`, copied.
struct LogLine {
    qint64 tNs{0};                          ///< Nanoseconds on the runner's clock.
    mc::LogLevel level{mc::LogLevel::Info}; ///< Level the line was logged at.
    QString category;                       ///< Subsystem, e.g. "mc.session".
    QString message;                        ///< The text.
};

/// @brief How one ad-hoc request ended (`McDevice::requestFinished`), as a value copy.
struct RequestOutcome {
    quint64 id{0};        ///< The id the command answered with (`CommandResult::value`).
    int errorCode{0};     ///< `mc::ErrorCode` as an integer; 0 is success.
    QString message;      ///< The error's text; empty on success.
    QByteArray payload;   ///< Normalized read payload; empty for a write or a failure.
};

/// @brief What a runner's link fault says (`McDevice::linkFault`), as a value copy.
struct FaultReport {
    int kind{0};              ///< `mc::LinkFaultKind` as an integer.
    int errorCode{0};         ///< `mc::ErrorCode` as an integer.
    QString message;          ///< The error's text.
    bool reopenTransport{false}; ///< Whether the Session wants a fresh connection.
};

/**
 * @brief The threads of a runner's objects, as identity tokens.
 *
 * Each field is a `QThread*` converted to an integer so that it can only be compared, never
 * dereferenced on the GUI thread. A correct runner has every field equal to `runnerThread`.
 */
struct ThreadReport {
    quintptr runnerThread{0};    ///< The QThread the runner was started on.
    quintptr objectThread{0};    ///< `QObject::thread()` of the runner object.
    quintptr deviceThread{0};    ///< Thread of the McDevice, or where the MockPlc was created.
    quintptr transportThread{0}; ///< Thread of the transport, or of the TCP server of a mock.
    quintptr currentThread{0};   ///< The thread the report was made on.
};

/// @brief Totals of a mock runner; sent at most about 30 times a second while it is busy.
struct MockStats {
    quint64 requests{0};     ///< Requests the mock has seen since it was created.
    quint32 eotCount{0};     ///< EOT bytes received (serial frames).
    quint64 skippedBytes{0}; ///< Bytes skipped while looking for a frame start.
    int clients{0};          ///< Connected TCP clients (at most MockRunner::kMaxClients).
};

/**
 * @brief Registers every type of this header (and the device layer's) as a Qt meta type.
 *
 * Safe to call any number of times, from any thread.
 */
void registerRunnerMetaTypes();

} // namespace mc::workbench

Q_DECLARE_METATYPE(mc::workbench::CommandResult)
Q_DECLARE_METATYPE(mc::workbench::ValueUpdate)
Q_DECLARE_METATYPE(mc::workbench::ValueBatch)
Q_DECLARE_METATYPE(mc::workbench::FrameRecord)
Q_DECLARE_METATYPE(mc::workbench::LogLine)
Q_DECLARE_METATYPE(mc::workbench::RequestOutcome)
Q_DECLARE_METATYPE(mc::workbench::FaultReport)
Q_DECLARE_METATYPE(mc::workbench::ThreadReport)
Q_DECLARE_METATYPE(mc::workbench::MockStats)
