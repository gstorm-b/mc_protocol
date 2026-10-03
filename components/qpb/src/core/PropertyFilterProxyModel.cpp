#include <qpb/PropertyFilterProxyModel.h>
#include <qpb/PropertyModel.h>

#include <QtCore/qregularexpression.h>

namespace qpb {

namespace detail {

// Empty for now; keeps room for state added in later versions.
class PropertyFilterProxyModelPrivate
{
};

} // namespace detail

PropertyFilterProxyModel::PropertyFilterProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent)
    , d(std::make_unique<detail::PropertyFilterProxyModelPrivate>())
{
    setFilterKeyColumn(PropertyModel::NameColumn);
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setRecursiveFilteringEnabled(true); // groups with a matching descendant
}

PropertyFilterProxyModel::~PropertyFilterProxyModel() = default;

bool PropertyFilterProxyModel::filterAcceptsRow(
    int sourceRow, const QModelIndex& sourceParent) const
{
    if (filterRegularExpression().pattern().isEmpty())
        return true;
    const QModelIndex index
        = sourceModel()->index(sourceRow, PropertyModel::NameColumn, sourceParent);
    if (!index.data(PropertyModel::IsVisibleRole).toBool())
        return false;
    if (QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent))
        return true;
    // Descendants of a matching group. Done here rather than with
    // autoAcceptChildRows so that subclasses still see every row.
    const QRegularExpression expression = filterRegularExpression();
    for (QModelIndex group = sourceParent; group.isValid(); group = group.parent()) {
        if (expression.match(group.data(filterRole()).toString()).hasMatch())
            return true;
    }
    return false;
}

} // namespace qpb
