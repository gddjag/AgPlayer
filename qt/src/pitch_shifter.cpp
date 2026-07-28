#include "pitch_shifter.hpp"

#include <agplayer/c_api.h>

#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent>

namespace {

// Map format string to FFmpeg codec name + file extension.
// Mirrors FormatConverter::format_info so the pitch-shifter UI can reuse the
// same output format model.
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

PitchShifter::PitchShifter(QObject* parent)
    : QObject(parent)
{
}

PitchShifter::~PitchShifter()
{
    cancel();
    if (watcher_ != nullptr) {
        watcher_->future().waitForFinished();
    }
    ag_cancel_token* token =
        token_.exchange(nullptr, std::memory_order_acq_rel);
    if (token != nullptr) {
        ag_cancel_token_destroy(token);
    }
}

double PitchShifter::progress() const noexcept
{
    return progress_.load(std::memory_order_acquire);
}

bool PitchShifter::busy() const noexcept
{
    return busy_.load(std::memory_order_acquire);
}

QString PitchShifter::inputFileName() const noexcept
{
    return inputFileName_;
}

QUrl PitchShifter::inputUrl() const
{
    return QUrl::fromLocalFile(inputPath_);
}

bool PitchShifter::hasInput() const noexcept
{
    return !inputPath_.isEmpty();
}

QString PitchShifter::inputFormat() const noexcept
{
    return inputFormat_;
}

int PitchShifter::inputSampleRate() const noexcept
{
    return inputSampleRate_;
}

int PitchShifter::inputDurationMs() const noexcept
{
    return inputDurationMs_;
}

QVariantList PitchShifter::waveformPeaks() const noexcept
{
    return waveformPeaks_;
}

void PitchShifter::setBusy(bool value)
{
    busy_.store(value, std::memory_order_release);
    emit busyChanged();
}

void PitchShifter::setProgress(double value)
{
    progress_.store(value, std::memory_order_release);
    emit progressChanged();
}

void PitchShifter::loadFile(const QUrl& url)
{
    if (busy_.load(std::memory_order_acquire)) return;

    inputPath_ = url.toLocalFile();
    inputFileName_ = QFileInfo(inputPath_).fileName();

    const QByteArray inputUtf8 = inputPath_.toUtf8();

    ag_metadata* metadata = nullptr;
    if (ag_metadata_open(inputUtf8.constData(), &metadata) == AG_OK
        && metadata != nullptr) {
        const char* fmt = ag_metadata_format(metadata);
        inputFormat_ = fmt != nullptr ? QString::fromUtf8(fmt) : QString();
        inputSampleRate_ = ag_metadata_sample_rate(metadata);
        inputDurationMs_ = static_cast<int>(ag_metadata_duration_ms(metadata));
        ag_metadata_destroy(metadata);
    } else {
        inputFormat_.clear();
        inputSampleRate_ = 0;
        inputDurationMs_ = 0;
    }

    QVariantList peaks;
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
    waveformPeaks_ = std::move(peaks);
    emit waveformPeaksChanged();

    emit inputFileChanged();
}

QString PitchShifter::computeOutputPath(const QString& inputPath,
                                        const QString& outputFormat,
                                        const QString& outputDir) const
{
    const QFileInfo info(inputPath);
    const FormatInfo fi = format_info(outputFormat);

    const QString dir = outputDir.isEmpty() ? info.absolutePath() : outputDir;
    const QString baseName = info.completeBaseName();
    const QString ext = fi.extension != nullptr
        ? QString::fromLatin1(fi.extension)
        : info.suffix();

    QString candidate = dir + QStringLiteral("/") + baseName
                        + QStringLiteral("_pitched.") + ext;
    int counter = 1;
    while (QFileInfo::exists(candidate)) {
        candidate = dir + QStringLiteral("/") + baseName
                    + QStringLiteral("_pitched_") + QString::number(counter)
                    + QStringLiteral(".") + ext;
        ++counter;
    }
    return candidate;
}

void PitchShifter::start(int pitchCents,
                         bool keepTempo,
                         double tempoRatio,
                         const QString& outputFormat,
                         int outputSampleRate,
                         bool vocalProtection,
                         bool smoothTransition,
                         const QString& outputDir)
{
    if (busy_.load(std::memory_order_acquire)) return;
    if (inputPath_.isEmpty()) {
        emit errorOccurred(QStringLiteral("No input file loaded"));
        return;
    }

    setBusy(true);
    setProgress(0.0);

    const QString outputPath = computeOutputPath(inputPath_, outputFormat, outputDir);
    const QString inputPath = inputPath_;
    const FormatInfo fi = format_info(outputFormat);
    const QByteArray codecName = fi.codec_name != nullptr
        ? QByteArray(fi.codec_name)
        : QByteArray();

    ag_cancel_token* token = ag_cancel_token_create();
    token_.store(token, std::memory_order_release);

    auto* watcher = new QFutureWatcher<int>(this);
    watcher_ = watcher;
    connect(watcher, &QFutureWatcher<int>::finished, this,
        [this, watcher, outputPath]() {
            watcher->deleteLater();
            watcher_.clear();
            ag_cancel_token* token =
                token_.exchange(nullptr, std::memory_order_acq_rel);
            if (token != nullptr) {
                ag_cancel_token_destroy(token);
            }
            const int result = watcher->result();
            setBusy(false);
            if (result == AG_OK) {
                setProgress(1.0);
                emit pitchShiftCompleted(outputPath);
            } else if (result == AG_CANCELLED) {
                setProgress(0.0);
                emit errorOccurred(QStringLiteral("Pitch shift cancelled"));
            } else {
                emit errorOccurred(
                    QStringLiteral("Pitch shift failed (error %1)").arg(result));
            }
        });

    auto doShift = [this, inputPath, outputPath, pitchCents, keepTempo, tempoRatio,
                    codecName, outputSampleRate, vocalProtection,
                    smoothTransition, token]()
        -> int
    {
        const QByteArray inputUtf8 = inputPath.toUtf8();
        const QByteArray outputUtf8 = outputPath.toUtf8();

        auto callback = [](float frac, void* userData) {
            auto* self = static_cast<PitchShifter*>(userData);
            if (self) {
                self->progress_.store(frac, std::memory_order_release);
                emit self->progressChanged();
            }
        };

        ag_pitch_shift_options options{};
        options.vocal_protection = vocalProtection ? 1 : 0;
        options.smooth_transition = smoothTransition ? 1 : 0;
        options.output_sample_rate = outputSampleRate;

        const ag_result result = ag_pitch_shift_ex(
            inputUtf8.constData(),
            outputUtf8.constData(),
            pitchCents,
            keepTempo ? 1 : 0,
            tempoRatio,
            codecName.isEmpty() ? nullptr : codecName.constData(),
            &options,
            token,
            callback,
            this);

        return static_cast<int>(result);
    };

    QFuture<int> future = QtConcurrent::run(doShift);
    watcher->setFuture(future);
}

void PitchShifter::cancel()
{
    ag_cancel_token* token = token_.load(std::memory_order_acquire);
    if (token != nullptr) {
        ag_cancel_token_cancel(token);
    }
}

void PitchShifter::clear()
{
    if (busy_.load(std::memory_order_acquire)) return;
    inputPath_.clear();
    inputFileName_.clear();
    inputFormat_.clear();
    inputSampleRate_ = 0;
    inputDurationMs_ = 0;
    waveformPeaks_.clear();
    setProgress(0.0);
    emit waveformPeaksChanged();
    emit inputFileChanged();
}
