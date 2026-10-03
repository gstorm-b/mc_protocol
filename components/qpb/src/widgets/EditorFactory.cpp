#include <qpb/Attributes.h>
#include <qpb/Property.h>
#include <qpb/widgets/EditorFactory.h>

#include <QtCore/qhash.h>
#include <QtCore/qregularexpression.h>
#include <QtGui/qvalidator.h>
#include <QtWidgets/qcheckbox.h>
#include <QtWidgets/qcombobox.h>
#include <QtWidgets/qlineedit.h>
#include <QtWidgets/qplaintextedit.h>
#include <QtWidgets/qspinbox.h>

#include <limits>
#include <vector>

#include "Int64SpinBox_p.h"
#include "PathEdit_p.h"
#include "widgets_p.h"

namespace qpb {

namespace detail {

class EditorFactoryPrivate
{
public:
    void insert(const TypeId& id, const EditorHandler& handler)
    {
        index.insert(id, handlers.size());
        handlers.push_back(std::make_unique<EditorHandler>(handler));
        order.append(id);
    }

    std::vector<std::unique_ptr<EditorHandler>> handlers;
    QHash<TypeId, size_t> index;
    QList<TypeId> order;
};

} // namespace detail

namespace {

bool isComplete(const EditorHandler& handler)
{
    return handler.createEditor && handler.setEditorData && handler.editorData;
}

// --- Bool (used by form views; tree views toggle a check box instead) -------------

EditorHandler boolEditor()
{
    EditorHandler handler;
    handler.createEditor = [](QWidget* parent, const Property&) { return new QCheckBox(parent); };
    handler.setEditorData = [](QWidget* editor, const QVariant& value, const Property&) {
        static_cast<QCheckBox*>(editor)->setChecked(value.toBool());
    };
    handler.editorData = [](QWidget* editor, const Property&) {
        return QVariant(static_cast<QCheckBox*>(editor)->isChecked());
    };
    return handler;
}

// --- Int -------------------------------------------------------------------------

EditorHandler intEditor()
{
    EditorHandler handler;
    handler.createEditor = [](QWidget* parent, const Property&) {
        auto* spinBox = new QSpinBox(parent);
        spinBox->setFrame(false);
        spinBox->setKeyboardTracking(false);
        return spinBox;
    };
    handler.applyAttributes = [](QWidget* editor, const Property& property) {
        auto* spinBox = static_cast<QSpinBox*>(editor);
        spinBox->setRange(
            property.attribute(Attr::Minimum, std::numeric_limits<int>::min()).toInt(),
            property.attribute(Attr::Maximum, std::numeric_limits<int>::max()).toInt());
        spinBox->setSingleStep(property.attribute(Attr::Step, 1).toInt());
        spinBox->setPrefix(property.attribute(Attr::Prefix).toString());
        spinBox->setSuffix(property.attribute(Attr::Suffix).toString());
    };
    handler.setEditorData = [](QWidget* editor, const QVariant& value, const Property&) {
        static_cast<QSpinBox*>(editor)->setValue(value.toInt());
    };
    handler.editorData = [](QWidget* editor, const Property&) {
        auto* spinBox = static_cast<QSpinBox*>(editor);
        spinBox->interpretText();
        return QVariant(spinBox->value());
    };
    return handler;
}

// --- Int64 -----------------------------------------------------------------------

EditorHandler int64Editor()
{
    EditorHandler handler;
    handler.createEditor = [](QWidget* parent, const Property&) {
        auto* spinBox = new detail::Int64SpinBox(parent);
        spinBox->setFrame(false);
        spinBox->setKeyboardTracking(false);
        return spinBox;
    };
    handler.applyAttributes = [](QWidget* editor, const Property& property) {
        auto* spinBox = static_cast<detail::Int64SpinBox*>(editor);
        spinBox->setRange(
            property.attribute(Attr::Minimum, std::numeric_limits<qint64>::min()).toLongLong(),
            property.attribute(Attr::Maximum, std::numeric_limits<qint64>::max()).toLongLong());
        spinBox->setSingleStep(property.attribute(Attr::Step, 1).toLongLong());
        spinBox->setPrefix(property.attribute(Attr::Prefix).toString());
        spinBox->setSuffix(property.attribute(Attr::Suffix).toString());
    };
    handler.setEditorData = [](QWidget* editor, const QVariant& value, const Property&) {
        static_cast<detail::Int64SpinBox*>(editor)->setValue(value.toLongLong());
    };
    handler.editorData = [](QWidget* editor, const Property&) {
        auto* spinBox = static_cast<detail::Int64SpinBox*>(editor);
        spinBox->interpretInput();
        return QVariant::fromValue(spinBox->value());
    };
    return handler;
}

// --- Double ----------------------------------------------------------------------

EditorHandler doubleEditor()
{
    EditorHandler handler;
    handler.createEditor = [](QWidget* parent, const Property&) {
        auto* spinBox = new QDoubleSpinBox(parent);
        spinBox->setFrame(false);
        spinBox->setKeyboardTracking(false);
        return spinBox;
    };
    handler.applyAttributes = [](QWidget* editor, const Property& property) {
        auto* spinBox = static_cast<QDoubleSpinBox*>(editor);
        // Decimals first: setRange() rounds the limits to the current decimals.
        spinBox->setDecimals(qBound(0, property.attribute(Attr::Decimals, 2).toInt(), 15));
        spinBox->setRange(
            property.attribute(Attr::Minimum, std::numeric_limits<double>::lowest()).toDouble(),
            property.attribute(Attr::Maximum, std::numeric_limits<double>::max()).toDouble());
        spinBox->setSingleStep(property.attribute(Attr::Step, 1.0).toDouble());
        spinBox->setPrefix(property.attribute(Attr::Prefix).toString());
        spinBox->setSuffix(property.attribute(Attr::Suffix).toString());
    };
    handler.setEditorData = [](QWidget* editor, const QVariant& value, const Property&) {
        static_cast<QDoubleSpinBox*>(editor)->setValue(value.toDouble());
    };
    handler.editorData = [](QWidget* editor, const Property&) {
        auto* spinBox = static_cast<QDoubleSpinBox*>(editor);
        spinBox->interpretText();
        return QVariant(spinBox->value());
    };
    return handler;
}

// --- String ----------------------------------------------------------------------

// Height of the multi-line editor, in lines of text.
constexpr int MultilineEditorLines = 4;

EditorHandler stringEditor()
{
    EditorHandler handler;
    handler.createEditor = [](QWidget* parent, const Property& property) -> QWidget* {
        if (property.attribute(Attr::Multiline).toBool()) {
            auto* textEdit = new QPlainTextEdit(parent);
            textEdit->setTabChangesFocus(true);
            const int frame = 2 * textEdit->frameWidth();
            textEdit->setMinimumHeight(textEdit->fontMetrics().lineSpacing() * MultilineEditorLines
                + frame + 2 * int(textEdit->document()->documentMargin()));
            return textEdit;
        }
        auto* lineEdit = new QLineEdit(parent);
        lineEdit->setFrame(false);
        return lineEdit;
    };
    handler.applyAttributes = [](QWidget* editor, const Property& property) {
        if (auto* textEdit = qobject_cast<QPlainTextEdit*>(editor)) {
            textEdit->setPlaceholderText(property.attribute(Attr::Placeholder).toString());
            return; // maxLength and regularExpression are checked on commit
        }
        auto* lineEdit = static_cast<QLineEdit*>(editor);
        const int maxLength = property.attribute(Attr::MaxLength, -1).toInt();
        lineEdit->setMaxLength(maxLength >= 0 ? maxLength : 32767);
        lineEdit->setPlaceholderText(property.attribute(Attr::Placeholder).toString());
        const QString pattern = property.attribute(Attr::RegularExpression).toString();
        const QRegularExpression expression(pattern);
        if (!pattern.isEmpty() && expression.isValid())
            lineEdit->setValidator(new QRegularExpressionValidator(expression, lineEdit));
        else
            lineEdit->setValidator(nullptr);
    };
    handler.setEditorData = [](QWidget* editor, const QVariant& value, const Property&) {
        if (auto* textEdit = qobject_cast<QPlainTextEdit*>(editor)) {
            if (textEdit->toPlainText() != value.toString())
                textEdit->setPlainText(value.toString());
            return;
        }
        static_cast<QLineEdit*>(editor)->setText(value.toString());
    };
    handler.editorData = [](QWidget* editor, const Property&) {
        if (auto* textEdit = qobject_cast<QPlainTextEdit*>(editor))
            return QVariant(textEdit->toPlainText());
        return QVariant(static_cast<QLineEdit*>(editor)->text());
    };
    return handler;
}

// --- Enum ------------------------------------------------------------------------

EditorHandler enumEditor()
{
    EditorHandler handler;
    handler.createEditor = [](QWidget* parent, const Property&) {
        auto* comboBox = new QComboBox(parent);
        comboBox->setFrame(false);
        QObject::connect(comboBox, &QComboBox::activated, comboBox,
            [comboBox] { EditorFactory::notifyCommit(comboBox); });
        return comboBox;
    };
    handler.applyAttributes = [](QWidget* editor, const Property& property) {
        auto* comboBox = static_cast<QComboBox*>(editor);
        const QVariant current = comboBox->currentData();
        comboBox->clear();
        const auto options = property.attribute(Attr::Options).value<QList<EnumOption>>();
        for (const EnumOption& option : options)
            comboBox->addItem(option.label, option.value);
        if (current.isValid())
            comboBox->setCurrentIndex(comboBox->findData(current));
    };
    handler.setEditorData = [](QWidget* editor, const QVariant& value, const Property&) {
        auto* comboBox = static_cast<QComboBox*>(editor);
        for (int i = 0; i < comboBox->count(); ++i) {
            if (comboBox->itemData(i) == value) {
                comboBox->setCurrentIndex(i);
                return;
            }
        }
        comboBox->setCurrentIndex(-1);
    };
    handler.editorData = [](QWidget* editor, const Property&) {
        return static_cast<QComboBox*>(editor)->currentData();
    };
    return handler;
}

// --- FilePath / DirPath ------------------------------------------------------------

EditorHandler pathEditor(detail::PathEdit::Kind kind)
{
    EditorHandler handler;
    handler.createEditor
        = [kind](QWidget* parent, const Property&) { return new detail::PathEdit(kind, parent); };
    handler.applyAttributes = [](QWidget* editor, const Property& property) {
        auto* pathEdit = static_cast<detail::PathEdit*>(editor);
        pathEdit->setFileMode(
            FileMode(property.attribute(Attr::DialogMode, int(FileMode::Open)).toInt()));
        pathEdit->setFilter(property.attribute(Attr::Filter).toString());
        pathEdit->setDefaultDir(property.attribute(Attr::DefaultDir).toString());
    };
    handler.setEditorData = [](QWidget* editor, const QVariant& value, const Property&) {
        static_cast<detail::PathEdit*>(editor)->setPath(value.toString());
    };
    handler.editorData = [](QWidget* editor, const Property&) {
        return QVariant(static_cast<detail::PathEdit*>(editor)->path());
    };
    return handler;
}

} // namespace

EditorFactory& EditorFactory::global()
{
    static EditorFactory factory;
    return factory;
}

EditorFactory::EditorFactory()
    : d(std::make_unique<detail::EditorFactoryPrivate>())
{
    d->insert(Types::Bool, boolEditor());
    d->insert(Types::Int, intEditor());
    d->insert(Types::Double, doubleEditor());
    d->insert(Types::String, stringEditor());
    d->insert(Types::Enum, enumEditor());
    d->insert(Types::FilePath, pathEditor(detail::PathEdit::Kind::File));
    d->insert(Types::DirPath, pathEditor(detail::PathEdit::Kind::Directory));
    d->insert(Types::Int64, int64Editor()); // 1.2
}

EditorFactory::~EditorFactory() = default;

bool EditorFactory::registerEditor(const TypeId& id, const EditorHandler& handler)
{
    if (id.isEmpty() || contains(id) || !isComplete(handler))
        return false;
    d->insert(id, handler);
    return true;
}

bool EditorFactory::replaceEditor(const TypeId& id, const EditorHandler& handler)
{
    const auto it = d->index.constFind(id);
    if (it == d->index.constEnd() || !isComplete(handler))
        return false;
    *d->handlers[*it] = handler;
    return true;
}

bool EditorFactory::contains(const TypeId& id) const
{
    return d->index.contains(id);
}

const EditorHandler* EditorFactory::handler(const TypeId& id) const
{
    const auto it = d->index.constFind(id);
    return it == d->index.constEnd() ? nullptr : d->handlers[*it].get();
}

const EditorHandler* EditorFactory::handlerFor(const Property& property) const
{
    const TypeId editorId = property.attribute(Attr::EditorId).toString();
    if (!editorId.isEmpty()) {
        if (const EditorHandler* found = handler(editorId))
            return found;
    }
    return handler(property.typeId());
}

QList<TypeId> EditorFactory::editors() const
{
    return d->order;
}

QWidget* EditorFactory::createEditor(QWidget* parent, const Property& property) const
{
    const EditorHandler* found = handlerFor(property);
    if (!found)
        return nullptr;
    QWidget* editor = found->createEditor(parent, property);
    if (editor && found->applyAttributes)
        found->applyAttributes(editor, property);
    return editor;
}

void EditorFactory::notifyCommit(QWidget* editor)
{
    if (editor)
        emit detail::CommitNotifier::instance() -> commitRequested(editor);
}

EditorDialogScope::EditorDialogScope(QWidget* editor)
    : m_editor(editor)
{
    if (m_editor) {
        m_editor->setProperty(detail::DialogDepthProperty,
            m_editor->property(detail::DialogDepthProperty).toInt() + 1);
    }
}

EditorDialogScope::~EditorDialogScope()
{
    if (m_editor) {
        m_editor->setProperty(detail::DialogDepthProperty,
            m_editor->property(detail::DialogDepthProperty).toInt() - 1);
    }
}

} // namespace qpb
