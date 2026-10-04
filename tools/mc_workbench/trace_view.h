/**
 * @file trace_view.h
 * @brief `TraceView`: the table of one tab's wire chunks with its toolbar (pause, clear, decode,
 * ring size, copy, save).
 */
#pragma once

#include "mc_workbench/tab_telemetry.h"

#include <QPointer>
#include <QString>
#include <QWidget>

class QCheckBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableView;
class QTimer;

namespace mc::workbench {

class AsyncFileWriter;

/**
 * @brief Shows the `TraceModel` of a `TabTelemetry`: TX / RX chunks with nanosecond stamps, hex and
 * ASCII, frame boundaries and the decoder's text.
 *
 * Only a bounded copy lives here (the model's ring, whose size the "Rows" box sets). Saving writes
 * a text snapshot on a worker thread.
 *
 * @note GUI thread only.
 */
class TraceView : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Builds the view; it shows nothing until setTelemetry().
     * @param[in] parent Parent widget.
     */
    explicit TraceView(QWidget* parent = nullptr);

    /// @brief Waits (bounded) for a save in progress.
    ~TraceView() override;

    /**
     * @brief Shows the trace of @p telemetry.
     * @param[in] telemetry The tab's telemetry; may be null (an empty table). Not owned.
     */
    void setTelemetry(TabTelemetry* telemetry);

    /// @brief The telemetry shown.
    /// @return Null when none.
    TabTelemetry* telemetry() const { return m_telemetry; }

    /// @brief The table, for tests.
    /// @return Never null.
    QTableView* table() const noexcept { return m_table; }

    /// @brief The selected rows (or all rows when none is selected) as text.
    /// @return Text lines as `TraceModel::textOf()` writes them.
    QString copyText() const;

    /// @brief Puts copyText() on the clipboard.
    void copyToClipboard();

    /**
     * @brief Saves every row as text to @p path on a worker thread.
     * @param[in] path The file.
     */
    void saveTo(const QString& path);

    /// @brief The "N rows, ..." line under the table.
    /// @return Plain text.
    QString statusText() const;

signals:
    /// @brief A save ended.
    /// @param[out] path The file.
    /// @param[out] ok Whether it was written.
    /// @param[out] message The error text; empty on success.
    void saved(const QString& path, bool ok, const QString& message);

private:
    void updateStatus();
    void applyCapacity();

    QPointer<TabTelemetry> m_telemetry;
    QTableView* m_table;
    QPushButton* m_pause;
    QCheckBox* m_decode;
    QCheckBox* m_follow;
    QSpinBox* m_rows;
    QLabel* m_status;
    QTimer* m_statusTimer;
    QTimer* m_scrollTimer;
    AsyncFileWriter* m_writer;
};

} // namespace mc::workbench
