#ifndef QPB_WIDGETS_PROPERTYFORMVIEW_H
#define QPB_WIDGETS_PROPERTYFORMVIEW_H

#include <qpb/qpbglobal.h>

#include <QtCore/qstring.h>
#include <QtWidgets/qscrollarea.h>

#include <memory>

QT_BEGIN_NAMESPACE
class QAbstractItemModel;
QT_END_NAMESPACE

namespace qpb {

namespace detail {
class PropertyFormViewPrivate;
}

// Form view for a PropertyModel or a proxy of one (README.md, "Views and keyboard").
// Since 1.1.
//
// Every property is a row with a label and an editor that stays open; every
// group is a collapsible section. Edits are committed through the model when
// the editor loses the focus, on Enter, or at once for check boxes, combo
// boxes and editors calling EditorFactory::notifyCommit(). The view follows
// the model: values, visibility, read-only and enabled states, and added or
// removed properties. Modified properties have a bold label; the context menu
// of a label or section title offers "Reset to default" / "Reset group".
class QPB_WIDGETS_EXPORT PropertyFormView : public QScrollArea
{
    Q_OBJECT

public:
    explicit PropertyFormView(QWidget* parent = nullptr);
    ~PropertyFormView() override;

    // Accepts a PropertyModel or any proxy model whose source is one. The model
    // is not owned; pass nullptr to clear the view.
    void setModel(QAbstractItemModel* model);
    QAbstractItemModel* model() const;

    // The editor shown for the property at path, or nullptr (groups, unknown
    // paths, properties filtered out by a proxy). Properties whose type has no
    // editor are shown with a read-only label, which is returned instead.
    QWidget* editor(const QString& path) const;

    // Whether the section of the group at path is expanded (the default).
    // The state is kept by path, also for groups that do not exist yet.
    bool isExpanded(const QString& groupPath) const;
    void setExpanded(const QString& groupPath, bool expanded);

private:
    std::unique_ptr<detail::PropertyFormViewPrivate> d;
};

} // namespace qpb

#endif // QPB_WIDGETS_PROPERTYFORMVIEW_H
