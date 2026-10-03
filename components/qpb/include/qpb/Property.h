#ifndef QPB_PROPERTY_H
#define QPB_PROPERTY_H

#include <qpb/Types.h>
#include <qpb/ValidationResult.h>
#include <qpb/qpbglobal.h>

#include <QtCore/qflags.h>
#include <QtCore/qmetatype.h>
#include <QtCore/qstring.h>
#include <QtCore/qvariant.h>
#include <QtCore/qvariantmap.h>

#include <functional>
#include <memory>

namespace qpb {

class PropertyGroup;

namespace detail {
class PropertyPrivate;
}

// A node of the property tree: one editable value, or a PropertyGroup
// (README.md, "Properties").
//
// Properties are created through PropertyGroup::add*() or Property::create()
// and owned by their parent group; the root group is owned by the
// PropertyModel it is attached to. Property is not a QObject and is not meant
// to be subclassed by applications.
//
// Every value change - from the UI, from PropertyModel::setValue() or from
// setValue() below - goes through the same pipeline: conversion to the type's
// storage type, normalization, type validation, the property's own validator,
// and change notification through the attached PropertyModel.
class QPB_CORE_EXPORT Property
{
public:
    // Per-property state. Effective state also depends on the ancestors, see
    // isReadOnly(), isEnabled() and isVisible().
    enum class Flag {
        ReadOnly = 0x1, // value cannot be edited
        Disabled = 0x2, // shown greyed out, cannot be edited
        Hidden = 0x4, // not shown by views (still part of the model)
        // Since 1.3. The value is maintained by the application (a status, a
        // counter), not a setting: never "modified", skipped by group resets
        // and by qpb::serialization. See isLive().
        Live = 0x8,
    };
    Q_DECLARE_FLAGS(Flags, Flag)

    // Test on the value of another property, see setEnabledWhen(). Since 1.4.
    using Condition = std::function<bool(const QVariant& sourceValue)>;

    // Extra validation run after the type's own validation. Returning an error
    // rejects the value; the old value is kept.
    using Validator
        = std::function<ValidationResult(const QVariant& value, const Property& property)>;

    // Creates a detached property of any registered (or not yet registered)
    // type. Attach it with PropertyGroup::add(std::unique_ptr<Property>).
    // While its type is not registered the value is stored as given and
    // setValue() fails; once the type is registered the property behaves
    // like any other.
    // For the built-in types prefer the typed PropertyGroup::add*() functions.
    static std::unique_ptr<Property> create(
        const TypeId& type, const QString& id, const QVariant& value = QVariant());

    virtual ~Property();

    Property(const Property&) = delete;
    Property& operator=(const Property&) = delete;

    // --- identity ----------------------------------------------------------

    // Identifier, unique among the siblings. Never contains '/'.
    QString id() const;
    // IDs from the root's child down to this property joined with '/', e.g.
    // "Transform/x". Empty for the root group.
    QString path() const;
    TypeId typeId() const;
    bool isGroup() const;

    // Parent group, or nullptr for a root or detached property.
    PropertyGroup* parent() const;
    // This property as a group, or nullptr if it is not a group.
    PropertyGroup* toGroup();
    const PropertyGroup* toGroup() const;

    // --- presentation --------------------------------------------------------

    // Text shown in views. Defaults to id().
    QString displayName() const;
    void setDisplayName(const QString& name);

    QString toolTip() const;
    void setToolTip(const QString& toolTip);

    // --- value -------------------------------------------------------------

    // Current value. Always invalid for groups.
    QVariant value() const;
    // Sets the value from application code through the value pipeline (see
    // class comment). Read-only and disabled only restrict the user (edits
    // through a view), so application code can still update such properties.
    // Returns false and keeps the old value if the property is a group, its
    // type is not registered, or conversion/validation fails.
    bool setValue(const QVariant& value);

    // Value restored by resetToDefault(). Defaults to the value at creation.
    QVariant defaultValue() const;
    void setDefaultValue(const QVariant& value);
    // True when value() differs from defaultValue(). Always false for groups
    // and, since 1.3, for live properties (isLive()).
    bool isModified() const;
    // Sets the value back to defaultValue() through the value pipeline, like
    // setValue(). For a group, resets all descendants except live ones (since
    // 1.3). Returns false if any reset was rejected.
    bool resetToDefault();

    // Optional extra validation, run after the type's validation.
    Validator validator() const;
    void setValidator(Validator validator);

    // --- attributes (see Attr) -----------------------------------------------

    QVariantMap attributes() const;
    QVariant attribute(const QString& key, const QVariant& defaultValue = QVariant()) const;
    bool hasAttribute(const QString& key) const;
    void setAttribute(const QString& key, const QVariant& value);
    void removeAttribute(const QString& key);

    // --- state ---------------------------------------------------------------

    // Flags set on this property itself (ancestors not included).
    Flags flags() const;
    void setFlags(Flags flags);
    void setFlag(Flag flag, bool on = true);

    // Convenience setters for the corresponding own flag.
    void setReadOnly(bool readOnly);
    void setEnabled(bool enabled);
    void setVisible(bool visible);
    // Since 1.3.
    void setLive(bool live);

    // --- conditions (since 1.4) ------------------------------------------------
    //
    // Enable or show this property depending on the value of another one,
    // given by its path from the root ("General/autosave"). The PropertyModel
    // holding the tree evaluates the condition whenever values or the tree
    // change. The result is combined with the property's own flags: enabled =
    // not Disabled and condition met and ancestors enabled. Outside a model, or
    // while the source path does not exist, a condition counts as met.

    // Met when the source value is "true": a true bool, a non-zero number, a
    // non-empty string; other types when valid and not null.
    void setEnabledWhen(const QString& sourcePath);
    // Met when the source value equals value. (The int overload keeps a
    // literal 0, e.g. an enum index, from also matching Condition.)
    void setEnabledWhen(const QString& sourcePath, const QVariant& value);
    void setEnabledWhen(const QString& sourcePath, int value);
    // Met when condition returns true for the source value.
    void setEnabledWhen(const QString& sourcePath, Condition condition);
    void clearEnabledWhen();
    // The same for visibility.
    void setVisibleWhen(const QString& sourcePath);
    void setVisibleWhen(const QString& sourcePath, const QVariant& value);
    void setVisibleWhen(const QString& sourcePath, int value);
    void setVisibleWhen(const QString& sourcePath, Condition condition);
    void clearVisibleWhen();

    // Effective state: read-only if this property or any ancestor is read-only.
    bool isReadOnly() const;
    // Effective state: enabled only if this property and all ancestors are
    // enabled (own flag and, since 1.4, enabledWhen condition).
    bool isEnabled() const;
    // Effective state: visible only if this property and all ancestors are
    // visible (own flag and, since 1.4, visibleWhen condition).
    bool isVisible() const;
    // Effective state: live if this property or any ancestor is live. Since 1.3.
    bool isLive() const;

protected:
    explicit Property(std::unique_ptr<detail::PropertyPrivate> d);

    detail::PropertyPrivate* d_func()
    {
        return d.get();
    }
    const detail::PropertyPrivate* d_func() const
    {
        return d.get();
    }

private:
    std::unique_ptr<detail::PropertyPrivate> d;

    friend class detail::PropertyPrivate;
};

} // namespace qpb

Q_DECLARE_OPERATORS_FOR_FLAGS(qpb::Property::Flags)
Q_DECLARE_METATYPE(const qpb::Property*)

#endif // QPB_PROPERTY_H
