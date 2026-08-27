#pragma once

#include "library_model.hpp"
#include "lyrics_cache.hpp"
#include "lyrics_line_model.hpp"
#include "lyrics_provider.hpp"

#include <QHash>
#include <QObject>

class PlaybackController;
class SettingsController;
class QNetworkAccessManager;

class LyricsService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(QObject* lines READ lines CONSTANT)
    Q_PROPERTY(QString currentLine READ currentLine NOTIFY currentLineChanged)
    Q_PROPERTY(QString previousLine READ previousLine NOTIFY currentLineChanged)
    Q_PROPERTY(QString nextLine READ nextLine NOTIFY currentLineChanged)
    Q_PROPERTY(qint64 offsetMs READ offsetMs WRITE setOffsetMs NOTIFY offsetMsChanged)
    Q_PROPERTY(qint64 followPausedUntilMs READ followPausedUntilMs NOTIFY followPausedChanged)
    Q_PROPERTY(bool instrumental READ instrumental NOTIFY instrumentalChanged)
    Q_PROPERTY(QVariantMap diagnostics READ diagnostics NOTIFY diagnosticsChanged)
    Q_PROPERTY(qint64 degradedUntilMs READ degradedUntilMs NOTIFY diagnosticsChanged)

public:
    enum Status { Idle, Loading, Ready, NotFound, Offline, Error };
    Q_ENUM(Status)

    explicit LyricsService(LibraryModel* library, PlaybackController* playback,
                           SettingsController* settings,
                           LyricsProvider* provider = nullptr,
                           QObject* parent = nullptr);
    ~LyricsService() override;

    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] Status status() const noexcept;
    [[nodiscard]] QObject* lines() noexcept;
    [[nodiscard]] QString currentLine() const;
    [[nodiscard]] QString previousLine() const;
    [[nodiscard]] QString nextLine() const;
    [[nodiscard]] qint64 offsetMs() const noexcept;
    [[nodiscard]] qint64 followPausedUntilMs() const noexcept;
    [[nodiscard]] bool instrumental() const noexcept;
    [[nodiscard]] QVariantMap diagnostics() const;
    [[nodiscard]] qint64 degradedUntilMs() const noexcept;
    [[nodiscard]] int consecutiveTechnicalFailures() const noexcept;
    [[nodiscard]] qint64 clockMs() const;

    void setEnabled(bool enabled);
    void setOffsetMs(qint64 offsetMs);
    void requestTrack(const TrackRecord& track, const QString& embeddedLyrics = {});

    Q_INVOKABLE void retry();
    Q_INVOKABLE bool importLrc(const QUrl& fileUrl);
    Q_INVOKABLE void pauseFollow(qint64 milliseconds = 5000);

signals:
    void enabledChanged();
    void statusChanged();
    void currentLineChanged();
    void offsetMsChanged();
    void followPausedChanged();
    void instrumentalChanged();
    void diagnosticsChanged();

private:
    enum Stage { Exact, Search };
    struct Pending final {
        TrackRecord track;
        Stage stage = Exact;
        quint64 generation = 0;
        bool prefetch = false;
    };

    void requestCurrentTrack();
    void resolveLocal(const TrackRecord& track, const QString& embeddedLyrics);
    void beginExact(const TrackRecord& track, bool prefetch = false);
    void beginSearch(const TrackRecord& track, bool prefetch = false);
    void prefetchNext();
    void onProviderFinished(quint64 requestId, const LyricsProvider::Result& result);
    void applyDocument(const TrackRecord& track, LyricsCache::Entry entry);
    void updateCurrentLine();
    void setStatus(Status status);
    void cancelPending();
    void recordTechnicalFailure(const LyricsProvider::Result& result, bool updateStatus = true);
    [[nodiscard]] LyricsProvider::Track providerTrack(const TrackRecord& track,
                                                       bool lowPriority = false) const;
    [[nodiscard]] std::optional<LyricsProvider::Candidate> bestCandidate(
        const TrackRecord& track, const QList<LyricsProvider::Candidate>& candidates) const;
    [[nodiscard]] static QString normalizedMatch(const QString& value);

    LibraryModel* library_ = nullptr;
    PlaybackController* playback_ = nullptr;
    SettingsController* settings_ = nullptr;
    QNetworkAccessManager* networkManager_ = nullptr;
    LyricsProvider* provider_ = nullptr;
    LyricsLineModel lineModel_;
    LyricsCache cache_;
    TrackRecord currentTrack_;
    QString currentTrackId_;
    QHash<quint64, Pending> pending_;
    quint64 nextRequestId_ = 1;
    quint64 generation_ = 0;
    bool enabled_ = false;
    Status status_ = Idle;
    qint64 userOffsetMs_ = 0;
    qint64 documentOffsetMs_ = 0;
    qint64 followPausedUntilMs_ = 0;
    qint64 degradedUntilMs_ = 0;
    qint64 retryNotBeforeMs_ = 0;
    int consecutiveTechnicalFailures_ = 0;
    int lastHttpStatus_ = 0;
    bool instrumental_ = false;
    QString source_;
    QString lastDiagnostic_;
};
