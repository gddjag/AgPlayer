#include "audio_editor_controller.hpp"

#include "audio_editor/audio_file_analyzer.hpp"
#include "audio_editor/document_renderer.hpp"
#include "audio_editor/document_writer.hpp"
#include "bpm_analyzer.hpp"
#include "transcode_capability.hpp"

#include <QFileInfo>
#include <QDateTime>
#include <QDir>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QtConcurrent>

#include <algorithm>
#include <array>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <list>

using agplayer::editor::AudioDocument;
using agplayer::editor::AudioFileAnalysis;
using agplayer::editor::AudioFileAnalyzer;
using agplayer::editor::AudioSource;
using agplayer::editor::AudioSpan;
using agplayer::editor::DocumentRenderer;
using agplayer::editor::DocumentWriter;
using agplayer::editor::DocumentSnapshot;
using agplayer::editor::EditCommand;
using agplayer::editor::SampleFrame;
using agplayer::editor::Selection;
using agplayer::editor::WriteRequest;

namespace {

struct ViewportWaveformJobResult {
    struct Tile {
        qint64 start_frame{};
        qint64 end_frame{};
        int mode{};
        qint64 target_point_count{};
        std::vector<std::vector<float>> channels;
    };

    std::vector<Tile> tiles;
    bool cancelled{};
    bool ok{true};
    QString error;
};

constexpr qint64 kViewportWaveformCacheDefaultLimit = 16LL * 1024LL * 1024LL;

constexpr qreal kViewportModeAThreshold = 32.0;
constexpr qreal kViewportModeBThreshold = 1.2;

int viewportRenderMode(const qreal samplesPerPixel)
{
    if (!std::isfinite(samplesPerPixel) || samplesPerPixel <= 0.0) {
        return 0;
    }
    if (samplesPerPixel > kViewportModeAThreshold) {
        return 0;
    }
    if (samplesPerPixel > kViewportModeBThreshold) {
        return 1;
    }
    return 2;
}

qint64 viewportTargetPoints(const int renderMode, const qint64 visibleFrames,
                           const qreal viewportWidth)
{
    const qint64 width = std::max<qint64>(1, static_cast<qint64>(std::llround(
        std::max<qreal>(1.0, viewportWidth))));
    if (renderMode == 0) {
        return std::max<qint64>(48, std::min(visibleFrames, width / 2));
    }
    if (renderMode == 1) {
        return std::max<qint64>(width,
            std::min<qint64>(visibleFrames, width * 2));
    }
    return std::min(visibleFrames, width * 2);
}

qint64 viewportCacheBytes(const std::vector<std::vector<float>>& channels)
{
    qint64 bytes = 0;
    for (const auto& channel : channels) {
        bytes += static_cast<qint64>(channel.size())
            * static_cast<qint64>(sizeof(float));
    }
    return bytes;
}

QVariantList build_variant_peaks(
    const std::vector<std::vector<float>>& channels)
{
    QVariantList result;
    for (const auto& channel : channels) {
        QVariantList values;
        values.reserve(static_cast<qsizetype>(channel.size()));
        for (const float value : channel) {
            values.append(value);
        }
        result.append(QVariant::fromValue(values));
    }
    return result;
}

qint64 clamped_int64_to_qint64(const std::size_t value)
{
    return value > static_cast<std::size_t>(std::numeric_limits<qint64>::max())
        ? std::numeric_limits<qint64>::max() : static_cast<qint64>(value);
}

qint64 spanFromBucket(const qint64 frame, const qint64 totalFrames,
                      const qint64 peakBuckets)
{
    if (totalFrames <= 0 || peakBuckets <= 0) {
        return 0;
    }
    return std::clamp(
        (frame * peakBuckets) / totalFrames, 0LL,
        peakBuckets - 1);
}

qint64 mapFrameToBucket(const qint64 frame, const qint64 frameStart,
                        const qint64 frameEnd, const qint64 totalBuckets)
{
    const qint64 frameCount = std::max<qint64>(1, frameEnd - frameStart);
    const qint64 offset = std::max<qint64>(0, frame - frameStart);
    return std::clamp(
        (offset * totalBuckets) / frameCount, 0LL, std::max<qint64>(0, totalBuckets - 1));
}

void assignBucketRange(const std::vector<float>& sourceValues,
                      const qint64 sourceBuckets,
                      const qint64 totalFrames,
                      const qint64 requestStart,
                      const qint64 requestEnd,
                      const qint64 targetPoints,
                      std::vector<float>& outputMin,
                      std::vector<float>& outputMax)
{
    const qint64 requestFrames = requestEnd - requestStart;
    for (qint64 point = 0; point < targetPoints; ++point) {
        const qint64 pointStart = requestStart + point * requestFrames
            / targetPoints;
        const qint64 pointEnd = requestStart + (point + 1) * requestFrames
            / targetPoints;
        const qint64 bucketStart = spanFromBucket(
            pointStart, totalFrames, sourceBuckets);
        const qint64 bucketEnd = spanFromBucket(
            pointEnd, totalFrames, sourceBuckets);
        const qint64 first = std::min(bucketStart, bucketEnd);
        const qint64 last = std::max(bucketStart, bucketEnd);
        float minimum = 0.0F;
        float maximum = 0.0F;
        bool hasValue = false;
        for (qint64 bucket = first; bucket <= last
             && bucket < sourceBuckets; ++bucket) {
            const qint64 minIndex = bucket * 2;
            const qint64 maxIndex = minIndex + 1;
            if (minIndex + 1 >= static_cast<qint64>(sourceValues.size())
                || maxIndex >= static_cast<qint64>(sourceValues.size())) {
                continue;
            }
            const float minimumCandidate = sourceValues[minIndex];
            const float maximumCandidate = sourceValues[maxIndex];
            if (!hasValue) {
                minimum = minimumCandidate;
                maximum = maximumCandidate;
                hasValue = true;
            } else {
                minimum = std::min(minimum, minimumCandidate);
                maximum = std::max(maximum, maximumCandidate);
            }
        }
        if (!hasValue) {
            outputMin[static_cast<size_t>(point)] = 0.0F;
            outputMax[static_cast<size_t>(point)] = 0.0F;
        } else {
            outputMin[static_cast<size_t>(point)] = minimum;
            outputMax[static_cast<size_t>(point)] = maximum;
        }
    }
}

std::vector<std::vector<float>> viewportPeaksFromSnapshot(
    const std::vector<std::vector<float>>& channelSourcePeaks,
    const std::vector<std::vector<float>>& fallbackPeaks,
    const QVariantList& sourcePeaks,
    const qint64 totalFrames,
    const qint64 requestStart,
    const qint64 requestEnd,
    const qint64 targetPoints,
    const int channels)
{
    const qint64 sourceBuckets = static_cast<qint64>(
        sourcePeaks.empty() ? 0 : sourcePeaks.front().size());
    std::vector<std::vector<float>> result;
    result.resize(static_cast<std::size_t>(std::max<qint64>(0, channels)));
    const std::vector<std::vector<float>>* source = nullptr;
    const auto& sourceVector = channelSourcePeaks.empty() ? fallbackPeaks
                                                         : channelSourcePeaks;
    if (!sourceVector.empty()) {
        source = &sourceVector;
    } else {
        result.assign(static_cast<std::size_t>(std::max<qint64>(0, channels)),
                      std::vector<float>(static_cast<std::size_t>(targetPoints * 2U),
                                        0.0F));
        return result;
    }
    const qint64 requestFrames = std::max<qint64>(1, requestEnd - requestStart);
    const qint64 availableChannels = static_cast<qint64>(source->size());
    for (qint64 channelIndex = 0; channelIndex < channels; ++channelIndex) {
        const qint64 safeChannel = std::clamp(
            channelIndex, 0LL, availableChannels - 1 >= 0 ? availableChannels - 1 : 0);
        const auto& sourceChannelRaw = (*source)[static_cast<std::size_t>(safeChannel)];
        if (sourceChannelRaw.empty()) {
            result[static_cast<std::size_t>(channelIndex)].assign(
                static_cast<std::size_t>(targetPoints * 2LL), 0.0F);
            continue;
        }
        const qint64 bucketCount = static_cast<qint64>(sourceChannelRaw.size() / 2);
        auto& minOut = result[static_cast<std::size_t>(channelIndex)];
        auto& maxOut = minOut;
        result[static_cast<std::size_t>(channelIndex)].assign(
            static_cast<std::size_t>(targetPoints * 2LL), 0.0F);
        (void)maxOut;
        std::vector<float> localMin(static_cast<std::size_t>(targetPoints), 0.0F);
        std::vector<float> localMax(static_cast<std::size_t>(targetPoints), 0.0F);
        assignBucketRange(sourceChannelRaw, bucketCount, totalFrames,
                         requestStart, requestEnd, targetPoints, localMin, localMax);
        for (qint64 point = 0; point < targetPoints; ++point) {
            const float minimum = localMin[static_cast<std::size_t>(point)];
            const float maximum = localMax[static_cast<std::size_t>(point)];
            const qint64 sampleIndex = point * 2LL;
            result[static_cast<std::size_t>(channelIndex)]
                  [static_cast<std::size_t>(sampleIndex)] = minimum;
            result[static_cast<std::size_t>(channelIndex)]
                  [static_cast<std::size_t>(sampleIndex + 1)] = maximum;
        }
    }
    (void)requestFrames;
    return result;
}
QVariantList to_variant_peaks(
    const std::vector<std::vector<float>>& channels)
{
    QVariantList result;
    for (const auto& channel : channels) {
        QVariantList values;
        values.reserve(static_cast<qsizetype>(channel.size()));
        for (const float value : channel) {
            values.append(value);
        }
        result.append(QVariant::fromValue(values));
    }
    return result;
}

QString local_path(const QUrl& url)
{
    return url.isLocalFile() ? url.toLocalFile() : QString{};
}

} // namespace

AudioEditorController::AudioEditorController(
    const ag_audio_backend backend, QObject* parent)
    : QObject(parent), actions_(this), viewport_(this), backend_(backend)
{
    ag_player_config config{};
    config.backend = backend_;
    config.buffer_frames = 0;
    if (ag_player_create_with_config(&config, &player_) != AG_OK) {
        player_ = nullptr;
    }
    playback_timer_.setInterval(17);
    connect(&playback_timer_, &QTimer::timeout,
            this, &AudioEditorController::pollPlayback);
    recording_timer_.setInterval(33);
    connect(&recording_timer_, &QTimer::timeout, this, [this] {
        if (!recording()) return;
        const auto capturedFrames = static_cast<qint64>(
            recording_session_.framesCaptured());
        const qint64 sampleRate = std::max<qint64>(1, recording_sample_rate_);
        const qint64 recordingPosition = static_cast<qint64>(std::llround(
            static_cast<double>(capturedFrames) * 1'000.0 / static_cast<double>(sampleRate)));
        if (position_ms_ != recordingPosition) position_ms_ = recordingPosition;
        const qint64 projectedFrames = std::max<qint64>(
            document_.totalFrames(),
            (insert_recording_at_cursor_ ? recording_insert_frame_ + capturedFrames
                                        : capturedFrames));
        if (projectedFrames != 0 && projectedFrames > viewport_.documentFrames()) {
            viewport_.setDocumentFrames(projectedFrames);
        }
        emit recordingChanged();
        emit playbackChanged();
    });
    QSettings settings;
    settings.beginGroup(QStringLiteral("audioEditor"));
    recording_directory_ = settings.value(
        QStringLiteral("recordingDirectory")).toString();
    recording_device_id_ = settings.value(
        QStringLiteral("recordingDeviceId")).toString();
    recording_sample_rate_ = settings.value(
        QStringLiteral("recordingSampleRate"), 48'000).toInt();
    recording_channels_ = settings.value(
        QStringLiteral("recordingChannels"), 2).toInt();
    recording_monitor_ = settings.value(
        QStringLiteral("recordingMonitor"), false).toBool();
    settings.endGroup();
    if (!recording_directory_.isEmpty()) {
        (void)agplayer::editor::RecordingSession::recoverIncomplete(
            std::filesystem::path(recording_directory_.toStdWString()));
    }
    export_formats_ = buildExportFormats();
    refreshRecordingDevices();
    refreshActions();
}

AudioEditorController::~AudioEditorController()
{
    cancelOperation();
    if (write_watcher_) write_watcher_->future().waitForFinished();
    if (time_pitch_watcher_) time_pitch_watcher_->future().waitForFinished();
    if (recording()) (void)recording_session_.stop();
    if (player_) {
        ag_player_stop(player_);
        ag_player_destroy(player_);
    }
}

bool AudioEditorController::recording() const noexcept
{
    using agplayer::editor::RecordingState;
    const auto value = recording_session_.state();
    return value == RecordingState::Recording
        || value == RecordingState::Paused
        || value == RecordingState::Finalizing;
}

bool AudioEditorController::recordingPaused() const noexcept
{
    return recording_session_.state()
        == agplayer::editor::RecordingState::Paused;
}

double AudioEditorController::inputLevel() const noexcept
{
    return std::clamp(static_cast<double>(recording_session_.peak()), 0.0, 1.0);
}

qint64 AudioEditorController::recordingFrames() const noexcept
{
    return recording_session_.framesCaptured();
}

QVariantList AudioEditorController::buildExportFormats() const
{
    struct Candidate final {
        const char* key;
        const char* label;
        const char* extension;
        const char* codec;
        const char* filter;
    };
    static constexpr Candidate candidates[]{
        {"wav", QT_TRANSLATE_NOOP("AudioEditorController", "WAV PCM 24-bit"),
         "wav", "pcm_s24le", QT_TRANSLATE_NOOP("AudioEditorController", "WAV 音频 (*.wav)")},
        {"flac", QT_TRANSLATE_NOOP("AudioEditorController", "FLAC 无损"),
         "flac", "flac", QT_TRANSLATE_NOOP("AudioEditorController", "FLAC 音频 (*.flac)")},
        {"mp3", "MP3", "mp3", "libmp3lame",
         QT_TRANSLATE_NOOP("AudioEditorController", "MP3 音频 (*.mp3)")},
        {"m4a", "AAC", "m4a", "aac",
         QT_TRANSLATE_NOOP("AudioEditorController", "AAC 音频 (*.m4a)")},
        {"ogg", "Ogg Vorbis", "ogg", "libvorbis", "Ogg Vorbis (*.ogg)"},
        {"opus", "Opus", "opus", "libopus",
         QT_TRANSLATE_NOOP("AudioEditorController", "Opus 音频 (*.opus)")},
    };
    const auto capabilities = agplayer::transcode_capabilities();
    QVariantList result;
    for (const Candidate& candidate : candidates) {
        const auto* capability = agplayer::find_transcode_capability(
            capabilities, candidate.key);
        if (capability == nullptr || !capability->available
            || ag_encoder_available(candidate.codec) == 0) {
            continue;
        }
        QVariantList rates;
        for (const int rate : capability->sample_rates) rates.append(rate);
        result.append(QVariantMap{
            {QStringLiteral("text"), tr(candidate.label)},
            {QStringLiteral("extension"), QString::fromLatin1(candidate.extension)},
            {QStringLiteral("codec"), QString::fromLatin1(candidate.codec)},
            {QStringLiteral("filter"), tr(candidate.filter)},
            {QStringLiteral("lossy"), capability->lossy},
            {QStringLiteral("supportsMetadata"), capability->supports_metadata
                && std::string_view(candidate.key) != "wav"},
            {QStringLiteral("sampleRates"), rates},
        });
    }
    return result;
}

bool AudioEditorController::busy() const noexcept
{
    return state_ == EditorSessionState::Processing
        || state_ == EditorSessionState::Saving
        || state_ == EditorSessionState::Exporting
        || state_ == EditorSessionState::Finalizing;
}

QVariantList AudioEditorController::markers() const
{
    QVariantList result;
    for (const auto& marker : document_.markers()) {
        result.append(QVariantMap{
            {QStringLiteral("name"), QString::fromStdString(marker.name)},
            {QStringLiteral("frame"), marker.frame},
            {QStringLiteral("positionMs"), sample_rate_ > 0
                ? marker.frame * 1'000 / sample_rate_ : 0}});
    }
    return result;
}

qint64 AudioEditorController::selectionStart() const noexcept
{
    const auto selection = document_.snapshot().selection;
    return selection ? selection->start : -1;
}

qint64 AudioEditorController::selectionEnd() const noexcept
{
    const auto selection = document_.snapshot().selection;
    return selection ? selection->end : -1;
}

qint64 AudioEditorController::selectionFrames() const noexcept
{
    const auto selection = document_.snapshot().selection;
    return selection ? selection->end - selection->start : 0;
}

QString AudioEditorController::fileName() const
{
    return source_path_.isEmpty() ? tr("未命名音频")
                                  : QFileInfo(source_path_).fileName();
}

qint64 AudioEditorController::durationMs() const noexcept
{
    return sample_rate_ > 0 ? totalFrames() * 1'000 / sample_rate_ : 0;
}

bool AudioEditorController::createUntitledDocument(
    const quint32 sampleRate, const quint32 channels, const qint64 frames)
{
    auto candidate = AudioDocument::fromSource(
        AudioSource{std::filesystem::path{}, sampleRate, channels, frames});
    if (candidate.totalFrames() <= 0) {
        return false;
    }
    stopPlayback();
    document_ = std::move(candidate);
    source_path_.clear();
    playback_path_.clear();
    format_name_ = QStringLiteral("WAV");
    sample_rate_ = static_cast<int>(sampleRate);
    channels_ = static_cast<int>(channels);
    bits_per_sample_ = 24;
    bit_rate_ = 0;
    source_channel_peaks_.clear();
    channel_peaks_.clear();
    has_document_ = true;
    modified_ = false;
    viewport_.setDocumentFrames(frames);
    setState(EditorSessionState::Ready);
    refreshActions();
    emit waveformChanged();
    emit documentChanged();
    return true;
}

bool AudioEditorController::openFile(const QUrl& source)
{
    if (modified_ && !allow_document_replace_) {
        pending_open_url_ = source;
        emit discardConfirmationRequested();
        return false;
    }
    allow_document_replace_ = false;
    const QString path = local_path(source);
    if (path.isEmpty()) {
        setError(tr("请选择本地音频文件"));
        return false;
    }
    stopPlayback();
    setProgress(0.05);
    const AudioFileAnalysis analysis = AudioFileAnalyzer::analyze(
        std::filesystem::path(path.toStdWString()), 2'048);
    if (!analysis.success) {
        setProgress(0.0);
        setError(QString::fromStdString(analysis.message));
        return false;
    }
    document_ = AudioDocument::fromSource(analysis.source);
    source_path_ = path;
    playback_path_ = path;
    format_name_ = QString::fromStdString(analysis.format).toUpper();
    sample_rate_ = static_cast<int>(analysis.source.sample_rate);
    channels_ = static_cast<int>(analysis.source.channels);
    bits_per_sample_ = analysis.bits_per_sample;
    bit_rate_ = analysis.bit_rate;
    source_channel_peaks_ = to_variant_peaks(analysis.channel_peaks);
    channel_peaks_ = source_channel_peaks_;
    has_document_ = true;
    modified_ = false;
    position_ms_ = 0;
    viewport_.setDocumentFrames(document_.totalFrames());
    setState(EditorSessionState::Ready);
    setProgress(1.0);
    setError({});
    refreshActions();
    emit waveformChanged();
    emit playbackChanged();
    emit documentChanged();
    return true;
}

bool AudioEditorController::confirmDiscardAndOpen()
{
    if (!pending_open_url_.isValid()) return false;
    const QUrl source = pending_open_url_;
    pending_open_url_.clear();
    allow_document_replace_ = true;
    return openFile(source);
}

void AudioEditorController::cancelDiscardAndOpen()
{
    pending_open_url_.clear();
}

bool AudioEditorController::save()
{
    if (source_path_.isEmpty()) {
        emit saveAsRequested();
        return false;
    }
    return saveAs(QUrl::fromLocalFile(source_path_));
}

bool AudioEditorController::saveAs(const QUrl& target)
{
    const QString path = local_path(target);
    if (!has_document_ || path.isEmpty()) {
        setError(tr("保存路径无效"));
        return false;
    }
    if (busy()) return false;
    stopPlayback();
    setState(EditorSessionState::Saving);
    setProgress(0.0);
    WriteRequest request;
    request.snapshot = document_.snapshot();
    request.output_path = std::filesystem::path(path.toStdWString());
    operation_cancelled_.store(false, std::memory_order_release);
    auto* watcher = new QFutureWatcher<agplayer::editor::WriteResult>(this);
    write_watcher_ = watcher;
    connect(watcher, &QFutureWatcher<agplayer::editor::WriteResult>::finished,
            this, [this, watcher, path] {
        write_watcher_ = nullptr;
        const auto result = watcher->result();
        watcher->deleteLater();
        if (!result.ok()) {
            setState(result.error == agplayer::editor::WriteError::Cancelled
                ? EditorSessionState::Ready : EditorSessionState::Error);
            setError(result.error == agplayer::editor::WriteError::Cancelled
                ? tr("操作已取消") : QString::fromStdString(result.message));
            return;
        }
        source_path_ = path;
        playback_path_ = path;
        modified_ = false;
        setProgress(1.0);
        setState(EditorSessionState::Ready);
        setError({});
        refreshActions();
        emit documentChanged();
    });
    const QPointer<AudioEditorController> guard(this);
    watcher->setFuture(QtConcurrent::run([this, request, guard] {
        return DocumentWriter{}.write(request, &operation_cancelled_,
            [guard](const float value) {
                if (guard) QMetaObject::invokeMethod(
                    guard, [guard, value] { if (guard) guard->setProgress(value); },
                    Qt::QueuedConnection);
            });
    }));
    return true;
}

bool AudioEditorController::exportTo(
    const QUrl& target, const bool selectionOnly, const QString& codecName,
    const int sampleRate, const int channels, const qint64 bitRate,
    const bool keepMetadata, const bool variableBitRate, const int quality)
{
    const QString path = local_path(target);
    const auto selection = document_.snapshot().selection;
    if (!has_document_ || path.isEmpty() || (selectionOnly && !selection)) {
        setError(tr("导出范围或路径无效"));
        return false;
    }
    const bool validSampleRate = sampleRate == 0
        || (sampleRate >= 8'000 && sampleRate <= 384'000);
    const bool validChannels = channels >= 0 && channels <= 2;
    const bool validBitRate = bitRate >= 0 && bitRate <= 1'536'000;
    if (!validSampleRate || !validChannels || !validBitRate
        || quality < 0 || quality > 100) {
        setError(tr("导出参数无效"));
        return false;
    }
    if (busy()) return false;
    setState(EditorSessionState::Exporting);
    setProgress(0.0);
    WriteRequest request;
    request.snapshot = document_.snapshot();
    request.output_path = std::filesystem::path(path.toStdWString());
    request.codec_name = codecName.toStdString();
    if (!source_path_.isEmpty()) {
        request.metadata_source_path = std::filesystem::path(
            source_path_.toStdWString());
    }
    request.sample_rate = sampleRate;
    request.channels = channels;
    request.bit_rate = bitRate;
    request.keep_metadata = keepMetadata;
    request.variable_bit_rate = variableBitRate;
    request.quality = quality;
    if (selectionOnly) {
        request.range = selection;
    }
    operation_cancelled_.store(false, std::memory_order_release);
    auto* watcher = new QFutureWatcher<agplayer::editor::WriteResult>(this);
    write_watcher_ = watcher;
    connect(watcher, &QFutureWatcher<agplayer::editor::WriteResult>::finished,
            this, [this, watcher] {
        write_watcher_ = nullptr;
        const auto result = watcher->result();
        watcher->deleteLater();
        if (!result.ok()) {
            setState(result.error == agplayer::editor::WriteError::Cancelled
                ? EditorSessionState::Ready : EditorSessionState::Error);
            setError(result.error == agplayer::editor::WriteError::Cancelled
                ? tr("操作已取消") : QString::fromStdString(result.message));
            return;
        }
        setProgress(1.0);
        setState(EditorSessionState::Ready);
        setError({});
    });
    const QPointer<AudioEditorController> guard(this);
    watcher->setFuture(QtConcurrent::run([this, request, guard] {
        return DocumentWriter{}.write(request, &operation_cancelled_,
            [guard](const float value) {
                if (guard) QMetaObject::invokeMethod(
                    guard, [guard, value] { if (guard) guard->setProgress(value); },
                    Qt::QueuedConnection);
            });
    }));
    return true;
}

bool AudioEditorController::setSelection(
    const qint64 startFrame, const qint64 endFrame)
{
    if (!has_document_ || !document_.setSelection({startFrame, endFrame})) {
        return false;
    }
    refreshActions();
    emit documentChanged();
    return true;
}

bool AudioEditorController::clearSelection()
{
    if (!document_.clearSelection()) {
        return false;
    }
    refreshActions();
    emit documentChanged();
    return true;
}

bool AudioEditorController::addMarker(const QString& name, const qint64 frame)
{
    if (!has_document_ || !document_.addMarker({name.toStdString(), frame})) {
        return false;
    }
    modified_ = true;
    refreshActions();
    emit documentChanged();
    return true;
}

bool AudioEditorController::renameMarker(const int index, const QString& name)
{
    const QString normalized = name.trimmed();
    if (!has_document_ || index < 0 || normalized.isEmpty()
        || !document_.renameMarker(static_cast<std::size_t>(index),
                                   normalized.toStdString())) {
        return false;
    }
    modified_ = true;
    refreshActions();
    emit documentChanged();
    return true;
}

bool AudioEditorController::removeMarker(const int index)
{
    if (!has_document_ || index < 0
        || !document_.removeMarker(static_cast<std::size_t>(index))) {
        return false;
    }
    modified_ = true;
    refreshActions();
    emit documentChanged();
    return true;
}

bool AudioEditorController::seekPreviousMarker()
{
    const qint64 current = position_ms_ * sample_rate_ / 1'000;
    const auto& items = document_.markers();
    auto match = std::lower_bound(items.begin(), items.end(), current,
        [](const auto& marker, const qint64 frame) { return marker.frame < frame; });
    if (match == items.begin()) return false;
    --match;
    return seekMs(match->frame * 1'000 / sample_rate_);
}

bool AudioEditorController::seekNextMarker()
{
    const qint64 current = position_ms_ * sample_rate_ / 1'000;
    const auto& items = document_.markers();
    const auto match = std::upper_bound(items.begin(), items.end(), current,
        [](const qint64 frame, const auto& marker) { return frame < marker.frame; });
    return match != items.end()
        && seekMs(match->frame * 1'000 / sample_rate_);
}

void AudioEditorController::cancelOperation()
{
    operation_cancelled_.store(true, std::memory_order_release);
}

bool AudioEditorController::insertSilence(
    const qint64 frame, const qint64 frameCount)
{
    return runDocumentCommand(EditCommand::insertSilence(frame, frameCount));
}

bool AudioEditorController::applyGain(const double decibels)
{
    if (!std::isfinite(decibels) || decibels < -60.0 || decibels > 24.0) {
        return false;
    }
    return runDocumentCommand(EditCommand::gain(
        static_cast<float>(std::pow(10.0, decibels / 20.0))));
}

bool AudioEditorController::normalize()
{
    float peak = 0.0F;
    for (const QVariant& channelValue : channel_peaks_) {
        for (const QVariant& value : channelValue.toList()) {
            peak = std::max(peak, static_cast<float>(std::abs(value.toDouble())));
        }
    }
    return peak > 0.0F && runDocumentCommand(
        EditCommand::gain(0.89125094F / peak));
}

bool AudioEditorController::detectBpm()
{
    if (!has_document_ || source_path_.isEmpty()) return false;
    const BpmAnalyzeResult result = analyze_bpm(source_path_);
    if (result.bpm <= 0.0) {
        setError(tr("BPM 检测失败"));
        return false;
    }
    time_pitch_.setOriginalBpm(result.bpm);
    emit timePitchChanged();
    return true;
}

void AudioEditorController::setOriginalBpm(const double value)
{
    time_pitch_.setOriginalBpm(value);
    emit timePitchChanged();
}

bool AudioEditorController::setTargetBpm(const double value)
{
    const bool changed = time_pitch_.setTargetBpm(value);
    if (changed) {
        stopPlayback();
        playback_path_.clear();
        time_pitch_preview_active_ = false;
        emit timePitchChanged();
    }
    return changed;
}

bool AudioEditorController::setSpeedPercent(const double value)
{
    const bool changed = time_pitch_.setSpeedPercent(value);
    if (changed) {
        stopPlayback();
        playback_path_.clear();
        time_pitch_preview_active_ = false;
        emit timePitchChanged();
    }
    return changed;
}

void AudioEditorController::setKeepPitch(const bool value)
{
    if (time_pitch_.keepPitch() == value) return;
    time_pitch_.setKeepPitch(value);
    stopPlayback();
    playback_path_.clear();
    time_pitch_preview_active_ = false;
    emit timePitchChanged();
}

bool AudioEditorController::setPitch(const int semitones, const int cents)
{
    const bool changed = time_pitch_.setPitch(semitones, cents);
    if (changed) {
        stopPlayback();
        playback_path_.clear();
        time_pitch_preview_active_ = false;
        emit timePitchChanged();
    }
    return changed;
}

bool AudioEditorController::applyTimePitch()
{
    if (!has_document_ || !preview_directory_.isValid() || busy()) return false;
    stopPlayback();
    setState(EditorSessionState::Processing);
    const auto snapshot = document_.snapshot();
    const auto range = snapshot.selection;
    const QString output = preview_directory_.filePath(
        QStringLiteral("processed-%1.wav").arg(
            QDateTime::currentMSecsSinceEpoch()));
    operation_cancelled_.store(false, std::memory_order_release);
    const auto parameters = time_pitch_;
    auto* watcher = new QFutureWatcher<agplayer::editor::TimePitchResult>(this);
    time_pitch_watcher_ = watcher;
    connect(watcher, &QFutureWatcher<agplayer::editor::TimePitchResult>::finished,
            this, [this, watcher, range] {
        time_pitch_watcher_ = nullptr;
        const auto result = watcher->result();
        watcher->deleteLater();
        if (!result.success) {
            const bool cancelled = operation_cancelled_.load(
                std::memory_order_acquire);
            setState(cancelled ? EditorSessionState::Ready
                               : EditorSessionState::Error);
            setError(cancelled ? tr("操作已取消")
                               : QString::fromStdString(result.message));
            return;
        }
        const Selection replacement = range.value_or(
            Selection{0, document_.totalFrames()});
        if (!document_.replaceRangeWithSource(result.source, replacement)) {
            setState(EditorSessionState::Error);
            setError(tr("无法提交速度与音高处理结果"));
            return;
        }
        modified_ = true;
        playback_path_.clear();
        (void)time_pitch_.setSpeedPercent(100.0);
        (void)time_pitch_.setPitch(0, 0);
        time_pitch_preview_active_ = false;
        viewport_.setDocumentFrames(document_.totalFrames());
        const AudioFileAnalysis analysis = AudioFileAnalyzer::analyze(
            result.source.path, 2'048);
        if (analysis.success) {
            source_channel_peaks_ = to_variant_peaks(analysis.channel_peaks);
        }
        rebuildEditorPeaks();
        setProgress(1.0);
        setState(EditorSessionState::Ready);
        setError({});
        refreshActions();
        emit waveformChanged();
        emit documentChanged();
        emit timePitchChanged();
    });
    const QPointer<AudioEditorController> guard(this);
    watcher->setFuture(QtConcurrent::run(
        [this, parameters, snapshot, output, range, guard] {
        return parameters.process(
            snapshot, std::filesystem::path(output.toStdWString()), range,
            &operation_cancelled_, [guard](const float value) {
                if (guard) QMetaObject::invokeMethod(
                    guard, [guard, value] { if (guard) guard->setProgress(value); },
                    Qt::QueuedConnection);
            });
    }));
    return true;
}

void AudioEditorController::refreshRecordingDevices()
{
    QVariantList result;
    for (const auto& device : agplayer::editor::RecordingSession::inputDevices()) {
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), QString::fromStdString(device.id));
        entry.insert(QStringLiteral("name"), QString::fromUtf8(device.name));
        entry.insert(QStringLiteral("isDefault"), device.is_default);
        result.append(entry);
    }
    recording_devices_ = std::move(result);
    emit recordingDevicesChanged();
}

bool AudioEditorController::startRecording(
    const QUrl& target, const QString& deviceId,
    const int recordingSampleRate, const int recordingChannels,
    const bool monitor, const bool insertAtCursor)
{
    const int effectiveSampleRate = std::max(1, recordingSampleRate);
    const int effectiveChannels = std::max(1, recordingChannels);
    QString path = local_path(target);
    if (path.isEmpty()) {
        QString directory = recording_directory_;
        if (directory.isEmpty()) {
            directory = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        }
        if (directory.isEmpty()) {
            directory = QDir::currentPath();
        }
        QDir outputDir(directory);
        if (!outputDir.exists()) {
            outputDir.mkpath(QStringLiteral("."));
        }
        const QString timestamp = QDateTime::currentDateTime().toString(
            QStringLiteral("yyyyMMdd_HHmmss"));
        int attempt = 0;
        do {
            path = outputDir.filePath(QStringLiteral("AgPlayer_Recording_%1%2.wav")
                                     .arg(timestamp, (attempt > 0
                                         ? QString("_%1").arg(attempt)
                                         : QString{})));
            ++attempt;
        } while (QFileInfo::exists(path));
        recording_directory_ = outputDir.absolutePath();
    }
    if (recording()) return false;
    stopPlayback();
    agplayer::editor::RecordingConfig config;
    config.output_path = std::filesystem::path(path.toStdWString());
    config.device_id = deviceId.toStdString();
    config.sample_rate = static_cast<std::uint32_t>(effectiveSampleRate);
    config.channels = static_cast<std::uint32_t>(effectiveChannels);
    config.monitor = monitor;
    insert_recording_at_cursor_ = insertAtCursor && has_document_;
    recording_insert_frame_ = position_ms_ * sample_rate_ / 1'000;
    if (!recording_session_.start(config)) {
        setError(tr("无法启动录音设备，请检查设备与权限"));
        return false;
    }
    QSettings settings;
    settings.beginGroup(QStringLiteral("audioEditor"));
    recording_directory_ = QFileInfo(path).absolutePath();
    recording_device_id_ = deviceId;
    recording_sample_rate_ = effectiveSampleRate;
    recording_channels_ = effectiveChannels;
    recording_monitor_ = monitor;
    settings.setValue(QStringLiteral("recordingDirectory"), recording_directory_);
    settings.setValue(QStringLiteral("recordingDeviceId"), recording_device_id_);
    settings.setValue(QStringLiteral("recordingSampleRate"), recording_sample_rate_);
    settings.setValue(QStringLiteral("recordingChannels"), recording_channels_);
    settings.setValue(QStringLiteral("recordingMonitor"), recording_monitor_);
    settings.endGroup();
    emit recordingPreferencesChanged();
    recording_timer_.start();
    position_ms_ = insert_recording_at_cursor_ ? position_ms_ : 0;
    setState(EditorSessionState::Recording);
    setError({});
    emit playbackChanged();
    emit recordingChanged();
    return true;
}

bool AudioEditorController::pauseRecording()
{
    if (!recording_session_.pause()) return false;
    setState(EditorSessionState::RecordingPaused);
    emit recordingChanged();
    return true;
}

bool AudioEditorController::resumeRecording()
{
    if (!recording_session_.resume()) return false;
    setState(EditorSessionState::Recording);
    emit recordingChanged();
    return true;
}

bool AudioEditorController::stopRecording()
{
    if (!recording()) return false;
    setState(EditorSessionState::Finalizing);
    recording_timer_.stop();
    const auto result = recording_session_.stop();
    if (!result.success) {
        setState(EditorSessionState::Error);
        setError(QString::fromStdString(result.message));
        emit recordingChanged();
        return false;
    }
    const auto analysis = AudioFileAnalyzer::analyze(result.path, 2'048);
    if (!analysis.success) {
        setState(EditorSessionState::Error);
        setError(QString::fromStdString(analysis.message));
        emit recordingChanged();
        return false;
    }
    if (insert_recording_at_cursor_) {
        if (!document_.insertSource(analysis.source, recording_insert_frame_)) {
            setState(EditorSessionState::Error);
            setError(tr("录音完成，但无法插入当前文档"));
            emit recordingChanged();
            return false;
        }
        modified_ = true;
        playback_path_.clear();
        viewport_.setDocumentFrames(document_.totalFrames());
        rebuildEditorPeaks();
    } else {
        document_ = AudioDocument::fromSource(analysis.source);
        source_path_ = QString::fromStdWString(result.path.wstring());
        playback_path_ = source_path_;
        format_name_ = QStringLiteral("WAV");
        sample_rate_ = static_cast<int>(analysis.source.sample_rate);
        channels_ = static_cast<int>(analysis.source.channels);
        bits_per_sample_ = 24;
        bit_rate_ = sample_rate_ * channels_ * bits_per_sample_;
        source_channel_peaks_ = to_variant_peaks(analysis.channel_peaks);
        channel_peaks_ = source_channel_peaks_;
        has_document_ = true;
        modified_ = false;
        viewport_.setDocumentFrames(document_.totalFrames());
    }
    setState(EditorSessionState::Ready);
    refreshActions();
    emit waveformChanged();
    emit documentChanged();
    emit recordingChanged();
    emit playbackChanged();
    return true;
}

bool AudioEditorController::cancelRecording()
{
    if (!recording()) return false;
    recording_timer_.stop();
    const bool cancelled = recording_session_.cancel();
    position_ms_ = 0;
    setState(has_document_ ? EditorSessionState::Ready
                           : EditorSessionState::Empty);
    if (cancelled) setError(tr("录音已取消"));
    emit recordingChanged();
    emit playbackChanged();
    return cancelled;
}

bool AudioEditorController::actionEnabled(const QString& id) const noexcept
{
    const EditorAction* const item = actions_.action(id);
    return item != nullptr && item->enabled;
}

bool AudioEditorController::triggerAction(const QString& id)
{
    EditorAction* const item = action(id);
    if (!item || !item->enabled) {
        return false;
    }
    if (id == QStringLiteral("editor.open")) {
        emit openRequested();
        return true;
    }
    if (id == QStringLiteral("editor.newRecording")) {
        emit newRecordingRequested();
        return true;
    }
    if (id == QStringLiteral("editor.save")) return save();
    if (id == QStringLiteral("editor.export")) {
        emit exportRequested();
        return true;
    }
    if (id == QStringLiteral("editor.moreMenu")) {
        emit moreMenuRequested();
        return true;
    }
    bool changed = false;
    if (id == QStringLiteral("editor.undo")) changed = document_.undo();
    else if (id == QStringLiteral("editor.redo")) changed = document_.redo();
    else if (id == QStringLiteral("editor.cut")) changed = document_.apply(EditCommand::cutSelection());
    else if (id == QStringLiteral("editor.copy")) changed = document_.apply(EditCommand::copySelection());
    else if (id == QStringLiteral("editor.paste")) {
        const qint64 frame = position_ms_ * sample_rate_ / 1'000;
        changed = document_.apply(EditCommand::pasteAt(frame));
    }
    else if (id == QStringLiteral("editor.deleteSelection")) changed = document_.apply(EditCommand::deleteSelection());
    else if (id == QStringLiteral("editor.cropToSelection")) changed = document_.apply(EditCommand::cropToSelection());
    else if (id == QStringLiteral("editor.silenceSelection")) changed = document_.apply(EditCommand::silenceSelection());
    else if (id == QStringLiteral("editor.fadeIn")) changed = document_.apply(EditCommand::fadeIn());
    else if (id == QStringLiteral("editor.fadeOut")) changed = document_.apply(EditCommand::fadeOut());
    if (!changed) return false;
    if (id != QStringLiteral("editor.copy")) {
        stopPlayback();
        modified_ = true;
        playback_path_.clear();
        viewport_.setDocumentFrames(document_.totalFrames());
        rebuildEditorPeaks();
    }
    refreshActions();
    emit waveformChanged();
    emit documentChanged();
    return true;
}

bool AudioEditorController::runDocumentCommand(
    const EditCommand& command, const bool modifiesDocument)
{
    const bool changed = document_.apply(command);
    if (!changed) return false;
    if (modifiesDocument) {
        stopPlayback();
        modified_ = true;
        playback_path_.clear();
        viewport_.setDocumentFrames(document_.totalFrames());
        rebuildEditorPeaks();
    }
    refreshActions();
    emit waveformChanged();
    emit documentChanged();
    return true;
}

bool AudioEditorController::preparePlayback()
{
    if (!has_document_ || !player_) return false;
    if (playback_path_.isEmpty()) {
        if (!preview_directory_.isValid()) {
            setError(tr("无法创建预览目录"));
            return false;
        }
        const bool processed = std::abs(time_pitch_.speedPercent() - 100.0) > 0.001
            || time_pitch_.pitchCents() != 0;
        const QString path = preview_directory_.filePath(
            processed ? QStringLiteral("time-pitch-preview.wav")
                      : QStringLiteral("preview.wav"));
        if (processed) {
            const auto result = time_pitch_.process(
                document_.snapshot(), std::filesystem::path(path.toStdWString()));
            if (!result.success) {
                setError(QString::fromStdString(result.message));
                return false;
            }
        } else {
            const auto rendered = DocumentRenderer{}.renderFloatWav(
                document_.snapshot(), std::nullopt,
                std::filesystem::path(path.toStdWString()));
            if (!rendered.success) {
                setError(QString::fromStdString(rendered.message));
                return false;
            }
        }
        playback_path_ = path;
        if (time_pitch_preview_active_ != processed) {
            time_pitch_preview_active_ = processed;
            emit timePitchChanged();
        }
    }
    if (ag_player_load(player_, playback_path_.toUtf8().constData()) != AG_OK) {
        setError(tr("无法载入编辑预览"));
        return false;
    }
    ag_player_set_volume(player_, static_cast<float>(volume_));
    if (position_ms_ > 0) ag_player_seek(player_, position_ms_);
    return true;
}

bool AudioEditorController::playPause()
{
    if (!has_document_ || !player_) return false;
    if (playing_) {
        if (ag_player_pause(player_) != AG_OK) return false;
        playing_ = false;
        playback_timer_.stop();
        setState(EditorSessionState::Ready);
        emit playbackChanged();
        return true;
    }
    ag_playback_snapshot snapshot{};
    if (ag_player_snapshot(player_, &snapshot) != AG_OK
        || snapshot.state == AG_STOPPED || snapshot.state == AG_ERROR) {
        if (!preparePlayback()) return false;
    }
    if (ag_player_play(player_) != AG_OK) return false;
    playing_ = true;
    playback_timer_.start();
    setState(EditorSessionState::Playing);
    emit playbackChanged();
    return true;
}

bool AudioEditorController::stopPlayback()
{
    if (!player_) return false;
    const bool wasActive = playing_ || position_ms_ != 0;
    ag_player_stop(player_);
    playback_timer_.stop();
    playing_ = false;
    position_ms_ = 0;
    if (has_document_ && state_ == EditorSessionState::Playing) {
        setState(EditorSessionState::Ready);
    }
    if (wasActive) emit playbackChanged();
    return true;
}

bool AudioEditorController::seekMs(const qint64 value)
{
    if (!has_document_ || value < 0 || value > durationMs()) return false;
    position_ms_ = value;
    if (player_) {
        const qint64 preview_position = time_pitch_preview_active_
            ? static_cast<qint64>(std::llround(
                static_cast<double>(value) * 100.0
                / time_pitch_.speedPercent()))
            : value;
        ag_player_seek(player_, preview_position);
    }
    emit playbackChanged();
    return true;
}

void AudioEditorController::setVolume(const double value)
{
    const double bounded = std::clamp(value, 0.0, 1.0);
    if (qFuzzyCompare(volume_, bounded)) return;
    volume_ = bounded;
    if (player_) ag_player_set_volume(player_, static_cast<float>(volume_));
    emit playbackChanged();
}

void AudioEditorController::setLoopEnabled(const bool enabled)
{
    if (loop_enabled_ == enabled) return;
    loop_enabled_ = enabled;
    emit playbackChanged();
}

void AudioEditorController::pollPlayback()
{
    if (!player_) return;
    ag_playback_snapshot snapshot{};
    if (ag_player_snapshot(player_, &snapshot) != AG_OK) return;
    position_ms_ = time_pitch_preview_active_
        ? static_cast<qint64>(std::llround(
            static_cast<double>(snapshot.position_ms)
            * time_pitch_.speedPercent() / 100.0))
        : snapshot.position_ms;
    if (loop_enabled_) {
        const auto selection = document_.snapshot().selection;
        if (selection && sample_rate_ > 0) {
            const qint64 start = selection->start * 1'000 / sample_rate_;
            const qint64 end = selection->end * 1'000 / sample_rate_;
            if (position_ms_ >= end) {
                ag_player_seek(player_, start);
                position_ms_ = start;
            }
        }
    }
    if (snapshot.state != AG_PLAYING) {
        playing_ = false;
        playback_timer_.stop();
        setState(EditorSessionState::Ready);
    }
    emit playbackChanged();
}

void AudioEditorController::rebuildEditorPeaks()
{
    if (source_channel_peaks_.isEmpty() || document_.totalFrames() <= 0) {
        channel_peaks_.clear();
        return;
    }
    const qsizetype points = source_channel_peaks_.constFirst().toList().size() / 2;
    QVariantList rebuilt;
    for (int channel = 0; channel < channels_; ++channel) {
        const QVariantList source = source_channel_peaks_[
            std::min<qsizetype>(static_cast<qsizetype>(channel),
                                source_channel_peaks_.size() - 1)].toList();
        QVariantList values;
        values.reserve(points * 2);
        const auto spans = document_.spans();
        qint64 cursor = 0;
        auto span = spans.cbegin();
        for (qsizetype point = 0; point < points; ++point) {
            const qint64 frame = document_.totalFrames() * point
                / std::max<qsizetype>(1, points - 1);
            while (span != spans.cend() && frame >= cursor + span->frame_count) {
                cursor += span->frame_count;
                ++span;
            }
            if (span == spans.cend() || span->silent || !span->source) {
                values.append(0.0F);
                values.append(0.0F);
                continue;
            }
            const qint64 local = frame - cursor;
            const qint64 sourceFrame = span->source_start + local;
            const qsizetype sourcePoint = static_cast<qsizetype>(std::clamp<qint64>(
                sourceFrame * points / std::max<qint64>(1, span->source->total_frames),
                0, points - 1));
            const float ratio = span->frame_count <= 1 ? 0.0F
                : static_cast<float>(local) / static_cast<float>(span->frame_count - 1);
            const float gain = span->gain_start
                + (span->gain_end - span->gain_start) * ratio;
            values.append(source[sourcePoint * 2].toFloat() * gain);
            values.append(source[sourcePoint * 2 + 1].toFloat() * gain);
        }
        rebuilt.append(QVariant::fromValue(values));
    }
    channel_peaks_ = rebuilt;
}

void AudioEditorController::refreshActions()
{
    const bool selection = document_.snapshot().selection.has_value();
    const bool idle = state_ != EditorSessionState::Saving
        && state_ != EditorSessionState::Exporting
        && state_ != EditorSessionState::Processing
        && state_ != EditorSessionState::Finalizing
        && state_ != EditorSessionState::Recording
        && state_ != EditorSessionState::RecordingPaused;
    actions_.setEnabled(QStringLiteral("editor.open"), idle);
    actions_.setEnabled(QStringLiteral("editor.newRecording"), idle);
    actions_.setEnabled(QStringLiteral("editor.save"), has_document_ && idle);
    actions_.setEnabled(QStringLiteral("editor.export"), has_document_ && idle);
    actions_.setEnabled(QStringLiteral("editor.moreMenu"), has_document_ && idle);
    actions_.setEnabled(QStringLiteral("editor.undo"), document_.canUndo() && idle);
    actions_.setEnabled(QStringLiteral("editor.redo"), document_.canRedo() && idle);
    actions_.setEnabled(QStringLiteral("editor.paste"), document_.hasClipboard() && idle);
    for (const QString& id : {
             QStringLiteral("editor.cut"), QStringLiteral("editor.copy"),
             QStringLiteral("editor.deleteSelection"),
             QStringLiteral("editor.cropToSelection"),
             QStringLiteral("editor.silenceSelection"),
             QStringLiteral("editor.fadeIn"), QStringLiteral("editor.fadeOut")}) {
        actions_.setEnabled(id, has_document_ && selection && idle);
    }
}

void AudioEditorController::setState(const EditorSessionState value)
{
    if (state_ == value) return;
    state_ = value;
    refreshActions();
    emit stateChanged();
}

void AudioEditorController::setError(QString message)
{
    if (error_message_ == message) return;
    error_message_ = std::move(message);
    emit errorMessageChanged();
}

void AudioEditorController::setProgress(const double value)
{
    const double bounded = std::clamp(value, 0.0, 1.0);
    if (qFuzzyCompare(progress_, bounded)) return;
    progress_ = bounded;
    emit progressChanged();
}
