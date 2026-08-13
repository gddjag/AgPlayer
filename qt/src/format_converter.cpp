#include "format_converter.hpp"

#include "audio_file_discovery.hpp"
#include "format_conversion_filter_model.hpp"
#include "format_conversion_task_model.hpp"
#include "transcode_capability.hpp"
#include "metadata_writer.hpp"
#include "runtime_log.hpp"

#include <agplayer/c_api.h>

#include <QDir>
#include <QClipboard>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QFutureWatcher>
#include <QSet>
#include <QThread>
#include <QThreadPool>
#include <QUuid>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif
#include <QVector>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <numeric>

namespace {

QString cover_mime_type(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("png")) return QStringLiteral("image/png");
    if (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg"))
        return QStringLiteral("image/jpeg");
    if (suffix == QLatin1String("bmp")) return QStringLiteral("image/bmp");
    return {};
}

agplayer::CanonicalField canonical_field(const QString& key)
{
    if (key == QLatin1String("title")) return agplayer::CanonicalField::Title;
    if (key == QLatin1String("artist")) return agplayer::CanonicalField::Artist;
    if (key == QLatin1String("album")) return agplayer::CanonicalField::Album;
    if (key == QLatin1String("albumArtist")) return agplayer::CanonicalField::AlbumArtist;
    if (key == QLatin1String("genre")) return agplayer::CanonicalField::Genre;
    if (key == QLatin1String("year")) return agplayer::CanonicalField::Year;
    if (key == QLatin1String("date")) return agplayer::CanonicalField::Date;
    if (key == QLatin1String("composer")) return agplayer::CanonicalField::Composer;
    return agplayer::CanonicalField::Bpm;
}

agplayer::MetadataEditPlan metadata_plan(const QVariantMap& fields,
                                         const QByteArray& coverData,
                                         const QString& coverMime)
{
    agplayer::MetadataEditPlan plan;
    const QStringList keys{QStringLiteral("title"), QStringLiteral("artist"),
        QStringLiteral("album"), QStringLiteral("albumArtist"), QStringLiteral("genre"),
        QStringLiteral("year"), QStringLiteral("date"), QStringLiteral("composer"),
        QStringLiteral("bpm")};
    for (const QString& key : keys) {
        const QVariantMap value = fields.value(key).toMap();
        const QString mode = value.value(QStringLiteral("mode"),
                                         QStringLiteral("keep")).toString();
        if (mode == QLatin1String("keep")) continue;
        const auto action = mode == QLatin1String("clear")
            ? agplayer::MetadataAction::Clear : agplayer::MetadataAction::Set;
        plan.fields.push_back({canonical_field(key), action,
            action == agplayer::MetadataAction::Set
                ? std::optional<std::string>(value.value(QStringLiteral("value"))
                    .toString().toUtf8().toStdString()) : std::nullopt});
    }
    const QString coverMode = fields.value(QStringLiteral("coverMode"),
                                           QStringLiteral("keep")).toString();
    if (coverMode == QLatin1String("set")) {
        plan.cover_action = agplayer::CoverAction::Set;
        plan.cover_data = reinterpret_cast<const unsigned char*>(coverData.constData());
        plan.cover_size = static_cast<std::size_t>(coverData.size());
        plan.cover_mime_type = coverMime.toStdString();
    } else if (coverMode == QLatin1String("clear")) {
        plan.cover_action = agplayer::CoverAction::Clear;
    }
    return plan;
}

} // namespace

namespace {

QString staging_path_for(const QString& finalPath)
{
    const QFileInfo info(finalPath);
    return info.dir().filePath(
        QStringLiteral(".%1.agplayer-part-%2.%3")
            .arg(info.completeBaseName(),
                 QUuid::createUuid().toString(QUuid::Id128),
                 info.suffix()));
}

bool validate_audio_output(const QString& path)
{
    ag_metadata* metadata = nullptr;
    const QByteArray utf8 = path.toUtf8();
    if (ag_metadata_open(utf8.constData(), &metadata) != AG_OK
        || metadata == nullptr) {
        return false;
    }
    const bool valid = ag_metadata_duration_ms(metadata) > 0;
    ag_metadata_destroy(metadata);
    return valid;
}

bool commit_staged_output(const QString& stagedPath,
                          const QString& finalPath,
                          bool overwriteExisting)
{
    if (!overwriteExisting && QFileInfo::exists(finalPath)) {
        return false;
    }
#ifdef Q_OS_WIN
    const std::wstring staged = QDir::toNativeSeparators(stagedPath).toStdWString();
    const std::wstring final = QDir::toNativeSeparators(finalPath).toStdWString();
    if (QFileInfo::exists(finalPath)) {
        return ReplaceFileW(final.c_str(), staged.c_str(), nullptr,
                            REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)
               != FALSE;
    }
    return MoveFileExW(staged.c_str(), final.c_str(), MOVEFILE_WRITE_THROUGH)
           != FALSE;
#else
    std::error_code error;
    if (overwriteExisting) {
        std::filesystem::remove(finalPath.toStdString(), error);
        error.clear();
    }
    std::filesystem::rename(stagedPath.toStdString(), finalPath.toStdString(),
                            error);
    return !error;
#endif
}

// Map format string to FFmpeg codec name + file extension.
struct FormatInfo {
    const char* codec_name;
    const char* extension;
    const char* muxer_name;
};

FormatInfo format_info(const QString& format)
{
    const QString f = format.toLower();
    if (f == QStringLiteral("mp3"))
        return {"libmp3lame", "mp3", "mp3"};
    if (f == QStringLiteral("wav"))
        return {"pcm_s16le", "wav", "wav"};
    if (f == QStringLiteral("flac"))
        return {"flac", "flac", "flac"};
    if (f == QStringLiteral("aac"))
        return {"aac", "aac", "adts"};
    if (f == QStringLiteral("m4a"))
        return {"aac", "m4a", "ipod"};
    if (f == QStringLiteral("ogg"))
        return {"libvorbis", "ogg", "ogg"};
    if (f == QStringLiteral("opus"))
        return {"libopus", "opus", "opus"};
    if (f == QStringLiteral("alac"))
        return {"alac", "m4a", "ipod"};
    if (f == QStringLiteral("wma"))
        return {"wmav2", "wma", "asf"};
    // Default to mp3.
    return {"libmp3lame", "mp3", "mp3"};
}

bool is_video_file(const QString& path)
{
    static const QSet<QString> extensions = {
        QStringLiteral("mp4"), QStringLiteral("mkv"),
        QStringLiteral("avi"), QStringLiteral("mov"),
        QStringLiteral("webm")
    };
    return extensions.contains(QFileInfo(path).suffix().toLower());
}

} // namespace

FormatConverter::FormatConverter(QObject* parent)
    : QObject(parent)
    , taskModel_(new FormatConversionTaskModel(this))
    , filteredTaskModel_(new FormatConversionFilterModel(this))
{
    filteredTaskModel_->setSourceModel(taskModel_);
    connect(this, &FormatConverter::filesChanged, this,
            &FormatConverter::syncTaskModel);
    connect(taskModel_, &QAbstractItemModel::dataChanged, this,
            &FormatConverter::checkedCountChanged);
    connect(taskModel_, &QAbstractItemModel::rowsInserted, this,
            &FormatConverter::checkedCountChanged);
    connect(taskModel_, &QAbstractItemModel::rowsRemoved, this,
            &FormatConverter::checkedCountChanged);
    connect(taskModel_, &QAbstractItemModel::modelReset, this,
            &FormatConverter::checkedCountChanged);
}

FormatConverter::~FormatConverter()
{
    cancel();
    if (discoveryWatcher_ != nullptr) {
        discoveryWatcher_->future().waitForFinished();
    }
    if (loadWatcher_ != nullptr) {
        loadWatcher_->future().waitForFinished();
    }
    if (watcher_ != nullptr) {
        watcher_->future().waitForFinished();
    }
}

void FormatConverter::setParallelJobs(int value)
{
    if (busy()) {
        return;
    }
    const int bounded = std::clamp(value, 1, 4);
    if (parallelJobs_ == bounded) {
        return;
    }
    parallelJobs_ = bounded;
    emit parallelJobsChanged();
}

void FormatConverter::setBitrateMode(const QString& value)
{
    if (busy_.load(std::memory_order_acquire)) {
        return;
    }
    const QString normalized = value.trimmed().toLower();
    if (normalized != QStringLiteral("cbr")
        && normalized != QStringLiteral("vbr")) {
        return;
    }
    if (bitrateMode_ == normalized) {
        return;
    }
    bitrateMode_ = normalized;
    emit bitrateModeChanged();
}

void FormatConverter::setConflictPolicy(const QString& value)
{
    if (busy_.load(std::memory_order_acquire)) {
        return;
    }
    const QString normalized = value.trimmed().toLower();
    if (normalized != QStringLiteral("auto-number")
        && normalized != QStringLiteral("skip")
        && normalized != QStringLiteral("overwrite")
        && normalized != QStringLiteral("ask")) {
        return;
    }
    if (conflictPolicy_ == normalized) {
        return;
    }
    conflictPolicy_ = normalized;
    overwriteExisting_ = normalized == QStringLiteral("overwrite");
    emit conflictPolicyChanged();
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

QAbstractItemModel* FormatConverter::taskModel() const
{
    return taskModel_;
}

QAbstractItemModel* FormatConverter::filteredTaskModel() const
{
    return filteredTaskModel_;
}

void FormatConverter::setSelectedFormat(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized.isEmpty() || normalized == selectedFormat_) return;
    selectedFormat_ = normalized;
    emit currentCapabilityChanged();
}

QVariantMap FormatConverter::currentCapability() const
{
    for (const QVariant& value : supportedOutputFormats()) {
        const QVariantMap format = value.toMap();
        if (format.value(QStringLiteral("key")).toString() == selectedFormat_) {
            return format;
        }
    }
    return {};
}

QString FormatConverter::etaText() const
{
    if (!busy() || progress() <= 0.0 || progress() >= 1.0) {
        return QStringLiteral("--:--");
    }
    return tr("计算中");
}

int FormatConverter::checkedCount() const
{
    return taskModel_->checkedCount();
}

int FormatConverter::convertingCount() const
{
    int count = 0;
    QMutexLocker lock(&mutex_);
    for (const FileEntry& entry : entries_) {
        if (entry.status == FileStatus::Converting) ++count;
    }
    return count;
}

int FormatConverter::cancelledCount() const
{
    int count = 0;
    QMutexLocker lock(&mutex_);
    for (const FileEntry& entry : entries_) {
        if (entry.status == FileStatus::Cancelled) ++count;
    }
    return count;
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
        map[QStringLiteral("sampleRate")] = entry.sampleRate;
        map[QStringLiteral("bitRate")] = entry.bitRate;
        map[QStringLiteral("channels")] = entry.channels;
        map[QStringLiteral("progress")] = entry.progress;
        map[QStringLiteral("outputFormat")] = entry.outputFormat;
        map[QStringLiteral("outputPath")] = entry.outputPath;
        map[QStringLiteral("status")] = statusString(entry.status);
        map[QStringLiteral("errorMessage")] = entry.errorMessage;
        list.append(map);
    }
    return list;
}

void FormatConverter::syncTaskModel()
{
    QList<FileEntry> entries;
    {
        QMutexLocker lock(&mutex_);
        entries = entries_;
    }
    QSet<QString> liveIds;
    for (const FileEntry& entry : entries) {
        const QString taskId = entry.taskId.isEmpty()
            ? QDir::cleanPath(entry.path).toCaseFolded() : entry.taskId;
        liveIds.insert(taskId);
        const QVariantMap values{
            {QStringLiteral("taskId"), taskId},
            {QStringLiteral("checked"), true},
            {QStringLiteral("fileName"), entry.fileName},
            {QStringLiteral("path"), entry.path},
            {QStringLiteral("sourceFormat"), entry.format},
            {QStringLiteral("durationMs"), entry.durationMs},
            {QStringLiteral("sampleRate"), entry.sampleRate},
            {QStringLiteral("bitRate"), entry.bitRate},
            {QStringLiteral("channelLayout"), entry.channels == 1
                ? QStringLiteral("mono") : QStringLiteral("stereo")},
            {QStringLiteral("outputFormat"), entry.outputFormat},
            {QStringLiteral("outputPath"), entry.outputPath},
            {QStringLiteral("status"), statusString(entry.status)},
            {QStringLiteral("stage"), statusString(entry.status)},
            {QStringLiteral("progress"), entry.progress},
            {QStringLiteral("errorSummary"), entry.errorMessage},
            {QStringLiteral("errorDetail"), entry.errorMessage}};
        if (taskModel_->containsTask(taskId)) {
            QVariantMap changes = values;
            changes.remove(QStringLiteral("taskId"));
            changes.remove(QStringLiteral("checked"));
            taskModel_->updateTask(taskId, changes);
        } else {
            taskModel_->appendTask(values);
        }
    }
    QStringList removed;
    for (const QString& taskId : taskModel_->taskIds()) {
        if (!liveIds.contains(taskId)) removed.append(taskId);
    }
    taskModel_->removeTasks(removed);
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
    case FileStatus::Ready:
        return QStringLiteral("Ready");
    case FileStatus::PendingConfirmation:
        return QStringLiteral("PendingConfirmation");
    case FileStatus::Skipped:
        return QStringLiteral("Skipped");
    case FileStatus::Converting:
        return QStringLiteral("Converting");
    case FileStatus::Done:
        return QStringLiteral("Done");
    case FileStatus::Error:
        return QStringLiteral("Error");
    case FileStatus::Cancelled:
        return QStringLiteral("Cancelled");
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
        if (status == FileStatus::Done || status == FileStatus::Error
            || status == FileStatus::Cancelled) {
            entries_[index].progress = 1.0;
        }
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

QVariantMap FormatConverter::previewSelected(const QVariantList& indices,
                                             const QString& outputFormat,
                                             int bitRate,
                                             int sampleRate,
                                             int channels,
                                             const QString& outputDir,
                                             bool extractAudio)
{
    Q_UNUSED(bitRate)
    Q_UNUSED(channels)
    Q_UNUSED(extractAudio)

    const QString normalizedFormat = outputFormat.trimmed().toLower();
    bool supported = false;
    QString unsupportedReason;
    for (const QVariant& value : supportedOutputFormats()) {
        const QVariantMap candidate = value.toMap();
        if (candidate.value(QStringLiteral("key")).toString()
            == normalizedFormat) {
            supported = candidate.value(QStringLiteral("available")).toBool();
            unsupportedReason = candidate.value(QStringLiteral("reason")).toString();
            break;
        }
    }
    if (!supported || indices.isEmpty()) {
        return {{QStringLiteral("ready"), false},
                {QStringLiteral("requiresConfirmation"), false},
                {QStringLiteral("reason"), unsupportedReason.isEmpty()
                    ? tr("没有可用的转换任务或输出格式") : unsupportedReason}};
    }

    QVariantList conflicts;
    bool hasUsableEntry = false;
    if (conflictPolicy_ == QStringLiteral("ask")) {
        const FormatInfo format = format_info(normalizedFormat);
        QMutexLocker lock(&mutex_);
        for (const QVariant& value : indices) {
            const int index = value.toInt();
            if (index < 0 || index >= entries_.size()) {
                continue;
            }
            hasUsableEntry = true;
            const QFileInfo input(entries_.at(index).path);
            const QString directory = outputDir.isEmpty()
                ? input.absolutePath() : outputDir;
            const QString target = QDir(directory).filePath(
                input.completeBaseName() + QLatin1Char('.')
                + QString::fromLatin1(format.extension));
            if (QFileInfo::exists(target)) {
                conflicts.append(QVariantMap{
                    {QStringLiteral("inputPath"), input.absoluteFilePath()},
                    {QStringLiteral("outputPath"), target},
                });
            }
        }
    } else {
        QMutexLocker lock(&mutex_);
        for (const QVariant& value : indices) {
            const int index = value.toInt();
            hasUsableEntry = hasUsableEntry
                || (index >= 0 && index < entries_.size());
        }
    }
    if (!hasUsableEntry) {
        return {{QStringLiteral("ready"), false},
                {QStringLiteral("requiresConfirmation"), false},
                {QStringLiteral("reason"), tr("没有可用的转换任务")}};
    }

    const int resolvedSampleRate = normalizedFormat == QStringLiteral("opus")
        ? 48000 : sampleRate;
    const bool requiresConfirmation = (sampleRate != 0
        && resolvedSampleRate != sampleRate) || !conflicts.isEmpty();
    const QVariantMap resolvedProfile{
        {QStringLiteral("format"), normalizedFormat},
        {QStringLiteral("sampleRate"), resolvedSampleRate},
        {QStringLiteral("conflictPolicy"), conflictPolicy_},
        {QStringLiteral("conflictCount"), conflicts.size()},
    };
    {
        QMutexLocker lock(&mutex_);
        for (const QVariant& value : indices) {
            const int index = value.toInt();
            if (index < 0 || index >= entries_.size()) {
                continue;
            }
            entries_[index].status = requiresConfirmation
                ? FileStatus::PendingConfirmation : FileStatus::Ready;
            entries_[index].outputFormat = normalizedFormat.toUpper();
        }
    }
    emit filesChanged();
    return {{QStringLiteral("ready"), true},
            {QStringLiteral("requiresConfirmation"), requiresConfirmation},
            {QStringLiteral("resolvedProfile"), resolvedProfile},
            {QStringLiteral("conflictCount"), conflicts.size()},
            {QStringLiteral("conflicts"), conflicts}};
}

QVariantList FormatConverter::supportedOutputFormats() const
{
    struct Candidate {
        const char* key;
        const char* label;
        const char* codec;
        const char* encoderLabel;
    };
    static constexpr Candidate candidates[] = {
        {"mp3", "MP3", "libmp3lame", "LAME MP3"},
        {"wav", "WAV", "pcm_s16le", "PCM"},
        {"flac", "FLAC", "flac", "FLAC"},
        {"aac", "AAC", "aac", "AAC"},
        {"m4a", "AAC / M4A", "aac", "AAC"},
        {"ogg", "OGG", "libvorbis", "Vorbis"},
        {"opus", "Opus", "libopus", "libopus"},
        {"alac", "ALAC", "alac", "ALAC"},
    };
    const std::vector<agplayer::TranscodeFormatCapability> capabilities =
        agplayer::transcode_capabilities();
    if (!capabilities.empty()) {
        QVariantList actualFormats;
        for (const agplayer::TranscodeFormatCapability& capability
             : capabilities) {
            QVariantList sampleRates;
            for (const int value : capability.sample_rates)
                sampleRates.append(value);
            QVariantList sampleFormats;
            for (const std::string& value : capability.sample_formats)
                sampleFormats.append(QString::fromStdString(value));
            QVariantList channelLayouts;
            for (const std::string& value : capability.channel_layouts)
                channelLayouts.append(QString::fromStdString(value));
            QVariantList bitrateModes;
            for (const agplayer::TranscodeOptionChoice& mode
                 : capability.bitrate_modes) {
                bitrateModes.append(QVariantMap{
                    {QStringLiteral("key"), QString::fromStdString(mode.key)},
                    {QStringLiteral("label"), QString::fromStdString(mode.label)},
                    {QStringLiteral("default"), mode.is_default}});
            }
            actualFormats.append(QVariantMap{
                {QStringLiteral("key"), QString::fromStdString(capability.key)},
                {QStringLiteral("label"), QString::fromStdString(capability.label)},
                {QStringLiteral("codec"), QString::fromStdString(capability.codec_name)},
                {QStringLiteral("encoderLabel"), QString::fromStdString(capability.codec_name)},
                {QStringLiteral("muxer"), QString::fromStdString(capability.muxer_name)},
                {QStringLiteral("available"), capability.available},
                {QStringLiteral("reason"), QString::fromStdString(capability.unavailable_reason)},
                {QStringLiteral("lossy"), capability.lossy},
                {QStringLiteral("supportsMetadata"), capability.supports_metadata},
                {QStringLiteral("supportsCover"), capability.supports_cover},
                {QStringLiteral("sampleRates"), sampleRates},
                {QStringLiteral("sampleFormats"), sampleFormats},
                {QStringLiteral("channelLayouts"), channelLayouts},
                {QStringLiteral("bitrateModes"), bitrateModes}});
        }
        return actualFormats;
    }
    QVariantList formats;
    for (const Candidate& candidate : candidates) {
        const bool available = ag_encoder_available(candidate.codec) != 0;
        formats.append(QVariantMap{
            {QStringLiteral("key"), QString::fromLatin1(candidate.key)},
            {QStringLiteral("label"), QString::fromLatin1(candidate.label)},
            {QStringLiteral("codec"), QString::fromLatin1(candidate.codec)},
            {QStringLiteral("encoderLabel"),
             QString::fromLatin1(candidate.encoderLabel)},
            {QStringLiteral("available"), available},
            {QStringLiteral("reason"), available ? QString()
                : tr("当前内置编码器不可用：%1")
                      .arg(QString::fromLatin1(candidate.codec))},
        });
    }
    return formats;
}

void FormatConverter::updateEntryProgress(int index, double value,
                                          const QVector<int>& jobIndices)
{
    double weightedProgress = 0.0;
    double totalWeight = 0.0;
    bool changed = false;
    {
        QMutexLocker lock(&mutex_);
        if (index < 0 || index >= entries_.size()) {
            return;
        }
        FileEntry& entry = entries_[index];
        const double normalized = std::clamp(value, 0.0, 1.0);
        if (std::abs(entry.progress - normalized) >= 0.005
            || normalized >= 1.0) {
            entry.progress = normalized;
            changed = true;
        }
        if (!changed) {
            return;
        }
        for (const int jobIndex : jobIndices) {
            if (jobIndex < 0 || jobIndex >= entries_.size()) {
                continue;
            }
            const FileEntry& job = entries_.at(jobIndex);
            const double weight = static_cast<double>(std::max<qint64>(
                1, job.durationMs));
            totalWeight += weight;
            weightedProgress += weight * job.progress;
        }
    }
    if (totalWeight > 0.0) {
        progress_.store(weightedProgress / totalWeight,
                        std::memory_order_release);
        emit progressChanged();
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

    cancelFlag_.store(false, std::memory_order_release);
    setProgress(0.0);
    setBusy(true);

    QStringList importRoots;
    for (const QUrl& url : urls) {
        const QFileInfo info(url.toLocalFile());
        if (info.isDir()) {
            const QString root = QDir::cleanPath(info.absoluteFilePath());
            if (!importRoots.contains(root, Qt::CaseInsensitive)) {
                importRoots.append(root);
            }
        }
    }

    auto* discoveryWatcher = new QFutureWatcher<QList<QUrl>>(this);
    discoveryWatcher_ = discoveryWatcher;
    connect(discoveryWatcher, &QFutureWatcher<QList<QUrl>>::finished, this,
            [this, discoveryWatcher, seen = std::move(seen),
             importRoots = std::move(importRoots)]() mutable {
        const QList<QUrl> expandedUrls = discoveryWatcher->result();
        discoveryWatcher_.clear();
        discoveryWatcher->deleteLater();
        if (cancelFlag_.load(std::memory_order_acquire)) {
            setProgress(0.0);
            setBusy(false);
            return;
        }

        auto* watcher = new QFutureWatcher<QList<FileEntry>>(this);
        loadWatcher_ = watcher;
        connect(watcher, &QFutureWatcher<QList<FileEntry>>::finished, this,
                [this, watcher] {
            const QList<FileEntry> discovered = watcher->result();
            if (!cancelFlag_.load(std::memory_order_acquire)) {
                {
                    QMutexLocker lock(&mutex_);
                    entries_.append(discovered);
                }
                if (!discovered.isEmpty()) {
                    emit fileCountChanged();
                    emit filesChanged();
                }
            }
            loadWatcher_.clear();
            setProgress(0.0);
            setBusy(false);
            watcher->deleteLater();
        });

        watcher->setFuture(QtConcurrent::run(
            [expandedUrls, seen = std::move(seen), importRoots, this]() mutable {
        QList<FileEntry> discovered;
        discovered.reserve(expandedUrls.size());
        const int total = expandedUrls.size();
        for (int index = 0; index < total; ++index) {
            if (cancelFlag_.load(std::memory_order_acquire)) {
                break;
            }
            const QString path = expandedUrls.at(index).toLocalFile();
            if (path.isEmpty() || seen.contains(path)) {
                continue;
            }

            FileEntry entry;
            entry.path = path;
            entry.taskId = QDir::cleanPath(path).toCaseFolded();
            const QFileInfo info(path);
            const QString absolutePath = QDir::cleanPath(info.absoluteFilePath());
            for (const QString& root : importRoots) {
                const QString relative = QDir(root).relativeFilePath(absolutePath);
                const bool insideRoot = relative != QStringLiteral("..")
                    && !relative.startsWith(QStringLiteral("../"))
                    && !relative.startsWith(QStringLiteral("..\\"));
                if (insideRoot && root.size() > entry.importRoot.size()) {
                    entry.importRoot = root;
                }
            }
            entry.fileName = info.fileName();
            entry.fileSize = info.size();
            entry.status = FileStatus::Waiting;

            ag_metadata* metadata = nullptr;
            if (ag_metadata_open(path.toUtf8().constData(), &metadata) == AG_OK
                && metadata != nullptr) {
                entry.format = QString::fromUtf8(ag_metadata_format(metadata));
                entry.durationMs = ag_metadata_duration_ms(metadata);
                entry.sampleRate = ag_metadata_sample_rate(metadata);
                entry.bitRate = ag_metadata_bit_rate(metadata);
                entry.channels = ag_metadata_channels(metadata);
                ag_metadata_destroy(metadata);
            }
            if (entry.format.isEmpty()) {
                entry.format = info.suffix().toUpper();
            }
            discovered.append(std::move(entry));
            seen.insert(path);
            progress_.store(static_cast<double>(index + 1) / total,
                            std::memory_order_release);
        }
        return discovered;
    }));
    });
    discoveryWatcher->setFuture(
        agplayer::qt::expandAudioUrlsAsync(urls, true));
}

void FormatConverter::addFolder(const QUrl& folderUrl)
{
    loadFiles({folderUrl});
}

void FormatConverter::addPlaylistPaths(const QStringList& paths)
{
    QList<QUrl> urls;
    urls.reserve(paths.size());
    for (const QString& path : paths) {
        urls.append(QUrl::fromLocalFile(path));
    }
    loadFiles(urls);
}

void FormatConverter::removeChecked()
{
    if (busy()) return;
    const QSet<QString> checked = [&] {
        QSet<QString> ids;
        for (const QString& taskId : taskModel_->taskIds()) {
            for (int row = 0; row < taskModel_->rowCount(); ++row) {
                if (taskModel_->taskIdAt(row) == taskId
                    && taskModel_->taskAt(row)
                           .value(QStringLiteral("checked")).toBool()) {
                    ids.insert(taskId);
                }
            }
        }
        return ids;
    }();
    {
        QMutexLocker lock(&mutex_);
        for (int index = entries_.size() - 1; index >= 0; --index) {
            if (checked.contains(entries_.at(index).taskId)) {
                entries_.removeAt(index);
            }
        }
    }
    emit fileCountChanged();
    emit filesChanged();
}

void FormatConverter::clearFinished()
{
    if (busy()) return;
    {
        QMutexLocker lock(&mutex_);
        for (int index = entries_.size() - 1; index >= 0; --index) {
            const FileStatus status = entries_.at(index).status;
            if (status == FileStatus::Done || status == FileStatus::Error
                || status == FileStatus::Cancelled
                || status == FileStatus::Skipped) {
                entries_.removeAt(index);
            }
        }
    }
    emit fileCountChanged();
    emit filesChanged();
}

QVariantMap FormatConverter::buildPreflight(const QVariantMap& request)
{
    QVariantMap plan = request;
    const QString format = request.value(
        QStringLiteral("outputFormat"), selectedFormat_).toString().toLower();
    plan.insert(QStringLiteral("outputFormat"), format);
    plan.insert(QStringLiteral("taskCount"), checkedCount());
    plan.insert(QStringLiteral("capability"), currentCapability());
    const bool requiresConfirmation =
        request.value(QStringLiteral("requiresConfirmation")).toBool()
        || conflictPolicy_ == QStringLiteral("ask");
    plan.insert(QStringLiteral("requiresConfirmation"), requiresConfirmation);
    plan.insert(QStringLiteral("ready"), checkedCount() > 0
        && currentCapability().value(QStringLiteral("available")).toBool());
    pendingPlan_ = plan;
    emit pendingPlanChanged();
    return plan;
}

void FormatConverter::confirmPendingPlan()
{
    if (pendingPlan_.isEmpty()) return;
    const QVariantMap plan = pendingPlan_;
    pendingPlan_.clear();
    emit pendingPlanChanged();
    QVector<int> checked;
    {
        QMutexLocker lock(&mutex_);
        for (int index = 0; index < entries_.size(); ++index) {
            const QString taskId = entries_.at(index).taskId;
            for (int row = 0; row < taskModel_->rowCount(); ++row) {
                if (taskModel_->taskIdAt(row) == taskId
                    && taskModel_->taskAt(row)
                           .value(QStringLiteral("checked")).toBool()) {
                    checked.append(index);
                    break;
                }
            }
        }
    }
    startJobs(checked,
              plan.value(QStringLiteral("outputFormat"), selectedFormat_).toString(),
              plan.value(QStringLiteral("bitRate"), 192000).toInt(),
              plan.value(QStringLiteral("sampleRate"), 0).toInt(),
              plan.value(QStringLiteral("channels"), 0).toInt(),
              plan.value(QStringLiteral("outputDir")).toString(),
              plan.value(QStringLiteral("keepMetadata"), true).toBool(),
              plan.value(QStringLiteral("volumeNormalize"), false).toBool(),
              plan.value(QStringLiteral("extractAudio"), false).toBool(),
              plan.value(QStringLiteral("keepCover"), false).toBool(),
              plan.value(QStringLiteral("sampleFormat")).toString(),
              plan.value(QStringLiteral("channelLayout")).toString(),
              plan.value(QStringLiteral("audioStreamIndex"), -1).toInt(),
              plan.value(QStringLiteral("preserveDirectories"), false).toBool());
}

void FormatConverter::rejectPendingPlan()
{
    if (pendingPlan_.isEmpty()) return;
    pendingPlan_.clear();
    emit pendingPlanChanged();
}

void FormatConverter::cancelTask(const QString& taskId)
{
    QMutexLocker lock(&mutex_);
    for (int index = 0; index < entries_.size(); ++index) {
        if (entries_.at(index).taskId == taskId) {
            lock.unlock();
            cancelEntry(index);
            return;
        }
    }
}

void FormatConverter::copyText(const QString& text) const
{
    if (QGuiApplication::clipboard() != nullptr) {
        QGuiApplication::clipboard()->setText(text);
    }
}

void FormatConverter::setTaskChecked(const QString& taskId, const bool checked)
{
    taskModel_->setChecked(taskId, checked);
    emit filesChanged();
}

void FormatConverter::setAllVisibleChecked(const bool checked)
{
    filteredTaskModel_->setAllVisibleChecked(checked);
    emit filesChanged();
}

bool FormatConverter::setMetadataEditPlan(const QVariantMap& fields,
                                          const QUrl& coverUrl)
{
    const QString coverMode = fields.value(QStringLiteral("coverMode"),
                                           QStringLiteral("keep")).toString();
    QByteArray coverData;
    QString coverMime;
    if (coverMode == QLatin1String("set")) {
        const QString path = coverUrl.toLocalFile();
        QFile coverFile(path);
        coverMime = cover_mime_type(path);
        if (path.isEmpty() || coverMime.isEmpty() || !coverFile.open(QIODevice::ReadOnly)) {
            emit errorOccurred(tr("无法读取转换模式的封面图片"));
            return false;
        }
        coverData = coverFile.readAll();
        const QSize size = QImageReader(path).size();
        if (coverData.isEmpty() || coverData.size() > 20 * 1024 * 1024
            || !size.isValid() || size.width() > 4096 || size.height() > 4096) {
            emit errorOccurred(tr("转换模式的封面图片无效"));
            return false;
        }
    }
    agplayer::MetadataEditPlan plan = metadata_plan(fields, coverData, coverMime);
    std::string error;
    if (!agplayer::validate_metadata_edit_plan(plan, error)) {
        emit errorOccurred(QString::fromStdString(error));
        return false;
    }
    metadataFields_ = fields;
    metadataCoverData_ = coverData;
    metadataCoverMime_ = coverMime;
    return true;
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
                                           const QString& outputDir,
                                           const QSet<QString>& reservedPaths,
                                           bool overwriteExisting) const
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
    while ((!overwriteExisting && QFileInfo::exists(candidate))
           || QDir::cleanPath(candidate).compare(
                  QDir::cleanPath(inputPath), Qt::CaseInsensitive) == 0
           || reservedPaths.contains(
               QDir::cleanPath(candidate).toCaseFolded())) {
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
    QVector<int> indices;
    {
        QMutexLocker lock(&mutex_);
        indices.resize(entries_.size());
    }
    std::iota(indices.begin(), indices.end(), 0);
    startJobs(indices, outputFormat, bitRate, sampleRate, channels, outputDir,
              keepMetadata, volumeNormalize, extractAudio, false, {}, {}, -1,
              false);
}

void FormatConverter::startSelected(const QVariantList& indices,
                                    const QString& outputFormat,
                                    int bitRate,
                                    int sampleRate,
                                    int channels,
                                    const QString& outputDir,
                                    bool keepMetadata,
                                    bool volumeNormalize,
                                    bool extractAudio)
{
    QVector<int> selected;
    QSet<int> seen;
    int entryCount = 0;
    {
        QMutexLocker lock(&mutex_);
        entryCount = entries_.size();
    }
    selected.reserve(indices.size());
    for (const QVariant& value : indices) {
        bool ok = false;
        const int index = value.toInt(&ok);
        if (ok && index >= 0 && index < entryCount && !seen.contains(index)) {
            selected.push_back(index);
            seen.insert(index);
        }
    }
    startJobs(selected, outputFormat, bitRate, sampleRate, channels, outputDir,
              keepMetadata, volumeNormalize, extractAudio, false, {}, {}, -1,
              false);
}

void FormatConverter::retryFailed(const QString& outputFormat,
                                  int bitRate,
                                  int sampleRate,
                                  int channels,
                                  const QString& outputDir,
                                  bool keepMetadata,
                                  bool volumeNormalize,
                                  bool extractAudio)
{
    QVector<int> failed;
    {
        QMutexLocker lock(&mutex_);
        failed.reserve(entries_.size());
        for (int index = 0; index < entries_.size(); ++index) {
            if (entries_.at(index).status == FileStatus::Error
                || entries_.at(index).status == FileStatus::Cancelled) {
                failed.push_back(index);
            }
        }
    }
    startJobs(failed, outputFormat, bitRate, sampleRate, channels, outputDir,
              keepMetadata, volumeNormalize, extractAudio, false, {}, {}, -1,
              false);
}

void FormatConverter::startJobs(const QVector<int>& jobIndices,
                                const QString& outputFormat,
                                int bitRate,
                                int sampleRate,
                                int channels,
                                const QString& outputDir,
                                bool keepMetadata,
                                bool volumeNormalize,
                                bool extractAudio,
                                bool keepCover,
                                const QString& sampleFormat,
                                const QString& channelLayout,
                                int audioStreamIndex,
                                bool preserveDirectories)
{
    if (busy_.load(std::memory_order_acquire)) {
        return;
    }

    const int fileCount = jobIndices.size();
    if (fileCount == 0) {
        emit errorOccurred(tr("没有可转换的文件"));
        return;
    }
    const QString normalizedFormat = outputFormat.trimmed().toLower();
    bool formatSupported = false;
    QString unsupportedReason;
    for (const QVariant& value : supportedOutputFormats()) {
        const QVariantMap candidate = value.toMap();
        if (candidate.value(QStringLiteral("key")).toString()
            == normalizedFormat) {
            formatSupported = candidate.value(QStringLiteral("available")).toBool();
            unsupportedReason = candidate.value(QStringLiteral("reason")).toString();
            break;
        }
    }
    if (!formatSupported) {
        emit errorOccurred(unsupportedReason.isEmpty()
            ? tr("当前编码器不支持输出格式：%1").arg(outputFormat)
            : unsupportedReason);
        return;
    }
    const bool lossyFormat = normalizedFormat == QStringLiteral("mp3")
        || normalizedFormat == QStringLiteral("aac")
        || normalizedFormat == QStringLiteral("m4a")
        || normalizedFormat == QStringLiteral("ogg")
        || normalizedFormat == QStringLiteral("opus")
        || normalizedFormat == QStringLiteral("wma");
    if (lossyFormat && (bitRate < 8000 || bitRate > 512000)) {
        emit errorOccurred(tr("%1 需要有效的目标码率")
                               .arg(outputFormat.toUpper()));
        return;
    }
    if (sampleRate != 0 && (sampleRate < 8000 || sampleRate > 192000)) {
        emit errorOccurred(tr("采样率必须在 8 kHz 到 192 kHz 之间"));
        return;
    }
    if (channels < 0 || channels > 2) {
        emit errorOccurred(tr("声道数只支持自动、单声道或立体声"));
        return;
    }
    if (normalizedFormat == QStringLiteral("opus")
        && sampleRate != 0 && sampleRate != 48000) {
        emit errorOccurred(
            QStringLiteral("Opus output requires 48 kHz. Confirm the adjustment before conversion."));
        return;
    }
    const int effectiveSampleRate = sampleRate;
    if (!outputDir.isEmpty() && !QDir().mkpath(outputDir)) {
        emit errorOccurred(tr("无法创建输出目录：%1").arg(outputDir));
        return;
    }

    cancelFlag_.store(false, std::memory_order_release);
    {
        QMutexLocker lock(&tokenMutex_);
        for (const int index : jobIndices) {
            cancelledEntries_.remove(index);
        }
    }
    setBusy(true);
    setProgress(0.0);
    setCompletedCount(0);
    setFailedCount(0);

    // Reset all statuses to Waiting before starting.
    {
        QMutexLocker lock(&mutex_);
        for (const int index : jobIndices) {
            if (index >= 0 && index < entries_.size()) {
                entries_[index].status = FileStatus::Waiting;
                entries_[index].progress = 0.0;
                entries_[index].errorMessage.clear();
            }
        }
    }
    emit filesChanged();

    const bool overwriteExisting = overwriteExisting_;
    const QString bitrateMode = bitrateMode_;
    const QString conflictPolicy = conflictPolicy_;
    const QVariantMap metadataFields = metadataFields_;
    const QByteArray metadataCoverData = metadataCoverData_;
    const QString metadataCoverMime = metadataCoverMime_;
    auto* watcher = new QFutureWatcher<void>(this);
    watcher_ = watcher;
    connect(watcher, &QFutureWatcher<void>::finished, this,
        [this, watcher]() {
            watcher->deleteLater();
            watcher_.clear();
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
        [this, outputFormat, bitRate, effectiveSampleRate, channels, outputDir,
         keepMetadata, volumeNormalize, extractAudio, overwriteExisting,
         bitrateMode, conflictPolicy, metadataFields, metadataCoverData,
         metadataCoverMime, keepCover, sampleFormat, channelLayout,
         audioStreamIndex, preserveDirectories,
         jobIndices]() {
            runTranscode(outputFormat, bitRate, effectiveSampleRate, channels,
                         outputDir, keepMetadata, volumeNormalize,
                         extractAudio, overwriteExisting, bitrateMode, conflictPolicy,
                         metadataFields, metadataCoverData, metadataCoverMime,
                         jobIndices, keepCover, sampleFormat, channelLayout,
                         audioStreamIndex, preserveDirectories);
        });
    watcher->setFuture(future);
}

void FormatConverter::runTranscode(const QString& outputFormat,
                                   int bitRate,
                                   int sampleRate,
                                   int channels,
                                   const QString& outputDir,
                                   bool keepMetadata,
                                   bool volumeNormalize,
                                   bool extractAudio,
                                   bool overwriteExisting,
                                   const QString& bitrateMode,
                                   const QString& conflictPolicy,
                                   const QVariantMap& metadataFields,
                                   const QByteArray& metadataCoverData,
                                   const QString& metadataCoverMime,
                                   const QVector<int>& jobIndices,
                                   bool keepCover,
                                   const QString& sampleFormat,
                                   const QString& channelLayout,
                                   int audioStreamIndex,
                                   bool preserveDirectories)
{
    const FormatInfo fi = format_info(outputFormat);
    const QByteArray codecName = QByteArray(fi.codec_name);
    (void)volumeNormalize; // The versioned core request has no normalize field.
    const agplayer::MetadataEditPlan plan = metadata_plan(
        metadataFields, metadataCoverData, metadataCoverMime);

    QVector<QString> inputPaths;
    QVector<QString> importRoots;
    {
        QMutexLocker lock(&mutex_);
        inputPaths.reserve(jobIndices.size());
        importRoots.reserve(jobIndices.size());
        for (const int index : jobIndices) {
            if (index >= 0 && index < entries_.size()) {
                inputPaths.push_back(entries_.at(index).path);
                importRoots.push_back(entries_.at(index).importRoot);
            }
        }
    }

    const int totalJobs = static_cast<int>(inputPaths.size());
    QVector<QString> outputPaths;
    outputPaths.reserve(totalJobs);
    QSet<QString> reservedPaths;
    for (int index = 0; index < inputPaths.size(); ++index) {
        const QString& inputPath = inputPaths.at(index);
        QString effectiveOutputDir = outputDir;
        if (preserveDirectories && !outputDir.isEmpty()
            && index < importRoots.size() && !importRoots.at(index).isEmpty()) {
            const QString relative = QDir(importRoots.at(index))
                .relativeFilePath(inputPath);
            const QString relativeDir = QFileInfo(relative).path();
            const bool safeRelativeDir = relativeDir != QStringLiteral("..")
                && !relativeDir.startsWith(QStringLiteral("../"))
                && !relativeDir.startsWith(QStringLiteral("..\\"));
            if (safeRelativeDir && relativeDir != QStringLiteral(".")) {
                effectiveOutputDir = QDir(outputDir).filePath(relativeDir);
            }
        }
        const QString outputPath = computeOutputPath(
            inputPath, outputFormat, effectiveOutputDir, reservedPaths,
            overwriteExisting || conflictPolicy == QStringLiteral("skip"));
        outputPaths.push_back(outputPath);
        reservedPaths.insert(QDir::cleanPath(outputPath).toCaseFolded());
    }
    {
        QMutexLocker lock(&mutex_);
        for (int index = 0; index < outputPaths.size()
             && index < jobIndices.size(); ++index) {
            const int entryIndex = jobIndices.at(index);
            entries_[entryIndex].outputFormat = outputFormat.toUpper();
            entries_[entryIndex].outputPath = outputPaths.at(index);
            entries_[entryIndex].progress = 0.0;
        }
    }
    emit filesChanged();

    QVector<int> jobs(totalJobs);
    std::iota(jobs.begin(), jobs.end(), 0);
    QThreadPool pool;
    pool.setMaxThreadCount(std::clamp(
        parallelJobs_, 1, std::max(1, QThread::idealThreadCount())));

    QtConcurrent::blockingMap(&pool, jobs, [&](int i) {
        if (i >= inputPaths.size()) {
            return;
        }
        const int entryIndex = jobIndices.at(i);
        const auto complete = [this, entryIndex, totalJobs](
                                  FileStatus status,
                                         const QString& error) {
            setEntryError(entryIndex, error);
            setEntryStatus(entryIndex, status);
            if (status == FileStatus::Error) {
                failedCount_.fetch_add(1, std::memory_order_acq_rel);
                emit failedCountChanged();
            }
            const int completed =
                completedCount_.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (totalJobs > 0) {
                setProgress(static_cast<double>(completed) / totalJobs);
            }
        };
        const auto entryCancelled = [this, entryIndex]() {
            QMutexLocker lock(&tokenMutex_);
            return cancelledEntries_.contains(entryIndex);
        };
        if (cancelFlag_.load(std::memory_order_acquire)) {
            complete(FileStatus::Cancelled, tr("已取消"));
            return;
        }
        if (entryCancelled()) {
            complete(FileStatus::Cancelled, tr("已取消"));
            return;
        }

        const QString& inputPath = inputPaths.at(i);
        const QString& outputPath = outputPaths.at(i);
        if (!QDir().mkpath(QFileInfo(outputPath).absolutePath())) {
            complete(FileStatus::Error,
                     tr("无法创建输出目录：%1")
                         .arg(QFileInfo(outputPath).absolutePath()));
            return;
        }
        if (conflictPolicy == QStringLiteral("skip")
            && QFileInfo::exists(outputPath)) {
            setEntryStatus(entryIndex, FileStatus::Skipped);
            const int completed = completedCount_.fetch_add(
                1, std::memory_order_acq_rel) + 1;
            if (totalJobs > 0) {
                setProgress(static_cast<double>(completed) / totalJobs);
            }
            return;
        }
        const QString stagedPath = staging_path_for(outputPath);

        setEntryStatus(entryIndex, FileStatus::Converting);
        if (is_video_file(inputPath) && !extractAudio) {
            complete(FileStatus::Error,
                     tr("视频文件需要启用“从视频中提取音频”"));
            return;
        }
        ag_cancel_token* token = ag_cancel_token_create();
        {
            QMutexLocker lock(&tokenMutex_);
            activeTokens_.insert(entryIndex, token);
        }
        if (cancelFlag_.load(std::memory_order_acquire)) {
            ag_cancel_token_cancel(token);
        }
        const QByteArray inputUtf8 = inputPath.toUtf8();
        const QByteArray outputUtf8 = stagedPath.toUtf8();
        const QByteArray muxerName(fi.muxer_name);
        const QByteArray sampleFormatUtf8 = sampleFormat.toUtf8();
        QString resolvedLayout = channelLayout.trimmed().toLower();
        if (resolvedLayout.isEmpty()) {
            if (channels == 1) resolvedLayout = QStringLiteral("mono");
            if (channels == 2) resolvedLayout = QStringLiteral("stereo");
        }
        const QByteArray channelLayoutUtf8 = resolvedLayout.toUtf8();

        struct EntryProgressContext {
            FormatConverter* converter;
            int entryIndex;
            const QVector<int>* jobIndices;
        } progressContext{this, entryIndex, &jobIndices};
        const auto progressCallback = [](float progress, void* userData) {
            auto* context = static_cast<EntryProgressContext*>(userData);
            context->converter->updateEntryProgress(
                context->entryIndex, progress, *context->jobIndices);
        };

        ag_transcode_request_v2 request{};
        request.struct_size = sizeof(request);
        request.api_version = AG_TRANSCODE_REQUEST_V2_VERSION;
        request.output_path = outputUtf8.constData();
        request.muxer_name = muxerName.constData();
        request.codec_name = codecName.isEmpty() ? nullptr : codecName.constData();
        request.bit_rate = static_cast<long long>(bitRate);
        request.sample_rate = sampleRate;
        request.channel_layout = channelLayoutUtf8.isEmpty()
            ? nullptr : channelLayoutUtf8.constData();
        request.sample_format = sampleFormatUtf8.isEmpty()
            ? nullptr : sampleFormatUtf8.constData();
        request.audio_stream_index = audioStreamIndex;
        request.keep_metadata = keepMetadata ? 1 : 0;
        request.keep_cover = keepCover ? 1 : 0;
        request.bitrate_mode = bitrateMode == QStringLiteral("vbr") ? 1 : 0;
        request.quality = 75;
        const ag_result result = ag_transcode_v2(
            inputUtf8.constData(), &request, token, progressCallback,
            &progressContext);

        {
            QMutexLocker lock(&tokenMutex_);
            activeTokens_.remove(entryIndex);
        }
        ag_cancel_token_destroy(token);

        if (cancelFlag_.load(std::memory_order_acquire) || entryCancelled()) {
            QFile::remove(stagedPath);
            complete(FileStatus::Cancelled, tr("已取消"));
            return;
        } else if (result == AG_OK) {
            if (!validate_audio_output(stagedPath)) {
                QFile::remove(stagedPath);
                complete(FileStatus::Error,
                         tr("转换结果无法重新打开或不包含有效音频"));
                return;
            }
            if (!plan.fields.empty()
                || plan.cover_action != agplayer::CoverAction::Keep) {
                agplayer::MetadataFileResult metadataResult;
                const ag_result metadataWrite = agplayer::write_metadata_plan(
                    stagedPath.toUtf8().toStdString(), plan, metadataResult);
                if (metadataWrite != AG_OK) {
                    QFile::remove(stagedPath);
                    QFile::remove(stagedPath + QStringLiteral(".agbak"));
                    complete(FileStatus::Error,
                             tr("转换元数据验证失败，未发布输出文件：%1")
                                 .arg(QString::fromStdString(metadataResult.message)));
                    return;
                }
                QFile::remove(stagedPath + QStringLiteral(".agbak"));
            }
            if (!commit_staged_output(stagedPath, outputPath,
                                      overwriteExisting)) {
                QFile::remove(stagedPath);
                complete(FileStatus::Error,
                         tr("无法安全写入输出文件：%1").arg(outputPath));
                return;
            }
            setEntryStatus(entryIndex, FileStatus::Done);
        } else if (result == AG_CANCELLED) {
            QFile::remove(stagedPath);
            complete(FileStatus::Cancelled, tr("已取消"));
            return;
        } else {
            QFile::remove(stagedPath);
            const QString coreError = QString::fromUtf8(ag_last_error()).trimmed();
            const QString detail = coreError.isEmpty()
                ? RuntimeLog::mapResult(result) : coreError;
            RuntimeLog::log(result, QStringLiteral("FormatConverter"),
                            QStringLiteral("input=%1 output=%2 format=%3: %4")
                                .arg(inputPath, outputPath, outputFormat, detail));
            complete(FileStatus::Error,
                     tr("转换失败：%1").arg(detail));
            return;
        }

        const int completed = completedCount_.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (totalJobs > 0) {
            setProgress(static_cast<double>(completed) / totalJobs);
        }
    });
}

void FormatConverter::cancel()
{
    cancelFlag_.store(true, std::memory_order_release);
    QMutexLocker lock(&tokenMutex_);
    for (ag_cancel_token* token : activeTokens_) {
        ag_cancel_token_cancel(token);
    }
}

void FormatConverter::cancelEntry(int index)
{
    QMutexLocker lock(&tokenMutex_);
    cancelledEntries_.insert(index);
    if (ag_cancel_token* token = activeTokens_.value(index, nullptr);
        token != nullptr) {
        ag_cancel_token_cancel(token);
    }
}
