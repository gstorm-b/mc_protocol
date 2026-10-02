/**
 * @file json_reader.h
 * @brief Strict JSON reading shared by the profile and plan loaders: every key a
 * loader does not read is an error, and every error carries its JSON path.
 */
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QString>

#include <cstdint>

namespace mc::hil {

/**
 * @struct LoadError
 * @brief Why a profile or plan was rejected: the JSON path of the offending key and a message.
 */
struct LoadError {
    QString path;    ///< JSON path, e.g. "profile.scratch[2]"; empty for a whole-file problem.
    QString message; ///< What is wrong.

    /// @brief "path: message", or the bare message when there is no path.
    QString text() const {
        return path.isEmpty() ? message : path + QStringLiteral(": ") + message;
    }
};

/// @brief Joins a parent path and a key.
inline QString childPath(const QString& base, const QString& key) {
    return base.isEmpty() ? key : base + QLatin1Char('.') + key;
}

/// @brief Appends an array index to a path.
inline QString indexPath(const QString& base, int index) {
    return QStringLiteral("%1[%2]").arg(base).arg(index);
}

/**
 * @class JsonFailure
 * @brief First-failure-wins error slot shared by every reader of one load.
 */
class JsonFailure {
  public:
    /// @brief Records a failure unless one was recorded already.
    /// @return Always false, so a reader can `return f.fail(...)`.
    bool fail(const QString& path, const QString& message) {
        if (!m_failed) {
            m_failed = true;
            m_error = LoadError{path, message};
        }
        return false;
    }
    /// @brief Whether a failure was recorded.
    bool failed() const { return m_failed; }
    /// @brief The first failure.
    const LoadError& error() const { return m_error; }

  private:
    bool m_failed{false};
    LoadError m_error;
};

/**
 * @class ObjectReader
 * @brief Reads the keys of one JSON object and rejects the ones nobody asked for.
 *
 * After a failure every accessor returns its default and records nothing more, so a loader can
 * run straight-line and check JsonFailure::failed() once per block.
 */
class ObjectReader {
  public:
    /// @brief Starts reading @p obj, reported under @p path.
    ObjectReader(JsonFailure& failure, const QJsonObject& obj, QString path)
        : m_failure(failure), m_obj(obj), m_path(std::move(path)) {}

    /// @brief The path of this object.
    const QString& path() const { return m_path; }
    /// @brief Whether the object has @p key (does not consume it).
    bool has(const char* key) const { return m_obj.contains(QLatin1String(key)); }

    /// @brief Reads a string key.
    QString string(const char* key, bool required, const QString& def = QString()) {
        const QJsonValue v = take(key, required);
        if (v.isUndefined()) {
            return def;
        }
        if (!v.isString()) {
            m_failure.fail(childPath(m_path, QLatin1String(key)),
                           QStringLiteral("expected a string"));
            return def;
        }
        return v.toString();
    }

    /// @brief Reads a boolean key.
    bool boolean(const char* key, bool required, bool def = false) {
        const QJsonValue v = take(key, required);
        if (v.isUndefined()) {
            return def;
        }
        if (!v.isBool()) {
            m_failure.fail(childPath(m_path, QLatin1String(key)),
                           QStringLiteral("expected true or false"));
            return def;
        }
        return v.toBool();
    }

    /// @brief Reads an integer key within [@p lo, @p hi].
    qint64 integer(const char* key, bool required, qint64 def, qint64 lo, qint64 hi) {
        const QJsonValue v = take(key, required);
        if (v.isUndefined()) {
            return def;
        }
        const QString p = childPath(m_path, QLatin1String(key));
        if (!v.isDouble()) {
            m_failure.fail(p, QStringLiteral("expected an integer"));
            return def;
        }
        const double d = v.toDouble();
        if (d != static_cast<double>(static_cast<qint64>(d))) {
            m_failure.fail(p, QStringLiteral("expected an integer"));
            return def;
        }
        const qint64 n = static_cast<qint64>(d);
        if (n < lo || n > hi) {
            m_failure.fail(p, QStringLiteral("out of range %1..%2").arg(lo).arg(hi));
            return def;
        }
        return n;
    }

    /// @brief Reads an object key; @p present tells whether it existed.
    QJsonObject object(const char* key, bool required, bool* present = nullptr) {
        const QJsonValue v = take(key, required);
        if (present != nullptr) {
            *present = !v.isUndefined();
        }
        if (v.isUndefined()) {
            return QJsonObject();
        }
        if (!v.isObject()) {
            m_failure.fail(childPath(m_path, QLatin1String(key)),
                           QStringLiteral("expected an object"));
            return QJsonObject();
        }
        return v.toObject();
    }

    /// @brief Reads an array key; @p present tells whether it existed.
    QJsonArray array(const char* key, bool required, bool* present = nullptr) {
        const QJsonValue v = take(key, required);
        if (present != nullptr) {
            *present = !v.isUndefined();
        }
        if (v.isUndefined()) {
            return QJsonArray();
        }
        if (!v.isArray()) {
            m_failure.fail(childPath(m_path, QLatin1String(key)),
                           QStringLiteral("expected an array"));
            return QJsonArray();
        }
        return v.toArray();
    }

    /// @brief Reads a key of any JSON type; undefined when absent and optional.
    QJsonValue value(const char* key, bool required) { return take(key, required); }

    /// @brief Fails with the first key nobody read (an unknown or misspelled key is an error).
    void finish() {
        if (m_failure.failed()) {
            return;
        }
        for (auto it = m_obj.begin(); it != m_obj.end(); ++it) {
            if (!m_read.contains(it.key())) {
                m_failure.fail(childPath(m_path, it.key()), QStringLiteral("unknown key"));
                return;
            }
        }
    }

  private:
    QJsonValue take(const char* key, bool required) {
        if (m_failure.failed()) {
            return QJsonValue(QJsonValue::Undefined);
        }
        const QString k = QLatin1String(key);
        m_read.insert(k);
        const QJsonValue v = m_obj.value(k);
        if (v.isUndefined() && required) {
            m_failure.fail(childPath(m_path, k), QStringLiteral("missing required key"));
        }
        return v;
    }

    JsonFailure& m_failure;
    QJsonObject m_obj;
    QString m_path;
    QSet<QString> m_read;
};

} // namespace mc::hil
