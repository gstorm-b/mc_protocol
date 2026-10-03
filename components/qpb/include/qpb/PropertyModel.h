#ifndef QPB_PROPERTYMODEL_H
#define QPB_PROPERTYMODEL_H

#include <qpb/PropertyGroup.h>
#include <qpb/qpbglobal.h>

#include <QtCore/qabstractitemmodel.h>
#include <QtCore/qstring.h>
#include <QtCore/qstringlist.h>
#include <QtCore/qvariant.h>

#include <functional>
#include <memory>

namespace qpb {

class Property;

namespace detail {
class PropertyModelPrivate;
}

// Item model exposing a property tree to Qt views (README.md, "Model").
//
// Two columns: the property name and its value. Every row is one property;
// groups have children. The model owns the root group. Any QAbstractItemView
// (or proxy model) can be used with it; qpb's own views add editors and
// property-specific painting.
class QPB_CORE_EXPORT PropertyModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Column {
        NameColumn = 0,
        ValueColumn = 1,
    };

    // Custom data roles, available on both columns.
    // Qt::UserRole + 1 ... Qt::UserRole + 99 are reserved for qpb; new roles are
    // only ever appended. Application roles start at PropertyModel::UserRole.
    enum Role {
        PropertyRole = Qt::UserRole + 1, // const qpb::Property*
        TypeIdRole, // QString, Property::typeId()
        PathRole, // QString, Property::path()
        IsGroupRole, // bool
        IsModifiedRole, // bool, Property::isModified()
        AttributesRole, // QVariantMap, Property::attributes()
        IsVisibleRole, // bool, effective Property::isVisible()
        UserRole = Qt::UserRole + 100,
    };
    Q_ENUM(Role)

    explicit PropertyModel(QObject* parent = nullptr);
    explicit PropertyModel(std::unique_ptr<PropertyGroup> root, QObject* parent = nullptr);
    ~PropertyModel() override;

    // --- tree ------------------------------------------------------------------

    // Replaces the whole tree (model reset). Pointers into the old tree become
    // invalid. A model without a root is empty.
    void setRoot(std::unique_ptr<PropertyGroup> root);
    PropertyGroup* root() const;

    // Property of an index (either column), or nullptr for an invalid index.
    Property* propertyAt(const QModelIndex& index) const;
    // Index of a property of this model's tree, or an invalid index.
    QModelIndex indexOf(const Property* property, int column = NameColumn) const;
    // Property at a path relative to the root (e.g. "Transform/x"), or nullptr.
    Property* find(const QString& path) const;

    // --- values ----------------------------------------------------------------

    // Sets the value of the property at path from application code, like
    // Property::setValue() (read-only and disabled properties included).
    // Returns false if there is no such property or the value is rejected.
    // setData() is the entry point for views: it rejects read-only and
    // disabled properties.
    bool setValue(const QString& path, const QVariant& value);
    // Resets the property at index (a group: all its descendants) to the
    // default value. Returns false if any reset was rejected.
    bool resetToDefault(const QModelIndex& index);
    // Since 1.5. Resets the whole tree like root()->resetToDefault(): read-only
    // and disabled properties too, live ones not; one batchValueChanged().
    // Returns false if any reset was rejected, true for a model without a root.
    bool resetAllToDefault();

    // Groups value changes: valueChanged() is still emitted for every change,
    // and batchValueChanged() is emitted once, by the outermost endBatch(),
    // with the paths of all properties changed in between. Batches nest.
    void beginBatch();
    void endBatch();

    // Since 1.4. Calls handler with the new value whenever the value of the
    // property at path changes (after valueChanged()), from the user or the
    // application. Like QObject::connect: the connection ends when context is
    // destroyed (nullptr: when the model is) and can be disconnected with the
    // returned handle. It follows the path, not a Property, so it survives
    // setRoot() and properties being removed and added again.
    QMetaObject::Connection onValueChanged(const QString& path, const QObject* context,
        std::function<void(const QVariant& value)> handler);
    // The same for the property at path and all its descendants (path of a
    // group; empty for the whole tree); handler also receives the path.
    QMetaObject::Connection onValueChanged(const QString& path, const QObject* context,
        std::function<void(const QString& path, const QVariant& value)> handler);

    // --- QAbstractItemModel ------------------------------------------------------

    QModelIndex index(
        int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    bool hasChildren(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QVariant headerData(
        int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    // A property value changed (from any source). path is Property::path().
    void valueChanged(const QString& path, const QVariant& newValue, const QVariant& oldValue);
    // A value was rejected by conversion or validation; the old value is kept.
    void validationFailed(
        const QString& path, const QVariant& rejectedValue, const QString& message);
    // Emitted by the outermost endBatch() if any value changed during the batch.
    void batchValueChanged(const QStringList& paths);

private:
    std::unique_ptr<detail::PropertyModelPrivate> d;

    friend class detail::PropertyModelPrivate;
};

} // namespace qpb

#endif // QPB_PROPERTYMODEL_H
