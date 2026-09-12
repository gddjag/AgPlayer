#include "tag_filter_model.hpp"

#include "tag_model.hpp"

TagFilterModel::TagFilterModel(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    setSortRole(TagModel::TrackCountRole);
    sort(0, Qt::AscendingOrder);
}

bool TagFilterModel::lessThan(const QModelIndex& left,
                              const QModelIndex& right) const
{
    const int leftCount = sourceModel()->data(left, TagModel::TrackCountRole).toInt();
    const int rightCount = sourceModel()->data(right, TagModel::TrackCountRole).toInt();
    if (leftCount != rightCount)
        return leftCount > rightCount;
    const QString leftName = sourceModel()->data(left, TagModel::DisplayNameRole).toString();
    const QString rightName = sourceModel()->data(right, TagModel::DisplayNameRole).toString();
    return QString::compare(leftName, rightName, Qt::CaseInsensitive) < 0;
}

QString TagFilterModel::query() const { return query_; }

void TagFilterModel::setQuery(const QString& query)
{
    const QString trimmed = query.trimmed();
    if (query_ == trimmed) return;
    query_ = trimmed;
    invalidateRowsFilter();
    emit queryChanged();
}

bool TagFilterModel::filterAcceptsRow(const int sourceRow,
                                      const QModelIndex& sourceParent) const
{
    if (query_.isEmpty()) return true;
    const QModelIndex sourceIndex = sourceModel()->index(sourceRow, 0,
                                                         sourceParent);
    const QString key = sourceModel()->data(sourceIndex,
                                             TagModel::KeyRole).toString();
    const QString displayName = sourceModel()->data(
        sourceIndex, TagModel::DisplayNameRole).toString();
    return key.contains(query_, Qt::CaseInsensitive)
        || displayName.contains(query_, Qt::CaseInsensitive);
}
