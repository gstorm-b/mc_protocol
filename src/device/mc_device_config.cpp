// McDeviceConfig: validate() and the version 1 JSON mapping (SPEC-qt-device.md,
// "mc_device_config.h").
#include "mc/device/mc_device_config.h"

#include "mc/core/device.h"
#include "mc/core/poll_plan.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonValue>
#include <QLatin1String>

#include <cmath>
#include <cstddef>
#include <limits>
#include <string_view>

namespace mc {

namespace {

Error configError(const char* message) noexcept {
    Error e{};
    e.category = ErrorCategory::Config;
    e.code = ErrorCode::InvalidConfig;
    e.message = message;
    return e;
}

void setWhere(QString* where, const QString& path) {
    if (where != nullptr) {
        *where = path;
    }
}

QString joinPath(const QString& base, const char* key) {
    return base.isEmpty() ? QString::fromLatin1(key) : base + QLatin1Char('.') + QLatin1String(key);
}

QString indexPath(const QString& base, int index) {
    return QStringLiteral("%1[%2]").arg(base).arg(index);
}

// ---- enum <-> string tables ------------------------------------------------------------------

template <class E> struct EnumName {
    E value;
    const char* name;
};

constexpr EnumName<FrameType> kFrameTypes[] = {{FrameType::F3E, "3E"}, {FrameType::F1E, "1E"},
                                               {FrameType::F3C, "3C"}, {FrameType::F1C, "1C"},
                                               {FrameType::F4E, "4E"}, {FrameType::F4C, "4C"}};
constexpr EnumName<DataCode> kDataCodes[] = {{DataCode::Binary, "Binary"},
                                             {DataCode::Ascii, "Ascii"}};
constexpr EnumName<SerialFormat> kSerialFormats[] = {{SerialFormat::Format1, "Format1"},
                                                     {SerialFormat::Format2, "Format2"},
                                                     {SerialFormat::Format3, "Format3"},
                                                     {SerialFormat::Format4, "Format4"},
                                                     {SerialFormat::Format5, "Format5"}};
constexpr EnumName<PlcSeries> kPlcSeries[] = {{PlcSeries::QL, "QL"}, {PlcSeries::IqR, "IqR"}};
constexpr EnumName<TargetFamily> kTargetFamilies[] = {
    {TargetFamily::IqR_Q_L, "IqR_Q_L"}, {TargetFamily::QnA, "QnA"}, {TargetFamily::A, "A"}};
constexpr EnumName<C1CommandSet> kCommandSets[] = {{C1CommandSet::ACPU, "ACPU"},
                                                   {C1CommandSet::AnA, "AnA"}};
constexpr EnumName<CycleMode> kCycleModes[] = {{CycleMode::FixedRate, "FixedRate"},
                                               {CycleMode::FixedDelay, "FixedDelay"}};
constexpr EnumName<TransportKind> kTransportKinds[] = {{TransportKind::Tcp, "Tcp"},
                                                       {TransportKind::Serial, "Serial"}};
constexpr EnumName<QSerialPort::Parity> kParities[] = {{QSerialPort::NoParity, "None"},
                                                       {QSerialPort::EvenParity, "Even"},
                                                       {QSerialPort::OddParity, "Odd"},
                                                       {QSerialPort::SpaceParity, "Space"},
                                                       {QSerialPort::MarkParity, "Mark"}};
constexpr EnumName<QSerialPort::StopBits> kStopBits[] = {
    {QSerialPort::OneStop, "1"}, {QSerialPort::OneAndHalfStop, "1.5"}, {QSerialPort::TwoStop, "2"}};
constexpr EnumName<QSerialPort::FlowControl> kFlowControls[] = {
    {QSerialPort::NoFlowControl, "None"},
    {QSerialPort::HardwareControl, "Hardware"},
    {QSerialPort::SoftwareControl, "Software"}};

template <class E, size_t N> const char* enumName(const EnumName<E> (&table)[N], E value) {
    for (const EnumName<E>& entry : table) {
        if (entry.value == value) {
            return entry.name;
        }
    }
    return "";
}

template <class E, size_t N> QJsonValue enumJson(const EnumName<E> (&table)[N], E value) {
    return QJsonValue(QString::fromLatin1(enumName(table, value)));
}

// ---- reading ---------------------------------------------------------------------------------

// The first failure of a parse: the JSON path and a static message.
struct Failure {
    QString path;
    const char* message{nullptr};

    // Always returns false so a reader can `return f.set(...)`.
    bool set(QString p, const char* m) {
        path = std::move(p);
        message = m;
        return false;
    }
};

// A missing key is not a failure: `out` keeps the default it already holds.
template <class T>
bool readInt(const QJsonObject& obj, const char* key, const QString& base, T& out, Failure& f) {
    const QJsonValue v = obj.value(QLatin1String(key));
    if (v.isUndefined()) {
        return true;
    }
    if (!v.isDouble()) {
        return f.set(joinPath(base, key), "expected a number");
    }
    const double d = v.toDouble();
    if (d != std::floor(d)) {
        return f.set(joinPath(base, key), "expected an integer");
    }
    if (d < static_cast<double>(std::numeric_limits<T>::min()) ||
        d > static_cast<double>(std::numeric_limits<T>::max())) {
        return f.set(joinPath(base, key), "number out of range");
    }
    out = static_cast<T>(d);
    return true;
}

bool readBool(const QJsonObject& obj, const char* key, const QString& base, bool& out, Failure& f) {
    const QJsonValue v = obj.value(QLatin1String(key));
    if (v.isUndefined()) {
        return true;
    }
    if (!v.isBool()) {
        return f.set(joinPath(base, key), "expected a boolean");
    }
    out = v.toBool();
    return true;
}

bool readString(const QJsonObject& obj, const char* key, const QString& base, QString& out,
                Failure& f) {
    const QJsonValue v = obj.value(QLatin1String(key));
    if (v.isUndefined()) {
        return true;
    }
    if (!v.isString()) {
        return f.set(joinPath(base, key), "expected a string");
    }
    out = v.toString();
    return true;
}

template <class E, size_t N>
bool readEnum(const QJsonObject& obj, const char* key, const QString& base,
              const EnumName<E> (&table)[N], E& out, Failure& f) {
    const QJsonValue v = obj.value(QLatin1String(key));
    if (v.isUndefined()) {
        return true;
    }
    if (!v.isString()) {
        return f.set(joinPath(base, key), "expected a string");
    }
    const QString text = v.toString();
    for (const EnumName<E>& entry : table) {
        if (text == QLatin1String(entry.name)) {
            out = entry.value;
            return true;
        }
    }
    return f.set(joinPath(base, key), "unknown enumerator");
}

// A missing key leaves `present` false and `out` empty; a non-object value is a failure.
bool readObject(const QJsonObject& obj, const char* key, const QString& base, QJsonObject& out,
                bool& present, Failure& f) {
    const QJsonValue v = obj.value(QLatin1String(key));
    present = !v.isUndefined();
    if (!present) {
        return true;
    }
    if (!v.isObject()) {
        return f.set(joinPath(base, key), "expected an object");
    }
    out = v.toObject();
    return true;
}

bool readFrame(const QJsonObject& o, const QString& base, FrameConfig& c, Failure& f) {
    return readEnum(o, "frame", base, kFrameTypes, c.frame, f) &&
           readEnum(o, "code", base, kDataCodes, c.code, f) &&
           readInt(o, "network", base, c.network, f) && readInt(o, "pc", base, c.pc, f) &&
           readInt(o, "io", base, c.io, f) && readInt(o, "station", base, c.station, f) &&
           readInt(o, "monitoringTimer", base, c.monitoringTimer, f) &&
           readEnum(o, "series", base, kPlcSeries, c.series, f) &&
           readBool(o, "checkRoute", base, c.checkRoute, f) &&
           readInt(o, "serialStart", base, c.serialStart, f) &&
           readEnum(o, "format", base, kSerialFormats, c.format, f) &&
           readInt(o, "stationNo", base, c.stationNo, f) &&
           readInt(o, "selfStation", base, c.selfStation, f) &&
           readBool(o, "sumCheck", base, c.sumCheck, f) &&
           readInt(o, "blockNo", base, c.blockNo, f) &&
           readBool(o, "checkBlockNo", base, c.checkBlockNo, f) &&
           readBool(o, "sendEotOnError", base, c.sendEotOnError, f) &&
           readBool(o, "f3ShortResponseHasSum", base, c.f3ShortResponseHasSum, f) &&
           readInt(o, "messageWait", base, c.messageWait, f) &&
           readEnum(o, "commandSet", base, kCommandSets, c.commandSet, f) &&
           readBool(o, "e1AliasLS", base, c.e1AliasLS, f) &&
           readEnum(o, "targetFamily", base, kTargetFamilies, c.targetFamily, f) &&
           readBool(o, "highPerformanceQcpu", base, c.highPerformanceQcpu, f) &&
           readBool(o, "aSeriesTarget", base, c.aSeriesTarget, f) &&
           readBool(o, "splitWrites", base, c.splitWrites, f) &&
           readInt(o, "timeoutMs", base, c.timeoutMs, f) &&
           readInt(o, "readRetries", base, c.readRetries, f);
}

bool readMaxGap(const QJsonObject& o, const QString& base, uint32_t& out, Failure& f) {
    const QJsonValue v = o.value(QLatin1String("maxGap"));
    if (v.isUndefined()) {
        return true;
    }
    if (v.isString()) {
        if (v.toString() == QLatin1String("auto")) {
            out = kAutoGap;
            return true;
        }
        return f.set(joinPath(base, "maxGap"), "expected \"auto\" or a number");
    }
    // A number equal to kAutoGap is the sentinel itself; "auto" is its only spelling.
    uint32_t gap = 0;
    if (!readInt(o, "maxGap", base, gap, f)) {
        return false;
    }
    if (gap == kAutoGap) {
        return f.set(joinPath(base, "maxGap"), "number out of range");
    }
    out = gap;
    return true;
}

bool readHeartbeat(const QJsonObject& o, const QString& base, HeartbeatConfig& c, Failure& f) {
    if (!readBool(o, "enabled", base, c.enabled, f)) {
        return false;
    }
    QString text;
    if (!readString(o, "device", base, text, f)) {
        return false;
    }
    if (o.contains(QLatin1String("device"))) {
        const QByteArray utf8 = text.toUtf8();
        const Expected<Device> parsed =
            parseDevice(std::string_view(utf8.constData(), static_cast<size_t>(utf8.size())));
        if (!parsed) {
            return f.set(joinPath(base, "device"), "not a device");
        }
        c.device = parsed.value();
    }
    return true;
}

bool readSession(const QJsonObject& o, const QString& base, SessionConfig& c, Failure& f) {
    if (!(readInt(o, "cycleIntervalMs", base, c.cycleIntervalMs, f) &&
          readEnum(o, "cycleMode", base, kCycleModes, c.cycleMode, f) &&
          readBool(o, "bitsAsWords", base, c.plan.bitsAsWords, f) &&
          readMaxGap(o, base, c.plan.maxGap, f) &&
          readInt(o, "adHocCapacity", base, c.adHocCapacity, f) &&
          readInt(o, "adHocArenaBytes", base, c.adHocArenaBytes, f) &&
          readInt(o, "maxAdHocBurst", base, c.maxAdHocBurst, f) &&
          readInt(o, "maxConsecutiveLinkErrors", base, c.maxConsecutiveLinkErrors, f) &&
          readInt(o, "serialInterCharMs", base, c.serialInterCharMs, f) &&
          readInt(o, "serialFlushMs", base, c.serialFlushMs, f))) {
        return false;
    }
    QJsonObject heartbeat;
    bool present = false;
    if (!readObject(o, "heartbeat", base, heartbeat, present, f)) {
        return false;
    }
    return !present || readHeartbeat(heartbeat, joinPath(base, "heartbeat"), c.heartbeat, f);
}

bool readTcp(const QJsonObject& o, const QString& base, TcpSettings& c, Failure& f) {
    return readString(o, "host", base, c.host, f) && readInt(o, "port", base, c.port, f) &&
           readInt(o, "connectTimeoutMs", base, c.connectTimeoutMs, f);
}

bool readSerial(const QJsonObject& o, const QString& base, SerialSettings& c, Failure& f) {
    if (!(readString(o, "portName", base, c.portName, f) &&
          readInt(o, "baudRate", base, c.baudRate, f))) {
        return false;
    }
    int dataBits = static_cast<int>(c.dataBits);
    if (!readInt(o, "dataBits", base, dataBits, f)) {
        return false;
    }
    if (dataBits < 5 || dataBits > 8) {
        return f.set(joinPath(base, "dataBits"), "number out of range");
    }
    c.dataBits = static_cast<QSerialPort::DataBits>(dataBits);
    return readEnum(o, "parity", base, kParities, c.parity, f) &&
           readEnum(o, "stopBits", base, kStopBits, c.stopBits, f) &&
           readEnum(o, "flowControl", base, kFlowControls, c.flowControl, f);
}

bool readTransport(const QJsonObject& o, const QString& base, McDeviceConfig& c, Failure& f) {
    if (!readEnum(o, "kind", base, kTransportKinds, c.transport, f)) {
        return false;
    }
    QJsonObject sub;
    bool present = false;
    if (!readObject(o, "tcp", base, sub, present, f)) {
        return false;
    }
    if (present && !readTcp(sub, joinPath(base, "tcp"), c.tcp, f)) {
        return false;
    }
    if (!readObject(o, "serial", base, sub, present, f)) {
        return false;
    }
    return !present || readSerial(sub, joinPath(base, "serial"), c.serial, f);
}

bool readSubscriptions(const QJsonObject& o, const QString& base, QVector<SubscriptionSpec>& out,
                       Failure& f) {
    const QJsonValue v = o.value(QLatin1String("subscriptions"));
    if (v.isUndefined()) {
        return true;
    }
    if (!v.isArray()) {
        return f.set(joinPath(base, "subscriptions"), "expected an array");
    }
    const QJsonArray array = v.toArray();
    out.clear();
    for (int i = 0; i < array.size(); ++i) {
        const QString itemPath = indexPath(joinPath(base, "subscriptions"), i);
        if (!array.at(i).isObject()) {
            return f.set(itemPath, "expected an object");
        }
        const QJsonObject item = array.at(i).toObject();
        SubscriptionSpec spec;
        if (!(readString(item, "device", itemPath, spec.device, f) &&
              readInt(item, "count", itemPath, spec.count, f))) {
            return false;
        }
        out.push_back(spec);
    }
    return true;
}

// The first mismatch between a subscription and the frame, or success. `where` gets the path.
Expected<void> checkSubscription(const McDeviceConfig& cfg, int index, QString* where) {
    const SubscriptionSpec& spec = cfg.subscriptions.at(index);
    const QString base = indexPath(QStringLiteral("subscriptions"), index);

    const QByteArray utf8 = spec.device.toUtf8();
    const Expected<Device> head =
        parseDevice(std::string_view(utf8.constData(), static_cast<size_t>(utf8.size())));
    if (!head) {
        setWhere(where, joinPath(base, "device"));
        return head.error();
    }

    // The same test the engine applies when it plans reads: the subscription alone, on this
    // frame, with the session's plan options.
    RangeSet set;
    const Expected<SubscriptionId> added = set.add(head.value(), spec.count);
    if (!added) {
        setWhere(where, joinPath(base, added.error().code == ErrorCode::InvalidDevice ? "device"
                                                                                      : "count"));
        return added.error();
    }
    const Expected<ReadPlan> plan = ReadPlan::build(set, cfg.frame, cfg.session.plan);
    if (!plan) {
        setWhere(where, joinPath(base, plan.error().code == ErrorCode::InvalidDevice ? "device"
                                                                                     : "count"));
        return plan.error();
    }
    return {};
}

QJsonObject frameToJson(const FrameConfig& c) {
    QJsonObject o;
    o.insert(QStringLiteral("frame"), enumJson(kFrameTypes, c.frame));
    o.insert(QStringLiteral("code"), enumJson(kDataCodes, c.code));
    o.insert(QStringLiteral("network"), c.network);
    o.insert(QStringLiteral("pc"), c.pc);
    o.insert(QStringLiteral("io"), c.io);
    o.insert(QStringLiteral("station"), c.station);
    o.insert(QStringLiteral("monitoringTimer"), c.monitoringTimer);
    o.insert(QStringLiteral("series"), enumJson(kPlcSeries, c.series));
    o.insert(QStringLiteral("checkRoute"), c.checkRoute);
    o.insert(QStringLiteral("serialStart"), c.serialStart);
    o.insert(QStringLiteral("format"), enumJson(kSerialFormats, c.format));
    o.insert(QStringLiteral("stationNo"), c.stationNo);
    o.insert(QStringLiteral("selfStation"), c.selfStation);
    o.insert(QStringLiteral("sumCheck"), c.sumCheck);
    o.insert(QStringLiteral("blockNo"), c.blockNo);
    o.insert(QStringLiteral("checkBlockNo"), c.checkBlockNo);
    o.insert(QStringLiteral("sendEotOnError"), c.sendEotOnError);
    o.insert(QStringLiteral("f3ShortResponseHasSum"), c.f3ShortResponseHasSum);
    o.insert(QStringLiteral("messageWait"), c.messageWait);
    o.insert(QStringLiteral("commandSet"), enumJson(kCommandSets, c.commandSet));
    o.insert(QStringLiteral("e1AliasLS"), c.e1AliasLS);
    o.insert(QStringLiteral("targetFamily"), enumJson(kTargetFamilies, c.targetFamily));
    o.insert(QStringLiteral("highPerformanceQcpu"), c.highPerformanceQcpu);
    o.insert(QStringLiteral("aSeriesTarget"), c.aSeriesTarget);
    o.insert(QStringLiteral("splitWrites"), c.splitWrites);
    o.insert(QStringLiteral("timeoutMs"), static_cast<qint64>(c.timeoutMs));
    o.insert(QStringLiteral("readRetries"), c.readRetries);
    return o;
}

QJsonObject sessionToJson(const SessionConfig& c) {
    QJsonObject o;
    o.insert(QStringLiteral("cycleIntervalMs"), static_cast<qint64>(c.cycleIntervalMs));
    o.insert(QStringLiteral("cycleMode"), enumJson(kCycleModes, c.cycleMode));
    o.insert(QStringLiteral("bitsAsWords"), c.plan.bitsAsWords);
    if (c.plan.maxGap == kAutoGap) {
        o.insert(QStringLiteral("maxGap"), QStringLiteral("auto"));
    } else {
        o.insert(QStringLiteral("maxGap"), static_cast<qint64>(c.plan.maxGap));
    }
    o.insert(QStringLiteral("adHocCapacity"), c.adHocCapacity);
    o.insert(QStringLiteral("adHocArenaBytes"), static_cast<qint64>(c.adHocArenaBytes));
    o.insert(QStringLiteral("maxAdHocBurst"), c.maxAdHocBurst);
    o.insert(QStringLiteral("maxConsecutiveLinkErrors"), c.maxConsecutiveLinkErrors);
    o.insert(QStringLiteral("serialInterCharMs"), c.serialInterCharMs);
    o.insert(QStringLiteral("serialFlushMs"), c.serialFlushMs);

    char text[32];
    const size_t needed = formatDevice(c.heartbeat.device, text, sizeof text);
    QJsonObject heartbeat;
    heartbeat.insert(QStringLiteral("enabled"), c.heartbeat.enabled);
    heartbeat.insert(
        QStringLiteral("device"),
        QString::fromLatin1(text, static_cast<int>(needed < sizeof text ? needed : 0)));
    o.insert(QStringLiteral("heartbeat"), heartbeat);
    return o;
}

} // namespace

Expected<void> McDeviceConfig::validate(QString* where) const {
    setWhere(where, QString());

    if (const Expected<void> r = frame.validate(); !r) {
        setWhere(where, QStringLiteral("frame"));
        return r;
    }
    if (const Expected<void> r = session.validate(frame); !r) {
        setWhere(where, QStringLiteral("session.heartbeat.device"));
        return r;
    }
    for (int i = 0; i < subscriptions.size(); ++i) {
        if (const Expected<void> r = checkSubscription(*this, i, where); !r) {
            return r;
        }
    }
    if (transport == TransportKind::Tcp) {
        if (tcp.host.isEmpty()) {
            setWhere(where, QStringLiteral("transport.tcp.host"));
            return configError("TCP host is empty");
        }
        if (tcp.port == 0) {
            setWhere(where, QStringLiteral("transport.tcp.port"));
            return configError("TCP port is 0");
        }
    } else {
        if (serial.portName.isEmpty()) {
            setWhere(where, QStringLiteral("transport.serial.portName"));
            return configError("serial port name is empty");
        }
        if (serial.baudRate <= 0) {
            setWhere(where, QStringLiteral("transport.serial.baudRate"));
            return configError("baud rate must be positive");
        }
    }
    return {};
}

QJsonObject McDeviceConfig::toJson() const {
    QJsonObject tcpObject;
    tcpObject.insert(QStringLiteral("host"), tcp.host);
    tcpObject.insert(QStringLiteral("port"), static_cast<int>(tcp.port));
    tcpObject.insert(QStringLiteral("connectTimeoutMs"), tcp.connectTimeoutMs);

    QJsonObject serialObject;
    serialObject.insert(QStringLiteral("portName"), serial.portName);
    serialObject.insert(QStringLiteral("baudRate"), static_cast<qint64>(serial.baudRate));
    serialObject.insert(QStringLiteral("dataBits"), static_cast<int>(serial.dataBits));
    serialObject.insert(QStringLiteral("parity"), enumJson(kParities, serial.parity));
    serialObject.insert(QStringLiteral("stopBits"), enumJson(kStopBits, serial.stopBits));
    serialObject.insert(QStringLiteral("flowControl"), enumJson(kFlowControls, serial.flowControl));

    QJsonObject transportObject;
    transportObject.insert(QStringLiteral("kind"), enumJson(kTransportKinds, transport));
    transportObject.insert(QStringLiteral("tcp"), tcpObject);
    transportObject.insert(QStringLiteral("serial"), serialObject);

    QJsonArray subs;
    for (const SubscriptionSpec& spec : subscriptions) {
        QJsonObject item;
        item.insert(QStringLiteral("device"), spec.device);
        item.insert(QStringLiteral("count"), static_cast<qint64>(spec.count));
        subs.append(item);
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema"), 1);
    root.insert(QStringLiteral("frame"), frameToJson(frame));
    root.insert(QStringLiteral("session"), sessionToJson(session));
    root.insert(QStringLiteral("transport"), transportObject);
    root.insert(QStringLiteral("subscriptions"), subs);
    return root;
}

Expected<McDeviceConfig> McDeviceConfig::fromJson(const QJsonObject& obj, QString* where) {
    Failure f;
    McDeviceConfig cfg;

    const auto parse = [&]() {
        int schema = 1;
        if (!readInt(obj, "schema", QString(), schema, f)) {
            return false;
        }
        if (schema != 1) {
            return f.set(QStringLiteral("schema"), "unsupported schema version");
        }

        QJsonObject section;
        bool present = false;
        if (!readObject(obj, "frame", QString(), section, present, f)) {
            return false;
        }
        if (present && !readFrame(section, QStringLiteral("frame"), cfg.frame, f)) {
            return false;
        }
        if (!readObject(obj, "session", QString(), section, present, f)) {
            return false;
        }
        if (present && !readSession(section, QStringLiteral("session"), cfg.session, f)) {
            return false;
        }
        if (!readObject(obj, "transport", QString(), section, present, f)) {
            return false;
        }
        if (present && !readTransport(section, QStringLiteral("transport"), cfg, f)) {
            return false;
        }
        return readSubscriptions(obj, QString(), cfg.subscriptions, f);
    };

    if (!parse()) {
        setWhere(where, f.path);
        return configError(f.message);
    }
    setWhere(where, QString());
    return cfg;
}

} // namespace mc
