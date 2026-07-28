#pragma once

#include <QAbstractListModel>
#include <QSet>

class PlaylistModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        PlaylistIdRole = Qt::UserRole + 1,
        NameRole,
        TrackCountRole,
    };
    Q_ENUM(Role)

    explicit PlaylistModel(QString filePath = {}, QObject* parent = nullptr);

    [[nodiscard]] int count() const;
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index,
                                int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QString createPlaylist(const QString& name);
    Q_INVOKABLE bool renamePlaylist(const QString& playlistId, const QString& name);
    Q_INVOKABLE bool removePlaylist(const QString& playlistId);
    Q_INVOKABLE bool addTrack(const QString& playlistId, const QString& trackId);
    Q_INVOKABLE bool removeTrack(const QString& playlistId, const QString& trackId);
    Q_INVOKABLE bool containsTrack(const QString& playlistId,
                                   const QString& trackId) const;
    Q_INVOKABLE QString nameForId(const QString& playlistId) const;
    Q_INVOKABLE QString idAt(int row) const;

    bool load();
    bool flush() const;

signals:
    void countChanged();
    void membershipChanged(const QString& playlistId);

private:
    struct Playlist {
        QString id;
        QString name;
        QSet<QString> trackIds;
    };

    [[nodiscard]] int rowForId(const QString& playlistId) const;
    [[nodiscard]] bool hasName(const QString& name, int exceptRow = -1) const;

    QString filePath_;
    QList<Playlist> playlists_;
};
