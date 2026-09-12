#include "lossless_task_model.hpp"

#include <array>
#include <algorithm>

namespace {

const QHash<int, QByteArray>& taskRoles()
{
    static const QHash<int, QByteArray> roles{
        {LosslessTaskModel::TaskIdRole, "taskId"},
        {LosslessTaskModel::FileNameRole, "fileName"},
        {LosslessTaskModel::FilePathRole, "filePath"},
        {LosslessTaskModel::FormatNameRole, "formatName"},
        {LosslessTaskModel::AudioFormatRole, "audioFormat"},
        {LosslessTaskModel::VerdictCodeRole, "verdictCode"},
        {LosslessTaskModel::VerdictTextRole, "verdictText"},
        {LosslessTaskModel::ConfidenceRole, "confidence"},
        {LosslessTaskModel::StateRole, "state"},
        {LosslessTaskModel::StateTextRole, "stateText"},
        {LosslessTaskModel::CheckedRole, "checked"},
        {LosslessTaskModel::ProgressRole, "progress"},
    };
    return roles;
}

} // namespace

LosslessTaskModel::LosslessTaskModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int LosslessTaskModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : visible_rows_.size();
}

int LosslessTaskModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : 7;
}

QVariant LosslessTaskModel::data(const QModelIndex& index, const int role) const
{
    if (!index.isValid() || index.row() < 0
        || index.row() >= visible_rows_.size()) {
        return {};
    }
    const QVariantMap& task = tasks_.at(visible_rows_.at(index.row()));
    if (role == Qt::DisplayRole) {
        static constexpr std::array<const char*, 7> columns{
            "checked", "fileName", "formatName", "audioFormat",
            "verdictText", "confidence", "stateText"};
        return task.value(QString::fromLatin1(
            columns.at(static_cast<std::size_t>(index.column()))));
    }
    const QByteArray roleName = taskRoles().value(role);
    return roleName.isEmpty()
        ? QVariant{}
        : task.value(QString::fromLatin1(roleName));
}

QHash<int, QByteArray> LosslessTaskModel::roleNames() const
{
    return taskRoles();
}

QVariant LosslessTaskModel::headerData(
    const int section, const Qt::Orientation orientation, const int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole
        || section < 0 || section >= columnCount()) {
        return {};
    }
    static const std::array<const char*, 7> headers{
        "", "文件名", "容器", "音频格式", "判定", "评分", "状态"};
    return QString::fromUtf8(headers.at(static_cast<std::size_t>(section)));
}

void LosslessTaskModel::sort(const int column, const Qt::SortOrder order)
{
    if (column < 0 || column >= columnCount()) return;
    sort_column_ = column;
    sort_order_ = order;
    rebuildVisible();
}

bool LosslessTaskModel::appendTask(const QVariantMap& task)
{
    const QString taskId = task.value(QStringLiteral("taskId")).toString();
    const QString identity = task.value(QStringLiteral("_identity")).toString();
    if (taskId.isEmpty() || identity.isEmpty() || row_by_id_.contains(taskId)
        || id_by_identity_.contains(identity)) {
        return false;
    }
    const int sourceRow = tasks_.size();
    tasks_.append(task);
    row_by_id_.insert(taskId, sourceRow);
    id_by_identity_.insert(identity, taskId);
    if (!isVisible(task)) return true;
    if (sort_column_ >= 0) {
        rebuildVisible();
    } else {
        const int visibleRow = visible_rows_.size();
        beginInsertRows({}, visibleRow, visibleRow);
        visible_rows_.append(sourceRow);
        endInsertRows();
        emit visibleRowsChanged();
    }
    return true;
}

bool LosslessTaskModel::updateTask(const QString& taskId,
                                   const QVariantMap& changes)
{
    const auto found = row_by_id_.constFind(taskId);
    if (found == row_by_id_.cend() || changes.isEmpty()) return false;
    const int sourceRow = found.value();
    QVariantMap& item = tasks_[sourceRow];
    QList<int> changedRoles;
    bool visibilityMayChange = false;
    for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
        if (item.value(it.key()) == it.value()) continue;
        item.insert(it.key(), it.value());
        for (auto role = taskRoles().cbegin(); role != taskRoles().cend();
             ++role) {
            if (QString::fromLatin1(role.value()) == it.key()) {
                changedRoles.append(role.key());
                break;
            }
        }
        visibilityMayChange = visibilityMayChange
            || it.key() == QStringLiteral("fileName")
            || it.key() == QStringLiteral("filePath")
            || it.key() == QStringLiteral("verdictCode")
            || it.key() == QStringLiteral("verdictText");
    }
    if (changedRoles.isEmpty()
        && std::none_of(changes.keyBegin(), changes.keyEnd(),
                        [](const QString& key) { return key.startsWith('_'); })) {
        return false;
    }
    if (visibilityMayChange || sort_column_ >= 0) {
        rebuildVisible();
    } else {
        const int visibleRow = visibleRowForSourceRow(sourceRow);
        if (visibleRow >= 0 && !changedRoles.isEmpty()) {
            emit dataChanged(index(visibleRow, 0),
                             index(visibleRow, columnCount() - 1),
                             changedRoles);
        }
    }
    return true;
}

QVariantMap LosslessTaskModel::task(const QString& taskId) const
{
    const auto found = row_by_id_.constFind(taskId);
    return found == row_by_id_.cend() ? QVariantMap{}
                                      : tasks_.at(found.value());
}

QVariantMap LosslessTaskModel::taskAtVisibleRow(const int row) const
{
    return row < 0 || row >= visible_rows_.size()
        ? QVariantMap{}
        : tasks_.at(visible_rows_.at(row));
}

QVector<QVariantMap> LosslessTaskModel::allTasks() const
{
    return tasks_;
}

QStringList LosslessTaskModel::taskIds() const
{
    QStringList ids;
    ids.reserve(tasks_.size());
    for (const QVariantMap& item : tasks_) {
        ids.append(item.value(QStringLiteral("taskId")).toString());
    }
    return ids;
}

QStringList LosslessTaskModel::visibleTaskIds() const
{
    QStringList ids;
    ids.reserve(visible_rows_.size());
    for (const int row : visible_rows_) {
        ids.append(tasks_.at(row).value(QStringLiteral("taskId")).toString());
    }
    return ids;
}

int LosslessTaskModel::taskCount() const noexcept
{
    return tasks_.size();
}

bool LosslessTaskModel::containsIdentity(const QString& identity) const
{
    return id_by_identity_.contains(identity);
}

QString LosslessTaskModel::taskIdForIdentity(const QString& identity) const
{
    return id_by_identity_.value(identity);
}

int LosslessTaskModel::checkedCount() const
{
    return static_cast<int>(std::count_if(
        tasks_.cbegin(), tasks_.cend(), [](const QVariantMap& item) {
            return item.value(QStringLiteral("checked")).toBool();
        }));
}

void LosslessTaskModel::setChecked(const QString& taskId, const bool checked)
{
    updateTask(taskId, {{QStringLiteral("checked"), checked}});
}

void LosslessTaskModel::selectAllVisible(const bool checked)
{
    if (visible_rows_.isEmpty()) return;
    for (const int sourceRow : visible_rows_) {
        tasks_[sourceRow].insert(QStringLiteral("checked"), checked);
    }
    emit dataChanged(index(0, 0), index(visible_rows_.size() - 1,
                     columnCount() - 1), {CheckedRole});
}

void LosslessTaskModel::removeTasks(const QStringList& taskIds)
{
    if (taskIds.isEmpty()) return;
    const QSet<QString> removed(taskIds.cbegin(), taskIds.cend());
    beginResetModel();
    tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(),
        [&removed](const QVariantMap& item) {
            return removed.contains(
                item.value(QStringLiteral("taskId")).toString());
        }), tasks_.end());
    rebuildIndexes();
    visible_rows_.clear();
    for (int row = 0; row < tasks_.size(); ++row) {
        if (isVisible(tasks_.at(row))) visible_rows_.append(row);
    }
    sortVisibleRows();
    endResetModel();
    emit visibleRowsChanged();
}

void LosslessTaskModel::clearTasks()
{
    if (tasks_.isEmpty()) return;
    beginResetModel();
    tasks_.clear();
    visible_rows_.clear();
    row_by_id_.clear();
    id_by_identity_.clear();
    endResetModel();
    emit visibleRowsChanged();
}

void LosslessTaskModel::setFilter(const QString& filter)
{
    const QString normalized = filter.trimmed().toCaseFolded();
    const QString effective = normalized.isEmpty()
        ? QStringLiteral("all") : normalized;
    if (filter_ == effective) return;
    filter_ = effective;
    rebuildVisible();
}

void LosslessTaskModel::setSearchText(const QString& searchText)
{
    const QString normalized = searchText.trimmed();
    if (search_text_ == normalized) return;
    search_text_ = normalized;
    rebuildVisible();
}

bool LosslessTaskModel::matchesFilter(const QVariantMap& task,
                                      const QString& filter)
{
    if (filter == QStringLiteral("all")) return true;
    const QString verdict = task.value(QStringLiteral("verdictCode")).toString();
    if (filter == QStringLiteral("credible")) {
        return verdict == QStringLiteral("credible_lossless")
            || verdict == QStringLiteral("credible_native_dsd");
    }
    if (filter == QStringLiteral("transcode")) {
        return verdict == QStringLiteral("suspected_lossy_transcode")
            || verdict == QStringLiteral("suspected_lossy_upsample");
    }
    if (filter == QStringLiteral("upsample")) {
        return verdict == QStringLiteral("suspected_upsample")
            || verdict == QStringLiteral("suspected_bit_depth_expansion");
    }
    if (filter == QStringLiteral("inconclusive")) {
        return verdict == QStringLiteral("inconclusive")
            || verdict == QStringLiteral("suspected_pcm_to_dsd")
            || verdict == QStringLiteral("analysis_failed")
            || verdict == QStringLiteral("cancelled");
    }
    return true;
}

bool LosslessTaskModel::isVisible(const QVariantMap& task) const
{
    if (!matchesFilter(task, filter_)) return false;
    if (search_text_.isEmpty()) return true;
    const QString haystack =
        task.value(QStringLiteral("fileName")).toString() + QLatin1Char('\n')
        + task.value(QStringLiteral("filePath")).toString() + QLatin1Char('\n')
        + task.value(QStringLiteral("verdictCode")).toString() + QLatin1Char('\n')
        + task.value(QStringLiteral("verdictText")).toString();
    return haystack.contains(search_text_, Qt::CaseInsensitive);
}

void LosslessTaskModel::rebuildIndexes()
{
    row_by_id_.clear();
    id_by_identity_.clear();
    for (int row = 0; row < tasks_.size(); ++row) {
        const QVariantMap& item = tasks_.at(row);
        row_by_id_.insert(item.value(QStringLiteral("taskId")).toString(), row);
        id_by_identity_.insert(item.value(QStringLiteral("_identity")).toString(),
                               item.value(QStringLiteral("taskId")).toString());
    }
}

void LosslessTaskModel::rebuildVisible()
{
    beginResetModel();
    visible_rows_.clear();
    for (int row = 0; row < tasks_.size(); ++row) {
        if (isVisible(tasks_.at(row))) visible_rows_.append(row);
    }
    sortVisibleRows();
    endResetModel();
    emit visibleRowsChanged();
}

void LosslessTaskModel::sortVisibleRows()
{
    if (sort_column_ >= 0) {
        std::stable_sort(visible_rows_.begin(), visible_rows_.end(),
            [this](const int left, const int right) {
                const QString leftKey =
                    sortKeyForColumn(tasks_.at(left), sort_column_);
                const QString rightKey =
                    sortKeyForColumn(tasks_.at(right), sort_column_);
                const int comparison =
                    QString::localeAwareCompare(leftKey, rightKey);
                return sort_order_ == Qt::AscendingOrder
                    ? comparison < 0 : comparison > 0;
            });
    }
}

int LosslessTaskModel::visibleRowForSourceRow(const int sourceRow) const
{
    return visible_rows_.indexOf(sourceRow);
}

QString LosslessTaskModel::sortKeyForColumn(const QVariantMap& task,
                                            const int column) const
{
    static constexpr std::array<const char*, 7> keys{
        "checked", "fileName", "formatName", "audioFormat",
        "verdictText", "confidence", "stateText"};
    const QVariant value = task.value(QString::fromLatin1(
        keys.at(static_cast<std::size_t>(column))));
    if (column == 5) return QStringLiteral("%1").arg(value.toInt(), 4, 10, QLatin1Char('0'));
    return value.toString();
}
