#include "lyrics_service.hpp"

#include "playback_controller.hpp"
#include "settings_controller.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QStandardPaths>

#include <algorithm>

namespace {

constexpr qint64 kDegradedWindowMs = 20 * 60 * 1000;
constexpr int kFailuresBeforeDegraded = 3;

QString lyricsCacheDirectory(SettingsController* settings)
{
    const QString base = settings != nullptr && !settings->cacheDirectory().isEmpty()
        ? settings->cacheDirectory()
        : QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return base;
}

LyricsProvider::Source localSource(const QString& providerId, const QString& providerName)
{
    return {providerId, providerName, {}, {}, true};
}

} // namespace

LyricsService::LyricsService(LibraryModel* library, PlaybackController* playback,
                             SettingsController* settings, LyricsProvider* provider,
                             QObject* parent)
    : QObject(parent)
    , library_(library)
    , playback_(playback)
    , settings_(settings)
    , lineModel_(this)
    , cache_(lyricsCacheDirectory(settings))
{
    if (provider != nullptr) {
        provider_ = provider;
    } else {
        networkManager_ = new QNetworkAccessManager(this);
        provider_ = new LrclibProvider(networkManager_, this);
    }
    connect(provider_, &LyricsProvider::finished, this, &LyricsService::onProviderFinished);
    if (playback_ != nullptr) {
        connect(playback_, &PlaybackController::currentTrackIdChanged,
                this, &LyricsService::requestCurrentTrack);
        connect(playback_, &PlaybackController::lyricsChanged,
                this, &LyricsService::requestCurrentTrack);
        connect(playback_, &PlaybackController::positionMsChanged,
                this, &LyricsService::updateCurrentLine);
    }
    if (settings_ != nullptr) {
        connect(settings_, &SettingsController::cacheDirectoryChanged, this, [this] {
            cache_.setCacheDirectory(lyricsCacheDirectory(settings_));
            if (enabled_) requestCurrentTrack();
        });
    }
}

LyricsService::~LyricsService() { cancelPending(); }

bool LyricsService::enabled() const noexcept { return enabled_; }
LyricsService::Status LyricsService::status() const noexcept { return status_; }
QObject* LyricsService::lines() noexcept { return &lineModel_; }
qint64 LyricsService::offsetMs() const noexcept { return userOffsetMs_; }
qint64 LyricsService::followPausedUntilMs() const noexcept { return followPausedUntilMs_; }
bool LyricsService::instrumental() const noexcept { return instrumental_; }
qint64 LyricsService::degradedUntilMs() const noexcept { return degradedUntilMs_; }
int LyricsService::consecutiveTechnicalFailures() const noexcept { return consecutiveTechnicalFailures_; }
qint64 LyricsService::clockMs() const { return QDateTime::currentMSecsSinceEpoch(); }

QString LyricsService::currentLine() const
{
    return playback_ == nullptr ? QString() : lineModel_.lineAt(
        playback_->positionMs(), documentOffsetMs_ + userOffsetMs_);
}
QString LyricsService::previousLine() const
{
    return playback_ == nullptr ? QString() : lineModel_.previousLine(
        playback_->positionMs(), documentOffsetMs_ + userOffsetMs_);
}
QString LyricsService::nextLine() const
{
    return playback_ == nullptr ? QString() : lineModel_.nextLine(
        playback_->positionMs(), documentOffsetMs_ + userOffsetMs_);
}

QVariantMap LyricsService::diagnostics() const
{
    // This boundary intentionally exports only aggregate provider health.  It
    // must never expose local paths, request URLs, metadata, or lyrics text.
    return {{QStringLiteral("provider"), source_},
            {QStringLiteral("source"), source_},
            {QStringLiteral("lastHttpStatus"), lastHttpStatus_},
            {QStringLiteral("failureCount"), consecutiveTechnicalFailures_},
            {QStringLiteral("degradedUntilMs"), degradedUntilMs_},
            {QStringLiteral("lastError"), lastDiagnostic_}};
}

void LyricsService::setEnabled(const bool enabled)
{
    if (enabled_ == enabled) return;
    enabled_ = enabled;
    emit enabledChanged();
    if (!enabled_) {
        cancelPending();
        setStatus(Idle);
        return;
    }
    requestCurrentTrack();
}

void LyricsService::setOffsetMs(const qint64 offsetMs)
{
    if (userOffsetMs_ == offsetMs) return;
    userOffsetMs_ = offsetMs;
    emit offsetMsChanged();
    updateCurrentLine();
}

void LyricsService::requestCurrentTrack()
{
    if (!enabled_ || playback_ == nullptr || library_ == nullptr) return;
    const QString trackId = playback_->currentTrackId();
    const TrackRecord* track = library_->recordForId(trackId);
    if (track == nullptr) {
        cancelPending();
        currentTrackId_.clear();
        lineModel_.clear();
        setStatus(Idle);
        return;
    }
    requestTrack(*track, playback_->lyrics());
    prefetchNext();
}

void LyricsService::requestTrack(const TrackRecord& track, const QString& embeddedLyrics)
{
    if (!enabled_) return;
    cancelPending();
    ++generation_;
    currentTrack_ = track;
    currentTrackId_ = track.trackId;
    instrumental_ = false;
    userOffsetMs_ = 0;
    documentOffsetMs_ = 0;
    lineModel_.clear();
    emit instrumentalChanged();
    emit offsetMsChanged();
    resolveLocal(track, embeddedLyrics);
}

void LyricsService::resolveLocal(const TrackRecord& track, const QString& embeddedLyrics)
{
    if (!embeddedLyrics.trimmed().isEmpty()) {
        const LyricsDocument document = LyricsLineModel::parseLrc(embeddedLyrics.toUtf8());
        if (hasUsableLyrics(document)) {
            applyDocument(track, {document,
                                  localSource(QStringLiteral("embedded"),
                                              QStringLiteral("Embedded")),
                                  false, !document.lines.isEmpty()});
            return;
        }
    }
    const QFileInfo audio(track.path);
    const QString sidecar = audio.dir().filePath(audio.completeBaseName() + QStringLiteral(".lrc"));
    QFile file(sidecar);
    if (file.open(QIODevice::ReadOnly)) {
        const LyricsDocument document = LyricsLineModel::parseLrc(file.readAll());
        if (!document.lines.isEmpty() || !document.untimedText.isEmpty()) {
            applyDocument(track, {document,
                                  localSource(QStringLiteral("sidecar"),
                                              QStringLiteral("Sidecar")),
                                  false, !document.lines.isEmpty()});
            return;
        }
    }
    if (const auto cached = cache_.load(track); cached.has_value()) {
        applyDocument(track, *cached);
        return;
    }
    if (clockMs() < degradedUntilMs_ || clockMs() < retryNotBeforeMs_) {
        setStatus(Offline);
        return;
    }
    beginExact(track);
}

bool LyricsService::hasLocalLyrics(const TrackRecord& track, const QString& embeddedLyrics) const
{
    if (!embeddedLyrics.trimmed().isEmpty()
        && hasUsableLyrics(LyricsLineModel::parseLrc(embeddedLyrics.toUtf8()))) {
        return true;
    }
    const QFileInfo audio(track.path);
    const QString sidecar = audio.dir().filePath(audio.completeBaseName() + QStringLiteral(".lrc"));
    QFile file(sidecar);
    if (file.open(QIODevice::ReadOnly) && hasUsableLyrics(LyricsLineModel::parseLrc(file.readAll()))) {
        return true;
    }
    return cache_.load(track).has_value();
}

LyricsProvider::Track LyricsService::providerTrack(const TrackRecord& track,
                                                   const bool lowPriority) const
{
    return {track.title, track.artist, track.album, track.durationMs, lowPriority};
}

void LyricsService::beginExact(const TrackRecord& track, const bool prefetch)
{
    if (track.title.trimmed().isEmpty()) { if (!prefetch) setStatus(NotFound); return; }
    if (provider_.isNull()) { if (!prefetch) setStatus(Error); return; }
    const quint64 requestId = nextRequestId_++;
    pending_.insert(requestId, {track, Exact, generation_, prefetch});
    if (!prefetch) setStatus(Loading);
    provider_->requestExact(requestId, providerTrack(track, prefetch));
}

void LyricsService::beginSearch(const TrackRecord& track, const bool prefetch)
{
    if (provider_.isNull()) { if (!prefetch) setStatus(Error); return; }
    const quint64 requestId = nextRequestId_++;
    pending_.insert(requestId, {track, Search, generation_, prefetch});
    if (!prefetch) setStatus(Loading);
    provider_->requestSearch(requestId, providerTrack(track, prefetch));
}

void LyricsService::prefetchNext()
{
    if (!enabled_ || playback_ == nullptr || library_ == nullptr || pending_.size() >= 2) return;
    const QStringList queue = playback_->queueTrackIds();
    const qsizetype index = queue.indexOf(currentTrackId_);
    if (index < 0 || index + 1 >= queue.size()) return;
    const TrackRecord* next = library_->recordForId(queue.at(index + 1));
    if (next == nullptr || next->title.trimmed().isEmpty()
        || hasLocalLyrics(*next, next->lyrics)) return;
    beginExact(*next, true);
}

void LyricsService::onProviderFinished(const quint64 requestId,
                                       const LyricsProvider::Result& result)
{
    const auto iterator = pending_.find(requestId);
    if (iterator == pending_.end()) return;
    const Pending pending = *iterator;
    pending_.erase(iterator);
    if (pending.generation != generation_
        || (!pending.prefetch && pending.track.trackId != currentTrackId_)) return;

    if (result.kind == LyricsProvider::Result::NotFound) {
        if (pending.stage == Exact) { beginSearch(pending.track, pending.prefetch); return; }
        consecutiveTechnicalFailures_ = 0;
        lastHttpStatus_ = 0;
        lastDiagnostic_.clear();
        emit diagnosticsChanged();
        if (!pending.prefetch) setStatus(NotFound);
        return;
    }
    if (result.kind == LyricsProvider::Result::RateLimited) {
        retryNotBeforeMs_ = std::max(retryNotBeforeMs_, clockMs() + std::max<qint64>(0, result.retryAfterMs));
        lastHttpStatus_ = 429;
        lastDiagnostic_ = QStringLiteral("rate-limited");
        if (!pending.prefetch) setStatus(Offline);
        emit diagnosticsChanged();
        return;
    }
    if (result.kind == LyricsProvider::Result::TechnicalError) {
        recordTechnicalFailure(result, !pending.prefetch);
        return;
    }

    std::optional<LyricsProvider::Candidate> candidate;
    if (result.kind == LyricsProvider::Result::Found) candidate = result.candidate;
    if (result.kind == LyricsProvider::Result::SearchResults) {
        candidate = bestCandidate(pending.track, result.candidates);
    }
    if (!candidate.has_value()) {
        consecutiveTechnicalFailures_ = 0;
        lastHttpStatus_ = 0;
        lastDiagnostic_.clear();
        emit diagnosticsChanged();
        if (!pending.prefetch) setStatus(NotFound);
        return;
    }

    consecutiveTechnicalFailures_ = 0;
    lastHttpStatus_ = result.httpStatus;
    lastDiagnostic_.clear();
    const QString rawLyrics = !candidate->syncedLyrics.trimmed().isEmpty()
        ? candidate->syncedLyrics : candidate->plainLyrics;
    LyricsDocument document = LyricsLineModel::parseLrc(rawLyrics.toUtf8());
    if (candidate->instrumental) {
        document = {};
    } else if (document.lines.isEmpty() && document.untimedText.isEmpty()) {
        consecutiveTechnicalFailures_ = 0;
        lastHttpStatus_ = 0;
        lastDiagnostic_.clear();
        emit diagnosticsChanged();
        if (!pending.prefetch) setStatus(NotFound);
        return;
    }
    if (pending.prefetch) {
        static_cast<void>(cache_.save(pending.track,
            {document, candidate->source, candidate->instrumental, !document.lines.isEmpty()}));
        return;
    }
    applyDocument(pending.track,
                  {document, candidate->source, candidate->instrumental,
                   !document.lines.isEmpty()});
}

void LyricsService::applyDocument(const TrackRecord& track, LyricsCache::Entry entry)
{
    if (entry.source.providerId != QStringLiteral("embedded")
        && entry.source.providerId != QStringLiteral("sidecar")) {
        static_cast<void>(cache_.save(track, entry));
    }
    documentOffsetMs_ = entry.document.offsetMs;
    instrumental_ = entry.instrumental;
    source_ = std::move(entry.source.providerId);
    lineModel_.setLines(entry.document.lines);
    setStatus(Ready);
    emit instrumentalChanged();
    emit currentLineChanged();
    emit diagnosticsChanged();
}

void LyricsService::updateCurrentLine()
{
    if (enabled_) emit currentLineChanged();
}

void LyricsService::setStatus(const Status status)
{
    if (status_ == status) return;
    status_ = status;
    emit statusChanged();
}

void LyricsService::cancelPending()
{
    if (!provider_.isNull()) {
        for (auto it = pending_.cbegin(); it != pending_.cend(); ++it) provider_->cancel(it.key());
    }
    pending_.clear();
}

void LyricsService::recordTechnicalFailure(const LyricsProvider::Result& result,
                                           const bool updateStatus)
{
    ++consecutiveTechnicalFailures_;
    lastHttpStatus_ = result.httpStatus;
    lastDiagnostic_ = result.offline ? QStringLiteral("network-unavailable")
                                     : QStringLiteral("provider-error");
    if (consecutiveTechnicalFailures_ >= kFailuresBeforeDegraded) {
        degradedUntilMs_ = clockMs() + kDegradedWindowMs;
        if (updateStatus) setStatus(Offline);
    } else if (updateStatus) {
        setStatus(result.offline ? Offline : Error);
    }
    emit diagnosticsChanged();
}

std::optional<LyricsProvider::Candidate> LyricsService::bestCandidate(
    const TrackRecord& track, const QList<LyricsProvider::Candidate>& candidates) const
{
    const QString title = normalizedMatch(track.title);
    const QString artist = normalizedMatch(track.artist);
    const QString album = normalizedMatch(track.album);
    int bestScore = -1;
    std::optional<LyricsProvider::Candidate> best;
    for (const LyricsProvider::Candidate& candidate : candidates) {
        const QString candidateTitle = normalizedMatch(candidate.title);
        const QString candidateArtist = normalizedMatch(candidate.artist);
        if ((!title.isEmpty() && candidateTitle != title)
            || (!artist.isEmpty() && candidateArtist != artist)) continue;
        if (track.durationMs > 0 && candidate.durationSeconds > 0
            && std::llabs(track.durationMs / 1000 - candidate.durationSeconds) > 3) continue;
        int score = 200;
        if (!album.isEmpty() && normalizedMatch(candidate.album) == album) score += 30;
        if (track.durationMs > 0 && candidate.durationSeconds > 0
            && std::llabs(track.durationMs / 1000 - candidate.durationSeconds) <= 3) score += 20;
        if (!candidate.syncedLyrics.trimmed().isEmpty()) score += 4;
        if (score > bestScore) { bestScore = score; best = candidate; }
    }
    return best;
}

bool LyricsService::hasUsableLyrics(const LyricsDocument& document)
{
    return !document.lines.isEmpty() || !document.untimedText.isEmpty();
}

QString LyricsService::normalizedMatch(const QString& value)
{
    QString result;
    for (const QChar character : value.toCaseFolded()) {
        if (character.isLetterOrNumber()) result.append(character);
    }
    return result;
}

void LyricsService::retry()
{
    if (!enabled_ || currentTrackId_.isEmpty()) return;
    if (clockMs() < degradedUntilMs_ || clockMs() < retryNotBeforeMs_) {
        setStatus(Offline);
        return;
    }
    cancelPending();
    beginExact(currentTrack_);
}

bool LyricsService::importLrc(const QUrl& fileUrl)
{
    if (!enabled_ || currentTrackId_.isEmpty() || !fileUrl.isLocalFile()) return false;
    QFile file(fileUrl.toLocalFile());
    if (!file.open(QIODevice::ReadOnly)) return false;
    const LyricsDocument document = LyricsLineModel::parseLrc(file.readAll());
    if (document.lines.isEmpty() && document.untimedText.isEmpty()) return false;
    cancelPending();
    applyDocument(currentTrack_,
                  {document,
                   localSource(QStringLiteral("manual"), QStringLiteral("Manual")),
                   false, !document.lines.isEmpty()});
    return true;
}

void LyricsService::pauseFollow(const qint64 milliseconds)
{
    followPausedUntilMs_ = clockMs() + std::max<qint64>(0, milliseconds);
    emit followPausedChanged();
}
