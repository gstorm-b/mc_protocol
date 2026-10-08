#ifndef QPB_COMPAT_P_H
#define QPB_COMPAT_P_H

// The Qt API qpb's sources use that differs between Qt 6 and Qt 5.15 (since
// 1.7). Sources call these instead of the Qt functions, so every difference
// between the two configurations is in this file.

#include <QtCore/qglobal.h>
#include <QtCore/qjsonvalue.h>
#include <QtCore/qmetaobject.h>
#include <QtCore/qmetatype.h>
#include <QtCore/qnumeric.h>
#include <QtCore/qregularexpression.h>
#include <QtCore/qsortfilterproxymodel.h>
#include <QtCore/qvariant.h>

#include <limits>

namespace qpb::detail {

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)

// The type of TypeHandler::storageType.
using StorageType = QMetaType;

template <class T> StorageType storageTypeFor()
{
    return QMetaType::fromType<T>();
}

inline bool isValidStorageType(const StorageType& type)
{
    return type.isValid();
}

inline StorageType storageTypeOf(const QVariant& value)
{
    return value.metaType();
}

inline StorageType storageTypeOf(const QMetaProperty& property)
{
    return property.metaType();
}

inline int typeIdOf(const StorageType& type)
{
    return type.id();
}

inline const char* typeNameOf(const StorageType& type)
{
    return type.name();
}

inline bool convertTo(QVariant& value, const StorageType& type)
{
    return value.convert(type);
}

// A JSON number as an integer (only called for numbers up to 2^53).
inline qint64 jsonInteger(const QJsonValue& json)
{
    return json.toInteger();
}

// a + b and a * b; false with *result set, or true when the result overflows.
inline bool addOverflow(qint64 a, qint64 b, qint64* result)
{
    return qAddOverflow(a, b, result);
}

inline bool mulOverflow(qint64 a, qint64 b, qint64* result)
{
    return qMulOverflow(a, b, result);
}

// The filter of proxy, whichever setFilter...() function set it.
inline QRegularExpression filterExpression(const QSortFilterProxyModel& proxy)
{
    return proxy.filterRegularExpression();
}

#else

using StorageType = int; // a QMetaType::Type or qMetaTypeId<T>()

template <class T> StorageType storageTypeFor()
{
    return qMetaTypeId<T>();
}

inline bool isValidStorageType(StorageType type)
{
    return type != QMetaType::UnknownType;
}

inline StorageType storageTypeOf(const QVariant& value)
{
    return value.userType();
}

inline StorageType storageTypeOf(const QMetaProperty& property)
{
    return property.userType();
}

inline int typeIdOf(StorageType type)
{
    return type;
}

inline const char* typeNameOf(StorageType type)
{
    return QMetaType::typeName(type);
}

inline bool convertTo(QVariant& value, StorageType type)
{
    return value.convert(type);
}

// Qt 5 stores every JSON number as a double, exact up to 2^53.
inline qint64 jsonInteger(const QJsonValue& json)
{
    return qint64(json.toDouble());
}

// qAddOverflow() and qMulOverflow() are not public in Qt 5.
inline bool addOverflow(qint64 a, qint64 b, qint64* result)
{
    constexpr qint64 max = std::numeric_limits<qint64>::max();
    constexpr qint64 min = std::numeric_limits<qint64>::min();
    if ((b > 0 && a > max - b) || (b < 0 && a < min - b))
        return true;
    *result = a + b;
    return false;
}

inline bool mulOverflow(qint64 a, qint64 b, qint64* result)
{
    constexpr qint64 max = std::numeric_limits<qint64>::max();
    constexpr qint64 min = std::numeric_limits<qint64>::min();
    const bool overflow = a > 0 ? (b > 0 ? a > max / b : b < min / a)
                                : (b > 0 ? a < min / b : (a != 0 && b < max / a));
    if (overflow)
        return true;
    *result = a * b;
    return false;
}

// Qt 5's setFilterFixedString() and setFilterWildcard() set the QRegExp
// filter, not filterRegularExpression(): convert it, for partial matches as
// QSortFilterProxyModel does.
inline QRegularExpression filterExpression(const QSortFilterProxyModel& proxy)
{
    const QRegularExpression expression = proxy.filterRegularExpression();
    if (!expression.pattern().isEmpty())
        return expression;
    const QRegExp regExp = proxy.filterRegExp();
    QString pattern = regExp.pattern();
    if (pattern.isEmpty())
        return expression;
    switch (regExp.patternSyntax()) {
    case QRegExp::FixedString:
        pattern = QRegularExpression::escape(pattern);
        break;
    case QRegExp::Wildcard:
    case QRegExp::WildcardUnix:
        pattern = QRegularExpression::wildcardToRegularExpression(pattern);
        // \A(?:...)\z: a whole-string match; QRegExp found it anywhere.
        if (pattern.startsWith(QLatin1String("\\A(?:")) && pattern.endsWith(QLatin1String(")\\z")))
            pattern = pattern.mid(5, pattern.size() - 8);
        break;
    default:
        break;
    }
    return QRegularExpression(pattern,
        regExp.caseSensitivity() == Qt::CaseInsensitive ? QRegularExpression::CaseInsensitiveOption
                                                        : QRegularExpression::NoPatternOption);
}

#endif

} // namespace qpb::detail

#endif // QPB_COMPAT_P_H
