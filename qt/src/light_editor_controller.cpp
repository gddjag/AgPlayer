#include "light_editor_controller.hpp"

#include "editor_timeline_math.hpp"

#include <agplayer/c_api.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QStandardPaths>
#include <QUuid>
#include <QVariantMap>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <memory>

namespace {

constexpr double kReliableBpmConfidence = 50.0;

QByteArray codecForFormat(const QString& format)
{
    const QString normalized = format.toLower();
    if (normalized == QStringLiteral("mp3")) {
        return QByteArrayLiteral("libmp3lame");
    }
    if (normalized == QStringLiteral("wav")) {
        return QByteArrayLiteral("pcm_s16le");
    }
    if (normalized == QStringLiteral("flac")) {
        return QByteArrayLiteral("flac");
    }
    return {};
}

struct AnalyzeResult {
    int status = AG_INTERNAL_ERROR;
    ag_bpm_result bpm{};
};

struct BpmJob {
    int trackIndex = -1;
    QString inputPath;
    QString outputPath;
    double originalBpm = 0.0;
    double confidence = 0.0;
};

struct BpmJobResult {
    int trackIndex = -1;
    QString inputPath;
    QString outputPath;
    double originalBpm = 0.0;
    double confidence = 0.0;
    double speedRatio = 1.0;
};

struct BpmBatchResult {
    int status = AG_INTERNAL_ERROR;
    int failureIndex = -1;
    QString message;
    std::vector<BpmJobResult> tracks;
};

} // namespace

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

QVariantList LightEditor::tracks() const noexcept
{
    QVariantList list;
    list.reserve(kTrackCount);
    for (const auto& track : tracks_) {
        QVariantMap value;
        value.insert(QStringLiteral("path"), track.path);
        value.insert(QStringLiteral("renderPath"), track.renderPath);
        value.insert(QStringLiteral("name"), track.name);
        value.insert(QStringLiteral("format"), track.format);
        value.insert(QStringLiteral("durationMs"), track.durationMs);
        value.insert(QStringLiteral("timelineStartMs"), track.timelineStartMs);
        value.insert(QStringLiteral("inMs"), track.inMs);
        value.insert(QStringLiteral("outMs"), track.outMs);
        value.insert(QStringLiteral("fadeInMs"), track.fadeInMs);
        value.insert(QStringLiteral("fadeOutMs"), track.fadeOutMs);
        value.insert(QStringLiteral("gain"), track.gain);
        value.insert(QStringLiteral("originalBpm"), track.originalBpm);
        value.insert(QStringLiteral("bpmConfidence"), track.bpmConfidence);
        value.insert(QStringLiteral("speedRatio"), track.speedRatio);
        value.insert(QStringLiteral("muted"), track.muted);
        value.insert(QStringLiteral("solo"), track.solo);
        value.insert(QStringLiteral("locked"), track.locked);
        value.insert(QStringLiteral("aligned"), track.aligned);
        value.insert(QStringLiteral("sampleRate"), track.sampleRate);
        value.insert(QStringLiteral("channels"), track.channels);
        value.insert(QStringLiteral("peaks"), track.peaks);
        value.insert(QStringLiteral("hasFile"), !track.path.isEmpty());
        list.append(value);
    }
    return list;
}

double LightEditor::targetBpm() const noexcept
{
    return targetBpm_;
}

void LightEditor::setTargetBpm(double value)
{
    if (value < 40.0 || value > 240.0 || qFuzzyCompare(value, targetBpm_)) {
        return;
    }
    targetBpm_ = value;
    emit targetBpmChanged();
}

bool LightEditor::snapEnabled() const noexcept
{
    return snapEnabled_;
}

void LightEditor::setSnapEnabled(bool value)
{
    if (value == snapEnabled_) {
        return;
    }
    snapEnabled_ = value;
    emit snapEnabledChanged();
}

bool LightEditor::keepPitch() const noexcept
{
    return keepPitch_;
}

void LightEditor::setKeepPitch(bool value)
{
    if (value == keepPitch_) {
        return;
    }
    keepPitch_ = value;
    emit keepPitchChanged();
}

bool LightEditor::canUndo() const noexcept
{
    return !undoStack_.empty();
}

bool LightEditor::canRedo() const noexcept
{
    return !redoStack_.empty();
}

bool LightEditor::hasClipboard() const noexcept
{
    return clipboard_.has_value();
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

void LightEditor::pushUndoState()
{
    if (static_cast<int>(undoStack_.size()) >= kMaximumUndoStates) {
        undoStack_.erase(undoStack_.begin());
    }
    undoStack_.push_back(EditorState{tracks_, selectedTrack_});
    redoStack_.clear();
    emit undoStateChanged();
}

void LightEditor::restoreState(EditorState state)
{
    tracks_ = std::move(state.tracks);
    selectedTrack_ = state.selectedTrack;
    emitEditorStateChanged();
}

void LightEditor::emitEditorStateChanged()
{
    emit tracksChanged();
    emit selectedTrackChanged();
    emit inputFileChanged();
    emit waveformPeaksChanged();
    emit undoStateChanged();
}

void LightEditor::loadPathIntoTrack(const QString& path, int trackIndex)
{
    if (!isValidTrackIndex(trackIndex)) {
        return;
    }

    Track& track = tracks_[trackIndex];
    track.path = path;
    track.renderPath.clear();
    track.name = QFileInfo(path).fileName();
    track.format.clear();
    track.durationMs = 0;
    track.timelineStartMs = 0;
    track.inMs = 0;
    track.outMs = 0;
    track.fadeInMs = 0;
    track.fadeOutMs = 0;
    track.gain = 1.0;
    track.originalBpm = 0.0;
    track.bpmConfidence = 0.0;
    track.speedRatio = 1.0;
    track.muted = false;
    track.solo = false;
    track.locked = false;
    track.aligned = false;
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
        track.outMs = track.durationMs;
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
    pushUndoState();
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
    pushUndoState();
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
    while (!overwriteExisting_ && QFileInfo::exists(candidate)) {
        candidate = dir + QStringLiteral("/") + baseName + suffix
                    + QStringLiteral("_") + QString::number(counter)
                    + QStringLiteral(".") + ext;
        ++counter;
    }
    return candidate;
}

void LightEditor::analyzeTrackBpm(int trackIndex)
{
    if (busy_.load(std::memory_order_acquire)) {
        emit trackError(trackIndex, tr("Editor is busy"));
        return;
    }
    if (!isValidTrackIndex(trackIndex) || tracks_[trackIndex].path.isEmpty()) {
        emit trackError(trackIndex, tr("No input file loaded"));
        return;
    }

    const QString inputPath = tracks_[trackIndex].path;
    auto result = std::make_shared<AnalyzeResult>();
    setBusy(true);
    setProgress(0.0);

    auto* watcher = new QFutureWatcher<int>(this);
    watcher_ = watcher;
    connect(watcher, &QFutureWatcher<int>::finished, this,
        [this, watcher, result, trackIndex, inputPath]() {
            watcher->deleteLater();
            watcher_.clear();
            setBusy(false);
            if (result->status != AG_OK) {
                setProgress(0.0);
                const QString message = tr("BPM analysis failed");
                emit trackError(trackIndex, message);
                emit errorOccurred(message);
                return;
            }

            Track& track = tracks_[trackIndex];
            if (track.path != inputPath) {
                return;
            }
            track.originalBpm = result->bpm.bpm;
            track.bpmConfidence = result->bpm.confidence;
            track.aligned = false;
            setProgress(1.0);
            emit tracksChanged();
        });

    watcher->setFuture(QtConcurrent::run([result, inputPath]() {
        const QByteArray path = inputPath.toUtf8();
        result->status = ag_bpm_analyze(path.constData(), &result->bpm);
        return result->status;
    }));
}

void LightEditor::unifyBpm(bool alignBeats)
{
    if (busy_.load(std::memory_order_acquire)) {
        return;
    }

    QString cacheRoot =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (cacheRoot.isEmpty()) {
        cacheRoot = QDir::tempPath();
    }
    cacheRoot = QDir(cacheRoot).filePath(QStringLiteral("light-editor"));
    if (!QDir().mkpath(cacheRoot)) {
        emit errorOccurred(tr("Failed to create BPM cache directory"));
        return;
    }

    std::vector<BpmJob> jobs;
    for (int index = 0; index < kTrackCount; ++index) {
        const Track& track = tracks_[index];
        if (track.path.isEmpty()) {
            continue;
        }
        BpmJob job;
        job.trackIndex = index;
        job.inputPath = track.path;
        job.outputPath = QDir(cacheRoot).filePath(
            QUuid::createUuid().toString(QUuid::WithoutBraces)
            + QStringLiteral(".wav"));
        job.originalBpm = track.originalBpm;
        job.confidence = track.bpmConfidence;
        jobs.push_back(std::move(job));
    }

    if (jobs.empty()) {
        emit errorOccurred(tr("No input file loaded"));
        return;
    }

    const double targetBpm = targetBpm_;
    const bool keepPitch = keepPitch_;
    auto result = std::make_shared<BpmBatchResult>();
    ag_cancel_token* token = ag_cancel_token_create();
    token_.store(token, std::memory_order_release);
    setBusy(true);
    setProgress(0.0);

    auto* watcher = new QFutureWatcher<int>(this);
    watcher_ = watcher;
    connect(watcher, &QFutureWatcher<int>::finished, this,
        [this, watcher, result, alignBeats]() {
            watcher->deleteLater();
            watcher_.clear();
            ag_cancel_token* t = nullptr;
            {
                QMutexLocker lock(&tokenMutex_);
                t = token_.exchange(nullptr, std::memory_order_acq_rel);
            }
            if (t != nullptr) {
                ag_cancel_token_destroy(t);
            }
            setBusy(false);

            if (result->status != AG_OK) {
                setProgress(0.0);
                const QString message = result->message.isEmpty()
                    ? tr("BPM alignment failed")
                    : result->message;
                if (result->failureIndex >= 0) {
                    emit trackError(result->failureIndex, message);
                }
                emit errorOccurred(message);
                return;
            }

            pushUndoState();
            for (const BpmJobResult& item : result->tracks) {
                Track& track = tracks_[item.trackIndex];
                if (track.path != item.inputPath) {
                    continue;
                }
                track.originalBpm = item.originalBpm;
                track.bpmConfidence = item.confidence;
                track.speedRatio = item.speedRatio;
                track.renderPath = item.outputPath;
                track.inMs = qRound64(track.inMs / item.speedRatio);
                track.outMs = qRound64(track.outMs / item.speedRatio);
                track.durationMs = qRound64(
                    track.durationMs / item.speedRatio);
                if (alignBeats) {
                    track.timelineStartMs =
                        snapMs(track.timelineStartMs, targetBpm_, 4);
                }
                track.aligned = true;
            }
            setProgress(1.0);
            emit tracksChanged();
            emit inputFileChanged();
        });

    watcher->setFuture(QtConcurrent::run(
        [this, jobs, result, targetBpm, keepPitch, token]() {
            std::vector<QString> createdFiles;
            result->tracks.reserve(jobs.size());
            for (size_t index = 0; index < jobs.size(); ++index) {
                if (token == nullptr) {
                    result->status = AG_INTERNAL_ERROR;
                    break;
                }

                BpmJob job = jobs[index];
                if (job.originalBpm < 40.0 || job.originalBpm > 240.0
                    || job.confidence < kReliableBpmConfidence) {
                    ag_bpm_result bpm{};
                    const QByteArray inputPath = job.inputPath.toUtf8();
                    const ag_result analysis =
                        ag_bpm_analyze(inputPath.constData(), &bpm);
                    if (analysis != AG_OK
                        || bpm.confidence < kReliableBpmConfidence) {
                        result->status = analysis == AG_OK
                            ? AG_DECODE_ERROR : analysis;
                        result->failureIndex = job.trackIndex;
                        result->message = tr("Reliable BPM was not detected");
                        break;
                    }
                    job.originalBpm = bpm.bpm;
                    job.confidence = bpm.confidence;
                }

                const double speedRatio = targetBpm / job.originalBpm;
                if (speedRatio < 0.5 || speedRatio > 2.0) {
                    result->status = AG_INVALID_ARGUMENT;
                    result->failureIndex = job.trackIndex;
                    result->message =
                        tr("Target BPM is outside the supported speed range");
                    break;
                }

                const QByteArray inputPath = job.inputPath.toUtf8();
                const QByteArray outputPath = job.outputPath.toUtf8();
                ag_pitch_shift_options options{};
                const ag_result shift = ag_pitch_shift_ex(
                    inputPath.constData(), outputPath.constData(), 0,
                    keepPitch ? 1 : 0, 1.0 / speedRatio, "pcm_s16le",
                    &options, token, nullptr, nullptr);
                if (shift != AG_OK) {
                    result->status = shift;
                    result->failureIndex = job.trackIndex;
                    result->message = shift == AG_CANCELLED
                        ? tr("BPM alignment cancelled")
                        : tr("Failed to adjust track BPM");
                    break;
                }

                createdFiles.push_back(job.outputPath);
                result->tracks.push_back(BpmJobResult{
                    job.trackIndex, job.inputPath, job.outputPath,
                    job.originalBpm, job.confidence, speedRatio});
                progress_.store(
                    static_cast<double>(index + 1)
                        / static_cast<double>(jobs.size()),
                    std::memory_order_release);
                QMetaObject::invokeMethod(
                    this, &LightEditor::progressChanged, Qt::QueuedConnection);
            }

            if (result->tracks.size() == jobs.size()) {
                result->status = AG_OK;
            } else {
                for (const QString& path : createdFiles) {
                    QFile::remove(path);
                }
                result->tracks.clear();
            }
            return result->status;
        }));
}

void LightEditor::exportProject(const QString& outputDir,
                                const QString& outputFormat,
                                int outputSampleRate,
                                int outputChannels)
{
    if (busy_.load(std::memory_order_acquire)) {
        return;
    }

    const bool hasSolo = std::any_of(
        tracks_.begin(), tracks_.end(), [](const Track& track) {
            return !track.path.isEmpty() && !track.muted && track.solo;
        });
    std::vector<int> included;
    for (int index = 0; index < kTrackCount; ++index) {
        const Track& track = tracks_[index];
        if (track.path.isEmpty() || track.muted
            || (hasSolo && !track.solo)) {
            continue;
        }
        included.push_back(index);
    }
    if (included.empty()) {
        emit errorOccurred(tr("No audible tracks to export"));
        return;
    }

    if (outputSampleRate < 0 || outputChannels < 0 || outputChannels > 2) {
        emit errorOccurred(tr("Invalid export audio settings"));
        return;
    }
    if (!outputDir.isEmpty() && !QDir().mkpath(outputDir)) {
        emit errorOccurred(tr("Failed to create export directory"));
        return;
    }

    std::vector<QByteArray> pathBytes;
    std::vector<long long> timelineStarts;
    std::vector<long long> trimStarts;
    std::vector<long long> trimEnds;
    std::vector<int> fadeIns;
    std::vector<int> fadeOuts;
    std::vector<double> gains;
    pathBytes.reserve(included.size());
    timelineStarts.reserve(included.size());
    trimStarts.reserve(included.size());
    trimEnds.reserve(included.size());
    fadeIns.reserve(included.size());
    fadeOuts.reserve(included.size());
    gains.reserve(included.size());

    QString firstSource;
    for (int index : included) {
        const Track& track = tracks_[index];
        const QString source =
            !track.renderPath.isEmpty() && QFileInfo::exists(track.renderPath)
            ? track.renderPath : track.path;
        if (firstSource.isEmpty()) {
            firstSource = source;
        }
        pathBytes.push_back(source.toUtf8());
        timelineStarts.push_back(track.timelineStartMs);
        trimStarts.push_back(track.inMs);
        trimEnds.push_back(track.outMs);
        fadeIns.push_back(track.fadeInMs);
        fadeOuts.push_back(track.fadeOutMs);
        gains.push_back(track.gain);
    }

    const QString outputPath = computeOutputPath(
        firstSource, outputDir, outputFormat.toLower(),
        static_cast<int>(included.size()));
    const QByteArray codecName = codecForFormat(outputFormat);
    const QString mixPath = outputPath + QStringLiteral(".agmix-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces)
        + QStringLiteral(".wav");
    ag_cancel_token* token = ag_cancel_token_create();
    token_.store(token, std::memory_order_release);
    setBusy(true);
    setProgress(0.0);

    auto* watcher = new QFutureWatcher<int>(this);
    watcher_ = watcher;
    connect(watcher, &QFutureWatcher<int>::finished, this,
        [this, watcher, outputPath]() {
            watcher->deleteLater();
            watcher_.clear();
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
                emit errorOccurred(tr("Export cancelled"));
            } else {
                emit errorOccurred(
                    tr("Export failed (error %1)").arg(result));
            }
        });

    watcher->setFuture(QtConcurrent::run(
        [this, pathBytes, timelineStarts, trimStarts, trimEnds, fadeIns,
         fadeOuts, gains, outputPath, mixPath, outputSampleRate,
         outputChannels, codecName, token]() {
            std::vector<const char*> inputPaths;
            inputPaths.reserve(pathBytes.size());
            for (const QByteArray& path : pathBytes) {
                inputPaths.push_back(path.constData());
            }
            const QByteArray outputUtf8 = outputPath.toUtf8();
            const QByteArray mixUtf8 = mixPath.toUtf8();
            struct ProgressContext {
                LightEditor* self;
                double base;
                double span;
            };
            auto callback = [](float value, void* userData) {
                auto* context = static_cast<ProgressContext*>(userData);
                auto* self = context->self;
                self->progress_.store(
                    context->base + context->span * value,
                    std::memory_order_release);
                QMetaObject::invokeMethod(
                    self, &LightEditor::progressChanged, Qt::QueuedConnection);
            };

            ProgressContext mixProgress{this, 0.0, 0.7};
            const ag_result mixResult = ag_multitrack_edit_ex(
                inputPaths.size(), inputPaths.data(), timelineStarts.data(),
                trimStarts.data(), trimEnds.data(), fadeIns.data(),
                fadeOuts.data(), gains.data(), mixUtf8.constData(), token,
                callback, &mixProgress);
            if (mixResult != AG_OK) {
                QFile::remove(mixPath);
                return static_cast<int>(mixResult);
            }

            ProgressContext transcodeProgress{this, 0.7, 0.3};
            const ag_result transcodeResult = ag_transcode(
                mixUtf8.constData(), outputUtf8.constData(),
                codecName.isEmpty() ? nullptr : codecName.constData(), 0,
                outputSampleRate, outputChannels, token, callback,
                &transcodeProgress);
            QFile::remove(mixPath);
            return static_cast<int>(transcodeResult);
        }));
}

void LightEditor::start(qint64 trimStartMs, qint64 trimEndMs,
                        int fadeInMs, int fadeOutMs, double gain,
                        const QString& outputDir,
                        const QString& outputFormat,
                        int outputSampleRate,
                        int outputChannels)
{
    Q_UNUSED(trimStartMs)
    Q_UNUSED(trimEndMs)
    Q_UNUSED(fadeInMs)
    Q_UNUSED(fadeOutMs)
    Q_UNUSED(gain)
    exportProject(outputDir, outputFormat, outputSampleRate, outputChannels);
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
    pushUndoState();
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
    pushUndoState();
    tracks_[trackIndex] = Track();
    emit tracksChanged();
    if (trackIndex == selectedTrack_) {
        emit inputFileChanged();
        emit waveformPeaksChanged();
    }
}

bool LightEditor::moveClip(int trackIndex, qint64 timelineStartMs)
{
    if (!isValidTrackIndex(trackIndex)) {
        return false;
    }
    Track& track = tracks_[trackIndex];
    if (track.path.isEmpty() || track.locked) {
        return false;
    }

    const qint64 start = snapEnabled_
        ? snapMs(timelineStartMs, targetBpm_, 4)
        : std::max<qint64>(0, timelineStartMs);
    if (start == track.timelineStartMs) {
        return true;
    }

    pushUndoState();
    track.timelineStartMs = start;
    emit tracksChanged();
    return true;
}

bool LightEditor::trimClip(int trackIndex, qint64 inMs, qint64 outMs)
{
    if (!isValidTrackIndex(trackIndex)) {
        return false;
    }
    Track& track = tracks_[trackIndex];
    if (track.path.isEmpty() || track.locked) {
        return false;
    }

    const ClipBounds bounds = normalizeClip(
        ClipBounds{track.timelineStartMs, inMs, outMs}, track.durationMs);
    if (bounds.inMs == track.inMs && bounds.outMs == track.outMs) {
        return true;
    }

    pushUndoState();
    track.inMs = bounds.inMs;
    track.outMs = bounds.outMs;
    emit tracksChanged();
    if (trackIndex == selectedTrack_) {
        emit inputFileChanged();
    }
    return true;
}

bool LightEditor::copySelectedClip()
{
    if (busy_.load(std::memory_order_acquire)
        || !isValidTrackIndex(selectedTrack_)) {
        return false;
    }
    const Track& track = tracks_[selectedTrack_];
    if (track.path.isEmpty()) {
        return false;
    }
    clipboard_ = track;
    emit clipboardChanged();
    return true;
}

bool LightEditor::cutSelectedClip()
{
    if (busy_.load(std::memory_order_acquire)
        || !isValidTrackIndex(selectedTrack_)
        || tracks_[selectedTrack_].locked) {
        return false;
    }
    if (!copySelectedClip()) {
        return false;
    }
    pushUndoState();
    tracks_[selectedTrack_] = Track();
    emitEditorStateChanged();
    return true;
}

bool LightEditor::pasteClip()
{
    if (busy_.load(std::memory_order_acquire) || !clipboard_.has_value()) {
        return false;
    }

    int destination = selectedTrack_;
    if (!isValidTrackIndex(destination) || !tracks_[destination].path.isEmpty()) {
        const auto empty = std::find_if(
            tracks_.begin(), tracks_.end(),
            [](const Track& track) { return track.path.isEmpty(); });
        if (empty == tracks_.end()) {
            return false;
        }
        destination = static_cast<int>(std::distance(tracks_.begin(), empty));
    }

    pushUndoState();
    tracks_[destination] = *clipboard_;
    selectedTrack_ = destination;
    emitEditorStateChanged();
    return true;
}

bool LightEditor::deleteSelectedClip()
{
    if (busy_.load(std::memory_order_acquire)
        || !isValidTrackIndex(selectedTrack_)
        || tracks_[selectedTrack_].path.isEmpty()
        || tracks_[selectedTrack_].locked) {
        return false;
    }
    pushUndoState();
    tracks_[selectedTrack_] = Track();
    emitEditorStateChanged();
    return true;
}

bool LightEditor::splitSelectedClip(qint64 projectPositionMs)
{
    if (busy_.load(std::memory_order_acquire)
        || !isValidTrackIndex(selectedTrack_)) {
        return false;
    }
    Track& first = tracks_[selectedTrack_];
    if (first.path.isEmpty() || first.locked) {
        return false;
    }
    const qint64 sourcePosition =
        first.inMs + projectPositionMs - first.timelineStartMs;
    if (sourcePosition - first.inMs < 200
        || first.outMs - sourcePosition < 200) {
        return false;
    }
    const auto empty = std::find_if(
        tracks_.begin(), tracks_.end(),
        [](const Track& track) { return track.path.isEmpty(); });
    if (empty == tracks_.end()) {
        return false;
    }
    const int destination =
        static_cast<int>(std::distance(tracks_.begin(), empty));

    pushUndoState();
    Track second = first;
    first.outMs = sourcePosition;
    second.inMs = sourcePosition;
    second.timelineStartMs = projectPositionMs;
    tracks_[destination] = std::move(second);
    emit tracksChanged();
    emit inputFileChanged();
    return true;
}

bool LightEditor::mergeSelectedClip()
{
    if (busy_.load(std::memory_order_acquire)
        || !isValidTrackIndex(selectedTrack_)) {
        return false;
    }
    Track& selected = tracks_[selectedTrack_];
    if (selected.path.isEmpty() || selected.locked) {
        return false;
    }

    int neighborIndex = -1;
    bool selectedFirst = false;
    for (int index = 0; index < kTrackCount; ++index) {
        if (index == selectedTrack_) {
            continue;
        }
        const Track& candidate = tracks_[index];
        if (candidate.path != selected.path || candidate.locked) {
            continue;
        }
        const qint64 selectedEnd = selected.timelineStartMs
            + selected.outMs - selected.inMs;
        const qint64 candidateEnd = candidate.timelineStartMs
            + candidate.outMs - candidate.inMs;
        if (selectedEnd == candidate.timelineStartMs
            && selected.outMs == candidate.inMs) {
            neighborIndex = index;
            selectedFirst = true;
            break;
        }
        if (candidateEnd == selected.timelineStartMs
            && candidate.outMs == selected.inMs) {
            neighborIndex = index;
            selectedFirst = false;
            break;
        }
    }
    if (neighborIndex < 0) {
        return false;
    }

    pushUndoState();
    const Track neighbor = tracks_[neighborIndex];
    if (selectedFirst) {
        selected.outMs = neighbor.outMs;
    } else {
        selected.timelineStartMs = neighbor.timelineStartMs;
        selected.inMs = neighbor.inMs;
    }
    tracks_[neighborIndex] = Track();
    emit tracksChanged();
    emit inputFileChanged();
    return true;
}

bool LightEditor::cropSelectedClip(qint64 projectPositionMs)
{
    if (!isValidTrackIndex(selectedTrack_)) {
        return false;
    }
    const Track& track = tracks_[selectedTrack_];
    if (track.path.isEmpty()) {
        return false;
    }
    const qint64 sourcePosition =
        track.inMs + projectPositionMs - track.timelineStartMs;
    return trimClip(selectedTrack_, track.inMs, sourcePosition);
}

void LightEditor::setTrackMuted(int trackIndex, bool value)
{
    if (!isValidTrackIndex(trackIndex) || tracks_[trackIndex].muted == value) {
        return;
    }
    pushUndoState();
    tracks_[trackIndex].muted = value;
    emit tracksChanged();
}

void LightEditor::setTrackSolo(int trackIndex, bool value)
{
    if (!isValidTrackIndex(trackIndex) || tracks_[trackIndex].solo == value) {
        return;
    }
    pushUndoState();
    tracks_[trackIndex].solo = value;
    emit tracksChanged();
}

void LightEditor::setTrackLocked(int trackIndex, bool value)
{
    if (!isValidTrackIndex(trackIndex) || tracks_[trackIndex].locked == value) {
        return;
    }
    pushUndoState();
    tracks_[trackIndex].locked = value;
    emit tracksChanged();
}

void LightEditor::undo()
{
    if (undoStack_.empty()) {
        return;
    }
    redoStack_.push_back(EditorState{tracks_, selectedTrack_});
    EditorState previous = std::move(undoStack_.back());
    undoStack_.pop_back();
    restoreState(std::move(previous));
}

void LightEditor::redo()
{
    if (redoStack_.empty()) {
        return;
    }
    undoStack_.push_back(EditorState{tracks_, selectedTrack_});
    EditorState next = std::move(redoStack_.back());
    redoStack_.pop_back();
    restoreState(std::move(next));
}
