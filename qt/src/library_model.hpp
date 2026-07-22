#pragma once

#include <QAbstractListModel>
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
    bool available = false;
    QString importError;
};

QString canonicalLibraryPath(const QString& path);
QString trackIdForPath(const QString& path);

class LibraryModel : public QAbstractListModel {
    Q_OBJECT

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
        AvailableRole,
        ImportErrorRole
    };
    Q_ENUM(Role)

    explicit LibraryModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool append(TrackRecord track);
    void replaceAll(QList<TrackRecord> tracks);
    const QList<TrackRecord>& tracks() const noexcept;
    bool containsPath(const QString& path) const;

    Q_INVOKABLE bool setFavorite(int row, bool favorite);
    Q_INVOKABLE void playRow(int row);
    Q_INVOKABLE void flush();

signals:
    void playRequested(int row);
    void flushRequested();

private:
    QList<TrackRecord> tracks_;
    QSet<QString> pathKeys_;
};
