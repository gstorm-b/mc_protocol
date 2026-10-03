#ifndef QPB_PROPERTYFILTERPROXYMODEL_H
#define QPB_PROPERTYFILTERPROXYMODEL_H

#include <qpb/qpbglobal.h>

#include <QtCore/qsortfilterproxymodel.h>

#include <memory>

namespace qpb {

namespace detail {
class PropertyFilterProxyModelPrivate;
}

// Filters a PropertyModel by property name for a search box (README.md,
// "Other core classes"). Since 1.1.
//
// Set the text with the usual QSortFilterProxyModel functions, typically
// setFilterFixedString() connected to QLineEdit::textChanged. The filter
// matches the display name, ignoring case by default. A group is shown when
// one of its descendants matches; when a group's name matches, all of its
// descendants are shown. Hidden properties never match. PropertyTreeView and
// PropertyFormView accept the proxy in place of the model.
class QPB_CORE_EXPORT PropertyFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit PropertyFilterProxyModel(QObject* parent = nullptr);
    ~PropertyFilterProxyModel() override;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    std::unique_ptr<detail::PropertyFilterProxyModelPrivate> d;
};

} // namespace qpb

#endif // QPB_PROPERTYFILTERPROXYMODEL_H
