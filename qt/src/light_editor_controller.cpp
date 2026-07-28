#include "light_editor_controller.hpp"

#include "editor_timeline_math.hpp"

#include <agplayer/c_api.h>

#include <QFileInfo>
#include <QFutureWatcher>
#include <QVariantMap>
#include <QtConcurrent>

#include <algorithm>

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

bool LightEditor::canUndo() const noexcept
{
    return !undoStack_.empty();
}

bool LightEditor::canRedo() const noexcept
{
    return !redoStack_.empty();
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
                // Queue the signal emission back to the main thread; the C
                // progress callback runs on the worker thread.
                QMetaObject::invokeMethod(
                    self, &LightEditor::progressChanged, Qt::QueuedConnection);
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
