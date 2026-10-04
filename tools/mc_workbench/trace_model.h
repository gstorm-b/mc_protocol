/**
 * @file trace_model.h
 * @brief `TraceModel`: the bounded ring of wire chunks one tab's frame trace shows.
 */
#pragma once

#include "mc_workbench/runner_types.h"

#include <QAbstractTableModel>
#include <QByteArray>
#include <QString>
#include <QVector>

#include <deque>

namespace mc::workbench {

/**
 * @brief A table of wire chunks (tx and rx) with nanosecond stamps, hex and ASCII, frame
 * boundaries and the decoder's text, kept in a ring bounded by a row count and a byte count.
 *
 * Fed with `FrameRecord` batches from a runner (GUI thread only). When a limit is reached the
 * oldest rows are dropped, so a chatty link cannot grow memory without limit
 * (SPEC-gui-tool.md, "Back-pressure"). Both limits are configurable.
 *
 * @note GUI thread only. Holds value copies only.
 */
class TraceModel : public QAbstractTableModel {
    Q_OBJECT
public:
    /// @brief Columns of the table.
    enum Column {
        ColSeq,   ///< Running number of the chunk since the model was created or cleared.
        ColTime,  ///< Nanoseconds on the runner's clock.
        ColDir,   ///< "TX" or "RX".
        ColLen,   ///< Bytes in the chunk.
        ColFrame, ///< "frame" when the chunk ends a frame, "part" for a fragment.
        ColHex,   ///< The first bytes in hex.
        ColAscii, ///< The first bytes as ASCII.
        ColNote,  ///< The decoder's text.
        ColCount
    };

    /// @brief Extra data roles.
    enum Role {
        RawBytesRole = Qt::UserRole + 1, ///< The whole chunk as a QByteArray.
        IsTxRole                          ///< bool: a request (tx) or an answer (rx).
    };

    /// @brief The default row limit (the spec's example: 100 000 trace lines).
    static constexpr int kDefaultRows = 100000;

    /// @brief The default byte limit of the bytes held.
    static constexpr qint64 kDefaultBytes = 32ll * 1024 * 1024;

    /// @brief Bytes of a chunk shown in the hex and ASCII columns; the tooltip shows more.
    static constexpr int kShownBytes = 48;

    /// @brief Bytes of a chunk in the tooltip and in copied text.
    static constexpr int kDetailBytes = 4096;

    /**
     * @brief Creates an empty model with the default limits.
     * @param[in] parent Qt parent.
     */
    explicit TraceModel(QObject* parent = nullptr);

    /**
     * @brief Sets the limits; rows beyond them are dropped, oldest first.
     * @param[in] rows The most rows kept, at least 1.
     * @param[in] bytes The most chunk bytes kept, at least 1.
     */
    void setCapacity(int rows, qint64 bytes);

    /// @brief The row limit.
    /// @return The most rows kept.
    int capacityRows() const noexcept { return m_capRows; }

    /// @brief The byte limit.
    /// @return The most chunk bytes kept.
    qint64 capacityBytes() const noexcept { return m_capBytes; }

    /**
     * @brief Appends a batch.
     *
     * While paused the batch is only counted (`skippedWhilePaused()`).
     *
     * @param[in] frames The chunks, oldest first.
     */
    void append(const QVector<FrameRecord>& frames);

    /// @brief Removes every row; the counters restart.
    void clear();

    /// @brief Stops (or resumes) taking rows; chunks that arrive while paused are counted only.
    /// @param[in] paused true to pause.
    void setPaused(bool paused);

    /// @brief Whether new chunks are being ignored.
    /// @return true while paused.
    bool paused() const noexcept { return m_paused; }

    /// @brief Adds to the count of chunks the runner left out of its batches.
    /// @param[in] total The runner's running total of dropped chunks.
    void setDroppedByRunner(quint64 total) { m_droppedByRunner = total; }

    /// @brief Chunks the runner left out because the GUI was behind.
    /// @return The running total reported by the runner.
    quint64 droppedByRunner() const noexcept { return m_droppedByRunner; }

    /// @brief Chunks that arrived while paused.
    /// @return The count.
    quint64 skippedWhilePaused() const noexcept { return m_skippedWhilePaused; }

    /// @brief Rows removed because a limit was reached.
    /// @return The count.
    quint64 evicted() const noexcept { return m_evicted; }

    /// @brief Chunks seen since the model was created or cleared (kept or not).
    /// @return The count.
    quint64 totalSeen() const noexcept { return m_nextSeq; }

    /// @brief Chunk bytes held now.
    /// @return The sum of the chunk sizes.
    qint64 heldBytes() const noexcept { return m_bytes; }

    /// @brief An estimate of the memory the model holds (chunk bytes, notes and row overhead).
    /// @return Bytes.
    qint64 memoryBytes() const noexcept { return m_memory; }

    /// @brief The whole chunk of a row.
    /// @param[in] row Row index.
    /// @return The bytes; empty when out of range.
    QByteArray bytesAt(int row) const;

    /// @brief Rows as text lines (`seq  ns  dir  len  hex | ascii  note`), for copy and save.
    /// @param[in] first First row.
    /// @param[in] last Last row, inclusive.
    /// @return One line per row.
    QString textOf(int first, int last) const;

    /// @brief Row count.
    /// @param[in] parent Ignored.
    /// @return The rows held.
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;

    /// @brief Column count.
    /// @param[in] parent Ignored.
    /// @return `ColCount`.
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;

    /// @brief Cell data.
    /// @param[in] index The cell.
    /// @param[in] role Display, tooltip, alignment or one of `Role`.
    /// @return The value.
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    /// @brief Column titles.
    /// @param[in] section Column.
    /// @param[in] orientation Horizontal only.
    /// @param[in] role Display.
    /// @return The title.
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    struct Row {
        quint64 seq{0};
        qint64 tNs{0};
        bool tx{true};
        FrameEdge edge{FrameEdge::Unknown};
        QByteArray bytes;
        QString note;
    };

    static qint64 costOf(const Row& row) noexcept;
    static QString hexOf(const QByteArray& bytes, int limit);
    static QString asciiOf(const QByteArray& bytes, int limit);

    std::deque<Row> m_rows;
    int m_capRows{kDefaultRows};
    qint64 m_capBytes{kDefaultBytes};
    qint64 m_bytes{0};
    qint64 m_memory{0};
    quint64 m_nextSeq{0};
    quint64 m_evicted{0};
    quint64 m_skippedWhilePaused{0};
    quint64 m_droppedByRunner{0};
    bool m_paused{false};
};

} // namespace mc::workbench
