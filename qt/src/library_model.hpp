#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QSet>
#include <QUrl>

struct TrackRecord {
    QString trackId;
    QString path;
    QString title;
    QString artist;
    QString album;
    QString format;
    int sampleRate = 0;
    int bitDepth = 0;
    qint64 bitRate = 0;
    qint64 durationMs = 0;
    qint64 fileSize = 0;
    QUrl coverUrl;
    bool favorite = false;
    int rating = 0;
    double bpm = 0.0;
    bool available = false;
    QString importError;
    QString lyrics;
    int playCount = 0;
    qint64 lastPlayedAtMs = 0;
};

QString canonicalLibraryPath(const QString& path);
QString trackIdForPath(const QString& path);

class LibraryModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int favoriteCount READ favoriteCount NOTIFY favoriteCountChanged)
    Q_PROPERTY(int historyCount READ historyCount NOTIFY historyCountChanged)

public:
    enum Role {
        TrackIdRole = Qt::UserRole + 1,
        PathRole,
        TitleRole,
        ArtistRole,
        AlbumRole,
        FormatRole,
        SampleRateRole,
        BitDepthRole,
        BitRateRole,
        DurationMsRole,
        FileSizeRole,
        CoverUrlRole,
        FavoriteRole,
        RatingRole,
        BpmRole,
        AvailableRole,
        ImportErrorRole,
        LyricsRole,
        PlayCountRole,
        LastPlayedAtRole
    };
    Q_ENUM(Role)

    explicit LibraryModel(QObject* parent = nullptr);

    int count() const noexcept;
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool append(TrackRecord track);
    void replaceAll(QList<TrackRecord> tracks);
    const QList<TrackRecord>& tracks() const noexcept;
    bool containsPath(const QString& path) const;
    int indexForLocalFile(const QString& localFilePath) const;
    Q_INVOKABLE int indexForTrackId(const QString& trackId) const;

    Q_INVOKABLE bool setFavorite(int row, bool favorite);
    Q_INVOKABLE bool setRating(int row, int rating);
    bool markPlayed(const QString& trackId, qint64 playedAtMs = 0);
    Q_INVOKABLE void playRow(int row);
    Q_INVOKABLE void flush();

    int favoriteCount() const noexcept;
    int historyCount() const noexcept;

signals:
    void playRequested(int row);
    void flushRequested();
    void favoriteCountChanged();
    void historyCountChanged();
    void countChanged();

private:
    QList<TrackRecord> tracks_;
    QSet<QString> pathKeys_;
    QHash<QString, int> trackRows_;
};
