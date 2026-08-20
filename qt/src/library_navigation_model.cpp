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
                [this](const QModelIndex&, int first, int last) {
                    handleRowsInserted(first, last);
                });
        connect(library_, &QAbstractItemModel::rowsAboutToBeRemoved, this,
                [this](const QModelIndex&, int first, int last) {
                    handleRowsAboutToBeRemoved(first, last);
                });
        connect(library_, &QAbstractItemModel::dataChanged, this,
                [this](const QModelIndex& first, const QModelIndex& last,
                       const QList<int>& roles) {
                    handleDataChanged(first, last, roles);
                });
        connect(library_, &QAbstractItemModel::modelReset, this,
                [this] { rebuildBaseRows(); });
    }
    if (playlists_ != nullptr) {
        connect(playlists_, &QAbstractItemModel::rowsInserted, this,
                [this] { rebuildBaseRows(); });
        connect(playlists_, &QAbstractItemModel::rowsRemoved, this,
                [this] { rebuildBaseRows(); });
        connect(playlists_, &QAbstractItemModel::dataChanged, this,
                [this](const QModelIndex& first, const QModelIndex& last,
                       const QList<int>&) { updatePlaylistCounts(first, last); });
        connect(playlists_, &QAbstractItemModel::modelReset, this,
                [this] { rebuildBaseRows(); });
    }
    if (tags_ != nullptr) {
        connect(tags_, &QAbstractItemModel::rowsInserted, this,
                [this] { updateTagCount(); });
        connect(tags_, &QAbstractItemModel::rowsRemoved, this,
                [this] { updateTagCount(); });
        connect(tags_, &QAbstractItemModel::modelReset, this,
                [this] { updateTagCount(); });
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
    rebuildTrackStates();
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

void LibraryNavigationModel::rebuildTrackStates()
{
    trackStates_.clear();
    if (library_ == nullptr) return;
    trackStates_.reserve(library_->count());
    for (const TrackRecord& track : library_->tracks()) {
        trackStates_.insert(track.trackId,
                            {QDir::fromNativeSeparators(QDir::cleanPath(track.path)),
                             track.favorite});
    }
}

void LibraryNavigationModel::handleRowsInserted(const int first, const int last)
{
    if (library_ == nullptr) return;
    int favoriteDelta = 0;
    for (int row = first; row <= last; ++row) {
        const QModelIndex sourceIndex = library_->index(row, 0);
        const QString trackId = library_->data(sourceIndex, LibraryModel::TrackIdRole).toString();
        const QString path = QDir::fromNativeSeparators(QDir::cleanPath(
            library_->data(sourceIndex, LibraryModel::PathRole).toString()));
        const bool favorite = library_->data(sourceIndex, LibraryModel::FavoriteRole).toBool();
        trackStates_.insert(trackId, {path, favorite});
        applyPathDelta(path, 1);
        favoriteDelta += favorite ? 1 : 0;
    }
    for (int row = 0; row < nodes_.size(); ++row) {
        if (nodes_.at(row).nodeType == QStringLiteral("library")) {
            updateNodeCount(row, nodes_.at(row).count + last - first + 1, {CountRole});
        } else if (nodes_.at(row).nodeType == QStringLiteral("favorites") && favoriteDelta != 0) {
            updateNodeCount(row, nodes_.at(row).count + favoriteDelta, {CountRole});
        }
    }
}

void LibraryNavigationModel::handleRowsAboutToBeRemoved(const int first, const int last)
{
    int favoriteDelta = 0;
    for (int row = first; row <= last; ++row) {
        const QModelIndex sourceIndex = library_->index(row, 0);
        const QString trackId = library_->data(sourceIndex, LibraryModel::TrackIdRole).toString();
        const TrackState state = trackStates_.value(trackId);
        applyPathDelta(state.path, -1);
        favoriteDelta -= state.favorite ? 1 : 0;
        trackStates_.remove(trackId);
    }
    for (int row = 0; row < nodes_.size(); ++row) {
        if (nodes_.at(row).nodeType == QStringLiteral("library")) {
            updateNodeCount(row, nodes_.at(row).count - (last - first + 1), {CountRole});
        } else if (nodes_.at(row).nodeType == QStringLiteral("favorites") && favoriteDelta != 0) {
            updateNodeCount(row, nodes_.at(row).count + favoriteDelta, {CountRole});
        }
    }
}

void LibraryNavigationModel::handleDataChanged(const QModelIndex& first,
                                               const QModelIndex& last,
                                               const QList<int>& roles)
{
    const bool pathChanged = roles.isEmpty() || roles.contains(LibraryModel::PathRole);
    const bool favoriteChanged = roles.isEmpty() || roles.contains(LibraryModel::FavoriteRole);
    if (library_ == nullptr || (!pathChanged && !favoriteChanged)) return;
    for (int row = first.row(); row <= last.row(); ++row) {
        const QModelIndex sourceIndex = library_->index(row, 0);
        const QString trackId = library_->data(sourceIndex, LibraryModel::TrackIdRole).toString();
        TrackState& state = trackStates_[trackId];
        if (pathChanged) {
            const QString nextPath = QDir::fromNativeSeparators(QDir::cleanPath(
                library_->data(sourceIndex, LibraryModel::PathRole).toString()));
            if (state.path != nextPath) {
                applyPathChange(state.path, nextPath);
                state.path = nextPath;
            }
        }
        if (favoriteChanged) {
            const bool favorite = library_->data(sourceIndex, LibraryModel::FavoriteRole).toBool();
            if (state.favorite != favorite) {
                const int delta = favorite ? 1 : -1;
                state.favorite = favorite;
                for (int nodeRow = 0; nodeRow < nodes_.size(); ++nodeRow) {
                    if (nodes_.at(nodeRow).nodeType == QStringLiteral("favorites")) {
                        updateNodeCount(nodeRow, nodes_.at(nodeRow).count + delta, {CountRole});
                        break;
                    }
                }
            }
        }
    }
}

void LibraryNavigationModel::applyPathDelta(const QString& path, const int delta)
{
    if (path.isEmpty() || delta == 0) return;
    for (int row = 0; row < nodes_.size(); ++row) {
        const Node& node = nodes_.at(row);
        if ((node.nodeType == QStringLiteral("resourceRoot")
             || node.nodeType == QStringLiteral("resourceFolder"))
            && path.startsWith(node.resourceFolder + QLatin1Char('/'), Qt::CaseInsensitive)) {
            updateNodeCount(row, qMax(0, node.count + delta), {CountRole});
        }
    }
}

void LibraryNavigationModel::applyPathChange(const QString& oldPath, const QString& newPath)
{
    for (int row = 0; row < nodes_.size(); ++row) {
        const Node& node = nodes_.at(row);
        if (node.nodeType != QStringLiteral("resourceRoot")
            && node.nodeType != QStringLiteral("resourceFolder")) continue;
        const QString prefix = node.resourceFolder + QLatin1Char('/');
        const bool wasMember = oldPath.startsWith(prefix, Qt::CaseInsensitive);
        const bool isMember = newPath.startsWith(prefix, Qt::CaseInsensitive);
        if (wasMember == isMember) continue;
        updateNodeCount(row, qMax(0, node.count + (isMember ? 1 : -1)), {CountRole});
    }
}

void LibraryNavigationModel::updateTagCount()
{
    for (int row = 0; row < nodes_.size(); ++row) {
        if (nodes_.at(row).nodeType == QStringLiteral("tags")) {
            updateNodeCount(row, tags_ == nullptr ? 0 : tags_->count(), {CountRole});
            return;
        }
    }
}

void LibraryNavigationModel::updatePlaylistCounts(const QModelIndex& first,
                                                  const QModelIndex& last)
{
    if (playlists_ == nullptr) return;
    for (int sourceRow = first.row(); sourceRow <= last.row(); ++sourceRow) {
        const QModelIndex sourceIndex = playlists_->index(sourceRow, 0);
        const QString id = playlists_->data(sourceIndex, PlaylistModel::PlaylistIdRole).toString();
        const int row = rowForNodeId(navigationNodeId(QStringLiteral("playlist"), id));
        if (row >= 0) updateNodeCount(row,
                                      playlists_->data(sourceIndex, PlaylistModel::TrackCountRole).toInt(),
                                      {CountRole});
    }
}

void LibraryNavigationModel::updateNodeCount(const int row, const int nextCount,
                                             const QList<int>& roles)
{
    if (row < 0 || row >= nodes_.size() || nodes_.at(row).count == nextCount) return;
    nodes_[row].count = nextCount;
    emit dataChanged(index(row, 0), index(row, 0), roles);
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
