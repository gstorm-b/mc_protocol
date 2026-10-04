#include "mc_workbench/workspace.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSet>

namespace mc::workbench {

namespace {

QString childPath(const QString& base, const QString& key) {
    return base.isEmpty() ? key : base + QLatin1Char('.') + key;
}

QString indexPath(const QString& base, int index) {
    return QStringLiteral("%1[%2]").arg(base).arg(index);
}

// Reads the keys of one object and reports every key nobody asked for (the hil-capture
// ObjectReader, except that all problems are collected).
class Reader {
public:
    Reader(const QJsonObject& obj, QString path, QVector<WorkspaceProblem>& problems)
        : m_obj(obj), m_path(std::move(path)), m_problems(problems) {}

    bool has(const char* key) const { return m_obj.contains(QLatin1String(key)); }

    QString string(const char* key, bool required, const QString& def = QString()) {
        const QJsonValue v = take(key, required);
        if (v.isUndefined()) {
            return def;
        }
        if (!v.isString()) {
            fail(key, QStringLiteral("expected a string"));
            return def;
        }
        return v.toString();
    }

    bool boolean(const char* key, bool def) {
        const QJsonValue v = take(key, false);
        if (v.isUndefined()) {
            return def;
        }
        if (!v.isBool()) {
            fail(key, QStringLiteral("expected true or false"));
            return def;
        }
        return v.toBool();
    }

    qint64 integer(const char* key, qint64 def, qint64 lo, qint64 hi) {
        const QJsonValue v = take(key, false);
        if (v.isUndefined()) {
            return def;
        }
        if (!v.isDouble()) {
            fail(key, QStringLiteral("expected an integer"));
            return def;
        }
        const double d = v.toDouble();
        if (d != static_cast<double>(static_cast<qint64>(d))) {
            fail(key, QStringLiteral("expected an integer"));
            return def;
        }
        const qint64 n = static_cast<qint64>(d);
        if (n < lo || n > hi) {
            fail(key, QStringLiteral("out of range %1..%2").arg(lo).arg(hi));
            return def;
        }
        return n;
    }

    QJsonObject object(const char* key, bool required, bool* present = nullptr) {
        const QJsonValue v = take(key, required);
        if (present != nullptr) {
            *present = !v.isUndefined();
        }
        if (v.isUndefined()) {
            return {};
        }
        if (!v.isObject()) {
            fail(key, QStringLiteral("expected an object"));
            if (present != nullptr) {
                *present = false;
            }
            return {};
        }
        return v.toObject();
    }

    QJsonArray array(const char* key, bool* present = nullptr) {
        const QJsonValue v = take(key, false);
        if (present != nullptr) {
            *present = !v.isUndefined();
        }
        if (v.isUndefined()) {
            return {};
        }
        if (!v.isArray()) {
            fail(key, QStringLiteral("expected an array"));
            if (present != nullptr) {
                *present = false;
            }
            return {};
        }
        return v.toArray();
    }

    void finish() {
        for (auto it = m_obj.begin(); it != m_obj.end(); ++it) {
            if (!m_read.contains(it.key())) {
                m_problems.push_back({childPath(m_path, it.key()), QStringLiteral("unknown key")});
            }
        }
    }

    const QString& path() const { return m_path; }

private:
    QJsonValue take(const char* key, bool required) {
        const QString k = QLatin1String(key);
        m_read.insert(k);
        const QJsonValue v = m_obj.value(k);
        if (v.isUndefined() && required) {
            m_problems.push_back({childPath(m_path, k), QStringLiteral("missing required key")});
        }
        return v;
    }

    void fail(const char* key, const QString& message) {
        m_problems.push_back({childPath(m_path, QLatin1String(key)), message});
    }

    QJsonObject m_obj;
    QString m_path;
    QVector<WorkspaceProblem>& m_problems;
    QSet<QString> m_read;
};

QJsonObject referenceConfigJson() {
    mc::McDeviceConfig cfg;
    mc::SubscriptionSpec spec;
    spec.device = QStringLiteral("D0");
    spec.count = 1;
    cfg.subscriptions.push_back(spec);
    return cfg.toJson();
}

void collectUnknown(const QJsonObject& json, const QJsonObject& reference, const QString& path,
                    QVector<WorkspaceProblem>& out) {
    for (auto it = json.begin(); it != json.end(); ++it) {
        const QString childP = childPath(path, it.key());
        if (!reference.contains(it.key())) {
            out.push_back({childP, QStringLiteral("unknown key")});
            continue;
        }
        const QJsonValue ref = reference.value(it.key());
        if (ref.isObject() && it.value().isObject()) {
            collectUnknown(it.value().toObject(), ref.toObject(), childP, out);
        } else if (ref.isArray() && it.value().isArray()) {
            const QJsonArray refItems = ref.toArray();
            const QJsonArray items = it.value().toArray();
            if (!refItems.isEmpty() && refItems.at(0).isObject()) {
                for (int i = 0; i < items.size(); ++i) {
                    if (items.at(i).isObject()) {
                        collectUnknown(items.at(i).toObject(), refItems.at(0).toObject(),
                                       indexPath(childP, i), out);
                    }
                }
            }
        }
    }
}

bool validSource(const QString& s) {
    return s == QLatin1String("plc") || s == QLatin1String("mock") ||
           s == QLatin1String("virtual_plc");
}

QJsonObject presetToJson(const MemoryPreset& preset) {
    QJsonArray values;
    for (quint16 v : preset.values) {
        values.append(static_cast<int>(v));
    }
    QJsonObject o;
    o.insert(QStringLiteral("head"), preset.head);
    o.insert(QStringLiteral("kind"), preset.bits ? QStringLiteral("bits") : QStringLiteral("words"));
    o.insert(QStringLiteral("values"), values);
    return o;
}

QString base64Of(const QByteArray& bytes) {
    return QString::fromLatin1(bytes.toBase64());
}

// Decodes a base64 string key; a bad encoding is a problem.
QByteArray readBase64(Reader& r, const char* key) {
    const QString text = r.string(key, false);
    if (text.isEmpty()) {
        return {};
    }
    const auto decoded = QByteArray::fromBase64Encoding(
        text.toLatin1(), QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded) {
        // The Reader holds no problem list of its own to append to here, so the caller checks
        // the empty result against a non-empty text.
        return {};
    }
    return *decoded;
}

void readPreset(const QJsonObject& json, const QString& path, QVector<WorkspaceProblem>& problems,
                QVector<MemoryPreset>& out) {
    Reader r(json, path, problems);
    MemoryPreset preset;
    preset.head = r.string("head", true).trimmed();
    const QString kind = r.string("kind", true);
    if (kind == QLatin1String("bits")) {
        preset.bits = true;
    } else if (kind == QLatin1String("words")) {
        preset.bits = false;
    } else if (!kind.isEmpty()) {
        problems.push_back({childPath(path, QStringLiteral("kind")),
                            QStringLiteral("expected \"words\" or \"bits\"")});
    }
    if (preset.head.isEmpty() && json.contains(QStringLiteral("head")) &&
        json.value(QStringLiteral("head")).isString()) {
        problems.push_back({childPath(path, QStringLiteral("head")), QStringLiteral("empty device")});
    }
    bool present = false;
    const QJsonArray values = r.array("values", &present);
    if (!present && !json.contains(QStringLiteral("values"))) {
        problems.push_back({childPath(path, QStringLiteral("values")),
                            QStringLiteral("missing required key")});
    }
    if (values.size() > kMaxPresetPoints) {
        problems.push_back({childPath(path, QStringLiteral("values")),
                            QStringLiteral("more than %1 points").arg(kMaxPresetPoints)});
    } else if (values.isEmpty() && present) {
        problems.push_back({childPath(path, QStringLiteral("values")), QStringLiteral("no points")});
    } else {
        const qint64 hi = preset.bits ? 1 : 65535;
        for (int i = 0; i < values.size(); ++i) {
            const QJsonValue v = values.at(i);
            const double d = v.toDouble(-1);
            if (!v.isDouble() || d != static_cast<double>(static_cast<qint64>(d)) || d < 0 ||
                d > static_cast<double>(hi)) {
                problems.push_back({indexPath(childPath(path, QStringLiteral("values")), i),
                                    QStringLiteral("expected an integer 0..%1").arg(hi)});
                break;
            }
            preset.values.push_back(static_cast<quint16>(d));
        }
    }
    r.finish();
    out.push_back(preset);
}

} // namespace

QVector<WorkspaceProblem> unknownConfigKeys(const QJsonObject& json, const QString& basePath) {
    QVector<WorkspaceProblem> out;
    collectUnknown(json, referenceConfigJson(), basePath, out);
    return out;
}

QJsonObject workspaceToJson(const Workspace& workspace) {
    QJsonArray devices;
    for (const WorkspaceDevice& d : workspace.devices) {
        QJsonObject o;
        o.insert(QStringLiteral("name"), d.name);
        o.insert(QStringLiteral("config"), d.config.toJson());
        devices.append(o);
    }
    QJsonArray mocks;
    for (const WorkspaceMock& m : workspace.mocks) {
        QJsonObject o;
        o.insert(QStringLiteral("name"), m.name);
        o.insert(QStringLiteral("settings"), m.settings);
        QJsonArray presets;
        for (const MemoryPreset& p : m.presets) {
            presets.append(presetToJson(p));
        }
        o.insert(QStringLiteral("memory"), presets);
        mocks.append(o);
    }

    QJsonObject root;
    root.insert(QStringLiteral("format"), QString::fromLatin1(kWorkspaceFormat));
    root.insert(QStringLiteral("version"), kWorkspaceVersion);
    root.insert(QStringLiteral("devices"), devices);
    root.insert(QStringLiteral("mocks"), mocks);
    if (workspace.hasHil) {
        const WorkspaceHil& h = workspace.hil;
        QJsonObject o;
        o.insert(QStringLiteral("profile"), h.profile);
        o.insert(QStringLiteral("plan"), h.plan);
        o.insert(QStringLiteral("groups"), h.groups);
        o.insert(QStringLiteral("plcState"), h.plcState);
        o.insert(QStringLiteral("outputRoot"), h.outputRoot);
        o.insert(QStringLiteral("source"), h.source);
        o.insert(QStringLiteral("note"), h.note);
        o.insert(QStringLiteral("benchReps"), h.benchReps);
        o.insert(QStringLiteral("overwrite"), h.overwrite);
        root.insert(QStringLiteral("hil"), o);
    }
    if (workspace.hasLayout) {
        const WorkspaceLayout& l = workspace.layout;
        QJsonObject o;
        if (!l.windowGeometry.isEmpty()) {
            o.insert(QStringLiteral("windowGeometry"), base64Of(l.windowGeometry));
        }
        if (!l.dockState.isEmpty()) {
            o.insert(QStringLiteral("docks"), base64Of(l.dockState));
        }
        if (l.currentDevice >= 0) {
            o.insert(QStringLiteral("currentDevice"), l.currentDevice);
        }
        if (l.currentMock >= 0) {
            o.insert(QStringLiteral("currentMock"), l.currentMock);
        }
        root.insert(QStringLiteral("layout"), o);
    }
    return root;
}

QByteArray workspaceToBytes(const Workspace& workspace) {
    return QJsonDocument(workspaceToJson(workspace)).toJson(QJsonDocument::Indented);
}

bool workspaceFromJson(const QJsonObject& json, Workspace& out, QVector<WorkspaceProblem>& problems) {
    out = Workspace{};
    problems.clear();
    Workspace ws;

    Reader root(json, QString(), problems);
    const QString format = root.string("format", true);
    if (!format.isEmpty() && format != QLatin1String(kWorkspaceFormat)) {
        problems.push_back({QStringLiteral("format"),
                            QStringLiteral("expected \"%1\"").arg(QLatin1String(kWorkspaceFormat))});
    }
    const qint64 version = root.integer("version", -1, -1, 1000000);
    if (!json.contains(QStringLiteral("version"))) {
        problems.push_back({QStringLiteral("version"), QStringLiteral("missing required key")});
    } else if (version >= 0 && version != kWorkspaceVersion) {
        problems.push_back({QStringLiteral("version"),
                            QStringLiteral("unsupported version %1 (this build reads version %2)")
                                .arg(version)
                                .arg(kWorkspaceVersion)});
    }

    const QJsonArray devices = root.array("devices");
    if (devices.size() > kMaxWorkspaceTabs) {
        problems.push_back({QStringLiteral("devices"),
                            QStringLiteral("more than %1 device tabs").arg(kMaxWorkspaceTabs)});
    } else {
        for (int i = 0; i < devices.size(); ++i) {
            const QString p = indexPath(QStringLiteral("devices"), i);
            if (!devices.at(i).isObject()) {
                problems.push_back({p, QStringLiteral("expected an object")});
                continue;
            }
            Reader r(devices.at(i).toObject(), p, problems);
            WorkspaceDevice device;
            device.name = r.string("name", true).trimmed();
            if (r.has("name") && device.name.isEmpty()) {
                problems.push_back({childPath(p, QStringLiteral("name")), QStringLiteral("empty name")});
            }
            bool present = false;
            const QJsonObject cfgJson = r.object("config", true, &present);
            r.finish();
            if (present) {
                const QString cp = childPath(p, QStringLiteral("config"));
                const QVector<WorkspaceProblem> unknown = unknownConfigKeys(cfgJson, cp);
                problems += unknown;
                QString where;
                const auto parsed = mc::McDeviceConfig::fromJson(cfgJson, &where);
                if (!parsed) {
                    problems.push_back({where.isEmpty() ? cp : childPath(cp, where),
                                        QString::fromUtf8(parsed.error().message)});
                } else {
                    QString validateWhere;
                    const auto valid = parsed.value().validate(&validateWhere);
                    if (!valid) {
                        problems.push_back({validateWhere.isEmpty() ? cp : childPath(cp, validateWhere),
                                            QString::fromUtf8(valid.error().message)});
                    } else {
                        device.config = parsed.value();
                    }
                }
            }
            ws.devices.push_back(device);
        }
    }

    const QJsonArray mocks = root.array("mocks");
    if (mocks.size() > kMaxWorkspaceTabs) {
        problems.push_back({QStringLiteral("mocks"),
                            QStringLiteral("more than %1 mock tabs").arg(kMaxWorkspaceTabs)});
    } else {
        for (int i = 0; i < mocks.size(); ++i) {
            const QString p = indexPath(QStringLiteral("mocks"), i);
            if (!mocks.at(i).isObject()) {
                problems.push_back({p, QStringLiteral("expected an object")});
                continue;
            }
            Reader r(mocks.at(i).toObject(), p, problems);
            WorkspaceMock mock;
            mock.name = r.string("name", true).trimmed();
            if (r.has("name") && mock.name.isEmpty()) {
                problems.push_back({childPath(p, QStringLiteral("name")), QStringLiteral("empty name")});
            }
            mock.settings = r.object("settings", true);
            bool present = false;
            const QJsonArray presets = r.array("memory", &present);
            for (int k = 0; k < presets.size(); ++k) {
                const QString pp = indexPath(childPath(p, QStringLiteral("memory")), k);
                if (!presets.at(k).isObject()) {
                    problems.push_back({pp, QStringLiteral("expected an object")});
                    continue;
                }
                readPreset(presets.at(k).toObject(), pp, problems, mock.presets);
            }
            r.finish();
            ws.mocks.push_back(mock);
        }
    }

    bool present = false;
    const QJsonObject hilJson = root.object("hil", false, &present);
    if (present) {
        Reader r(hilJson, QStringLiteral("hil"), problems);
        WorkspaceHil h;
        h.profile = r.string("profile", false);
        h.plan = r.string("plan", false);
        h.groups = r.string("groups", false);
        h.plcState = r.string("plcState", false, h.plcState);
        if (h.plcState != QLatin1String("RUN") && h.plcState != QLatin1String("STOP")) {
            problems.push_back({QStringLiteral("hil.plcState"),
                                QStringLiteral("expected \"RUN\" or \"STOP\"")});
        }
        h.outputRoot = r.string("outputRoot", false);
        h.source = r.string("source", false, h.source);
        if (!validSource(h.source)) {
            problems.push_back({QStringLiteral("hil.source"),
                                QStringLiteral("expected \"plc\", \"mock\" or \"virtual_plc\"")});
        }
        h.note = r.string("note", false);
        h.benchReps = static_cast<int>(r.integer("benchReps", h.benchReps, 1, 100000));
        h.overwrite = r.boolean("overwrite", false);
        r.finish();
        ws.hasHil = true;
        ws.hil = h;
    }

    const QJsonObject layoutJson = root.object("layout", false, &present);
    if (present) {
        Reader r(layoutJson, QStringLiteral("layout"), problems);
        WorkspaceLayout l;
        l.windowGeometry = readBase64(r, "windowGeometry");
        if (layoutJson.value(QStringLiteral("windowGeometry")).isString() &&
            layoutJson.value(QStringLiteral("windowGeometry")).toString().size() > 0 &&
            l.windowGeometry.isEmpty()) {
            problems.push_back({QStringLiteral("layout.windowGeometry"),
                                QStringLiteral("not valid base64")});
        }
        l.dockState = readBase64(r, "docks");
        if (layoutJson.value(QStringLiteral("docks")).isString() &&
            layoutJson.value(QStringLiteral("docks")).toString().size() > 0 && l.dockState.isEmpty()) {
            problems.push_back({QStringLiteral("layout.docks"), QStringLiteral("not valid base64")});
        }
        l.currentDevice = static_cast<int>(r.integer("currentDevice", -1, 0, kMaxWorkspaceTabs));
        l.currentMock = static_cast<int>(r.integer("currentMock", -1, 0, kMaxWorkspaceTabs));
        r.finish();
        ws.hasLayout = true;
        ws.layout = l;
    }

    root.finish();
    if (!problems.isEmpty()) {
        return false;
    }
    out = ws;
    return true;
}

bool workspaceFromBytes(const QByteArray& bytes, Workspace& out, QVector<WorkspaceProblem>& problems) {
    out = Workspace{};
    problems.clear();
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError) {
        problems.push_back({QString(), QStringLiteral("not valid JSON: %1 (at byte %2)")
                                           .arg(error.errorString())
                                           .arg(error.offset)});
        return false;
    }
    if (!doc.isObject()) {
        problems.push_back({QString(), QStringLiteral("not a workspace: the top level is not an object")});
        return false;
    }
    return workspaceFromJson(doc.object(), out, problems);
}

QString workspaceProblemsText(const QVector<WorkspaceProblem>& problems, int maxLines) {
    QStringList lines;
    const int shown = qMin(static_cast<int>(problems.size()), qMax(1, maxLines));
    for (int i = 0; i < shown; ++i) {
        lines.append(problems.at(i).text());
    }
    if (problems.size() > shown) {
        lines.append(QStringLiteral("... and %1 more").arg(problems.size() - shown));
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace mc::workbench
