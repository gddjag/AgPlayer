#include "format_conversion_task_model.hpp"

#include <algorithm>

namespace {

const QHash<int, QByteArray>& task_roles()
{
    static const QHash<int, QByteArray> roles{
        {FormatConversionTaskModel::TaskIdRole, "taskId"},
        {FormatConversionTaskModel::CheckedRole, "checked"},
        {FormatConversionTaskModel::FileNameRole, "fileName"},
        {FormatConversionTaskModel::PathRole, "path"},
        {FormatConversionTaskModel::SourceFormatRole, "sourceFormat"},
        {FormatConversionTaskModel::DurationMsRole, "durationMs"},
        {FormatConversionTaskModel::SampleRateRole, "sampleRate"},
        {FormatConversionTaskModel::BitRateRole, "bitRate"},
        {FormatConversionTaskModel::ChannelLayoutRole, "channelLayout"},
        {FormatConversionTaskModel::SampleFormatRole, "sampleFormat"},
        {FormatConversionTaskModel::OutputFormatRole, "outputFormat"},
        {FormatConversionTaskModel::OutputPathRole, "outputPath"},
        {FormatConversionTaskModel::StatusRole, "status"},
        {FormatConversionTaskModel::StageRole, "stage"},
        {FormatConversionTaskModel::ProgressRole, "progress"},
        {FormatConversionTaskModel::ErrorSummaryRole, "errorSummary"},
        {FormatConversionTaskModel::ErrorDetailRole, "errorDetail"},
        {FormatConversionTaskModel::ResolvedProfileRole, "resolvedProfile"},
        {FormatConversionTaskModel::ImportRootRole, "importRoot"},
        {FormatConversionTaskModel::AudioStreamsRole, "audioStreams"}};
    return roles;
}

} // namespace

FormatConversionTaskModel::FormatConversionTaskModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int FormatConversionTaskModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : tasks_.size();
}

int FormatConversionTaskModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : 9;
}

QVariant FormatConversionTaskModel::data(const QModelIndex& index,
                                         const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= tasks_.size()) {
        return {};
    }
    const QVariantMap& task = tasks_.at(index.row());
    if (role >= TaskIdRole) {
        return task.value(QString::fromLatin1(task_roles().value(role)));
    }
    if (role == Qt::DisplayRole) {
        static const std::array<const char*, 9> columns{
            "checked", "fileName", "sourceFormat", "durationMs", "sampleRate",
            "bitRate", "outputFormat", "status", "progress"};
        return task.value(QString::fromLatin1(columns.at(index.column())));
    }
    return {};
}

QHash<int, QByteArray> FormatConversionTaskModel::roleNames() const
{
    return task_roles();
}

QVariant FormatConversionTaskModel::headerData(
    const int section, const Qt::Orientation orientation, const int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole
        || section < 0 || section >= 9) {
        return {};
    }
    static const std::array<const char*, 9> headers{
        "", "文件名", "原格式", "时长", "采样率", "码率", "输出格式", "状态", "进度"};
    return QString::fromUtf8(headers.at(section));
}

void FormatConversionTaskModel::appendTask(const QVariantMap& task)
{
    const QString taskId = task.value(QStringLiteral("taskId")).toString();
    if (taskId.isEmpty() || rowById_.contains(taskId)) {
        return;
    }
    const int row = tasks_.size();
    beginInsertRows({}, row, row);
    tasks_.append(task);
    rowById_.insert(taskId, row);
    endInsertRows();
}

bool FormatConversionTaskModel::updateTask(const QString& taskId,
                                           const QVariantMap& changes)
{
    const auto found = rowById_.constFind(taskId);
    if (found == rowById_.cend() || changes.isEmpty()) {
        return false;
    }
    QVariantMap& task = tasks_[found.value()];
    QList<int> changedRoles;
    for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
        if (task.value(it.key()) == it.value()) {
            continue;
        }
        task.insert(it.key(), it.value());
        const int role = roleForKey(it.key());
        if (role >= TaskIdRole) {
            changedRoles.append(role);
        }
    }
    if (changedRoles.isEmpty()) {
        return false;
    }
    const QModelIndex first = index(found.value(), 0);
    emit dataChanged(first, index(found.value(), columnCount() - 1),
                     changedRoles);
    return true;
}

QString FormatConversionTaskModel::taskIdAt(const int row) const
{
    return row >= 0 && row < tasks_.size()
        ? tasks_.at(row).value(QStringLiteral("taskId")).toString()
        : QString{};
}

QVariantMap FormatConversionTaskModel::taskAt(const int row) const
{
    return row >= 0 && row < tasks_.size() ? tasks_.at(row) : QVariantMap{};
}

QStringList FormatConversionTaskModel::taskIds() const
{
    QStringList result;
    result.reserve(tasks_.size());
    for (const QVariantMap& task : tasks_) {
        result.append(task.value(QStringLiteral("taskId")).toString());
    }
    return result;
}

bool FormatConversionTaskModel::containsTask(const QString& taskId) const
{
    return rowById_.contains(taskId);
}

int FormatConversionTaskModel::checkedCount() const
{
    return static_cast<int>(std::count_if(
        tasks_.cbegin(), tasks_.cend(), [](const QVariantMap& task) {
            return task.value(QStringLiteral("checked")).toBool();
        }));
}

double FormatConversionTaskModel::durationWeightedProgress() const
{
    double weighted = 0.0;
    double total = 0.0;
    for (const QVariantMap& task : tasks_) {
        const double duration = std::max(
            1.0, task.value(QStringLiteral("durationMs")).toDouble());
        total += duration;
        weighted += duration
                    * task.value(QStringLiteral("progress")).toDouble();
    }
    return total > 0.0 ? weighted / total : 0.0;
}

void FormatConversionTaskModel::setChecked(const QString& taskId,
                                           const bool checked)
{
    updateTask(taskId, {{QStringLiteral("checked"), checked}});
}

void FormatConversionTaskModel::setAllVisibleChecked(const bool checked)
{
    if (tasks_.isEmpty()) {
        return;
    }
    bool changed = false;
    for (QVariantMap& task : tasks_) {
        if (task.value(QStringLiteral("checked")).toBool() != checked) {
            task.insert(QStringLiteral("checked"), checked);
            changed = true;
        }
    }
    if (changed) {
        emit dataChanged(index(0, 0), index(tasks_.size() - 1,
                         columnCount() - 1), {CheckedRole});
    }
}

void FormatConversionTaskModel::removeTasks(const QStringList& taskIds)
{
    QVector<int> rows;
    rows.reserve(taskIds.size());
    for (const QString& taskId : taskIds) {
        const auto found = rowById_.constFind(taskId);
        if (found != rowById_.cend()) {
            rows.append(found.value());
        }
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    for (const int row : rows) {
        beginRemoveRows({}, row, row);
        tasks_.removeAt(row);
        endRemoveRows();
    }
    rebuildRows();
}

void FormatConversionTaskModel::clearTasks()
{
    if (tasks_.isEmpty()) return;
    beginResetModel();
    tasks_.clear();
    rowById_.clear();
    endResetModel();
}

void FormatConversionTaskModel::rebuildRows()
{
    rowById_.clear();
    for (int row = 0; row < tasks_.size(); ++row) {
        rowById_.insert(taskIdAt(row), row);
    }
}

int FormatConversionTaskModel::roleForKey(const QString& key)
{
    for (auto it = task_roles().cbegin(); it != task_roles().cend(); ++it) {
        if (QString::fromLatin1(it.value()) == key) {
            return it.key();
        }
    }
    return -1;
}
