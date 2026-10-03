#include <qpb/Property.h>
#include <qpb/PropertyGroup.h>
#include <qpb/Serialization.h>
#include <qpb/TypeRegistry.h>

#include <QtCore/qjsonvalue.h>
#include <QtCore/qsettings.h>

#include <functional>

namespace qpb::serialization {

namespace {

const TypeHandler* handlerOf(const Property& property)
{
    return TypeRegistry::global().handler(property.typeId());
}

// Read-only and live properties are maintained by the application (D42, D44).
bool isStored(const Property& property)
{
    return !property.isReadOnly() && !property.isLive();
}

// Visits the stored leaves under group with their path relative to it.
void forEachLeaf(const PropertyGroup& group, const QString& prefix,
    const std::function<void(Property&, const QString&)>& visit)
{
    for (Property* child : group.children()) {
        const QString key = prefix + child->id();
        if (const PropertyGroup* childGroup = child->toGroup())
            forEachLeaf(*childGroup, key + QLatin1Char('/'), visit);
        else if (isStored(*child))
            visit(*child, key);
    }
}

} // namespace

QJsonObject toJson(const PropertyGroup& group)
{
    QJsonObject json;
    for (const Property* child : group.children()) {
        if (const PropertyGroup* childGroup = child->toGroup()) {
            // Like save(): a group with nothing to store leaves no trace.
            const QJsonObject values = toJson(*childGroup);
            if (!values.isEmpty())
                json.insert(child->id(), values);
        } else if (isStored(*child)) {
            const TypeHandler* handler = handlerOf(*child);
            json.insert(child->id(),
                handler && handler->toJson ? handler->toJson(child->value(), *child)
                                           : QJsonValue::fromVariant(child->value()));
        }
    }
    return json;
}

bool fromJson(PropertyGroup& group, const QJsonObject& json)
{
    bool accepted = true;
    for (auto it = json.begin(); it != json.end(); ++it) {
        Property* child = group.child(it.key());
        if (!child)
            continue;
        if (PropertyGroup* childGroup = child->toGroup()) {
            if (it.value().isObject())
                accepted = fromJson(*childGroup, it.value().toObject()) && accepted;
            continue;
        }
        if (!isStored(*child))
            continue;
        const TypeHandler* handler = handlerOf(*child);
        const QVariant value = handler && handler->fromJson ? handler->fromJson(it.value(), *child)
                                                            : it.value().toVariant();
        accepted = child->setValue(value) && accepted;
    }
    return accepted;
}

void save(const PropertyGroup& group, QSettings& settings)
{
    forEachLeaf(group, QString(), [&settings](const Property& property, const QString& key) {
        settings.setValue(key, property.value());
    });
}

bool load(PropertyGroup& group, const QSettings& settings)
{
    bool accepted = true;
    forEachLeaf(group, QString(), [&](Property& property, const QString& key) {
        if (settings.contains(key))
            accepted = property.setValue(settings.value(key)) && accepted;
    });
    return accepted;
}

} // namespace qpb::serialization
