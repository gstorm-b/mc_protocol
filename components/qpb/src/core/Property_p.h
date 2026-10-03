#ifndef QPB_PROPERTY_P_H
#define QPB_PROPERTY_P_H

// Internal header, not part of the public API.

#include <qpb/Property.h>
#include <qpb/PropertyGroup.h>

#include <memory>
#include <vector>

namespace qpb::detail {

// Receives change notifications from a property tree. Implemented by
// PropertyModel; only the root of a tree holds the pointer, every node finds
// it by walking up (see PropertyPrivate::observer()).
class TreeObserver
{
public:
    virtual ~TreeObserver() = default;

    virtual void aboutToInsert(PropertyGroup* parent, int row) = 0;
    virtual void inserted(PropertyGroup* parent, int row) = 0;
    virtual void aboutToRemove(PropertyGroup* parent, int row) = 0;
    virtual void removed(PropertyGroup* parent, int row) = 0;
    // Presentation, attributes or flags of property changed. With recursive,
    // the effective state of all descendants may have changed too.
    virtual void changed(Property* property, bool recursive) = 0;
    virtual void valueChanged(
        Property* property, const QVariant& newValue, const QVariant& oldValue)
        = 0;
    virtual void validationFailed(
        Property* property, const QVariant& rejectedValue, const QString& message)
        = 0;
    virtual void beginBatch() = 0;
    virtual void endBatch() = 0;
    // A condition (enabledWhen / visibleWhen) was set or cleared on property.
    virtual void conditionsChanged(Property* property) = 0;
};

// A condition on another property's value (1.4), evaluated by the model.
struct PropertyCondition
{
    QString sourcePath; // from the root, like PropertyGroup::find()
    Property::Condition test;
    bool met = true; // last evaluation; true while not evaluated
    bool warned = false; // missing source reported
};

class PropertyPrivate
{
public:
    PropertyPrivate(const TypeId& typeId, const QString& id);

    static PropertyPrivate* get(Property* property)
    {
        return property->d.get();
    }
    static const PropertyPrivate* get(const Property* property)
    {
        return property->d.get();
    }

    // Observer of the tree this property belongs to, or nullptr.
    TreeObserver* observer() const;

    // Converts value to the type's storage type if possible (no validation).
    // Used for initial values.
    QVariant convertInitial(const QVariant& value) const;

    // Who is setting a value: the user through a view (PropertyModel::setData),
    // or application code (Property::setValue, PropertyModel::setValue, resets).
    enum class Origin {
        User,
        Application,
    };

    // The value pipeline (README.md, "Values and the value pipeline"). Returns false if the value was
    // rejected; emits notifications through the observer. Read-only and
    // disabled properties only reject values that come from the user.
    bool assign(const QVariant& value, Origin origin);

    void notifyChanged(bool recursive = false);

    // Sets (test non-empty) or clears a condition and lets the model evaluate it.
    void setCondition(std::unique_ptr<PropertyCondition>& slot, const QString& sourcePath,
        Property::Condition test);

    Property* q = nullptr;
    QString id;
    TypeId typeId;
    QString displayName;
    QString toolTip;
    QVariant value;
    QVariant defaultValue;
    QVariantMap attributes;
    Property::Flags flags;
    Property::Validator validator;
    PropertyGroup* parent = nullptr;
    std::unique_ptr<PropertyCondition> enabledWhen; // 1.4
    std::unique_ptr<PropertyCondition> visibleWhen; // 1.4

    // Groups only.
    std::vector<std::unique_ptr<Property>> children;

    // Root only: set by PropertyModel while it owns the tree.
    TreeObserver* treeObserver = nullptr;
};

} // namespace qpb::detail

#endif // QPB_PROPERTY_P_H
