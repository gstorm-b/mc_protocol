#include "hil_capture/plan.h"

#include "mc/core/convert.h"
#include "mc/core/device.h"

#include <QByteArray>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <string_view>

namespace mc::hil {

namespace {

bool isIdChar(QChar c) {
    return (c >= QLatin1Char('a') && c <= QLatin1Char('z')) ||
           (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) ||
           (c >= QLatin1Char('0') && c <= QLatin1Char('9')) || c == QLatin1Char('-') ||
           c == QLatin1Char('_') || c == QLatin1Char('.');
}

bool validId(const QString& id) {
    if (id.isEmpty() || id.size() > 64) {
        return false;
    }
    for (const QChar c : id) {
        if (!isIdChar(c)) {
            return false;
        }
    }
    return true;
}

// A number written as a JSON integer, "123", "0x1F" or "1FH".
bool parseNumberText(const QString& textIn, uint32_t& out) {
    QString text = textIn.trimmed();
    int base = 10;
    if (text.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
        text = text.mid(2);
        base = 16;
    } else if (text.endsWith(QLatin1Char('H'), Qt::CaseInsensitive)) {
        text.chop(1);
        base = 16;
    }
    if (text.isEmpty()) {
        return false;
    }
    bool ok = false;
    const qulonglong v = text.toULongLong(&ok, base);
    if (!ok || v > 0xFFFFFFFFull) {
        return false;
    }
    out = static_cast<uint32_t>(v);
    return true;
}

bool parseNumberValue(const QJsonValue& v, uint32_t& out) {
    if (v.isDouble()) {
        const double d = v.toDouble();
        if (d < 0 || d > 4294967295.0 || d != static_cast<double>(static_cast<qint64>(d))) {
            return false;
        }
        out = static_cast<uint32_t>(d);
        return true;
    }
    if (v.isString()) {
        return parseNumberText(v.toString(), out);
    }
    return false;
}

bool parseHexText(const QString& text, QByteArray& out) {
    QString digits;
    for (const QChar c : text) {
        if (!c.isSpace()) {
            digits.push_back(c);
        }
    }
    if (digits.size() % 2 != 0) {
        return false;
    }
    const QByteArray bytes = QByteArray::fromHex(digits.toLatin1());
    if (bytes.size() * 2 != digits.size()) {
        return false;
    }
    for (const QChar c : digits) {
        const bool ok = (c >= QLatin1Char('0') && c <= QLatin1Char('9')) ||
                        (c >= QLatin1Char('A') && c <= QLatin1Char('F')) ||
                        (c >= QLatin1Char('a') && c <= QLatin1Char('f'));
        if (!ok) {
            return false;
        }
    }
    out = bytes;
    return true;
}

bool parseLimitName(const QString& name, Limit& out) {
    if (name == QLatin1String("Wmax")) {
        out = Limit::Wmax;
    } else if (name == QLatin1String("BRmax")) {
        out = Limit::BRmax;
    } else if (name == QLatin1String("BWmax")) {
        out = Limit::BWmax;
    } else if (name == QLatin1String("WBRmax")) {
        out = Limit::WBRmax;
    } else if (name == QLatin1String("WBWmax")) {
        out = Limit::WBWmax;
    } else {
        return false;
    }
    return true;
}

bool parseCountText(const QString& textIn, CountExpr& out, QString& why) {
    const QString text = textIn.trimmed();
    out = CountExpr{};
    out.present = true;
    out.text = text;
    static const QRegularExpression minRe(QStringLiteral(R"(^min\(\s*(\w+)\s*,\s*(\w+)\s*\)$)"));
    static const QRegularExpression limitRe(QStringLiteral(R"(^(\w+?)\s*(?:([+-])\s*(\d+))?$)"));
    const QRegularExpressionMatch mm = minRe.match(text);
    if (mm.hasMatch()) {
        out.isMin = true;
        if (!parseLimitName(mm.captured(1), out.a) || !parseLimitName(mm.captured(2), out.b)) {
            why = QStringLiteral("unknown limit name in '%1'").arg(text);
            return false;
        }
        return true;
    }
    bool isNumber = false;
    const qlonglong n = text.toLongLong(&isNumber);
    if (isNumber) {
        if (n < 1 || n > 1000000) {
            why = QStringLiteral("count must be 1..1000000");
            return false;
        }
        out.value = n;
        return true;
    }
    const QRegularExpressionMatch lm = limitRe.match(text);
    if (!lm.hasMatch() || !parseLimitName(lm.captured(1), out.a)) {
        why = QStringLiteral("expected a number or a limit such as Wmax+40, got '%1'").arg(text);
        return false;
    }
    if (!lm.captured(3).isEmpty()) {
        out.delta = lm.captured(3).toLongLong();
        if (lm.captured(2) == QLatin1String("-")) {
            out.delta = -out.delta;
        }
    }
    return true;
}

void parseCount(const QJsonValue& v, CountExpr& out, const QString& path, JsonFailure& f) {
    if (f.failed() || v.isUndefined()) {
        return;
    }
    QString why;
    if (v.isDouble()) {
        const double d = v.toDouble();
        if (d != static_cast<double>(static_cast<qint64>(d))) {
            f.fail(path, QStringLiteral("expected an integer"));
            return;
        }
        if (!parseCountText(QString::number(static_cast<qint64>(d)), out, why)) {
            f.fail(path, why);
        }
    } else if (v.isString()) {
        if (!parseCountText(v.toString(), out, why)) {
            f.fail(path, why);
        }
    } else {
        f.fail(path, QStringLiteral("expected a number or a limit expression"));
    }
}

void appendWords(const ByteBuf& bytes, QVector<uint16_t>& out) {
    for (size_t i = 0; i + 1 < bytes.size(); i += 2) {
        out.push_back(static_cast<uint16_t>(bytes[i] | (bytes[i + 1] << 8)));
    }
}

// One element of a values array: a word (or bit), or a conversion that expands to several words.
bool parseValueItem(const QJsonValue& v, QVector<uint16_t>& out, QString& why) {
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        if (o.size() != 1) {
            why = QStringLiteral("expected one of f64, f32, i32, u32");
            return false;
        }
        const QString key = o.begin().key();
        const QJsonValue val = o.begin().value();
        if (!val.isDouble()) {
            why = QStringLiteral("expected a number for %1").arg(key);
            return false;
        }
        if (key == QLatin1String("f64")) {
            appendWords(convert::fromFloat64({val.toDouble()}), out);
        } else if (key == QLatin1String("f32")) {
            appendWords(convert::fromFloat32({static_cast<float>(val.toDouble())}), out);
        } else if (key == QLatin1String("i32")) {
            appendWords(convert::fromInt32({static_cast<int32_t>(val.toDouble())}), out);
        } else if (key == QLatin1String("u32")) {
            const auto u = static_cast<uint32_t>(val.toDouble());
            out.push_back(static_cast<uint16_t>(u & 0xFFFF));
            out.push_back(static_cast<uint16_t>(u >> 16));
        } else {
            why = QStringLiteral("unknown conversion '%1'").arg(key);
            return false;
        }
        return true;
    }
    uint32_t n = 0;
    if (!parseNumberValue(v, n) || n > 0xFFFF) {
        why = QStringLiteral("expected a number 0..65535");
        return false;
    }
    out.push_back(static_cast<uint16_t>(n));
    return true;
}

bool parseBitString(const QString& text, QVector<uint16_t>& out) {
    for (const QChar c : text) {
        if (c.isSpace()) {
            continue;
        }
        if (c != QLatin1Char('0') && c != QLatin1Char('1')) {
            return false;
        }
        out.push_back(c == QLatin1Char('1') ? 1 : 0);
    }
    return !out.isEmpty();
}

void parseValues(const QJsonValue& v, ValuesSpec& out, const QString& path, JsonFailure& f) {
    if (f.failed() || v.isUndefined()) {
        return;
    }
    if (v.isString()) {
        out.mode = ValuesSpec::Mode::List;
        if (!parseBitString(v.toString(), out.list)) {
            f.fail(path, QStringLiteral("a string must hold 0 and 1 characters, e.g. \"1100\""));
        }
        return;
    }
    if (v.isArray()) {
        out.mode = ValuesSpec::Mode::List;
        const QJsonArray arr = v.toArray();
        for (int i = 0; i < arr.size(); ++i) {
            QString why;
            if (!parseValueItem(arr.at(i), out.list, why)) {
                f.fail(indexPath(path, i), why);
                return;
            }
        }
        if (out.list.isEmpty()) {
            f.fail(path, QStringLiteral("empty values"));
        }
        return;
    }
    if (v.isObject()) {
        ObjectReader r(f, v.toObject(), path);
        const QString gen = r.string("gen", true);
        if (gen == QLatin1String("index")) {
            out.mode = ValuesSpec::Mode::Index;
            out.mul = static_cast<uint32_t>(r.integer("mul", true, 1, 0, 65535));
        } else if (gen == QLatin1String("alt")) {
            out.mode = ValuesSpec::Mode::Alternating;
            out.first = static_cast<uint32_t>(r.integer("first", false, 1, 0, 1));
        } else if (gen == QLatin1String("fill")) {
            out.mode = ValuesSpec::Mode::Fill;
            out.fill = static_cast<uint32_t>(r.integer("value", true, 0, 0, 65535));
        } else if (!f.failed()) {
            f.fail(childPath(path, QStringLiteral("gen")),
                   QStringLiteral("expected index, alt or fill"));
        }
        r.finish();
        return;
    }
    f.fail(path, QStringLiteral("expected an array, a bit string or a generator object"));
}

bool parseExpectKind(const QString& text, ExpectKind& out) {
    if (text == QLatin1String("ok")) {
        out = ExpectKind::Ok;
    } else if (text == QLatin1String("plcError")) {
        out = ExpectKind::PlcError;
    } else if (text == QLatin1String("timeout")) {
        out = ExpectKind::Timeout;
    } else if (text == QLatin1String("noResponse")) {
        out = ExpectKind::NoResponse;
    } else if (text == QLatin1String("notSent")) {
        out = ExpectKind::NotSent;
    } else if (text == QLatin1String("record")) {
        out = ExpectKind::Record;
    } else {
        return false;
    }
    return true;
}

void parseExpect(const QJsonValue& v, Expect& out, bool& present, const QString& path,
                 JsonFailure& f) {
    if (f.failed() || v.isUndefined()) {
        return;
    }
    present = true;
    if (v.isString()) {
        if (!parseExpectKind(v.toString(), out.kind)) {
            f.fail(path, QStringLiteral("expected ok, plcError, timeout, noResponse, notSent or "
                                        "record"));
        }
        out.explicitKind = true;
        return;
    }
    if (!v.isObject()) {
        f.fail(path, QStringLiteral("expected a string or an object"));
        return;
    }
    ObjectReader r(f, v.toObject(), path);
    const QString kind = r.string("kind", false, QStringLiteral("ok"));
    if (!f.failed() && !parseExpectKind(kind, out.kind)) {
        f.fail(childPath(path, QStringLiteral("kind")), QStringLiteral("unknown expectation"));
    }
    out.explicitKind = r.has("kind");
    const QJsonValue values = r.value("values", false);
    if (!values.isUndefined() && !f.failed()) {
        ValuesSpec spec;
        parseValues(values, spec, childPath(path, QStringLiteral("values")), f);
        if (!f.failed() && spec.mode != ValuesSpec::Mode::List) {
            f.fail(childPath(path, QStringLiteral("values")), QStringLiteral("expected a list"));
        }
        out.hasValues = true;
        out.values = spec.list;
    }
    bool bitsPresent = false;
    const QJsonArray bits = r.array("bitsOn", false, &bitsPresent);
    for (int i = 0; i < bits.size() && !f.failed(); ++i) {
        if (!bits.at(i).isDouble() || bits.at(i).toDouble() < 0) {
            f.fail(indexPath(childPath(path, QStringLiteral("bitsOn")), i),
                   QStringLiteral("expected a bit index"));
            break;
        }
        out.bitsOn.push_back(static_cast<int>(bits.at(i).toDouble()));
    }
    out.hasBitsOn = bitsPresent;
    out.valuesFrom = r.string("valuesFrom", false);
    out.frames = static_cast<int>(r.integer("frames", false, -1, 0, 1000));
    r.finish();
}

// Reads the OP fields of `r` into `op`. `isWrite` is fixed by the caller (the step kind).
void parseOp(ObjectReader& r, OpSpec& op, bool isWrite, JsonFailure& f) {
    op.write = isWrite;
    const QString path = r.path();
    const QString device = r.string("device", true);
    if (!f.failed()) {
        QString why;
        if (!parseDeviceRef(device, op.device, why)) {
            f.fail(childPath(path, QStringLiteral("device")), why);
        }
    }
    parseCount(r.value("count", false), op.count, childPath(path, QStringLiteral("count")), f);
    op.unit = r.string("unit", false);
    if (!f.failed() && !op.unit.isEmpty() && op.unit != QLatin1String("bit") &&
        op.unit != QLatin1String("word")) {
        f.fail(childPath(path, QStringLiteral("unit")), QStringLiteral("expected bit or word"));
    }
    if (isWrite) {
        const QJsonValue values = r.value("values", true);
        parseValues(values, op.values, childPath(path, QStringLiteral("values")), f);
        op.readBack = r.boolean("readBack", false);
    } else {
        op.metaKey = r.string("metaKey", false);
    }
    parseExpect(r.value("expect", false), op.expect, op.hasExpect,
                childPath(path, QStringLiteral("expect")), f);
    if (!f.failed() && !op.hasExpect) {
        op.expect.kind = ExpectKind::Ok;
    }
}

bool parseRecover(const QString& text, Recover& out) {
    if (text.isEmpty() || text == QLatin1String("none")) {
        out = Recover::None;
    } else if (text == QLatin1String("reconnect")) {
        out = Recover::Reconnect;
    } else if (text == QLatin1String("eot")) {
        out = Recover::Eot;
    } else {
        return false;
    }
    return true;
}

void parseEdits(const QJsonArray& arr, QVector<Edit>& out, const QString& path, JsonFailure& f) {
    for (int i = 0; i < arr.size() && !f.failed(); ++i) {
        const QString p = indexPath(path, i);
        if (!arr.at(i).isObject() || arr.at(i).toObject().size() != 1) {
            f.fail(p, QStringLiteral("expected an object with one edit"));
            return;
        }
        const QJsonObject o = arr.at(i).toObject();
        const QString key = o.begin().key();
        const QJsonValue val = o.begin().value();
        const QString ep = childPath(p, key);
        Edit e;
        if (key == QLatin1String("setByte") || key == QLatin1String("setNibble") ||
            key == QLatin1String("replaceSum")) {
            if (!val.isObject()) {
                f.fail(ep, QStringLiteral("expected an object"));
                return;
            }
            ObjectReader r(f, val.toObject(), ep);
            if (key == QLatin1String("replaceSum")) {
                e.kind = Edit::Kind::ReplaceSum;
                const bool hasAdd = r.has("add");
                if (hasAdd) {
                    e.relative = true;
                    e.value = static_cast<uint32_t>(r.integer("add", true, 0, 0, 255));
                } else {
                    const QJsonValue v = r.value("value", true);
                    uint32_t n = 0;
                    if (!f.failed() && (!parseNumberValue(v, n) || n > 255)) {
                        f.fail(childPath(ep, QStringLiteral("value")),
                               QStringLiteral("expected a byte 0..255"));
                    }
                    e.value = n;
                }
            } else {
                e.kind =
                    key == QLatin1String("setByte") ? Edit::Kind::SetByte : Edit::Kind::SetNibble;
                e.at = r.integer("at", true, 0, -100000, 100000);
                const QJsonValue v = r.value("value", true);
                uint32_t n = 0;
                const uint32_t maxValue = e.kind == Edit::Kind::SetByte ? 255 : 15;
                if (!f.failed() && (!parseNumberValue(v, n) || n > maxValue)) {
                    f.fail(childPath(ep, QStringLiteral("value")),
                           QStringLiteral("expected 0..%1").arg(maxValue));
                }
                e.value = n;
                if (e.kind == Edit::Kind::SetNibble) {
                    const QString nibble = r.string("nibble", true);
                    if (!f.failed() && nibble != QLatin1String("high") &&
                        nibble != QLatin1String("low")) {
                        f.fail(childPath(ep, QStringLiteral("nibble")),
                               QStringLiteral("expected high or low"));
                    }
                    e.high = nibble == QLatin1String("high");
                }
            }
            r.finish();
        } else if (key == QLatin1String("append")) {
            e.kind = Edit::Kind::Append;
            if (!val.isString() || !parseHexText(val.toString(), e.bytes) || e.bytes.isEmpty()) {
                f.fail(ep, QStringLiteral("expected hexadecimal bytes such as \"30\""));
                return;
            }
        } else if (key == QLatin1String("truncate")) {
            e.kind = Edit::Kind::Truncate;
            if (!val.isDouble() || val.toDouble() < 1 || val.toDouble() > 100000) {
                f.fail(ep, QStringLiteral("expected the number of bytes to remove"));
                return;
            }
            e.count = static_cast<int64_t>(val.toDouble());
        } else {
            f.fail(ep, QStringLiteral("unknown edit; expected setByte, setNibble, append, "
                                      "truncate or replaceSum"));
            return;
        }
        out.push_back(e);
    }
}

void parsePollSub(const QJsonObject& obj, PollSub& sub, const QString& path, JsonFailure& f) {
    ObjectReader r(f, obj, path);
    sub.name = r.string("name", true);
    const QString device = r.string("device", true);
    if (!f.failed()) {
        QString why;
        if (!parseDeviceRef(device, sub.device, why)) {
            f.fail(childPath(path, QStringLiteral("device")), why);
        }
    }
    parseCount(r.value("count", true), sub.count, childPath(path, QStringLiteral("count")), f);
    sub.input = r.boolean("input", false);
    r.finish();
}

void parsePoll(ObjectReader& r, PollSpec& poll, JsonFailure& f) {
    const QString path = r.path();
    const QString heartbeat = r.string("heartbeat", false);
    if (!f.failed() && !heartbeat.isEmpty()) {
        QString why;
        poll.hasHeartbeat = true;
        if (!parseDeviceRef(heartbeat, poll.heartbeat, why)) {
            f.fail(childPath(path, QStringLiteral("heartbeat")), why);
        }
    }
    const QJsonArray subs = r.array("subscribe", true);
    for (int i = 0; i < subs.size() && !f.failed(); ++i) {
        const QString p = indexPath(childPath(path, QStringLiteral("subscribe")), i);
        if (!subs.at(i).isObject()) {
            f.fail(p, QStringLiteral("expected an object"));
            break;
        }
        PollSub sub;
        parsePollSub(subs.at(i).toObject(), sub, p, f);
        poll.subs.push_back(sub);
    }
    poll.rounds = static_cast<int>(r.integer("rounds", true, 1, 1, 100000));
    if (r.has("bitsAsWords")) {
        poll.bitsAsWords = r.boolean("bitsAsWords", true);
    }
    const QJsonArray actions = r.array("actions", false);
    for (int i = 0; i < actions.size() && !f.failed(); ++i) {
        const QString p = indexPath(childPath(path, QStringLiteral("actions")), i);
        if (!actions.at(i).isObject()) {
            f.fail(p, QStringLiteral("expected an object"));
            break;
        }
        ObjectReader ar(f, actions.at(i).toObject(), p);
        PollAction action;
        action.after = static_cast<int>(ar.integer("after", true, 1, 1, 100000));
        bool present = false;
        if (ar.has("write")) {
            action.kind = PollAction::Kind::Write;
            const QJsonObject w = ar.object("write", true, &present);
            ObjectReader wr(f, w, childPath(p, QStringLiteral("write")));
            parseOp(wr, action.write, true, f);
            wr.finish();
        } else if (ar.has("subscribe")) {
            action.kind = PollAction::Kind::Subscribe;
            const QJsonObject s = ar.object("subscribe", true, &present);
            parsePollSub(s, action.sub, childPath(p, QStringLiteral("subscribe")), f);
        } else if (ar.has("unsubscribe")) {
            action.kind = PollAction::Kind::Unsubscribe;
            action.name = ar.string("unsubscribe", true);
        } else if (ar.has("prompt")) {
            action.kind = PollAction::Kind::Prompt;
            action.text = ar.string("prompt", true);
        } else if (!f.failed()) {
            f.fail(p, QStringLiteral("expected write, subscribe, unsubscribe or prompt"));
        }
        ar.finish();
        if (!f.failed() && action.after > poll.rounds) {
            f.fail(childPath(p, QStringLiteral("after")),
                   QStringLiteral("after the last round (%1)").arg(poll.rounds));
        }
        poll.actions.push_back(action);
    }
    // Names: unique, and every unsubscribe names a subscription made before it.
    if (!f.failed()) {
        QSet<QString> names;
        for (const PollSub& s : poll.subs) {
            if (names.contains(s.name)) {
                f.fail(childPath(path, QStringLiteral("subscribe")),
                       QStringLiteral("duplicate subscription name '%1'").arg(s.name));
                return;
            }
            names.insert(s.name);
        }
        // The actions run in the order of their "after", whatever order the plan lists them in
        // (a stable sort: actions of the same round keep their written order). The names are
        // checked in that execution order; a message still names the action's written index.
        QVector<int> order;
        for (int i = 0; i < poll.actions.size(); ++i) {
            order.push_back(i);
        }
        std::stable_sort(order.begin(), order.end(), [&](int x, int y) {
            return poll.actions[x].after < poll.actions[y].after;
        });
        for (const int i : order) {
            const PollAction& a = poll.actions[i];
            const QString p = indexPath(childPath(path, QStringLiteral("actions")), i);
            if (a.kind == PollAction::Kind::Subscribe) {
                if (names.contains(a.sub.name)) {
                    f.fail(p, QStringLiteral("duplicate subscription name '%1'").arg(a.sub.name));
                    return;
                }
                names.insert(a.sub.name);
            } else if (a.kind == PollAction::Kind::Unsubscribe) {
                if (!names.contains(a.name)) {
                    f.fail(p,
                           QStringLiteral("unsubscribe of unknown subscription '%1'").arg(a.name));
                    return;
                }
                names.remove(a.name);
            }
        }
        QVector<PollAction> sorted;
        for (const int i : order) {
            sorted.push_back(poll.actions[i]);
        }
        poll.actions = sorted;
    }
}

const char* const kFrameOverrideHint = "frame key";

// Every key of a frameOverride must be a FrameConfig key of the device JSON.
void checkFrameOverride(const QJsonObject& o, const QString& path, JsonFailure& f) {
    const QJsonObject known = McDeviceConfig{}.toJson().value(QStringLiteral("frame")).toObject();
    for (auto it = o.begin(); it != o.end(); ++it) {
        if (!known.contains(it.key())) {
            f.fail(childPath(path, it.key()),
                   QStringLiteral("unknown %1").arg(QLatin1String(kFrameOverrideHint)));
            return;
        }
    }
}

void parseStep(const QJsonObject& obj, const QString& path, Step& step, JsonFailure& f) {
    ObjectReader r(f, obj, path);
    step.id = r.string("id", true);
    const QString kind = r.string("kind", true);
    if (f.failed()) {
        return;
    }
    if (!validId(step.id)) {
        f.fail(childPath(path, QStringLiteral("id")),
               QStringLiteral("letters, digits, '-', '_' or '.' only"));
        return;
    }
    const qsizetype dash = step.id.indexOf(QLatin1Char('-'));
    step.group = dash < 0 ? step.id : step.id.left(dash);
    step.title = r.string("title", false);
    step.mirrors = r.string("mirrors", false);
    step.expandOver = r.string("foreach", false);
    if (!f.failed() && !step.expandOver.isEmpty() && step.expandOver != QLatin1String("supports") &&
        step.expandOver != QLatin1String("other")) {
        f.fail(childPath(path, QStringLiteral("foreach")),
               QStringLiteral("expected supports or other"));
    }
    const QJsonArray conditions = r.array("requires", false);
    for (int i = 0; i < conditions.size() && !f.failed(); ++i) {
        QString why;
        const QString p = indexPath(childPath(path, QStringLiteral("requires")), i);
        if (!conditions.at(i).isString()) {
            f.fail(p, QStringLiteral("expected a string"));
            break;
        }
        if (!checkCondition(conditions.at(i).toString(), why)) {
            f.fail(p, why);
            break;
        }
        step.conditions.push_back(conditions.at(i).toString());
    }
    step.frameOverride = r.object("frameOverride", false);
    checkFrameOverride(step.frameOverride, childPath(path, QStringLiteral("frameOverride")), f);
    const QString scratch = r.string("scratchTooSmall", false, QStringLiteral("refuse"));
    if (!f.failed() && scratch != QLatin1String("refuse") && scratch != QLatin1String("skip")) {
        f.fail(childPath(path, QStringLiteral("scratchTooSmall")),
               QStringLiteral("expected refuse or skip"));
    }
    step.scratchSkip = scratch == QLatin1String("skip");

    const auto parseThen = [&]() {
        const QJsonArray arr = r.array("then", false);
        for (int i = 0; i < arr.size() && !f.failed(); ++i) {
            const QString p = indexPath(childPath(path, QStringLiteral("then")), i);
            if (!arr.at(i).isObject()) {
                f.fail(p, QStringLiteral("expected an object"));
                break;
            }
            ObjectReader tr(f, arr.at(i).toObject(), p);
            const QString tk = tr.string("kind", true);
            if (!f.failed() && tk != QLatin1String("read") && tk != QLatin1String("write")) {
                f.fail(childPath(p, QStringLiteral("kind")),
                       QStringLiteral("expected read or write"));
                break;
            }
            OpSpec op;
            parseOp(tr, op, tk == QLatin1String("write"), f);
            tr.finish();
            step.then.push_back(op);
        }
    };

    if (kind == QLatin1String("write") || kind == QLatin1String("read")) {
        step.kind = kind == QLatin1String("write") ? StepKind::Write : StepKind::Read;
        parseOp(r, step.op, step.kind == StepKind::Write, f);
        parseThen();
    } else if (kind == QLatin1String("mutate")) {
        step.kind = StepKind::Mutate;
        const QJsonObject req = r.object("request", true);
        if (!f.failed()) {
            ObjectReader rr(f, req, childPath(path, QStringLiteral("request")));
            const QString rk = rr.string("kind", true);
            if (!f.failed() && rk != QLatin1String("read") && rk != QLatin1String("write")) {
                f.fail(childPath(rr.path(), QStringLiteral("kind")),
                       QStringLiteral("expected read or write"));
            }
            parseOp(rr, step.op, rk == QLatin1String("write"), f);
            rr.finish();
        }
        parseEdits(r.array("edits", false), step.edits, childPath(path, QStringLiteral("edits")),
                   f);
        step.readOnly = r.boolean("readOnly", false);
        const QString recover = r.string("recover", false);
        if (!f.failed() && !parseRecover(recover, step.recover)) {
            f.fail(childPath(path, QStringLiteral("recover")),
                   QStringLiteral("expected none, reconnect or eot"));
        }
        parseExpect(r.value("expect", false), step.expect, step.hasExpect,
                    childPath(path, QStringLiteral("expect")), f);
        if (!step.hasExpect) {
            step.expect.kind = ExpectKind::Record;
        }
        parseThen();
    } else if (kind == QLatin1String("raw")) {
        step.kind = StepKind::Raw;
        if (r.has("requests")) {
            const QJsonArray reqs = r.array("requests", true);
            for (int i = 0; i < reqs.size() && !f.failed(); ++i) {
                const QString p = indexPath(childPath(path, QStringLiteral("requests")), i);
                if (!reqs.at(i).isObject()) {
                    f.fail(p, QStringLiteral("expected an object"));
                    break;
                }
                ObjectReader rr(f, reqs.at(i).toObject(), p);
                const QString rk = rr.string("kind", true);
                if (!f.failed() && rk != QLatin1String("read") && rk != QLatin1String("write")) {
                    f.fail(childPath(p, QStringLiteral("kind")),
                           QStringLiteral("expected read or write"));
                    break;
                }
                OpSpec op;
                parseOp(rr, op, rk == QLatin1String("write"), f);
                rr.finish();
                step.rawRequests.push_back(op);
            }
            if (!f.failed() && step.rawRequests.isEmpty()) {
                f.fail(childPath(path, QStringLiteral("requests")), QStringLiteral("empty list"));
            }
        } else {
            step.rawHex = r.string("hex", true);
        }
        if (!f.failed() && step.rawRequests.isEmpty()) {
            QString probe = step.rawHex;
            probe.replace(QStringLiteral("{EOT}"), QStringLiteral("04"));
            QByteArray bytes;
            if (!parseHexText(probe, bytes) || bytes.isEmpty()) {
                f.fail(childPath(path, QStringLiteral("hex")),
                       QStringLiteral("expected hexadecimal bytes"));
            }
        }
        step.readOnly = r.boolean("readOnly", false);
        const QString recover = r.string("recover", false);
        if (!f.failed() && !parseRecover(recover, step.recover)) {
            f.fail(childPath(path, QStringLiteral("recover")),
                   QStringLiteral("expected none, reconnect or eot"));
        }
        parseExpect(r.value("expect", false), step.expect, step.hasExpect,
                    childPath(path, QStringLiteral("expect")), f);
        if (!step.hasExpect) {
            step.expect.kind = ExpectKind::Record;
        }
        parseThen();
    } else if (kind == QLatin1String("poll")) {
        step.kind = StepKind::Poll;
        parsePoll(r, step.poll, f);
        parseExpect(r.value("expect", false), step.expect, step.hasExpect,
                    childPath(path, QStringLiteral("expect")), f);
        if (!step.hasExpect) {
            step.expect.kind = ExpectKind::Record;
        }
    } else if (kind == QLatin1String("bench")) {
        step.kind = StepKind::Bench;
        if (r.has("request")) {
            const QJsonObject req = r.object("request", true);
            step.bench.hasRequest = true;
            if (!f.failed()) {
                ObjectReader rr(f, req, childPath(path, QStringLiteral("request")));
                const QString rk = rr.string("kind", true);
                if (!f.failed() && rk != QLatin1String("read") && rk != QLatin1String("write")) {
                    f.fail(childPath(rr.path(), QStringLiteral("kind")),
                           QStringLiteral("expected read or write"));
                }
                parseOp(rr, step.bench.request, rk == QLatin1String("write"), f);
                rr.finish();
            }
        } else {
            step.bench.pollSet = r.string("pollSet", true);
        }
        step.bench.reps = static_cast<int>(r.integer("reps", false, -1, 1, 1000000));
        step.bench.warmup = static_cast<int>(r.integer("warmup", false, 1, 0, 1000));
        parseExpect(r.value("expect", false), step.expect, step.hasExpect,
                    childPath(path, QStringLiteral("expect")), f);
        if (!step.hasExpect) {
            step.expect.kind = ExpectKind::Record;
        }
    } else if (!f.failed()) {
        f.fail(childPath(path, QStringLiteral("kind")),
               QStringLiteral("expected write, read, poll, mutate, raw or bench"));
    }
    r.finish();
}

} // namespace

bool checkCondition(const QString& textIn, QString& why) {
    QString text = textIn.trimmed();
    if (text.startsWith(QLatin1Char('!'))) {
        text = text.mid(1);
    }
    const qsizetype colon = text.indexOf(QLatin1Char(':'));
    const QString key = colon < 0 ? text : text.left(colon);
    const QString arg = colon < 0 ? QString() : text.mid(colon + 1);
    if (key == QLatin1String("scanTime") && colon < 0) {
        return true;
    }
    if ((key == QLatin1String("supports") || key == QLatin1String("scratch") ||
         key == QLatin1String("endNotMultiple16")) &&
        deviceTypeFromSymbol(arg)) {
        return true;
    }
    if (key == QLatin1String("frame") &&
        (arg == QLatin1String("3E") || arg == QLatin1String("1E") || arg == QLatin1String("3C") ||
         arg == QLatin1String("1C"))) {
        return true;
    }
    if (key == QLatin1String("code") &&
        (arg == QLatin1String("Binary") || arg == QLatin1String("Ascii"))) {
        return true;
    }
    if (key == QLatin1String("format") && arg.size() == 1 && arg[0] >= QLatin1Char('1') &&
        arg[0] <= QLatin1Char('4')) {
        return true;
    }
    if (key == QLatin1String("sumCheck") &&
        (arg == QLatin1String("on") || arg == QLatin1String("off"))) {
        return true;
    }
    if (key == QLatin1String("profile") && !arg.isEmpty()) {
        return true;
    }
    why = QStringLiteral("unknown condition '%1'").arg(textIn);
    return false;
}

bool parseDeviceRef(const QString& textIn, DeviceRef& out, QString& why) {
    const QString text = textIn.trimmed();
    out = DeviceRef{};
    out.text = text;
    if (text.contains(QStringLiteral("{T}"))) {
        out.hasTemplate = true;
        return true;
    }
    if (text == QLatin1String("@scan")) {
        out.base = DeviceRef::Base::ScanTime;
        return true;
    }
    static const QRegularExpression specialRe(QStringLiteral(R"(^(SB|SW)(?:([+-])(\d+))?$)"));
    static const QRegularExpression scratchRe(
        QStringLiteral(R"(^([A-Za-z]+)@s(\d+)?(?:([+-])(\d+))?$)"));
    static const QRegularExpression endRe(QStringLiteral(R"(^([A-Za-z]+)@end(?:([+-])(\d+))?$)"));

    const QRegularExpressionMatch sm = specialRe.match(text);
    if (sm.hasMatch()) {
        out.base = sm.captured(1) == QLatin1String("SB") ? DeviceRef::Base::SpecialBit
                                                         : DeviceRef::Base::SpecialWord;
        if (!sm.captured(3).isEmpty()) {
            out.offset = sm.captured(3).toLongLong();
            if (sm.captured(2) == QLatin1String("-")) {
                out.offset = -out.offset;
            }
        }
        return true;
    }
    const QRegularExpressionMatch scm = scratchRe.match(text);
    if (scm.hasMatch()) {
        const std::optional<DeviceType> t = deviceTypeFromSymbol(scm.captured(1));
        if (!t) {
            why = QStringLiteral("unknown device type '%1'").arg(scm.captured(1));
            return false;
        }
        out.type = *t;
        out.base = DeviceRef::Base::ScratchStart;
        if (!scm.captured(2).isEmpty()) {
            out.base = DeviceRef::Base::ScratchAligned;
            out.align = scm.captured(2).toUInt();
            if (out.align == 0) {
                why = QStringLiteral("alignment must be at least 1");
                return false;
            }
        }
        if (!scm.captured(4).isEmpty()) {
            out.offset = scm.captured(4).toLongLong();
            if (scm.captured(3) == QLatin1String("-")) {
                out.offset = -out.offset;
            }
        }
        return true;
    }
    const QRegularExpressionMatch em = endRe.match(text);
    if (em.hasMatch()) {
        const std::optional<DeviceType> t = deviceTypeFromSymbol(em.captured(1));
        if (!t) {
            why = QStringLiteral("unknown device type '%1'").arg(em.captured(1));
            return false;
        }
        out.type = *t;
        out.base = DeviceRef::Base::End;
        if (!em.captured(3).isEmpty()) {
            out.offset = em.captured(3).toLongLong();
            if (em.captured(2) == QLatin1String("-")) {
                out.offset = -out.offset;
            }
        }
        return true;
    }
    // The plan does not know the PLC's X/Y notation: a literal is only checked for being a device
    // here (any digit a notation allows passes), and resolveRef() reads its number in the
    // profile's xyNotation.
    const QByteArray bytes = text.toLatin1();
    const Expected<Device> d =
        parseDevice(std::string_view(bytes.constData(), static_cast<size_t>(bytes.size())));
    if (!d) {
        why = QStringLiteral("'%1' is not a device reference (D@s, D@s+10, M@s16, D@end, D@end+1, "
                             "SB, SW, @scan or a literal such as D100)")
                  .arg(text);
        return false;
    }
    out.base = DeviceRef::Base::Literal;
    out.type = d.value().type;
    return true;
}

PlanLoad loadPlan(const QJsonObject& root) {
    JsonFailure f;
    PlanLoad result;
    Plan plan;

    ObjectReader top(f, root, QString());
    const qint64 schema = top.integer("schema", true, 0, 0, 1000000);
    if (!f.failed() && schema != 1) {
        f.fail(QStringLiteral("schema"), QStringLiteral("unsupported schema version"));
    }
    const QJsonObject planObj = top.object("plan", true);
    const QJsonArray steps = top.array("steps", true);
    top.finish();

    ObjectReader pr(f, planObj, QStringLiteral("plan"));
    plan.id = pr.string("id", true);
    plan.title = pr.string("title", false);
    plan.family = pr.string("family", false);
    pr.finish();

    QSet<QString> ids;
    for (int i = 0; i < steps.size() && !f.failed(); ++i) {
        const QString path = indexPath(QStringLiteral("steps"), i);
        if (!steps.at(i).isObject()) {
            f.fail(path, QStringLiteral("expected an object"));
            break;
        }
        Step step;
        parseStep(steps.at(i).toObject(), path, step, f);
        if (f.failed()) {
            break;
        }
        if (ids.contains(step.id)) {
            f.fail(childPath(path, QStringLiteral("id")),
                   QStringLiteral("duplicate step id '%1'").arg(step.id));
            break;
        }
        ids.insert(step.id);
        plan.steps.push_back(step);
    }
    // A bench of a poll set names a poll step of this plan.
    for (int i = 0; i < plan.steps.size() && !f.failed(); ++i) {
        const Step& s = plan.steps[i];
        if (s.kind == StepKind::Bench && !s.bench.hasRequest) {
            bool found = false;
            for (const Step& other : plan.steps) {
                found = found || (other.id == s.bench.pollSet && other.kind == StepKind::Poll);
            }
            if (!found) {
                f.fail(childPath(indexPath(QStringLiteral("steps"), i), QStringLiteral("pollSet")),
                       QStringLiteral("no poll step '%1' in this plan").arg(s.bench.pollSet));
            }
        }
    }
    if (f.failed()) {
        result.error = f.error();
        return result;
    }
    result.plan = std::move(plan);
    return result;
}

PlanLoad loadPlanFile(const QString& path) {
    PlanLoad result;
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
    return loadPlan(doc.object());
}

} // namespace mc::hil
