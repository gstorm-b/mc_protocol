#include <qpb/Property.h>
#include <qpb/PropertyModel.h>
#include <qpb/widgets/EditorFactory.h>
#include <qpb/widgets/PropertyFormView.h>

#include <QtCore/qhash.h>
#include <QtCore/qpointer.h>
#include <QtCore/qset.h>
#include <QtCore/qtimer.h>
#include <QtGui/qevent.h>
#include <QtWidgets/qabstractbutton.h>
#include <QtWidgets/qabstractspinbox.h>
#include <QtWidgets/qapplication.h>
#include <QtWidgets/qboxlayout.h>
#include <QtWidgets/qcombobox.h>
#include <QtWidgets/qformlayout.h>
#include <QtWidgets/qlabel.h>
#include <QtWidgets/qlineedit.h>
#include <QtWidgets/qmenu.h>
#include <QtWidgets/qscrollbar.h>
#include <QtWidgets/qstyle.h>
#include <QtWidgets/qtoolbutton.h>
#include <QtWidgets/qtooltip.h>

#include <vector>

#include "widgets_p.h"

namespace qpb {

namespace detail {

namespace {

// One property shown by the form: a label and an editor, or for a group a
// section with a title button and a body.
struct FormRow
{
    QPersistentModelIndex index; // name column
    QString path;
    // Leaves.
    QLabel* label = nullptr;
    QWidget* editor = nullptr; // the editor, or a read-only label (hasEditor false)
    bool hasEditor = false;
    QFormLayout* form = nullptr;
    QVariant attributes; // last attributes applied to the editor
    // Groups.
    QWidget* section = nullptr;
    QToolButton* title = nullptr;
    QWidget* body = nullptr;
};

// Editors are made for table cells, without frames; a form shows them framed.
void showFrames(QWidget* editor)
{
    const auto apply = [](QWidget* widget) {
        if (auto* lineEdit = qobject_cast<QLineEdit*>(widget))
            lineEdit->setFrame(true);
        else if (auto* spinBox = qobject_cast<QAbstractSpinBox*>(widget))
            spinBox->setFrame(true);
        else if (auto* comboBox = qobject_cast<QComboBox*>(widget))
            comboBox->setFrame(true);
    };
    apply(editor);
    const QList<QWidget*> children = editor->findChildren<QWidget*>();
    for (QWidget* child : children) {
        // Not the line edit inside a spin box or combo box.
        const QWidget* parent = child->parentWidget();
        if (!qobject_cast<const QAbstractSpinBox*>(parent)
            && !qobject_cast<const QComboBox*>(parent))
            apply(child);
    }
}

bool isUserEditable(const QModelIndex& value)
{
    const Qt::ItemFlags flags = value.flags();
    return flags.testFlag(Qt::ItemIsEditable) || flags.testFlag(Qt::ItemIsUserCheckable);
}

// Applies the style sheet rules again after a selector property changed.
void repolish(QWidget* widget)
{
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
}

// Group titles and labels of modified properties are bold. Only the weight is
// set, everything else is inherited (from the parent or a style sheet); a
// style sheet resets the font when it polishes the widget, so eventFilter()
// sets it again (D52).
bool wantsBold(const QWidget* widget)
{
    const QString part = widget->property(StylePartProperty).toString();
    return part == QLatin1String("groupTitle")
        || (part == QLatin1String("label") && widget->property(StyleModifiedProperty).toBool());
}

void updateEmphasis(QWidget* widget)
{
    if (wantsBold(widget)) {
        if (!widget->font().bold()) {
            QFont bold;
            bold.setBold(true);
            widget->setFont(bold);
        }
    } else if (widget->testAttribute(Qt::WA_SetFont)) {
        widget->setFont(QFont()); // inherit again
    }
}

} // namespace

class PropertyFormViewPrivate : public QObject
{
public:
    explicit PropertyFormViewPrivate(PropertyFormView* view)
        : q(view)
    { }

    // --- Building --------------------------------------------------------------

    // Structural changes are collected and applied once, when control returns
    // to the event loop or when the view is queried.
    void scheduleRebuild()
    {
        dirty = true;
        if (rebuildScheduled)
            return;
        rebuildScheduled = true;
        QTimer::singleShot(0, this, [this] {
            rebuildScheduled = false;
            ensureBuilt();
        });
    }

    void ensureBuilt()
    {
        if (dirty)
            rebuild();
    }

    void rebuild()
    {
        // Keep what the user typed into the focused editor, if it still exists.
        QString focusPath;
        if (FormRow* focused = rowFor(QApplication::focusWidget())) {
            focusPath = focused->path;
            commit(focused);
        }
        dirty = false;
        ++generation;
        rebuilding = true;
        const int scroll = q->verticalScrollBar()->value();
        if (QWidget* old = q->takeWidget()) {
            old->hide();
            old->deleteLater(); // may be running one of its editors' handlers
        }
        byPath.clear();
        byEditor.clear();
        rows.clear();

        auto* content = new QWidget;
        auto* layout = new QVBoxLayout(content);
        if (model)
            addChildren(layout, QModelIndex());
        layout->addStretch(1);
        q->setWidget(content);
        layout->activate();
        q->verticalScrollBar()->setValue(scroll);
        rebuilding = false;

        if (const FormRow* row = byPath.value(focusPath); row && row->hasEditor)
            row->editor->setFocus();
    }

    void addChildren(QVBoxLayout* container, const QModelIndex& parent)
    {
        QFormLayout* form = nullptr;
        const int count = model->rowCount(parent);
        for (int row = 0; row < count; ++row) {
            const QModelIndex name = model->index(row, PropertyModel::NameColumn, parent);
            if (name.data(PropertyModel::IsGroupRole).toBool()) {
                form = nullptr; // properties after the group start a new form
                addGroup(container, name);
                continue;
            }
            if (!form) {
                form = new QFormLayout;
                form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
                container->addLayout(form);
            }
            addLeaf(form, name);
        }
    }

    FormRow* addRow(const QModelIndex& name)
    {
        rows.push_back(std::make_unique<FormRow>());
        FormRow* row = rows.back().get();
        row->index = name;
        row->path = name.data(PropertyModel::PathRole).toString();
        byPath.insert(row->path, row);
        return row;
    }

    void addGroup(QVBoxLayout* container, const QModelIndex& name)
    {
        FormRow* row = addRow(name);
        row->section = new QWidget;
        row->section->setProperty(StylePartProperty, QStringLiteral("group"));
        auto* sectionLayout = new QVBoxLayout(row->section);
        sectionLayout->setContentsMargins(0, 0, 0, 0);

        row->title = new QToolButton;
        row->title->setProperty(StylePartProperty, QStringLiteral("groupTitle"));
        row->title->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        row->title->setAutoRaise(true);
        row->title->setCheckable(true);
        row->title->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        row->title->installEventFilter(this);
        updateEmphasis(row->title);
        row->body = new QWidget;
        row->body->setProperty(StylePartProperty, QStringLiteral("groupBody"));
        auto* bodyLayout = new QVBoxLayout(row->body);
        bodyLayout->setContentsMargins(q->fontMetrics().height(), 0, 0, 0);
        sectionLayout->addWidget(row->title);
        sectionLayout->addWidget(row->body);

        setSectionExpanded(*row, !collapsed.contains(row->path));
        connect(row->title, &QToolButton::toggled, this,
            [this, path = row->path](bool expanded) { q->setExpanded(path, expanded); });
        enableResetMenu(row->title, row->path);

        container->addWidget(row->section);
        addChildren(bodyLayout, name);
        updateRow(*row);
    }

    void addLeaf(QFormLayout* form, const QModelIndex& name)
    {
        FormRow* row = addRow(name);
        row->form = form;
        row->label = new QLabel;
        row->label->setProperty(StylePartProperty, QStringLiteral("label"));
        row->label->setProperty(StyleModifiedProperty, false);
        row->label->installEventFilter(this);
        const Property* property = propertyOf(name);
        QWidget* editor
            = property ? EditorFactory::global().createEditor(nullptr, *property) : nullptr;
        row->hasEditor = editor != nullptr;
        if (editor) {
            showFrames(editor);
            editor->installEventFilter(this);
            const QList<QWidget*> children = editor->findChildren<QWidget*>();
            for (QWidget* child : children)
                child->installEventFilter(this);
            // Check boxes and other checkable buttons commit when toggled.
            if (auto* button = qobject_cast<QAbstractButton*>(editor);
                button && button->isCheckable())
                connect(button, &QAbstractButton::toggled, this,
                    [this, editor] { commitEditor(editor); });
        } else {
            auto* text = new QLabel; // a type without an editor: display only
            text->setProperty(StylePartProperty, QStringLiteral("value"));
            text->setTextInteractionFlags(Qt::TextSelectableByMouse);
            editor = text;
        }
        row->editor = editor;
        row->label->setBuddy(editor);
        enableResetMenu(row->label, row->path);
        form->addRow(row->label, editor);
        byEditor.insert(editor, row);
        updateRow(*row);
    }

    // --- Model to view -----------------------------------------------------------

    void updateRow(FormRow& row)
    {
        const QModelIndex name = row.index;
        if (!name.isValid())
            return;
        const QModelIndex value = name.siblingAtColumn(PropertyModel::ValueColumn);
        const bool visible = name.data(PropertyModel::IsVisibleRole).toBool();
        const bool enabled = value.flags().testFlag(Qt::ItemIsEnabled);
        const QString displayName = name.data(Qt::DisplayRole).toString();
        const QString toolTip = name.data(Qt::ToolTipRole).toString();

        if (row.section) {
            row.section->setVisible(visible);
            row.title->setText(displayName);
            row.title->setToolTip(toolTip);
            return;
        }

        row.form->setRowVisible(row.editor, visible);
        row.label->setText(displayName);
        row.label->setToolTip(toolTip);
        row.label->setEnabled(enabled);
        const bool modified = name.data(PropertyModel::IsModifiedRole).toBool();
        if (row.label->property(StyleModifiedProperty).toBool() != modified) {
            row.label->setProperty(StyleModifiedProperty, modified);
            repolish(row.label); // rules for [qpbModified="true"] apply at once
        }
        updateEmphasis(row.label);

        if (!row.hasEditor) {
            static_cast<QLabel*>(row.editor)->setText(value.data(Qt::DisplayRole).toString());
            row.editor->setEnabled(enabled);
            return;
        }
        row.editor->setEnabled(enabled && isUserEditable(value));
        row.editor->setToolTip(toolTip);
        const Property* property = propertyOf(name);
        const EditorHandler* handler
            = property ? EditorFactory::global().handlerFor(*property) : nullptr;
        if (!handler)
            return;
        const QSignalBlocker blocker(row.editor);
        const QVariant attributes = name.data(PropertyModel::AttributesRole);
        if (handler->applyAttributes && attributes != row.attributes) {
            row.attributes = attributes;
            handler->applyAttributes(row.editor, *property);
        }
        const QVariant current = value.data(Qt::EditRole);
        if (handler->editorData(row.editor, *property) != current)
            handler->setEditorData(row.editor, current, *property);
    }

    void dataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight)
    {
        if (dirty || !model)
            return; // a rebuild is pending and will read everything
        for (int row = topLeft.row(); row <= bottomRight.row(); ++row) {
            const QModelIndex name = model->index(row, PropertyModel::NameColumn, topLeft.parent());
            if (FormRow* formRow = byPath.value(name.data(PropertyModel::PathRole).toString()))
                updateRow(*formRow);
        }
    }

    void setSectionExpanded(FormRow& row, bool expanded)
    {
        const QSignalBlocker blocker(row.title);
        row.title->setChecked(expanded);
        row.title->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        row.body->setVisible(expanded);
    }

    // --- View to model -----------------------------------------------------------

    // The row whose editor is or contains widget.
    FormRow* rowFor(QObject* object) const
    {
        for (auto* widget = qobject_cast<QWidget*>(object); widget;
             widget = widget->parentWidget()) {
            if (FormRow* row = byEditor.value(widget))
                return row;
        }
        return nullptr;
    }

    void commitEditor(QWidget* widget)
    {
        commit(rowFor(widget));
    }

    // Writes the editor's value through the model (R3). A rejected value is
    // replaced by the model's value and the reason is shown as a tool tip.
    void commit(FormRow* row)
    {
        if (rebuilding || !row || !row->hasEditor || !model || !row->index.isValid())
            return;
        const QModelIndex value
            = QModelIndex(row->index).siblingAtColumn(PropertyModel::ValueColumn);
        const Property* property = propertyOf(value);
        const EditorHandler* handler
            = property ? EditorFactory::global().handlerFor(*property) : nullptr;
        if (!handler || !isUserEditable(value))
            return;
        QWidget* editor = row->editor;
        const QVariant edited = handler->editorData(editor, *property);
        if (edited == value.data(Qt::EditRole))
            return;

        QString error;
        QMetaObject::Connection connection;
        if (PropertyModel* propertyModel = propertyModelOf(value)) {
            connection = connect(propertyModel, &PropertyModel::validationFailed, this,
                [&error](
                    const QString&, const QVariant&, const QString& message) { error = message; });
        }
        const quint64 before = generation;
        const QPointer<QWidget> guard(editor);
        const bool accepted = model->setData(value, edited, Qt::EditRole);
        disconnect(connection);
        if (accepted || before != generation || !guard)
            return;

        {
            const QSignalBlocker blocker(editor);
            handler->setEditorData(editor, value.data(Qt::EditRole), *property);
        }
        if (!error.isEmpty())
            QToolTip::showText(editor->mapToGlobal(QPoint(0, editor->height())), error, editor);
    }

    // Enter: commit once the editor has handled the key (spin boxes and line
    // edits fix up their text first).
    void commitLater(QWidget* widget)
    {
        QTimer::singleShot(0, this, [this, widget = QPointer<QWidget>(widget)] {
            if (widget)
                commitEditor(widget);
        });
    }

    void revert(FormRow* row)
    {
        if (!row || !row->hasEditor || !row->index.isValid())
            return;
        const QModelIndex value
            = QModelIndex(row->index).siblingAtColumn(PropertyModel::ValueColumn);
        const Property* property = propertyOf(value);
        if (const EditorHandler* handler
            = property ? EditorFactory::global().handlerFor(*property) : nullptr) {
            const QSignalBlocker blocker(row->editor);
            handler->setEditorData(row->editor, value.data(Qt::EditRole), *property);
        }
    }

    bool eventFilter(QObject* object, QEvent* event) override
    {
        switch (event->type()) {
        case QEvent::FontChange:
            // A style sheet replaced the font of a bold title or label (D52).
            if (auto* widget = qobject_cast<QWidget*>(object); widget && wantsBold(widget))
                updateEmphasis(widget);
            break;
        case QEvent::FocusOut: {
            FormRow* row = rowFor(object);
            if (!row || rebuilding)
                break;
            // Focus moving between the editor's own child widgets.
            for (QWidget* widget = QApplication::focusWidget(); widget;
                 widget = widget->parentWidget()) {
                if (widget == row->editor)
                    return false;
            }
            // A dialog, popup or menu opened from the editor.
            if (isShowingDialog(row->editor) || QApplication::activePopupWidget())
                break;
            commit(row);
            break;
        }
        case QEvent::KeyPress: {
            const auto* keyEvent = static_cast<QKeyEvent*>(event);
            const int key = keyEvent->key();
            if (key == Qt::Key_Return || key == Qt::Key_Enter) {
                FormRow* row = rowFor(object);
                // Multi-line text: Enter starts a new line, Ctrl+Enter commits.
                if (isMultilineText(object)) {
                    if (!keyEvent->modifiers().testFlag(Qt::ControlModifier))
                        break;
                    commitLater(row ? row->editor : nullptr);
                    return true;
                }
                commitLater(row ? row->editor : nullptr);
            } else if (key == Qt::Key_Escape) {
                revert(rowFor(object));
            }
            break;
        }
        default:
            break;
        }
        return false;
    }

    // --- Context menu --------------------------------------------------------------

    void enableResetMenu(QWidget* widget, const QString& path)
    {
        widget->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(widget, &QWidget::customContextMenuRequested, this,
            [this, widget, path](
                const QPoint& position) { showResetMenu(path, widget->mapToGlobal(position)); });
    }

    void showResetMenu(const QString& path, const QPoint& globalPosition)
    {
        const FormRow* row = byPath.value(path);
        if (!row)
            return;
        QModelIndex sourceIndex;
        PropertyModel* propertyModel = propertyModelOf(row->index, &sourceIndex);
        const Property* property = propertyOf(row->index);
        if (!propertyModel || !property)
            return;
        QMenu menu(q);
        QAction* reset
            = menu.addAction(property->isGroup() ? PropertyFormView::tr("Reset group")
                                                 : PropertyFormView::tr("Reset to default"));
        reset->setEnabled(isResettableByUser(property));
        connect(reset, &QAction::triggered, propertyModel,
            [propertyModel, persistent = QPersistentModelIndex(sourceIndex)] {
                if (const Property* target = propertyModel->propertyAt(persistent))
                    resetByUser(propertyModel, target);
            });
        menu.exec(globalPosition);
    }

    PropertyFormView* q;
    QPointer<QAbstractItemModel> model;
    QList<QMetaObject::Connection> connections;
    std::vector<std::unique_ptr<FormRow>> rows;
    QHash<QString, FormRow*> byPath;
    QHash<const QWidget*, FormRow*> byEditor;
    QSet<QString> collapsed; // paths of collapsed sections
    quint64 generation = 0; // incremented by every rebuild
    bool dirty = false; // a structural change has not been applied yet
    bool rebuildScheduled = false;
    bool rebuilding = false; // also set while the view is destroyed
};

} // namespace detail

PropertyFormView::PropertyFormView(QWidget* parent)
    : QScrollArea(parent)
    , d(std::make_unique<detail::PropertyFormViewPrivate>(this))
{
    setWidgetResizable(true);
    connect(detail::CommitNotifier::instance(), &detail::CommitNotifier::commitRequested, d.get(),
        [this](QWidget* editor) { d->commitEditor(editor); });
    d->rebuild();
}

PropertyFormView::~PropertyFormView()
{
    d->rebuilding = true; // no commits from focus changes while tearing down
    for (const QMetaObject::Connection& connection : std::as_const(d->connections))
        disconnect(connection);
}

void PropertyFormView::setModel(QAbstractItemModel* model)
{
    if (d->model == model)
        return;
    for (const QMetaObject::Connection& connection : std::as_const(d->connections))
        disconnect(connection);
    d->connections.clear();
    d->model = model;
    if (model) {
        detail::PropertyFormViewPrivate* p = d.get();
        d->connections << connect(model, &QAbstractItemModel::dataChanged, p,
            [p](const QModelIndex& topLeft, const QModelIndex& bottomRight) {
                p->dataChanged(topLeft, bottomRight);
            });
        const auto rebuild = [p] { p->scheduleRebuild(); };
        d->connections << connect(model, &QAbstractItemModel::rowsInserted, p, rebuild);
        d->connections << connect(model, &QAbstractItemModel::rowsRemoved, p, rebuild);
        d->connections << connect(model, &QAbstractItemModel::rowsMoved, p, rebuild);
        d->connections << connect(model, &QAbstractItemModel::modelReset, p, rebuild);
        d->connections << connect(model, &QAbstractItemModel::layoutChanged, p, rebuild);
        d->connections << connect(model, &QObject::destroyed, p, rebuild);
    }
    d->rebuild();
}

QAbstractItemModel* PropertyFormView::model() const
{
    return d->model;
}

QWidget* PropertyFormView::editor(const QString& path) const
{
    d->ensureBuilt();
    const detail::FormRow* row = d->byPath.value(path);
    return row ? row->editor : nullptr;
}

bool PropertyFormView::isExpanded(const QString& groupPath) const
{
    return !d->collapsed.contains(groupPath);
}

void PropertyFormView::setExpanded(const QString& groupPath, bool expanded)
{
    if (expanded)
        d->collapsed.remove(groupPath);
    else
        d->collapsed.insert(groupPath);
    d->ensureBuilt();
    if (detail::FormRow* row = d->byPath.value(groupPath); row && row->section)
        d->setSectionExpanded(*row, expanded);
}

} // namespace qpb
