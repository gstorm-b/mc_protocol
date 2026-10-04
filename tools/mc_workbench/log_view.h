/**
 * @file log_view.h
 * @brief `LogView`: the debug log of every tab with level and tab filters, search, copy and save.
 */
#pragma once

#include <QString>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTableView;

namespace mc::workbench {

class AsyncFileWriter;
class LogFilterModel;
class LogModel;

/**
 * @brief Shows a `LogModel` through a level / tab / text filter.
 *
 * Only a bounded copy lives here (the model's ring, whose size the "Rows" box sets). Saving writes
 * the shown lines on a worker thread.
 *
 * @note GUI thread only.
 */
class LogView : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Builds the view over @p model.
     * @param[in] model The shared log; not owned, must outlive the view.
     * @param[in] parent Parent widget.
     */
    explicit LogView(LogModel* model, QWidget* parent = nullptr);

    /// @brief Waits (bounded) for a save in progress.
    ~LogView() override;

    /// @brief The filter over the log, for tests.
    /// @return Never null.
    LogFilterModel* filter() const noexcept { return m_filter; }

    /// @brief The table, for tests.
    /// @return Never null.
    QTableView* table() const noexcept { return m_table; }

    /// @brief The shown lines as text (the selected ones when any are selected).
    /// @return One line per row.
    QString copyText() const;

    /// @brief Puts copyText() on the clipboard.
    void copyToClipboard();

    /**
     * @brief Saves the shown lines to @p path on a worker thread.
     * @param[in] path The file.
     */
    void saveTo(const QString& path);

    /// @brief Sets the lowest level shown, as the level box would.
    /// @param[in] levelName "Trace", "Debug", "Info", "Warn" or "Error".
    void setLevelFilter(const QString& levelName);

    /// @brief Shows one tab, as the tab box would.
    /// @param[in] tab Tab name; empty for all tabs.
    void setTabFilter(const QString& tab);

    /// @brief Sets the search text, as the search box would.
    /// @param[in] text Text to look for in category and message.
    void setSearchText(const QString& text);

signals:
    /// @brief A save ended.
    /// @param[out] path The file.
    /// @param[out] ok Whether it was written.
    /// @param[out] message The error text; empty on success.
    void saved(const QString& path, bool ok, const QString& message);

private:
    void updateStatus();
    void refillTabs();

    LogModel* m_model;
    LogFilterModel* m_filter;
    QTableView* m_table;
    QComboBox* m_level;
    QComboBox* m_tab;
    QLineEdit* m_search;
    QSpinBox* m_rows;
    QLabel* m_status;
    AsyncFileWriter* m_writer;
};

} // namespace mc::workbench
