#ifndef QPB_WIDGETS_EDITORFACTORY_H
#define QPB_WIDGETS_EDITORFACTORY_H

#include <qpb/Types.h>
#include <qpb/qpbglobal.h>

#include <QtCore/qlist.h>
#include <QtCore/qpointer.h>
#include <QtCore/qvariant.h>

#include <functional>
#include <memory>

QT_BEGIN_NAMESPACE
class QPainter;
class QStyleOptionViewItem;
class QWidget;
QT_END_NAMESPACE

namespace qpb {

class Property;

namespace detail {
class EditorFactoryPrivate;
}

// Editor widget behaviour of one type or editor ID (README.md, "Custom editors").
//
// Aggregate that may gain fields at the end in later versions; a field left
// empty means "default behaviour". createEditor, setEditorData and editorData
// are required.
struct EditorHandler
{
    // Creates the editor widget for property as a child of parent.
    std::function<QWidget*(QWidget* parent, const Property& property)> createEditor;
    // Shows value in editor.
    std::function<void(QWidget* editor, const QVariant& value, const Property& property)>
        setEditorData;
    // Returns the value currently entered in editor.
    std::function<QVariant(QWidget* editor, const Property& property)> editorData;
    // Optional: paints the value cell when it is not being edited (e.g. a
    // color swatch). Empty: the model's display text is drawn.
    std::function<void(QPainter* painter, const QStyleOptionViewItem& option, const QVariant& value,
        const Property& property)>
        paint;
    // Optional: applies the property's attributes (range, filter, ...) to
    // editor. Called after createEditor and whenever attributes change.
    std::function<void(QWidget* editor, const Property& property)> applyAttributes;
};

// Registry of editors, keyed by type or editor ID (README.md, "Custom editors").
//
// Editors for the built-in types are registered the first time global() is
// called. Registering under a new ID and setting Attr::EditorId on a property
// replaces the editor of that one property; its storage type and validation
// still follow its TypeId. Not thread-safe; use from the GUI thread.
class QPB_WIDGETS_EXPORT EditorFactory
{
public:
    static EditorFactory& global();

    ~EditorFactory();

    EditorFactory(const EditorFactory&) = delete;
    EditorFactory& operator=(const EditorFactory&) = delete;

    // Registers a new editor. Returns false, changing nothing, if id is
    // already registered or a required field of handler is empty.
    bool registerEditor(const TypeId& id, const EditorHandler& handler);
    // Replaces an already registered editor, built-in ones included. Returns
    // false if id is not registered or a required field is empty.
    bool replaceEditor(const TypeId& id, const EditorHandler& handler);

    bool contains(const TypeId& id) const;
    // Handler registered under id, or nullptr. Stays valid until replaced.
    const EditorHandler* handler(const TypeId& id) const;
    // Handler used for property: Attr::EditorId if set and registered,
    // otherwise its TypeId. nullptr means the property has no editor.
    const EditorHandler* handlerFor(const Property& property) const;
    QList<TypeId> editors() const;

    // Creates the editor for property (see handlerFor()), with attributes
    // applied, or returns nullptr if it has none.
    QWidget* createEditor(QWidget* parent, const Property& property) const;

    // Called by an editor when the user has finished choosing a value (e.g.
    // picked an item or a file) so the view commits it immediately instead of
    // waiting for the editor to lose focus.
    static void notifyCommit(QWidget* editor);

private:
    EditorFactory();

    std::unique_ptr<detail::EditorFactoryPrivate> d;
};

// Marks an editor as showing a modal dialog for as long as the scope object
// lives, so views neither commit nor close the editor when the dialog takes
// focus. Use it in custom editors that open dialogs:
//
//   void ColorButton::pickColor()
//   {
//       qpb::EditorDialogScope scope(this);
//       const QColor color = QColorDialog::getColor(m_color, this);
//       ...
//   }
class QPB_WIDGETS_EXPORT EditorDialogScope
{
public:
    explicit EditorDialogScope(QWidget* editor);
    ~EditorDialogScope();

    EditorDialogScope(const EditorDialogScope&) = delete;
    EditorDialogScope& operator=(const EditorDialogScope&) = delete;

private:
    QPointer<QWidget> m_editor;
};

} // namespace qpb

#endif // QPB_WIDGETS_EDITORFACTORY_H
