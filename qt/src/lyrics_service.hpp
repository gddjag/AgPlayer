#pragma once

#include "library_model.hpp"
#include "lyrics_cache.hpp"
#include "lyrics_line_model.hpp"
#include "lyrics_provider.hpp"
#include "lyrics_provider_chain.hpp"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>

class PlaybackController;
class SettingsController;
class QNetworkAccessManager;

class LyricsService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(QObject* lines READ lines CONSTANT)
    Q_PROPERTY(QString currentLine READ currentLine NOTIFY currentLineChanged)
    Q_PROPERTY(int currentLineIndex READ currentLineIndex NOTIFY currentLineChanged)
    Q_PROPERTY(QString previousLine READ previousLine NOTIFY currentLineChanged)
    Q_PROPERTY(QString nextLine READ nextLine NOTIFY currentLineChanged)
    Q_PROPERTY(qint64 offsetMs READ offsetMs WRITE setOffsetMs NOTIFY offsetMsChanged)
    Q_PROPERTY(qint64 followPausedUntilMs READ followPausedUntilMs NOTIFY followPausedChanged)
    Q_PROPERTY(bool instrumental READ instrumental NOTIFY instrumentalChanged)
    Q_PROPERTY(QVariantMap diagnostics READ diagnostics NOTIFY diagnosticsChanged)
    Q_PROPERTY(QString sourceProvider READ sourceProvider NOTIFY sourceChanged)
    Q_PROPERTY(QString sourceAttribution READ sourceAttribution NOTIFY sourceChanged)
    Q_PROPERTY(bool synchronizedLyrics READ synchronizedLyrics NOTIFY sourceChanged)
    Q_PROPERTY(QString untimedLyrics READ untimedLyrics NOTIFY sourceChanged)
    Q_PROPERTY(QVariantMap routeNotice READ routeNotice NOTIFY routeNoticeChanged)
    Q_PROPERTY(QVariantList routeAttempts READ routeAttempts NOTIFY routeAttemptsChanged)

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
    [[nodiscard]] int currentLineIndex() const;
    [[nodiscard]] QString previousLine() const;
    [[nodiscard]] QString nextLine() const;
    [[nodiscard]] qint64 offsetMs() const noexcept;
    [[nodiscard]] qint64 followPausedUntilMs() const noexcept;
    [[nodiscard]] bool instrumental() const noexcept;
    [[nodiscard]] QVariantMap diagnostics() const;
    [[nodiscard]] QString sourceProvider() const;
    [[nodiscard]] QString sourceAttribution() const;
    [[nodiscard]] bool synchronizedLyrics() const noexcept;
    [[nodiscard]] QString untimedLyrics() const;
    [[nodiscard]] QVariantMap routeNotice() const;
    [[nodiscard]] QVariantList routeAttempts() const;
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
    void sourceChanged();
    void routeNoticeChanged();
    void routeAttemptsChanged();

private:
    enum Stage { Exact, Search };
    struct Pending final {
        TrackRecord track;
        Stage stage = Exact;
        quint64 generation = 0;
        bool prefetch = false;
        QList<LyricsProvider::RouteAttempt> carriedAttempts;
    };

    void requestCurrentTrack();
    void resolveLocal(const TrackRecord& track, const QString& embeddedLyrics);
    [[nodiscard]] bool hasLocalLyrics(const TrackRecord& track,
                                      const QString& embeddedLyrics) const;
    void beginExact(const TrackRecord& track, bool prefetch = false);
    void beginSearch(const TrackRecord& track, bool prefetch = false,
                     QList<LyricsProvider::RouteAttempt> carriedAttempts = {});
    void prefetchNext();
    void onProviderFinished(quint64 requestId, const LyricsProvider::Result& result);
    void onRouteFailed(quint64 requestId, const LyricsProvider::RouteAttempt& attempt);
    void applyDocument(const TrackRecord& track, LyricsCache::Entry entry);
    void updateCurrentLine();
    void setStatus(Status status);
    void cancelPending();
    [[nodiscard]] LyricsProvider::Track providerTrack(const TrackRecord& track,
                                                       bool lowPriority = false) const;
    [[nodiscard]] std::optional<LyricsProvider::Candidate> bestCandidate(
        const LyricsProvider::Track& track,
        const QList<LyricsProvider::Candidate>& candidates) const;
    [[nodiscard]] static QString normalizedMatch(const QString& value);
    [[nodiscard]] static bool hasUsableLyrics(const LyricsDocument& document);
    void resetPresentationState();
    void setRouteAttempts(const QList<LyricsProvider::RouteAttempt>& attempts);
    [[nodiscard]] static QVariantMap safeAttempt(const LyricsProvider::RouteAttempt& attempt);

    LibraryModel* library_ = nullptr;
    PlaybackController* playback_ = nullptr;
    SettingsController* settings_ = nullptr;
    QNetworkAccessManager* networkManager_ = nullptr;
    QPointer<LyricsProvider> provider_;
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
    int lastPublishedLineIndex_ = -1;
    qint64 followPausedUntilMs_ = 0;
    int lastHttpStatus_ = 0;
    bool instrumental_ = false;
    LyricsProvider::Source sourceInfo_;
    bool synchronizedLyrics_ = false;
    QString untimedLyrics_;
    QVariantMap routeNotice_;
    QVariantList routeAttempts_;
    quint64 routeNoticeToken_ = 0;
    QString lastDiagnostic_;
};
