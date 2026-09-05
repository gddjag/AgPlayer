#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QUrl>

#include <optional>

struct TrackRecord {
    QString trackId;
    QString path;
    QString title;
    QString artist;
    QString album;
    QString albumArtist;
    QString genre;
    QString year;
    QString date;
    QString composer;
    QString format;
    int sampleRate = 0;
    int bitDepth = 0;
    int channels = 0;
    bool hasAudio = false;
    bool hasVideo = false;
    bool metadataProbeAttempted = false;
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
    QStringList tags;
    qint64 addedAtMs = 0;
    QString fileStatus = QStringLiteral("normal");
    QString contentHash;
    QByteArray audioFingerprint;
    bool replayGainScanned = false;
    double replayGainTrackDb = 0.0;
    double replayGainAlbumDb = 0.0;
    double replayPeak = 0.0;
};

struct MetadataProbeClaim {
    QString trackId;
    QString path;
    quint64 generation = 0;
};

struct LibraryMetadataRefresh {
    MetadataProbeClaim claim;
    TrackRecord record;
};

struct ag_metadata;
// Read-only snapshot construction. May perform cover-cache I/O; use a worker
// when the metadata handle was opened in the background.
TrackRecord readLibraryMetadata(const QString& path, const ag_metadata* metadata,
                               const QUrl& previousCover);

QString canonicalLibraryPath(const QString& path);
QString trackIdForPath(const QString& path);

class LibraryModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int favoriteCount READ favoriteCount NOTIFY favoriteCountChanged)
    Q_PROPERTY(int historyCount READ historyCount NOTIFY historyCountChanged)
    Q_PROPERTY(int recentAddedCount READ recentAddedCount
                   NOTIFY recentAddedCountChanged)
    Q_PROPERTY(int neverPlayedCount READ neverPlayedCount
                   NOTIFY neverPlayedCountChanged)

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
        LastPlayedAtRole,
        TagsRole,
        AddedAtRole,
        FileStatusRole,
        ContentHashRole,
        AudioFingerprintRole,
        ReplayGainScannedRole,
        ReplayGainTrackDbRole,
        ReplayGainAlbumDbRole,
        ReplayPeakRole,
        AlbumArtistRole,
        GenreRole,
        YearRole,
        DateRole,
        ComposerRole,
        ChannelsRole,
        HasAudioRole,
        HasVideoRole,
        MetadataProbeAttemptedRole
    };
    Q_ENUM(Role)

    explicit LibraryModel(QObject* parent = nullptr);

    int count() const noexcept;
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool append(TrackRecord track);
    QStringList appendBatch(QList<TrackRecord> tracks);
    QStringList insertBatch(int row, QList<TrackRecord> tracks);
    void replaceAll(QList<TrackRecord> tracks);
    const QList<TrackRecord>& tracks() const noexcept;
    const TrackRecord* recordForId(const QString& trackId) const noexcept;
    bool containsPath(const QString& path) const;
    int indexForLocalFile(const QString& localFilePath) const;
    Q_INVOKABLE int indexForTrackId(const QString& trackId) const;
    Q_INVOKABLE QVariantMap trackForId(const QString& trackId) const;
    Q_INVOKABLE bool removeTrack(const QString& trackId);
    Q_INVOKABLE QUrl containingFolderUrl(const QString& trackId) const;

    Q_INVOKABLE bool setFavorite(int row, bool favorite);
    Q_INVOKABLE bool setRating(int row, int rating);
    Q_INVOKABLE bool setBpm(const QString& trackId, double bpm);
    Q_INVOKABLE bool setTags(const QString& trackId, const QStringList& tags);
    Q_INVOKABLE int setTagsForTracks(const QStringList& trackIds,
                                     const QStringList& tags);
    Q_INVOKABLE int addTagToTracks(const QStringList& trackIds,
                                   const QString& tag);
    Q_INVOKABLE int removeTagFromTracks(const QStringList& trackIds,
                                        const QString& tag);
    Q_INVOKABLE int renameTag(const QString& oldKey, const QString& displayName);
    Q_INVOKABLE int removeTag(const QString& key);
    Q_INVOKABLE bool moveTrack(int fromRow, int toRow);
    Q_INVOKABLE int reorderTracks(const QStringList& trackIds,
                                  const QString& beforeTrackId);
    std::optional<MetadataProbeClaim> beginMetadataProbe(const QString& trackId);
    std::optional<MetadataProbeClaim> beginMetadataRefresh(const QString& trackId);
    int completeMetadataRefreshes(const QList<LibraryMetadataRefresh>& updates);
    bool completeMetadataProbe(const MetadataProbeClaim& claim, bool succeeded,
                               const TrackRecord& probed);
    bool completeMediaKindProbe(const MetadataProbeClaim& claim,
                                bool succeeded,
                                const TrackRecord& probed);
    bool abandonMetadataProbe(const MetadataProbeClaim& claim)
    {
        if (QThread::currentThread() != thread()) return false;
        const auto inFlight = metadataProbeInFlight_.constFind(claim.trackId);
        if (inFlight == metadataProbeInFlight_.cend()
            || inFlight.value() != claim.generation) {
            return false;
        }
        metadataProbeInFlight_.remove(claim.trackId);
        return true;
    }
    bool refreshMetadataForPath(const QString& path);
    int refreshMetadataForPaths(const QStringList& paths);
    bool applyReplayGainResult(const QString& trackId, double trackGainDb,
                               double albumGainDb, double peak);
    bool updateTrackPath(const QString& trackId, const QString& newPath);
    bool updateTrackPaths(const QHash<QString, QString>& paths);
    bool markPlayed(const QString& trackId, qint64 playedAtMs = 0);
    Q_INVOKABLE bool removeFromHistory(const QString& trackId);
    Q_INVOKABLE void playRow(int row);
    Q_INVOKABLE void flush();

    int favoriteCount() const noexcept;
    int historyCount() const noexcept;
    int recentAddedCount() const noexcept;
    int neverPlayedCount() const noexcept;

signals:
    void playRequested(int row);
    void flushRequested();
    void favoriteCountChanged();
    void historyCountChanged();
    void recentAddedCountChanged();
    void neverPlayedCountChanged();
    void countChanged();
    void trackRemoved(const QString& trackId);
    void tagsChanged(const QString& trackId, const QStringList& oldTags,
                     const QStringList& newTags);

private:
    bool applyTagsAtRow(int row, const QStringList& tags);
    QList<TrackRecord> tracks_;
    QSet<QString> pathKeys_;
    QHash<QString, int> pathRows_;
    QHash<QString, int> trackRows_;
    QHash<QString, quint64> metadataProbeInFlight_;
    // Zero is an exhaustion sentinel. Never wrap and reuse a live-process
    // generation: failing closed after 2^64-1 claims is safer than ABA reuse.
    quint64 nextMetadataProbeGeneration_ = 1;
};
