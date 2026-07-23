#include "format_converter.hpp"

#include <agplayer/c_api.h>

#include <QFileInfo>
#include <QFutureWatcher>
#include <QMutex>
#include <QSet>
#include <QStandardPaths>
#include <QThreadPool>
#include <QtConcurrent>

namespace {

struct TranscodeResult {
    bool success = false;
    QString error;
};

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

void FormatConverter::loadFiles(const QList<QUrl>& urls)
{
    if (busy_.load(std::memory_order_acquire)) {
        return;
    }

    entries_.clear();
    for (const QUrl& url : urls) {
        const QString path = url.toLocalFile();
        if (path.isEmpty()) continue;
        FileEntry entry;
        entry.path = path;
        entry.fileName = QFileInfo(path).fileName();
        entries_.append(entry);
    }
    emit fileCountChanged();
}

QString FormatConverter::entryAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(entries_.size())) {
        return {};
    }
    return entries_[index].fileName;
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
                            int cpuCores,
                            const QString& outputDir)
{
    if (busy_.load(std::memory_order_acquire)) {
        return;
    }
    if (entries_.isEmpty()) {
        emit errorOccurred(QStringLiteral("No files to transcode"));
        return;
    }

    cancelFlag_.store(false, std::memory_order_release);
    setBusy(true);
    setProgress(0.0);
    setCompletedCount(0);
    setFailedCount(0);

    const FormatInfo fi = format_info(outputFormat);
    const QByteArray codecName = QByteArray(fi.codec_name);

    // Prepare the list of transcode jobs.
    struct TranscodeJob {
        QString inputPath;
        QString outputPath;
    };
    QList<TranscodeJob> jobs;
    jobs.reserve(entries_.size());
    for (const FileEntry& entry : entries_) {
        TranscodeJob job;
        job.inputPath = entry.path;
        job.outputPath = computeOutputPath(entry.path, outputFormat, outputDir);
        jobs.append(job);
    }

    // Configure thread pool for parallel transcoding. Each job runs in its
    // own thread; max threads = cpuCores (clamped to 1..32).
    const int clampedCores = qBound(1, cpuCores, 32);
    QThreadPool* pool = QThreadPool::globalInstance();
    const int prevMax = pool->maxThreadCount();
    pool->setMaxThreadCount(clampedCores);

    const int totalJobs = jobs.size();
    const QString codecNameStr = QString::fromLatin1(fi.codec_name);

    auto* watcher = new QFutureWatcher<TranscodeResult>(this);
    connect(watcher, &QFutureWatcher<TranscodeResult>::finished, this,
        [this, watcher, prevMax, totalJobs]() {
            watcher->deleteLater();
            QThreadPool::globalInstance()->setMaxThreadCount(prevMax);

            const QList<TranscodeResult> results = watcher->future().results();
            int success = 0;
            int failure = 0;
            for (const TranscodeResult& r : results) {
                if (r.success) ++success;
                else ++failure;
            }
            setCompletedCount(success);
            setFailedCount(failure);
            setProgress(1.0);
            setBusy(false);
            emit transcodeCompleted(success, failure);
        });

    // Run transcoding in parallel. Each job calls ag_transcode independently.
    // Progress is tracked atomically across all jobs.
    auto transcodeOne = [this, codecName, bitRate, sampleRate, channels, totalJobs]
                        (const TranscodeJob& job) -> TranscodeResult {
        if (cancelFlag_.load(std::memory_order_acquire)) {
            return {false, QStringLiteral("Cancelled")};
        }

        ag_cancel_token* token = ag_cancel_token_create();
        const QByteArray inputUtf8 = job.inputPath.toUtf8();
        const QByteArray outputUtf8 = job.outputPath.toUtf8();

        const ag_result result = ag_transcode(
            inputUtf8.constData(),
            outputUtf8.constData(),
            codecName.isEmpty() ? nullptr : codecName.constData(),
            static_cast<long long>(bitRate),
            sampleRate,
            channels,
            token,
            nullptr,
            nullptr);

        ag_cancel_token_destroy(token);

        TranscodeResult tr;
        if (result == AG_OK) {
            tr.success = true;
        } else if (result == AG_CANCELLED) {
            tr.success = false;
            tr.error = QStringLiteral("Cancelled");
        } else {
            tr.success = false;
            tr.error = QStringLiteral("Transcode failed (error %1)").arg(result);
        }

        if (!cancelFlag_.load(std::memory_order_acquire)) {
            const int completed = completedCount_.fetch_add(1, std::memory_order_acq_rel) + 1;
            const int failed = failedCount_.load(std::memory_order_acquire);
            const int done = completed + failed;
            if (totalJobs > 0) {
                const double frac = static_cast<double>(done) / totalJobs;
                progress_.store(frac, std::memory_order_release);
            }
        }
        return tr;
    };

    QFuture<TranscodeResult> future = QtConcurrent::mapped(jobs, transcodeOne);
    watcher->setFuture(future);
}

void FormatConverter::cancel()
{
    cancelFlag_.store(true, std::memory_order_release);
}

void FormatConverter::clear()
{
    if (busy_.load(std::memory_order_acquire)) {
        return;
    }
    entries_.clear();
    setProgress(0.0);
    setCompletedCount(0);
    setFailedCount(0);
    emit fileCountChanged();
}
