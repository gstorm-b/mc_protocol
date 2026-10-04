/**
 * @file mock_types.h
 * @brief The value types of the mock tab that cross between the `MockRunner` thread and the GUI
 * thread: settings, serial line, request log entries and memory blocks.
 *
 * Plain values only (no pointers into runner-owned memory), registered as Qt meta types where a
 * signal carries them (SPEC-gui-tool.md, "Architecture").
 */
#pragma once

#include "mc/mock/mock_plc.h"

#include <QMetaType>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace mc::workbench {

/**
 * @brief The `MockOptions` error codes as a copyable value (the log sink pointer stays with the
 * runner).
 */
struct MockSettings {
    quint16 unsupportedQna{0xC059};      ///< 3E / 3C: unsupported command or subcommand.
    quint8 unsupported1e{0x50};          ///< 1E end code for an unsupported command.
    quint8 unsupported1c{0x06};          ///< 1C NAK code for an unsupported command.
    quint16 sumErrorQna{0x7151};         ///< 3C request with a wrong SUM.
    quint8 sumError1c{0x06};             ///< 1C request with a wrong SUM.
    quint16 outOfRangeQna{0xC051};       ///< 3E / 3C: device beyond the limit.
    quint8 outOfRange1e{0x5B};           ///< 1E end code for a device beyond the limit.
    quint8 outOfRange1eAbnormal{0x10};   ///< 1E abnormal code sent with outOfRange1e.
    quint8 outOfRange1c{0x06};           ///< 1C NAK code for a device beyond the limit.

    /**
     * @brief Builds the library's options from this value.
     * @param[in] sink The log sink the mock writes to; not owned, may be null.
     * @return The options.
     */
    mc::MockOptions toOptions(mc::LogSink* sink) const;
};

/// @brief The line settings of a served COM port (`QSerialPort` values as integers).
struct SerialLine {
    QString portName;     ///< System name, e.g. "COM54".
    qint32 baudRate{9600}; ///< Bits per second.
    int dataBits{7};      ///< 5 to 8.
    int parity{2};        ///< `QSerialPort::Parity`: 0 none, 2 even, 3 odd, 4 space, 5 mark.
    int stopBits{1};      ///< `QSerialPort::StopBits`: 1 one, 3 one and a half, 2 two.
};

/// @brief One request the mock saw, copied out of `MockPlc::requests()`.
struct MockRequestEntry {
    quint64 seq{0};        ///< Running number of the request since the mock was created.
    qint64 tNs{0};         ///< Nanoseconds on the runner's clock when the batch was taken.
    QString op;            ///< "ReadBits", "ReadWords", "WriteBits" or "WriteWords".
    QString head;          ///< First point, e.g. "D100".
    quint16 count{0};      ///< Points of the request.
    bool answered{false};  ///< false when muted, dropped, or the station did not match.
    int errorCode{0};      ///< `mc::ErrorCode` of the answer as an integer; 0 is Ok.
    quint16 plcCode{0};    ///< The PLC end code the mock returned; 0 when Ok.
    QString message;       ///< The library's text of the error; empty when Ok.
};

/// @brief Request log entries since the last emit (a batch is bounded).
struct MockRequestBatch {
    QVector<MockRequestEntry> entries; ///< Oldest first.
    quint32 dropped{0};                ///< Entries discarded because the batch was full.
};

/// @brief A copy of consecutive points of the mock's memory image.
struct MemoryBlock {
    quint64 token{0};         ///< The token of the read that produced it.
    QString head;             ///< Head device as read, e.g. "D100".
    bool bits{false};         ///< true: `values` are 0 or 1 per bit; false: one word each.
    QVector<quint16> values;  ///< The points, in device order.
};

/**
 * @brief Registers the types of this header as Qt meta types.
 *
 * Safe to call any number of times, from any thread.
 */
void registerMockMetaTypes();

} // namespace mc::workbench

Q_DECLARE_METATYPE(mc::workbench::MockRequestEntry)
Q_DECLARE_METATYPE(mc::workbench::MockRequestBatch)
Q_DECLARE_METATYPE(mc::workbench::MemoryBlock)
