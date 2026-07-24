#include "light_editor_controller.hpp"

#include <agplayer/c_api.h>

#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent>

LightEditor::LightEditor(QObject* parent)
    : QObject(parent)
    , tracks_(kTrackCount)
{
}

LightEditor::~LightEditor()
{
    cancel();
    if (watcher_ != nullptr) {
        watcher_->waitForFinished();
    }
    QMutexLocker lock(&tokenMutex_);
    ag_cancel_token* t = token_.exchange(nullptr, std::memory_order_acq_rel);
    lock.unlock();
    if (t != nullptr) {
        ag_cancel_token_destroy(t);
    }
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
    return currentTrack().name;
}

bool LightEditor::hasInput() const noexcept
{
    return !currentTrack().path.isEmpty();
}

qint64 LightEditor::durationMs() const noexcept
{
    return currentTrack().durationMs;
}

QString LightEditor::inputFormat() const noexcept
{
    return currentTrack().format;
}

int LightEditor::inputSampleRate() const noexcept
{
    return currentTrack().sampleRate;
}

int LightEditor::inputChannels() const noexcept
{
    return currentTrack().channels;
}

QVariantList LightEditor::waveformPeaks() const noexcept
{
    return currentTrack().peaks;
}

int LightEditor::trackCount() const noexcept
{
    return kTrackCount;
}

int LightEditor::selectedTrack() const noexcept
{
    return selectedTrack_;
}

void LightEditor::setSelectedTrack(int value)
{
    if (value == selectedTrack_ || value < 0 || value >= kTrackCount) {
        return;
    }
    selectedTrack_ = value;
    emit selectedTrackChanged();
    emit inputFileChanged();
    emit waveformPeaksChanged();
}

QVariantList LightEditor::trackNames() const noexcept
{
    QVariantList list;
    list.reserve(kTrackCount);
    for (const auto& track : tracks_) {
        list.append(track.name);
    }
    return list;
}

QVariantList LightEditor::trackHasFiles() const noexcept
{
    QVariantList list;
    list.reserve(kTrackCount);
    for (const auto& track : tracks_) {
        list.append(!track.path.isEmpty());
    }
    return list;
}

QVariantList LightEditor::trackPeaks() const noexcept
{
    QVariantList list;
    list.reserve(kTrackCount);
    for (const auto& track : tracks_) {
        list.append(track.peaks);
    }
    return list;
}

const LightEditor::Track& LightEditor::currentTrack() const
{
    return tracks_[selectedTrack_];
}

LightEditor::Track& LightEditor::currentTrack()
{
    return tracks_[selectedTrack_];
}

bool LightEditor::isValidTrackIndex(int index) const noexcept
{
    return index >= 0 && index < kTrackCount;
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

void LightEditor::loadPathIntoTrack(const QString& path, int trackIndex)
{
    if (!isValidTrackIndex(trackIndex)) {
        return;
    }

    Track& track = tracks_[trackIndex];
    track.path = path;
    track.name = QFileInfo(path).fileName();
    track.format.clear();
    track.durationMs = 0;
    track.sampleRate = 0;
    track.channels = 0;
    track.peaks.clear();

    if (path.isEmpty()) {
        return;
    }

    const QByteArray utf8Path = path.toUtf8();
    ag_metadata* meta = nullptr;
    if (ag_metadata_open(utf8Path.constData(), &meta) == AG_OK && meta != nullptr) {
        const char* fmt = ag_metadata_format(meta);
        track.format = fmt != nullptr ? QString::fromUtf8(fmt) : QString();
        track.sampleRate = ag_metadata_sample_rate(meta);
        track.channels = ag_metadata_channels(meta);
        track.durationMs = ag_metadata_duration_ms(meta);
        ag_metadata_destroy(meta);
    }

    ag_waveform* waveform = nullptr;
    if (ag_waveform_analyze(utf8Path.constData(), 256, nullptr, nullptr,
                            nullptr, &waveform) == AG_OK
        && waveform != nullptr) {
        const size_t count = ag_waveform_count(waveform);
        track.peaks.reserve(static_cast<int>(count));
        for (size_t i = 0; i < count; ++i) {
            track.peaks.append(static_cast<double>(ag_waveform_peak(waveform, i)));
        }
        ag_waveform_destroy(waveform);
    }
}

void LightEditor::loadFile(const QUrl& url)
{
    if (busy_.load(std::memory_order_acquire)) {
        return;
    }
    loadPathIntoTrack(url.toLocalFile(), selectedTrack_);
    emit tracksChanged();
    emit inputFileChanged();
    emit waveformPeaksChanged();
}

void LightEditor::loadFileToTrack(int trackIndex, const QUrl& url)
{
    if (busy_.load(std::memory_order_acquire)) {
        return;
    }
    if (!isValidTrackIndex(trackIndex)) {
        emit errorOccurred(QStringLiteral("Invalid track index"));
        return;
    }
    loadPathIntoTrack(url.toLocalFile(), trackIndex);
    emit tracksChanged();
    if (trackIndex == selectedTrack_) {
        emit inputFileChanged();
        emit waveformPeaksChanged();
    }
}

QString LightEditor::computeOutputPath(const QString& firstInputPath,
                                       const QString& outputDir,
                                       const QString& outputFormat,
                                       int loadedCount) const
{
    const QFileInfo info(firstInputPath);
    const QString dir = outputDir.isEmpty() ? info.absolutePath() : outputDir;
    const QString baseName = info.completeBaseName();

    QString ext;
    if (outputFormat.isEmpty() || outputFormat == QStringLiteral("source")) {
        ext = info.suffix();
        if (ext.isEmpty()) {
            ext = QStringLiteral("wav");
        }
    } else {
        ext = outputFormat;
    }

    const QString suffix = loadedCount > 1
                               ? QStringLiteral("_mix")
                               : QStringLiteral("_edit");

    QString candidate = dir + QStringLiteral("/") + baseName + suffix
                        + QStringLiteral(".") + ext;
    int counter = 1;
    while (QFileInfo::exists(candidate)) {
        candidate = dir + QStringLiteral("/") + baseName + suffix
                    + QStringLiteral("_") + QString::number(counter)
                    + QStringLiteral(".") + ext;
        ++counter;
    }
    return candidate;
}

void LightEditor::start(qint64 trimStartMs, qint64 trimEndMs,
                        int fadeInMs, int fadeOutMs, double gain,
                        const QString& outputDir,
                        const QString& outputFormat,
                        int outputSampleRate,
                        int outputChannels)
{
    if (busy_.load(std::memory_order_acquire)) {
        return;
    }

    std::vector<int> loadedIndices;
    loadedIndices.reserve(kTrackCount);
    for (int i = 0; i < kTrackCount; ++i) {
        if (!tracks_[i].path.isEmpty()) {
            loadedIndices.push_back(i);
        }
    }

    if (loadedIndices.empty()) {
        emit errorOccurred(QStringLiteral("No input file loaded"));
        return;
    }

    // ag_multitrack_edit determines the output container from the file extension
    // and does not currently expose sample-rate or channel conversion. These
    // parameters are accepted for forward compatibility with the export UI.
    Q_UNUSED(outputSampleRate)
    Q_UNUSED(outputChannels)

    setBusy(true);
    setProgress(0.0);

    ag_cancel_token* token = ag_cancel_token_create();
    token_.store(token, std::memory_order_release);

    const QString firstPath = tracks_[loadedIndices.front()].path;
    const int loadedCount = static_cast<int>(loadedIndices.size());
    const QString outputPath = computeOutputPath(firstPath, outputDir,
                                                 outputFormat, loadedCount);

    std::vector<QByteArray> pathBytes;
    pathBytes.reserve(loadedCount);
    for (int index : loadedIndices) {
        pathBytes.push_back(tracks_[index].path.toUtf8());
    }

    std::vector<long long> trimStarts(loadedCount, trimStartMs);
    std::vector<long long> trimEnds(loadedCount, trimEndMs);
    std::vector<int> fadeIns(loadedCount, fadeInMs);
    std::vector<int> fadeOuts(loadedCount, fadeOutMs);
    std::vector<double> gains(loadedCount, gain);

    auto* watcher = new QFutureWatcher<int>(this);
    watcher_ = watcher;
    connect(watcher, &QFutureWatcher<int>::finished, this,
        [this, watcher, outputPath]() {
            watcher_->deleteLater();
            ag_cancel_token* t = nullptr;
            {
                QMutexLocker lock(&tokenMutex_);
                t = token_.exchange(nullptr, std::memory_order_acq_rel);
            }
            if (t != nullptr) {
                ag_cancel_token_destroy(t);
            }
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

    auto doEdit = [pathBytes, trimStarts, trimEnds, fadeIns, fadeOuts, gains,
                   outputPath, token, this]() -> int
    {
        std::vector<const char*> inputPaths;
        inputPaths.reserve(pathBytes.size());
        for (const auto& bytes : pathBytes) {
            inputPaths.push_back(bytes.constData());
        }

        const QByteArray outputUtf8 = outputPath.toUtf8();

        auto callback = [](float frac, void* userData) {
            auto* self = static_cast<LightEditor*>(userData);
            if (self) {
                self->progress_.store(frac, std::memory_order_release);
                emit self->progressChanged();
            }
        };

        const ag_result result = ag_multitrack_edit(
            inputPaths.size(),
            inputPaths.data(),
            trimStarts.data(),
            trimEnds.data(),
            fadeIns.data(),
            fadeOuts.data(),
            gains.data(),
            outputUtf8.constData(),
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
    QMutexLocker lock(&tokenMutex_);
    ag_cancel_token* t = token_.load(std::memory_order_acquire);
    if (t != nullptr) {
        ag_cancel_token_cancel(t);
    }
}

void LightEditor::clear()
{
    if (busy_.load(std::memory_order_acquire)) {
        return;
    }
    for (auto& track : tracks_) {
        track = Track();
    }
    selectedTrack_ = 0;
    setProgress(0.0);
    emit tracksChanged();
    emit selectedTrackChanged();
    emit inputFileChanged();
    emit waveformPeaksChanged();
}

void LightEditor::clearTrack(int trackIndex)
{
    if (busy_.load(std::memory_order_acquire)) {
        return;
    }
    if (!isValidTrackIndex(trackIndex)) {
        emit errorOccurred(QStringLiteral("Invalid track index"));
        return;
    }
    tracks_[trackIndex] = Track();
    emit tracksChanged();
    if (trackIndex == selectedTrack_) {
        emit inputFileChanged();
        emit waveformPeaksChanged();
    }
}
