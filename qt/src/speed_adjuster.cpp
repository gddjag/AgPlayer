#include "speed_adjuster.hpp"
#include "bpm_analyzer.hpp"

#include <agplayer/c_api.h>

#include <QFileInfo>
#include <QFutureWatcher>
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
    // Default to source extension (empty codec name => same as input).
    return {nullptr, nullptr};
}

} // namespace

SpeedAdjuster::SpeedAdjuster(QObject* parent)
    : QObject(parent)
{
}

SpeedAdjuster::~SpeedAdjuster()
{
    // If a background task is still running, the token is owned by the task
    // lambda and destroyed there. Just ensure we don't dangle.
    token_.store(nullptr, std::memory_order_release);
}

double SpeedAdjuster::progress() const noexcept
{
    return progress_.load(std::memory_order_acquire);
}

bool SpeedAdjuster::busy() const noexcept
{
    return busy_.load(std::memory_order_acquire);
}

QString SpeedAdjuster::inputFileName() const noexcept
{
    return inputFileName_;
}

bool SpeedAdjuster::hasInput() const noexcept
{
    return !inputPath_.isEmpty();
}

int SpeedAdjuster::inputDurationMs() const noexcept
{
    return inputDurationMs_;
}

QVariantList SpeedAdjuster::waveformPeaks() const noexcept
{
    return waveformPeaks_;
}

double SpeedAdjuster::detectedBpm() const noexcept
{
    return detectedBpm_;
}

double SpeedAdjuster::bpmConfidence() const noexcept
{
    return bpmConfidence_;
}

double SpeedAdjuster::targetBpm() const noexcept
{
    return targetBpm_;
}

void SpeedAdjuster::setTargetBpm(double value)
{
    if (value < 20.0) value = 20.0;
    if (value > 300.0) value = 300.0;
    if (qFuzzyCompare(targetBpm_, value)) return;
    targetBpm_ = value;
    emit targetBpmChanged();
}

double SpeedAdjuster::speedPercentage() const noexcept
{
    if (detectedBpm_ <= 0.0) return 100.0;
    return (targetBpm_ / detectedBpm_) * 100.0;
}

bool SpeedAdjuster::keepPitch() const noexcept
{
    return keepPitch_;
}

void SpeedAdjuster::setKeepPitch(bool value)
{
    if (keepPitch_ == value) return;
    keepPitch_ = value;
    emit keepPitchChanged();
}

bool SpeedAdjuster::beatAlign() const noexcept
{
    return beatAlign_;
}

void SpeedAdjuster::setBeatAlign(bool value)
{
    if (beatAlign_ == value) return;
    beatAlign_ = value;
    emit beatAlignChanged();
}

QVariantList SpeedAdjuster::markers() const noexcept
{
    return markers_;
}

void SpeedAdjuster::setBusy(bool value)
{
    busy_.store(value, std::memory_order_release);
    emit busyChanged();
}

void SpeedAdjuster::setProgress(double value)
{
    progress_.store(value, std::memory_order_release);
    emit progressChanged();
}

void SpeedAdjuster::refreshWaveform()
{
    QVariantList peaks;
    if (!inputPath_.isEmpty()) {
        const QByteArray inputUtf8 = inputPath_.toUtf8();
        ag_waveform* waveform = nullptr;
        if (ag_waveform_analyze(inputUtf8.constData(), 256, nullptr, nullptr,
                                nullptr, &waveform) == AG_OK
            && waveform != nullptr) {
            const size_t count = ag_waveform_count(waveform);
            peaks.reserve(static_cast<int>(count));
            for (size_t i = 0; i < count; ++i) {
                peaks.append(static_cast<double>(ag_waveform_peak(waveform, i)));
            }
            ag_waveform_destroy(waveform);
        }
    }
    waveformPeaks_ = std::move(peaks);
    emit waveformPeaksChanged();
}

void SpeedAdjuster::loadFile(const QUrl& url)
{
    if (busy_.load(std::memory_order_acquire)) return;

    inputPath_ = url.toLocalFile();
    inputFileName_ = QFileInfo(inputPath_).fileName();

    const QByteArray inputUtf8 = inputPath_.toUtf8();
    ag_metadata* metadata = nullptr;
    if (ag_metadata_open(inputUtf8.constData(), &metadata) == AG_OK
        && metadata != nullptr) {
        inputDurationMs_ = static_cast<int>(ag_metadata_duration_ms(metadata));
        ag_metadata_destroy(metadata);
    } else {
        inputDurationMs_ = 0;
    }

    refreshWaveform();
    analyzeBpm();

    emit inputFileChanged();
}

void SpeedAdjuster::analyzeBpm()
{
    if (inputPath_.isEmpty()) {
        detectedBpm_ = 0.0;
        bpmConfidence_ = 0.0;
        emit bpmChanged();
        return;
    }

    const agplayer::BpmAnalyzeResult result = agplayer::analyze_bpm(inputPath_);
    detectedBpm_ = result.bpm;
    bpmConfidence_ = result.confidence;
    targetBpm_ = detectedBpm_;
    emit bpmChanged();
    emit targetBpmChanged();
}

void SpeedAdjuster::halfBeat()
{
    if (detectedBpm_ <= 0.0) return;
    detectedBpm_ /= 2.0;
    emit bpmChanged();
    emit targetBpmChanged();
}

void SpeedAdjuster::doubleBeat()
{
    if (detectedBpm_ <= 0.0) return;
    detectedBpm_ *= 2.0;
    emit bpmChanged();
    emit targetBpmChanged();
}

void SpeedAdjuster::addMarker(double timeMs, const QString& label)
{
    QVariantMap marker;
    marker[QStringLiteral("label")] = label;
    marker[QStringLiteral("timeMs")] = timeMs;
    marker[QStringLiteral("bpm")] = targetBpm_;
    markers_.append(marker);
    emit markersChanged();
}

void SpeedAdjuster::removeMarker(int index)
{
    if (index < 0 || index >= markers_.size()) return;
    markers_.removeAt(index);
    emit markersChanged();
}

void SpeedAdjuster::clearMarkers()
{
    if (markers_.isEmpty()) return;
    markers_.clear();
    emit markersChanged();
}

QString SpeedAdjuster::computeOutputPath(const QString& inputPath,
                                         const QString& outputFormat,
                                         const QString& outputDir,
                                         const QString& suffix) const
{
    const QFileInfo info(inputPath);
    const FormatInfo fi = format_info(outputFormat);

    const QString dir = outputDir.isEmpty() ? info.absolutePath() : outputDir;
    const QString baseName = info.completeBaseName();
    const QString ext = fi.extension != nullptr
        ? QString::fromLatin1(fi.extension)
        : info.suffix();

    QString candidate = dir + QStringLiteral("/") + baseName
                        + QStringLiteral("_") + suffix + QStringLiteral(".") + ext;
    int counter = 1;
    while (QFileInfo::exists(candidate)) {
        candidate = dir + QStringLiteral("/") + baseName
                    + QStringLiteral("_") + suffix + QStringLiteral("_")
                    + QString::number(counter) + QStringLiteral(".") + ext;
        ++counter;
    }
    return candidate;
}

double SpeedAdjuster::targetBpmToSpeedRatio(double targetBpm) const noexcept
{
    if (detectedBpm_ <= 0.0) return 1.0;
    return targetBpm / detectedBpm_;
}

void SpeedAdjuster::start(double speedRatio, const QString& outputDir)
{
    if (busy_.load(std::memory_order_acquire)) return;
    if (inputPath_.isEmpty()) {
        emit errorOccurred(QStringLiteral("No input file loaded"));
        return;
    }
    if (speedRatio < 0.5 || speedRatio > 2.0) {
        emit errorOccurred(QStringLiteral("Speed ratio out of range (0.5..2.0)"));
        return;
    }

    setBusy(true);
    setProgress(0.0);

    // Create the cancel token on the main thread so cancel() can flip it.
    ag_cancel_token* token = ag_cancel_token_create();
    token_.store(token, std::memory_order_release);

    const QString outputPath = computeOutputPath(inputPath_, QString(), outputDir, QStringLiteral("speed"));
    const QString inputPath = inputPath_;
    // speed_ratio -> tempo_ratio: speed_ratio=2.0 (2x faster) means
    // tempo_ratio=0.5 (OLA compress to half length, preserving pitch).
    const double tempoRatio = 1.0 / speedRatio;

    auto* watcher = new QFutureWatcher<int>(this);
    connect(watcher, &QFutureWatcher<int>::finished, this,
        [this, watcher, outputPath]() {
            watcher->deleteLater();
            ag_cancel_token* t = token_.exchange(nullptr,
                std::memory_order_acq_rel);
            if (t) ag_cancel_token_destroy(t);
            const int result = watcher->result();
            setBusy(false);
            if (result == AG_OK) {
                setProgress(1.0);
                emit speedAdjustCompleted(outputPath);
            } else if (result == AG_CANCELLED) {
                setProgress(0.0);
                emit errorOccurred(QStringLiteral("Speed adjust cancelled"));
            } else {
                emit errorOccurred(
                    QStringLiteral("Speed adjust failed (error %1)").arg(result));
            }
        });

    auto doAdjust = [this, inputPath, outputPath, tempoRatio, token]() -> int
    {
        const QByteArray inputUtf8 = inputPath.toUtf8();
        const QByteArray outputUtf8 = outputPath.toUtf8();

        auto callback = [](float frac, void* userData) {
            auto* self = static_cast<SpeedAdjuster*>(userData);
            if (self) {
                self->progress_.store(frac, std::memory_order_release);
                emit self->progressChanged();
            }
        };

        const ag_result result = ag_pitch_shift(
            inputUtf8.constData(),
            outputUtf8.constData(),
            0,                  // pitch_cents = 0 (no pitch change)
            1,                  // keep_tempo = true (preserve pitch)
            tempoRatio,         // tempo_ratio = 1/speed_ratio
            token,
            callback,
            this);

        return static_cast<int>(result);
    };

    QFuture<int> future = QtConcurrent::run(doAdjust);
    watcher->setFuture(future);
}

void SpeedAdjuster::startBpmAdjust(double targetBpm,
                                   bool keepPitch,
                                   const QString& outputFormat,
                                   const QString& outputDir)
{
    if (busy_.load(std::memory_order_acquire)) return;
    if (inputPath_.isEmpty()) {
        emit errorOccurred(QStringLiteral("No input file loaded"));
        return;
    }
    if (detectedBpm_ <= 0.0) {
        emit errorOccurred(QStringLiteral("Analyze BPM first"));
        return;
    }

    const double speedRatio = targetBpmToSpeedRatio(targetBpm);
    if (speedRatio < 0.5 || speedRatio > 2.0) {
        emit errorOccurred(QStringLiteral(
            "Target BPM results in speed %1x, out of supported range (0.5..2.0x)")
            .arg(QString::number(speedRatio, 'f', 2)));
        return;
    }

    setTargetBpm(targetBpm);

    setBusy(true);
    setProgress(0.0);

    ag_cancel_token* token = ag_cancel_token_create();
    token_.store(token, std::memory_order_release);

    const QString outputPath = computeOutputPath(inputPath_, outputFormat, outputDir, QStringLiteral("bpm"));
    const QString inputPath = inputPath_;
    const double tempoRatio = 1.0 / speedRatio;
    const QByteArray codecName = [&outputFormat]() -> QByteArray {
        const FormatInfo fi = format_info(outputFormat);
        return fi.codec_name != nullptr ? QByteArray(fi.codec_name) : QByteArray();
    }();
    const bool beatAlign = beatAlign_;

    auto* watcher = new QFutureWatcher<int>(this);
    connect(watcher, &QFutureWatcher<int>::finished, this,
        [this, watcher, outputPath, beatAlign]() {
            watcher->deleteLater();
            ag_cancel_token* t = token_.exchange(nullptr,
                std::memory_order_acq_rel);
            if (t) ag_cancel_token_destroy(t);
            const int result = watcher->result();
            setBusy(false);
            if (result == AG_OK) {
                setProgress(1.0);
                if (beatAlign) {
                    emit warningOccurred(QStringLiteral(
                        "Exported: %1 (beat align not supported)").arg(outputPath));
                } else {
                    emit speedAdjustCompleted(outputPath);
                }
            } else if (result == AG_CANCELLED) {
                setProgress(0.0);
                emit errorOccurred(QStringLiteral("Speed adjust cancelled"));
            } else {
                emit errorOccurred(
                    QStringLiteral("Speed adjust failed (error %1)").arg(result));
            }
        });

    auto doAdjust = [this, inputPath, outputPath, tempoRatio, keepPitch, codecName,
                     token]() -> int
    {
        const QByteArray inputUtf8 = inputPath.toUtf8();
        const QByteArray outputUtf8 = outputPath.toUtf8();

        auto callback = [](float frac, void* userData) {
            auto* self = static_cast<SpeedAdjuster*>(userData);
            if (self) {
                self->progress_.store(frac, std::memory_order_release);
                emit self->progressChanged();
            }
        };

        ag_pitch_shift_options options{};
        options.vocal_protection = 0;
        options.smooth_transition = 0;
        options.output_sample_rate = 0;

        const ag_result result = ag_pitch_shift_ex(
            inputUtf8.constData(),
            outputUtf8.constData(),
            0,                          // pitch_cents = 0
            keepPitch ? 1 : 0,          // keep_tempo maps to keep pitch in UI terms
            tempoRatio,
            codecName.isEmpty() ? nullptr : codecName.constData(),
            &options,
            token,
            callback,
            this);

        return static_cast<int>(result);
    };

    QFuture<int> future = QtConcurrent::run(doAdjust);
    watcher->setFuture(future);
}

void SpeedAdjuster::cancel()
{
    ag_cancel_token* t = token_.load(std::memory_order_acquire);
    if (t) {
        ag_cancel_token_cancel(t);
    }
}

void SpeedAdjuster::clear()
{
    if (busy_.load(std::memory_order_acquire)) return;
    inputPath_.clear();
    inputFileName_.clear();
    inputDurationMs_ = 0;
    waveformPeaks_.clear();
    detectedBpm_ = 0.0;
    bpmConfidence_ = 0.0;
    targetBpm_ = 120.0;
    keepPitch_ = true;
    beatAlign_ = false;
    markers_.clear();
    setProgress(0.0);
    emit waveformPeaksChanged();
    emit bpmChanged();
    emit targetBpmChanged();
    emit keepPitchChanged();
    emit beatAlignChanged();
    emit markersChanged();
    emit inputFileChanged();
}
