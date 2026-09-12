#include "format_converter.hpp"

#include "audio_file_discovery.hpp"
#include "format_conversion_plan.hpp"
#include "format_conversion_filter_model.hpp"
#include "format_conversion_task_model.hpp"
#include "transcode_capability.hpp"
#include "transcode_probe.hpp"
#include "metadata_writer.hpp"
#include "transcoder.hpp"
#include "runtime_log.hpp"

#include <agplayer/c_api.h>

#include <QDir>
#include <QClipboard>
#include <QDateTime>
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
#include <vector>

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
    if (key == QLatin1String("customTag")) return agplayer::CanonicalField::CustomTag;
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
        QStringLiteral("customTag"), QStringLiteral("date"), QStringLiteral("composer"),
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

QString canonical_codec_name(QString codec)
{
    if (codec == QStringLiteral("libmp3lame")) return QStringLiteral("mp3");
    if (codec == QStringLiteral("libopus")) return QStringLiteral("opus");
    if (codec == QStringLiteral("libvorbis")) return QStringLiteral("vorbis");
    return codec;
}

bool container_matches(const QString& muxer, const QString& readback)
{
    if (muxer == QStringLiteral("ipod")) {
        return readback.contains(QStringLiteral("mov"), Qt::CaseInsensitive)
            || readback.contains(QStringLiteral("m4a"), Qt::CaseInsensitive);
    }
    if (muxer == QStringLiteral("adts")) {
        return readback.contains(QStringLiteral("aac"), Qt::CaseInsensitive);
    }
    return readback.contains(muxer, Qt::CaseInsensitive);
}

QString bit_depth_key(const int bits, const QString& sampleFormat)
{
    if (bits == 32 && sampleFormat.contains(QStringLiteral("flt"))) {
        return QStringLiteral("flt");
    }
    return bits > 0 ? QStringLiteral("s%1").arg(bits) : QStringLiteral("unknown");
}

int bit_depth_bits(const QString& key)
{
    if (key == QStringLiteral("s16")) return 16;
    if (key == QStringLiteral("s24")) return 24;
    if (key == QStringLiteral("s32") || key == QStringLiteral("flt")) return 32;
    return 0;
}

bool validate_audio_output_impl(const QString& path,
                                const QVariantMap& resolvedProfile,
                                QString& error)
{
    ag_metadata* metadata = nullptr;
    const QByteArray utf8 = path.toUtf8();
    const ag_result probe_result = ag_metadata_open(utf8.constData(), &metadata);
    if (probe_result != AG_OK || metadata == nullptr) {
        error = QStringLiteral("output cannot be reopened");
        return false;
    }
    const bool validDuration = ag_metadata_duration_ms(metadata) > 0;
    const int readbackRate = ag_metadata_sample_rate(metadata);
    const int readbackChannels = ag_metadata_channels(metadata);
    ag_metadata_destroy(metadata);
    if (!validDuration || readbackRate <= 0 || readbackChannels <= 0) {
        error = QStringLiteral("output duration/rate/channels are invalid");
        return false;
    }
    if (resolvedProfile.isEmpty()) return true;

    agplayer::MediaProbe readback;
    std::string probeError;
    if (agplayer::probe_transcode_input(utf8.toStdString(), readback,
                                        probeError) != AG_OK
        || readback.audio_streams.size() != 1U) {
        error = QStringLiteral("output stream readback failed: %1")
                    .arg(QString::fromStdString(probeError));
        return false;
    }
    const agplayer::AudioStreamProbe& audio = readback.audio_streams.front();
    const int expectedRate = resolvedProfile.value(
        QStringLiteral("sampleRate")).toInt();
    const QString expectedLayout = resolvedProfile.value(
        QStringLiteral("channelLayout")).toString();
    const QString expectedCodec = canonical_codec_name(resolvedProfile.value(
        QStringLiteral("codec")).toString());
    const QString expectedMuxer = resolvedProfile.value(
        QStringLiteral("muxer")).toString();
    const QString expectedSampleFormat = resolvedProfile.value(
        QStringLiteral("sampleFormat")).toString();
    const QString expectedBitDepth = resolvedProfile.value(
        QStringLiteral("bitDepth")).toString();
    const QString actualLayout = QString::fromStdString(audio.channel_layout);
    const QString actualCodec = QString::fromStdString(audio.codec);
    const QString actualMuxer = QString::fromStdString(readback.container);
    const QString actualSampleFormat = QString::fromStdString(audio.sample_format);
    if (expectedRate > 0 && audio.sample_rate != expectedRate) {
        error = QStringLiteral("resolved profile sampleRate mismatch "
                               "(expected=%1, actual=%2)")
                    .arg(expectedRate).arg(audio.sample_rate);
        return false;
    }
    if (!expectedLayout.isEmpty() && actualLayout != expectedLayout) {
        error = QStringLiteral("resolved profile channelLayout mismatch "
                               "(expected=%1, actual=%2)")
                    .arg(expectedLayout, actualLayout);
        return false;
    }
    if (!expectedCodec.isEmpty() && actualCodec != expectedCodec) {
        error = QStringLiteral("resolved profile codec mismatch "
                               "(expected=%1, actual=%2)")
                    .arg(expectedCodec, actualCodec);
        return false;
    }
    if (!expectedMuxer.isEmpty()
        && !container_matches(expectedMuxer, actualMuxer)) {
        error = QStringLiteral("resolved profile muxer mismatch "
                               "(expected=%1, actual=%2)")
                    .arg(expectedMuxer, actualMuxer);
        return false;
    }
    // For lossy codecs this profile value selects the encoder input format,
    // while probe readback exposes the decoder output format; they are not the
    // same contract (for example Opus s16 input decodes as fltp). Lossless
    // depth profiles freeze both values and can be compared directly.
    if (!expectedBitDepth.isEmpty() && !expectedSampleFormat.isEmpty()
        && actualSampleFormat != expectedSampleFormat) {
        error = QStringLiteral("resolved profile sampleFormat mismatch "
                               "(expected=%1, actual=%2)")
                    .arg(expectedSampleFormat, actualSampleFormat);
        return false;
    }
    const int expectedBits = bit_depth_bits(expectedBitDepth);
    if (expectedBits > 0 && audio.bits_per_sample != expectedBits) {
        error = QStringLiteral("resolved profile bitDepth mismatch "
                               "(expected=%1, actual=%2)")
                    .arg(expectedBitDepth,
                         bit_depth_key(audio.bits_per_sample,
                                       actualSampleFormat));
        return false;
    }
    return true;
}

QString prepare_output_directory(QString& path)
{
    path = path.trimmed();
    if (path.isEmpty()) {
        return {};
    }
    path = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (!QDir().mkpath(path)) {
        return QStringLiteral("Invalid outputDir: cannot create %1")
            .arg(path);
    }
    const QFileInfo info(path);
    if (!info.isDir()) {
        return QStringLiteral("Invalid outputDir: not a directory: %1")
            .arg(path);
    }
    if (!info.isWritable()) {
        return QStringLiteral("Invalid outputDir: not writable: %1")
            .arg(path);
    }
    return {};
}

} // namespace

bool format_converter_detail::validate_audio_output(
    const QString& path, const QVariantMap& resolvedProfile, QString& error)
{
    return validate_audio_output_impl(path, resolvedProfile, error);
}

bool format_converter_detail::commit_staged_output(
    const QString& stagedPath,
    const QString& finalPath,
    const OutputCommitMode mode,
    const std::function<void()>& beforeCommit)
{
    if (beforeCommit) {
        beforeCommit();
    }
#ifdef Q_OS_WIN
    const std::wstring staged = QDir::toNativeSeparators(stagedPath).toStdWString();
    const std::wstring final = QDir::toNativeSeparators(finalPath).toStdWString();
    DWORD flags = MOVEFILE_WRITE_THROUGH;
    if (mode == OutputCommitMode::Overwrite) {
        flags |= MOVEFILE_REPLACE_EXISTING;
    }
    return MoveFileExW(staged.c_str(), final.c_str(), flags) != FALSE;
#else
    std::error_code error;
    if (mode == OutputCommitMode::CreateNoReplace) {
        std::filesystem::create_hard_link(stagedPath.toStdString(),
                                          finalPath.toStdString(), error);
        if (error) {
            return false;
        }
        std::filesystem::remove(stagedPath.toStdString(), error);
        return true;
    }
    std::filesystem::rename(stagedPath.toStdString(), finalPath.toStdString(),
                            error);
    return !error;
#endif
}

namespace {

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
    if (f == QStringLiteral("aiff"))
        return {"pcm_s16be", "aiff", "aiff"};
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

QVariantList bitrate_choices_for(const QString& format)
{
    if (format == QStringLiteral("opus")) {
        return {64000, 96000, 128000, 160000, 192000, 256000, 320000};
    }
    if (format == QStringLiteral("ogg")) {
        return {96000, 128000, 192000, 256000, 320000};
    }
    if (format == QStringLiteral("mp3")
        || format == QStringLiteral("aac")) {
        return {128000, 192000, 256000, 320000};
    }
    return {};
}

QVariantMap preset(const QString& key, const QString& label, int bitRate,
                   const QString& bitrateMode, int sampleRate, int quality)
{
    return {{QStringLiteral("key"), key},
            {QStringLiteral("label"), label},
            {QStringLiteral("bitRate"), bitRate},
            {QStringLiteral("bitrateMode"), bitrateMode},
            {QStringLiteral("sampleRate"), sampleRate},
            {QStringLiteral("quality"), quality}};
}

QVariantList presets_for(const QString& format, bool lossy)
{
    if (!lossy) {
        return {preset(QStringLiteral("recommended"),
                       QStringLiteral("推荐（保留原始参数）"), 0, {}, 0, 100),
                preset(QStringLiteral("custom"), QStringLiteral("自定义"),
                       0, {}, 0, 100)};
    }
    if (format == QStringLiteral("opus")) {
        return {preset(QStringLiteral("recommended"), QStringLiteral("推荐"),
                       320000, QStringLiteral("vbr"), 48000, 85),
                preset(QStringLiteral("high"), QStringLiteral("高质量"),
                       320000, QStringLiteral("vbr"), 48000, 95),
                preset(QStringLiteral("compatible"), QStringLiteral("兼容"),
                       128000, QStringLiteral("cbr"), 48000, 70),
                preset(QStringLiteral("custom"), QStringLiteral("自定义"),
                       320000, QStringLiteral("vbr"), 48000, 85)};
    }
    if (format == QStringLiteral("aac")) {
        return {preset(QStringLiteral("recommended"), QStringLiteral("推荐"),
                       256000, QStringLiteral("vbr"), 0, 85),
                preset(QStringLiteral("high"), QStringLiteral("高质量"),
                       320000, QStringLiteral("vbr"), 0, 95),
                preset(QStringLiteral("compatible"), QStringLiteral("兼容"),
                       192000, QStringLiteral("cbr"), 44100, 70),
                preset(QStringLiteral("custom"), QStringLiteral("自定义"),
                       256000, QStringLiteral("vbr"), 0, 85)};
    }
    if (format == QStringLiteral("ogg")) {
        return {preset(QStringLiteral("recommended"), QStringLiteral("推荐"),
                       192000, QStringLiteral("vbr"), 0, 85),
                preset(QStringLiteral("high"), QStringLiteral("高质量"),
                       320000, QStringLiteral("vbr"), 0, 95),
                preset(QStringLiteral("compatible"), QStringLiteral("兼容"),
                       128000, QStringLiteral("cbr"), 44100, 70),
                preset(QStringLiteral("custom"), QStringLiteral("自定义"),
                       192000, QStringLiteral("vbr"), 0, 85)};
    }
    return {preset(QStringLiteral("recommended"), QStringLiteral("推荐"),
                   320000, QStringLiteral("cbr"), 0, 85),
            preset(QStringLiteral("high"), QStringLiteral("高质量"),
                   320000, QStringLiteral("vbr"), 0, 95),
            preset(QStringLiteral("compatible"), QStringLiteral("兼容"),
                   192000, QStringLiteral("cbr"), 44100, 70),
            preset(QStringLiteral("custom"), QStringLiteral("自定义"),
                   320000, QStringLiteral("cbr"), 0, 85)};
}

bool list_contains_string(const QVariantList& values, const QString& needle,
                          const QString& valueKey = {})
{
    return std::any_of(values.cbegin(), values.cend(),
                       [&needle, &valueKey](const QVariant& value) {
        return (valueKey.isEmpty() ? value.toString()
                                   : value.toMap().value(valueKey).toString())
            == needle;
    });
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

QString canonical_path_value(const QString& path)
{
    const QFileInfo info(path);
    QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty()) canonical = info.absoluteFilePath();
    return QDir::cleanPath(canonical);
}

QString normalized_path_key(const QString& path)
{
    QString key = canonical_path_value(path);
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
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
    const int bounded = std::clamp(value, 1, 10);
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
    if (!normalized.isEmpty()
        && normalized != QStringLiteral("cbr")
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

int FormatConverter::doneCount() const noexcept
{
    return doneCount_.load(std::memory_order_acquire);
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
        map[QStringLiteral("taskId")] = entry.taskId;
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
        map[QStringLiteral("resolvedProfile")] = entry.resolvedProfile;
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

void FormatConverter::setDoneCount(int value)
{
    doneCount_.store(value, std::memory_order_release);
    emit doneCountChanged();
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
    if (!outputCapabilitiesCache_.isEmpty()) {
        return outputCapabilitiesCache_;
    }
    struct Candidate {
        const char* key;
        const char* label;
        const char* codec;
        const char* encoderLabel;
    };
    static constexpr Candidate candidates[] = {
        {"mp3", "MP3", "libmp3lame", "LAME MP3"},
        {"flac", "FLAC", "flac", "FLAC"},
        {"wav", "WAV", "pcm_s16le", "PCM"},
        {"aac", "AAC", "aac", "AAC"},
        {"opus", "Opus", "libopus", "libopus"},
        {"ogg", "OGG", "libvorbis", "Vorbis"},
        {"alac", "ALAC", "alac", "ALAC"},
        {"aiff", "AIFF", "pcm_s16be", "PCM"},
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
            QVariantList qualityChoices;
            for (const int value : capability.quality_choices)
                qualityChoices.append(value);
            QVariantList sampleRateChoices;
            for (const int value : capability.sample_rate_choices)
                sampleRateChoices.append(value);
            QVariantList bitRateChoices;
            for (const int value : capability.bit_rate_choices)
                bitRateChoices.append(value);
            QVariantList bitDepths;
            for (const agplayer::TranscodeOptionChoice& depth
                 : capability.bit_depth_choices) {
                bitDepths.append(QVariantMap{
                    {QStringLiteral("key"), QString::fromStdString(depth.key)},
                    {QStringLiteral("label"), QString::fromStdString(depth.label)},
                    {QStringLiteral("default"), depth.is_default}});
            }
            const QString formatKey = QString::fromStdString(capability.key);
            actualFormats.append(QVariantMap{
                {QStringLiteral("key"), formatKey},
                {QStringLiteral("label"), QString::fromStdString(capability.label)},
                {QStringLiteral("codec"), QString::fromStdString(capability.codec_name)},
                {QStringLiteral("encoderLabel"), QString::fromStdString(capability.codec_name)},
                {QStringLiteral("muxer"), QString::fromStdString(capability.muxer_name)},
                {QStringLiteral("outputExtension"),
                 QString::fromStdString(capability.output_extension)},
                {QStringLiteral("available"), capability.available},
                {QStringLiteral("reason"), QString::fromStdString(capability.unavailable_reason)},
                {QStringLiteral("lossy"), capability.lossy},
                {QStringLiteral("supportsMetadata"), capability.supports_metadata},
                {QStringLiteral("supportsCover"), capability.supports_cover},
                {QStringLiteral("parameterKind"),
                 QString::fromStdString(capability.parameter_kind)},
                {QStringLiteral("qualityChoices"), qualityChoices},
                {QStringLiteral("defaultQuality"), capability.default_quality},
                {QStringLiteral("bitDepths"), bitDepths},
                {QStringLiteral("sampleRates"), sampleRates},
                {QStringLiteral("sampleRateChoices"), sampleRateChoices},
                {QStringLiteral("sampleFormats"), sampleFormats},
                {QStringLiteral("channelLayouts"), channelLayouts},
                {QStringLiteral("bitrateModes"), bitrateModes},
                {QStringLiteral("bitRateChoices"), bitRateChoices},
                {QStringLiteral("defaultBitRate"), capability.default_bit_rate},
                {QStringLiteral("bitRates"), bitrate_choices_for(formatKey)},
                {QStringLiteral("presets"),
                 presets_for(formatKey, capability.lossy)}});
        }
        outputCapabilitiesCache_ = actualFormats;
        return outputCapabilitiesCache_;
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
    outputCapabilitiesCache_ = formats;
    return outputCapabilitiesCache_;
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
            seen.insert(normalized_path_key(entry.canonicalPath));
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
            const QString canonicalPath = canonical_path_value(path);
            const QString canonicalKey = normalized_path_key(canonicalPath);
            if (path.isEmpty() || canonicalKey.isEmpty()
                || seen.contains(canonicalKey)) {
                continue;
            }
            seen.insert(canonicalKey);

            FileEntry entry;
            entry.path = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
            entry.taskId = canonicalKey;
            entry.importInstanceId = QUuid::createUuid().toString(
                QUuid::WithoutBraces);
            const QFileInfo info(path);
            const QString absolutePath = QDir::cleanPath(info.absoluteFilePath());
            entry.canonicalPath = canonicalPath;
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
            entry.sourceLastModifiedMs = info.lastModified().toMSecsSinceEpoch();
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
            agplayer::MediaProbe probe;
            std::string probeDetail;
            if (agplayer::probe_transcode_input(
                    path.toUtf8().toStdString(), probe, probeDetail) == AG_OK) {
                entry.probeContainer = QString::fromStdString(probe.container);
                entry.probeIsVideo = probe.is_video || is_video_file(path);
                entry.probeHasCover = probe.has_cover;
                entry.audioStreams.reserve(
                    static_cast<qsizetype>(probe.audio_streams.size()));
                for (const agplayer::AudioStreamProbe& audio
                     : probe.audio_streams) {
                    entry.audioStreams.append({
                        audio.stream_index,
                        QString::fromStdString(audio.codec),
                        QString::fromStdString(audio.language),
                        QString::fromStdString(audio.title),
                        audio.is_default,
                        audio.sample_rate,
                        QString::fromStdString(audio.sample_format),
                        QString::fromStdString(audio.channel_layout),
                        audio.bit_rate,
                        audio.duration_ms,
                        audio.bits_per_sample});
                }
            } else {
                entry.probeError = probeDetail.empty()
                    ? QStringLiteral("Failed to probe input")
                    : QString::fromStdString(probeDetail);
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
        for (int row = 0; row < taskModel_->rowCount(); ++row) {
            if (taskModel_->data(taskModel_->index(row, 0),
                                 FormatConversionTaskModel::CheckedRole).toBool()) {
                ids.insert(taskModel_->taskIdAt(row));
            }
        }
        return ids;
    }();
    {
        QMutexLocker lock(&mutex_);
        entries_.removeIf([&checked](const FileEntry& entry) {
            return checked.contains(entry.taskId);
        });
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
    const QString format = request.value(QStringLiteral("outputFormat"),
                                         selectedFormat_)
                               .toString().trimmed().toLower();
    const QString requestedConflictPolicy = request.value(
        QStringLiteral("conflictPolicy"), conflictPolicy_)
                                                .toString().trimmed().toLower();
    QVariantMap plan = request;
    plan.insert(QStringLiteral("outputFormat"), format);
    plan.insert(QStringLiteral("conflictPolicy"), requestedConflictPolicy);
    QVariantMap capability;
    for (const QVariant& candidate : supportedOutputFormats()) {
        const QVariantMap candidateMap = candidate.toMap();
        if (candidateMap.value(QStringLiteral("key")).toString() == format) {
            capability = candidateMap;
            break;
        }
    }
    plan.insert(QStringLiteral("capability"), capability);

    const bool hasExplicitPreset = request.contains(QStringLiteral("preset"));
    const QString presetKey = request.value(
        QStringLiteral("preset"), QStringLiteral("recommended"))
                                  .toString().trimmed().toLower();
    const QVariantList availablePresets = capability.value(
        QStringLiteral("presets")).toList();
    QVariantMap selectedPreset;
    for (const QVariant& value : availablePresets) {
        const QVariantMap candidate = value.toMap();
        if (candidate.value(QStringLiteral("key")).toString() == presetKey) {
            selectedPreset = candidate;
            break;
        }
    }
    if (hasExplicitPreset && presetKey != QStringLiteral("custom")
        && !selectedPreset.isEmpty()) {
        for (const QString& key : {QStringLiteral("bitRate"),
                                   QStringLiteral("bitrateMode"),
                                   QStringLiteral("sampleRate"),
                                   QStringLiteral("quality")}) {
            plan.insert(key, selectedPreset.value(key));
        }
    }

    const auto failPreflight = [this, &plan](const QString& reason) {
        plan.insert(QStringLiteral("tasks"), QVariantList{});
        plan.insert(QStringLiteral("taskCount"), 0);
        plan.insert(QStringLiteral("conflicts"), QVariantList{});
        plan.insert(QStringLiteral("conflictCount"), 0);
        plan.insert(QStringLiteral("requiresConfirmation"), false);
        plan.insert(QStringLiteral("ready"), false);
        plan.insert(QStringLiteral("reason"), reason);
        plan.insert(QStringLiteral("error"), reason);
        pendingPlan_.clear();
        pendingRequest_.clear();
        pendingJobs_.clear();
        emit pendingPlanChanged();
        emit errorOccurred(reason);
        return plan;
    };

    QSet<QString> checkedIds;
    for (int row = 0; row < taskModel_->rowCount(); ++row) {
        if (taskModel_->taskAt(row).value(QStringLiteral("checked")).toBool()) {
            checkedIds.insert(taskModel_->taskIdAt(row));
        }
    }
    QList<FileEntry> selectedEntries;
    {
        QMutexLocker lock(&mutex_);
        for (const FileEntry& entry : entries_) {
            if (checkedIds.contains(entry.taskId)) {
                selectedEntries.append(entry);
            }
        }
    }
    if (selectedEntries.isEmpty()) {
        return failPreflight(tr("没有已选择的转换任务"));
    }

    if (capability.isEmpty()) {
        return failPreflight(QStringLiteral("Invalid outputFormat: %1").arg(format));
    }
    if (!capability.value(QStringLiteral("available")).toBool()) {
        const QString reason = capability.value(QStringLiteral("reason")).toString();
        return failPreflight(reason.isEmpty()
            ? QStringLiteral("Unavailable outputFormat: %1").arg(format)
            : reason);
    }

    FormatConflictPolicy conflictPolicy = FormatConflictPolicy::AutoNumber;
    if (requestedConflictPolicy == QStringLiteral("auto-number")) {
        conflictPolicy = FormatConflictPolicy::AutoNumber;
    } else if (requestedConflictPolicy == QStringLiteral("skip")) {
        conflictPolicy = FormatConflictPolicy::Skip;
    } else if (requestedConflictPolicy == QStringLiteral("overwrite")) {
        conflictPolicy = FormatConflictPolicy::Overwrite;
    } else if (requestedConflictPolicy == QStringLiteral("ask")) {
        conflictPolicy = FormatConflictPolicy::Ask;
    } else {
        return failPreflight(QStringLiteral("Invalid conflictPolicy: %1")
                                 .arg(requestedConflictPolicy));
    }

    FormatConversionRequest conversionRequest;
    conversionRequest.formatKey = format;
    conversionRequest.codecName = request.value(
        QStringLiteral("codecName")).toString();
    conversionRequest.bitrateMode = plan.value(
        QStringLiteral("bitrateMode"), bitrateMode_)
                                         .toString().trimmed().toLower();
    const QString requestedBitrateMode = conversionRequest.bitrateMode;
    const QVariantList supportedBitrateModes = capability.value(
        QStringLiteral("bitrateModes")).toList();
    QSet<QString> supportedBitrateModeKeys;
    for (const QVariant& mode : supportedBitrateModes) {
        supportedBitrateModeKeys.insert(
            mode.toMap().value(QStringLiteral("key")).toString());
    }
    const bool resolvesUnusedBitrateMode = supportedBitrateModeKeys.isEmpty();
    if (resolvesUnusedBitrateMode) {
        conversionRequest.bitrateMode.clear();
    } else if (conversionRequest.bitrateMode != QStringLiteral("cbr")
               && conversionRequest.bitrateMode != QStringLiteral("vbr")) {
        return failPreflight(QStringLiteral("Invalid bitrateMode: %1")
                                 .arg(conversionRequest.bitrateMode));
    } else if (!supportedBitrateModeKeys.contains(
                   conversionRequest.bitrateMode)) {
        return failPreflight(QStringLiteral("Unsupported bitrateMode: %1")
                                 .arg(conversionRequest.bitrateMode));
    }
    bool validNumber = false;
    conversionRequest.bitrate = plan.value(
        QStringLiteral("bitRate"),
        capability.value(QStringLiteral("defaultBitRate"), 0))
                                     .toLongLong(&validNumber);
    if (!validNumber || conversionRequest.bitrate < 0
        || conversionRequest.bitrate > 512000) {
        return failPreflight(
            QStringLiteral("Invalid bitRate: expected 0..512000"));
    }
    const QString parameterKind = capability.value(
        QStringLiteral("parameterKind")).toString();
    const int defaultQuality = (parameterKind == QStringLiteral("quality")
                                || parameterKind == QStringLiteral("compression"))
        ? capability.value(QStringLiteral("defaultQuality")).toInt() : 75;
    conversionRequest.quality = plan.value(
        QStringLiteral("quality"), defaultQuality).toInt(&validNumber);
    if (!validNumber || conversionRequest.quality < 0
        || conversionRequest.quality > 100) {
        return failPreflight(QStringLiteral("Invalid quality: expected 0..100"));
    }
    const int requestedSampleRate = plan.value(
        QStringLiteral("sampleRate"), 0).toInt(&validNumber);
    if (!validNumber || requestedSampleRate < 0
        || requestedSampleRate > 384000) {
        return failPreflight(
            QStringLiteral("Invalid sampleRate '%1': expected 0..384000")
                .arg(plan.value(QStringLiteral("sampleRate")).toString()));
    }
    conversionRequest.sampleRate = requestedSampleRate;
    const bool adjustsOpusSampleRate = format == QStringLiteral("opus")
        && requestedSampleRate != 0 && requestedSampleRate != 48000;
    if (adjustsOpusSampleRate) {
        conversionRequest.sampleRate = 48000;
    }
    conversionRequest.channelLayout = request.value(
        QStringLiteral("channelLayout")).toString().trimmed().toLower();
    if (!conversionRequest.channelLayout.isEmpty()
        && conversionRequest.channelLayout != QStringLiteral("mono")
        && conversionRequest.channelLayout != QStringLiteral("stereo")) {
        return failPreflight(QStringLiteral("Invalid channelLayout: %1")
                                 .arg(conversionRequest.channelLayout));
    }
    if (request.contains(QStringLiteral("channels"))) {
        const int channels = request.value(QStringLiteral("channels"))
                                 .toInt(&validNumber);
        if (!validNumber || channels < 0 || channels > 2) {
            return failPreflight(
                QStringLiteral("Invalid channels: expected 0, 1, or 2"));
        }
        if (conversionRequest.channelLayout.isEmpty()) {
            if (channels == 1) {
                conversionRequest.channelLayout = QStringLiteral("mono");
            } else if (channels == 2) {
                conversionRequest.channelLayout = QStringLiteral("stereo");
            }
        }
    }
    conversionRequest.sampleFormat = request.value(
        QStringLiteral("sampleFormat")).toString().trimmed().toLower();
    conversionRequest.bitDepth = request.value(
        QStringLiteral("bitDepth")).toString().trimmed().toLower();
    const QVariantList supportedSampleFormats = capability.value(
        QStringLiteral("sampleFormats")).toList();
    if (!conversionRequest.sampleFormat.isEmpty()
        && !supportedSampleFormats.contains(conversionRequest.sampleFormat)) {
        return failPreflight(QStringLiteral("Invalid sampleFormat: %1")
                                 .arg(conversionRequest.sampleFormat));
    }
    const QVariantList supportedBitDepths = capability.value(
        QStringLiteral("bitDepths")).toList();
    if (!conversionRequest.bitDepth.isEmpty()) {
        bool supportedDepth = false;
        for (const QVariant& value : supportedBitDepths) {
            if (value.toMap().value(QStringLiteral("key")).toString()
                == conversionRequest.bitDepth) {
                supportedDepth = true;
                break;
            }
        }
        if (!supportedDepth) {
            return failPreflight(QStringLiteral("Invalid bitDepth: %1")
                                     .arg(conversionRequest.bitDepth));
        }
    }
    const QVariantList qualityChoices = capability.value(
        QStringLiteral("qualityChoices")).toList();
    if ((parameterKind == QStringLiteral("quality")
         || parameterKind == QStringLiteral("compression"))
        && !qualityChoices.contains(conversionRequest.quality)) {
        return failPreflight(QStringLiteral("Invalid quality for %1: %2")
                                 .arg(format)
                                 .arg(conversionRequest.quality));
    }
    const QVariantList supportedLayouts = capability.value(
        QStringLiteral("channelLayouts")).toList();
    if (!conversionRequest.channelLayout.isEmpty()
        && !supportedLayouts.contains(conversionRequest.channelLayout)) {
        return failPreflight(QStringLiteral("Unsupported channelLayout: %1")
                                 .arg(conversionRequest.channelLayout));
    }
    const QVariantList supportedRates = capability.value(
        QStringLiteral("sampleRateChoices")).toList();
    if (conversionRequest.sampleRate != 0
        && !supportedRates.contains(conversionRequest.sampleRate)) {
        return failPreflight(QStringLiteral("Unsupported sampleRate: %1")
                                 .arg(conversionRequest.sampleRate));
    }
    const QVariantList bitRateChoices = capability.value(
        QStringLiteral("bitRateChoices")).toList();
    if (parameterKind == QStringLiteral("bitrate")) {
        if (!bitRateChoices.contains(conversionRequest.bitrate)) {
            return failPreflight(QStringLiteral("Invalid bitRate for %1: %2")
                                     .arg(format)
                                     .arg(conversionRequest.bitrate));
        }
    } else if (conversionRequest.bitrate != 0) {
        return failPreflight(QStringLiteral("Unsupported bitRate for %1")
                                 .arg(format));
    }
    const QString requestedCodec = conversionRequest.codecName.trimmed();
    if (!requestedCodec.isEmpty()
        && requestedCodec != capability.value(QStringLiteral("codec")).toString()) {
        return failPreflight(QStringLiteral("Invalid codecName: %1")
                                 .arg(requestedCodec));
    }
    int selectedAudioStreamIndex = -1;
    if (request.contains(QStringLiteral("audioStreamIndex"))) {
        selectedAudioStreamIndex = request.value(
            QStringLiteral("audioStreamIndex")).toInt(&validNumber);
        if (!validNumber || selectedAudioStreamIndex < -1) {
            return failPreflight(
                QStringLiteral("Invalid audioStreamIndex: expected -1 or greater"));
        }
    }
    conversionRequest.outputDirectory = request.value(
        QStringLiteral("outputDir")).toString();
    const QString outputDirectoryError = prepare_output_directory(
        conversionRequest.outputDirectory);
    if (!outputDirectoryError.isEmpty()) {
        return failPreflight(outputDirectoryError);
    }
    conversionRequest.conflictPolicy = conflictPolicy;
    conversionRequest.keepMetadata = request.value(
        QStringLiteral("keepMetadata"), true).toBool();
    conversionRequest.keepCover = request.value(
        QStringLiteral("keepCover"), true).toBool()
        && capability.value(QStringLiteral("supportsCover")).toBool();
    // Unsupported ancillary data must not turn a valid audio conversion into
    // a failure.  The resolved profile records that it was omitted.
    if (conversionRequest.keepMetadata
        && !capability.value(QStringLiteral("supportsMetadata")).toBool()) {
        conversionRequest.keepMetadata = false;
    }
    conversionRequest.preserveDirectories = request.value(
        QStringLiteral("preserveDirectories"), true).toBool();
    conversionRequest.extractAudio = request.value(
        QStringLiteral("extractAudio"), true).toBool();

    static const QUuid taskNamespace(
        QStringLiteral("{76df29bf-589f-4dc4-a64a-9938f8b862d8}"));
    QList<FormatPlanInput> inputs;
    QHash<QUuid, QString> externalTaskIds;
    QHash<QString, FileEntry> selectedByTaskId;
    QString probeError;
    for (const FileEntry& entry : selectedEntries) {
        if (!entry.probeError.isEmpty() || entry.audioStreams.isEmpty()) {
            probeError = entry.probeError.isEmpty()
                ? tr("输入文件不包含音频流：%1").arg(entry.path)
                : entry.probeError;
            break;
        }
        agplayer::MediaProbe probe;
        probe.container = entry.probeContainer.toStdString();
        probe.is_video = entry.probeIsVideo;
        probe.has_cover = entry.probeHasCover;
        probe.audio_streams.reserve(
            static_cast<std::size_t>(entry.audioStreams.size()));
        for (const AudioStreamSnapshot& snapshot : entry.audioStreams) {
            probe.audio_streams.push_back({
                snapshot.streamIndex,
                snapshot.codec.toStdString(),
                snapshot.language.toStdString(),
                snapshot.title.toStdString(),
                snapshot.isDefault,
                snapshot.sampleRate,
                snapshot.sampleFormat.toStdString(),
                snapshot.channelLayout.toStdString(),
                snapshot.bitRate,
                snapshot.durationMs,
                snapshot.bitsPerSample});
        }
        const QUuid planTaskId = QUuid::createUuidV5(
            taskNamespace, entry.importInstanceId.toUtf8());
        inputs.append({planTaskId, entry.path, entry.importRoot,
                       std::move(probe),
                       selectedAudioStreamIndex});
        externalTaskIds.insert(planTaskId, entry.taskId);
        selectedByTaskId.insert(entry.taskId, entry);
    }

    FormatBatchPlan batch;
    if (probeError.isEmpty()) {
        batch = build_format_conversion_plan(inputs, conversionRequest);
    } else {
        batch.fatalError = probeError;
    }
    if (batch.ready) {
        QSet<QString> checkedOutputParents;
        for (const FormatTaskPlan& task : batch.tasks) {
            if (task.skipped) {
                continue;
            }
            QString outputParent = QFileInfo(task.outputPath).absolutePath();
            QString parentKey = QDir::cleanPath(outputParent);
#ifdef Q_OS_WIN
            parentKey = parentKey.toCaseFolded();
#endif
            if (checkedOutputParents.contains(parentKey)) {
                continue;
            }
            const QString parentError = prepare_output_directory(outputParent);
            if (!parentError.isEmpty()) {
                return failPreflight(parentError);
            }
            checkedOutputParents.insert(parentKey);
        }
    }
    if (resolvesUnusedBitrateMode && batch.ready) {
        for (FormatTaskPlan& task : batch.tasks) {
            task.differences.push_back({
                QStringLiteral("bitrateMode"), requestedBitrateMode, QString(),
                QStringLiteral("Output format does not use bitrate modes"),
                true});
        }
        batch.requiresConfirmation = true;
    }
    if (adjustsOpusSampleRate && batch.ready) {
        for (FormatTaskPlan& task : batch.tasks) {
            task.differences.push_back({
                QStringLiteral("sampleRate"), requestedSampleRate, 48000,
                QStringLiteral("Opus output requires 48 kHz"), true});
        }
        batch.requiresConfirmation = true;
    }
    if (!batch.fatalError.isEmpty()) {
        return failPreflight(batch.fatalError);
    }

    QVariantList serializedTasks;
    QVariantList conflicts;
    QVector<FrozenConversionJob> frozenJobs;
    serializedTasks.reserve(batch.tasks.size());
    frozenJobs.reserve(batch.tasks.size());
    for (const FormatTaskPlan& task : batch.tasks) {
        QVariantList differences;
        differences.reserve(task.differences.size());
        for (const FormatPlanDifference& difference : task.differences) {
            const QVariantMap serializedDifference{
                {QStringLiteral("field"), difference.field},
                {QStringLiteral("requested"), difference.requested},
                {QStringLiteral("resolved"), difference.resolved},
                {QStringLiteral("reason"), difference.reason},
                {QStringLiteral("requiresConfirmation"),
                 difference.requiresConfirmation}};
            differences.append(serializedDifference);
            if (difference.field == QStringLiteral("conflict")) {
                conflicts.append(serializedDifference);
            }
        }
        const QString externalTaskId = externalTaskIds.value(task.taskId);
        const FileEntry entry = selectedByTaskId.value(externalTaskId);
        QVariantMap resolvedProfile = task.resolvedProfile;
        // A batch may combine files with and without embedded artwork.  Keep
        // the request frozen per source so an absent cover is never treated as
        // an encoding failure or a request to synthesize artwork.
        resolvedProfile.insert(QStringLiteral("keepCover"),
                               conversionRequest.keepCover && entry.probeHasCover);
        const bool confirmedOverwrite = conflictPolicy
                == FormatConflictPolicy::Overwrite
            || (conflictPolicy == FormatConflictPolicy::Ask
                && std::any_of(task.differences.cbegin(), task.differences.cend(),
                    [](const FormatPlanDifference& difference) {
                        return difference.field == QStringLiteral("conflict");
                    }));
        serializedTasks.append(QVariantMap{
            {QStringLiteral("taskId"), externalTaskId},
            {QStringLiteral("inputPath"), task.inputPath},
            {QStringLiteral("outputPath"), task.outputPath},
            {QStringLiteral("audioStreamIndex"), task.audioStreamIndex},
            {QStringLiteral("resolvedProfile"), resolvedProfile},
            {QStringLiteral("differences"), differences},
            {QStringLiteral("skipped"), task.skipped},
            {QStringLiteral("action"), task.skipped
                ? QStringLiteral("skip")
                : confirmedOverwrite ? QStringLiteral("overwrite")
                                     : QStringLiteral("create")}});
        const bool metadataPlanApplies = metadataPlanActive_
            && (metadataTargetPaths_.isEmpty()
                || metadataTargetPaths_.contains(
                    normalized_path_key(entry.path)));
        FrozenConversionJob frozenJob;
        frozenJob.taskId = externalTaskId;
        frozenJob.importInstanceId = entry.importInstanceId;
        frozenJob.inputPath = entry.path;
        frozenJob.canonicalPath = entry.canonicalPath;
        frozenJob.importRoot = entry.importRoot;
        frozenJob.outputPath = task.outputPath;
        frozenJob.sourceSize = entry.fileSize;
        frozenJob.sourceLastModifiedMs = entry.sourceLastModifiedMs;
        frozenJob.resolvedProfile = resolvedProfile;
        frozenJob.skipped = task.skipped;
        frozenJob.overwriteExisting = confirmedOverwrite;
        frozenJob.extractAudio = conversionRequest.extractAudio;
        frozenJob.preserveDirectories = conversionRequest.preserveDirectories;
        frozenJob.metadataPlanActive = metadataPlanApplies;
        if (metadataPlanApplies) {
            frozenJob.metadataFields = metadataFields_;
            frozenJob.metadataCoverData = metadataCoverData_;
            frozenJob.metadataCoverMime = metadataCoverMime_;
        }
        frozenJobs.append(std::move(frozenJob));
    }
    plan.insert(QStringLiteral("tasks"), serializedTasks);
    plan.insert(QStringLiteral("taskCount"), serializedTasks.size());
    plan.insert(QStringLiteral("conflicts"), conflicts);
    plan.insert(QStringLiteral("conflictCount"), conflicts.size());
    plan.insert(QStringLiteral("requiresConfirmation"),
                batch.requiresConfirmation
                    || request.value(QStringLiteral("requiresConfirmation"))
                           .toBool());
    plan.insert(QStringLiteral("ready"), batch.ready);
    if (!batch.tasks.isEmpty()) {
        plan.insert(QStringLiteral("resolvedProfile"),
                    batch.tasks.first().resolvedProfile);
    }
    pendingPlan_.clear();
    pendingRequest_.clear();
    pendingJobs_.clear();
    if (batch.ready) {
        pendingPlan_ = plan;
        pendingRequest_ = request;
        pendingRequest_.insert(QStringLiteral("outputFormat"), format);
        pendingRequest_.insert(QStringLiteral("conflictPolicy"),
                               requestedConflictPolicy);
        pendingRequest_.insert(QStringLiteral("bitrateMode"),
                               conversionRequest.bitrateMode);
        pendingRequest_.insert(QStringLiteral("bitRate"),
                               conversionRequest.bitrate);
        pendingRequest_.insert(QStringLiteral("sampleRate"),
                               conversionRequest.sampleRate);
        pendingRequest_.insert(QStringLiteral("channelLayout"),
                               conversionRequest.channelLayout);
        pendingRequest_.insert(QStringLiteral("sampleFormat"),
                               conversionRequest.sampleFormat);
        pendingRequest_.insert(QStringLiteral("outputDir"),
                               conversionRequest.outputDirectory);
        pendingRequest_.insert(QStringLiteral("keepMetadata"),
                               conversionRequest.keepMetadata);
        pendingRequest_.insert(QStringLiteral("keepCover"),
                               conversionRequest.keepCover);
        pendingRequest_.insert(QStringLiteral("preserveDirectories"),
                               conversionRequest.preserveDirectories);
        pendingRequest_.insert(QStringLiteral("extractAudio"),
                               conversionRequest.extractAudio);
        pendingJobs_ = frozenJobs;
    }
    emit pendingPlanChanged();
    return plan;
}

void FormatConverter::confirmPendingPlan()
{
    if (pendingPlan_.isEmpty()) return;
    const QVariantMap request = pendingRequest_;
    const QVector<FrozenConversionJob> jobs = pendingJobs_;
    pendingPlan_.clear();
    pendingRequest_.clear();
    pendingJobs_.clear();
    emit pendingPlanChanged();

    QVector<int> frozenIndices;
    QVector<FileEntry> currentEntries;
    {
        QMutexLocker lock(&mutex_);
        currentEntries = entries_.toVector();
    }
    QHash<QString, int> currentIndexByTaskId;
    for (int index = 0; index < currentEntries.size(); ++index) {
        currentIndexByTaskId.insert(currentEntries.at(index).taskId, index);
    }
    frozenIndices.reserve(jobs.size());
    for (const FrozenConversionJob& job : jobs) {
        const int matchingIndex = currentIndexByTaskId.value(job.taskId, -1);
        const FileEntry current = matchingIndex >= 0
            ? currentEntries.at(matchingIndex) : FileEntry{};
        if (matchingIndex < 0
            || current.importInstanceId != job.importInstanceId
            || current.path != job.inputPath
            || current.canonicalPath != job.canonicalPath
            || current.importRoot != job.importRoot
            || (current.status != FileStatus::Waiting
                && current.status != FileStatus::Ready
                && current.status != FileStatus::PendingConfirmation)) {
            emit errorOccurred(tr("预检计划已失效，请重新预检后再开始"));
            return;
        }
        const QFileInfo source(job.inputPath);
        QString canonicalPath = source.canonicalFilePath();
        if (canonicalPath.isEmpty()) {
            canonicalPath = QDir::cleanPath(source.absoluteFilePath());
        }
        if (!source.isFile()
            || canonicalPath.compare(job.canonicalPath,
                                     Qt::CaseInsensitive) != 0
            || source.size() != job.sourceSize
            || source.lastModified().toMSecsSinceEpoch()
                != job.sourceLastModifiedMs) {
            emit errorOccurred(tr("源文件在预检后发生变化，请重新预检"));
            return;
        }
        if (job.outputPath.isEmpty()
            || QDir::cleanPath(QFileInfo(job.outputPath).absoluteFilePath())
                   .compare(QDir::cleanPath(source.absoluteFilePath()),
                            Qt::CaseInsensitive) == 0) {
            emit errorOccurred(tr("预检输出路径无效，请重新预检"));
            return;
        }
        if (!job.skipped && !job.overwriteExisting
            && QFileInfo::exists(job.outputPath)) {
            emit errorOccurred(tr("输出路径在预检后发生变化，请重新预检"));
            return;
        }
        frozenIndices.append(matchingIndex);
    }
    if (frozenIndices.size() != jobs.size() || jobs.isEmpty()) {
        emit errorOccurred(tr("预检计划已失效，请重新预检后再开始"));
        return;
    }

    const QVariantMap firstProfile = jobs.first().resolvedProfile;
    const QString channelLayout = firstProfile.value(
        QStringLiteral("channelLayout")).toString();
    int channels = 0;
    if (channelLayout == QStringLiteral("mono")) channels = 1;
    if (channelLayout == QStringLiteral("stereo")) channels = 2;
    startJobs(frozenIndices,
              firstProfile.value(QStringLiteral("format")).toString(),
              firstProfile.value(QStringLiteral("bitrate")).toInt(),
              firstProfile.value(QStringLiteral("sampleRate")).toInt(),
              channels,
              request.value(QStringLiteral("outputDir")).toString(),
              firstProfile.value(QStringLiteral("keepMetadata")).toBool(),
              request.value(QStringLiteral("volumeNormalize"), false).toBool(),
              request.value(QStringLiteral("extractAudio"), false).toBool(),
              firstProfile.value(QStringLiteral("keepCover")).toBool(),
              firstProfile.value(QStringLiteral("sampleFormat")).toString(),
              channelLayout,
              firstProfile.value(QStringLiteral("audioStreamIndex"), -1).toInt(),
              request.value(QStringLiteral("preserveDirectories"), false)
                  .toBool(),
              jobs);
}

void FormatConverter::rejectPendingPlan()
{
    if (pendingPlan_.isEmpty()) return;
    pendingPlan_.clear();
    pendingRequest_.clear();
    pendingJobs_.clear();
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
    if (busy_.load(std::memory_order_acquire)) {
        emit errorOccurred(tr("转换任务运行时不能更改元数据计划"));
        return false;
    }
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
    metadataPlanActive_ = true;
    metadataTargetPaths_.clear();
    return true;
}

bool FormatConverter::setMetadataEditPlanForFiles(
    const QVariantMap& fields, const QUrl& coverUrl,
    const QList<QUrl>& targetUrls)
{
    if (!setMetadataEditPlan(fields, coverUrl)) {
        return false;
    }
    QSet<QString> targets;
    for (const QUrl& url : targetUrls) {
        if (!url.isLocalFile() || url.toLocalFile().isEmpty()) {
            clearMetadataEditPlan();
            emit errorOccurred(tr("元数据转换计划只能绑定本地文件"));
            return false;
        }
        targets.insert(normalized_path_key(url.toLocalFile()));
    }
    if (targets.isEmpty()) {
        clearMetadataEditPlan();
        emit errorOccurred(tr("元数据转换计划没有目标文件"));
        return false;
    }
    metadataTargetPaths_ = std::move(targets);
    return true;
}

void FormatConverter::clearMetadataEditPlan()
{
    metadataFields_.clear();
    metadataCoverData_.clear();
    metadataCoverMime_.clear();
    metadataTargetPaths_.clear();
    metadataPlanActive_ = false;
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
    syncTaskModel();
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
    syncTaskModel();
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

void FormatConverter::retryFailed()
{
    QVector<int> failed;
    {
        QMutexLocker lock(&mutex_);
        failed.reserve(entries_.size());
        for (int index = 0; index < entries_.size(); ++index) {
            const FileEntry& entry = entries_.at(index);
            if ((entry.status == FileStatus::Error
                 || entry.status == FileStatus::Cancelled)
                && !entry.resolvedProfile.isEmpty()) {
                failed.push_back(index);
            }
        }
    }
    retryFrozenEntries(failed);
}

void FormatConverter::retryTask(const QString& taskId)
{
    QVector<int> indices;
    {
        QMutexLocker lock(&mutex_);
        for (int index = 0; index < entries_.size(); ++index) {
            const FileEntry& entry = entries_.at(index);
            if (entry.taskId == taskId
                && (entry.status == FileStatus::Error
                    || entry.status == FileStatus::Cancelled)
                && !entry.resolvedProfile.isEmpty()) {
                indices.push_back(index);
                break;
            }
        }
    }
    retryFrozenEntries(indices);
}

void FormatConverter::retryFrozenEntries(const QVector<int>& indices)
{
    QVector<FrozenConversionJob> jobs;
    QVariantMap firstProfile;
    {
        QMutexLocker lock(&mutex_);
        jobs.reserve(indices.size());
        for (const int index : indices) {
            if (index < 0 || index >= entries_.size()) continue;
            const FileEntry& entry = entries_.at(index);
            if (entry.resolvedProfile.isEmpty()) continue;
            FrozenConversionJob job;
            job.taskId = entry.taskId;
            job.importInstanceId = entry.importInstanceId;
            job.inputPath = entry.path;
            job.canonicalPath = entry.canonicalPath;
            job.importRoot = entry.importRoot;
            job.outputPath = entry.outputPath;
            job.sourceSize = entry.fileSize;
            job.sourceLastModifiedMs = entry.sourceLastModifiedMs;
            job.resolvedProfile = entry.resolvedProfile;
            job.overwriteExisting = entry.overwriteExisting;
            job.extractAudio = entry.frozenExtractAudio;
            job.preserveDirectories = entry.frozenPreserveDirectories;
            job.metadataFields = entry.frozenMetadataFields;
            job.metadataCoverData = entry.frozenMetadataCoverData;
            job.metadataCoverMime = entry.frozenMetadataCoverMime;
            job.metadataPlanActive = entry.frozenMetadataPlanActive;
            jobs.push_back(std::move(job));
            if (firstProfile.isEmpty()) firstProfile = entry.resolvedProfile;
        }
    }
    if (jobs.isEmpty()) {
        emit errorOccurred(tr("没有可重试的冻结转换任务"));
        return;
    }
    QString layout = firstProfile.value(QStringLiteral("channelLayout"))
                         .toString();
    const int channels = layout == QStringLiteral("mono") ? 1
        : layout == QStringLiteral("stereo") ? 2 : 0;
    startJobs(indices, firstProfile.value(QStringLiteral("format")).toString(),
              firstProfile.value(QStringLiteral("bitrate")).toInt(),
              firstProfile.value(QStringLiteral("sampleRate")).toInt(),
              channels, QString(),
              firstProfile.value(QStringLiteral("keepMetadata")).toBool(),
              false, jobs.first().extractAudio,
              firstProfile.value(QStringLiteral("keepCover")).toBool(),
              firstProfile.value(QStringLiteral("sampleFormat")).toString(),
              layout,
              firstProfile.value(QStringLiteral("audioStreamIndex"), -1).toInt(),
              jobs.first().preserveDirectories, jobs);
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
                                bool preserveDirectories,
                                const QVector<FrozenConversionJob>& plannedJobs)
{
    if (busy_.load(std::memory_order_acquire)) {
        return;
    }

    const int fileCount = jobIndices.size();
    if (fileCount == 0) {
        emit errorOccurred(tr("没有可转换的文件"));
        return;
    }
    if (!plannedJobs.isEmpty() && plannedJobs.size() != fileCount) {
        emit errorOccurred(tr("冻结计划与任务队列不一致，请重新预检"));
        return;
    }
    const QString normalizedFormat = outputFormat.trimmed().toLower();
    bool formatSupported = false;
    QString unsupportedReason;
    QString parameterKind;
    for (const QVariant& value : supportedOutputFormats()) {
        const QVariantMap candidate = value.toMap();
        if (candidate.value(QStringLiteral("key")).toString()
            == normalizedFormat) {
            formatSupported = candidate.value(QStringLiteral("available")).toBool();
            unsupportedReason = candidate.value(QStringLiteral("reason")).toString();
            parameterKind = candidate.value(QStringLiteral("parameterKind")).toString();
            break;
        }
    }
    if (!formatSupported) {
        emit errorOccurred(unsupportedReason.isEmpty()
            ? tr("当前编码器不支持输出格式：%1").arg(outputFormat)
            : unsupportedReason);
        return;
    }
    if (parameterKind == QStringLiteral("bitrate")
        && (bitRate < 8000 || bitRate > 512000)) {
        emit errorOccurred(tr("%1 需要有效的目标码率")
                               .arg(outputFormat.toUpper()));
        return;
    }
    if (sampleRate < 0 || sampleRate > 384000) {
        emit errorOccurred(tr("采样率必须在自动到 384 kHz 之间"));
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
    bool metadataPlanApplies = metadataPlanActive_;
    if (metadataPlanApplies && !metadataTargetPaths_.isEmpty()) {
        metadataPlanApplies = false;
        QMutexLocker lock(&mutex_);
        for (const int index : jobIndices) {
            if (index >= 0 && index < entries_.size()
                && metadataTargetPaths_.contains(
                    normalized_path_key(entries_.at(index).path))) {
                metadataPlanApplies = true;
                break;
            }
        }
    }
    if (metadataPlanApplies) {
        const FormatInfo info = format_info(normalizedFormat);
        agplayer::TranscodeConfig metadataPreflight;
        metadataPreflight.output_path =
            (QDir(outputDir.isEmpty() ? QStringLiteral(".") : outputDir)
                 .filePath(QStringLiteral("agplayer-metadata-preflight.%1")
                               .arg(QString::fromLatin1(info.extension))))
                .toUtf8().toStdString();
        metadataPreflight.container_name = info.muxer_name;
        metadataPreflight.metadata_edit_plan = metadata_plan(
            metadataFields_, metadataCoverData_, metadataCoverMime_);
        std::string metadataError;
        const ag_result metadataResult = agplayer::preflight_transcode_metadata(
            metadataPreflight, metadataError);
        if (metadataResult != AG_OK) {
            emit errorOccurred(
                tr("输出格式不支持当前元数据修改：%1")
                    .arg(QString::fromStdString(metadataError)));
            return;
        }
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
    setDoneCount(0);
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
    const bool metadataPlanActive = metadataPlanApplies;
    const QSet<QString> metadataTargetPaths = metadataTargetPaths_;
    const int frozenParallelJobs = std::clamp(parallelJobs_, 1, 10);
    clearMetadataEditPlan();
    auto* watcher = new QFutureWatcher<void>(this);
    watcher_ = watcher;
    connect(watcher, &QFutureWatcher<void>::finished, this,
        [this, watcher]() {
            watcher->deleteLater();
            watcher_.clear();
            const int success = doneCount_.load(std::memory_order_acquire);
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
         metadataCoverMime, metadataPlanActive, metadataTargetPaths,
         keepCover, sampleFormat, channelLayout,
         audioStreamIndex, preserveDirectories, frozenParallelJobs, plannedJobs,
         jobIndices]() {
            runTranscode(outputFormat, bitRate, effectiveSampleRate, channels,
                         outputDir, keepMetadata, volumeNormalize,
                         extractAudio, overwriteExisting, bitrateMode, conflictPolicy,
                         metadataFields, metadataCoverData, metadataCoverMime,
                         metadataPlanActive, metadataTargetPaths,
                         jobIndices, keepCover, sampleFormat, channelLayout,
                         audioStreamIndex, preserveDirectories,
                         frozenParallelJobs, plannedJobs);
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
                                   const bool metadataPlanActive,
                                   const QSet<QString>& metadataTargetPaths,
                                   const QVector<int>& jobIndices,
                                   bool keepCover,
                                   const QString& sampleFormat,
                                   const QString& channelLayout,
                                   int audioStreamIndex,
                                   bool preserveDirectories,
                                   int parallelJobs,
                                   const QVector<FrozenConversionJob>& plannedJobs)
{
    (void)volumeNormalize; // The versioned core request has no normalize field.
    const agplayer::MetadataEditPlan plan = metadata_plan(
        metadataFields, metadataCoverData, metadataCoverMime);

    QVector<QString> inputPaths;
    QVector<QString> importRoots;
    const bool usesPlannedJobs = !plannedJobs.isEmpty();
    if (usesPlannedJobs) {
        inputPaths.reserve(plannedJobs.size());
        importRoots.reserve(plannedJobs.size());
        for (const FrozenConversionJob& job : plannedJobs) {
            inputPaths.push_back(job.inputPath);
            importRoots.push_back(job.importRoot);
        }
    } else {
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
        if (usesPlannedJobs) {
            outputPaths.push_back(plannedJobs.at(index).outputPath);
            continue;
        }
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
            const QString entryFormat = usesPlannedJobs
                ? plannedJobs.at(index).resolvedProfile
                      .value(QStringLiteral("format")).toString()
                : outputFormat;
            entries_[entryIndex].outputFormat = entryFormat.toUpper();
            entries_[entryIndex].outputPath = outputPaths.at(index);
            if (usesPlannedJobs) {
                entries_[entryIndex].resolvedProfile =
                    plannedJobs.at(index).resolvedProfile;
                entries_[entryIndex].overwriteExisting =
                    plannedJobs.at(index).overwriteExisting;
                entries_[entryIndex].frozenExtractAudio =
                    plannedJobs.at(index).extractAudio;
                entries_[entryIndex].frozenPreserveDirectories =
                    plannedJobs.at(index).preserveDirectories;
                entries_[entryIndex].frozenMetadataFields =
                    plannedJobs.at(index).metadataFields;
                entries_[entryIndex].frozenMetadataCoverData =
                    plannedJobs.at(index).metadataCoverData;
                entries_[entryIndex].frozenMetadataCoverMime =
                    plannedJobs.at(index).metadataCoverMime;
                entries_[entryIndex].frozenMetadataPlanActive =
                    plannedJobs.at(index).metadataPlanActive;
            }
            entries_[entryIndex].progress = 0.0;
        }
    }
    emit filesChanged();

    QVector<int> jobs(totalJobs);
    std::iota(jobs.begin(), jobs.end(), 0);
    QThreadPool pool;
    // The persisted/UI contract is explicitly 1–10.  QThreadPool can queue
    // work above logical-core count; do not silently rewrite the user's limit.
    pool.setMaxThreadCount(parallelJobs);

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
        const FrozenConversionJob* plannedJob = usesPlannedJobs
            ? &plannedJobs.at(i) : nullptr;
        const QVariantMap resolvedProfile = plannedJob != nullptr
            ? plannedJob->resolvedProfile : QVariantMap{};
        const bool applyMetadataPlan = plannedJob != nullptr
            ? plannedJob->metadataPlanActive
            : metadataPlanActive
                && (metadataTargetPaths.isEmpty()
                    || metadataTargetPaths.contains(
                        normalized_path_key(inputPath)));
        const agplayer::MetadataEditPlan entryMetadataPlan =
            plannedJob != nullptr && applyMetadataPlan
            ? metadata_plan(plannedJob->metadataFields,
                            plannedJob->metadataCoverData,
                            plannedJob->metadataCoverMime)
            : plan;
        const QString jobFormat = plannedJob != nullptr
            ? resolvedProfile.value(QStringLiteral("format")).toString()
            : outputFormat;
        const FormatInfo formatInfo = format_info(jobFormat);
        const QByteArray codecName = plannedJob != nullptr
            ? resolvedProfile.value(QStringLiteral("codec")).toString().toUtf8()
            : QByteArray(formatInfo.codec_name);
        const QByteArray muxerName = plannedJob != nullptr
            ? resolvedProfile.value(QStringLiteral("muxer")).toString().toUtf8()
            : QByteArray(formatInfo.muxer_name);
        const qint64 jobBitRate = plannedJob != nullptr
            ? resolvedProfile.value(QStringLiteral("bitrate")).toLongLong()
            : bitRate;
        const int jobSampleRate = plannedJob != nullptr
            ? resolvedProfile.value(QStringLiteral("sampleRate")).toInt()
            : sampleRate;
        const QString jobSampleFormat = plannedJob != nullptr
            ? resolvedProfile.value(QStringLiteral("sampleFormat")).toString()
            : sampleFormat;
        QString resolvedLayout = plannedJob != nullptr
            ? resolvedProfile.value(QStringLiteral("channelLayout")).toString()
            : channelLayout.trimmed().toLower();
        if (resolvedLayout.isEmpty()) {
            if (channels == 1) resolvedLayout = QStringLiteral("mono");
            if (channels == 2) resolvedLayout = QStringLiteral("stereo");
        }
        const int jobAudioStreamIndex = plannedJob != nullptr
            ? resolvedProfile.value(QStringLiteral("audioStreamIndex"), -1).toInt()
            : audioStreamIndex;
        const bool jobKeepMetadata = plannedJob != nullptr
            ? resolvedProfile.value(QStringLiteral("keepMetadata")).toBool()
            : keepMetadata;
        const bool jobKeepCover = plannedJob != nullptr
            ? resolvedProfile.value(QStringLiteral("keepCover")).toBool()
            : keepCover;
        const QString jobBitrateMode = plannedJob != nullptr
            ? resolvedProfile.value(QStringLiteral("bitrateMode")).toString()
            : bitrateMode;
        const int jobQuality = plannedJob != nullptr
            ? resolvedProfile.value(QStringLiteral("quality"), 75).toInt()
            : 75;
        const bool jobOverwriteExisting = plannedJob != nullptr
            ? plannedJob->overwriteExisting : overwriteExisting;
        const bool jobExtractAudio = plannedJob != nullptr
            ? plannedJob->extractAudio : extractAudio;
        if (plannedJob != nullptr && !plannedJob->skipped) {
            const QFileInfo currentSource(inputPath);
            QString currentCanonicalPath = currentSource.canonicalFilePath();
            if (currentCanonicalPath.isEmpty()) {
                currentCanonicalPath = QDir::cleanPath(
                    currentSource.absoluteFilePath());
            }
            if (!currentSource.isFile()
                || currentCanonicalPath.compare(plannedJob->canonicalPath,
                                                Qt::CaseInsensitive) != 0
                || currentSource.size() != plannedJob->sourceSize
                || currentSource.lastModified().toMSecsSinceEpoch()
                    != plannedJob->sourceLastModifiedMs) {
                complete(FileStatus::Error,
                         tr("源文件在预检后发生变化，请重新预检"));
                return;
            }
        }
        if (!QDir().mkpath(QFileInfo(outputPath).absolutePath())) {
            complete(FileStatus::Error,
                     tr("无法创建输出目录：%1")
                         .arg(QFileInfo(outputPath).absolutePath()));
            return;
        }
        if ((plannedJob != nullptr && plannedJob->skipped)
            || (plannedJob == nullptr
                && conflictPolicy == QStringLiteral("skip")
                && QFileInfo::exists(outputPath))) {
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
        if (is_video_file(inputPath) && !jobExtractAudio) {
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
        const QByteArray sampleFormatUtf8 = jobSampleFormat.toUtf8();
        const QByteArray channelLayoutUtf8 = resolvedLayout.toUtf8();
        std::vector<ag_metadata_field_edit> metadataEdits;
        metadataEdits.reserve(entryMetadataPlan.fields.size());
        for (const agplayer::FieldEdit& edit : entryMetadataPlan.fields) {
            metadataEdits.push_back({
                static_cast<ag_metadata_field>(edit.field),
                static_cast<ag_metadata_edit_action>(edit.action),
                edit.value_utf8.has_value() ? edit.value_utf8->c_str() : nullptr});
        }

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
        request.bit_rate = static_cast<long long>(jobBitRate);
        request.sample_rate = jobSampleRate;
        request.channel_layout = channelLayoutUtf8.isEmpty()
            ? nullptr : channelLayoutUtf8.constData();
        request.sample_format = sampleFormatUtf8.isEmpty()
            ? nullptr : sampleFormatUtf8.constData();
        request.audio_stream_index = jobAudioStreamIndex;
        request.keep_metadata = (jobKeepMetadata || applyMetadataPlan) ? 1 : 0;
        request.keep_cover =
            (jobKeepCover
             || (applyMetadataPlan
                 && entryMetadataPlan.cover_action
                     == agplayer::CoverAction::Keep))
            ? 1 : 0;
        request.bitrate_mode = jobBitrateMode == QStringLiteral("vbr") ? 1 : 0;
        request.quality = jobQuality;
        request.metadata_fields = !applyMetadataPlan || metadataEdits.empty()
            ? nullptr : metadataEdits.data();
        request.metadata_field_count = applyMetadataPlan
            ? metadataEdits.size() : 0;
        request.metadata_cover_action = applyMetadataPlan
            ? static_cast<ag_metadata_cover_action>(
                entryMetadataPlan.cover_action)
            : AG_METADATA_COVER_KEEP;
        request.metadata_cover_data = applyMetadataPlan
            ? entryMetadataPlan.cover_data : nullptr;
        request.metadata_cover_size = applyMetadataPlan
            ? entryMetadataPlan.cover_size : 0;
        request.metadata_cover_mime_type = !applyMetadataPlan
                || entryMetadataPlan.cover_mime_type.empty()
            ? nullptr : entryMetadataPlan.cover_mime_type.c_str();
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
            QString validationError;
            if (!format_converter_detail::validate_audio_output(
                    stagedPath, resolvedProfile, validationError)) {
                QFile::remove(stagedPath);
                complete(FileStatus::Error,
                         tr("转换结果验证失败：%1").arg(validationError));
                return;
            }
            const auto commitMode = jobOverwriteExisting
                ? format_converter_detail::OutputCommitMode::Overwrite
                : format_converter_detail::OutputCommitMode::CreateNoReplace;
            if (!format_converter_detail::commit_staged_output(
                    stagedPath, outputPath, commitMode)) {
                QFile::remove(stagedPath);
                complete(FileStatus::Error,
                         tr("无法安全写入输出文件：%1").arg(outputPath));
                return;
            }
            setEntryStatus(entryIndex, FileStatus::Done);
            doneCount_.fetch_add(1, std::memory_order_acq_rel);
            emit doneCountChanged();
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
                                .arg(inputPath, outputPath, jobFormat, detail));
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
    clearMetadataEditPlan();
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
