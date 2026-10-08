#include <qpb/Property.h>
#include <qpb/PropertyModel.h>
#include <qpb/widgets/PropertyDelegate.h>
#include <qpb/widgets/PropertyTreeView.h>

#include <QtCore/qset.h>
#include <QtGui/qevent.h>
#include <QtWidgets/qheaderview.h>
#include <QtWidgets/qmenu.h>

#include "widgets_p.h"

namespace qpb {

namespace detail {

class PropertyTreeViewPrivate
{
public:
    explicit PropertyTreeViewPrivate(PropertyTreeView* view)
        : q(view)
        , delegate(new PropertyDelegate(view))
        , treeIndentation(view->indentation())
    { }

    bool isGroup(const QModelIndex& index) const
    {
        return index.data(PropertyModel::IsGroupRole).toBool();
    }

    // Applies hidden rows, spanned group rows and expansion to rows
    // first..last under parent (and, with recursive, to their descendants).
    void updateRows(const QModelIndex& parent, int first, int last, bool recursive)
    {
        QAbstractItemModel* model = q->model();
        if (!model)
            return;
        for (int row = first; row <= last; ++row) {
            const QModelIndex index = model->index(row, PropertyModel::NameColumn, parent);
            if (!index.isValid())
                continue;
            q->setRowHidden(row, parent, !index.data(PropertyModel::IsVisibleRole).toBool());
            const bool group = isGroup(index);
            q->setFirstColumnSpanned(row, parent, group);
            if (group && (mode == PropertyTreeView::Mode::List || !seen.contains(index))) {
                seen.insert(index);
                q->expand(index);
            }
            if (recursive && model->rowCount(index) > 0)
                updateRows(index, 0, model->rowCount(index) - 1, true);
        }
    }

    void updateAll()
    {
        seen.clear();
        if (QAbstractItemModel* model = q->model())
            updateRows(QModelIndex(), 0, model->rowCount() - 1, true);
        fitNameColumn();
    }

    // Sizes the name column to its contents until the width is set explicitly
    // (setNameColumnWidth() or by dragging the header). Capped so the value
    // column keeps at least 40% of the view.
    void fitNameColumn()
    {
        if (nameWidthFixed || !q->model())
            return;
        const bool wasResizing = resizing;
        resizing = true;
        q->resizeColumnToContents(PropertyModel::NameColumn);
        // Bold text (modified properties) is wider than the regular text measured.
        int width
            = q->columnWidth(PropertyModel::NameColumn) + q->fontMetrics().averageCharWidth() * 2;
        if (q->viewport()->width() > 0)
            width = qMin(width, q->viewport()->width() * 3 / 5);
        width = qMax(width, q->header()->minimumSectionSize());
        q->setColumnWidth(PropertyModel::NameColumn, width);
        resizing = wasResizing;
    }

    void applyMode()
    {
        const bool list = mode == PropertyTreeView::Mode::List;
        q->setRootIsDecorated(!list);
        q->setItemsExpandable(!list);
        q->setExpandsOnDoubleClick(!list);
        q->setIndentation(list ? 0 : treeIndentation);
        if (list)
            q->expandAll();
    }

    // A value cell Tab can stop at.
    bool isTabStop(const QModelIndex& nameIndex) const
    {
        const QModelIndex value = nameIndex.siblingAtColumn(PropertyModel::ValueColumn);
        if (!value.isValid() || q->isRowHidden(nameIndex.row(), nameIndex.parent()))
            return false;
        return value.flags().testFlag(Qt::ItemIsEditable) || isCheckBoxStop(nameIndex);
    }

    // A check box the user can change, when check boxes are Tab stops.
    bool isCheckBoxStop(const QModelIndex& nameIndex) const
    {
        const QModelIndex value = nameIndex.siblingAtColumn(PropertyModel::ValueColumn);
        const Qt::ItemFlags flags = value.flags();
        return tabStopsOnCheckBoxes && value.isValid() && flags.testFlag(Qt::ItemIsUserCheckable)
            && flags.testFlag(Qt::ItemIsEnabled)
            && !q->isRowHidden(nameIndex.row(), nameIndex.parent());
    }

    PropertyTreeView* q;
    PropertyDelegate* delegate;
    int treeIndentation;
    PropertyTreeView::Mode mode = PropertyTreeView::Mode::Tree;
    // Groups whose default expansion has been applied (Tree mode keeps the
    // user's later choices).
    QSet<QPersistentModelIndex> seen;
    QList<QMetaObject::Connection> connections;
    bool nameWidthFixed = false; // set explicitly: stop fitting it to the contents
    bool resizing = false; // fitNameColumn() is resizing the section
    bool tabStopsOnCheckBoxes = false;
    // Colours for style sheets (1.6); invalid / NoBrush: the default.
    QBrush groupBackground;
    QColor groupForeground;
    QColor modifiedForeground;
    QColor readOnlyForeground;
};

} // namespace detail

PropertyTreeView::PropertyTreeView(QWidget* parent)
    : QTreeView(parent)
    , d(std::make_unique<detail::PropertyTreeViewPrivate>(this))
{
    setItemDelegate(d->delegate);
    setEditTriggers(QAbstractItemView::CurrentChanged | QAbstractItemView::SelectedClicked
        | QAbstractItemView::EditKeyPressed);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setAlternatingRowColors(true);
    setUniformRowHeights(true);
    setAllColumnsShowFocus(true);
    header()->setStretchLastSection(true);
    header()->setSectionResizeMode(QHeaderView::Interactive);
    connect(header(), &QHeaderView::sectionResized, this, [this](int section) {
        if (section == PropertyModel::NameColumn && !d->resizing)
            d->nameWidthFixed = true;
    });
}

PropertyTreeView::~PropertyTreeView() = default;

void PropertyTreeView::setModel(QAbstractItemModel* model)
{
    for (const QMetaObject::Connection& connection : std::as_const(d->connections))
        disconnect(connection);
    d->connections.clear();

    QTreeView::setModel(model);
    d->updateAll();
    if (!model)
        return;

    d->connections << connect(model, &QAbstractItemModel::rowsInserted, this,
        [this](const QModelIndex& parent, int first, int last) {
            d->updateRows(parent, first, last, true);
            d->fitNameColumn();
        });
    d->connections << connect(model, &QAbstractItemModel::dataChanged, this,
        [this](
            const QModelIndex& topLeft, const QModelIndex& bottomRight, const QVector<int>& roles) {
            if (roles.isEmpty() || roles.contains(PropertyModel::IsVisibleRole)
                || roles.contains(PropertyModel::IsGroupRole)) {
                d->updateRows(topLeft.parent(), topLeft.row(), bottomRight.row(), false);
            }
            if (topLeft.column() == PropertyModel::NameColumn)
                d->fitNameColumn();
        });
    d->connections << connect(
        model, &QAbstractItemModel::modelReset, this, [this] { d->updateAll(); });
    d->connections << connect(
        model, &QAbstractItemModel::layoutChanged, this, [this] { d->updateAll(); });
}

PropertyTreeView::Mode PropertyTreeView::mode() const
{
    return d->mode;
}

void PropertyTreeView::setMode(Mode mode)
{
    if (d->mode == mode)
        return;
    d->mode = mode;
    d->applyMode();
    d->updateAll(); // indentation changed: refit the name column
}

int PropertyTreeView::nameColumnWidth() const
{
    return columnWidth(PropertyModel::NameColumn);
}

void PropertyTreeView::setNameColumnWidth(int width)
{
    d->nameWidthFixed = true;
    setColumnWidth(PropertyModel::NameColumn, width);
}

PropertyDelegate* PropertyTreeView::propertyDelegate() const
{
    return d->delegate;
}

bool PropertyTreeView::tabStopsOnCheckBoxes() const
{
    return d->tabStopsOnCheckBoxes;
}

void PropertyTreeView::setTabStopsOnCheckBoxes(bool on)
{
    d->tabStopsOnCheckBoxes = on;
}

QBrush PropertyTreeView::groupBackground() const
{
    return d->groupBackground;
}

void PropertyTreeView::setGroupBackground(const QBrush& brush)
{
    d->groupBackground = brush;
    viewport()->update();
}

QColor PropertyTreeView::groupForeground() const
{
    return d->groupForeground;
}

void PropertyTreeView::setGroupForeground(const QColor& color)
{
    d->groupForeground = color;
    viewport()->update();
}

QColor PropertyTreeView::modifiedForeground() const
{
    return d->modifiedForeground;
}

void PropertyTreeView::setModifiedForeground(const QColor& color)
{
    d->modifiedForeground = color;
    viewport()->update();
}

QColor PropertyTreeView::readOnlyForeground() const
{
    return d->readOnlyForeground;
}

void PropertyTreeView::setReadOnlyForeground(const QColor& color)
{
    d->readOnlyForeground = color;
    viewport()->update();
}

void PropertyTreeView::contextMenuEvent(QContextMenuEvent* event)
{
    const QModelIndex index = indexAt(event->pos());
    const Property* property = detail::propertyOf(index);
    QModelIndex sourceIndex;
    PropertyModel* propertyModel = detail::propertyModelOf(index, &sourceIndex);
    if (!property || !propertyModel) {
        QTreeView::contextMenuEvent(event);
        return;
    }

    QMenu menu(this);
    QAction* reset
        = menu.addAction(property->isGroup() ? tr("Reset group") : tr("Reset to default"));
    reset->setEnabled(detail::isResettableByUser(property));
    // A user action: read-only and disabled properties (possibly maintained by
    // the application) are left alone.
    connect(reset, &QAction::triggered, propertyModel,
        [propertyModel, persistent = QPersistentModelIndex(sourceIndex)] {
            if (const Property* target = propertyModel->propertyAt(persistent))
                detail::resetByUser(propertyModel, target);
        });
    menu.exec(event->globalPos());
    event->accept();
}

QModelIndex PropertyTreeView::moveCursor(CursorAction cursorAction, Qt::KeyboardModifiers modifiers)
{
    if (cursorAction != MoveNext && cursorAction != MovePrevious)
        return QTreeView::moveCursor(cursorAction, modifiers);

    const bool forward = cursorAction == MoveNext;
    QModelIndex index = currentIndex().siblingAtColumn(PropertyModel::NameColumn);
    while (true) {
        index = forward ? indexBelow(index) : indexAbove(index);
        if (!index.isValid())
            return QModelIndex();
        if (d->isTabStop(index))
            return index.siblingAtColumn(PropertyModel::ValueColumn);
    }
}

bool PropertyTreeView::focusNextPrevChild(bool next)
{
    // A check box stop has no editor, so Tab reaches the view itself: go on
    // along the chain instead of leaving it (at either end, leave as usual).
    if (state() != EditingState && d->isCheckBoxStop(currentIndex())) {
        const QModelIndex target = moveCursor(next ? MoveNext : MovePrevious, Qt::NoModifier);
        if (target.isValid()) {
            setCurrentIndex(target);
            return true;
        }
    }
    return QTreeView::focusNextPrevChild(next);
}

} // namespace qpb
