#include <qpb/PropertyGroup.h>

#include "Property_p.h"

namespace qpb {

using detail::PropertyPrivate;

std::unique_ptr<PropertyGroup> PropertyGroup::create(const QString& id)
{
    return std::unique_ptr<PropertyGroup>(
        new PropertyGroup(std::make_unique<PropertyPrivate>(TypeId(Types::Group), id)));
}

PropertyGroup::PropertyGroup(std::unique_ptr<detail::PropertyPrivate> d)
    : Property(std::move(d))
{ }

PropertyGroup::~PropertyGroup() = default;

Property& PropertyGroup::add(std::unique_ptr<Property> property)
{
    Q_ASSERT_X(property, "PropertyGroup::add", "property must not be null");
    PropertyPrivate* childData = PropertyPrivate::get(property.get());

    if (Property* existing = child(childData->id)) {
        qWarning("qpb: group \"%s\" already has a child \"%s\"; the new property is discarded",
            qUtf8Printable(path().isEmpty() ? id() : path()), qUtf8Printable(childData->id));
        return *existing;
    }

    auto& children = d_func()->children;
    const int row = int(children.size());
    detail::TreeObserver* observer = d_func()->observer();
    if (observer)
        observer->aboutToInsert(this, row);
    childData->parent = this;
    childData->treeObserver = nullptr; // only the root of a tree holds the observer
    children.push_back(std::move(property));
    if (observer)
        observer->inserted(this, row);
    return *children.back();
}

Property& PropertyGroup::add(const TypeId& type, const QString& id, const QVariant& value)
{
    return add(Property::create(type, id, value));
}

PropertyGroup& PropertyGroup::addGroup(const QString& id)
{
    std::unique_ptr<PropertyGroup> group = create(id);
    const Property* existing = child(group->id());
    if (existing && !existing->isGroup()) {
        // The existing child cannot be returned as a group: add the group under
        // the first free id instead.
        QString freeId;
        for (int n = 2; child(freeId = QStringLiteral("%1_%2").arg(group->id()).arg(n)); ++n) { }
        qWarning("qpb: \"%s\" is not a group; adding the group as \"%s\"",
            qUtf8Printable(group->id()), qUtf8Printable(freeId));
        group = create(freeId);
    }
    return *add(std::move(group)).toGroup();
}

BoolBuilder PropertyGroup::addBool(const QString& id, bool value)
{
    return BoolBuilder(add(Types::Bool, id, value));
}

Int64Builder PropertyGroup::addInt64(const QString& id, qint64 value)
{
    return Int64Builder(add(Types::Int64, id, value));
}

IntBuilder PropertyGroup::addInt(const QString& id, int value)
{
    return IntBuilder(add(Types::Int, id, value));
}

DoubleBuilder PropertyGroup::addDouble(const QString& id, double value)
{
    return DoubleBuilder(add(Types::Double, id, value));
}

StringBuilder PropertyGroup::addString(const QString& id, const QString& value)
{
    return StringBuilder(add(Types::String, id, value));
}

EnumBuilder PropertyGroup::addEnum(const QString& id, const QStringList& labels, int currentIndex)
{
    QList<EnumOption> options;
    options.reserve(labels.size());
    for (int i = 0; i < labels.size(); ++i)
        options.append(EnumOption {labels.at(i), i});
    return addEnum(id, options, currentIndex);
}

EnumBuilder PropertyGroup::addEnum(
    const QString& id, const QList<EnumOption>& options, const QVariant& value)
{
    auto property = Property::create(Types::Enum, id, value);
    PropertyPrivate::get(property.get())
        ->attributes.insert(Attr::Options, QVariant::fromValue(options));
    return EnumBuilder(add(std::move(property)));
}

FilePathBuilder PropertyGroup::addFilePath(const QString& id, const QString& path)
{
    return FilePathBuilder(add(Types::FilePath, id, path));
}

DirPathBuilder PropertyGroup::addDirPath(const QString& id, const QString& path)
{
    return DirPathBuilder(add(Types::DirPath, id, path));
}

bool PropertyGroup::remove(const QString& id)
{
    auto& children = d_func()->children;
    const int row = indexOf(child(id));
    if (row < 0)
        return false;

    detail::TreeObserver* observer = d_func()->observer();
    if (observer)
        observer->aboutToRemove(this, row);
    std::unique_ptr<Property> removed = std::move(children[size_t(row)]);
    children.erase(children.begin() + row);
    PropertyPrivate::get(removed.get())->parent = nullptr;
    if (observer)
        observer->removed(this, row);
    return true; // removed is destroyed here, after the model has been updated
}

int PropertyGroup::childCount() const
{
    return int(d_func()->children.size());
}

Property* PropertyGroup::child(int index) const
{
    const auto& children = d_func()->children;
    if (index < 0 || size_t(index) >= children.size())
        return nullptr;
    return children[size_t(index)].get();
}

Property* PropertyGroup::child(const QString& id) const
{
    for (const auto& child : d_func()->children) {
        if (PropertyPrivate::get(child.get())->id == id)
            return child.get();
    }
    return nullptr;
}

QList<Property*> PropertyGroup::children() const
{
    QList<Property*> result;
    result.reserve(qsizetype(d_func()->children.size()));
    for (const auto& child : d_func()->children)
        result.append(child.get());
    return result;
}

int PropertyGroup::indexOf(const Property* child) const
{
    if (!child)
        return -1;
    const auto& children = d_func()->children;
    for (size_t i = 0; i < children.size(); ++i) {
        if (children[i].get() == child)
            return int(i);
    }
    return -1;
}

Property* PropertyGroup::find(const QString& path) const
{
    if (path.isEmpty())
        return nullptr;
    const PropertyGroup* group = this;
    Property* found = nullptr;
    const QStringList ids = path.split(QLatin1Char('/'));
    for (const QString& id : ids) {
        if (!group)
            return nullptr;
        found = group->child(id);
        if (!found)
            return nullptr;
        group = found->toGroup();
    }
    return found;
}

} // namespace qpb
