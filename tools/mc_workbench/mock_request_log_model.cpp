#include "mc_workbench/mock_request_log_model.h"

#include <algorithm>

namespace mc::workbench {

MockRequestLogModel::MockRequestLogModel(QObject* parent) : QAbstractTableModel(parent) {}

void MockRequestLogModel::setCapacity(int capacity) {
    m_capacity = std::max(1, capacity);
    const int surplus = static_cast<int>(m_rows.size()) - m_capacity;
    if (surplus > 0) {
        beginRemoveRows(QModelIndex(), 0, surplus - 1);
        m_rows.erase(m_rows.begin(), m_rows.begin() + surplus);
        m_dropped += static_cast<quint64>(surplus);
        endRemoveRows();
    }
}

void MockRequestLogModel::append(const MockRequestBatch& batch) {
    m_dropped += batch.dropped;
    if (batch.entries.isEmpty()) {
        return;
    }
    // A batch larger than the capacity keeps only its newest entries.
    const int incoming = static_cast<int>(batch.entries.size());
    const int skip = std::max(0, incoming - m_capacity);
    m_dropped += static_cast<quint64>(skip);
    const int add = incoming - skip;

    const int overflow = static_cast<int>(m_rows.size()) + add - m_capacity;
    if (overflow > 0) {
        const int remove = std::min(overflow, static_cast<int>(m_rows.size()));
        beginRemoveRows(QModelIndex(), 0, remove - 1);
        m_rows.erase(m_rows.begin(), m_rows.begin() + remove);
        m_dropped += static_cast<quint64>(remove);
        endRemoveRows();
    }
    const int first = static_cast<int>(m_rows.size());
    beginInsertRows(QModelIndex(), first, first + add - 1);
    for (int i = skip; i < incoming; ++i) {
        m_rows.push_back(batch.entries.at(i));
    }
    endInsertRows();
}

void MockRequestLogModel::clear() {
    beginResetModel();
    m_rows.clear();
    m_dropped = 0;
    endResetModel();
}

MockRequestEntry MockRequestLogModel::entry(int row) const {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return {};
    }
    return m_rows[static_cast<size_t>(row)];
}

int MockRequestLogModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

int MockRequestLogModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant MockRequestLogModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(m_rows.size())) {
        return {};
    }
    const MockRequestEntry& e = m_rows[static_cast<size_t>(index.row())];
    if (role != Qt::DisplayRole) {
        return {};
    }
    switch (index.column()) {
    case Seq:
        return e.seq;
    case Time:
        return QStringLiteral("%1 ms").arg(static_cast<double>(e.tNs) / 1e6, 0, 'f', 1);
    case Operation:
        return e.op;
    case Device:
        return e.head;
    case Count:
        return e.count;
    case Result:
        if (!e.answered) {
            return QStringLiteral("no answer");
        }
        if (e.errorCode == 0) {
            return QStringLiteral("ok");
        }
        return QStringLiteral("error 0x%1 %2")
            .arg(QString::number(e.plcCode, 16).toUpper().rightJustified(4, QLatin1Char('0')))
            .arg(e.message);
    default:
        return {};
    }
}

QVariant MockRequestLogModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case Seq:
        return QStringLiteral("#");
    case Time:
        return QStringLiteral("Time");
    case Operation:
        return QStringLiteral("Operation");
    case Device:
        return QStringLiteral("Device");
    case Count:
        return QStringLiteral("Points");
    case Result:
        return QStringLiteral("Result");
    default:
        return {};
    }
}

} // namespace mc::workbench
