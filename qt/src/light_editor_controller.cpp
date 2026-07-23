#include "light_editor_controller.hpp"

#include <agplayer/c_api.h>

#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent>

LightEditor::LightEditor(QObject* parent)
    : QObject(parent)
{
}

LightEditor::~LightEditor()
{
    token_.store(nullptr, std::memory_order_release);
}

double LightEditor::progress() const noexcept
{
    return progress_.load(std::memory_order_acquire);
}

bool LightEditor::busy() const noexcept
{
    return busy_.load(std::memory_order_acquire);
}

QString LightEditor::inputFileName() const noexcept
{
    return inputFileName_;
}

bool LightEditor::hasInput() const noexcept
{
    return !inputPath_.isEmpty();
}

qint64 LightEditor::durationMs() const noexcept
{
    return durationMs_;
}

QString LightEditor::inputFormat() const noexcept
{
    return inputFormat_;
}

int LightEditor::inputSampleRate() const noexcept
{
    return inputSampleRate_;
}

int LightEditor::inputChannels() const noexcept
{
    return inputChannels_;
}

QVariantList LightEditor::waveformPeaks() const noexcept
{
    return waveformPeaks_;
}

void LightEditor::setBusy(bool value)
{
    busy_.store(value, std::memory_order_release);
    emit busyChanged();
}

void LightEditor::setProgress(double value)
{
    progress_.store(value, std::memory_order_release);
    emit progressChanged();
}

void LightEditor::loadFile(const QUrl& url)
{
    if (busy_.load(std::memory_order_acquire)) return;

    inputPath_ = url.toLocalFile();
    inputFileName_ = QFileInfo(inputPath_).fileName();

    // Read metadata and waveform peaks so the multi-track editor can display
    // the active audio track and bound the trim region correctly.
    durationMs_ = 0;
    inputFormat_.clear();
    inputSampleRate_ = 0;
    inputChannels_ = 0;
    waveformPeaks_.clear();

    const QByteArray utf8Path = inputPath_.toUtf8();
    ag_metadata* meta = nullptr;
    if (ag_metadata_open(utf8Path.constData(), &meta) == AG_OK && meta != nullptr) {
        const char* fmt = ag_metadata_format(meta);
        inputFormat_ = fmt != nullptr ? QString::fromUtf8(fmt) : QString();
        inputSampleRate_ = ag_metadata_sample_rate(meta);
        inputChannels_ = ag_metadata_channels(meta);
        durationMs_ = ag_metadata_duration_ms(meta);
        ag_metadata_destroy(meta);
    }

    ag_waveform* waveform = nullptr;
    if (ag_waveform_analyze(utf8Path.constData(), 256, nullptr, nullptr,
                            nullptr, &waveform) == AG_OK
        && waveform != nullptr) {
        const size_t count = ag_waveform_count(waveform);
        waveformPeaks_.reserve(static_cast<int>(count));
        for (size_t i = 0; i < count; ++i) {
            waveformPeaks_.append(static_cast<double>(ag_waveform_peak(waveform, i)));
        }
        ag_waveform_destroy(waveform);
    }
    emit waveformPeaksChanged();

    emit inputFileChanged();
}

QString LightEditor::computeOutputPath(const QString& inputPath,
                                       const QString& outputDir) const
{
    const QFileInfo info(inputPath);
    const QString dir = outputDir.isEmpty() ? info.absolutePath() : outputDir;
    const QString baseName = info.completeBaseName();
    const QString ext = info.suffix();

    QString candidate = dir + QStringLiteral("/") + baseName
                        + QStringLiteral("_edit.") + ext;
    int counter = 1;
    while (QFileInfo::exists(candidate)) {
        candidate = dir + QStringLiteral("/") + baseName
                    + QStringLiteral("_edit_") + QString::number(counter)
                    + QStringLiteral(".") + ext;
        ++counter;
    }
    return candidate;
}

void LightEditor::start(qint64 trimStartMs, qint64 trimEndMs,
                        int fadeInMs, int fadeOutMs, double gain,
                        const QString& outputDir)
{
    if (busy_.load(std::memory_order_acquire)) return;
    if (inputPath_.isEmpty()) {
        emit errorOccurred(QStringLiteral("No input file loaded"));
        return;
    }

    setBusy(true);
    setProgress(0.0);

    ag_cancel_token* token = ag_cancel_token_create();
    token_.store(token, std::memory_order_release);

    const QString outputPath = computeOutputPath(inputPath_, outputDir);
    const QString inputPath = inputPath_;

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
                emit lightEditCompleted(outputPath);
            } else if (result == AG_CANCELLED) {
                setProgress(0.0);
                emit errorOccurred(QStringLiteral("Light edit cancelled"));
            } else {
                emit errorOccurred(
                    QStringLiteral("Light edit failed (error %1)").arg(result));
            }
        });

    auto doEdit = [this, inputPath, outputPath, trimStartMs, trimEndMs,
                   fadeInMs, fadeOutMs, gain, token]() -> int
    {
        const QByteArray inputUtf8 = inputPath.toUtf8();
        const QByteArray outputUtf8 = outputPath.toUtf8();

        auto callback = [](float frac, void* userData) {
            auto* self = static_cast<LightEditor*>(userData);
            if (self) {
                self->progress_.store(frac, std::memory_order_release);
                emit self->progressChanged();
            }
        };

        const ag_result result = ag_light_edit(
            inputUtf8.constData(),
            outputUtf8.constData(),
            trimStartMs,
            trimEndMs,
            fadeInMs,
            fadeOutMs,
            gain,
            token,
            callback,
            this);

        return static_cast<int>(result);
    };

    QFuture<int> future = QtConcurrent::run(doEdit);
    watcher->setFuture(future);
}

void LightEditor::cancel()
{
    ag_cancel_token* t = token_.load(std::memory_order_acquire);
    if (t) {
        ag_cancel_token_cancel(t);
    }
}

void LightEditor::clear()
{
    if (busy_.load(std::memory_order_acquire)) return;
    inputPath_.clear();
    inputFileName_.clear();
    inputFormat_.clear();
    durationMs_ = 0;
    inputSampleRate_ = 0;
    inputChannels_ = 0;
    waveformPeaks_.clear();
    setProgress(0.0);
    emit waveformPeaksChanged();
    emit inputFileChanged();
}
