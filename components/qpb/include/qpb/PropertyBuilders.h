#ifndef QPB_PROPERTYBUILDERS_H
#define QPB_PROPERTYBUILDERS_H

#include <qpb/Attributes.h>
#include <qpb/Property.h>
#include <qpb/qpbglobal.h>

#include <QtCore/qstring.h>
#include <QtCore/qvariant.h>

#include <utility>

namespace qpb {

// Fluent configuration of a property right after it was added to a group
// (README.md, "Properties"):
//
//   group.addInt("fov", 60).range(10, 170).suffix(QStringLiteral(" deg"));
//
// A builder is a lightweight handle to a property owned by its group; keep the
// Property& (builders convert implicitly) rather than the builder itself.
// Setters that do not apply to a type are simply not available on its builder.
template <class Derived> class PropertyBuilderBase
{
public:
    Property& property() const
    {
        return *m_property;
    }
    operator Property&() const
    {
        return *m_property;
    } // NOLINT(google-explicit-constructor)

    Derived& displayName(const QString& name)
    {
        m_property->setDisplayName(name);
        return self();
    }
    Derived& toolTip(const QString& toolTip)
    {
        m_property->setToolTip(toolTip);
        return self();
    }
    Derived& readOnly(bool readOnly = true)
    {
        m_property->setReadOnly(readOnly);
        return self();
    }
    Derived& enabled(bool enabled = true)
    {
        m_property->setEnabled(enabled);
        return self();
    }
    Derived& visible(bool visible = true)
    {
        m_property->setVisible(visible);
        return self();
    }
    // Since 1.3. See Property::Flag::Live.
    Derived& live(bool live = true)
    {
        m_property->setLive(live);
        return self();
    }
    // Since 1.4. See Property::setEnabledWhen() / setVisibleWhen().
    Derived& enabledWhen(const QString& sourcePath)
    {
        m_property->setEnabledWhen(sourcePath);
        return self();
    }
    Derived& enabledWhen(const QString& sourcePath, const QVariant& value)
    {
        m_property->setEnabledWhen(sourcePath, value);
        return self();
    }
    Derived& enabledWhen(const QString& sourcePath, int value)
    {
        m_property->setEnabledWhen(sourcePath, value);
        return self();
    }
    Derived& enabledWhen(const QString& sourcePath, Property::Condition condition)
    {
        m_property->setEnabledWhen(sourcePath, std::move(condition));
        return self();
    }
    Derived& visibleWhen(const QString& sourcePath)
    {
        m_property->setVisibleWhen(sourcePath);
        return self();
    }
    Derived& visibleWhen(const QString& sourcePath, const QVariant& value)
    {
        m_property->setVisibleWhen(sourcePath, value);
        return self();
    }
    Derived& visibleWhen(const QString& sourcePath, int value)
    {
        m_property->setVisibleWhen(sourcePath, value);
        return self();
    }
    Derived& visibleWhen(const QString& sourcePath, Property::Condition condition)
    {
        m_property->setVisibleWhen(sourcePath, std::move(condition));
        return self();
    }
    Derived& validator(Property::Validator validator)
    {
        m_property->setValidator(std::move(validator));
        return self();
    }
    // Sets any attribute, including custom ones (see Attr).
    Derived& attribute(const QString& key, const QVariant& value)
    {
        m_property->setAttribute(key, value);
        return self();
    }
    // Uses the editor registered under editorId for this property only
    // (Attr::EditorId, see EditorFactory).
    Derived& editor(const TypeId& editorId)
    {
        m_property->setAttribute(Attr::EditorId, editorId);
        return self();
    }

protected:
    explicit PropertyBuilderBase(Property& property)
        : m_property(&property)
    { }

private:
    Derived& self()
    {
        return static_cast<Derived&>(*this);
    }

    Property* m_property;
};

// Builder for Types::Bool.
class QPB_CORE_EXPORT BoolBuilder : public PropertyBuilderBase<BoolBuilder>
{
public:
    explicit BoolBuilder(Property& property);
};

// Builder for Types::Int.
class QPB_CORE_EXPORT IntBuilder : public PropertyBuilderBase<IntBuilder>
{
public:
    explicit IntBuilder(Property& property);

    IntBuilder& range(int minimum, int maximum);
    IntBuilder& minimum(int minimum);
    IntBuilder& maximum(int maximum);
    IntBuilder& step(int step);
    IntBuilder& prefix(const QString& prefix);
    IntBuilder& suffix(const QString& suffix);
};

// Builder for Types::Int64. Since 1.2.
class QPB_CORE_EXPORT Int64Builder : public PropertyBuilderBase<Int64Builder>
{
public:
    explicit Int64Builder(Property& property);

    Int64Builder& range(qint64 minimum, qint64 maximum);
    Int64Builder& minimum(qint64 minimum);
    Int64Builder& maximum(qint64 maximum);
    Int64Builder& step(qint64 step);
    Int64Builder& prefix(const QString& prefix);
    Int64Builder& suffix(const QString& suffix);
};

// Builder for Types::Double.
class QPB_CORE_EXPORT DoubleBuilder : public PropertyBuilderBase<DoubleBuilder>
{
public:
    explicit DoubleBuilder(Property& property);

    DoubleBuilder& range(double minimum, double maximum);
    DoubleBuilder& minimum(double minimum);
    DoubleBuilder& maximum(double maximum);
    DoubleBuilder& step(double step);
    DoubleBuilder& decimals(int decimals);
    DoubleBuilder& prefix(const QString& prefix);
    DoubleBuilder& suffix(const QString& suffix);
};

// Builder for Types::String.
class QPB_CORE_EXPORT StringBuilder : public PropertyBuilderBase<StringBuilder>
{
public:
    explicit StringBuilder(Property& property);

    StringBuilder& maxLength(int length);
    StringBuilder& placeholder(const QString& text);
    // The whole value must match pattern (QRegularExpression syntax).
    StringBuilder& regularExpression(const QString& pattern);
    // Edit the value as several lines of text (Attr::Multiline). Since 1.1.
    StringBuilder& multiline(bool multiline = true);
};

// Builder for Types::Enum.
class QPB_CORE_EXPORT EnumBuilder : public PropertyBuilderBase<EnumBuilder>
{
public:
    explicit EnumBuilder(Property& property);
};

// Builder for Types::FilePath.
class QPB_CORE_EXPORT FilePathBuilder : public PropertyBuilderBase<FilePathBuilder>
{
public:
    explicit FilePathBuilder(Property& property);

    // Dialog name filter, e.g. "Images (*.png *.jpg)".
    FilePathBuilder& filter(const QString& filter);
    FilePathBuilder& dialogMode(FileMode mode);
    FilePathBuilder& defaultDir(const QString& directory);
    FilePathBuilder& mustExist(bool mustExist = true);
};

// Builder for Types::DirPath.
class QPB_CORE_EXPORT DirPathBuilder : public PropertyBuilderBase<DirPathBuilder>
{
public:
    explicit DirPathBuilder(Property& property);

    DirPathBuilder& defaultDir(const QString& directory);
    DirPathBuilder& mustExist(bool mustExist = true);
};

} // namespace qpb

#endif // QPB_PROPERTYBUILDERS_H
