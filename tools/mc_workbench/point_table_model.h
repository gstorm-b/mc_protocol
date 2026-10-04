/**
 * @file point_table_model.h
 * @brief `PointTableModel`: the subscribed points of a device tab with their live values, the
 * highlight of changed values and the trend selection.
 */
#pragma once

#include "mc/core/frame_config.h"
#include "mc/core/value_store.h"
#include "mc_workbench/runner_types.h"

#include <QAbstractTableModel>
#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVector>

namespace mc::workbench {

/**
 * @brief One row per subscribed point (columns Device, Value, Signed, Hex, State, Trend).
 *
 * Fed with the `ValueBatch` copies a `DeviceHost` emits: snapshots give the layout and the current
 * values, changes mark the points that changed (also those that changed and changed back between
 * two batches). A changed point is highlighted for kHighlightMs. The Trend column is a check box;
 * a checked point reports its value through `trendSample()` with every snapshot.
 *
 * @note GUI thread only; no pointer into a runner is ever held.
 */
class PointTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    /// @brief Columns of the table.
    enum Column { DeviceColumn, ValueColumn, SignedColumn, HexColumn, StateColumn, TrendColumn,
                  ColumnCount };

    /// @brief Role that tells whether a row is highlighted as changed (bool).
    static constexpr int HighlightRole = Qt::UserRole + 1;

    /// @brief How long a changed value stays highlighted.
    static constexpr int kHighlightMs = 1500;

    /// @brief Creates an empty table.
    /// @param[in] parent Qt parent.
    explicit PointTableModel(QObject* parent = nullptr);

    /// @brief Sets the base of X and Y numbers shown in the Device column.
    /// @param[in] xy The notation of the frame (Hex, or Octal for FX CPUs).
    void setXyNotation(mc::XyNumbering xy);

    /// @brief Takes the newest values.
    /// @param[in] batch Snapshots and changes of one emit of the runner.
    void applyBatch(const ValueBatch& batch);

    /// @brief Marks every point Stale (the link went down); the values stay.
    void markStale();

    /// @brief Removes every row (the device was rebuilt); the trend selection stays.
    void clear();

    /// @brief Looks a point up by its text.
    /// @param[in] device Device text as shown, e.g. "D100".
    /// @return The row, or -1.
    int rowOfDevice(const QString& device) const;

    /// @brief The text of a row's device.
    /// @param[in] row The row.
    /// @return For example "D100".
    QString deviceText(int row) const;

    /// @brief The value of a word row (or 0/1 of a bit row).
    /// @param[in] row The row.
    /// @return The raw value.
    quint16 valueAt(int row) const;

    /// @brief The point state of a row.
    /// @param[in] row The row.
    /// @return The state.
    mc::PointState stateAt(int row) const;

    /// @brief Whether a row was changed within kHighlightMs.
    /// @param[in] row The row.
    /// @return true while the highlight lasts.
    bool isHighlighted(int row) const;

    /// @brief Adds or removes a point from the trend.
    /// @param[in] device Device text, e.g. "D100".
    /// @param[in] on true to trend it.
    void setTrend(const QString& device, bool on);

    /// @brief Whether a point is in the trend.
    /// @param[in] device Device text.
    /// @return true when selected.
    bool isTrended(const QString& device) const { return m_trend.contains(device); }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

signals:
    /// @brief A trended point was read.
    /// @param[out] device The point, e.g. "D100".
    /// @param[out] value The value (0 or 1 for a bit).
    void trendSample(const QString& device, double value);

    /// @brief The trend selection changed.
    /// @param[out] device The point.
    /// @param[out] on Whether it is trended now.
    void trendToggled(const QString& device, bool on);

private:
    struct Row {
        mc::Device device;
        QString text;
        bool bit{false};
        quint16 value{0};
        quint8 state{static_cast<quint8>(mc::PointState::NoValue)};
        bool hasValue{false};
        qint64 changedAtMs{-1};
    };

    QString textOf(const mc::Device& device) const;
    void applySnapshot(const mc::DeviceSnapshot& snapshot);
    void markChanged(const ValueUpdate& update);
    void rebuildIndex();
    void fadeTick();
    bool anyHighlighted() const;

    QVector<Row> m_rows;
    QHash<quint64, int> m_index; ///< device key -> row
    QSet<QString> m_trend;
    mc::XyNumbering m_xy{mc::XyNumbering::Hex};
    QElapsedTimer m_clock;
    QTimer m_fade;
};

} // namespace mc::workbench
