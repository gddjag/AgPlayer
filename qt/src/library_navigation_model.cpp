#include "library_navigation_model.hpp"

#include "resource_folder_controller.hpp"
#include "library_model.hpp"
#include "playlist_model.hpp"
#include "resource_path.hpp"
#include "tag_model.hpp"

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <utility>

LibraryNavigationModel::LibraryNavigationModel(
    LibraryModel* library, PlaylistModel* playlists, TagModel* tags,
    ResourceFolderController* manager, QObject* parent)
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
        connect(library_, &QAbstractItemModel::rowsRemoved, this,
                [this] { handleRowsRemoved(); });
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
        connect(manager_, &ResourceFolderController::resourceTopologyChanged,
                this, [this] { rebuildBaseRows(); });
    }
}

int LibraryNavigationModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : nodes_.size();
}

QVariant LibraryNavigationModel::data(const QModelIndex& index,
                                      const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= nodes_.size())
        return {};
    const Node& node = nodes_.at(index.row());
    switch (role) {
    case NodeIdRole: return node.nodeId;
    case NodeTypeRole: return node.nodeType;
    case DepthRole: return node.depth;
    case DisplayNameRole: return node.displayName;
    case CountRole: return node.count;
    case ExpandedRole: return node.expanded;
    case ResourceFolderRole: return node.resourceFolder;
    case HasChildrenRole: return node.hasChildren;
    default: return {};
    }
}

QHash<int, QByteArray> LibraryNavigationModel::roleNames() const
{
    return {{NodeIdRole, "nodeId"}, {NodeTypeRole, "nodeType"},
            {DepthRole, "depth"}, {DisplayNameRole, "displayName"},
            {CountRole, "count"}, {ExpandedRole, "expanded"},
            {ResourceFolderRole, "resourceFolder"},
            {HasChildrenRole, "hasChildren"}};
}

bool LibraryNavigationModel::setExpanded(const QString& nodeId,
                                         const bool expanded)
{
    const int row = rowForNodeId(nodeId);
    if (row < 0) return false;
    const Node parent = nodes_.at(row);
    if (parent.expanded == expanded || !parent.hasChildren) {
        return false;
    }

    if (parent.nodeType == QStringLiteral("library")) {
        if (expanded) {
            QList<Node> playlists;
            if (playlists_ != nullptr) {
                for (int sourceRow = 0; sourceRow < playlists_->rowCount(); ++sourceRow) {
                    const QModelIndex sourceIndex = playlists_->index(sourceRow, 0);
                    const QString id = playlists_->data(
                        sourceIndex, PlaylistModel::PlaylistIdRole).toString();
                    playlists.append({
                        navigationNodeId(QStringLiteral("playlist"), id),
                        QStringLiteral("playlist"), 1,
                        playlists_->data(sourceIndex,
                                         PlaylistModel::NameRole).toString(),
                        playlists_->data(sourceIndex,
                                         PlaylistModel::TrackCountRole).toInt(),
                        false, {}});
                }
            }
            if (!playlists.isEmpty()) {
                beginInsertRows({}, row + 1, row + playlists.size());
                for (int offset = 0; offset < playlists.size(); ++offset)
                    nodes_.insert(row + 1 + offset, playlists.at(offset));
                endInsertRows();
            }
        } else {
            int last = row;
            while (last + 1 < nodes_.size()
                   && nodes_.at(last + 1).depth > parent.depth) {
                ++last;
            }
            if (last > row) {
                beginRemoveRows({}, row + 1, last);
                while (last > row) nodes_.removeAt(last--);
                endRemoveRows();
            }
        }
        libraryExpanded_ = expanded;
        nodes_[row].expanded = expanded;
        emit dataChanged(index(row, 0), index(row, 0), {ExpandedRole});
        return true;
    }

    if (parent.nodeType != QStringLiteral("resourceRoot")
        && parent.nodeType != QStringLiteral("resourceFolder")) {
        return false;
    }

    const QString key = folderKey(parent.resourceFolder);
    if (expanded) {
        expandedFolderKeys_.insert(key);
        QList<Node> children;
        appendVisibleResourceChildren(children, parent.resourceFolder,
                                      parent.depth + 1);
        if (!children.isEmpty()) {
            beginInsertRows({}, row + 1, row + children.size());
            for (int offset = 0; offset < children.size(); ++offset)
                nodes_.insert(row + 1 + offset, children.at(offset));
            endInsertRows();
        }
    } else {
        expandedFolderKeys_.remove(key);
        int last = row;
        while (last + 1 < nodes_.size()
               && nodes_.at(last + 1).depth > parent.depth) {
            ++last;
        }
        if (last > row) {
            beginRemoveRows({}, row + 1, last);
            while (last > row) nodes_.removeAt(last--);
            endRemoveRows();
        }
    }
    nodes_[row].expanded = expanded;
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
    return agplayer::qt::resourcePathIdentity(folder);
}

QString LibraryNavigationModel::folderKey(const QString& folder)
{
    QString key = QDir::fromNativeSeparators(QDir::cleanPath(folder));
    if (agplayer::qt::resourcePathCaseSensitivity() == Qt::CaseInsensitive)
        key = key.toCaseFolded();
    return key;
}

QString LibraryNavigationModel::parentFolder(const QString& folder)
{
    return QDir::fromNativeSeparators(
        QDir::cleanPath(QFileInfo(folder).dir().absolutePath()));
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
    rebuildResourceTopology();
    QList<Node> rows;
    const int libraryCount = library_ == nullptr ? 0 : library_->count();
    const int favoriteCount = library_ == nullptr ? 0 : library_->favoriteCount();
    rows.append({navigationNodeId(QStringLiteral("library"), QStringLiteral("all")),
                 QStringLiteral("library"), 0, tr("我的音乐库"), libraryCount,
                 libraryExpanded_, {}, true});
    if (libraryExpanded_ && playlists_ != nullptr) {
        for (int row = 0; row < playlists_->rowCount(); ++row) {
            const QModelIndex sourceIndex = playlists_->index(row, 0);
            const QString id = playlists_->data(
                sourceIndex, PlaylistModel::PlaylistIdRole).toString();
            rows.append({navigationNodeId(QStringLiteral("playlist"), id),
                         QStringLiteral("playlist"), 1,
                         playlists_->data(sourceIndex,
                                          PlaylistModel::NameRole).toString(),
                         playlists_->data(sourceIndex,
                             PlaylistModel::TrackCountRole).toInt(), false, {}});
        }
    }
    rows.append({navigationNodeId(QStringLiteral("favorites"),
                                  QStringLiteral("favorites")),
                 QStringLiteral("favorites"), 0, tr("我的收藏"), favoriteCount,
                 false, {}});
    rows.append({navigationNodeId(QStringLiteral("tags"), QStringLiteral("manage")),
                 QStringLiteral("tags"), 0, tr("标签管理"),
                 tags_ == nullptr ? 0 : tags_->count(), false, {}});
    rows.append({navigationNodeId(QStringLiteral("section"),
                                  QStringLiteral("resources")),
                 QStringLiteral("resourceSection"), 0, tr("资源文件夹"), 0,
                 false, {}});
    if (manager_ != nullptr) {
        for (const QString& requestedRoot : manager_->monitoredFolders()) {
            const QString root = normalizedFolder(requestedRoot);
            const QString key = folderKey(root);
            const bool expanded = expandedFolderKeys_.contains(key);
            const bool hasChildren =
                !childrenByFolderKey_.value(key).isEmpty();
            rows.append({navigationNodeId(QStringLiteral("root"), root),
                         QStringLiteral("resourceRoot"), 0,
                         QFileInfo(root).fileName().isEmpty()
                             ? root : QFileInfo(root).fileName(),
                         countForFolder(root), expanded, root, hasChildren});
            if (expanded)
                appendVisibleResourceChildren(rows, root, 1);
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
                            {agplayer::qt::resourcePathIdentity(track.path),
                             track.favorite});
    }
}

void LibraryNavigationModel::rebuildResourceTopology()
{
    folderPathByKey_.clear();
    childrenByFolderKey_.clear();
    folderCountsByKey_.clear();
    rootFolderKeys_.clear();
    snapshotFolderKeys_.clear();
    if (manager_ == nullptr) return;

    QStringList roots;
    for (const QString& requestedRoot : manager_->monitoredFolders()) {
        const QString root = normalizedFolder(requestedRoot);
        const QString key = folderKey(root);
        if (root.isEmpty() || rootFolderKeys_.contains(key)) continue;
        roots.append(root);
        rootFolderKeys_.insert(key);
        folderPathByKey_.insert(key, root);
        folderCountsByKey_.insert(key, 0);
    }

    const auto addChain = [this](const QString& requestedFolder,
                                 const QString& root,
                                 const bool snapshot) {
        QString current = QDir::fromNativeSeparators(
            QDir::cleanPath(requestedFolder));
        const QString rootKey = folderKey(root);
        if (!agplayer::qt::resourcePathIdentityIsWithin(current, root)) return;
        while (!current.isEmpty()) {
            const QString key = folderKey(current);
            folderPathByKey_.insert(key, current);
            if (!folderCountsByKey_.contains(key))
                folderCountsByKey_.insert(key, 0);
            if (snapshot) snapshotFolderKeys_.insert(key);
            if (key == rootKey) break;
            const QString parent = parentFolder(current);
            if (folderKey(parent) == key) break;
            current = parent;
        }
    };

    for (const QString& requestedDirectory : manager_->resourceDirectories()) {
        const QString directory = normalizedFolder(requestedDirectory);
        for (const QString& root : std::as_const(roots))
            addChain(directory, root, true);
    }

    for (const TrackState& state : std::as_const(trackStates_)) {
        for (const QString& root : std::as_const(roots)) {
            if (!agplayer::qt::resourcePathIdentityIsWithin(state.path, root))
                continue;
            QString current = parentFolder(state.path);
            const QString rootKey = folderKey(root);
            while (!current.isEmpty()) {
                const QString key = folderKey(current);
                folderPathByKey_.insert(key, current);
                folderCountsByKey_[key] = folderCountsByKey_.value(key) + 1;
                if (key == rootKey) break;
                const QString parent = parentFolder(current);
                if (folderKey(parent) == key) break;
                current = parent;
            }
        }
    }

    for (auto it = folderPathByKey_.cbegin(); it != folderPathByKey_.cend(); ++it) {
        const QString childKey = it.key();
        const QString parent = parentFolder(it.value());
        const QString parentKey = folderKey(parent);
        if (parentKey == childKey || !folderPathByKey_.contains(parentKey))
            continue;
        childrenByFolderKey_[parentKey].append(it.value());
    }
    for (auto it = childrenByFolderKey_.begin();
         it != childrenByFolderKey_.end(); ++it) {
        std::sort(it.value().begin(), it.value().end(),
                  [](const QString& left, const QString& right) {
            return left.compare(
                right, agplayer::qt::resourcePathCaseSensitivity()) < 0;
        });
    }

    const QSet<QString> expanded = expandedFolderKeys_;
    for (const QString& key : expanded) {
        if (!folderPathByKey_.contains(key)) expandedFolderKeys_.remove(key);
    }
}

void LibraryNavigationModel::appendVisibleResourceChildren(
    QList<Node>& rows, const QString& requestedParent, const int childDepth) const
{
    const QStringList children =
        childrenByFolderKey_.value(folderKey(requestedParent));
    for (const QString& folder : children) {
        const QString key = folderKey(folder);
        const bool expanded = expandedFolderKeys_.contains(key);
        const bool hasChildren = !childrenByFolderKey_.value(key).isEmpty();
        rows.append({navigationNodeId(QStringLiteral("folder"), folder),
                     QStringLiteral("resourceFolder"), childDepth,
                     QFileInfo(folder).fileName(), countForFolder(folder),
                     expanded, folder, hasChildren});
        if (expanded)
            appendVisibleResourceChildren(rows, folder, childDepth + 1);
    }
}

void LibraryNavigationModel::handleRowsInserted(const int first, const int last)
{
    if (library_ == nullptr) return;
    int favoriteDelta = 0;
    for (int row = first; row <= last; ++row) {
        const QModelIndex sourceIndex = library_->index(row, 0);
        const QString trackId = library_->data(
            sourceIndex, LibraryModel::TrackIdRole).toString();
        const QString path = agplayer::qt::resourcePathIdentity(library_->data(
            sourceIndex, LibraryModel::PathRole).toString());
        const bool favorite = library_->data(
            sourceIndex, LibraryModel::FavoriteRole).toBool();
        ensurePathTopology(path);
        trackStates_.insert(trackId, {path, favorite});
        applyPathDelta(path, 1);
        favoriteDelta += favorite ? 1 : 0;
    }
    for (int row = 0; row < nodes_.size(); ++row) {
        if (nodes_.at(row).nodeType == QStringLiteral("library")) {
            updateNodeCount(row, nodes_.at(row).count + last - first + 1,
                            {CountRole});
        } else if (nodes_.at(row).nodeType == QStringLiteral("favorites")
                   && favoriteDelta != 0) {
            updateNodeCount(row, nodes_.at(row).count + favoriteDelta,
                            {CountRole});
        }
    }
}

void LibraryNavigationModel::handleRowsAboutToBeRemoved(const int first,
                                                        const int last)
{
    pendingFavoriteRemoval_ = 0;
    for (int row = first; row <= last; ++row) {
        const QModelIndex sourceIndex = library_->index(row, 0);
        const QString trackId = library_->data(
            sourceIndex, LibraryModel::TrackIdRole).toString();
        const TrackState state = trackStates_.take(trackId);
        pendingFavoriteRemoval_ += state.favorite ? 1 : 0;
        applyPathDelta(state.path, -1);
    }
    for (int row = 0; row < nodes_.size(); ++row) {
        if (nodes_.at(row).nodeType == QStringLiteral("library")) {
            updateNodeCount(row, nodes_.at(row).count - (last - first + 1),
                            {CountRole});
        } else if (nodes_.at(row).nodeType == QStringLiteral("favorites")
                   && pendingFavoriteRemoval_ != 0) {
            updateNodeCount(row,
                            nodes_.at(row).count - pendingFavoriteRemoval_,
                            {CountRole});
        }
    }
}

void LibraryNavigationModel::handleRowsRemoved()
{
    pendingFavoriteRemoval_ = 0;
    pruneEmptyFoldersIncrementally();
}

void LibraryNavigationModel::handleDataChanged(const QModelIndex& first,
                                               const QModelIndex& last,
                                               const QList<int>& roles)
{
    const bool pathChanged = roles.isEmpty()
        || roles.contains(LibraryModel::PathRole);
    const bool favoriteChanged = roles.isEmpty()
        || roles.contains(LibraryModel::FavoriteRole);
    if (library_ == nullptr || (!pathChanged && !favoriteChanged)) return;
    for (int row = first.row(); row <= last.row(); ++row) {
        const QModelIndex sourceIndex = library_->index(row, 0);
        const QString trackId = library_->data(
            sourceIndex, LibraryModel::TrackIdRole).toString();
        TrackState& state = trackStates_[trackId];
        if (pathChanged) {
            const QString nextPath = agplayer::qt::resourcePathIdentity(
                library_->data(sourceIndex, LibraryModel::PathRole).toString());
            if (folderKey(state.path) != folderKey(nextPath)) {
                ensurePathTopology(nextPath);
                applyPathChange(state.path, nextPath);
                state.path = nextPath;
            }
        }
        if (favoriteChanged) {
            const bool favorite = library_->data(
                sourceIndex, LibraryModel::FavoriteRole).toBool();
            if (state.favorite != favorite) {
                const int delta = favorite ? 1 : -1;
                state.favorite = favorite;
                for (int nodeRow = 0; nodeRow < nodes_.size(); ++nodeRow) {
                    if (nodes_.at(nodeRow).nodeType
                        == QStringLiteral("favorites")) {
                        updateNodeCount(nodeRow,
                                        nodes_.at(nodeRow).count + delta,
                                        {CountRole});
                        break;
                    }
                }
            }
        }
    }
    pruneEmptyFoldersIncrementally();
}

QList<QString> LibraryNavigationModel::containingFoldersForPath(
    const QString& requestedPath) const
{
    QList<QString> result;
    QSet<QString> seen;
    const QString path = QDir::fromNativeSeparators(
        QDir::cleanPath(requestedPath));
    for (const QString& rootKey : rootFolderKeys_) {
        const QString root = folderPathByKey_.value(rootKey);
        if (!agplayer::qt::resourcePathIdentityIsWithin(path, root)) continue;
        QString current = parentFolder(path);
        while (!current.isEmpty()) {
            const QString key = folderKey(current);
            if (!seen.contains(key)) {
                seen.insert(key);
                result.append(current);
            }
            if (key == rootKey) break;
            const QString parent = parentFolder(current);
            if (folderKey(parent) == key) break;
            current = parent;
        }
    }
    return result;
}

int LibraryNavigationModel::visibleRowForFolderKey(const QString& key) const
{
    for (int row = 0; row < nodes_.size(); ++row) {
        const Node& node = nodes_.at(row);
        if ((node.nodeType == QStringLiteral("resourceRoot")
             || node.nodeType == QStringLiteral("resourceFolder"))
            && folderKey(node.resourceFolder) == key) {
            return row;
        }
    }
    return -1;
}

void LibraryNavigationModel::updateVisibleHasChildren(const QString& key)
{
    const int row = visibleRowForFolderKey(key);
    if (row < 0) return;
    const bool hasChildren = !childrenByFolderKey_.value(key).isEmpty();
    if (nodes_.at(row).hasChildren == hasChildren) return;
    nodes_[row].hasChildren = hasChildren;
    emit dataChanged(index(row, 0), index(row, 0), {HasChildrenRole});
}

void LibraryNavigationModel::insertVisibleFolder(const QString& folder)
{
    const QString parent = parentFolder(folder);
    const QString parentKey = folderKey(parent);
    const int parentRow = visibleRowForFolderKey(parentKey);
    if (parentRow < 0 || !nodes_.at(parentRow).expanded) return;

    const int childDepth = nodes_.at(parentRow).depth + 1;
    int insertRow = parentRow + 1;
    while (insertRow < nodes_.size()
           && nodes_.at(insertRow).depth > nodes_.at(parentRow).depth) {
        if (nodes_.at(insertRow).depth == childDepth
            && folder.compare(nodes_.at(insertRow).resourceFolder,
                              agplayer::qt::resourcePathCaseSensitivity()) < 0) {
            break;
        }
        ++insertRow;
    }

    const QString key = folderKey(folder);
    const Node child{
        navigationNodeId(QStringLiteral("folder"), folder),
        QStringLiteral("resourceFolder"), childDepth,
        QFileInfo(folder).fileName(), countForFolder(folder), false, folder,
        !childrenByFolderKey_.value(key).isEmpty()};
    beginInsertRows({}, insertRow, insertRow);
    nodes_.insert(insertRow, child);
    endInsertRows();
}

bool LibraryNavigationModel::ensurePathTopology(const QString& path)
{
    const QList<QString> folders = containingFoldersForPath(path);
    QSet<QString> missing;
    for (const QString& folder : folders) {
        const QString key = folderKey(folder);
        if (!folderPathByKey_.contains(key)) missing.insert(key);
        folderPathByKey_.insert(key, folder);
        if (!folderCountsByKey_.contains(key))
            folderCountsByKey_.insert(key, 0);
    }
    if (missing.isEmpty()) return false;

    QSet<QString> changedParents;
    for (const QString& key : std::as_const(missing)) {
        const QString folder = folderPathByKey_.value(key);
        const QString parentKey = folderKey(parentFolder(folder));
        if (!folderPathByKey_.contains(parentKey)) continue;
        QStringList& children = childrenByFolderKey_[parentKey];
        const bool alreadyPresent = std::any_of(
            children.cbegin(), children.cend(), [&key](const QString& child) {
                return folderKey(child) == key;
            });
        if (!alreadyPresent) children.append(folder);
        std::sort(children.begin(), children.end(),
                  [](const QString& left, const QString& right) {
            return left.compare(
                right, agplayer::qt::resourcePathCaseSensitivity()) < 0;
        });
        changedParents.insert(parentKey);
    }

    QList<QString> topmostFolders;
    for (const QString& key : std::as_const(missing)) {
        const QString folder = folderPathByKey_.value(key);
        if (!missing.contains(folderKey(parentFolder(folder))))
            topmostFolders.append(folder);
    }
    std::sort(topmostFolders.begin(), topmostFolders.end(),
              [](const QString& left, const QString& right) {
        return left.compare(right,
            agplayer::qt::resourcePathCaseSensitivity()) < 0;
    });
    for (const QString& parentKey : std::as_const(changedParents))
        updateVisibleHasChildren(parentKey);
    for (const QString& folder : std::as_const(topmostFolders))
        insertVisibleFolder(folder);
    return true;
}

void LibraryNavigationModel::pruneEmptyFoldersIncrementally()
{
    QSet<QString> removable;
    for (auto it = folderCountsByKey_.cbegin();
         it != folderCountsByKey_.cend(); ++it) {
        if (it.value() == 0 && !rootFolderKeys_.contains(it.key())
            && !snapshotFolderKeys_.contains(it.key())) {
            removable.insert(it.key());
        }
    }
    if (removable.isEmpty()) return;

    QSet<QString> changedParents;
    QList<QPair<int, QString>> visibleBranches;
    for (const QString& key : std::as_const(removable)) {
        const QString folder = folderPathByKey_.value(key);
        const QString parentKey = folderKey(parentFolder(folder));
        changedParents.insert(parentKey);
        if (removable.contains(parentKey)) continue;
        const int row = visibleRowForFolderKey(key);
        if (row >= 0) visibleBranches.append({row, key});
    }
    std::sort(visibleBranches.begin(), visibleBranches.end(),
              [](const auto& left, const auto& right) {
        return left.first > right.first;
    });
    for (const auto& branch : std::as_const(visibleBranches)) {
        const int first = branch.first;
        const int depth = nodes_.at(first).depth;
        int last = first;
        while (last + 1 < nodes_.size()
               && nodes_.at(last + 1).depth > depth) {
            ++last;
        }
        beginRemoveRows({}, first, last);
        for (int row = last; row >= first; --row) nodes_.removeAt(row);
        endRemoveRows();
    }

    for (auto it = childrenByFolderKey_.begin();
         it != childrenByFolderKey_.end(); ++it) {
        QStringList& children = it.value();
        children.erase(std::remove_if(
            children.begin(), children.end(), [&removable](const QString& child) {
                return removable.contains(folderKey(child));
            }), children.end());
    }
    for (const QString& key : std::as_const(removable)) {
        childrenByFolderKey_.remove(key);
        folderPathByKey_.remove(key);
        folderCountsByKey_.remove(key);
        expandedFolderKeys_.remove(key);
    }
    for (const QString& parentKey : std::as_const(changedParents))
        updateVisibleHasChildren(parentKey);
}

void LibraryNavigationModel::applyPathDelta(const QString& path,
                                            const int delta)
{
    if (path.isEmpty() || delta == 0) return;
    const QList<QString> folders = containingFoldersForPath(path);
    for (const QString& folder : folders) {
        const QString key = folderKey(folder);
        folderCountsByKey_[key] = qMax(0, folderCountsByKey_.value(key) + delta);
        for (int row = 0; row < nodes_.size(); ++row) {
            const Node& node = nodes_.at(row);
            if ((node.nodeType == QStringLiteral("resourceRoot")
                 || node.nodeType == QStringLiteral("resourceFolder"))
                && folderKey(node.resourceFolder) == key) {
                updateNodeCount(row, folderCountsByKey_.value(key), {CountRole});
            }
        }
    }
}

void LibraryNavigationModel::applyPathChange(const QString& oldPath,
                                             const QString& newPath)
{
    if (folderKey(oldPath) == folderKey(newPath)) return;
    QHash<QString, QString> oldFolders;
    QHash<QString, QString> newFolders;
    for (const QString& folder : containingFoldersForPath(oldPath))
        oldFolders.insert(folderKey(folder), folder);
    for (const QString& folder : containingFoldersForPath(newPath))
        newFolders.insert(folderKey(folder), folder);

    const auto applyFolderDelta = [this](const QString& key, const int delta) {
        folderCountsByKey_[key] = qMax(
            0, folderCountsByKey_.value(key) + delta);
        for (int row = 0; row < nodes_.size(); ++row) {
            const Node& node = nodes_.at(row);
            if ((node.nodeType == QStringLiteral("resourceRoot")
                 || node.nodeType == QStringLiteral("resourceFolder"))
                && folderKey(node.resourceFolder) == key) {
                updateNodeCount(row, folderCountsByKey_.value(key), {CountRole});
            }
        }
    };
    for (auto it = oldFolders.cbegin(); it != oldFolders.cend(); ++it) {
        if (!newFolders.contains(it.key())) applyFolderDelta(it.key(), -1);
    }
    for (auto it = newFolders.cbegin(); it != newFolders.cend(); ++it) {
        if (!oldFolders.contains(it.key())) applyFolderDelta(it.key(), 1);
    }
}

void LibraryNavigationModel::updateTagCount()
{
    for (int row = 0; row < nodes_.size(); ++row) {
        if (nodes_.at(row).nodeType == QStringLiteral("tags")) {
            updateNodeCount(row, tags_ == nullptr ? 0 : tags_->count(),
                            {CountRole});
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
        const QString id = playlists_->data(
            sourceIndex, PlaylistModel::PlaylistIdRole).toString();
        const int row = rowForNodeId(
            navigationNodeId(QStringLiteral("playlist"), id));
        if (row >= 0) {
            Node& node = nodes_[row];
            const QString displayName = playlists_->data(
                sourceIndex, PlaylistModel::NameRole).toString();
            const int count = playlists_->data(
                sourceIndex, PlaylistModel::TrackCountRole).toInt();
            QList<int> changedRoles;
            if (node.displayName != displayName) {
                node.displayName = displayName;
                changedRoles.append(DisplayNameRole);
            }
            if (node.count != count) {
                node.count = count;
                changedRoles.append(CountRole);
            }
            if (!changedRoles.isEmpty())
                emit dataChanged(index(row, 0), index(row, 0), changedRoles);
        }
    }
}

void LibraryNavigationModel::updateNodeCount(const int row,
                                             const int nextCount,
                                             const QList<int>& roles)
{
    if (row < 0 || row >= nodes_.size()
        || nodes_.at(row).count == nextCount) return;
    nodes_[row].count = nextCount;
    emit dataChanged(index(row, 0), index(row, 0), roles);
}

int LibraryNavigationModel::countForFolder(const QString& folder) const
{
    return folderCountsByKey_.value(folderKey(folder));
}
