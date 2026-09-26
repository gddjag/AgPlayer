#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QUrl>

class ResourceFolderController;
class LibraryModel;
class PlaylistModel;
class TagModel;

class LibraryNavigationModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role { NodeIdRole = Qt::UserRole + 1, NodeTypeRole, DepthRole,
                DisplayNameRole, CountRole, ExpandedRole, ResourceFolderRole,
                HasChildrenRole };
    Q_ENUM(Role)

    explicit LibraryNavigationModel(LibraryModel* library, PlaylistModel* playlists,
                                    TagModel* tags, ResourceFolderController* manager,
                                    QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE bool setExpanded(const QString& nodeId, bool expanded);
    Q_INVOKABLE bool addResourceFolder(const QUrl& folder);
    Q_INVOKABLE bool removeResourceFolder(const QString& folder);

private:
    struct Node {
        QString nodeId;
        QString nodeType;
        int depth = 0;
        QString displayName;
        int count = 0;
        bool expanded = false;
        QString resourceFolder;
        bool hasChildren = false;
    };
    struct TrackState {
        QString path;
        bool favorite = false;
    };

    static QString navigationNodeId(const QString& type, const QString& stableValue);
    static QString normalizedFolder(const QString& folder);
    static QString folderKey(const QString& folder);
    static QString parentFolder(const QString& folder);
    int rowForNodeId(const QString& nodeId) const;
    void rebuildBaseRows();
    void rebuildTrackStates();
    void rebuildResourceTopology();
    void appendVisibleResourceChildren(QList<Node>& rows,
                                       const QString& parentFolder,
                                       int childDepth) const;
    void handleRowsInserted(int first, int last);
    void handleRowsAboutToBeRemoved(int first, int last);
    void handleRowsRemoved();
    void handleDataChanged(const QModelIndex& first, const QModelIndex& last,
                           const QList<int>& roles);
    void applyPathDelta(const QString& path, int delta);
    void applyPathChange(const QString& oldPath, const QString& newPath);
    void updateTagCount();
    void updatePlaylistCounts(const QModelIndex& first, const QModelIndex& last);
    void updateNodeCount(int row, int nextCount, const QList<int>& roles);
    int countForFolder(const QString& folder) const;
    QList<QString> containingFoldersForPath(const QString& path) const;
    bool ensurePathTopology(const QString& path);
    void pruneEmptyFoldersIncrementally();
    int visibleRowForFolderKey(const QString& key) const;
    void updateVisibleHasChildren(const QString& key);
    void insertVisibleFolder(const QString& folder);

    QPointer<LibraryModel> library_;
    QPointer<PlaylistModel> playlists_;
    QPointer<TagModel> tags_;
    QPointer<ResourceFolderController> manager_;
    QList<Node> nodes_;
    QHash<QString, TrackState> trackStates_;
    QHash<QString, QString> folderPathByKey_;
    QHash<QString, QStringList> childrenByFolderKey_;
    QHash<QString, int> folderCountsByKey_;
    QSet<QString> rootFolderKeys_;
    QSet<QString> snapshotFolderKeys_;
    QSet<QString> expandedFolderKeys_;
    bool libraryExpanded_ = true;
    int pendingFavoriteRemoval_ = 0;
};
