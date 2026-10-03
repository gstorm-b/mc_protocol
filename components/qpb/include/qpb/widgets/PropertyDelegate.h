#ifndef QPB_WIDGETS_PROPERTYDELEGATE_H
#define QPB_WIDGETS_PROPERTYDELEGATE_H

#include <qpb/qpbglobal.h>

#include <QtWidgets/qstyleditemdelegate.h>

#include <memory>

namespace qpb {

namespace detail {
class PropertyDelegatePrivate;
}

// Item delegate that edits and paints PropertyModel values (README.md, "Widgets").
//
// Editors come from EditorFactory::global(). Enter commits, Escape cancels,
// Tab / Shift+Tab commit and move to the next / previous editable value, and
// losing focus commits (except while an EditorDialogScope is active). Rejected
// values keep the old value and show the validation message as a tool tip.
//
// PropertyTreeView installs one automatically; set it on other views showing a
// PropertyModel (or a proxy of one) to get the same editing behaviour.
class QPB_WIDGETS_EXPORT PropertyDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit PropertyDelegate(QObject* parent = nullptr);
    ~PropertyDelegate() override;

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
        const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void setModelData(
        QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
        const QModelIndex& index) const override;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
        const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

protected:
    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option,
        const QModelIndex& index) override;
    bool eventFilter(QObject* object, QEvent* event) override;

private:
    std::unique_ptr<detail::PropertyDelegatePrivate> d;
};

} // namespace qpb

#endif // QPB_WIDGETS_PROPERTYDELEGATE_H
