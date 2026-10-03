#ifndef QPB_WIDGETS_PROPERTYTREEVIEW_H
#define QPB_WIDGETS_PROPERTYTREEVIEW_H

#include <qpb/qpbglobal.h>

#include <QtGui/qbrush.h>
#include <QtGui/qcolor.h>
#include <QtWidgets/qtreeview.h>

#include <memory>

namespace qpb {

class PropertyDelegate;

namespace detail {
class PropertyTreeViewPrivate;
}

// Two-column view (name | value) for a PropertyModel or a proxy of one
// (README.md, "Views and keyboard").
//
// Tree mode shows collapsible groups; List mode shows the same model flat,
// with groups as section headers. Switching modes keeps the model, the values
// and the current selection. Hidden properties are hidden rows; modified
// properties are shown in bold; the context menu offers "Reset to default".
class QPB_WIDGETS_EXPORT PropertyTreeView : public QTreeView
{
    Q_OBJECT
    Q_PROPERTY(Mode mode READ mode WRITE setMode)
    Q_PROPERTY(bool tabStopsOnCheckBoxes READ tabStopsOnCheckBoxes WRITE setTabStopsOnCheckBoxes)
    Q_PROPERTY(QBrush groupBackground READ groupBackground WRITE setGroupBackground)
    Q_PROPERTY(QColor groupForeground READ groupForeground WRITE setGroupForeground)
    Q_PROPERTY(QColor modifiedForeground READ modifiedForeground WRITE setModifiedForeground)
    Q_PROPERTY(QColor readOnlyForeground READ readOnlyForeground WRITE setReadOnlyForeground)

public:
    enum class Mode {
        Tree,
        List,
    };
    Q_ENUM(Mode)

    explicit PropertyTreeView(QWidget* parent = nullptr);
    ~PropertyTreeView() override;

    // Accepts a PropertyModel or any proxy model whose source is one.
    void setModel(QAbstractItemModel* model) override;

    Mode mode() const;
    void setMode(Mode mode);

    // Width of the name column in pixels.
    int nameColumnWidth() const;
    void setNameColumnWidth(int width);

    // The delegate installed by this view.
    PropertyDelegate* propertyDelegate() const;

    // Since 1.5. Whether Tab / Shift+Tab also stop on check boxes (Bool
    // properties the user can change). No editor opens there: Space toggles
    // the value and the next Tab / Shift+Tab goes on. Off by default, as in
    // 1.0-1.4: check boxes are reached with the arrow keys.
    bool tabStopsOnCheckBoxes() const;
    void setTabStopsOnCheckBoxes(bool on);

    // Since 1.6. Colours of what the view paints itself, for style sheets
    // (qpb--PropertyTreeView { qproperty-groupBackground: #2c3038; }) or code
    // (README.md, "Style sheets"). An invalid colour or Qt::NoBrush selects the
    // default: the palette's Button behind group rows, the item text colour for
    // group names and names of modified properties (both stay bold), and
    // PlaceholderText for read-only values. A matching style sheet ::item rule
    // with a color (or background) takes precedence, as in any item view.
    QBrush groupBackground() const;
    void setGroupBackground(const QBrush& brush);
    QColor groupForeground() const;
    void setGroupForeground(const QColor& color);
    QColor modifiedForeground() const;
    void setModifiedForeground(const QColor& color);
    QColor readOnlyForeground() const;
    void setReadOnlyForeground(const QColor& color);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    // MoveNext / MovePrevious (Tab / Shift+Tab while editing) skip to the next
    // editable value, passing over groups, read-only and hidden rows and, unless
    // tabStopsOnCheckBoxes() is set, check boxes.
    QModelIndex moveCursor(CursorAction cursorAction, Qt::KeyboardModifiers modifiers) override;
    // Tab / Shift+Tab on a check box stop go on along the chain (since 1.5).
    bool focusNextPrevChild(bool next) override;

private:
    std::unique_ptr<detail::PropertyTreeViewPrivate> d;
};

} // namespace qpb

#endif // QPB_WIDGETS_PROPERTYTREEVIEW_H
