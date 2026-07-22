#include "import_controller.hpp"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>

#include <mutex>

struct ImportCallbackState {
    std::mutex mutex;
    ImportController* controller = nullptr;
    std::atomic_bool* cancelled = nullptr;
};

namespace {
const QUrl kBrandCover(QStringLiteral("qrc:/AgPlayer/assets/brand/logo-mark.png"));

QString copiedUtf8(const char* value)
{
    return value == nullptr ? QString{} : QString::fromUtf8(value);
}

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

ProbeResult probeMetadata(const QString& requestedPath)
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
    track.title = copiedUtf8(ag_metadata_title(metadata));
    track.artist = copiedUtf8(ag_metadata_artist(metadata));
    track.album = copiedUtf8(ag_metadata_album(metadata));
    track.format = copiedUtf8(ag_metadata_format(metadata));
    track.sampleRate = ag_metadata_sample_rate(metadata);
    track.bitDepth = ag_metadata_bits_per_sample(metadata);
    track.bitRate = ag_metadata_bit_rate(metadata);
    track.durationMs = ag_metadata_duration_ms(metadata);
    size_t coverSize = 0;
    const char* coverMime = nullptr;
    const unsigned char* coverData = ag_metadata_cover(metadata, &coverSize, &coverMime);
    const QString mimeType = copiedUtf8(coverMime);
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
    return {AG_OK, std::move(track), {}};
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
}

ImportController::ImportController(LibraryModel* model, QObject* parent)
    : ImportController(model, probeMetadata, parent)
{
}

ImportController::ImportController(LibraryModel* model, ProbeFunction probe, QObject* parent)
    : QObject(parent),
      model_(model),
      probe_(std::move(probe)),
      callbackState_(std::make_shared<ImportCallbackState>())
{
    callbackState_->controller = this;
    callbackState_->cancelled = &cancelled_;
}

ImportController::~ImportController()
{
    cancel();
    std::lock_guard<std::mutex> lock(callbackState_->mutex);
    callbackState_->controller = nullptr;
}

void ImportController::cancel()
{
    cancelled_.store(true, std::memory_order_release);
    if (future_.isRunning()) {
        future_.waitForFinished();
    }
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

void ImportController::importUrls(const QList<QUrl>& urls)
{
    if (busy_) {
        return;
    }

    cancelled_.store(false, std::memory_order_release);
    errors_.clear();
    emit errorsChanged();
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

    QSet<QString> seen;
    for (const TrackRecord& track : model_->tracks()) {
        seen.insert(deduplicationKey(track.path));
    }
    QStringList paths;
    for (const QUrl& url : urls) {
        const QString path = url.toLocalFile();
        const QString key = deduplicationKey(path);
        if (!path.isEmpty() && !seen.contains(key)) {
            seen.insert(key);
            paths.append(canonicalLibraryPath(path));
        }
    }

    const std::shared_ptr<ImportCallbackState> callbackState = callbackState_;
    const ProbeFunction probe = probe_;
    future_ = QtConcurrent::run([callbackState, paths, probe] {
        if (paths.isEmpty()) {
            postToController(callbackState, [](ImportController* controller) {
                controller->handleResult({}, {AG_OK, {}, {}}, 0, 0);
            });
            return;
        }
        int completed = 0;
        for (const QString& path : paths) {
            if (isCancelled(callbackState)) {
                postToController(callbackState,
                                 [completed, total = paths.size()](
                                     ImportController* controller) {
                                     controller->handleResult({}, {AG_CANCELLED, {}, {}},
                                                              total, total);
                                 });
                return;
            }
            ProbeResult result = probe(path);
            ++completed;
            postToController(
                callbackState,
                [path, result = std::move(result), completed, total = paths.size()](
                    ImportController* controller) mutable {
                    controller->handleResult(path, std::move(result), completed, total);
                });
        }
    });
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

void ImportController::handleResult(const QString& path,
                                    ProbeResult result,
                                    int completed,
                                    int total)
{
    if (total > 0) {
        if (result.result == AG_OK) {
            result.track.path = canonicalLibraryPath(result.track.path.isEmpty() ? path : result.track.path);
            LibraryModel* const model = model_.data();
            if (model == nullptr) {
                errors_.append(QStringLiteral("%1: library model is unavailable").arg(path));
                emit errorsChanged();
            } else if (model->thread() != thread()) {
                errors_.append(QStringLiteral("%1: library model thread affinity mismatch").arg(path));
                emit errorsChanged();
            } else if (!model->containsPath(result.track.path)) {
                model->append(std::move(result.track));
            }
        } else {
            const QString detail = result.error.isEmpty() ? errorFor(result.result) : result.error;
            errors_.append(QStringLiteral("%1: %2").arg(path, detail));
            emit errorsChanged();
        }
        progress_ = static_cast<double>(completed) / static_cast<double>(total);
        emit progressChanged();
    } else {
        progress_ = 1.0;
        emit progressChanged();
    }

    if (completed == total) {
        busy_ = false;
        emit busyChanged();
        emit finished();
    }
}
