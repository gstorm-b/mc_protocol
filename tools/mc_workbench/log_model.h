/**
 * @file log_model.h
 * @brief `LogModel` and `LogFilterModel`: the bounded ring of debug log lines of every tab, and
 * the level / tab / search filter over it.
 */
#pragma once

#include "mc/core/log.h"
#include "mc_workbench/runner_types.h"

#include <QAbstractTableModel>
#include <QSortFilterProxyModel>
#include <QString>
#include <QStringList>
#include <QVector>

#include <deque>

namespace mc::workbench {

/**
 * @brief The log lines of every session, device and mock, newest last, in a ring bounded by a
 * row count (configurable).
 *
 * @note GUI thread only. Holds value copies only.
 */
class LogModel : public QAbstractTableModel {
    Q_OBJECT
public:
    /// @brief Columns of the table.
    enum Column {
        ColSeq,      ///< Running number of the line.
        ColTime,     ///< Nanoseconds on the source's clock.
        ColLevel,    ///< Trace, Debug, Info, Warn or Error.
        ColTab,      ///< The tab the line came from.
        ColCategory, ///< Subsystem, e.g. "mc.session".
        ColMessage,  ///< The text.
        ColCount
    };

    /// @brief Extra data roles.
    enum Role {
        LevelRole = Qt::UserRole + 1, ///< The `mc::LogLevel` as an int, for filters and delegates.
        TabRole                        ///< The tab name.
    };

    /// @brief The default row limit.
    static constexpr int kDefaultRows = 100000;

    /// @brief A message longer than this is cut when stored.
    static constexpr int kMaxMessageChars = 4000;

    /**
     * @brief Creates an empty model with the default limit.
     * @param[in] parent Qt parent.
     */
    explicit LogModel(QObject* parent = nullptr);

    /**
     * @brief Sets the row limit; older rows beyond it are dropped.
     * @param[in] rows The most rows kept, at least 1.
     */
    void setCapacity(int rows);

    /// @brief The row limit.
    /// @return The most rows kept.
    int capacityRows() const noexcept { return m_capRows; }

    /**
     * @brief Appends lines of one source.
     * @param[in] tab Name of the tab the lines belong to.
     * @param[in] lines The lines, oldest first.
     */
    void append(const QString& tab, const QVector<LogLine>& lines);

    /// @brief Removes every row.
    void clear();

    /// @brief Rows removed because the limit was reached.
    /// @return The count.
    quint64 evicted() const noexcept { return m_evicted; }

    /// @brief Lines seen since the model was created or cleared.
    /// @return The count.
    quint64 totalSeen() const noexcept { return m_nextSeq; }

    /// @brief An estimate of the memory held.
    /// @return Bytes.
    qint64 memoryBytes() const noexcept { return m_memory; }

    /// @brief The tabs that have written a line, sorted.
    /// @return Their names.
    QStringList tabs() const;

    /// @brief The name of a level.
    /// @param[in] level The level.
    /// @return "Trace", "Debug", "Info", "Warn" or "Error".
    static QString levelName(mc::LogLevel level);

    /// @brief One line as `ns  level  tab  category  message`.
    /// @param[in] row Row index.
    /// @return The text; empty when out of range.
    QString lineText(int row) const;

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
    /// @param[in] role Display or one of `Role`.
    /// @return The value.
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    /// @brief Column titles.
    /// @param[in] section Column.
    /// @param[in] orientation Horizontal only.
    /// @param[in] role Display.
    /// @return The title.
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

signals:
    /// @brief A tab name was seen for the first time (feeds the tab filter).
    /// @param[out] tab The name.
    void tabAdded(const QString& tab);

private:
    struct Row {
        quint64 seq{0};
        qint64 tNs{0};
        mc::LogLevel level{mc::LogLevel::Info};
        QString tab;
        QString category;
        QString message;
    };

    static qint64 costOf(const Row& row) noexcept;

    std::deque<Row> m_rows;
    QStringList m_tabs;
    int m_capRows{kDefaultRows};
    qint64 m_memory{0};
    quint64 m_nextSeq{0};
    quint64 m_evicted{0};
};

/**
 * @brief Shows the lines at or above a level, of one tab (or all), that contain a text.
 */
class LogFilterModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    /**
     * @brief Creates the filter; it accepts every line until told otherwise.
     * @param[in] parent Qt parent.
     */
    explicit LogFilterModel(QObject* parent = nullptr);

    /// @brief Hides lines below @p level.
    /// @param[in] level The lowest level shown.
    void setMinLevel(mc::LogLevel level);

    /// @brief Shows one tab only.
    /// @param[in] tab The tab name; empty shows every tab.
    void setTab(const QString& tab);

    /// @brief Shows lines whose category or message contains @p text (case-insensitive).
    /// @param[in] text The text; empty shows every line.
    void setSearch(const QString& text);

    /// @brief All the shown lines as text, one per line.
    /// @return The text.
    QString shownText() const;

protected:
    /// @brief The filter.
    /// @param[in] sourceRow Row of the log model.
    /// @param[in] sourceParent Ignored.
    /// @return true when the line is shown.
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    void beginChange();
    void endChange();

    mc::LogLevel m_minLevel{mc::LogLevel::Trace};
    QString m_tab;
    QString m_search;
};

} // namespace mc::workbench
