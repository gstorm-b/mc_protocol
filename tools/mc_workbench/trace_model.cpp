#include "mc_workbench/trace_model.h"

#include <QtGlobal>

#include <algorithm>

namespace mc::workbench {

namespace {

constexpr qint64 kRowOverhead = 96; // the Row's own size plus the deque's bookkeeping, estimated

} // namespace

TraceModel::TraceModel(QObject* parent) : QAbstractTableModel(parent) {}

qint64 TraceModel::costOf(const Row& row) noexcept {
    return kRowOverhead + row.bytes.size() + row.note.size() * 2;
}

void TraceModel::setCapacity(int rows, qint64 bytes) {
    m_capRows = std::max(1, rows);
    m_capBytes = std::max<qint64>(1, bytes);
    // Apply the new limits to what is held: drop the oldest rows.
    int remove = 0;
    qint64 bytesLeft = m_bytes;
    int rowsLeft = static_cast<int>(m_rows.size());
    while (remove < static_cast<int>(m_rows.size()) && (rowsLeft > m_capRows || bytesLeft > m_capBytes)) {
        bytesLeft -= m_rows[static_cast<size_t>(remove)].bytes.size();
        --rowsLeft;
        ++remove;
    }
    if (remove > 0) {
        beginRemoveRows(QModelIndex(), 0, remove - 1);
        for (int i = 0; i < remove; ++i) {
            m_bytes -= m_rows.front().bytes.size();
            m_memory -= costOf(m_rows.front());
            m_rows.pop_front();
        }
        m_evicted += static_cast<quint64>(remove);
        endRemoveRows();
    }
}

void TraceModel::append(const QVector<FrameRecord>& frames) {
    if (frames.isEmpty()) {
        return;
    }
    if (m_paused) {
        m_skippedWhilePaused += static_cast<quint64>(frames.size());
        return;
    }
    const int count = static_cast<int>(frames.size());
    const quint64 firstSeq = m_nextSeq;
    m_nextSeq += static_cast<quint64>(count);

    // Only the newest chunks can fit: skip the older part of a batch that is bigger than a limit.
    int start = std::max(0, count - m_capRows);
    qint64 incoming = 0;
    for (int i = start; i < count; ++i) {
        incoming += frames[i].bytes.size();
    }
    while (start < count && incoming > m_capBytes) {
        incoming -= frames[start].bytes.size();
        ++start;
    }
    m_evicted += static_cast<quint64>(start);
    const int add = count - start;
    if (add == 0) {
        return;
    }

    // Drop the oldest held rows until the new ones fit.
    int remove = 0;
    qint64 bytesLeft = m_bytes;
    int rowsLeft = static_cast<int>(m_rows.size());
    while (remove < static_cast<int>(m_rows.size()) &&
           (rowsLeft + add > m_capRows || bytesLeft + incoming > m_capBytes)) {
        bytesLeft -= m_rows[static_cast<size_t>(remove)].bytes.size();
        --rowsLeft;
        ++remove;
    }
    if (remove > 0) {
        beginRemoveRows(QModelIndex(), 0, remove - 1);
        for (int i = 0; i < remove; ++i) {
            m_bytes -= m_rows.front().bytes.size();
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
        row.tNs = frames[i].tNs;
        row.tx = frames[i].tx;
        row.edge = frames[i].edge;
        row.bytes = frames[i].bytes;
        row.note = frames[i].note;
        m_bytes += row.bytes.size();
        m_memory += costOf(row);
        m_rows.push_back(std::move(row));
    }
    endInsertRows();
}

void TraceModel::clear() {
    beginResetModel();
    m_rows.clear();
    m_bytes = 0;
    m_memory = 0;
    m_nextSeq = 0;
    m_evicted = 0;
    m_skippedWhilePaused = 0;
    endResetModel();
}

void TraceModel::setPaused(bool paused) {
    m_paused = paused;
}

QByteArray TraceModel::bytesAt(int row) const {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return QByteArray();
    }
    return m_rows[static_cast<size_t>(row)].bytes;
}

QString TraceModel::hexOf(const QByteArray& bytes, int limit) {
    const QByteArray head = bytes.left(limit);
    QString text = QString::fromLatin1(head.toHex(' ')).toUpper();
    if (bytes.size() > limit) {
        text += QStringLiteral(" ...");
    }
    return text;
}

QString TraceModel::asciiOf(const QByteArray& bytes, int limit) {
    const QByteArray head = bytes.left(limit);
    QString text;
    text.reserve(head.size() + 4);
    for (const char byte : head) {
        const unsigned char u = static_cast<unsigned char>(byte);
        text += (u >= 0x20 && u < 0x7F) ? QChar(u) : QChar(u'.');
    }
    if (bytes.size() > limit) {
        text += QStringLiteral("...");
    }
    return text;
}

QString TraceModel::textOf(int first, int last) const {
    QString out;
    const int begin = std::max(0, first);
    const int end = std::min(last, static_cast<int>(m_rows.size()) - 1);
    for (int i = begin; i <= end; ++i) {
        const Row& row = m_rows[static_cast<size_t>(i)];
        out += QStringLiteral("%1\t%2\t%3\t%4\t%5 | %6")
                   .arg(row.seq)
                   .arg(row.tNs)
                   .arg(row.tx ? QStringLiteral("TX") : QStringLiteral("RX"))
                   .arg(row.bytes.size())
                   .arg(hexOf(row.bytes, kDetailBytes), asciiOf(row.bytes, kDetailBytes));
        if (!row.note.isEmpty()) {
            out += QStringLiteral("\t") + row.note;
        }
        out += QLatin1Char('\n');
    }
    return out;
}

int TraceModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

int TraceModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColCount;
}

QVariant TraceModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_rows.size())) {
        return QVariant();
    }
    const Row& row = m_rows[static_cast<size_t>(index.row())];
    if (role == RawBytesRole) {
        return row.bytes;
    }
    if (role == IsTxRole) {
        return row.tx;
    }
    if (role == Qt::TextAlignmentRole) {
        const bool number = index.column() == ColSeq || index.column() == ColTime ||
                            index.column() == ColLen;
        return static_cast<int>(number ? (Qt::AlignRight | Qt::AlignVCenter)
                                       : (Qt::AlignLeft | Qt::AlignVCenter));
    }
    if (role == Qt::ToolTipRole && (index.column() == ColHex || index.column() == ColAscii)) {
        return index.column() == ColHex ? hexOf(row.bytes, kDetailBytes)
                                        : asciiOf(row.bytes, kDetailBytes);
    }
    if (role != Qt::DisplayRole) {
        return QVariant();
    }
    switch (index.column()) {
    case ColSeq:
        return static_cast<qulonglong>(row.seq);
    case ColTime:
        return static_cast<qlonglong>(row.tNs);
    case ColDir:
        return row.tx ? QStringLiteral("TX") : QStringLiteral("RX");
    case ColLen:
        return static_cast<int>(row.bytes.size());
    case ColFrame:
        return row.edge == FrameEdge::Complete  ? QStringLiteral("frame")
               : row.edge == FrameEdge::Partial ? QStringLiteral("part")
                                                : QString();
    case ColHex:
        return hexOf(row.bytes, kShownBytes);
    case ColAscii:
        return asciiOf(row.bytes, kShownBytes);
    case ColNote:
        return row.note;
    default:
        return QVariant();
    }
}

QVariant TraceModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QVariant();
    }
    switch (section) {
    case ColSeq:
        return QStringLiteral("#");
    case ColTime:
        return QStringLiteral("t (ns)");
    case ColDir:
        return QStringLiteral("Dir");
    case ColLen:
        return QStringLiteral("Len");
    case ColFrame:
        return QStringLiteral("Frame");
    case ColHex:
        return QStringLiteral("Hex");
    case ColAscii:
        return QStringLiteral("ASCII");
    case ColNote:
        return QStringLiteral("Decode");
    default:
        return QVariant();
    }
}

} // namespace mc::workbench
