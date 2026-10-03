#include <qpb/Property.h>
#include <qpb/PropertyModel.h>
#include <qpb/widgets/EditorFactory.h>
#include <qpb/widgets/PropertyDelegate.h>
#include <qpb/widgets/PropertyTreeView.h>

#include <QtCore/qpointer.h>
#include <QtCore/qtimer.h>
#include <QtGui/qevent.h>
#include <QtGui/qpainter.h>
#include <QtWidgets/qabstractitemview.h>
#include <QtWidgets/qapplication.h>
#include <QtWidgets/qstyle.h>
#include <QtWidgets/qtooltip.h>

#include "widgets_p.h"

namespace qpb {

namespace detail {

class PropertyDelegatePrivate
{
public:
    explicit PropertyDelegatePrivate(PropertyDelegate* delegate)
        : q(delegate)
    { }

    // The editor owned by this delegate that contains object, or nullptr.
    QWidget* editorFor(QObject* object) const
    {
        for (auto* widget = qobject_cast<QWidget*>(object); widget;
             widget = widget->parentWidget()) {
            if (widget->property(EditorOwnerProperty).value<QObject*>() == q)
                return widget;
        }
        return nullptr;
    }

    // True while the view still has editor open. When the view closes an
    // editor it forgets it first, then moves the focus (the editor is still
    // visible at that point): events from that phase must be ignored.
    static bool isOpen(QWidget* editor)
    {
        const QWidget* viewport = editor->parentWidget();
        const auto* view
            = qobject_cast<const QAbstractItemView*>(viewport ? viewport->parentWidget() : nullptr);
        const auto index = editor->property(EditorIndexProperty).value<QPersistentModelIndex>();
        return view && index.isValid() && view->indexWidget(index) == editor;
    }

    void commitAndClose(QWidget* editor, QAbstractItemDelegate::EndEditHint hint)
    {
        if (!isOpen(editor))
            return;
        emit q->commitData(editor);
        emit q->closeEditor(editor, hint);
    }

    // Enter: commit after the event has been processed, so line edits and spin
    // boxes can fix up their text first (as QStyledItemDelegate does).
    void commitAndCloseLater(QWidget* editor)
    {
        QTimer::singleShot(0, q, [this, editor = QPointer<QWidget>(editor)] {
            if (editor)
                commitAndClose(editor, QAbstractItemDelegate::NoHint);
        });
    }

    bool handleFocusOut(QWidget* editor)
    {
        if (!isOpen(editor))
            return false;
        // Focus moving between the editor's own child widgets.
        for (QWidget* widget = QApplication::focusWidget(); widget;
             widget = widget->parentWidget()) {
            if (widget == editor)
                return false;
        }
        // A dialog, popup or menu opened from the editor.
        if (isShowingDialog(editor) || QApplication::activePopupWidget())
            return false;
        commitAndClose(editor, QAbstractItemDelegate::NoHint);
        return false;
    }

    PropertyDelegate* q;
    // Message of the last validation failure during setModelData().
    QString lastError;
};

namespace {

// Sets the colour an item's text is drawn with (a style sheet ::item rule with
// a color still replaces it). Active and inactive groups only, so that
// disabled items keep looking disabled.
void setItemTextColor(QPalette& palette, const QColor& color)
{
    for (const QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive})
        palette.setColor(group, QPalette::Text, color);
}

} // namespace

} // namespace detail

using detail::propertyOf;

PropertyDelegate::PropertyDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
    , d(std::make_unique<detail::PropertyDelegatePrivate>(this))
{
    connect(detail::CommitNotifier::instance(), &detail::CommitNotifier::commitRequested, this,
        [this](QWidget* editor) {
            if (QWidget* owned = d->editorFor(editor))
                d->commitAndClose(owned, QAbstractItemDelegate::NoHint);
        });
}

PropertyDelegate::~PropertyDelegate() = default;

QWidget* PropertyDelegate::createEditor(
    QWidget* parent, const QStyleOptionViewItem&, const QModelIndex& index) const
{
    const Property* property = propertyOf(index);
    if (!property || property->isGroup() || index.column() != PropertyModel::ValueColumn)
        return nullptr;

    QWidget* editor = EditorFactory::global().createEditor(parent, *property);
    if (!editor)
        return nullptr;
    editor->setAutoFillBackground(true);
    editor->setProperty(
        detail::EditorOwnerProperty, QVariant::fromValue(static_cast<QObject*>(d->q)));
    editor->setProperty(
        detail::EditorIndexProperty, QVariant::fromValue(QPersistentModelIndex(index)));
    // The view filters the editor itself; child widgets (e.g. the line edit of
    // a path editor) need the filter too for keys and focus changes.
    const QList<QWidget*> children = editor->findChildren<QWidget*>();
    for (QWidget* child : children)
        child->installEventFilter(d->q);
    return editor;
}

void PropertyDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const
{
    const Property* property = propertyOf(index);
    const EditorHandler* handler
        = property ? EditorFactory::global().handlerFor(*property) : nullptr;
    if (handler)
        handler->setEditorData(editor, index.data(Qt::EditRole), *property);
}

void PropertyDelegate::setModelData(
    QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const
{
    const Property* property = propertyOf(index);
    const EditorHandler* handler
        = property ? EditorFactory::global().handlerFor(*property) : nullptr;
    if (!handler)
        return;

    d->lastError.clear();
    QMetaObject::Connection connection;
    if (PropertyModel* propertyModel = detail::propertyModelOf(index)) {
        connection = connect(propertyModel, &PropertyModel::validationFailed, this,
            [this](const QString&, const QVariant&, const QString& message) {
                d->lastError = message;
            });
    }
    const bool accepted
        = model->setData(index, handler->editorData(editor, *property), Qt::EditRole);
    disconnect(connection);

    if (!accepted && !d->lastError.isEmpty()) {
        // Shown once the editor has closed: focus returning to the view would
        // otherwise hide the tool tip immediately.
        const QPoint position = editor->mapToGlobal(QPoint(0, editor->height()));
        QTimer::singleShot(0, this,
            [position, message = d->lastError,
                viewport = QPointer<QWidget>(editor->parentWidget())] {
                if (viewport)
                    QToolTip::showText(position, message, viewport);
            });
    }
}

void PropertyDelegate::updateEditorGeometry(
    QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex&) const
{
    // Editors taller than a row (multi-line text) grow downwards, or upwards
    // when they would leave the viewport.
    QRect rect = option.rect;
    if (editor->minimumHeight() > rect.height()) {
        rect.setHeight(editor->minimumHeight());
        if (const QWidget* viewport = editor->parentWidget()) {
            if (rect.bottom() >= viewport->height())
                rect.moveBottom(viewport->height() - 1);
            if (rect.top() < 0)
                rect.moveTop(0);
        }
    }
    editor->setGeometry(rect);
}

void PropertyDelegate::paint(
    QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    // Colours a PropertyTreeView may set, e.g. from a style sheet (1.6).
    const auto* tree = qobject_cast<const PropertyTreeView*>(opt.widget);
    const bool isGroup = index.data(PropertyModel::IsGroupRole).toBool();
    if (isGroup) {
        opt.font.setBold(true);
        if (!(opt.state & QStyle::State_Selected)) {
            // A style sheet's background-color also sets Button, to the colour
            // of the other rows; groupBackground keeps group rows apart.
            const QBrush background = tree ? tree->groupBackground() : QBrush();
            opt.backgroundBrush
                = background.style() != Qt::NoBrush ? background : opt.palette.button();
        }
        if (tree && tree->groupForeground().isValid())
            detail::setItemTextColor(opt.palette, tree->groupForeground());
    } else if (index.column() == PropertyModel::NameColumn
        && index.data(PropertyModel::IsModifiedRole).toBool()) {
        opt.font.setBold(true);
        if (tree && tree->modifiedForeground().isValid())
            detail::setItemTextColor(opt.palette, tree->modifiedForeground());
    } else if (index.column() == PropertyModel::ValueColumn) {
        // Read-only values (enabled, but neither editable nor checkable) are dimmed.
        const Qt::ItemFlags flags = index.flags();
        if (flags.testFlag(Qt::ItemIsEnabled) && !flags.testFlag(Qt::ItemIsEditable)
            && !flags.testFlag(Qt::ItemIsUserCheckable)) {
            const QColor color = tree && tree->readOnlyForeground().isValid()
                ? tree->readOnlyForeground()
                : opt.palette.color(QPalette::PlaceholderText);
            detail::setItemTextColor(opt.palette, color);
        }
    }
    if (index.column() == PropertyModel::ValueColumn)
        opt.textElideMode = Qt::ElideMiddle;

    const Property* property = propertyOf(index);
    const EditorHandler* handler
        = property && !isGroup && index.column() == PropertyModel::ValueColumn
        ? EditorFactory::global().handlerFor(*property)
        : nullptr;

    QStyle* style = opt.widget ? opt.widget->style() : QApplication::style();
    if (handler && handler->paint) {
        opt.text.clear();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
        const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget);
        QStyleOptionViewItem valueOption = opt;
        valueOption.rect = textRect;
        painter->save();
        painter->setFont(opt.font);
        painter->setPen(opt.palette.color(
            opt.state & QStyle::State_Selected ? QPalette::HighlightedText : QPalette::Text));
        handler->paint(painter, valueOption, index.data(Qt::EditRole), *property);
        painter->restore();
        return;
    }
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
}

QSize PropertyDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    size.setHeight(qMax(size.height(), option.fontMetrics.height() + 8));
    return size;
}

bool PropertyDelegate::editorEvent(QEvent* event, QAbstractItemModel* model,
    const QStyleOptionViewItem& option, const QModelIndex& index)
{
    const Qt::ItemFlags flags = model->flags(index);
    if (!flags.testFlag(Qt::ItemIsUserCheckable) || !flags.testFlag(Qt::ItemIsEnabled))
        return QStyledItemDelegate::editorEvent(event, model, option, index);

    // Check boxes toggle on a click anywhere in the cell, or on Space / Select.
    const auto toggle = [&] {
        const bool checked = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
        return model->setData(index, checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
    };
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick: {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        return mouse->button() == Qt::LeftButton
            && option.rect.contains(mouse->position().toPoint());
    }
    case QEvent::MouseButtonRelease: {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() != Qt::LeftButton || !option.rect.contains(mouse->position().toPoint()))
            return false;
        toggle();
        return true;
    }
    case QEvent::KeyPress: {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key != Qt::Key_Space && key != Qt::Key_Select)
            return false;
        toggle();
        return true;
    }
    default:
        return false;
    }
}

bool PropertyDelegate::eventFilter(QObject* object, QEvent* event)
{
    QWidget* editor = d->editorFor(object);
    if (!editor)
        return QStyledItemDelegate::eventFilter(object, event);

    switch (event->type()) {
    case QEvent::ShortcutOverride: {
        // Keep the view's and window's shortcuts from stealing editing keys.
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Escape || key == Qt::Key_Return || key == Qt::Key_Enter) {
            event->accept();
            return true;
        }
        return false;
    }
    case QEvent::KeyPress: {
        const auto* keyEvent = static_cast<QKeyEvent*>(event);
        switch (keyEvent->key()) {
        case Qt::Key_Tab:
            d->commitAndClose(editor, QAbstractItemDelegate::EditNextItem);
            return true;
        case Qt::Key_Backtab:
            d->commitAndClose(editor, QAbstractItemDelegate::EditPreviousItem);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            // Multi-line text: Enter starts a new line, Ctrl+Enter commits.
            if (detail::isMultilineText(object)) {
                if (!keyEvent->modifiers().testFlag(Qt::ControlModifier))
                    return false;
                d->commitAndCloseLater(editor);
                return true;
            }
            d->commitAndCloseLater(editor);
            return false;
        case Qt::Key_Escape:
            emit closeEditor(editor, QAbstractItemDelegate::RevertModelCache);
            return true;
        default:
            return false;
        }
    }
    case QEvent::FocusOut:
        return d->handleFocusOut(editor);
    default:
        return false;
    }
}

} // namespace qpb
