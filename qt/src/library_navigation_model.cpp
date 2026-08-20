#include "library_navigation_model.hpp"

#include "library_manager_controller.hpp"
#include "library_model.hpp"
#include "playlist_model.hpp"
#include "tag_model.hpp"

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>

LibraryNavigationModel::LibraryNavigationModel(LibraryModel* library,
                                               PlaylistModel* playlists, TagModel* tags,
                                               LibraryManagerController* manager,
                                               QObject* parent)
    : QAbstractListModel(parent)
    , library_(library)
    , playlists_(playlists)
    , tags_(tags)
    , manager_(manager)
{
    rebuildBaseRows();
    if (library_ != nullptr) {
        connect(library_, &QAbstractItemModel::rowsInserted, this,
                [this] { refreshCounts(); });
        connect(library_, &QAbstractItemModel::rowsRemoved, this,
                [this] { refreshCounts(); });
        connect(library_, &QAbstractItemModel::dataChanged, this,
                [this] { refreshCounts(); });
        connect(library_, &QAbstractItemModel::modelReset, this,
                [this] { rebuildBaseRows(); });
    }
    if (playlists_ != nullptr) {
        connect(playlists_, &QAbstractItemModel::rowsInserted, this,
                [this] { rebuildBaseRows(); });
        connect(playlists_, &QAbstractItemModel::rowsRemoved, this,
                [this] { rebuildBaseRows(); });
        connect(playlists_, &QAbstractItemModel::dataChanged, this,
                [this] { rebuildBaseRows(); });
        connect(playlists_, &QAbstractItemModel::modelReset, this,
                [this] { rebuildBaseRows(); });
    }
    if (tags_ != nullptr) {
        connect(tags_, &QAbstractItemModel::rowsInserted, this,
                [this] { refreshCounts(); });
        connect(tags_, &QAbstractItemModel::rowsRemoved, this,
                [this] { refreshCounts(); });
        connect(tags_, &QAbstractItemModel::dataChanged, this,
                [this] { refreshCounts(); });
    }
    if (manager_ != nullptr) {
        connect(manager_, &LibraryManagerController::resourceRootsChanged, this,
                [this] { rebuildBaseRows(); });
    }
}

int LibraryNavigationModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : nodes_.size();
}

QVariant LibraryNavigationModel::data(const QModelIndex& index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= nodes_.size()) return {};
    const Node& node = nodes_.at(index.row());
    switch (role) {
    case NodeIdRole: return node.nodeId;
    case NodeTypeRole: return node.nodeType;
    case DepthRole: return node.depth;
    case DisplayNameRole: return node.displayName;
    case CountRole: return node.count;
    case ExpandedRole: return node.expanded;
    case ResourceFolderRole: return node.resourceFolder;
    default: return {};
    }
}

QHash<int, QByteArray> LibraryNavigationModel::roleNames() const
{
    return {{NodeIdRole, "nodeId"}, {NodeTypeRole, "nodeType"}, {DepthRole, "depth"},
            {DisplayNameRole, "displayName"}, {CountRole, "count"},
            {ExpandedRole, "expanded"}, {ResourceFolderRole, "resourceFolder"}};
}

bool LibraryNavigationModel::setExpanded(const QString& nodeId, const bool expanded)
{
    const int row = rowForNodeId(nodeId);
    if (row < 0 || nodes_.at(row).nodeType != QStringLiteral("resourceRoot")
        || nodes_.at(row).expanded == expanded) return false;
    if (expanded) {
        const QList<Node> children = immediateChildren(nodes_.at(row));
        if (!children.isEmpty()) {
            beginInsertRows({}, row + 1, row + children.size());
            for (int offset = 0; offset < children.size(); ++offset) {
                nodes_.insert(row + 1 + offset, children.at(offset));
            }
            endInsertRows();
        }
        nodes_[row].expanded = true;
        emit dataChanged(index(row, 0), index(row, 0), {ExpandedRole});
        return true;
    }
    int last = row;
    while (last + 1 < nodes_.size() && nodes_.at(last + 1).depth > nodes_.at(row).depth) ++last;
    if (last > row) {
        beginRemoveRows({}, row + 1, last);
        while (last > row) nodes_.removeAt(last--);
        endRemoveRows();
    }
    nodes_[row].expanded = false;
    emit dataChanged(index(row, 0), index(row, 0), {ExpandedRole});
    return true;
}

bool LibraryNavigationModel::addResourceFolder(const QUrl& folder)
{
    return manager_ != nullptr && folder.isLocalFile()
        && manager_->addMonitoredFolder(folder.toLocalFile());
}

bool LibraryNavigationModel::removeResourceFolder(const QString& folder)
{
    return manager_ != nullptr && manager_->removeMonitoredFolder(folder);
}

QString LibraryNavigationModel::navigationNodeId(const QString& type,
                                                 const QString& stableValue)
{
    return type + QLatin1Char(':') + stableValue;
}

QString LibraryNavigationModel::normalizedFolder(const QString& folder)
{
    return QDir::fromNativeSeparators(QDir::cleanPath(QFileInfo(folder).absoluteFilePath()));
}

int LibraryNavigationModel::rowForNodeId(const QString& nodeId) const
{
    for (int row = 0; row < nodes_.size(); ++row) {
        if (nodes_.at(row).nodeId == nodeId) return row;
    }
    return -1;
}

void LibraryNavigationModel::rebuildBaseRows()
{
    QList<Node> rows;
    const int libraryCount = library_ == nullptr ? 0 : library_->count();
    const int favoriteCount = library_ == nullptr ? 0 : library_->favoriteCount();
    rows.append({navigationNodeId(QStringLiteral("library"), QStringLiteral("all")),
                 QStringLiteral("library"), 0, tr("我的音乐库"), libraryCount, false, {}});
    rows.append({navigationNodeId(QStringLiteral("favorites"), QStringLiteral("favorites")),
                 QStringLiteral("favorites"), 0, tr("收藏"), favoriteCount, false, {}});
    rows.append({navigationNodeId(QStringLiteral("tags"), QStringLiteral("manage")),
                 QStringLiteral("tags"), 0, tr("标签管理"), tags_ == nullptr ? 0 : tags_->count(), false, {}});
    if (playlists_ != nullptr) {
        for (int row = 0; row < playlists_->rowCount(); ++row) {
            const QModelIndex index = playlists_->index(row, 0);
            const QString id = playlists_->data(index, PlaylistModel::PlaylistIdRole).toString();
            rows.append({navigationNodeId(QStringLiteral("playlist"), id), QStringLiteral("playlist"),
                         0, playlists_->data(index, PlaylistModel::NameRole).toString(),
                         playlists_->data(index, PlaylistModel::TrackCountRole).toInt(), false, {}});
        }
    }
    if (manager_ != nullptr) {
        for (const QString& root : manager_->monitoredFolders()) {
            const QString folder = normalizedFolder(root);
            rows.append({navigationNodeId(QStringLiteral("root"), folder),
                         QStringLiteral("resourceRoot"), 0,
                         QFileInfo(folder).fileName().isEmpty() ? folder : QFileInfo(folder).fileName(),
                         countForFolder(folder), false, folder});
        }
    }
    beginResetModel();
    nodes_ = std::move(rows);
    endResetModel();
}

void LibraryNavigationModel::refreshCounts()
{
    for (int row = 0; row < nodes_.size(); ++row) {
        Node& node = nodes_[row];
        int next = node.count;
        if (node.nodeType == QStringLiteral("library")) next = library_ == nullptr ? 0 : library_->count();
        else if (node.nodeType == QStringLiteral("favorites")) next = library_ == nullptr ? 0 : library_->favoriteCount();
        else if (node.nodeType == QStringLiteral("tags")) next = tags_ == nullptr ? 0 : tags_->count();
        else if (node.nodeType == QStringLiteral("resourceRoot") || node.nodeType == QStringLiteral("resourceFolder")) next = countForFolder(node.resourceFolder);
        if (node.count != next) {
            node.count = next;
            emit dataChanged(index(row, 0), index(row, 0), {CountRole});
        }
    }
}

QList<LibraryNavigationModel::Node> LibraryNavigationModel::immediateChildren(const Node& parent) const
{
    QList<Node> children;
    if (library_ == nullptr) return children;
    const QString prefix = parent.resourceFolder + QLatin1Char('/');
    QSet<QString> folders;
    for (const TrackRecord& track : library_->tracks()) {
        const QString path = QDir::fromNativeSeparators(QDir::cleanPath(track.path));
        if (!path.startsWith(prefix, Qt::CaseInsensitive)) continue;
        const QString relative = path.mid(prefix.size());
        const int slash = relative.indexOf(QLatin1Char('/'));
        if (slash <= 0) continue;
        folders.insert(prefix + relative.left(slash));
    }
    QStringList ordered = folders.values();
    std::sort(ordered.begin(), ordered.end(), [](const QString& left, const QString& right) {
        return left.compare(right, Qt::CaseInsensitive) < 0;
    });
    for (const QString& folder : ordered) {
        children.append({navigationNodeId(QStringLiteral("folder"), folder),
                         QStringLiteral("resourceFolder"), parent.depth + 1,
                         QFileInfo(folder).fileName(), countForFolder(folder), false, folder});
    }
    return children;
}

int LibraryNavigationModel::countForFolder(const QString& folder) const
{
    if (library_ == nullptr) return 0;
    const QString prefix = normalizedFolder(folder) + QLatin1Char('/');
    int count = 0;
    for (const TrackRecord& track : library_->tracks()) {
        const QString path = QDir::fromNativeSeparators(QDir::cleanPath(track.path));
        if (path.startsWith(prefix, Qt::CaseInsensitive)) ++count;
    }
    return count;
}
