#pragma once

#include "library_model.hpp"
#include "playlist_model.hpp"

#include <QHash>
#include <QPointer>
#include <QSortFilterProxyModel>
#include <QString>

class LibraryFilterModel : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* sourceModel READ sourceModel WRITE setSourceModel NOTIFY
                   sourceModelChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    Q_PROPERTY(int exactRating READ exactRating WRITE setExactRating NOTIFY exactRatingChanged)
    Q_PROPERTY(double minBpm READ minBpm WRITE setMinBpm NOTIFY minBpmChanged)
    Q_PROPERTY(double maxBpm READ maxBpm WRITE setMaxBpm NOTIFY maxBpmChanged)
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY categoryChanged)
    Q_PROPERTY(QString tagKey READ tagKey WRITE setTagKey NOTIFY tagKeyChanged)
    Q_PROPERTY(QString resourceFolder READ resourceFolder WRITE setResourceFolder NOTIFY resourceFolderChanged)
    Q_PROPERTY(PlaylistModel* playlistModel READ playlistModel WRITE setPlaylistModel NOTIFY
                   playlistModelChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit LibraryFilterModel(QObject* parent = nullptr);

    void setSourceModel(QAbstractItemModel* sourceModel) override;

    QString searchText() const noexcept;
    void setSearchText(const QString& text);

    int exactRating() const noexcept;
    void setExactRating(int rating);

    double minBpm() const noexcept;
    void setMinBpm(double bpm);

    double maxBpm() const noexcept;
    void setMaxBpm(double bpm);

    QString category() const noexcept;
    void setCategory(const QString& category);

    QString tagKey() const noexcept;
    void setTagKey(const QString& key);
    QString resourceFolder() const noexcept;
    void setResourceFolder(const QString& folder);

    PlaylistModel* playlistModel() const noexcept;
    void setPlaylistModel(PlaylistModel* playlistModel);

    int count() const;

    Q_INVOKABLE int sourceRow(int proxyRow) const;
    Q_INVOKABLE void playSourceRow(int proxyRow);
    Q_INVOKABLE bool setFavorite(int proxyRow, bool favorite);
    Q_INVOKABLE bool setRating(int proxyRow, int rating);

signals:
    void sourceModelChanged();
    void searchTextChanged();
    void exactRatingChanged();
    void minBpmChanged();
    void maxBpmChanged();
    void categoryChanged();
    void tagKeyChanged();
    void resourceFolderChanged();
    void playlistModelChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    bool lessThan(const QModelIndex& sourceLeft,
                  const QModelIndex& sourceRight) const override;

private:
    bool rowMatchesCategory(int sourceRow) const;
    bool rowMatchesSearch(int sourceRow) const;
    bool rowMatchesRating(int sourceRow) const;
    bool rowMatchesBpm(int sourceRow) const;
    bool rowMatchesTag(int sourceRow) const;
    bool rowMatchesResourceFolder(int sourceRow) const;
    QModelIndex sourceIndexForRow(int sourceRow) const;
    void rebuildPlaylistRanks();

    QString searchText_;
    QString foldedSearchText_;
    int exactRating_ = 0;
    double minBpm_ = 60.0;
    double maxBpm_ = 160.0;
    QString category_ = QStringLiteral("all");
    QString tagKey_;
    QString resourceFolder_;
    QPointer<PlaylistModel> playlistModel_;
    QHash<QString, int> playlistRanks_;
};
