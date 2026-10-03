#include "hil_capture/profile.h"

#include <QByteArray>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>

#include <string_view>

namespace mc::hil {

namespace {

constexpr size_t kTypeCount = static_cast<size_t>(DeviceType::Count);

DeviceType typeAt(size_t i) { return static_cast<DeviceType>(i); }

bool isDecDigit(QChar c) { return c >= QLatin1Char('0') && c <= QLatin1Char('9'); }

// Splits "TN10" into its symbol and its number text, longest symbol first as parseDevice() does.
bool splitDevice(const QString& text, DeviceType& type, QString& number, QString& why) {
    int bestLen = 0;
    for (size_t i = 0; i < kTypeCount; ++i) {
        const QString sym = deviceSymbol(typeAt(i));
        if (sym.size() > bestLen && text.startsWith(sym, Qt::CaseInsensitive)) {
            bestLen = static_cast<int>(sym.size());
            type = typeAt(i);
        }
    }
    if (bestLen == 0) {
        why = QStringLiteral("unknown device type in '%1'").arg(text);
        return false;
    }
    number = text.mid(bestLen);
    return true;
}

bool numberFor(DeviceType t, const QString& text, uint32_t& out, QString& why, XyNumbering xy) {
    if (!parseDeviceNumber(t, text, out, xy)) {
        const bool octal = xy == XyNumbering::Octal && (t == DeviceType::X || t == DeviceType::Y);
        why = QStringLiteral("'%1' is not a %2 number of device %3")
                  .arg(text,
                       octal ? QStringLiteral("octal")
                             : (deviceInfo(t).radix == Radix::Hex ? QStringLiteral("hexadecimal")
                                                                  : QStringLiteral("decimal")),
                       deviceSymbol(t));
        return false;
    }
    return true;
}

// "D100-D2099" or "W100-1FF": the second part may repeat the symbol, which must then match.
bool parseRange(const QString& textIn, ScratchRange& out, QString& why, XyNumbering xy) {
    const QString text = textIn.trimmed();
    const qsizetype dash = text.indexOf(QLatin1Char('-'));
    if (dash <= 0) {
        why = QStringLiteral("expected FIRST-LAST such as D100-D2099, got '%1'").arg(textIn);
        return false;
    }
    const QString left = text.left(dash).trimmed();
    const QString right = text.mid(dash + 1).trimmed();
    DeviceType t = DeviceType::D;
    QString firstText;
    if (!splitDevice(left, t, firstText, why)) {
        return false;
    }
    // The second part may repeat the symbol ("W100-W1FF") or give the bare number ("W100-1FF").
    QString lastText = right;
    const QString symbol = deviceSymbol(t);
    if (right.startsWith(symbol, Qt::CaseInsensitive)) {
        lastText = right.mid(symbol.size());
    }
    uint32_t first = 0;
    uint32_t last = 0;
    if (!numberFor(t, firstText, first, why, xy)) {
        return false;
    }
    if (!numberFor(t, lastText, last, why, xy)) {
        DeviceType other = DeviceType::D;
        QString otherNumber;
        QString ignored;
        if (splitDevice(right, other, otherNumber, ignored) && other != t) {
            why = QStringLiteral("range mixes device types %1 and %2")
                      .arg(symbol, deviceSymbol(other));
        }
        return false;
    }
    if (last < first) {
        why = QStringLiteral("range ends before it starts");
        return false;
    }
    out = ScratchRange{t, first, last};
    return true;
}

// Every key of `actual` must exist in `reference`, recursively through objects.
void checkKnownKeys(const QJsonObject& actual, const QJsonObject& reference, const QString& path,
                    JsonFailure& f) {
    for (auto it = actual.begin(); it != actual.end() && !f.failed(); ++it) {
        const QJsonValue ref = reference.value(it.key());
        if (ref.isUndefined()) {
            f.fail(childPath(path, it.key()), QStringLiteral("unknown key"));
            return;
        }
        if (it.value().isObject() && ref.isObject()) {
            checkKnownKeys(it.value().toObject(), ref.toObject(), childPath(path, it.key()), f);
        }
    }
}

} // namespace

bool folderSafe(const QString& id) {
    if (id.isEmpty() || id.size() > 80 || id.contains(QStringLiteral(".."))) {
        return false;
    }
    if (!(id.front().isLetterOrNumber())) {
        return false;
    }
    for (const QChar c : id) {
        const bool ok = (c >= QLatin1Char('a') && c <= QLatin1Char('z')) ||
                        (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) || isDecDigit(c) ||
                        c == QLatin1Char('-') || c == QLatin1Char('_') || c == QLatin1Char('.');
        if (!ok) {
            return false;
        }
    }
    return true;
}

namespace {

const char* const kFamilies[] = {"qna-ethernet", "a1e", "qna-serial", "a1c"};

} // namespace

QString deviceSymbol(DeviceType t) { return QString::fromLatin1(deviceInfo(t).symbol); }

QString deviceText(const Device& d, XyNumbering xy) {
    return deviceSymbol(d.type) + formatDeviceNumber(d.type, d.number, xy);
}

std::optional<DeviceType> deviceTypeFromSymbol(const QString& symbol) {
    for (size_t i = 0; i < kTypeCount; ++i) {
        if (deviceSymbol(typeAt(i)).compare(symbol, Qt::CaseInsensitive) == 0) {
            return typeAt(i);
        }
    }
    return std::nullopt;
}

// The number goes through the core parser behind its own symbol, so the radix rules (and the
// octal reading of X and Y) live in one place.
bool parseDeviceNumber(DeviceType t, const QString& text, uint32_t& out, XyNumbering xy) {
    if (text.isEmpty() || text.size() > 8) {
        return false;
    }
    const QByteArray bytes = (deviceSymbol(t) + text).toLatin1();
    const Expected<Device> d =
        parseDevice(std::string_view(bytes.constData(), static_cast<size_t>(bytes.size())), xy);
    if (!d || d.value().type != t) {
        return false;
    }
    out = d.value().number;
    return true;
}

QString formatDeviceNumber(DeviceType t, uint32_t number, XyNumbering xy) {
    char text[32];
    const size_t needed = formatDevice(Device{t, number}, text, sizeof text, xy);
    const size_t symbolLength = static_cast<size_t>(deviceSymbol(t).size());
    return QString::fromLatin1(text + symbolLength, static_cast<int>(needed - symbolLength));
}

bool Profile::supportsType(DeviceType t) const { return supports.contains(t); }

const ScratchRange* Profile::firstScratch(DeviceType t) const {
    for (const ScratchRange& r : scratch) {
        if (r.type == t) {
            return &r;
        }
    }
    return nullptr;
}

bool Profile::inScratch(DeviceType t, uint32_t head, uint64_t points) const {
    if (points == 0) {
        return false;
    }
    const uint64_t lastPoint = static_cast<uint64_t>(head) + points - 1;
    for (const ScratchRange& r : scratch) {
        if (r.type == t && head >= r.first && lastPoint <= r.last) {
            return true;
        }
    }
    return false;
}

ProfileLoad loadProfile(const QJsonObject& root) {
    JsonFailure f;
    ProfileLoad result;
    Profile p;

    ObjectReader top(f, root, QString());
    const qint64 schema = top.integer("schema", true, 0, 0, 1000000);
    if (!f.failed() && schema != 1) {
        f.fail(QStringLiteral("schema"), QStringLiteral("unsupported schema version"));
    }
    const QJsonObject profileObj = top.object("profile", true);
    const QJsonObject deviceObj = top.object("device", true);
    top.finish();

    ObjectReader pr(f, profileObj, QStringLiteral("profile"));
    p.id = pr.string("id", true);
    p.plc = pr.string("plc", true);
    p.module = pr.string("module", false);
    p.firmware = pr.string("firmware", false);
    p.adapter = pr.string("adapter", false);
    p.plcState = pr.string("plcState", false);
    const QJsonArray scratchArr = pr.array("scratch", true);
    const QJsonObject endObj = pr.object("deviceEnd", true);
    const QJsonArray supportsArr = pr.array("supports", true);
    p.scanTimeDevice = pr.string("scanTimeDevice", false);
    const QString specialBitText = pr.string("specialBit", true);
    const QString specialWordText = pr.string("specialWord", true);
    const QJsonArray familiesArr = pr.array("families", false);
    pr.finish();

    if (!f.failed() && !folderSafe(p.id)) {
        f.fail(QStringLiteral("profile.id"),
               QStringLiteral("must be a folder name: letters, digits, '-', '_' or '.', no '..'"));
    }

    // The connection comes first: its xyNotation decides how the device numbers below are read.
    if (!f.failed()) {
        checkKnownKeys(deviceObj, McDeviceConfig{}.toJson(), QStringLiteral("device"), f);
    }
    if (!f.failed()) {
        QString where;
        Expected<McDeviceConfig> cfg = McDeviceConfig::fromJson(deviceObj, &where);
        if (!cfg) {
            f.fail(childPath(QStringLiteral("device"), where),
                   QString::fromLatin1(cfg.error().message));
        } else {
            p.device = cfg.value();
        }
    }
    const XyNumbering xy = p.device.frame.xyNotation;

    for (int i = 0; i < scratchArr.size() && !f.failed(); ++i) {
        const QString path = indexPath(QStringLiteral("profile.scratch"), i);
        if (!scratchArr.at(i).isString()) {
            f.fail(path, QStringLiteral("expected a string such as D100-D2099"));
            break;
        }
        ScratchRange r;
        QString why;
        if (!parseRange(scratchArr.at(i).toString(), r, why, xy)) {
            f.fail(path, why);
            break;
        }
        p.scratch.push_back(r);
    }

    for (auto it = endObj.begin(); it != endObj.end() && !f.failed(); ++it) {
        const QString path = childPath(QStringLiteral("profile.deviceEnd"), it.key());
        const std::optional<DeviceType> t = deviceTypeFromSymbol(it.key());
        if (!t) {
            f.fail(path, QStringLiteral("unknown device type"));
            break;
        }
        QString text;
        if (it.value().isString()) {
            text = it.value().toString();
        } else if (it.value().isDouble()) {
            if (deviceInfo(*t).radix == Radix::Hex) {
                f.fail(path, QStringLiteral("device %1 is hexadecimal: write the number as a "
                                            "string such as \"1FFF\"")
                                 .arg(deviceSymbol(*t)));
                break;
            }
            const double d = it.value().toDouble();
            if (d < 0 || d != static_cast<double>(static_cast<qint64>(d))) {
                f.fail(path, QStringLiteral("expected a non-negative integer"));
                break;
            }
            text = QString::number(static_cast<qint64>(d));
        } else {
            f.fail(path, QStringLiteral("expected a number or a string"));
            break;
        }
        uint32_t number = 0;
        QString why;
        if (!numberFor(*t, text, number, why, xy)) {
            f.fail(path, why);
            break;
        }
        p.deviceEnd[static_cast<size_t>(*t)] = number;
    }

    for (int i = 0; i < supportsArr.size() && !f.failed(); ++i) {
        const QString path = indexPath(QStringLiteral("profile.supports"), i);
        const std::optional<DeviceType> t = supportsArr.at(i).isString()
                                                ? deviceTypeFromSymbol(supportsArr.at(i).toString())
                                                : std::nullopt;
        if (!t) {
            f.fail(path, QStringLiteral("expected a device type symbol such as D"));
            break;
        }
        p.supports.push_back(*t);
    }

    if (!f.failed() && !p.scanTimeDevice.isEmpty()) {
        const QByteArray text = p.scanTimeDevice.toLatin1();
        const Expected<Device> d =
            parseDevice(std::string_view(text.constData(), static_cast<size_t>(text.size())), xy);
        if (!d) {
            f.fail(QStringLiteral("profile.scanTimeDevice"), QStringLiteral("not a device"));
        } else {
            p.scanTime = d.value();
        }
    }

    const auto readSpecial = [&](const QString& text, const char* key, DeviceKind kind,
                                 Device& out) {
        if (f.failed()) {
            return;
        }
        const QString path = childPath(QStringLiteral("profile"), QLatin1String(key));
        const QByteArray bytes = text.toLatin1();
        const Expected<Device> d =
            parseDevice(std::string_view(bytes.constData(), static_cast<size_t>(bytes.size())), xy);
        if (!d) {
            f.fail(path, QStringLiteral("not a device"));
        } else if (deviceInfo(d.value().type).kind != kind) {
            f.fail(path, kind == DeviceKind::Bit ? QStringLiteral("must be a bit device")
                                                 : QStringLiteral("must be a word device"));
        } else {
            out = d.value();
        }
    };
    readSpecial(specialBitText, "specialBit", DeviceKind::Bit, p.specialBit);
    readSpecial(specialWordText, "specialWord", DeviceKind::Word, p.specialWord);

    for (int i = 0; i < familiesArr.size() && !f.failed(); ++i) {
        const QString path = indexPath(QStringLiteral("profile.families"), i);
        bool known = false;
        const QString name =
            familiesArr.at(i).isString() ? familiesArr.at(i).toString() : QString();
        for (const char* k : kFamilies) {
            known = known || name == QLatin1String(k);
        }
        if (!known) {
            f.fail(path, QStringLiteral("expected qna-ethernet, a1e, qna-serial or a1c"));
            break;
        }
        p.families.push_back(name);
    }

    // A scratch range must exist on the PLC: it cannot end beyond a declared deviceEnd.
    for (int i = 0; i < p.scratch.size() && !f.failed(); ++i) {
        const ScratchRange& r = p.scratch[i];
        const std::optional<uint32_t> end = p.end(r.type);
        if (end && r.last > *end) {
            f.fail(indexPath(QStringLiteral("profile.scratch"), i),
                   QStringLiteral("ends beyond deviceEnd of %1").arg(deviceSymbol(r.type)));
        }
    }

    if (!f.failed() && !p.device.subscriptions.isEmpty()) {
        f.fail(QStringLiteral("device.subscriptions"),
               QStringLiteral("must be empty: subscriptions belong to the plan's poll steps"));
    }
    if (!f.failed()) {
        QString where;
        const Expected<void> ok = p.device.validate(&where);
        if (!ok) {
            f.fail(childPath(QStringLiteral("device"), where),
                   QString::fromLatin1(ok.error().message));
        }
    }
    // A heartbeat is a write every round: it must sit in the scratch area (the safety gate's
    // rule applied to the profile's own session).
    if (!f.failed() && p.device.session.heartbeat.enabled) {
        const Device& hb = p.device.session.heartbeat.device;
        if (!p.inScratch(hb.type, hb.number, 1)) {
            f.fail(
                QStringLiteral("device.session.heartbeat.device"),
                QStringLiteral("the heartbeat writes every round: it must lie in a scratch range"));
        }
    }

    if (f.failed()) {
        result.error = f.error();
        return result;
    }
    result.profile = std::move(p);
    return result;
}

ProfileLoad loadProfileFile(const QString& path) {
    ProfileLoad result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = LoadError{
            QString(), QStringLiteral("cannot open %1: %2").arg(path, file.errorString())};
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (doc.isNull() || !doc.isObject()) {
        result.error = LoadError{
            QString(),
            QStringLiteral("%1: not a JSON object (%2)").arg(path, parseError.errorString())};
        return result;
    }
    return loadProfile(doc.object());
}

} // namespace mc::hil
