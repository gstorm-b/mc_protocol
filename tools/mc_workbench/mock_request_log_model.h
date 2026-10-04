/**
 * @file mock_request_log_model.h
 * @brief `MockRequestLogModel`: a table model of the request log of a mock, with a ring capacity.
 */
#pragma once

#include "mc_workbench/mock_types.h"

#include <QAbstractTableModel>
#include <QVector>

#include <deque>

namespace mc::workbench {

/**
 * @brief The requests a mock saw, newest at the bottom, bounded by a capacity (SPEC-gui-tool.md:
 * views keep bounded ring buffers).
 *
 * Fed from the GUI thread with the batches of `MockHost::requestsLogged`. When the capacity is
 * exceeded the oldest rows are removed; `droppedTotal()` counts what the runner dropped plus what
 * the ring removed.
 *
 * @note GUI thread only.
 */
class MockRequestLogModel : public QAbstractTableModel {
    Q_OBJECT
public:
    /// @brief Columns of the table.
    enum Column { Seq, Time, Operation, Device, Count, Result, ColumnCount };

    /// @brief Default number of rows kept.
    static constexpr int kDefaultCapacity = 10000;

    /**
     * @brief Creates an empty log.
     * @param[in] parent Qt parent.
     */
    explicit MockRequestLogModel(QObject* parent = nullptr);

    /**
     * @brief Sets how many rows are kept; surplus old rows are removed at once.
     * @param[in] capacity At least 1.
     */
    void setCapacity(int capacity);

    /// @brief Rows kept at most.
    /// @return The capacity.
    int capacity() const noexcept { return m_capacity; }

    /**
     * @brief Appends a batch.
     * @param[in] batch The entries, oldest first; copied.
     */
    void append(const MockRequestBatch& batch);

    /// @brief Removes every row (the dropped counter too).
    void clear();

    /// @brief Entries dropped by the runner or removed by the ring since the last clear().
    /// @return The count.
    quint64 droppedTotal() const noexcept { return m_dropped; }

    /// @brief The entry of a row.
    /// @param[in] row Row, 0 is the oldest.
    /// @return A copy; an empty entry when out of range.
    MockRequestEntry entry(int row) const;

    /// @brief Number of rows.
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    /// @brief Number of columns (`ColumnCount`).
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    /// @brief Text of a cell.
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    /// @brief Column titles.
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    std::deque<MockRequestEntry> m_rows;
    int m_capacity{kDefaultCapacity};
    quint64 m_dropped{0};
};

} // namespace mc::workbench
