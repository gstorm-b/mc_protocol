#ifndef QPB_TYPEREGISTRY_H
#define QPB_TYPEREGISTRY_H

#include <qpb/Types.h>
#include <qpb/ValidationResult.h>
#include <qpb/qpbglobal.h>

#include <QtCore/qjsonvalue.h>
#include <QtCore/qlist.h>
#include <QtCore/qmetatype.h>
#include <QtCore/qstring.h>
#include <QtCore/qvariant.h>

#include <functional>
#include <memory>

namespace qpb {

class Property;

namespace detail {
class TypeRegistryPrivate;
}

// UI-independent behaviour of one property type (README.md, "Your own type").
//
// Aggregate that may gain fields at the end in later versions; a field left
// empty always means "default behaviour". Configure it by assigning fields:
//
//   qpb::TypeHandler handler;
//   handler.displayText = [](const QVariant& v, const qpb::Property&) { ... };
struct TypeHandler
{
    // Type every value is converted to before it is stored. An invalid
    // QMetaType stores values unconverted.
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QMetaType storageType;
#else
    // Qt 5 (since 1.7): a type id such as qMetaTypeId<QColor>();
    // QMetaType::UnknownType stores values unconverted.
    int storageType = QMetaType::UnknownType;
#endif
    // Text shown for a value when it is not being edited.
    // Empty: QVariant::toString().
    std::function<QString(const QVariant& value, const Property& property)> displayText;
    // Adjusts a converted value before validation, e.g. clamping to a range.
    // Empty: the value is kept as is.
    std::function<QVariant(const QVariant& value, const Property& property)> normalize;
    // Accepts or rejects a normalized value. Empty: every value is accepted.
    std::function<ValidationResult(const QVariant& value, const Property& property)> validate;
    // Since 1.2. JSON form of a value for qpb::toJson(). Empty:
    // QJsonValue::fromVariant() (types convertible to QString are stored as text).
    std::function<QJsonValue(const QVariant& value, const Property& property)> toJson;
    // Since 1.2. Value for JSON read by qpb::fromJson(); it is then set with
    // Property::setValue() (conversion, validation). Empty: QJsonValue::toVariant().
    std::function<QVariant(const QJsonValue& json, const Property& property)> fromJson;
};

// Registry of property types, keyed by TypeId (README.md, "Your own type").
//
// The built-in types (see Types) are registered the first time global() is
// called. The registry is not thread-safe: register types on the main thread
// before creating properties that use them.
class QPB_CORE_EXPORT TypeRegistry
{
public:
    static TypeRegistry& global();

    ~TypeRegistry();

    TypeRegistry(const TypeRegistry&) = delete;
    TypeRegistry& operator=(const TypeRegistry&) = delete;

    // Registers a new type. Returns false, changing nothing, if id is already
    // registered or reserved (see TypeId).
    bool registerType(const TypeId& id, const TypeHandler& handler);
    // Same, with handler.storageType set to T.
    template <class T> bool registerType(const TypeId& id, TypeHandler handler)
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        handler.storageType = QMetaType::fromType<T>();
#else
        handler.storageType = qMetaTypeId<T>();
#endif
        return registerType(id, handler);
    }

    // Replaces the handler of an already registered type, built-in types
    // included. Returns false if id is not registered.
    bool replaceType(const TypeId& id, const TypeHandler& handler);

    bool contains(const TypeId& id) const;
    // Handler of id, or nullptr. The pointer stays valid until the type is
    // replaced.
    const TypeHandler* handler(const TypeId& id) const;
    // All registered type IDs, built-in ones included, in registration order.
    QList<TypeId> types() const;

private:
    TypeRegistry();

    std::unique_ptr<detail::TypeRegistryPrivate> d;
};

} // namespace qpb

#endif // QPB_TYPEREGISTRY_H
