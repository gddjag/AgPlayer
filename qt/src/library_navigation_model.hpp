#pragma once

#include <QAbstractListModel>
#include <QPointer>
#include <QUrl>

class LibraryManagerController;
class LibraryModel;
class PlaylistModel;
class TagModel;

class LibraryNavigationModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role { NodeIdRole = Qt::UserRole + 1, NodeTypeRole, DepthRole,
                DisplayNameRole, CountRole, ExpandedRole, ResourceFolderRole };
    Q_ENUM(Role)

    explicit LibraryNavigationModel(LibraryModel* library, PlaylistModel* playlists,
                                    TagModel* tags, LibraryManagerController* manager,
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
    };

    static QString navigationNodeId(const QString& type, const QString& stableValue);
    static QString normalizedFolder(const QString& folder);
    int rowForNodeId(const QString& nodeId) const;
    void rebuildBaseRows();
    void refreshCounts();
    QList<Node> immediateChildren(const Node& parent) const;
    int countForFolder(const QString& folder) const;

    QPointer<LibraryModel> library_;
    QPointer<PlaylistModel> playlists_;
    QPointer<TagModel> tags_;
    QPointer<LibraryManagerController> manager_;
    QList<Node> nodes_;
};
