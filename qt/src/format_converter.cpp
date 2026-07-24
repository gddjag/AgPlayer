#include "format_converter.hpp"

#include <agplayer/c_api.h>

#include <QFileInfo>
#include <QFutureWatcher>
#include <QSet>
#include <QtConcurrent>

namespace {

// Map format string to FFmpeg codec name + file extension.
struct FormatInfo {
    const char* codec_name;
    const char* extension;
};

FormatInfo format_info(const QString& format)
{
    const QString f = format.toLower();
    if (f == QStringLiteral("mp3"))
        return {"libmp3lame", "mp3"};
    if (f == QStringLiteral("wav"))
        return {"pcm_s16le", "wav"};
    if (f == QStringLiteral("flac"))
        return {"flac", "flac"};
    if (f == QStringLiteral("aac") || f == QStringLiteral("m4a"))
        return {"aac", "m4a"};
    if (f == QStringLiteral("ogg"))
        return {"libvorbis", "ogg"};
    if (f == QStringLiteral("opus"))
        return {"libopus", "opus"};
    // Default to mp3.
    return {"libmp3lame", "mp3"};
}

} // namespace

FormatConverter::FormatConverter(QObject* parent)
    : QObject(parent)
{
}

double FormatConverter::progress() const noexcept
{
    return progress_.load(std::memory_order_acquire);
}

bool FormatConverter::busy() const noexcept
{
    return busy_.load(std::memory_order_acquire);
}

int FormatConverter::fileCount() const noexcept
{
    QMutexLocker lock(&mutex_);
    return static_cast<int>(entries_.size());
}

int FormatConverter::completedCount() const noexcept
{
    return completedCount_.load(std::memory_order_acquire);
}

int FormatConverter::failedCount() const noexcept
{
    return failedCount_.load(std::memory_order_acquire);
}

QVariantList FormatConverter::files() const
{
    QMutexLocker lock(&mutex_);
    QVariantList list;
    list.reserve(entries_.size());
    for (const FileEntry& entry : entries_) {
        QVariantMap map;
        map[QStringLiteral("fileName")] = entry.fileName;
        map[QStringLiteral("format")] = entry.format;
        map[QStringLiteral("fileSize")] = entry.fileSize;
        map[QStringLiteral("durationMs")] = entry.durationMs;
        map[QStringLiteral("status")] = statusString(entry.status);
        map[QStringLiteral("errorMessage")] = entry.errorMessage;
        list.append(map);
    }
    return list;
}

void FormatConverter::setBusy(bool value)
{
    busy_.store(value, std::memory_order_release);
    emit busyChanged();
}

void FormatConverter::setProgress(double value)
{
    progress_.store(value, std::memory_order_release);
    emit progressChanged();
}

void FormatConverter::setCompletedCount(int value)
{
    completedCount_.store(value, std::memory_order_release);
    emit completedCountChanged();
}

void FormatConverter::setFailedCount(int value)
{
    failedCount_.store(value, std::memory_order_release);
    emit failedCountChanged();
}

QString FormatConverter::statusString(FileStatus status)
{
    switch (status) {
    case FileStatus::Waiting:
        return QStringLiteral("Waiting");
    case FileStatus::Converting:
        return QStringLiteral("Converting");
    case FileStatus::Done:
        return QStringLiteral("Done");
    case FileStatus::Error:
        return QStringLiteral("Error");
    }
    return QStringLiteral("Waiting");
}

void FormatConverter::setEntryStatus(int index, FileStatus status)
{
    {
        QMutexLocker lock(&mutex_);
        if (index < 0 || index >= entries_.size()) {
            return;
        }
        entries_[index].status = status;
    }
    emit filesChanged();
}

void FormatConverter::setEntryError(int index, const QString& error)
{
    {
        QMutexLocker lock(&mutex_);
        if (index < 0 || index >= entries_.size()) {
            return;
        }
        entries_[index].errorMessage = error;
    }
    emit filesChanged();
}

void FormatConverter::loadFiles(const QList<QUrl>& urls)
{
    if (busy_.load(std::memory_order_acquire)) {
        return;
    }

    QSet<QString> seen;
    {
        QMutexLocker lock(&mutex_);
        for (const FileEntry& entry : entries_) {
            seen.insert(entry.path);
        }
    }

    QList<FileEntry> newEntries;
    for (const QUrl& url : urls) {
        const QString path = url.toLocalFile();
        if (path.isEmpty() || seen.contains(path)) {
            continue;
        }

        FileEntry entry;
        entry.path = path;
        const QFileInfo info(path);
        entry.fileName = info.fileName();
        entry.fileSize = info.size();
        entry.status = FileStatus::Waiting;

        // Read audio metadata to populate format and duration.
        ag_metadata* metadata = nullptr;
        if (ag_metadata_open(path.toUtf8().constData(), &metadata) == AG_OK) {
            entry.format = QString::fromUtf8(ag_metadata_format(metadata));
            entry.durationMs = ag_metadata_duration_ms(metadata);
            ag_metadata_destroy(metadata);
        }
        if (entry.format.isEmpty()) {
            entry.format = info.suffix().toUpper();
        }

        newEntries.append(entry);
        seen.insert(path);
    }

    {
        QMutexLocker lock(&mutex_);
        entries_.append(newEntries);
    }
    emit fileCountChanged();
    emit filesChanged();
}

QString FormatConverter::entryAt(int index) const
{
    QMutexLocker lock(&mutex_);
    if (index < 0 || index >= static_cast<int>(entries_.size())) {
        return {};
    }
    return entries_[index].fileName;
}

void FormatConverter::removeFile(int index)
{
    if (busy_.load(std::memory_order_acquire)) {
        return;
    }

    {
        QMutexLocker lock(&mutex_);
        if (index < 0 || index >= entries_.size()) {
            return;
        }
        entries_.removeAt(index);
    }
    emit fileCountChanged();
    emit filesChanged();
}

void FormatConverter::clear()
{
    if (busy_.load(std::memory_order_acquire)) {
        return;
    }

    {
        QMutexLocker lock(&mutex_);
        entries_.clear();
    }
    setProgress(0.0);
    setCompletedCount(0);
    setFailedCount(0);
    emit fileCountChanged();
    emit filesChanged();
}

QString FormatConverter::formatFileSize(qint64 bytes) const
{
    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    if (bytes < 1024 * 1024) {
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 2);
    }
    if (bytes < 1024 * 1024 * 1024) {
        return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
    }
    return QStringLiteral("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

QString FormatConverter::formatDuration(qint64 ms) const
{
    if (ms <= 0) {
        return QStringLiteral("--:--");
    }
    const qint64 totalSeconds = ms / 1000;
    const qint64 minutes = totalSeconds / 60;
    const qint64 seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QChar('0'));
}

QString FormatConverter::computeOutputPath(const QString& inputPath,
                                           const QString& outputFormat,
                                           const QString& outputDir) const
{
    const QFileInfo info(inputPath);
    const FormatInfo fi = format_info(outputFormat);

    const QString dir = outputDir.isEmpty()
        ? info.absolutePath()
        : outputDir;
    const QString baseName = info.completeBaseName();

    // Avoid collision: if the file exists, append _1, _2, ...
    QString candidate = dir + QStringLiteral("/") + baseName
                        + QStringLiteral(".") + QString::fromLatin1(fi.extension);
    int counter = 1;
    while (QFileInfo::exists(candidate)) {
        candidate = dir + QStringLiteral("/") + baseName
                    + QStringLiteral("_") + QString::number(counter)
                    + QStringLiteral(".") + QString::fromLatin1(fi.extension);
        ++counter;
    }
    return candidate;
}

void FormatConverter::start(const QString& outputFormat,
                            int bitRate,
                            int sampleRate,
                            int channels,
                            const QString& outputDir,
                            bool keepMetadata,
                            bool volumeNormalize,
                            bool extractAudio)
{
    Q_UNUSED(extractAudio)

    if (busy_.load(std::memory_order_acquire)) {
        return;
    }

    int fileCount = 0;
    {
        QMutexLocker lock(&mutex_);
        fileCount = static_cast<int>(entries_.size());
    }
    if (fileCount == 0) {
        emit errorOccurred(QStringLiteral("No files to transcode"));
        return;
    }

    // keepMetadata stripping is not yet supported by the Core; metadata is preserved.
    if (!keepMetadata) {
        emit warningOccurred(QStringLiteral("Metadata stripping is not supported yet; existing metadata will be preserved."));
    }

    cancelFlag_.store(false, std::memory_order_release);
    currentToken_.store(nullptr, std::memory_order_release);
    setBusy(true);
    setProgress(0.0);
    setCompletedCount(0);
    setFailedCount(0);

    // Reset all statuses to Waiting before starting.
    {
        QMutexLocker lock(&mutex_);
        for (FileEntry& entry : entries_) {
            entry.status = FileStatus::Waiting;
            entry.errorMessage.clear();
        }
    }
    emit filesChanged();

    auto* watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this,
        [this, watcher]() {
            watcher->deleteLater();
            ag_cancel_token* t = currentToken_.exchange(nullptr,
                std::memory_order_acq_rel);
            if (t) ag_cancel_token_destroy(t);
            const int success = completedCount_.load(std::memory_order_acquire)
                                - failedCount_.load(std::memory_order_acquire);
            const int failure = failedCount_.load(std::memory_order_acquire);
            if (cancelFlag_.load(std::memory_order_acquire)) {
                setProgress(0.0);
            } else {
                setProgress(1.0);
            }
            setBusy(false);
            emit transcodeCompleted(success, failure);
        });

    QFuture<void> future = QtConcurrent::run(
        [this, outputFormat, bitRate, sampleRate, channels, outputDir, keepMetadata, volumeNormalize]() {
            runTranscode(outputFormat, bitRate, sampleRate, channels, outputDir, keepMetadata, volumeNormalize);
        });
    watcher->setFuture(future);
}

void FormatConverter::runTranscode(const QString& outputFormat,
                                   int bitRate,
                                   int sampleRate,
                                   int channels,
                                   const QString& outputDir,
                                   bool /*keepMetadata*/,
                                   bool volumeNormalize)
{
    const FormatInfo fi = format_info(outputFormat);
    const QByteArray codecName = QByteArray(fi.codec_name);

    int totalJobs = 0;
    {
        QMutexLocker lock(&mutex_);
        totalJobs = static_cast<int>(entries_.size());
    }

    for (int i = 0; i < totalJobs; ++i) {
        if (cancelFlag_.load(std::memory_order_acquire)) {
            break;
        }

        QString inputPath;
        QString outputPath;
        {
            QMutexLocker lock(&mutex_);
            if (i >= entries_.size()) {
                break;
            }
            inputPath = entries_[i].path;
        }
        outputPath = computeOutputPath(inputPath, outputFormat, outputDir);

        setEntryStatus(i, FileStatus::Converting);

        ag_cancel_token* token = ag_cancel_token_create();
        currentToken_.store(token, std::memory_order_release);
        const QByteArray inputUtf8 = inputPath.toUtf8();
        const QByteArray outputUtf8 = outputPath.toUtf8();

        ag_transcode_options options{};
        options.volume_normalize = volumeNormalize ? 1 : 0;

        const ag_result result = ag_transcode_ex(
            inputUtf8.constData(),
            outputUtf8.constData(),
            codecName.isEmpty() ? nullptr : codecName.constData(),
            static_cast<long long>(bitRate),
            sampleRate,
            channels,
            &options,
            token,
            nullptr,
            nullptr);

        currentToken_.store(nullptr, std::memory_order_release);
        ag_cancel_token_destroy(token);

        if (cancelFlag_.load(std::memory_order_acquire)) {
            setEntryError(i, QStringLiteral("Cancelled"));
            setEntryStatus(i, FileStatus::Error);
            failedCount_.fetch_add(1, std::memory_order_acq_rel);
            emit failedCountChanged();
        } else if (result == AG_OK) {
            setEntryStatus(i, FileStatus::Done);
        } else if (result == AG_CANCELLED) {
            setEntryError(i, QStringLiteral("Cancelled"));
            setEntryStatus(i, FileStatus::Error);
            failedCount_.fetch_add(1, std::memory_order_acq_rel);
            emit failedCountChanged();
        } else {
            setEntryError(i, QStringLiteral("Transcode failed (error %1)").arg(result));
            setEntryStatus(i, FileStatus::Error);
            failedCount_.fetch_add(1, std::memory_order_acq_rel);
            emit failedCountChanged();
        }

        const int completed = completedCount_.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (totalJobs > 0) {
            setProgress(static_cast<double>(completed) / totalJobs);
        }
    }
}

void FormatConverter::cancel()
{
    cancelFlag_.store(true, std::memory_order_release);
    ag_cancel_token* t = currentToken_.load(std::memory_order_acquire);
    if (t) {
        ag_cancel_token_cancel(t);
    }
}
