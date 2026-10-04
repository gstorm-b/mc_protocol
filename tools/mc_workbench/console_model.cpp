#include "mc_workbench/console_model.h"

#include <QRegularExpression>
#include <QStringList>
#include <QTime>

namespace mc::workbench {

namespace {

QStringList items(const QString& text) {
    static const QRegularExpression separators(QStringLiteral("[\\s,;]+"));
    return text.trimmed().split(separators, Qt::SkipEmptyParts);
}

} // namespace

bool parseWordList(const QString& text, QVector<quint16>& out, QString* error) {
    QVector<quint16> values;
    const QStringList parts = items(text);
    if (parts.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("no values: type words such as 1 2 0x10");
        }
        return false;
    }
    for (const QString& part : parts) {
        bool ok = false;
        const int base = part.startsWith(QLatin1String("0x"), Qt::CaseInsensitive) ? 16 : 10;
        const qint64 number = part.toLongLong(&ok, base);
        if (!ok || number < -32768 || number > 65535) {
            if (error != nullptr) {
                *error = QStringLiteral("\"%1\" is not a 16-bit word (-32768..65535 or 0x0..0xFFFF)")
                             .arg(part);
            }
            return false;
        }
        values.push_back(static_cast<quint16>(number & 0xFFFF));
    }
    out = values;
    return true;
}

bool parseBitList(const QString& text, QVector<bool>& out, QString* error) {
    QVector<bool> values;
    const QStringList parts = items(text);
    if (parts.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("no values: type bits such as 1 0 1");
        }
        return false;
    }
    for (const QString& part : parts) {
        const QString word = part.toLower();
        if (word == QLatin1String("1") || word == QLatin1String("true") ||
            word == QLatin1String("on")) {
            values.push_back(true);
        } else if (word == QLatin1String("0") || word == QLatin1String("false") ||
                   word == QLatin1String("off")) {
            values.push_back(false);
        } else {
            if (error != nullptr) {
                *error = QStringLiteral("\"%1\" is not a bit (0, 1, on, off, true, false)").arg(part);
            }
            return false;
        }
    }
    out = values;
    return true;
}

ConsoleModel::ConsoleModel(QObject* parent) : QAbstractTableModel(parent) {}

int ConsoleModel::begin(quint64 token, ConsoleOp op, const QString& command) {
    if (static_cast<int>(m_entries.size()) >= kCapacity) {
        beginRemoveRows(QModelIndex(), 0, 0);
        const ConsoleEntry& oldest = m_entries.front();
        m_serialByToken.remove(oldest.token);
        if (oldest.requestId != 0) {
            m_serialById.remove(oldest.requestId);
        }
        m_entries.pop_front();
        ++m_firstSerial;
        endRemoveRows();
    }
    const int row = static_cast<int>(m_entries.size());
    beginInsertRows(QModelIndex(), row, row);
    ConsoleEntry entry;
    entry.time = QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    entry.token = token;
    entry.op = op;
    entry.command = command;
    m_entries.push_back(entry);
    m_serialByToken.insert(token, m_firstSerial + static_cast<quint64>(row));
    endInsertRows();
    return row;
}

ConsoleEntry* ConsoleModel::byToken(quint64 token, int* row) {
    const auto it = m_serialByToken.constFind(token);
    if (it == m_serialByToken.constEnd() || it.value() < m_firstSerial) {
        return nullptr;
    }
    const int index = static_cast<int>(it.value() - m_firstSerial);
    if (index >= static_cast<int>(m_entries.size())) {
        return nullptr;
    }
    if (row != nullptr) {
        *row = index;
    }
    return &m_entries[static_cast<size_t>(index)];
}

int ConsoleModel::rowOfToken(quint64 token) const {
    int row = -1;
    const_cast<ConsoleModel*>(this)->byToken(token, &row);
    return row;
}

void ConsoleModel::changed(int row) {
    emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
}

QString ConsoleModel::describePayload(ConsoleOp op, const QByteArray& payload) {
    if (op == ConsoleOp::WriteWords || op == ConsoleOp::WriteBits) {
        return QStringLiteral("ok");
    }
    QStringList values;
    if (op == ConsoleOp::ReadWords) {
        for (int i = 0; i + 1 < payload.size(); i += 2) {
            const quint16 word = static_cast<quint16>(static_cast<quint8>(payload.at(i)) |
                                                      (static_cast<quint8>(payload.at(i + 1)) << 8));
            values.push_back(QString::number(word));
        }
    } else {
        for (const char byte : payload) {
            values.push_back(byte != 0 ? QStringLiteral("1") : QStringLiteral("0"));
        }
    }
    return QStringLiteral("ok: %1").arg(values.join(QLatin1Char(' ')));
}

void ConsoleModel::finish(int row, const RequestOutcome& outcome) {
    ConsoleEntry& entry = m_entries[static_cast<size_t>(row)];
    if (outcome.errorCode == 0) {
        entry.state = ConsoleEntry::State::Ok;
        entry.result = describePayload(entry.op, outcome.payload);
    } else {
        entry.state = ConsoleEntry::State::Failed;
        entry.result = QStringLiteral("error %1: %2").arg(outcome.errorCode).arg(outcome.message);
    }
    changed(row);
}

void ConsoleModel::onCommandDone(const CommandResult& result) {
    int row = -1;
    ConsoleEntry* entry = byToken(result.token, &row);
    if (entry == nullptr) {
        return;
    }
    if (!result.ok) {
        entry->state = ConsoleEntry::State::Failed;
        entry->result = result.errorCode != 0
                            ? QStringLiteral("error %1: %2").arg(result.errorCode).arg(result.message)
                            : result.message;
        changed(row);
        return;
    }
    entry->requestId = result.value;
    m_serialById.insert(result.value, m_firstSerial + static_cast<quint64>(row));
    const auto early = m_early.constFind(result.value);
    if (early != m_early.constEnd()) {
        const RequestOutcome outcome = early.value();
        m_early.erase(early);
        finish(row, outcome);
    } else {
        changed(row);
    }
}

void ConsoleModel::onRequestFinished(const RequestOutcome& outcome) {
    const auto it = m_serialById.constFind(outcome.id);
    if (it != m_serialById.constEnd() && it.value() >= m_firstSerial) {
        const int row = static_cast<int>(it.value() - m_firstSerial);
        if (row < static_cast<int>(m_entries.size())) {
            finish(row, outcome);
        }
        return;
    }
    // The command's own answer has not arrived yet (the device finished the request before it
    // returned its id). Ad-hoc requests of other users of the device would also land here, so the
    // stash is bounded.
    if (m_early.size() >= 256) {
        m_early.clear();
    }
    m_early.insert(outcome.id, outcome);
}

void ConsoleModel::clear() {
    beginResetModel();
    m_entries.clear();
    m_serialByToken.clear();
    m_serialById.clear();
    m_early.clear();
    m_firstSerial = 0;
    endResetModel();
}

int ConsoleModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

int ConsoleModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant ConsoleModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(m_entries.size())) {
        return {};
    }
    const ConsoleEntry& entry = m_entries[static_cast<size_t>(index.row())];
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case TimeColumn:
            return entry.time;
        case TokenColumn:
            return QString::number(entry.token);
        case RequestColumn:
            return entry.requestId != 0 ? QVariant(QString::number(entry.requestId)) : QVariant(QString());
        case CommandColumn:
            return entry.command;
        case ResultColumn:
            return entry.state == ConsoleEntry::State::Pending ? QStringLiteral("...") : entry.result;
        default:
            break;
        }
    }
    if (role == Qt::ToolTipRole && index.column() == ResultColumn) {
        return entry.result;
    }
    return {};
}

QVariant ConsoleModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case TimeColumn:
        return QStringLiteral("Time");
    case TokenColumn:
        return QStringLiteral("Token");
    case RequestColumn:
        return QStringLiteral("Request id");
    case CommandColumn:
        return QStringLiteral("Command");
    case ResultColumn:
        return QStringLiteral("Result");
    default:
        return {};
    }
}

} // namespace mc::workbench
