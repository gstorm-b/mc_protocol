#include <qpb/Attributes.h>
#include <qpb/Property.h>
#include <qpb/TypeRegistry.h>

#include <QtCore/qdir.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qhash.h>
#include <QtCore/qlocale.h>
#include <QtCore/qregularexpression.h>

#include <cmath>
#include <limits>
#include <vector>
#include "core/compat_p.h"

namespace qpb {

namespace detail {

class TypeRegistryPrivate
{
public:
    void insert(const TypeId& id, const TypeHandler& handler)
    {
        index.insert(id, handlers.size());
        handlers.push_back(std::make_unique<TypeHandler>(handler));
        order.append(id);
    }

    // Stable addresses: handler() returns pointers into this storage.
    std::vector<std::unique_ptr<TypeHandler>> handlers;
    QHash<TypeId, size_t> index;
    QList<TypeId> order;
};

} // namespace detail

namespace {

QString affixed(const QString& number, const Property& property)
{
    return property.attribute(Attr::Prefix).toString() + number
        + property.attribute(Attr::Suffix).toString();
}

// --- Bool ------------------------------------------------------------------------

TypeHandler boolHandler()
{
    TypeHandler handler;
    handler.storageType = detail::storageTypeFor<bool>();
    return handler;
}

// --- Int -------------------------------------------------------------------------

TypeHandler intHandler()
{
    TypeHandler handler;
    handler.storageType = detail::storageTypeFor<int>();
    handler.displayText = [](const QVariant& value, const Property& property) {
        return affixed(QLocale().toString(value.toInt()), property);
    };
    handler.normalize = [](const QVariant& value, const Property& property) {
        const int minimum
            = property.attribute(Attr::Minimum, std::numeric_limits<int>::min()).toInt();
        const int maximum
            = property.attribute(Attr::Maximum, std::numeric_limits<int>::max()).toInt();
        return QVariant(qBound(minimum, value.toInt(), qMax(minimum, maximum)));
    };
    return handler;
}

// --- Int64 -----------------------------------------------------------------------

TypeHandler int64Handler()
{
    TypeHandler handler;
    handler.storageType = detail::storageTypeFor<qint64>();
    handler.displayText = [](const QVariant& value, const Property& property) {
        return affixed(QLocale().toString(value.toLongLong()), property);
    };
    handler.normalize = [](const QVariant& value, const Property& property) {
        const qint64 minimum
            = property.attribute(Attr::Minimum, std::numeric_limits<qint64>::min()).toLongLong();
        const qint64 maximum
            = property.attribute(Attr::Maximum, std::numeric_limits<qint64>::max()).toLongLong();
        return QVariant::fromValue(qBound(minimum, value.toLongLong(), qMax(minimum, maximum)));
    };
    // JSON numbers are doubles: values beyond 2^53 are written as strings.
    handler.toJson = [](const QVariant& value, const Property&) {
        constexpr qint64 exact = qint64(1) << 53;
        const qint64 number = value.toLongLong();
        return number >= -exact && number <= exact ? QJsonValue(number)
                                                   : QJsonValue(QString::number(number));
    };
    handler.fromJson = [](const QJsonValue& json, const Property&) {
        if (json.isString())
            return QVariant(json.toString());
        return json.isDouble() ? QVariant::fromValue(detail::jsonInteger(json)) : json.toVariant();
    };
    return handler;
}

// --- Double ----------------------------------------------------------------------

constexpr int DefaultDecimals = 2;

int decimalsOf(const Property& property)
{
    return qBound(0, property.attribute(Attr::Decimals, DefaultDecimals).toInt(), 15);
}

TypeHandler doubleHandler()
{
    TypeHandler handler;
    handler.storageType = detail::storageTypeFor<double>();
    handler.displayText = [](const QVariant& value, const Property& property) {
        const QLocale locale;
        QString text = locale.toString(value.toDouble(), 'f', decimalsOf(property));
        const QString point = locale.decimalPoint();
        if (text.contains(point)) {
            while (text.endsWith(locale.zeroDigit()))
                text.chop(QString(locale.zeroDigit()).size()); // a QChar in Qt 5
            if (text.endsWith(point))
                text.chop(point.size());
        }
        return affixed(text, property);
    };
    handler.normalize = [](const QVariant& value, const Property& property) {
        double number = value.toDouble();
        if (std::isnan(number))
            return value;
        const double minimum
            = property.attribute(Attr::Minimum, -std::numeric_limits<double>::infinity())
                  .toDouble();
        const double maximum
            = property.attribute(Attr::Maximum, std::numeric_limits<double>::infinity()).toDouble();
        number = qBound(minimum, number, qMax(minimum, maximum));
        if (std::isfinite(number)) {
            const double scale = std::pow(10.0, decimalsOf(property));
            const double rounded = std::round(number * scale) / scale;
            if (std::isfinite(rounded))
                number = rounded;
        }
        return QVariant(number);
    };
    handler.validate = [](const QVariant& value, const Property&) {
        return std::isnan(value.toDouble())
            ? ValidationResult::error(QStringLiteral("The value is not a number"))
            : ValidationResult::valid();
    };
    return handler;
}

// --- String ----------------------------------------------------------------------

TypeHandler stringHandler()
{
    TypeHandler handler;
    handler.storageType = detail::storageTypeFor<QString>();
    handler.displayText = [](const QVariant& value, const Property& property) {
        QString text = value.toString();
        if (property.attribute(Attr::Multiline).toBool()) {
            // One line per cell: line breaks become a pilcrow.
            text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
            text.replace(QLatin1Char('\n'), QStringLiteral(u" \u00B6 "));
        }
        return text;
    };
    handler.validate = [](const QVariant& value, const Property& property) {
        const QString text = value.toString();
        if (property.hasAttribute(Attr::MaxLength)) {
            const int maxLength = property.attribute(Attr::MaxLength).toInt();
            if (maxLength >= 0 && text.size() > maxLength)
                return ValidationResult::error(
                    QStringLiteral("At most %1 characters are allowed").arg(maxLength));
        }
        const QString pattern = property.attribute(Attr::RegularExpression).toString();
        if (!pattern.isEmpty()) {
            const QRegularExpression expression(QRegularExpression::anchoredPattern(pattern));
            if (!expression.isValid()) {
                qWarning("qpb: invalid regular expression \"%s\" on property \"%s\": %s",
                    qUtf8Printable(pattern), qUtf8Printable(property.path()),
                    qUtf8Printable(expression.errorString()));
            } else if (!expression.match(text).hasMatch()) {
                return ValidationResult::error(
                    QStringLiteral("The value does not match the pattern %1").arg(pattern));
            }
        }
        return ValidationResult::valid();
    };
    return handler;
}

// --- Enum ------------------------------------------------------------------------

QList<EnumOption> optionsOf(const Property& property)
{
    return property.attribute(Attr::Options).value<QList<EnumOption>>();
}

TypeHandler enumHandler()
{
    TypeHandler handler; // values are int or QString: stored unconverted
    handler.displayText = [](const QVariant& value, const Property& property) {
        for (const EnumOption& option : optionsOf(property)) {
            if (option.value == value)
                return option.label;
        }
        return value.toString();
    };
    // Map a value of another type ("1" for 1, 1 for "1") onto the matching option.
    handler.normalize = [](const QVariant& value, const Property& property) {
        const QList<EnumOption> options = optionsOf(property);
        for (const EnumOption& option : options) {
            if (option.value == value)
                return option.value;
        }
        for (const EnumOption& option : options) {
            QVariant converted = value;
            if (detail::convertTo(converted, detail::storageTypeOf(option.value)) && converted == option.value)
                return option.value;
        }
        return value;
    };
    handler.validate = [](const QVariant& value, const Property& property) {
        for (const EnumOption& option : optionsOf(property)) {
            if (option.value == value)
                return ValidationResult::valid();
        }
        return ValidationResult::error(
            QStringLiteral("\"%1\" is not one of the options").arg(value.toString()));
    };
    return handler;
}

// --- FilePath / DirPath ----------------------------------------------------------

QString nativePath(const QVariant& value, const Property&)
{
    return QDir::toNativeSeparators(value.toString());
}

TypeHandler filePathHandler()
{
    TypeHandler handler;
    handler.storageType = detail::storageTypeFor<QString>();
    handler.displayText = nativePath;
    handler.validate = [](const QVariant& value, const Property& property) {
        const QString path = value.toString();
        const bool mustExist = property.attribute(Attr::MustExist, false).toBool();
        const auto mode
            = FileMode(property.attribute(Attr::DialogMode, int(FileMode::Open)).toInt());
        if (path.isEmpty() || !mustExist || mode != FileMode::Open || QFileInfo(path).isFile())
            return ValidationResult::valid();
        return ValidationResult::error(QStringLiteral("The file %1 does not exist").arg(path));
    };
    return handler;
}

TypeHandler dirPathHandler()
{
    TypeHandler handler;
    handler.storageType = detail::storageTypeFor<QString>();
    handler.displayText = nativePath;
    handler.validate = [](const QVariant& value, const Property& property) {
        const QString path = value.toString();
        const bool mustExist = property.attribute(Attr::MustExist, false).toBool();
        if (path.isEmpty() || !mustExist || QFileInfo(path).isDir())
            return ValidationResult::valid();
        return ValidationResult::error(QStringLiteral("The directory %1 does not exist").arg(path));
    };
    return handler;
}

bool isReserved(const TypeId& id)
{
    return id.isEmpty() || id.startsWith(QLatin1String("qpb.")) || id == Types::Group;
}

} // namespace

TypeRegistry& TypeRegistry::global()
{
    static TypeRegistry registry;
    return registry;
}

TypeRegistry::TypeRegistry()
    : d(std::make_unique<detail::TypeRegistryPrivate>())
{
    d->insert(Types::Bool, boolHandler());
    d->insert(Types::Int, intHandler());
    d->insert(Types::Double, doubleHandler());
    d->insert(Types::String, stringHandler());
    d->insert(Types::Enum, enumHandler());
    d->insert(Types::FilePath, filePathHandler());
    d->insert(Types::DirPath, dirPathHandler());
    d->insert(Types::Int64, int64Handler()); // 1.2: after the types of 1.0
}

TypeRegistry::~TypeRegistry() = default;

bool TypeRegistry::registerType(const TypeId& id, const TypeHandler& handler)
{
    if (isReserved(id) || contains(id))
        return false;
    d->insert(id, handler);
    return true;
}

bool TypeRegistry::replaceType(const TypeId& id, const TypeHandler& handler)
{
    const auto it = d->index.constFind(id);
    if (it == d->index.constEnd())
        return false;
    *d->handlers[*it] = handler;
    return true;
}

bool TypeRegistry::contains(const TypeId& id) const
{
    return d->index.contains(id);
}

const TypeHandler* TypeRegistry::handler(const TypeId& id) const
{
    const auto it = d->index.constFind(id);
    return it == d->index.constEnd() ? nullptr : d->handlers[*it].get();
}

QList<TypeId> TypeRegistry::types() const
{
    return d->order;
}

} // namespace qpb
