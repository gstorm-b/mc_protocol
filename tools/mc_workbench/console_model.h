/**
 * @file console_model.h
 * @brief `ConsoleModel`: the log of ad-hoc read and write commands of a device tab, with the
 * correlation of a command's token, its request id and its outcome; plus the parsers of the value
 * lists a user types.
 */
#pragma once

#include "mc_workbench/runner_types.h"

#include <QAbstractTableModel>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>

#include <deque>

namespace mc::workbench {

/**
 * @brief Parses "1 2 0x10 -5" into 16-bit words.
 * @param[in] text Values separated by spaces, commas or semicolons; decimal, `0x` hexadecimal, or
 *            negative decimal down to -32768.
 * @param[out] out The values; unchanged on failure.
 * @param[out] error Why the text is not a word list; may be null.
 * @return true when every item parsed and at least one value is present.
 */
bool parseWordList(const QString& text, QVector<quint16>& out, QString* error = nullptr);

/**
 * @brief Parses "1 0 1 true off" into bit values.
 * @param[in] text Values separated by spaces, commas or semicolons: 0, 1, true, false, on, off.
 * @param[out] out The values; unchanged on failure.
 * @param[out] error Why the text is not a bit list; may be null.
 * @return true when every item parsed and at least one value is present.
 */
bool parseBitList(const QString& text, QVector<bool>& out, QString* error = nullptr);

/// @brief What the console sent.
enum class ConsoleOp { ReadWords, ReadBits, WriteWords, WriteBits };

/// @brief One row of the console: a command and how it ended.
struct ConsoleEntry {
    enum class State { Pending, Ok, Failed };

    QString time;            ///< Wall-clock time the command was sent (HH:mm:ss.zzz).
    quint64 token{0};        ///< Token the host returned for the command.
    quint64 requestId{0};    ///< Request id (0 until the runner has answered, and for a refusal).
    ConsoleOp op{ConsoleOp::ReadWords};
    QString command;         ///< Human-readable command, e.g. "read words D100 x4".
    State state{State::Pending};
    QString result;          ///< Values read, "ok" for a write, or the error text.
};

/**
 * @brief Table model of the console log (columns Time, Token, Request, Command, Result).
 *
 * The runner answers a command twice: `commandDone` (carrying the request id) and
 * `requestFinished` (the outcome). They may arrive in either order; the model matches them by the
 * token and the id. The log is bounded (kCapacity rows).
 *
 * @note GUI thread only. Holds value copies, never a runner object.
 */
class ConsoleModel : public QAbstractTableModel {
    Q_OBJECT
public:
    /// @brief Columns of the table.
    enum Column { TimeColumn, TokenColumn, RequestColumn, CommandColumn, ResultColumn, ColumnCount };

    /// @brief The most rows kept; the oldest rows are dropped first.
    static constexpr int kCapacity = 1000;

    /// @brief Creates an empty log.
    /// @param[in] parent Qt parent.
    explicit ConsoleModel(QObject* parent = nullptr);

    /**
     * @brief Adds a pending row for a command that was just sent.
     * @param[in] token The token the host returned.
     * @param[in] op Kind of command.
     * @param[in] command Text shown in the Command column.
     * @return The row.
     */
    int begin(quint64 token, ConsoleOp op, const QString& command);

    /// @brief Takes the runner's answer to a command (the request id, or a refusal).
    /// @param[in] result The answer; a token this model does not know is ignored.
    void onCommandDone(const CommandResult& result);

    /// @brief Takes the outcome of a request.
    /// @param[in] outcome The outcome; an id this model does not know yet is kept for its command.
    void onRequestFinished(const RequestOutcome& outcome);

    /// @brief Removes every row.
    void clear();

    /// @brief One row.
    /// @param[in] row Row number, 0 is the oldest kept.
    /// @return The entry.
    const ConsoleEntry& entry(int row) const { return m_entries.at(static_cast<size_t>(row)); }

    /// @brief Finds the row of a command.
    /// @param[in] token The token.
    /// @return The row, or -1 when it is not (or no longer) in the log.
    int rowOfToken(quint64 token) const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    ConsoleEntry* byToken(quint64 token, int* row = nullptr);
    void finish(int row, const RequestOutcome& outcome);
    void changed(int row);
    static QString describePayload(ConsoleOp op, const QByteArray& payload);

    std::deque<ConsoleEntry> m_entries;
    quint64 m_firstSerial{0};                  ///< Serial of m_entries.front().
    QHash<quint64, quint64> m_serialByToken;   ///< token -> serial
    QHash<quint64, quint64> m_serialById;      ///< request id -> serial
    QHash<quint64, RequestOutcome> m_early;    ///< outcomes whose command was not answered yet
};

} // namespace mc::workbench
