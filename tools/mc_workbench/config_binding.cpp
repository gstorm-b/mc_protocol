#include "mc_workbench/config_binding.h"

#include "mc_workbench/qt_compat.h"

#include <qpb/PropertyGroup.h>
#include <qpb/PropertyModel.h>
#include <qpb/ValidationResult.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>

#include <climits>

namespace mc::workbench {

namespace {

constexpr const char* kPathSubscriptions = "subscriptions";
constexpr const char* kPathHeartbeatDevice = "session/heartbeat/device";
constexpr const char* kPathFrameType = "frame/frame";

void addField(QVector<FieldInfo>& list, const char* path, const char* label, FieldKind kind,
              const char* toolTip, const QStringList& options = {}, qint64 minimum = 0,
              qint64 maximum = 0) {
    FieldInfo info;
    info.path = QString::fromLatin1(path);
    info.label = QString::fromLatin1(label);
    info.kind = kind;
    info.options = options;
    info.minimum = minimum;
    info.maximum = maximum;
    info.toolTip = QString::fromLatin1(toolTip);
    list.push_back(info);
}

QVector<FieldInfo> buildFields() {
    QVector<FieldInfo> f;
    const QStringList hexOctal{QStringLiteral("Hex"), QStringLiteral("Octal")};
    constexpr qint64 u8 = 255;
    constexpr qint64 u16 = 65535;
    constexpr qint64 u32 = 4294967295LL;

    // transport
    addField(f, "transport/kind", "Transport", FieldKind::Enum, "TCP (3E, 1E) or serial (3C, 1C)",
             {QStringLiteral("Tcp"), QStringLiteral("Serial")});
    addField(f, "transport/tcp/host", "Host", FieldKind::Text, "Host name or address");
    addField(f, "transport/tcp/port", "Port", FieldKind::Int, "TCP port (1..65535)", {}, 0, u16);
    addField(f, "transport/tcp/connectTimeoutMs", "Connect timeout (ms)", FieldKind::Int,
             "0 or less: no connect timer", {}, INT_MIN, INT_MAX);
    addField(f, "transport/tcp/lowDelay", "Low delay", FieldKind::Bool,
             "No Nagle delay on requests");
    addField(f, "transport/tcp/keepAlive", "Keep-alive", FieldKind::Bool,
             "TCP keep-alive, OS timing");
    addField(f, "transport/tcp/closeGraceMs", "Close grace (ms)", FieldKind::Int,
             "Close: 0 = reset at once; > 0 = FIN, reset after this many ms", {}, 0, 60000);
    addField(f, "transport/serial/portName", "Port name", FieldKind::Text, "For example COM3");
    addField(f, "transport/serial/baudRate", "Baud rate", FieldKind::Int, "Bits per second", {},
             INT_MIN, INT_MAX);
    addField(f, "transport/serial/dataBits", "Data bits", FieldKind::Int, "5 to 8", {}, 5, 8);
    addField(f, "transport/serial/parity", "Parity", FieldKind::Enum, "Parity check",
             {QStringLiteral("None"), QStringLiteral("Even"), QStringLiteral("Odd"),
              QStringLiteral("Space"), QStringLiteral("Mark")});
    addField(f, "transport/serial/stopBits", "Stop bits", FieldKind::Enum, "Stop bits",
             {QStringLiteral("1"), QStringLiteral("1.5"), QStringLiteral("2")});
    addField(f, "transport/serial/flowControl", "Flow control", FieldKind::Enum, "Flow control",
             {QStringLiteral("None"), QStringLiteral("Hardware"), QStringLiteral("Software")});
    addField(f, "transport/serial/dataTerminalReady", "DTR on after open", FieldKind::Bool,
             "DTR on after open (most devices, e.g. a C24 with DTR/DSR control, need it)");
    addField(f, "transport/serial/requestToSend", "RTS on after open", FieldKind::Bool,
             "RTS on after open (ignored with hardware flow control)");

    // frame
    addField(f, "frame/frame", "Frame", FieldKind::Enum,
             "Frame family; another family loads its defaults when the values do not fit",
             {QStringLiteral("3E"), QStringLiteral("1E"), QStringLiteral("3C"),
              QStringLiteral("1C"), QStringLiteral("4E"), QStringLiteral("4C")});
    addField(f, "frame/code", "Data code", FieldKind::Enum, "Binary (3E, 1E) or ASCII",
             {QStringLiteral("Binary"), QStringLiteral("Ascii")});
    addField(f, "frame/network", "Network No.", FieldKind::Int, "Access route", {}, 0, u8);
    addField(f, "frame/pc", "PC No.", FieldKind::Int, "Access route", {}, 0, u8);
    addField(f, "frame/io", "Module I/O No.", FieldKind::Int, "Request destination module", {}, 0,
             u16);
    addField(f, "frame/station", "Module station No.", FieldKind::Int,
             "Request destination module", {}, 0, u8);
    addField(f, "frame/monitoringTimer", "Monitoring timer (x250 ms)", FieldKind::Int,
             "0 = wait forever (then the timeout is mandatory)", {}, 0, u16);
    addField(f, "frame/series", "PLC series", FieldKind::Enum, "QnA device code column",
             {QStringLiteral("QL"), QStringLiteral("IqR")});
    addField(f, "frame/checkRoute", "Check route", FieldKind::Bool,
             "Compare the response route with the request route");
    addField(f, "frame/serialStart", "4E serial number", FieldKind::Int, "Reserved (4E)", {}, 0,
             u16);
    addField(f, "frame/format", "Serial format", FieldKind::Enum, "3C and 1C envelope",
             {QStringLiteral("Format1"), QStringLiteral("Format2"), QStringLiteral("Format3"),
              QStringLiteral("Format4"), QStringLiteral("Format5")});
    addField(f, "frame/stationNo", "Station No.", FieldKind::Int, "Serial access route", {}, 0, u8);
    addField(f, "frame/selfStation", "Self station No.", FieldKind::Int, "3C access route", {}, 0,
             u8);
    addField(f, "frame/sumCheck", "Sum check", FieldKind::Bool, "Append and require the sum check");
    addField(f, "frame/blockNo", "Block No.", FieldKind::Int, "Format 2 only", {}, 0, u8);
    addField(f, "frame/checkBlockNo", "Check block No.", FieldKind::Bool,
             "Compare the response block No. (Format 2)");
    addField(f, "frame/sendEotOnError", "Send EOT on error", FieldKind::Bool,
             "Send EOT after an error response");
    addField(f, "frame/f3ShortResponseHasSum", "Format 3 short response has sum", FieldKind::Bool,
             "A Format 3 response without data carries a sum check");
    addField(f, "frame/messageWait", "Message wait (x10 ms)", FieldKind::Int, "1C, 0 to 15", {}, 0,
             u8);
    addField(f, "frame/commandSet", "1C command set", FieldKind::Enum, "ACPU or AnA/AnU commands",
             {QStringLiteral("ACPU"), QStringLiteral("AnA")});
    addField(f, "frame/e1AliasLS", "1E: L/S as M", FieldKind::Bool, "Accept L and S on 1E as M");
    addField(f, "frame/targetFamily", "Target family", FieldKind::Enum, "Point limit column",
             {QStringLiteral("IqR_Q_L"), QStringLiteral("QnA"), QStringLiteral("A")});
    addField(f, "frame/highPerformanceQcpu", "High performance QCPU", FieldKind::Bool,
             "ZR counts double (v1.1)");
    addField(f, "frame/aSeriesTarget", "A-series target", FieldKind::Bool,
             "Multiple-of-16 head rule for word access to bit devices");
    addField(f, "frame/splitWrites", "Split writes", FieldKind::Bool,
             "Allow a write across several commands (v1.1)");
    addField(f, "frame/maxReadWords", "Max read words", FieldKind::Int,
             "Max words per read (0 = frame default; FX5 ASCII 480, FX3-ENET-ADP 1E 32)", {}, 0,
             u16);
    addField(f, "frame/maxWriteWords", "Max write words", FieldKind::Int,
             "Max words per write (0 = frame default; FX5 ASCII 480, FX3-ENET-ADP 1E 10)", {}, 0,
             u16);
    addField(f, "frame/maxReadBits", "Max read bits", FieldKind::Int,
             "Max bits per read (0 = frame default; FX5 ASCII 1792, FX3-ENET-ADP 1E 256)", {}, 0,
             u16);
    addField(f, "frame/maxWriteBits", "Max write bits", FieldKind::Int,
             "Max bits per write (0 = frame default; FX5 ASCII 1792, FX3-ENET-ADP 1E 160)", {}, 0,
             u16);
    addField(f, "frame/timeoutMs", "Timeout (ms)", FieldKind::Int64, "0 = derived", {}, 0, u32);
    addField(f, "frame/readRetries", "Read retries", FieldKind::Int, "Serial links only", {}, 0,
             u8);
    addField(f, "frame/xyNotation", "X/Y notation", FieldKind::Enum,
             "Base of X and Y numbers written as text (Octal for FX CPUs)", hexOctal);
    addField(f, "frame/xyAsciiDigits", "X/Y ASCII digits", FieldKind::Enum,
             "Base of X and Y digits in ASCII frames", hexOctal);

    // session
    addField(f, "session/cycleIntervalMs", "Cycle interval (ms)", FieldKind::Int64,
             "0 = rounds back to back", {}, 0, u32);
    addField(f, "session/cycleMode", "Cycle mode", FieldKind::Enum, "Round scheduling",
             {QStringLiteral("FixedRate"), QStringLiteral("FixedDelay")});
    addField(f, "session/bitsAsWords", "Bits as words", FieldKind::Bool,
             "Read bit devices as words and unpack them");
    addField(f, "session/maxGap", "Max gap", FieldKind::MaxGap,
             "Largest gap merged into one read: auto or a number");
    addField(f, "session/adHocCapacity", "Ad-hoc capacity", FieldKind::Int,
             "Queued ad-hoc requests", {}, 0, u16);
    addField(f, "session/adHocArenaBytes", "Ad-hoc arena (bytes)", FieldKind::Int64,
             "Memory for queued ad-hoc frames", {}, 0, u32);
    addField(f, "session/maxAdHocBurst", "Max ad-hoc burst", FieldKind::Int,
             "Ad-hoc frames per round", {}, 0, u8);
    addField(f, "session/maxConsecutiveLinkErrors", "Max consecutive link errors", FieldKind::Int,
             "Serial: errors in a row before the link faults (at least 1)", {}, 0, u8);
    addField(f, "session/serialInterCharMs", "Serial inter-char (ms)", FieldKind::Int,
             "Serial: gap allowed inside a response", {}, 0, u16);
    addField(f, "session/serialFlushMs", "Serial flush (ms)", FieldKind::Int,
             "Serial: discard time after EOT", {}, 0, u16);
    addField(f, "session/firstResponseTimeoutMs", "First response timeout (ms)", FieldKind::Int64,
             "Deadline of the first request after connect; 0 = the frame timeout", {}, 0, u32);
    addField(f, "session/heartbeat/enabled", "Heartbeat", FieldKind::Bool,
             "Write a bit device every round");
    addField(f, kPathHeartbeatDevice, "Heartbeat device", FieldKind::Text,
             "A bit device, for example M2000");

    addField(f, kPathSubscriptions, "Subscriptions", FieldKind::Subscriptions,
             "Polled at connect: DEVICE:COUNT items separated by semicolons, e.g. D100:4; M0:16");
    return f;
}

QString groupLabel(const QString& id) {
    if (id == QLatin1String("frame")) {
        return QStringLiteral("Frame");
    }
    if (id == QLatin1String("session")) {
        return QStringLiteral("Session");
    }
    if (id == QLatin1String("heartbeat")) {
        return QStringLiteral("Heartbeat");
    }
    if (id == QLatin1String("transport")) {
        return QStringLiteral("Transport");
    }
    if (id == QLatin1String("tcp")) {
        return QStringLiteral("TCP");
    }
    if (id == QLatin1String("serial")) {
        return QStringLiteral("Serial");
    }
    return id;
}

const FieldInfo* findField(const QString& path) {
    for (const FieldInfo& field : ConfigBinding::fields()) {
        if (field.path == path) {
            return &field;
        }
    }
    return nullptr;
}

QJsonValue jsonAt(const QJsonObject& root, const QString& path) {
    const QStringList parts = path.split(QLatin1Char('/'));
    QJsonValue current(root);
    for (const QString& part : parts) {
        current = current.toObject().value(part);
    }
    return current;
}

void setJsonAt(QJsonObject& root, const QStringList& parts, int index, const QJsonValue& value) {
    const QString& key = parts.at(index);
    if (index == parts.size() - 1) {
        root.insert(key, value);
        return;
    }
    QJsonObject child = root.value(key).toObject();
    setJsonAt(child, parts, index + 1, value);
    root.insert(key, child);
}

void setJsonAt(QJsonObject& root, const QString& path, const QJsonValue& value) {
    setJsonAt(root, path.split(QLatin1Char('/')), 0, value);
}

QString subscriptionsText(const QVector<mc::SubscriptionSpec>& subs) {
    QStringList items;
    for (const mc::SubscriptionSpec& spec : subs) {
        items.push_back(QStringLiteral("%1:%2").arg(spec.device).arg(spec.count));
    }
    return items.join(QStringLiteral("; "));
}

// "D100:4; M0" -> [{device, count}]; the device text is checked later by the library.
bool parseSubscriptions(const QString& text, QJsonArray& out, QString& error) {
    static const QRegularExpression itemSplit(QStringLiteral("[;\\n]"));
    static const QRegularExpression partSplit(QStringLiteral("[\\s:,]+"));
    int number = 0;
    for (const QString& rawItem : text.split(itemSplit, Qt::SkipEmptyParts)) {
        const QString item = rawItem.trimmed();
        if (item.isEmpty()) {
            continue;
        }
        ++number;
        const QStringList parts = item.split(partSplit, Qt::SkipEmptyParts);
        bool countOk = true;
        qint64 count = 1;
        if (parts.size() == 2) {
            count = parts.at(1).toLongLong(&countOk);
        }
        if (parts.isEmpty() || parts.size() > 2 || !countOk || count < 0) {
            error = QStringLiteral("subscription %1: expected DEVICE:COUNT, for example D100:4")
                        .arg(number);
            return false;
        }
        QJsonObject spec;
        spec.insert(QStringLiteral("device"), parts.at(0));
        spec.insert(QStringLiteral("count"), count);
        out.append(spec);
    }
    return true;
}

// The JSON value a property value stands for; false (with @p error) when the text cannot be one.
bool toJsonValue(const FieldInfo& field, const QVariant& value, QJsonValue& out, QString& error) {
    switch (field.kind) {
    case FieldKind::Bool:
        out = QJsonValue(value.toBool());
        return true;
    case FieldKind::Int:
    case FieldKind::Int64:
        out = QJsonValue(static_cast<qint64>(value.toLongLong()));
        return true;
    case FieldKind::Enum:
    case FieldKind::Text:
        out = QJsonValue(value.toString());
        return true;
    case FieldKind::MaxGap: {
        const QString text = value.toString().trimmed();
        bool numeric = false;
        const qint64 number = text.toLongLong(&numeric);
        if (text.compare(QLatin1String("auto"), Qt::CaseInsensitive) == 0 || text.isEmpty()) {
            out = QJsonValue(QStringLiteral("auto"));
        } else if (numeric) {
            out = QJsonValue(number);
        } else {
            out = QJsonValue(text); // the library says: expected "auto" or a number
        }
        return true;
    }
    case FieldKind::Subscriptions: {
        QJsonArray array;
        if (!parseSubscriptions(value.toString(), array, error)) {
            return false;
        }
        out = QJsonValue(array);
        return true;
    }
    }
    return false;
}

QVariant toPropertyValue(const FieldInfo& field, const QJsonObject& json) {
    if (field.kind == FieldKind::Subscriptions) {
        QVector<mc::SubscriptionSpec> subs;
        for (const QJsonValue& item : json.value(QLatin1String(kPathSubscriptions)).toArray()) {
            mc::SubscriptionSpec spec;
            spec.device = item.toObject().value(QStringLiteral("device")).toString();
            spec.count =
                static_cast<quint32>(jsonInteger(item.toObject().value(QStringLiteral("count"))));
            subs.push_back(spec);
        }
        return subscriptionsText(subs);
    }
    const QJsonValue v = jsonAt(json, field.path);
    switch (field.kind) {
    case FieldKind::Bool:
        return v.toBool();
    case FieldKind::Int:
        return static_cast<int>(jsonInteger(v));
    case FieldKind::Int64:
        return jsonInteger(v);
    case FieldKind::MaxGap:
        return v.isString() ? v.toString() : QString::number(jsonInteger(v));
    case FieldKind::Enum:
    case FieldKind::Text:
    case FieldKind::Subscriptions:
        break;
    }
    return v.toString();
}

QString libraryMessage(const mc::Error& error, const QString& where) {
    const QString text = QString::fromUtf8(error.message);
    return where.isEmpty() ? text : QStringLiteral("%1 (%2)").arg(text, where);
}

mc::FrameConfig presetFor(mc::FrameType type, const mc::FrameConfig& keep, bool& known) {
    mc::FrameConfig preset = keep;
    known = true;
    switch (type) {
    case mc::FrameType::F3E:
        preset = mc::FrameConfig::frame3E();
        break;
    case mc::FrameType::F1E:
        preset = mc::FrameConfig::frame1E();
        break;
    case mc::FrameType::F3C:
        preset = mc::FrameConfig::frame3C();
        break;
    case mc::FrameType::F1C:
        preset = mc::FrameConfig::frame1C();
        break;
    default:
        known = false;
        return keep;
    }
    preset.xyNotation = keep.xyNotation;
    preset.xyAsciiDigits = keep.xyAsciiDigits;
    return preset;
}

} // namespace

const QVector<FieldInfo>& ConfigBinding::fields() {
    static const QVector<FieldInfo> list = buildFields();
    return list;
}

struct ConfigBinding::Candidate {
    bool parsed{false}; // fromJson() accepted it: a well-formed edit
    bool ok{false};     // and validate() accepted the whole configuration
    mc::McDeviceConfig cfg;
    QString message;
    QString where; // validate()'s JSON path (dotted) when parsed && !ok
};

QString ConfigBinding::invalidAttribute() {
    return QStringLiteral("mcInvalid");
}

namespace {

// validate() names a field with a dotted JSON path ("transport.serial.portName",
// "subscriptions[0].device"); the grid's path uses '/'. Falls back to the nearest ancestor that is
// a field ("subscriptions"); empty when nothing matches.
QString propertyPathFor(const QString& where) {
    QString path = where;
    path.replace(QLatin1Char('.'), QLatin1Char('/'));
    while (!path.isEmpty()) {
        const int bracket = path.indexOf(QLatin1Char('['));
        if (bracket >= 0) {
            path.truncate(bracket);
        }
        if (findField(path) != nullptr) {
            return path;
        }
        const int slash = path.lastIndexOf(QLatin1Char('/'));
        // A group named after its main field: validate() says "frame" for the field "frame/frame".
        const QString sameName = path + QLatin1Char('/') + path.mid(slash + 1);
        if (findField(sameName) != nullptr) {
            return sameName;
        }
        if (slash < 0) {
            break;
        }
        path.truncate(slash);
    }
    return findField(path) != nullptr ? path : QString();
}

} // namespace

ConfigBinding::ConfigBinding(const mc::McDeviceConfig& cfg, QObject* parent)
    : QObject(parent), m_config(cfg.validate() ? cfg : mc::McDeviceConfig{}) {
    buildTree();
    setConfig(cfg);
    connect(m_model, &qpb::PropertyModel::validationFailed, this,
            [this](const QString& path, const QVariant&, const QString& message) {
                m_lastMessage = message;
                emit rejected(path, message);
            });
    connect(m_model, &qpb::PropertyModel::valueChanged, this,
            [this](const QString& path, const QVariant& value, const QVariant&) {
                if (m_syncing) {
                    return;
                }
                const Candidate c = candidate(path, value);
                if (!c.parsed) { // the validator accepted it a moment ago; stay consistent anyway
                    syncModel();
                    return;
                }
                m_lastMessage.clear();
                m_draft = c.cfg;
                m_draftValid = c.ok;
                m_draftMessage = c.ok ? QString() : c.message;
                m_draftWhere = c.ok ? QString() : propertyPathFor(c.where);
                const bool valid = c.ok;
                if (valid) {
                    m_config = c.cfg;
                }
                syncModel();
                applyDraftMark();
                if (valid) {
                    emit configChanged(m_config);
                }
                emit draftChanged();
            });
}

ConfigBinding::~ConfigBinding() = default;

void ConfigBinding::applyDraftMark() {
    if (!m_markedPath.isEmpty()) {
        if (qpb::Property* old = m_model->find(m_markedPath)) {
            const FieldInfo* field = findField(m_markedPath);
            old->removeAttribute(invalidAttribute());
            old->setDisplayName(field->label);
            old->setToolTip(field->toolTip);
        }
        m_markedPath.clear();
    }
    if (m_draftValid || m_draftWhere.isEmpty()) {
        return;
    }
    if (qpb::Property* property = m_model->find(m_draftWhere)) {
        const FieldInfo* field = findField(m_draftWhere);
        property->setAttribute(invalidAttribute(), m_draftMessage);
        property->setDisplayName(field->label + QStringLiteral(" (!)"));
        property->setToolTip(m_draftMessage);
        m_markedPath = m_draftWhere;
    }
}

void ConfigBinding::buildTree() {
    auto root = qpb::PropertyGroup::create(QStringLiteral("device"));
    root->setDisplayName(QStringLiteral("Device"));
    const QJsonObject defaults = mc::McDeviceConfig{}.toJson();

    for (const FieldInfo& field : fields()) {
        const QStringList parts = field.path.split(QLatin1Char('/'));
        qpb::PropertyGroup* group = root.get();
        for (int i = 0; i < parts.size() - 1; ++i) {
            qpb::Property* existing = group->child(parts.at(i));
            if (existing == nullptr) {
                qpb::PropertyGroup& created = group->addGroup(parts.at(i));
                created.setDisplayName(groupLabel(parts.at(i)));
                group = &created;
            } else {
                group = existing->toGroup();
            }
        }
        const QString id = parts.last();
        const QString path = field.path;
        const auto validator = [this, path](const QVariant& value, const qpb::Property&) {
            if (m_syncing) {
                return qpb::ValidationResult::valid();
            }
            const Candidate c = candidate(path, value);
            return c.parsed ? qpb::ValidationResult::valid()
                            : qpb::ValidationResult::error(c.message);
        };
        const QVariant initial = toPropertyValue(field, defaults);
        switch (field.kind) {
        case FieldKind::Bool:
            group->addBool(id, initial.toBool())
                .displayName(field.label)
                .toolTip(field.toolTip)
                .validator(validator);
            break;
        case FieldKind::Int:
            group->addInt(id, initial.toInt())
                .range(static_cast<int>(field.minimum), static_cast<int>(field.maximum))
                .displayName(field.label)
                .toolTip(field.toolTip)
                .validator(validator);
            break;
        case FieldKind::Int64:
            group->addInt64(id, initial.toLongLong())
                .range(field.minimum, field.maximum)
                .displayName(field.label)
                .toolTip(field.toolTip)
                .validator(validator);
            break;
        case FieldKind::Enum: {
            QList<qpb::EnumOption> options;
            for (const QString& name : field.options) {
                options.push_back({name, name});
            }
            group->addEnum(id, options, initial)
                .displayName(field.label)
                .toolTip(field.toolTip)
                .validator(validator);
            break;
        }
        case FieldKind::Text:
        case FieldKind::MaxGap:
        case FieldKind::Subscriptions:
            group->addString(id, initial.toString())
                .displayName(field.label)
                .toolTip(field.toolTip)
                .validator(validator);
            break;
        }
    }
    m_model = new qpb::PropertyModel(std::move(root), this);
    // Only the settings of the selected transport matter.
    m_model->find(QStringLiteral("transport/tcp"))
        ->setEnabledWhen(QStringLiteral("transport/kind"), QVariant(QStringLiteral("Tcp")));
    m_model->find(QStringLiteral("transport/serial"))
        ->setEnabledWhen(QStringLiteral("transport/kind"), QVariant(QStringLiteral("Serial")));
}

ConfigBinding::Candidate ConfigBinding::candidate(const QString& path, const QVariant& value) const {
    Candidate c;
    const FieldInfo* field = findField(path);
    if (field == nullptr) {
        c.message = QStringLiteral("unknown field %1").arg(path);
        return c;
    }
    QJsonValue replacement;
    if (!toJsonValue(*field, value, replacement, c.message)) {
        return c;
    }
    QJsonObject json = m_draft.toJson();
    setJsonAt(json, path, replacement);
    // The heartbeat device is text read in the X/Y notation of the frame; keep the device itself
    // out of the parse unless this edit is the device, so that a notation change cannot make an
    // untouched field fail.
    const bool heartbeatEdit = path == QLatin1String(kPathHeartbeatDevice);
    if (!heartbeatEdit) {
        setJsonAt(json, QLatin1String(kPathHeartbeatDevice), QJsonValue(QStringLiteral("M0")));
    }

    QString where;
    const mc::Expected<mc::McDeviceConfig> parsed = mc::McDeviceConfig::fromJson(json, &where);
    if (!parsed) {
        c.message = libraryMessage(parsed.error(), where);
        return c;
    }
    c.parsed = true;
    c.cfg = parsed.value();
    if (!heartbeatEdit) {
        c.cfg.session.heartbeat.device = m_draft.session.heartbeat.device;
    }

    const mc::Expected<void> valid = c.cfg.validate(&where);
    if (valid) {
        c.ok = true;
        return c;
    }
    c.message = libraryMessage(valid.error(), where);
    c.where = where;

    if (path == QLatin1String(kPathFrameType) && c.where.startsWith(QLatin1String("frame"))) {
        // The other frame values may belong to the old family: try that family's defaults. The
        // library checks the frame first, so an alternative that fails outside "frame" has a good
        // frame: it is kept, as valid or as a draft marked on that other field (for example the
        // serial port name that is still empty).
        bool known = false;
        mc::McDeviceConfig alternative = c.cfg;
        alternative.frame = presetFor(c.cfg.frame.frame, m_draft.frame, known);
        QString alternativeWhere;
        const mc::Expected<void> alternativeValid = alternative.validate(&alternativeWhere);
        if (known && alternativeValid) {
            c.cfg = alternative;
            c.ok = true;
            c.message.clear();
            c.where.clear();
        } else if (known && !alternativeWhere.startsWith(QLatin1String("frame"))) {
            c.cfg = alternative;
            c.message = libraryMessage(alternativeValid.error(), alternativeWhere);
            c.where = alternativeWhere;
        }
    }
    return c;
}

void ConfigBinding::syncModel() {
    const bool wasSyncing = m_syncing;
    m_syncing = true;
    const QJsonObject json = m_draft.toJson();
    m_model->beginBatch();
    for (const FieldInfo& field : fields()) {
        m_model->setValue(field.path, toPropertyValue(field, json));
    }
    m_model->endBatch();
    m_syncing = wasSyncing;
}

bool ConfigBinding::setConfig(const mc::McDeviceConfig& cfg, QString* message) {
    QString where;
    const mc::Expected<void> valid = cfg.validate(&where);
    m_draft = cfg;
    m_draftValid = static_cast<bool>(valid);
    m_draftMessage = valid ? QString() : libraryMessage(valid.error(), where);
    m_draftWhere = valid ? QString() : propertyPathFor(where);
    if (valid) {
        m_config = cfg;
    } else if (message != nullptr) {
        *message = m_draftMessage;
    }
    m_lastMessage.clear();
    syncModel();
    applyDraftMark();
    return m_draftValid;
}

void ConfigBinding::setEditable(bool editable) {
    m_model->root()->setReadOnly(!editable);
}

} // namespace mc::workbench
