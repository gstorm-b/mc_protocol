#include "mc_workbench/log_model.h"

#include <algorithm>

namespace mc::workbench {

namespace {

constexpr qint64 kRowOverhead = 120; // the Row, three QStrings and the deque, estimated

} // namespace

LogModel::LogModel(QObject* parent) : QAbstractTableModel(parent) {}

qint64 LogModel::costOf(const Row& row) noexcept {
    return kRowOverhead + (row.tab.size() + row.category.size() + row.message.size()) * 2;
}

void LogModel::setCapacity(int rows) {
    m_capRows = std::max(1, rows);
    const int remove = static_cast<int>(m_rows.size()) - m_capRows;
    if (remove > 0) {
        beginRemoveRows(QModelIndex(), 0, remove - 1);
        for (int i = 0; i < remove; ++i) {
            m_memory -= costOf(m_rows.front());
            m_rows.pop_front();
        }
        m_evicted += static_cast<quint64>(remove);
        endRemoveRows();
    }
}

void LogModel::append(const QString& tab, const QVector<LogLine>& lines) {
    if (lines.isEmpty()) {
        return;
    }
    if (!m_tabs.contains(tab)) {
        m_tabs.append(tab);
        emit tabAdded(tab);
    }
    const int count = static_cast<int>(lines.size());
    const quint64 firstSeq = m_nextSeq;
    m_nextSeq += static_cast<quint64>(count);
    const int start = std::max(0, count - m_capRows);
    m_evicted += static_cast<quint64>(start);
    const int add = count - start;

    const int remove =
        std::max(0, std::min(static_cast<int>(m_rows.size()),
                             static_cast<int>(m_rows.size()) + add - m_capRows));
    if (remove > 0) {
        beginRemoveRows(QModelIndex(), 0, remove - 1);
        for (int i = 0; i < remove; ++i) {
            m_memory -= costOf(m_rows.front());
            m_rows.pop_front();
        }
        m_evicted += static_cast<quint64>(remove);
        endRemoveRows();
    }
    const int first = static_cast<int>(m_rows.size());
    beginInsertRows(QModelIndex(), first, first + add - 1);
    for (int i = start; i < count; ++i) {
        Row row;
        row.seq = firstSeq + static_cast<quint64>(i);
        row.tNs = lines[i].tNs;
        row.level = lines[i].level;
        row.tab = tab;
        row.category = lines[i].category;
        row.message = lines[i].message.size() > kMaxMessageChars
                          ? lines[i].message.left(kMaxMessageChars) + QStringLiteral("...")
                          : lines[i].message;
        m_memory += costOf(row);
        m_rows.push_back(std::move(row));
    }
    endInsertRows();
}

void LogModel::clear() {
    beginResetModel();
    m_rows.clear();
    m_memory = 0;
    m_nextSeq = 0;
    m_evicted = 0;
    endResetModel();
}

QStringList LogModel::tabs() const {
    QStringList sorted = m_tabs;
    sorted.sort(Qt::CaseInsensitive);
    return sorted;
}

QString LogModel::levelName(mc::LogLevel level) {
    switch (level) {
    case mc::LogLevel::Trace:
        return QStringLiteral("Trace");
    case mc::LogLevel::Debug:
        return QStringLiteral("Debug");
    case mc::LogLevel::Info:
        return QStringLiteral("Info");
    case mc::LogLevel::Warn:
        return QStringLiteral("Warn");
    case mc::LogLevel::Error:
        return QStringLiteral("Error");
    case mc::LogLevel::Off:
        return QStringLiteral("Off");
    }
    return QString();
}

QString LogModel::lineText(int row) const {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return QString();
    }
    const Row& r = m_rows[static_cast<size_t>(row)];
    return QStringLiteral("%1\t%2\t%3\t%4\t%5")
        .arg(r.tNs)
        .arg(levelName(r.level), r.tab, r.category, r.message);
}

int LogModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

int LogModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColCount;
}

QVariant LogModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_rows.size())) {
        return QVariant();
    }
    const Row& row = m_rows[static_cast<size_t>(index.row())];
    if (role == LevelRole) {
        return static_cast<int>(row.level);
    }
    if (role == TabRole) {
        return row.tab;
    }
    if (role != Qt::DisplayRole) {
        return QVariant();
    }
    switch (index.column()) {
    case ColSeq:
        return static_cast<qulonglong>(row.seq);
    case ColTime:
        return static_cast<qlonglong>(row.tNs);
    case ColLevel:
        return levelName(row.level);
    case ColTab:
        return row.tab;
    case ColCategory:
        return row.category;
    case ColMessage:
        return row.message;
    default:
        return QVariant();
    }
}

QVariant LogModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QVariant();
    }
    switch (section) {
    case ColSeq:
        return QStringLiteral("#");
    case ColTime:
        return QStringLiteral("t (ns)");
    case ColLevel:
        return QStringLiteral("Level");
    case ColTab:
        return QStringLiteral("Tab");
    case ColCategory:
        return QStringLiteral("Category");
    case ColMessage:
        return QStringLiteral("Message");
    default:
        return QVariant();
    }
}

LogFilterModel::LogFilterModel(QObject* parent) : QSortFilterProxyModel(parent) {}

// Qt 6.9 deprecates invalidateFilter() for begin/endFilterChange(); the GUI supports Qt 6.5 and up.
void LogFilterModel::beginChange() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    beginFilterChange();
#endif
}

void LogFilterModel::endChange() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateFilter();
#endif
}

void LogFilterModel::setMinLevel(mc::LogLevel level) {
    beginChange();
    m_minLevel = level;
    endChange();
}

void LogFilterModel::setTab(const QString& tab) {
    beginChange();
    m_tab = tab;
    endChange();
}

void LogFilterModel::setSearch(const QString& text) {
    beginChange();
    m_search = text;
    endChange();
}

bool LogFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex&) const {
    const QAbstractItemModel* source = sourceModel();
    if (source == nullptr) {
        return false;
    }
    const QModelIndex any = source->index(sourceRow, 0);
    if (source->data(any, LogModel::LevelRole).toInt() < static_cast<int>(m_minLevel)) {
        return false;
    }
    if (!m_tab.isEmpty() && source->data(any, LogModel::TabRole).toString() != m_tab) {
        return false;
    }
    if (!m_search.isEmpty()) {
        const QString category =
            source->data(source->index(sourceRow, LogModel::ColCategory)).toString();
        const QString message =
            source->data(source->index(sourceRow, LogModel::ColMessage)).toString();
        if (!category.contains(m_search, Qt::CaseInsensitive) &&
            !message.contains(m_search, Qt::CaseInsensitive)) {
            return false;
        }
    }
    return true;
}

QString LogFilterModel::shownText() const {
    const auto* log = qobject_cast<const LogModel*>(sourceModel());
    QString out;
    if (log == nullptr) {
        return out;
    }
    for (int i = 0; i < rowCount(); ++i) {
        out += log->lineText(mapToSource(index(i, 0)).row());
        out += QLatin1Char('\n');
    }
    return out;
}

} // namespace mc::workbench
