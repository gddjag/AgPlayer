#include "import_controller.hpp"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>

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
}

ImportController::ImportController(LibraryModel* model, QObject* parent)
    : ImportController(model, probeMetadata, parent)
{
}

ImportController::ImportController(LibraryModel* model, ProbeFunction probe, QObject* parent)
    : QObject(parent), model_(model), probe_(std::move(probe))
{
    Q_ASSERT(model_ != nullptr);
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

    errors_.clear();
    emit errorsChanged();
    progress_ = 0.0;
    emit progressChanged();
    busy_ = true;
    emit busyChanged();

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

    const QPointer<ImportController> guard(this);
    const ProbeFunction probe = probe_;
    future_ = QtConcurrent::run([guard, paths, probe] {
        if (paths.isEmpty()) {
            if (guard != nullptr) {
                QMetaObject::invokeMethod(
                    guard.data(),
                    [guard] {
                        if (guard != nullptr) {
                            guard->handleResult({}, {AG_OK, {}, {}}, 0, 0);
                        }
                    },
                    Qt::QueuedConnection);
            }
            return;
        }
        int completed = 0;
        for (const QString& path : paths) {
            ProbeResult result = probe(path);
            ++completed;
            if (guard != nullptr) {
                QMetaObject::invokeMethod(
                    guard.data(),
                    [guard, path, result = std::move(result), completed, total = paths.size()]() mutable {
                        if (guard != nullptr) {
                            guard->handleResult(path, std::move(result), completed, total);
                        }
                    },
                    Qt::QueuedConnection);
            }
        }
    });
}

void ImportController::handleResult(const QString& path,
                                    ProbeResult result,
                                    int completed,
                                    int total)
{
    if (total > 0) {
        if (result.result == AG_OK) {
            result.track.path = canonicalLibraryPath(result.track.path.isEmpty() ? path : result.track.path);
            if (!model_->containsPath(result.track.path)) {
                model_->append(std::move(result.track));
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
