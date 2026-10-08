#include <qpb/PropertyGroup.h>
#include <qpb/TypeRegistry.h>

#include "Property_p.h"
#include "core/compat_p.h"

namespace qpb {

namespace detail {

namespace {

// IDs may not be empty or contain the path separator; replace what cannot be
// used so the tree stays addressable by path.
QString sanitizedId(const QString& id)
{
    if (id.isEmpty()) {
        qWarning("qpb: empty property id replaced by \"property\"");
        return QStringLiteral("property");
    }
    if (id.contains(QLatin1Char('/'))) {
        QString fixed = id;
        fixed.replace(QLatin1Char('/'), QLatin1Char('_'));
        qWarning("qpb: property id \"%s\" contains '/', using \"%s\"", qUtf8Printable(id),
            qUtf8Printable(fixed));
        return fixed;
    }
    return id;
}

} // namespace

PropertyPrivate::PropertyPrivate(const TypeId& typeId, const QString& id)
    : id(sanitizedId(id))
    , typeId(typeId)
{ }

TreeObserver* PropertyPrivate::observer() const
{
    const Property* node = q;
    while (const PropertyGroup* up = get(node)->parent)
        node = up;
    return get(node)->treeObserver;
}

QVariant PropertyPrivate::convertInitial(const QVariant& input) const
{
    const TypeHandler* handler = TypeRegistry::global().handler(typeId);
    if (!handler || !detail::isValidStorageType(handler->storageType) || !input.isValid()
        || detail::storageTypeOf(input) == handler->storageType) {
        return input;
    }
    QVariant converted = input;
    return detail::convertTo(converted, handler->storageType) ? converted : input;
}

bool PropertyPrivate::assign(const QVariant& input, Origin origin)
{
    if (q->isGroup())
        return false;
    if (origin == Origin::User && (q->isReadOnly() || !q->isEnabled()))
        return false;

    const TypeHandler* handler = TypeRegistry::global().handler(typeId);
    if (!handler)
        return false;

    TreeObserver* tree = observer();
    const auto reject = [&](const QString& message) {
        if (tree)
            tree->validationFailed(q, input, message);
        return false;
    };

    QVariant candidate = input;
    if (detail::isValidStorageType(handler->storageType)
        && detail::storageTypeOf(candidate) != handler->storageType) {
        if (!detail::convertTo(candidate, handler->storageType)) {
            return reject(QStringLiteral("Cannot convert the value to %1")
                              .arg(QLatin1String(detail::typeNameOf(handler->storageType))));
        }
    }
    if (handler->normalize)
        candidate = handler->normalize(candidate, *q);
    if (handler->validate) {
        const ValidationResult result = handler->validate(candidate, *q);
        if (!result)
            return reject(result.message);
    }
    if (validator) {
        const ValidationResult result = validator(candidate, *q);
        if (!result)
            return reject(result.message);
    }

    if (candidate == value)
        return true;

    const QVariant oldValue = value;
    value = candidate;
    if (tree)
        tree->valueChanged(q, value, oldValue);
    return true;
}

void PropertyPrivate::notifyChanged(bool recursive)
{
    if (TreeObserver* tree = observer())
        tree->changed(q, recursive);
}

} // namespace detail

using detail::PropertyPrivate;

std::unique_ptr<Property> Property::create(
    const TypeId& type, const QString& id, const QVariant& value)
{
    std::unique_ptr<Property> property(new Property(std::make_unique<PropertyPrivate>(type, id)));
    PropertyPrivate* d = PropertyPrivate::get(property.get());
    d->value = d->convertInitial(value);
    d->defaultValue = d->value;
    return property;
}

Property::Property(std::unique_ptr<detail::PropertyPrivate> dd)
    : d(std::move(dd))
{
    d->q = this;
}

Property::~Property() = default;

QString Property::id() const
{
    return d->id;
}

QString Property::path() const
{
    QStringList ids;
    for (const Property* node = this; PropertyPrivate::get(node)->parent;
         node = PropertyPrivate::get(node)->parent) {
        ids.prepend(PropertyPrivate::get(node)->id);
    }
    return ids.join(QLatin1Char('/'));
}

TypeId Property::typeId() const
{
    return d->typeId;
}

bool Property::isGroup() const
{
    return d->typeId == Types::Group;
}

PropertyGroup* Property::parent() const
{
    return d->parent;
}

PropertyGroup* Property::toGroup()
{
    return isGroup() ? static_cast<PropertyGroup*>(this) : nullptr;
}

const PropertyGroup* Property::toGroup() const
{
    return isGroup() ? static_cast<const PropertyGroup*>(this) : nullptr;
}

QString Property::displayName() const
{
    return d->displayName.isEmpty() ? d->id : d->displayName;
}

void Property::setDisplayName(const QString& name)
{
    if (d->displayName == name)
        return;
    d->displayName = name;
    d->notifyChanged();
}

QString Property::toolTip() const
{
    return d->toolTip;
}

void Property::setToolTip(const QString& toolTip)
{
    if (d->toolTip == toolTip)
        return;
    d->toolTip = toolTip;
    d->notifyChanged();
}

QVariant Property::value() const
{
    return d->value;
}

bool Property::setValue(const QVariant& value)
{
    return d->assign(value, PropertyPrivate::Origin::Application);
}

QVariant Property::defaultValue() const
{
    return d->defaultValue;
}

void Property::setDefaultValue(const QVariant& value)
{
    if (isGroup())
        return;
    const QVariant converted = d->convertInitial(value);
    if (d->defaultValue == converted)
        return;
    d->defaultValue = converted;
    d->notifyChanged();
}

bool Property::isModified() const
{
    return !isGroup() && !isLive() && d->value != d->defaultValue;
}

bool Property::resetToDefault()
{
    if (const PropertyGroup* group = toGroup()) {
        detail::TreeObserver* observer = d->observer();
        if (observer)
            observer->beginBatch();
        bool ok = true;
        for (Property* child : group->children()) {
            if (!child->isLive())
                ok = child->resetToDefault() && ok;
        }
        if (observer)
            observer->endBatch();
        return ok;
    }
    return d->assign(d->defaultValue, PropertyPrivate::Origin::Application);
}

Property::Validator Property::validator() const
{
    return d->validator;
}

void Property::setValidator(Validator validator)
{
    d->validator = std::move(validator);
}

QVariantMap Property::attributes() const
{
    return d->attributes;
}

QVariant Property::attribute(const QString& key, const QVariant& defaultValue) const
{
    return d->attributes.value(key, defaultValue);
}

bool Property::hasAttribute(const QString& key) const
{
    return d->attributes.contains(key);
}

void Property::setAttribute(const QString& key, const QVariant& value)
{
    const auto it = d->attributes.constFind(key);
    if (it != d->attributes.constEnd() && *it == value)
        return;
    d->attributes.insert(key, value);
    d->notifyChanged();
}

void Property::removeAttribute(const QString& key)
{
    if (d->attributes.remove(key) > 0)
        d->notifyChanged();
}

Property::Flags Property::flags() const
{
    return d->flags;
}

void Property::setFlags(Flags flags)
{
    if (d->flags == flags)
        return;
    d->flags = flags;
    d->notifyChanged(true);
}

void Property::setFlag(Flag flag, bool on)
{
    setFlags(on ? d->flags | flag : d->flags & ~Flags(flag));
}

void Property::setReadOnly(bool readOnly)
{
    setFlag(Flag::ReadOnly, readOnly);
}

void Property::setEnabled(bool enabled)
{
    setFlag(Flag::Disabled, !enabled);
}

void Property::setVisible(bool visible)
{
    setFlag(Flag::Hidden, !visible);
}

void Property::setLive(bool live)
{
    setFlag(Flag::Live, live);
}

bool Property::isLive() const
{
    for (const Property* node = this; node; node = PropertyPrivate::get(node)->parent) {
        if (PropertyPrivate::get(node)->flags.testFlag(Flag::Live))
            return true;
    }
    return false;
}

bool Property::isReadOnly() const
{
    for (const Property* node = this; node; node = PropertyPrivate::get(node)->parent) {
        if (PropertyPrivate::get(node)->flags.testFlag(Flag::ReadOnly))
            return true;
    }
    return false;
}

bool Property::isEnabled() const
{
    for (const Property* node = this; node; node = PropertyPrivate::get(node)->parent) {
        const PropertyPrivate* data = PropertyPrivate::get(node);
        if (data->flags.testFlag(Flag::Disabled) || (data->enabledWhen && !data->enabledWhen->met))
            return false;
    }
    return true;
}

bool Property::isVisible() const
{
    for (const Property* node = this; node; node = PropertyPrivate::get(node)->parent) {
        const PropertyPrivate* data = PropertyPrivate::get(node);
        if (data->flags.testFlag(Flag::Hidden) || (data->visibleWhen && !data->visibleWhen->met))
            return false;
    }
    return true;
}

namespace {

bool isTruthy(const QVariant& value)
{
    switch (detail::typeIdOf(detail::storageTypeOf(value))) {
    case QMetaType::Bool:
        return value.toBool();
    case QMetaType::QString:
        return !value.toString().isEmpty();
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::Float:
        return value.toDouble() != 0.0;
    default:
        return value.isValid() && !value.isNull();
    }
}

Property::Condition equalTo(const QVariant& expected)
{
    return [expected](const QVariant& value) { return value == expected; };
}

} // namespace

void detail::PropertyPrivate::setCondition(std::unique_ptr<detail::PropertyCondition>& slot,
    const QString& sourcePath, Property::Condition test)
{
    const bool wasUnmet = slot && !slot->met;
    if (test) {
        slot = std::make_unique<detail::PropertyCondition>();
        slot->sourcePath = sourcePath;
        slot->test = std::move(test);
    } else {
        slot.reset();
    }
    // A condition that was not met no longer applies: the effective state
    // may change even if the new one (if any) evaluates the same.
    if (wasUnmet)
        notifyChanged(true);
    if (detail::TreeObserver* tree = observer())
        tree->conditionsChanged(q); // evaluates, notifies changes
}

void Property::setEnabledWhen(const QString& sourcePath)
{
    d->setCondition(d->enabledWhen, sourcePath, isTruthy);
}

void Property::setEnabledWhen(const QString& sourcePath, const QVariant& value)
{
    d->setCondition(d->enabledWhen, sourcePath, equalTo(value));
}

void Property::setEnabledWhen(const QString& sourcePath, int value)
{
    setEnabledWhen(sourcePath, QVariant(value));
}

void Property::setEnabledWhen(const QString& sourcePath, Condition condition)
{
    d->setCondition(d->enabledWhen, sourcePath, std::move(condition));
}

void Property::clearEnabledWhen()
{
    d->setCondition(d->enabledWhen, QString(), Condition());
}

void Property::setVisibleWhen(const QString& sourcePath)
{
    d->setCondition(d->visibleWhen, sourcePath, isTruthy);
}

void Property::setVisibleWhen(const QString& sourcePath, const QVariant& value)
{
    d->setCondition(d->visibleWhen, sourcePath, equalTo(value));
}

void Property::setVisibleWhen(const QString& sourcePath, int value)
{
    setVisibleWhen(sourcePath, QVariant(value));
}

void Property::setVisibleWhen(const QString& sourcePath, Condition condition)
{
    d->setCondition(d->visibleWhen, sourcePath, std::move(condition));
}

void Property::clearVisibleWhen()
{
    d->setCondition(d->visibleWhen, QString(), Condition());
}

} // namespace qpb
