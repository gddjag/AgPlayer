#include "import_controller.hpp"

#include "audio_file_discovery.hpp"
#include "bpm_analyzer.hpp"
#include "metadata_probe.hpp"
#include "metadata_text.hpp"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>

#include <atomic>
#include <condition_variable>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <utility>

struct ImportCallbackState {
    std::mutex mutex;
    ImportController* controller = nullptr;
    // Shared ownership so the background task can read the cancelled flag
    // safely after the ImportController is destroyed (the destructor does
    // not wait for the task — see ~ImportController).
    std::shared_ptr<std::atomic_bool> cancelled;
};

namespace {
const QUrl kBrandCover(
    QStringLiteral("qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png"));

using agplayer::qt::decodeMetadataText;

QString errorFor(ag_result result)
{
    switch (result) {
    case AG_INVALID_ARGUMENT:
        return QStringLiteral("invalid argument");
    case AG_IO_ERROR:
        return QStringLiteral("I/O error");
    case AG_UNSUPPORTED_FORMAT:
        return QStringLiteral("unsupported format");
    case AG_DECODE_ERROR:
        return QStringLiteral("decode failed");
    case AG_DEVICE_ERROR:
        return QStringLiteral("device error");
    case AG_CANCELLED:
        return QStringLiteral("cancelled");
    case AG_INTERNAL_ERROR:
        return QStringLiteral("internal error");
    case AG_OK:
        return {};
    }
    return QStringLiteral("unknown error");
}

QString coverSuffix(const QString& mimeType)
{
    if (mimeType.compare(QStringLiteral("image/jpeg"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral(".jpg");
    }
    if (mimeType.compare(QStringLiteral("image/png"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral(".png");
    }
    if (mimeType.compare(QStringLiteral("image/webp"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral(".webp");
    }
    return QStringLiteral(".bin");
}

QUrl cacheCover(const QByteArray& bytes, const QString& mimeType)
{
    if (bytes.isEmpty()) {
        return kBrandCover;
    }
    const QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString coverDirectory = QDir(cacheRoot).filePath(QStringLiteral("covers"));
    if (cacheRoot.isEmpty() || !QDir().mkpath(coverDirectory)) {
        return kBrandCover;
    }
    const QString digest = QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    const QString coverPath = QDir(coverDirectory).filePath(digest + coverSuffix(mimeType));
    if (!QFileInfo::exists(coverPath)) {
        QSaveFile file(coverPath);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()
            || !file.commit()) {
            return kBrandCover;
        }
    }
    return QUrl::fromLocalFile(coverPath);
}

QString deduplicationKey(const QString& path)
{
    const QString canonical = canonicalLibraryPath(path);
#ifdef Q_OS_WIN
    return canonical.toCaseFolded();
#else
    return canonical;
#endif
}

template<typename Function>
void postToController(const std::shared_ptr<ImportCallbackState>& state, Function function)
{
    std::lock_guard<std::mutex> lock(state->mutex);
    ImportController* const controller = state->controller;
    if (controller == nullptr) {
        return;
    }
    QMetaObject::invokeMethod(
        controller,
        [controller, function = std::move(function)]() mutable { function(controller); },
        Qt::QueuedConnection);
}

bool isCancelled(const std::shared_ptr<ImportCallbackState>& state)
{
    return state->cancelled != nullptr
           && state->cancelled->load(std::memory_order_relaxed);
}

void markCancelled(const std::shared_ptr<ImportCallbackState>& state, bool value)
{
    if (state->cancelled != nullptr) {
        state->cancelled->store(value, std::memory_order_release);
    }
}
}

double readEmbeddedBpmTag(const QString& requestedPath)
{
    const QByteArray path = canonicalLibraryPath(requestedPath).toUtf8();
    ag_metadata* metadata = nullptr;
    if (ag_metadata_open(path.constData(), &metadata) != AG_OK
        || metadata == nullptr) {
        return 0.0;
    }
    const QString text = decodeMetadataText(ag_metadata_bpm_tag(metadata)).trimmed();
    ag_metadata_destroy(metadata);
    bool ok = false;
    const double value = text.toDouble(&ok);
    return ok && std::isfinite(value) && value >= 20.0 && value <= 400.0
        ? value : 0.0;
}

ProbeResult probeMetadata(const QString& requestedPath, bool analyzeBpm)
{
    const QString path = canonicalLibraryPath(requestedPath);
    const QByteArray utf8Path = path.toUtf8();
    ag_metadata* metadata = nullptr;
    const ag_result result = ag_metadata_open(utf8Path.constData(), &metadata);
    if (result != AG_OK || metadata == nullptr) {
        return {result, {}, errorFor(result)};
    }

    TrackRecord track;
    track.path = path;
    track.title = decodeMetadataText(ag_metadata_title(metadata));
    track.artist = decodeMetadataText(ag_metadata_artist(metadata));
    track.album = decodeMetadataText(ag_metadata_album(metadata));
    track.albumArtist = decodeMetadataText(ag_metadata_album_artist(metadata));
    track.genre = decodeMetadataText(ag_metadata_genre(metadata));
    track.year = decodeMetadataText(ag_metadata_year(metadata));
    track.date = decodeMetadataText(ag_metadata_date(metadata));
    track.composer = decodeMetadataText(ag_metadata_composer(metadata));
    track.lyrics = decodeMetadataText(ag_metadata_lyrics(metadata));
    track.format = decodeMetadataText(ag_metadata_format(metadata));
    track.sampleRate = ag_metadata_sample_rate(metadata);
    track.bitDepth = ag_metadata_bits_per_sample(metadata);
    track.channels = ag_metadata_channels(metadata);
    track.hasAudio = ag_metadata_has_audio(metadata);
    track.hasVideo = ag_metadata_has_video(metadata);
    track.bitRate = ag_metadata_bit_rate(metadata);
    track.durationMs = ag_metadata_duration_ms(metadata);
    const QString embeddedBpm = decodeMetadataText(ag_metadata_bpm_tag(metadata)).trimmed();
    bool embeddedBpmOk = false;
    const double embeddedBpmValue = embeddedBpm.toDouble(&embeddedBpmOk);
    size_t coverSize = 0;
    const char* coverMime = nullptr;
    const unsigned char* coverData = ag_metadata_cover(metadata, &coverSize, &coverMime);
    const QString mimeType = decodeMetadataText(coverMime);
    const QByteArray coverBytes = coverData == nullptr
        ? QByteArray{}
        : QByteArray(reinterpret_cast<const char*>(coverData), static_cast<qsizetype>(coverSize));
    ag_metadata_destroy(metadata);

    const QFileInfo file(path);
    if (track.title.isEmpty()) {
        track.title = file.completeBaseName();
    }
    if (track.format.isEmpty()) {
        track.format = file.suffix().toLower();
    }
    track.fileSize = file.size();
    track.available = file.isFile();
    track.trackId = trackIdForPath(path);
    track.coverUrl = cacheCover(coverBytes, mimeType);
    if (embeddedBpmOk && std::isfinite(embeddedBpmValue)
        && embeddedBpmValue >= 20.0 && embeddedBpmValue <= 400.0) {
        track.bpm = embeddedBpmValue;
    } else if (analyzeBpm) {
        const BpmAnalyzeResult bpm = analyze_bpm(path);
        track.bpm = bpm.bpm;
    }
    return {AG_OK, std::move(track), {}};
}

ImportController::ImportController(LibraryModel* model, QObject* parent)
    : ImportController(model, [](const QString& path) {
          return probeMetadata(path, false);
      }, parent)
{
}

ImportController::ImportController(LibraryModel* model, ProbeFunction probe, QObject* parent)
    : ImportController(
          model, std::move(probe),
          [](const QList<QUrl>& urls) {
              QStringList paths;
              const QList<QUrl> expanded = agplayer::qt::expandAudioUrls(urls);
              paths.reserve(expanded.size() + urls.size());
              for (const QUrl& url : expanded) {
                  paths.append(url.toLocalFile());
              }
              for (const QUrl& url : urls) {
                  const QString path = url.toLocalFile();
                  const QFileInfo info(path);
                  // Directly selected/dropped files must be probed even when
                  // their suffix is missing or wrong, so the UI can report a
                  // concrete format error instead of silently ignoring them.
                  // Directory expansion remains extension-filtered.
                  if (!path.isEmpty() && !info.isDir()) {
                      paths.append(path);
                  }
              }
              return paths;
          },
          parent)
{
}

ImportController::ImportController(LibraryModel* model, ProbeFunction probe,
                                   DiscoveryFunction discovery, QObject* parent)
    : QObject(parent),
      model_(model),
      probe_(std::move(probe)),
      discovery_(std::move(discovery)),
      callbackState_(std::make_shared<ImportCallbackState>())
{
    callbackState_->controller = this;
    callbackState_->cancelled = std::make_shared<std::atomic_bool>(false);
}

ImportController::~ImportController()
{
    // Mark the background task as cancelled so it exits gracefully on the
    // next iteration.  We must NOT call cancel() (which waits for the
    // future) — the probe might be blocked (e.g. on slow I/O or a test
    // semaphore), and waiting would deadlock.  The future's lambda holds a
    // shared_ptr to callbackState_, so it can safely check the cancelled
    // flag and post no-ops (controller == nullptr) after we're gone.
    // QObject's destructor drops any still-queued invokeMethod events for
    // this object, so the raw pointer captured by postToController never
    // fires into a destroyed receiver.
    markCancelled(callbackState_, true);
    std::lock_guard<std::mutex> lock(callbackState_->mutex);
    callbackState_->controller = nullptr;
}

void ImportController::cancel()
{
    // Non-blocking: only set the cancelled flag and signal completion. We must
    // NOT waitForFinished() here — the probe (FFmpeg metadata open) may be
    // blocked on slow/unresponsive I/O, and shutdown calls cancel() first, so
    // waiting would stall the entire shutdown. The background task checks the
    // cancelled flag on its next iteration and exits gracefully. A late
    // handleResult is harmless: the model append is idempotent
    // (containsPath check) and handleResult guards the finished emission on
    // busy_ to avoid double delivery.
    markCancelled(callbackState_, true);
    pendingUrls_.clear();
    if (busy_) {
        busy_ = false;
        emit busyChanged();
        emit finished();
    }
}

double ImportController::progress() const noexcept
{
    return progress_;
}

bool ImportController::busy() const noexcept
{
    return busy_;
}

QStringList ImportController::errors() const
{
    return errors_;
}

QStringList ImportController::importedTrackIds() const
{
    return importedTrackIds_;
}

int ImportController::skippedCount() const noexcept
{
    return skippedCount_;
}

void ImportController::importFolder(const QUrl& folder)
{
    importUrls({folder});
}

void ImportController::importUrls(const QList<QUrl>& urls)
{
    if (busy_) {
        for (const QUrl& url : urls) {
            if (url.isValid() && !pendingUrls_.contains(url)) {
                pendingUrls_.append(url);
            }
        }
        return;
    }

    markCancelled(callbackState_, false);
    errors_.clear();
    emit errorsChanged();
    importedTrackIds_.clear();
    importedTrackIdSet_.clear();
    emit importedTrackIdsChanged();
    if (skippedCount_ != 0) {
        skippedCount_ = 0;
        emit skippedCountChanged();
    }
    progress_ = 0.0;
    emit progressChanged();
    busy_ = true;
    emit busyChanged();

    if (model_.isNull()) {
        finishWithoutImport(QStringLiteral("library model is unavailable"));
        return;
    }
    if (model_->thread() != thread()) {
        finishWithoutImport(QStringLiteral("library model thread affinity mismatch"));
        return;
    }

    const std::shared_ptr<ImportCallbackState> callbackState = callbackState_;
    const ProbeFunction probe = probe_;
    const DiscoveryFunction discovery = discovery_;
    QHash<QString, TrackRecord> knownTracks;
    knownTracks.reserve(model_->count());
    for (const TrackRecord& track : model_->tracks()) {
#ifdef Q_OS_WIN
        knownTracks.insert(track.path.toCaseFolded(), track);
#else
        knownTracks.insert(track.path, track);
#endif
    }
    future_ = QtConcurrent::run([callbackState, urls, probe, discovery, knownTracks] {
        QStringList discoveredPaths = discovery(urls);
        QSet<QString> seen;
        QStringList paths;
        paths.reserve(discoveredPaths.size());
        for (const QString& candidate : discoveredPaths) {
            const QString canonical = canonicalLibraryPath(candidate);
            const QString key = deduplicationKey(canonical);
            if (!canonical.isEmpty() && !seen.contains(key)) {
                seen.insert(key);
                paths.append(canonical);
            }
        }
        if (paths.isEmpty()) {
            postToController(callbackState, [](ImportController* controller) {
                controller->completeImport();
            });
            return;
        }

        struct PipelineState {
            std::mutex mutex;
            std::condition_variable readyChanged;
            std::map<qsizetype, ImportController::Outcome> ready;
            std::atomic<qsizetype> next{0};
            int workersRemaining = 0;
        };

        const auto pipeline = std::make_shared<PipelineState>();
        QThreadPool probePool;
        probePool.setMaxThreadCount(4);
        const int workerCount = qMin(4, paths.size());
        pipeline->workersRemaining = workerCount;
        QList<QFuture<void>> workers;
        workers.reserve(workerCount);
        for (int worker = 0; worker < workerCount; ++worker) {
            workers.append(QtConcurrent::run(
                &probePool, [callbackState, pipeline, paths, probe, knownTracks] {
                    while (!isCancelled(callbackState)) {
                        const qsizetype index = pipeline->next.fetch_add(1);
                        if (index >= paths.size()) {
                            break;
                        }
                        Outcome outcome;
                        outcome.ordinal = index;
                        outcome.path = paths[index];
#ifdef Q_OS_WIN
                        const QString key = outcome.path.toCaseFolded();
#else
                        const QString& key = outcome.path;
#endif
                        const auto known = knownTracks.constFind(key);
                        if (known != knownTracks.cend() && QFileInfo(outcome.path).isFile()) {
                            // insertBatch still resolves duplicates on the model thread.
                            // Do not reopen/decode every existing file on a folder drop.
                            outcome.cachedDuplicate = true;
                            outcome.result.result = AG_OK;
                            outcome.result.track = known.value();
                        } else {
                            outcome.result = probe(outcome.path);
                        }
                        {
                            std::lock_guard<std::mutex> lock(pipeline->mutex);
                            pipeline->ready.emplace(index, std::move(outcome));
                        }
                        pipeline->readyChanged.notify_one();
                    }
                    {
                        std::lock_guard<std::mutex> lock(pipeline->mutex);
                        --pipeline->workersRemaining;
                    }
                    pipeline->readyChanged.notify_one();
                }));
        }

        int delivered = 0;
        qsizetype nextToDeliver = 0;
        for (;;) {
            QList<Outcome> batch;
            {
                std::unique_lock<std::mutex> lock(pipeline->mutex);
                pipeline->readyChanged.wait_for(
                    lock, std::chrono::milliseconds(50), [&] {
                        return pipeline->ready.find(nextToDeliver) != pipeline->ready.end()
                               || pipeline->workersRemaining == 0;
                    });
                if (pipeline->ready.find(nextToDeliver) != pipeline->ready.end()
                    && pipeline->ready.size() < 64
                    && pipeline->workersRemaining > 0) {
                    pipeline->readyChanged.wait_for(
                        lock, std::chrono::milliseconds(50), [&] {
                            return pipeline->ready.size() >= 64
                                   || pipeline->workersRemaining == 0;
                        });
                }
                while (batch.size() < 64) {
                    auto ready = pipeline->ready.find(nextToDeliver);
                    if (ready == pipeline->ready.end()) break;
                    batch.append(std::move(ready->second));
                    pipeline->ready.erase(ready);
                    ++nextToDeliver;
                }
                if (batch.isEmpty() && pipeline->workersRemaining == 0) {
                    break;
                }
            }
            if (!batch.isEmpty()) {
                delivered += batch.size();
                postToController(
                    callbackState,
                    [batch = std::move(batch), delivered,
                     total = paths.size()](ImportController* controller) mutable {
                        controller->handleBatch(std::move(batch), delivered, total);
                    });
            }
        }
        for (QFuture<void>& worker : workers) {
            worker.waitForFinished();
        }
        postToController(callbackState, [](ImportController* controller) {
            controller->completeImport();
        });
    });
}

void ImportController::importPaths(const QStringList& paths)
{
    QList<QUrl> urls;
    urls.reserve(paths.size());
    for (const QString& path : paths) {
        urls.append(QUrl::fromLocalFile(path));
    }
    importUrls(urls);
}

void ImportController::finishWithoutImport(const QString& error)
{
    errors_.append(error);
    emit errorsChanged();
    progress_ = 1.0;
    emit progressChanged();
    busy_ = false;
    emit busyChanged();
    emit finished();
}

void ImportController::clearErrors()
{
    if (errors_.isEmpty()) {
        return;
    }
    errors_.clear();
    emit errorsChanged();
}

void ImportController::handleBatch(QList<Outcome> outcomes, int completed, int total)
{
    LibraryModel* const model = model_.data();
    QList<TrackRecord> tracks;
    QStringList successfulPaths;
    bool errorsChangedInBatch = false;
    int removedCachedDuplicates = 0;
    tracks.reserve(outcomes.size());
    successfulPaths.reserve(outcomes.size());
    for (Outcome& outcome : outcomes) {
        if (outcome.result.result == AG_OK) {
            if (outcome.cachedDuplicate && model != nullptr
                && model->thread() == thread()
                && model->indexForLocalFile(outcome.path) < 0) {
                // The track was deleted after the import snapshot was taken.
                // Respect that model-thread decision instead of resurrecting
                // the stale cached record without probing it.
                ++removedCachedDuplicates;
                continue;
            }
            if (outcome.result.track.path.isEmpty()) {
                outcome.result.track.path = outcome.path;
            }
            successfulPaths.append(outcome.result.track.path);
            outcome.result.track.metadataProbeAttempted = true;
            tracks.append(std::move(outcome.result.track));
            continue;
        }
        const QString detail = outcome.result.error.isEmpty()
            ? errorFor(outcome.result.result) : outcome.result.error;
        errors_.append(QStringLiteral("%1: %2").arg(outcome.path, detail));
        errorsChangedInBatch = true;
    }

    if (model == nullptr) {
        errors_.append(QStringLiteral("library model is unavailable"));
        errorsChangedInBatch = true;
    } else if (model->thread() != thread()) {
        errors_.append(QStringLiteral("library model thread affinity mismatch"));
        errorsChangedInBatch = true;
    } else {
        const int candidateCount = tracks.size();
        const QStringList insertedIds = model->insertBatch(
            importedTrackIds_.size(), std::move(tracks));
        const int skipped = candidateCount - insertedIds.size()
            + removedCachedDuplicates;
        if (skipped > 0) {
            skippedCount_ += skipped;
            emit skippedCountChanged();
        }
        bool idsChanged = false;
        // insertBatch already canonicalizes every path and returns accepted
        // IDs in input order. Only duplicates need a model lookup; avoid
        // another filesystem canonicalization for every newly inserted row.
        QStringList importedIds = insertedIds;
        if (skipped > 0) {
            importedIds.clear();
            importedIds.reserve(successfulPaths.size());
            for (const QString& path : successfulPaths) {
                const int row = model->indexForLocalFile(path);
                importedIds.append(row < 0 ? QString{}
                    : model->data(model->index(row, 0),
                                  LibraryModel::TrackIdRole).toString());
            }
        }
        for (const QString& trackId : importedIds) {
            if (!trackId.isEmpty() && !importedTrackIdSet_.contains(trackId)) {
                importedTrackIdSet_.insert(trackId);
                importedTrackIds_.append(trackId);
                idsChanged = true;
            }
        }
        if (idsChanged) {
            emit importedTrackIdsChanged();
        }
    }
    if (errorsChangedInBatch) {
        emit errorsChanged();
    }
    if (total > 0) {
        progress_ = qMax(progress_, static_cast<double>(completed)
                                      / static_cast<double>(total));
        emit progressChanged();
    }
}

void ImportController::completeImport()
{
    if (!busy_) {
        return;
    }
    progress_ = 1.0;
    emit progressChanged();
    busy_ = false;
    emit busyChanged();
    emit finished();
    if (!pendingUrls_.isEmpty()) {
        const QList<QUrl> queued = std::exchange(pendingUrls_, {});
        QMetaObject::invokeMethod(this, [this, queued] { importUrls(queued); },
                                  Qt::QueuedConnection);
    }
}
