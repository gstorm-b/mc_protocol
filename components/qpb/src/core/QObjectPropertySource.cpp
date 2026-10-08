#include <qpb/Attributes.h>
#include <qpb/Property.h>
#include <qpb/PropertyGroup.h>
#include <qpb/PropertyModel.h>
#include <qpb/QObjectPropertySource.h>
#include <qpb/TypeRegistry.h>
#include <qpb/Types.h>

#include "QObjectPropertySource_p.h"
#include "core/compat_p.h"

namespace qpb {

namespace detail {

namespace {

constexpr char ClassInfoPrefix[] = "qpb:";

// "min=0;max=10;readOnly" as key/value pairs; a key alone means true.
QVariantMap parseMetadata(const QMetaObject* meta, const QString& name)
{
    QVariantMap result;
    const QByteArray key = QByteArray(ClassInfoPrefix) + name.toLatin1();
    const int index = meta->indexOfClassInfo(key.constData());
    if (index < 0)
        return result;
    const QString text = QString::fromUtf8(meta->classInfo(index).value());
    for (const QString& entry : text.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const qsizetype equals = entry.indexOf(QLatin1Char('='));
        QString field = (equals < 0 ? entry : entry.left(equals)).trimmed();
        if (field == QLatin1String("min"))
            field = Attr::Minimum;
        else if (field == QLatin1String("max"))
            field = Attr::Maximum;
        if (field.isEmpty())
            continue;
        // Values keep their spaces (suffix= deg).
        result.insert(field, equals < 0 ? QVariant(true) : QVariant(entry.mid(equals + 1)));
    }
    return result;
}

bool flag(const QVariantMap& metadata, const char* key)
{
    const QVariant value = metadata.value(QLatin1String(key));
    if (!value.isValid())
        return false;
    return detail::typeIdOf(detail::storageTypeOf(value)) == QMetaType::Bool ? value.toBool()
                                             : value.toString().trimmed() != QLatin1String("false");
}

// The Q_PROPERTYs to show: "qpb:properties" if present, otherwise all but QObject's.
QList<QMetaProperty> propertiesOf(const QMetaObject* meta)
{
    QList<QMetaProperty> result;
    const int listIndex = meta->indexOfClassInfo("qpb:properties");
    if (listIndex >= 0) {
        const QString names = QString::fromUtf8(meta->classInfo(listIndex).value());
        for (const QString& name : names.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const int index = meta->indexOfProperty(name.trimmed().toLatin1().constData());
            if (index >= 0)
                result << meta->property(index);
        }
        return result;
    }
    for (int i = QObject::staticMetaObject.propertyCount(); i < meta->propertyCount(); ++i)
        result << meta->property(i);
    return result;
}

// TypeId for a Q_PROPERTY of type metaType; empty if none fits.
TypeId typeFor(StorageType metaType)
{
    switch (typeIdOf(metaType)) {
    case QMetaType::Bool:
        return Types::Bool;
    case QMetaType::Int:
        return Types::Int;
    case QMetaType::LongLong:
        return Types::Int64;
    case QMetaType::Double:
    case QMetaType::Float:
        return Types::Double;
    case QMetaType::QString:
        return Types::String; // file and directory paths need "type=" metadata
    default:
        break;
    }
    const TypeRegistry& registry = TypeRegistry::global();
    const QList<TypeId> types = registry.types();
    for (const TypeId& id : types) {
        if (registry.handler(id)->storageType == metaType)
            return id;
    }
    return {};
}

// Value of a Q_PROPERTY as stored in a property (enums as int).
QVariant readValue(const QMetaProperty& property, const QObject* object)
{
    const QVariant value = property.read(object);
    return property.isEnumType() ? QVariant(value.toInt()) : value;
}

// A group id not used by another child of parent.
QString uniqueId(const PropertyGroup& parent, const QString& base)
{
    QString id = base;
    for (int n = 2; parent.child(id); ++n)
        id = base + QLatin1Char('_') + QString::number(n);
    return id;
}

} // namespace

QObjectPropertySourcePrivate::QObjectPropertySourcePrivate(PropertyModel* propertyModel)
    : model(propertyModel)
{
    if (model) {
        modelConnection = connect(model, &PropertyModel::valueChanged, this,
            [this](const QString& path, const QVariant& value) { modelValueChanged(path, value); });
    }
}

QObjectPropertySourcePrivate::~QObjectPropertySourcePrivate()
{
    for (const auto& binding : bindings) {
        for (const QMetaObject::Connection& connection : std::as_const(binding->connections))
            disconnect(connection);
    }
}

ObjectBinding* QObjectPropertySourcePrivate::bindingOf(const QObject* object) const
{
    for (const auto& binding : bindings) {
        if (binding->key == object)
            return binding.get();
    }
    return nullptr;
}

PropertyGroup* QObjectPropertySourcePrivate::groupOf(const ObjectBinding& binding) const
{
    Property* property = model ? model->find(binding.groupPath) : nullptr;
    return property ? property->toGroup() : nullptr;
}

void QObjectPropertySourcePrivate::remove(ObjectBinding* binding, bool removeGroup)
{
    for (const QMetaObject::Connection& connection : std::as_const(binding->connections))
        disconnect(connection);
    if (removeGroup) {
        if (PropertyGroup* group = groupOf(*binding)) {
            if (PropertyGroup* parent = group->parent())
                parent->remove(group->id());
        }
    }
    for (auto it = bindings.begin(); it != bindings.end(); ++it) {
        if (it->get() == binding) {
            bindings.erase(it);
            break;
        }
    }
}

void QObjectPropertySourcePrivate::read(ObjectBinding& binding, const QString& id)
{
    PropertyGroup* group = groupOf(binding);
    Property* property = group ? group->child(id) : nullptr;
    if (!property || !binding.object)
        return;
    const QVariant value = readValue(binding.properties.value(id), binding.object);
    const bool wasUpdating = updating;
    updating = true;
    property->setValue(value);
    updating = wasUpdating;
}

void QObjectPropertySourcePrivate::modelValueChanged(const QString& path, const QVariant& value)
{
    if (updating)
        return;
    for (const auto& binding : bindings) {
        const QString prefix = binding->groupPath + QLatin1Char('/');
        if (!path.startsWith(prefix))
            continue;
        const QString id = path.mid(prefix.size());
        const auto it = binding->properties.constFind(id);
        if (it == binding->properties.constEnd() || !binding->object)
            return;
        updating = true;
        it->write(binding->object, value);
        updating = false;
        // The object may have refused or adjusted the value.
        if (readValue(*it, binding->object) != value)
            read(*binding, id);
        return;
    }
}

void QObjectPropertySourcePrivate::notified()
{
    ObjectBinding* binding = bindingOf(sender());
    if (!binding)
        return;
    const int signal = senderSignalIndex();
    const QStringList ids = binding->notified.value(signal);
    for (const QString& id : ids)
        read(*binding, id);
    if (binding->title.isValid() && binding->title.notifySignalIndex() == signal)
        readTitle(*binding);
}

void QObjectPropertySourcePrivate::readTitle(ObjectBinding& binding)
{
    PropertyGroup* group = groupOf(binding);
    if (!group || !binding.object)
        return;
    const QString title = binding.title.read(binding.object).toString();
    if (!title.isEmpty())
        group->setDisplayName(title);
}

} // namespace detail

QObjectPropertySource::QObjectPropertySource(PropertyModel* model, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<detail::QObjectPropertySourcePrivate>(model))
{ }

QObjectPropertySource::~QObjectPropertySource() = default;

PropertyModel* QObjectPropertySource::model() const
{
    return d->model;
}

PropertyGroup* QObjectPropertySource::addObject(
    QObject* object, PropertyGroup* parentGroup, const QString& id)
{
    if (!object || !d->model)
        return nullptr;
    if (detail::ObjectBinding* existing = d->bindingOf(object))
        return d->groupOf(*existing);

    if (!parentGroup && !d->model->root())
        d->model->setRoot(PropertyGroup::create(QStringLiteral("root")));
    PropertyGroup* parent = parentGroup ? parentGroup : d->model->root();
    const QMetaObject* meta = object->metaObject();
    QString base = id;
    if (base.isEmpty())
        base = object->objectName();
    if (base.isEmpty())
        base = QString::fromLatin1(meta->className());
    PropertyGroup& group = parent->addGroup(detail::uniqueId(*parent, base));

    auto binding = std::make_unique<detail::ObjectBinding>();
    binding->object = object;
    binding->key = object;
    binding->groupPath = group.path();
    const QMetaMethod slot = d->metaObject()->method(d->metaObject()->indexOfSlot("notified()"));

    for (const QMetaProperty& metaProperty : detail::propertiesOf(meta)) {
        if (!metaProperty.isReadable() || metaProperty.isFlagType())
            continue;
        const QString name = QString::fromLatin1(metaProperty.name());
        QVariantMap metadata = detail::parseMetadata(meta, name);
        if (detail::flag(metadata, "exclude"))
            continue;
        const QVariant value = detail::readValue(metaProperty, object);

        Property* property = nullptr;
        if (metaProperty.isEnumType()) {
            const QMetaEnum enumerator = metaProperty.enumerator();
            QList<EnumOption> options;
            for (int i = 0; i < enumerator.keyCount(); ++i) {
                EnumOption option;
                option.label = QString::fromLatin1(enumerator.key(i));
                option.value = enumerator.value(i);
                options << option;
            }
            property = &group.addEnum(name, options, value).property();
        } else {
            const TypeId type = metadata.contains(QStringLiteral("type"))
                ? metadata.value(QStringLiteral("type")).toString().trimmed()
                : detail::typeFor(detail::storageTypeOf(metaProperty));
            if (type.isEmpty() || !TypeRegistry::global().contains(type))
                continue;
            property = &group.add(type, name, value);
        }

        for (auto it = metadata.constBegin(); it != metadata.constEnd(); ++it) {
            const QString& key = it.key();
            if (key == QLatin1String("type") || key == QLatin1String("exclude"))
                continue;
            if (key == QLatin1String("displayName"))
                property->setDisplayName(it.value().toString());
            else if (key == QLatin1String("toolTip"))
                property->setToolTip(it.value().toString());
            else if (key == QLatin1String("readOnly"))
                property->setReadOnly(detail::flag(metadata, "readOnly"));
            else if (key == QLatin1String("hidden"))
                property->setVisible(!detail::flag(metadata, "hidden"));
            else if (key == QLatin1String("disabled"))
                property->setEnabled(!detail::flag(metadata, "disabled"));
            else if (key == QLatin1String("live"))
                property->setLive(detail::flag(metadata, "live"));
            // 1.4: another Q_PROPERTY of the same object.
            else if (key == QLatin1String("enabledWhen"))
                property->setEnabledWhen(
                    group.path() + QLatin1Char('/') + it.value().toString().trimmed());
            else if (key == QLatin1String("visibleWhen"))
                property->setVisibleWhen(
                    group.path() + QLatin1Char('/') + it.value().toString().trimmed());
            else
                property->setAttribute(key, it.value());
        }
        if (!metaProperty.isWritable()) {
            property->setReadOnly(true);
            // 1.3: values the object changes by itself are not settings.
            if (d->liveReadOnly && metaProperty.hasNotifySignal())
                property->setLive(true);
        }
        // Attributes may change the value (e.g. clamping): start from the object's.
        property->setValue(value);
        property->setDefaultValue(property->value());

        binding->properties.insert(property->id(), metaProperty);
        if (metaProperty.hasNotifySignal()) {
            const int signal = metaProperty.notifySignalIndex();
            if (!binding->notified.contains(signal)) {
                binding->connections << connect(object, metaProperty.notifySignal(), d.get(), slot);
            }
            binding->notified[signal] << property->id();
        }
    }

    // Group title (1.3): the class's "qpb:title", else setTitleProperty().
    QString titleName = d->titleProperty;
    const int titleInfo = meta->indexOfClassInfo("qpb:title");
    if (titleInfo >= 0)
        titleName = QString::fromUtf8(meta->classInfo(titleInfo).value()).trimmed();
    const int titleIndex
        = titleName.isEmpty() ? -1 : meta->indexOfProperty(titleName.toLatin1().constData());
    if (titleIndex >= 0 && meta->property(titleIndex).isReadable()) {
        binding->title = meta->property(titleIndex);
        const int signal = binding->title.notifySignalIndex();
        if (signal >= 0 && !binding->notified.contains(signal)) {
            binding->connections << connect(object, binding->title.notifySignal(), d.get(), slot);
            binding->notified.insert(signal, {});
        }
        d->readTitle(*binding);
    }

    binding->connections << connect(object, &QObject::destroyed, d.get(), [this, object] {
        if (detail::ObjectBinding* gone = d->bindingOf(object))
            d->remove(gone, true);
    });
    d->bindings.push_back(std::move(binding));
    return &group;
}

bool QObjectPropertySource::removeObject(QObject* object)
{
    detail::ObjectBinding* binding = d->bindingOf(object);
    if (!binding)
        return false;
    d->remove(binding, true);
    return true;
}

QList<QObject*> QObjectPropertySource::objects() const
{
    QList<QObject*> result;
    for (const auto& binding : d->bindings) {
        if (binding->object)
            result << binding->object;
    }
    return result;
}

PropertyGroup* QObjectPropertySource::groupOf(const QObject* object) const
{
    const detail::ObjectBinding* binding = d->bindingOf(object);
    return binding ? d->groupOf(*binding) : nullptr;
}

void QObjectPropertySource::refresh()
{
    for (const auto& binding : d->bindings) {
        for (auto it = binding->properties.constBegin(); it != binding->properties.constEnd(); ++it)
            d->read(*binding, it.key());
    }
}

void QObjectPropertySource::setTitleProperty(const QString& name)
{
    d->titleProperty = name;
}

QString QObjectPropertySource::titleProperty() const
{
    return d->titleProperty;
}

void QObjectPropertySource::setLiveReadOnlyProperties(bool live)
{
    d->liveReadOnly = live;
}

bool QObjectPropertySource::liveReadOnlyProperties() const
{
    return d->liveReadOnly;
}

} // namespace qpb
