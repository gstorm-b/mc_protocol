#ifndef QPB_PROPERTYGROUP_H
#define QPB_PROPERTYGROUP_H

#include <qpb/Attributes.h>
#include <qpb/Property.h>
#include <qpb/PropertyBuilders.h>
#include <qpb/qpbglobal.h>

#include <QtCore/qlist.h>
#include <QtCore/qstring.h>
#include <QtCore/qstringlist.h>
#include <QtCore/qvariant.h>

#include <memory>

namespace qpb {

// A property that holds an ordered list of child properties (README.md, "Properties").
//
// Groups nest without limit. A group has no value; its state flags apply to
// all descendants. When a group belongs to a PropertyModel, adding and removing
// children updates the model and its views immediately.
class QPB_CORE_EXPORT PropertyGroup : public Property
{
public:
    // Creates a detached root group. Hand it to a PropertyModel, or attach it
    // to another group with add(std::unique_ptr<Property>).
    static std::unique_ptr<PropertyGroup> create(const QString& id);

    ~PropertyGroup() override;

    // --- adding children -------------------------------------------------------
    //
    // Every add function appends a child and returns a handle to it. If a child
    // with the same id already exists, nothing is added, a warning is logged and
    // the existing child is returned (addGroup(): if that child is not a group,
    // the group is added under the first free id "<id>_2", "<id>_3", ...).
    // IDs must be non-empty and must not contain '/'; offending characters are
    // replaced by '_' with a warning. Initial values are stored as the default
    // value too.

    PropertyGroup& addGroup(const QString& id);

    BoolBuilder addBool(const QString& id, bool value);
    IntBuilder addInt(const QString& id, int value);
    // Since 1.2.
    Int64Builder addInt64(const QString& id, qint64 value);
    DoubleBuilder addDouble(const QString& id, double value);
    StringBuilder addString(const QString& id, const QString& value);

    // Options labelled with labels; the value is the index of the selected
    // option (int).
    EnumBuilder addEnum(const QString& id, const QStringList& labels, int currentIndex);
    // Options with explicit values (int or QString); value selects one of them.
    EnumBuilder addEnum(const QString& id, const QList<EnumOption>& options, const QVariant& value);

    FilePathBuilder addFilePath(const QString& id, const QString& path);
    DirPathBuilder addDirPath(const QString& id, const QString& path);

    // Adds a property of any type, typically one registered by the application.
    Property& add(const TypeId& type, const QString& id, const QVariant& value);
    // Takes ownership of a property created with Property::create() or
    // PropertyGroup::create().
    Property& add(std::unique_ptr<Property> property);

    // --- removing children -------------------------------------------------------

    // Removes and destroys the direct child id. Returns false if there is none.
    bool remove(const QString& id);

    // --- access ----------------------------------------------------------------

    int childCount() const;
    // Child at index, or nullptr if index is out of range.
    Property* child(int index) const;
    // Direct child with the given id, or nullptr.
    Property* child(const QString& id) const;
    QList<Property*> children() const;
    // Index of a direct child, or -1.
    int indexOf(const Property* child) const;
    // Descendant at a path relative to this group, e.g. "Transform/x", or nullptr.
    Property* find(const QString& path) const;

protected:
    explicit PropertyGroup(std::unique_ptr<detail::PropertyPrivate> d);
};

} // namespace qpb

#endif // QPB_PROPERTYGROUP_H
