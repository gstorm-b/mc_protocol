#include "mc_workbench/point_table_model.h"

#include <QBrush>
#include <QColor>

#include <algorithm>

namespace mc::workbench {

namespace {

quint64 keyOf(const mc::Device& device) {
    return (static_cast<quint64>(device.type) << 32) | device.number;
}

QString stateName(quint8 state) {
    switch (static_cast<mc::PointState>(state)) {
    case mc::PointState::NotSubscribed:
        return QStringLiteral("Not subscribed");
    case mc::PointState::NoValue:
        return QStringLiteral("No value");
    case mc::PointState::Valid:
        return QStringLiteral("Valid");
    case mc::PointState::Failed:
        return QStringLiteral("Failed");
    case mc::PointState::Stale:
        return QStringLiteral("Stale");
    }
    return QString();
}

bool hasLastValue(quint8 state) {
    const auto s = static_cast<mc::PointState>(state);
    return s == mc::PointState::Valid || s == mc::PointState::Failed || s == mc::PointState::Stale;
}

} // namespace

PointTableModel::PointTableModel(QObject* parent) : QAbstractTableModel(parent) {
    m_clock.start();
    m_fade.setInterval(250);
    connect(&m_fade, &QTimer::timeout, this, &PointTableModel::fadeTick);
}

QString PointTableModel::textOf(const mc::Device& device) const {
    char text[32];
    const size_t needed = mc::formatDevice(device, text, sizeof text, m_xy);
    return needed < sizeof text ? QString::fromLatin1(text, static_cast<int>(needed)) : QString();
}

void PointTableModel::setXyNotation(mc::XyNumbering xy) {
    if (m_xy == xy) {
        return;
    }
    m_xy = xy;
    for (Row& row : m_rows) {
        row.text = textOf(row.device);
    }
    if (!m_rows.isEmpty()) {
        emit dataChanged(index(0, DeviceColumn), index(m_rows.size() - 1, DeviceColumn));
    }
}

void PointTableModel::rebuildIndex() {
    m_index.clear();
    for (int i = 0; i < m_rows.size(); ++i) {
        m_index.insert(keyOf(m_rows[i].device), i);
    }
}

void PointTableModel::applyBatch(const ValueBatch& batch) {
    for (const mc::DeviceSnapshot& snapshot : batch.snapshots) {
        applySnapshot(snapshot);
    }
    for (const ValueUpdate& update : batch.changes) {
        markChanged(update);
    }
    if (!m_trend.isEmpty()) {
        for (const Row& row : m_rows) {
            if (row.state == static_cast<quint8>(mc::PointState::Valid) && m_trend.contains(row.text)) {
                emit trendSample(row.text, static_cast<double>(row.value));
            }
        }
    }
}

void PointTableModel::applySnapshot(const mc::DeviceSnapshot& snapshot) {
    struct Incoming {
        mc::Device device;
        bool bit;
        quint16 value;
        quint8 state;
    };
    QVector<Incoming> incoming;
    for (const mc::SnapshotSegment& segment : snapshot.segments) {
        const bool bit = mc::deviceInfo(snapshot.type).kind == mc::DeviceKind::Bit;
        for (quint32 i = 0; i < segment.count; ++i) {
            Incoming point;
            point.device = mc::Device{snapshot.type, segment.head.number + i};
            point.bit = bit;
            point.state = i < static_cast<quint32>(segment.states.size())
                              ? static_cast<quint8>(segment.states.at(static_cast<int>(i)))
                              : static_cast<quint8>(mc::PointState::NoValue);
            point.value = 0;
            if (bit) {
                if (i < static_cast<quint32>(segment.values.size())) {
                    point.value = segment.values.at(static_cast<int>(i)) != 0 ? 1 : 0;
                }
            } else if (2 * i + 1 < static_cast<quint32>(segment.values.size())) {
                point.value = static_cast<quint16>(
                    static_cast<quint8>(segment.values.at(static_cast<int>(2 * i))) |
                    (static_cast<quint8>(segment.values.at(static_cast<int>(2 * i + 1))) << 8));
            }
            incoming.push_back(point);
        }
    }

    // The rows of this device type are one run (rows are ordered by type).
    int first = 0;
    while (first < m_rows.size() && static_cast<int>(m_rows[first].device.type) < static_cast<int>(snapshot.type)) {
        ++first;
    }
    int last = first;
    while (last < m_rows.size() && m_rows[last].device.type == snapshot.type) {
        ++last;
    }

    bool sameLayout = (last - first) == incoming.size();
    for (int i = 0; sameLayout && i < incoming.size(); ++i) {
        sameLayout = m_rows[first + i].device == incoming[i].device;
    }

    if (!sameLayout) {
        beginResetModel();
        QVector<Row> rebuilt;
        rebuilt.reserve(m_rows.size() - (last - first) + incoming.size());
        for (int i = 0; i < first; ++i) {
            rebuilt.push_back(m_rows[i]);
        }
        for (const Incoming& point : incoming) {
            Row row;
            row.device = point.device;
            row.text = textOf(point.device);
            row.bit = point.bit;
            row.value = point.value;
            row.state = point.state;
            row.hasValue = hasLastValue(point.state);
            rebuilt.push_back(row);
        }
        for (int i = last; i < m_rows.size(); ++i) {
            rebuilt.push_back(m_rows[i]);
        }
        m_rows = rebuilt;
        rebuildIndex();
        endResetModel();
        return;
    }

    int dirtyFrom = -1;
    int dirtyTo = -1;
    for (int i = 0; i < incoming.size(); ++i) {
        Row& row = m_rows[first + i];
        const Incoming& point = incoming[i];
        const bool valueChanged = row.hasValue && hasLastValue(point.state) && row.value != point.value;
        if (row.value != point.value || row.state != point.state) {
            if (valueChanged) {
                row.changedAtMs = m_clock.elapsed();
                if (!m_fade.isActive()) {
                    m_fade.start();
                }
            }
            row.value = point.value;
            row.state = point.state;
            row.hasValue = row.hasValue || hasLastValue(point.state);
            if (dirtyFrom < 0) {
                dirtyFrom = first + i;
            }
            dirtyTo = first + i;
        }
    }
    if (dirtyFrom >= 0) {
        emit dataChanged(index(dirtyFrom, 0), index(dirtyTo, ColumnCount - 1));
    }
}

void PointTableModel::markChanged(const ValueUpdate& update) {
    for (const mc::Change& change : update.changes) {
        const auto it = m_index.constFind(keyOf(change.device));
        if (it == m_index.constEnd()) {
            continue;
        }
        m_rows[it.value()].changedAtMs = m_clock.elapsed();
        emit dataChanged(index(it.value(), 0), index(it.value(), ColumnCount - 1));
        if (!m_fade.isActive()) {
            m_fade.start();
        }
    }
}

void PointTableModel::markStale() {
    int changedRows = 0;
    for (Row& row : m_rows) {
        if (hasLastValue(row.state) && row.state != static_cast<quint8>(mc::PointState::Stale)) {
            row.state = static_cast<quint8>(mc::PointState::Stale);
            ++changedRows;
        }
    }
    if (changedRows > 0) {
        emit dataChanged(index(0, StateColumn), index(m_rows.size() - 1, StateColumn));
    }
}

void PointTableModel::clear() {
    beginResetModel();
    m_rows.clear();
    m_index.clear();
    endResetModel();
}

bool PointTableModel::anyHighlighted() const {
    const qint64 now = m_clock.elapsed();
    return std::any_of(m_rows.cbegin(), m_rows.cend(), [now](const Row& row) {
        return row.changedAtMs >= 0 && now - row.changedAtMs < kHighlightMs;
    });
}

void PointTableModel::fadeTick() {
    if (!m_rows.isEmpty()) {
        emit dataChanged(index(0, 0), index(m_rows.size() - 1, ColumnCount - 1),
                         {Qt::BackgroundRole, Qt::ForegroundRole, HighlightRole});
    }
    if (!anyHighlighted()) {
        m_fade.stop();
    }
}

int PointTableModel::rowOfDevice(const QString& device) const {
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].text.compare(device, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return -1;
}

QString PointTableModel::deviceText(int row) const {
    return m_rows.at(row).text;
}

quint16 PointTableModel::valueAt(int row) const {
    return m_rows.at(row).value;
}

mc::PointState PointTableModel::stateAt(int row) const {
    return static_cast<mc::PointState>(m_rows.at(row).state);
}

bool PointTableModel::isHighlighted(int row) const {
    const Row& r = m_rows.at(row);
    return r.changedAtMs >= 0 && m_clock.elapsed() - r.changedAtMs < kHighlightMs;
}

void PointTableModel::setTrend(const QString& device, bool on) {
    const QString key = device.trimmed().toUpper();
    const bool was = m_trend.contains(key);
    if (on == was) {
        return;
    }
    if (on) {
        m_trend.insert(key);
    } else {
        m_trend.remove(key);
    }
    const int row = rowOfDevice(key);
    if (row >= 0) {
        emit dataChanged(index(row, TrendColumn), index(row, TrendColumn), {Qt::CheckStateRole});
    }
    emit trendToggled(key, on);
}

int PointTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_rows.size();
}

int PointTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant PointTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_rows.size()) {
        return {};
    }
    const Row& row = m_rows[index.row()];
    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case DeviceColumn:
            return row.text;
        case ValueColumn:
            return row.hasValue ? QVariant(QString::number(row.value)) : QVariant(QString());
        case SignedColumn:
            return row.hasValue && !row.bit ? QVariant(QString::number(static_cast<qint16>(row.value)))
                                            : QVariant(QString());
        case HexColumn:
            return row.hasValue && !row.bit
                       ? QVariant(QStringLiteral("0x") + QString::number(row.value, 16).rightJustified(4, QLatin1Char('0')).toUpper())
                       : QVariant(QString());
        case StateColumn:
            return stateName(row.state);
        default:
            return {};
        }
    case Qt::CheckStateRole:
        if (index.column() == TrendColumn) {
            return static_cast<int>(m_trend.contains(row.text) ? Qt::Checked : Qt::Unchecked);
        }
        return {};
    case Qt::TextAlignmentRole:
        if (index.column() == ValueColumn || index.column() == SignedColumn ||
            index.column() == HexColumn) {
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        }
        return {};
    case Qt::BackgroundRole:
        if (isHighlighted(index.row())) {
            return QBrush(QColor(255, 224, 130));
        }
        return {};
    case Qt::ForegroundRole:
        if (isHighlighted(index.row())) {
            return QBrush(QColor(0, 0, 0));
        }
        if (row.state != static_cast<quint8>(mc::PointState::Valid)) {
            return QBrush(QColor(128, 128, 128));
        }
        return {};
    case HighlightRole:
        return isHighlighted(index.row());
    default:
        return {};
    }
}

bool PointTableModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || index.column() != TrendColumn || role != Qt::CheckStateRole ||
        index.row() >= m_rows.size()) {
        return false;
    }
    setTrend(m_rows[index.row()].text, value.toInt() == Qt::Checked);
    return true;
}

Qt::ItemFlags PointTableModel::flags(const QModelIndex& index) const {
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (index.column() == TrendColumn) {
        f |= Qt::ItemIsUserCheckable;
    }
    return f;
}

QVariant PointTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case DeviceColumn:
        return QStringLiteral("Device");
    case ValueColumn:
        return QStringLiteral("Value");
    case SignedColumn:
        return QStringLiteral("Signed");
    case HexColumn:
        return QStringLiteral("Hex");
    case StateColumn:
        return QStringLiteral("State");
    case TrendColumn:
        return QStringLiteral("Trend");
    default:
        return {};
    }
}

} // namespace mc::workbench
