#include "format_conversion_filter_model.hpp"

#include "format_conversion_task_model.hpp"

FormatConversionFilterModel::FormatConversionFilterModel(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    connect(this, &QAbstractItemModel::rowsInserted, this,
            &FormatConversionFilterModel::visibleCountsChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this,
            &FormatConversionFilterModel::visibleCountsChanged);
    connect(this, &QAbstractItemModel::modelReset, this,
            &FormatConversionFilterModel::visibleCountsChanged);
    connect(this, &QAbstractItemModel::dataChanged, this,
            &FormatConversionFilterModel::visibleCountsChanged);
}

QString FormatConversionFilterModel::query() const { return query_; }
QString FormatConversionFilterModel::statusFilter() const { return statusFilter_; }
QString FormatConversionFilterModel::formatFilter() const { return formatFilter_; }
int FormatConversionFilterModel::visibleCount() const { return rowCount(); }

int FormatConversionFilterModel::visibleCheckedCount() const
{
    int count = 0;
    for (int row = 0; row < rowCount(); ++row) {
        if (index(row, 0).data(FormatConversionTaskModel::CheckedRole).toBool()) {
            ++count;
        }
    }
    return count;
}

void FormatConversionFilterModel::setQuery(const QString& value)
{
    const QString normalized = value.trimmed();
    if (query_ == normalized) return;
    query_ = normalized;
    invalidateRowsFilter();
    emit queryChanged();
    emit visibleCountsChanged();
}

void FormatConversionFilterModel::setStatusFilter(const QString& value)
{
    const QString normalized = value.trimmed();
    if (statusFilter_ == normalized) return;
    statusFilter_ = normalized;
    invalidateRowsFilter();
    emit statusFilterChanged();
    emit visibleCountsChanged();
}

void FormatConversionFilterModel::setFormatFilter(const QString& value)
{
    const QString normalized = value.trimmed();
    if (formatFilter_ == normalized) return;
    formatFilter_ = normalized;
    invalidateRowsFilter();
    emit formatFilterChanged();
    emit visibleCountsChanged();
}

QString FormatConversionFilterModel::sourceTaskId(const int proxyRow) const
{
    return index(proxyRow, 0).data(
        FormatConversionTaskModel::TaskIdRole).toString();
}

void FormatConversionFilterModel::setAllVisibleChecked(const bool checked)
{
    auto* tasks = qobject_cast<FormatConversionTaskModel*>(sourceModel());
    if (tasks == nullptr) return;
    for (int row = 0; row < rowCount(); ++row) {
        tasks->setChecked(sourceTaskId(row), checked);
    }
    emit visibleCountsChanged();
}

bool FormatConversionFilterModel::filterAcceptsRow(
    const int sourceRow, const QModelIndex& sourceParent) const
{
    const QModelIndex item = sourceModel()->index(sourceRow, 0, sourceParent);
    if (!statusFilter_.isEmpty()
        && statusFilter_.compare(QStringLiteral("All"),
                                 Qt::CaseInsensitive) != 0
        && item.data(FormatConversionTaskModel::StatusRole).toString()
               .compare(statusFilter_, Qt::CaseInsensitive) != 0) {
        return false;
    }
    if (!formatFilter_.isEmpty()
        && formatFilter_.compare(QStringLiteral("All"),
                                 Qt::CaseInsensitive) != 0
        && item.data(FormatConversionTaskModel::OutputFormatRole).toString()
               .compare(formatFilter_, Qt::CaseInsensitive) != 0
        && item.data(FormatConversionTaskModel::SourceFormatRole).toString()
               .compare(formatFilter_, Qt::CaseInsensitive) != 0) {
        return false;
    }
    if (query_.isEmpty()) return true;
    static const int searchable[]{
        FormatConversionTaskModel::FileNameRole,
        FormatConversionTaskModel::PathRole,
        FormatConversionTaskModel::SourceFormatRole,
        FormatConversionTaskModel::OutputFormatRole,
        FormatConversionTaskModel::ResolvedProfileRole,
        FormatConversionTaskModel::ErrorSummaryRole};
    for (const int role : searchable) {
        if (item.data(role).toString().contains(query_, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}
