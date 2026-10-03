#include "widgets_p.h"

#include <qpb/Property.h>
#include <qpb/PropertyGroup.h>
#include <qpb/PropertyModel.h>

#include <QtCore/qabstractproxymodel.h>
#include <QtWidgets/qplaintextedit.h>
#include <QtWidgets/qtextedit.h>
#include <QtWidgets/qwidget.h>

#include <algorithm>

namespace qpb::detail {

CommitNotifier* CommitNotifier::instance()
{
    static CommitNotifier notifier;
    return &notifier;
}

bool isShowingDialog(const QWidget* editor)
{
    for (const QWidget* widget = editor; widget; widget = widget->parentWidget()) {
        if (widget->property(DialogDepthProperty).toInt() > 0)
            return true;
    }
    return false;
}

bool isMultilineText(const QObject* object)
{
    // Key events reach the viewport of a text edit, not the text edit itself.
    for (const QObject* o = object; o; o = o->parent()) {
        if (qobject_cast<const QPlainTextEdit*>(o) || qobject_cast<const QTextEdit*>(o))
            return true;
        if (o->isWidgetType() && static_cast<const QWidget*>(o)->isWindow())
            break;
    }
    return false;
}

const Property* propertyOf(const QModelIndex& index)
{
    return index.data(PropertyModel::PropertyRole).value<const Property*>();
}

bool isResettableByUser(const Property* property)
{
    if (const PropertyGroup* group = property->toGroup()) {
        const QList<Property*> children = group->children();
        return std::any_of(children.begin(), children.end(), isResettableByUser);
    }
    return property->isModified() && !property->isReadOnly() && property->isEnabled();
}

namespace {

void resetLeaves(PropertyModel* model, const Property* property)
{
    if (const PropertyGroup* group = property->toGroup()) {
        for (const Property* child : group->children())
            resetLeaves(model, child);
    } else if (property->isModified()) {
        model->setData(model->indexOf(property, PropertyModel::ValueColumn),
            property->defaultValue(), Qt::EditRole);
    }
}

} // namespace

void resetByUser(PropertyModel* model, const Property* property)
{
    model->beginBatch();
    resetLeaves(model, property);
    model->endBatch();
}

PropertyModel* propertyModelOf(const QModelIndex& index, QModelIndex* sourceIndex)
{
    QModelIndex current = index;
    const QAbstractItemModel* model = index.model();
    while (const auto* proxy = qobject_cast<const QAbstractProxyModel*>(model)) {
        current = proxy->mapToSource(current);
        model = proxy->sourceModel();
    }
    auto* propertyModel = qobject_cast<PropertyModel*>(const_cast<QAbstractItemModel*>(model));
    if (propertyModel && sourceIndex)
        *sourceIndex = current;
    return propertyModel;
}

} // namespace qpb::detail
