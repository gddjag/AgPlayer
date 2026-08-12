#include "light_editor_controller.hpp"

#include "audio_file_discovery.hpp"

#include "editor_timeline_math.hpp"
#include "waveform_cache.hpp"

#include <agplayer/c_api.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
#include <QVariantMap>
#include <QtConcurrent>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <filesystem>

namespace {

constexpr double kReliableBpmConfidence = 50.0;

constexpr std::array<const char*, 8> kTrackColors{
    "#E4007F", "#0078D4", "#00B4A0", "#D27722",
    "#8B5CF6", "#EC4899", "#22C55E", "#F59E0B"};

QString defaultTrackColor(int index)
{
    return QString::fromLatin1(
        kTrackColors[static_cast<std::size_t>(index) % kTrackColors.size()]);
}

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
    if (normalized == QStringLiteral("aac")
        || normalized == QStringLiteral("m4a")) {
        return QByteArrayLiteral("aac");
    }
    return {};
}

void discardRenderedCache(QString& renderPath)
{
    if (!renderPath.isEmpty()) {
        QFile::remove(renderPath);
        renderPath.clear();
    }
}

struct AnalyzeResult {
    int status = AG_INTERNAL_ERROR;
    ag_bpm_result bpm{};
};

struct BpmJob {
    QString clipId;
    int laneIndex = -1;
    QString inputPath;
    QString outputPath;
    double originalBpm = 0.0;
    double confidence = 0.0;
};

struct BpmJobResult {
    QString clipId;
    int laneIndex = -1;
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

std::filesystem::path filesystemPath(const QString& path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::u8path(path.toUtf8().constData());
#endif
}

struct EditorWaveformResult {
    QString clipId;
    QString path;
    quint64 generation = 0;
    std::vector<float> peaks;
    bool cancelled = false;
};

} // namespace

LightEditor::LightEditor(QObject* parent)
    : QObject(parent)
    , tracks_(kTrackCount)
{
    waveformPool_.setMaxThreadCount(2);
    waveformPool_.setExpiryTimeout(30000);
    for (int index = 0; index < kTrackCount; ++index) {
        tracks_[index].laneIndex = index;
        tracks_[index].trackName = QStringLiteral("Track %1").arg(index + 1);
        tracks_[index].color = defaultTrackColor(index);
    }
    autosaveTimer_.setSingleShot(true);
    autosaveTimer_.setInterval(750);
    connect(&autosaveTimer_, &QTimer::timeout,
            this, [this]() { autosaveNow(); });
    const auto markDirty = [this]() { markProjectDirty(); };
    connect(this, &LightEditor::tracksChanged, this, markDirty);
    connect(this, &LightEditor::targetBpmChanged, this, markDirty);
    connect(this, &LightEditor::snapEnabledChanged, this, markDirty);
    connect(this, &LightEditor::projectSettingsChanged, this, markDirty);
    connect(this, &LightEditor::keepPitchChanged, this, markDirty);
}

LightEditor::~LightEditor()
{
    for (const auto& token : std::as_const(waveformCancelTokens_)) {
        if (token != nullptr) {
            ag_cancel_token_cancel(token.get());
        }
    }
    waveformPool_.clear();
    waveformPool_.waitForDone();
    waveformCancelTokens_.clear();
    waveformGenerations_.clear();
    cancel();
    if (watcher_ != nullptr) {
        watcher_->waitForFinished();
    }
    if (importDiscoveryWatcher_ != nullptr) {
        importDiscoveryWatcher_->waitForFinished();
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

bool LightEditor::importBusy() const noexcept
{
    return importBusy_;
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
    if (value < 0 || value >= kTrackCount) {
        return;
    }
    const bool trackChanged = value != selectedTrack_;
    selectedTrack_ = value;
    if (!tracks_[value].path.isEmpty()) {
        const QString nextClipId = tracks_[value].clipId;
        if (selectedClipId_ != nextClipId) {
            selectOnly(nextClipId);
            emit selectedClipChanged();
        }
    } else if (!selectedClipId_.isEmpty()) {
        selectedClipId_.clear();
        selectedClipIds_.clear();
        emit selectedClipChanged();
    }
    if (!trackChanged) {
        return;
    }
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
        QVariantMap value = clipMap(track);
        value.insert(QStringLiteral("hasFile"), !track.path.isEmpty());
        QVariantList laneClips;
        if (!track.path.isEmpty()) {
            laneClips.append(clipMap(track));
        }
        for (const Track& clip : extraClips_) {
            if (clip.laneIndex == track.laneIndex && !clip.path.isEmpty()) {
                laneClips.append(clipMap(clip));
            }
        }
        std::sort(laneClips.begin(), laneClips.end(),
                  [](const QVariant& left, const QVariant& right) {
                      return left.toMap()
                                 .value(QStringLiteral("timelineStartMs"))
                                 .toLongLong()
                          < right.toMap()
                                 .value(QStringLiteral("timelineStartMs"))
                                 .toLongLong();
                  });
        value.insert(QStringLiteral("clips"), laneClips);
        list.append(value);
    }
    return list;
}

int LightEditor::clipCount() const noexcept
{
    const int primaryCount = static_cast<int>(std::count_if(
        tracks_.cbegin(), tracks_.cend(),
        [](const Track& track) { return !track.path.isEmpty(); }));
    return primaryCount + static_cast<int>(extraClips_.size());
}

QString LightEditor::selectedClipId() const
{
    return selectedClipId_;
}

QVariantList LightEditor::selectedClipIds() const
{
    QVariantList values;
    values.reserve(selectedClipIds_.size());
    for (const QString& clipId : selectedClipIds_) {
        values.append(clipId);
    }
    return values;
}

void LightEditor::setSelectedClipId(const QString& value)
{
    const Track* clip = findClip(value);
    if (clip == nullptr) {
        return;
    }
    const bool selectionChanged = selectedClipIds_.size() != 1
        || selectedClipIds_.constFirst() != value;
    selectOnly(value);
    if (selectedTrack_ != clip->laneIndex) {
        selectedTrack_ = clip->laneIndex;
        emit selectedTrackChanged();
    }
    if (selectionChanged) {
        emit selectedClipChanged();
    }
    emit inputFileChanged();
    emit waveformPeaksChanged();
}

double LightEditor::targetBpm() const noexcept
{
    return targetBpm_;
}

void LightEditor::setTargetBpm(double value)
{
    if (value < 40.0 || value > 300.0 || qFuzzyCompare(value, targetBpm_)) {
        return;
    }
    targetBpm_ = value;
    bool updatedFollowEvents = false;
    const auto updateFollowEvent = [this, value, &updatedFollowEvents](Track& clip) {
        if (clip.loopMode != QStringLiteral("FollowProjectBPM")
            || clip.originalBpm < 40.0) {
            return;
        }
        const double ratio = value / clip.originalBpm;
        if (ratio < 0.5 || ratio > 2.0) {
            return;
        }
        if (qFuzzyCompare(clip.targetBpm, value)
            && qFuzzyCompare(clip.speedRatio, ratio)) {
            return;
        }
        discardRenderedCache(clip.renderPath);
        clip.targetBpm = value;
        clip.speedRatio = ratio;
        clip.aligned = false;
        updatedFollowEvents = true;
    };
    for (Track& clip : tracks_) {
        updateFollowEvent(clip);
    }
    for (Track& clip : extraClips_) {
        updateFollowEvent(clip);
    }
    if (updatedFollowEvents) {
        emit tracksChanged();
    }
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

int LightEditor::snapDivision() const noexcept
{
    return snapDivision_;
}

void LightEditor::setSnapDivision(int value)
{
    static constexpr std::array<int, 8> supported{1, 2, 3, 4, 6, 8, 12, 16};
    if (std::find(supported.cbegin(), supported.cend(), value)
            == supported.cend()
        || snapDivision_ == value) {
        return;
    }
    snapDivision_ = value;
    emit projectSettingsChanged();
}

QString LightEditor::timeSignature() const
{
    return timeSignature_;
}

void LightEditor::setTimeSignature(const QString& value)
{
    static const QStringList supported{
        QStringLiteral("2/4"), QStringLiteral("3/4"),
        QStringLiteral("4/4"), QStringLiteral("6/8")};
    if (!supported.contains(value) || timeSignature_ == value) {
        return;
    }
    timeSignature_ = value;
    emit projectSettingsChanged();
}

QString LightEditor::projectKey() const
{
    return projectKey_;
}

void LightEditor::setProjectKey(const QString& value)
{
    const QString normalized = value.trimmed();
    if (normalized.isEmpty() || normalized.size() > 8
        || projectKey_ == normalized) {
        return;
    }
    projectKey_ = normalized;
    emit projectSettingsChanged();
}

bool LightEditor::loopEnabled() const noexcept
{
    return loopEnabled_;
}

void LightEditor::setLoopEnabled(bool value)
{
    if (loopEnabled_ == value) {
        return;
    }
    loopEnabled_ = value;
    emit projectSettingsChanged();
}

qint64 LightEditor::loopStartMs() const noexcept
{
    return loopStartMs_;
}

void LightEditor::setLoopStartMs(qint64 value)
{
    value = std::max<qint64>(0, value);
    if (loopStartMs_ == value) {
        return;
    }
    loopStartMs_ = value;
    if (loopEndMs_ > 0 && loopEndMs_ <= loopStartMs_) {
        loopEndMs_ = loopStartMs_ + 1;
    }
    emit projectSettingsChanged();
}

qint64 LightEditor::loopEndMs() const noexcept
{
    return loopEndMs_;
}

void LightEditor::setLoopEndMs(qint64 value)
{
    value = std::max<qint64>(0, value);
    if (value > 0 && value <= loopStartMs_) {
        value = loopStartMs_ + 1;
    }
    if (loopEndMs_ == value) {
        return;
    }
    loopEndMs_ = value;
    emit projectSettingsChanged();
}

bool LightEditor::rippleEditing() const noexcept
{
    return rippleEditing_;
}

void LightEditor::setRippleEditing(bool value)
{
    if (rippleEditing_ == value) {
        return;
    }
    rippleEditing_ = value;
    emit projectSettingsChanged();
}

bool LightEditor::autoCrossfade() const noexcept
{
    return autoCrossfade_;
}

void LightEditor::setAutoCrossfade(bool value)
{
    if (autoCrossfade_ == value) {
        return;
    }
    autoCrossfade_ = value;
    if (autoCrossfade_) {
        for (int lane = 0; lane < kTrackCount; ++lane) {
            applyAutoCrossfadesForLane(lane);
        }
    } else {
        for (int lane = 0; lane < kTrackCount; ++lane) {
            clearAutomaticCrossfadesForLane(lane);
        }
    }
    markProjectDirty();
    emit projectSettingsChanged();
    emit tracksChanged();
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

QString LightEditor::projectPath() const
{
    return projectPath_;
}

bool LightEditor::projectDirty() const noexcept
{
    return projectDirty_;
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

LightEditor::Track* LightEditor::findClip(const QString& clipId)
{
    if (clipId.isEmpty()) {
        return nullptr;
    }
    for (Track& track : tracks_) {
        if (track.clipId == clipId && !track.path.isEmpty()) {
            return &track;
        }
    }
    const auto extra = std::find_if(
        extraClips_.begin(), extraClips_.end(),
        [&clipId](const Track& clip) { return clip.clipId == clipId; });
    return extra == extraClips_.end() ? nullptr : &*extra;
}

const LightEditor::Track* LightEditor::findClip(const QString& clipId) const
{
    return const_cast<LightEditor*>(this)->findClip(clipId);
}

QVariantList LightEditor::waveformPeaksForClip(const QString& clipId) const
{
    const Track* clip = findClip(clipId);
    return clip != nullptr ? clip->peaks : QVariantList{};
}

QStringList LightEditor::clipIdsInTimelineOrder() const
{
    std::vector<const Track*> clips;
    clips.reserve(static_cast<std::size_t>(clipCount()));
    for (const Track& track : tracks_) {
        if (!track.path.isEmpty()) {
            clips.push_back(&track);
        }
    }
    for (const Track& clip : extraClips_) {
        if (!clip.path.isEmpty()) {
            clips.push_back(&clip);
        }
    }
    std::sort(clips.begin(), clips.end(), [](const Track* left,
                                             const Track* right) {
        if (left->timelineStartMs != right->timelineStartMs) {
            return left->timelineStartMs < right->timelineStartMs;
        }
        if (left->laneIndex != right->laneIndex) {
            return left->laneIndex < right->laneIndex;
        }
        return left->clipId < right->clipId;
    });
    QStringList ids;
    ids.reserve(static_cast<qsizetype>(clips.size()));
    for (const Track* clip : clips) {
        ids.append(clip->clipId);
    }
    return ids;
}

void LightEditor::clearAutomaticCrossfadesForLane(int trackIndex)
{
    if (!isValidTrackIndex(trackIndex)) {
        return;
    }
    std::vector<Track*> clips;
    if (!tracks_[trackIndex].path.isEmpty()) {
        clips.push_back(&tracks_[trackIndex]);
    }
    for (Track& clip : extraClips_) {
        if (clip.laneIndex == trackIndex && !clip.path.isEmpty()) {
            clips.push_back(&clip);
        }
    }
    for (Track* clip : clips) {
        if (clip->autoFadeIn) {
            clip->fadeInMs = 0;
            clip->autoFadeIn = false;
        }
        if (clip->autoFadeOut) {
            clip->fadeOutMs = 0;
            clip->autoFadeOut = false;
        }
    }
}

void LightEditor::applyAutoCrossfadesForLane(int trackIndex)
{
    if (!autoCrossfade_ || !isValidTrackIndex(trackIndex)) {
        return;
    }
    clearAutomaticCrossfadesForLane(trackIndex);
    std::vector<Track*> clips;
    if (!tracks_[trackIndex].path.isEmpty()) {
        clips.push_back(&tracks_[trackIndex]);
    }
    for (Track& clip : extraClips_) {
        if (clip.laneIndex == trackIndex && !clip.path.isEmpty()) {
            clips.push_back(&clip);
        }
    }
    std::sort(clips.begin(), clips.end(), [](const Track* left,
                                              const Track* right) {
        return left->timelineStartMs < right->timelineStartMs;
    });
    const auto duration = [](const Track& clip) {
        return std::max<qint64>(
            1, clip.timelineDurationMs > 0
                ? clip.timelineDurationMs : clip.outMs - clip.inMs);
    };
    for (std::size_t index = 1; index < clips.size(); ++index) {
        Track& earlier = *clips[index - 1];
        Track& later = *clips[index];
        const qint64 overlap = earlier.timelineStartMs + duration(earlier)
            - later.timelineStartMs;
        if (overlap <= 0) {
            continue;
        }
        const int fade = static_cast<int>(std::min<qint64>(
            {overlap, duration(earlier), duration(later),
             std::numeric_limits<int>::max()}));
        if (earlier.fadeOutMs == 0) {
            earlier.fadeOutMs = fade;
            earlier.fadeOutCurve = QStringLiteral("EqualPower");
            earlier.autoFadeOut = true;
        }
        if (later.fadeInMs == 0) {
            later.fadeInMs = fade;
            later.fadeInCurve = QStringLiteral("EqualPower");
            later.autoFadeIn = true;
        }
    }
}

void LightEditor::selectOnly(const QString& clipId)
{
    selectedClipId_ = clipId;
    selectedClipIds_.clear();
    if (!clipId.isEmpty()) {
        selectedClipIds_.append(clipId);
    }
}

qint64 LightEditor::laneEndMs(int trackIndex) const
{
    qint64 end = 0;
    auto include = [&end, trackIndex](const Track& clip) {
        if (clip.laneIndex == trackIndex && !clip.path.isEmpty()) {
            end = std::max(end, clip.timelineStartMs
                                   + std::max<qint64>(0, clip.outMs - clip.inMs));
        }
    };
    if (isValidTrackIndex(trackIndex)) {
        include(tracks_[trackIndex]);
    }
    for (const Track& clip : extraClips_) {
        include(clip);
    }
    return end;
}

void LightEditor::setBusy(bool value)
{
    busy_.store(value, std::memory_order_release);
    emit busyChanged();
}

void LightEditor::setImportBusy(bool value)
{
    if (importBusy_ == value) {
        return;
    }
    importBusy_ = value;
    emit importBusyChanged();
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
    undoStack_.push_back(
        EditorState{tracks_, extraClips_, selectedTrack_, selectedClipId_,
                    selectedClipIds_});
    redoStack_.clear();
    emit undoStateChanged();
}

void LightEditor::restoreState(EditorState state)
{
    tracks_ = std::move(state.tracks);
    extraClips_ = std::move(state.extraClips);
    selectedTrack_ = state.selectedTrack;
    selectedClipId_ = std::move(state.selectedClipId);
    selectedClipIds_ = std::move(state.selectedClipIds);
    if (selectedClipIds_.isEmpty() && !selectedClipId_.isEmpty()) {
        selectedClipIds_.append(selectedClipId_);
    }
    emitEditorStateChanged();
}

void LightEditor::emitEditorStateChanged()
{
    emit tracksChanged();
    emit selectedTrackChanged();
    emit selectedClipChanged();
    emit inputFileChanged();
    emit waveformPeaksChanged();
    emit undoStateChanged();
}

QVariantMap LightEditor::clipMap(const Track& track) const
{
    QVariantMap value;
    value.insert(QStringLiteral("clipId"), track.clipId);
    value.insert(QStringLiteral("trackIndex"), track.laneIndex);
    value.insert(QStringLiteral("path"), track.path);
    value.insert(QStringLiteral("renderPath"), track.renderPath);
    value.insert(QStringLiteral("name"), track.name);
    value.insert(QStringLiteral("format"), track.format);
    value.insert(QStringLiteral("durationMs"), track.durationMs);
    value.insert(QStringLiteral("timelineStartMs"), track.timelineStartMs);
    value.insert(QStringLiteral("inMs"), track.inMs);
    value.insert(QStringLiteral("outMs"), track.outMs);
    value.insert(QStringLiteral("loopMode"), track.loopMode);
    value.insert(QStringLiteral("timelineDurationMs"), track.timelineDurationMs);
    value.insert(QStringLiteral("fadeInMs"), track.fadeInMs);
    value.insert(QStringLiteral("fadeOutMs"), track.fadeOutMs);
    value.insert(QStringLiteral("fadeInCurve"), track.fadeInCurve);
    value.insert(QStringLiteral("fadeOutCurve"), track.fadeOutCurve);
    value.insert(QStringLiteral("autoFadeIn"), track.autoFadeIn);
    value.insert(QStringLiteral("autoFadeOut"), track.autoFadeOut);
    value.insert(QStringLiteral("gain"), track.gain);
    value.insert(QStringLiteral("originalBpm"), track.originalBpm);
    value.insert(QStringLiteral("bpmConfidence"), track.bpmConfidence);
    value.insert(QStringLiteral("targetBpm"),
                 track.targetBpm > 0.0
                     ? track.targetBpm
                     : track.originalBpm * track.speedRatio);
    value.insert(QStringLiteral("speedRatio"), track.speedRatio);
    value.insert(QStringLiteral("keepPitch"), track.keepPitch);
    value.insert(QStringLiteral("pitchSemitones"), track.pitchSemitones);
    value.insert(QStringLiteral("finePitchCents"), track.finePitchCents);
    value.insert(QStringLiteral("formantMode"), track.formantMode);
    value.insert(QStringLiteral("transientProtection"), track.transientProtection);
    value.insert(QStringLiteral("highQuality"), track.highQuality);
    value.insert(QStringLiteral("muted"), track.muted);
    value.insert(QStringLiteral("solo"), track.solo);
    value.insert(QStringLiteral("locked"), track.locked);
    value.insert(QStringLiteral("aligned"), track.aligned);
    value.insert(QStringLiteral("trackName"), track.trackName);
    value.insert(QStringLiteral("color"), track.color);
    value.insert(QStringLiteral("volume"), track.volume);
    value.insert(QStringLiteral("pan"), track.pan);
    value.insert(QStringLiteral("collapsed"), track.collapsed);
    value.insert(QStringLiteral("sampleRate"), track.sampleRate);
    value.insert(QStringLiteral("channels"), track.channels);
    value.insert(QStringLiteral("peaks"), track.peaks);
    value.insert(QStringLiteral("waveformPending"), track.waveformPending);
    value.insert(QStringLiteral("hasFile"), !track.path.isEmpty());
    value.insert(QStringLiteral("available"),
                 !track.path.isEmpty() && QFileInfo::exists(track.path));
    return value;
}

LightEditor::Track LightEditor::makeClip(const QString& path,
                                         int trackIndex) const
{
    Track track;
    track.clipId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    track.laneIndex = trackIndex;
    track.path = path;
    track.renderPath.clear();
    track.name = QFileInfo(path).fileName();
    track.format.clear();
    track.durationMs = 0;
    track.timelineStartMs = 0;
    track.inMs = 0;
    track.outMs = 0;
    track.loopMode = QStringLiteral("OneShot");
    track.timelineDurationMs = 0;
    track.fadeInMs = 0;
    track.fadeOutMs = 0;
    track.fadeInCurve = QStringLiteral("EqualPower");
    track.fadeOutCurve = QStringLiteral("EqualPower");
    track.gain = 1.0;
    track.originalBpm = 0.0;
    track.bpmConfidence = 0.0;
    track.targetBpm = 0.0;
    track.speedRatio = 1.0;
    track.keepPitch = keepPitch_;
    track.muted = false;
    track.solo = false;
    track.locked = false;
    track.aligned = false;
    track.trackName = QStringLiteral("Track %1").arg(trackIndex + 1);
    track.color = defaultTrackColor(trackIndex);
    track.volume = 1.0;
    track.pan = 0.0;
    track.collapsed = false;
    track.sampleRate = 0;
    track.channels = 0;
    track.peaks.clear();

    if (path.isEmpty()) {
        return track;
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
        track.timelineDurationMs = track.durationMs;
        ag_metadata_destroy(meta);
    }

    return track;
}

QString LightEditor::waveformCachePath(const QString& path) const
{
    if (path.isEmpty()) {
        return {};
    }
    const QString root = QDir(
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                             .filePath(QStringLiteral("EditorPeaks"));
    if (!QDir().mkpath(root)) {
        return {};
    }
    const std::string key =
        agplayer::WaveformCache::key_for(filesystemPath(path));
    if (key.empty()) {
        return {};
    }
    return QDir(root).filePath(QString::fromStdString(key)
                               + QStringLiteral(".agpeak"));
}

void LightEditor::requestWaveformAnalysis(const QString& clipId,
                                          const QString& path)
{
    if (clipId.isEmpty() || path.isEmpty()) {
        return;
    }
    if (const auto existing = waveformCancelTokens_.constFind(clipId);
        existing != waveformCancelTokens_.cend() && *existing != nullptr) {
        ag_cancel_token_cancel(existing->get());
    }

    const quint64 generation = ++nextWaveformGeneration_;
    waveformGenerations_.insert(clipId, generation);
    const auto token = std::shared_ptr<ag_cancel_token>(
        ag_cancel_token_create(),
        [](ag_cancel_token* value) {
            if (value != nullptr) {
                ag_cancel_token_destroy(value);
            }
        });
    waveformCancelTokens_.insert(clipId, token);
    if (Track* clip = findClip(clipId); clip != nullptr) {
        clip->waveformPending = true;
        clip->peaks.clear();
    }

    const QString cachePath = waveformCachePath(path);
    auto future = QtConcurrent::run(
        &waveformPool_,
        [this, clipId, path, cachePath, generation, token]() {
            EditorWaveformResult result;
            result.clipId = clipId;
            result.path = path;
            result.generation = generation;

            if (token == nullptr) {
                result.cancelled = true;
            } else if (!cachePath.isEmpty()
                       && agplayer::WaveformCache::load(
                           filesystemPath(cachePath), filesystemPath(path),
                           result.peaks)) {
                result.cancelled = false;
            } else {
                ag_waveform* waveform = nullptr;
                const QByteArray utf8Path = path.toUtf8();
                const int status = ag_waveform_analyze(
                    utf8Path.constData(), 1024, token.get(), nullptr, nullptr,
                    &waveform);
                if (status == AG_OK && waveform != nullptr) {
                    const size_t count = ag_waveform_count(waveform);
                    result.peaks.reserve(count);
                    for (size_t index = 0; index < count; ++index) {
                        result.peaks.push_back(
                            ag_waveform_peak(waveform, index));
                    }
                    if (!cachePath.isEmpty() && !result.peaks.empty()) {
                        (void)agplayer::WaveformCache::save(
                            filesystemPath(cachePath), filesystemPath(path),
                            result.peaks);
                    }
                }
                if (waveform != nullptr) {
                    ag_waveform_destroy(waveform);
                }
                result.cancelled = status == AG_CANCELLED;
            }

            QMetaObject::invokeMethod(
                this,
                [this, result = std::move(result)]() mutable {
                    const auto active =
                        waveformGenerations_.constFind(result.clipId);
                    if (active == waveformGenerations_.cend()
                        || *active != result.generation || result.cancelled) {
                        return;
                    }
                    Track* clip = findClip(result.clipId);
                    if (clip == nullptr || clip->path != result.path) {
                        waveformGenerations_.remove(result.clipId);
                        waveformCancelTokens_.remove(result.clipId);
                        return;
                    }
                    clip->peaks.clear();
                    clip->peaks.reserve(static_cast<int>(result.peaks.size()));
                    for (const float peak : result.peaks) {
                        clip->peaks.append(static_cast<double>(peak));
                    }
                    clip->waveformPending = false;
                    waveformGenerations_.remove(result.clipId);
                    waveformCancelTokens_.remove(result.clipId);
                    if (result.clipId == currentTrack().clipId) {
                        emit waveformPeaksChanged();
                    }
                    emit waveformAnalysisCompleted(result.clipId);
                },
                Qt::QueuedConnection);
        });
    Q_UNUSED(future);
}

void LightEditor::loadPathIntoTrack(const QString& path, int trackIndex)
{
    if (!isValidTrackIndex(trackIndex)) {
        return;
    }
    const QString trackName = tracks_[trackIndex].trackName;
    const QString color = tracks_[trackIndex].color;
    const double volume = tracks_[trackIndex].volume;
    const double pan = tracks_[trackIndex].pan;
    tracks_[trackIndex] = makeClip(path, trackIndex);
    tracks_[trackIndex].trackName = trackName;
    tracks_[trackIndex].color = color;
    tracks_[trackIndex].volume = volume;
    tracks_[trackIndex].pan = pan;
    selectOnly(tracks_[trackIndex].clipId);
    requestWaveformAnalysis(tracks_[trackIndex].clipId,
                            tracks_[trackIndex].path);
}

void LightEditor::loadFile(const QUrl& url)
{
    if (busy_.load(std::memory_order_acquire)) {
        return;
    }
    pushUndoState();
    const QUrl audioUrl = agplayer::qt::firstAudioUrl(url);
    if (audioUrl.isEmpty()) {
        emit errorOccurred(tr("未找到支持的音频文件"));
        return;
    }
    loadPathIntoTrack(audioUrl.toLocalFile(), selectedTrack_);
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
    const QUrl audioUrl = agplayer::qt::firstAudioUrl(url);
    if (audioUrl.isEmpty()) {
        emit errorOccurred(tr("未找到支持的音频文件"));
        return;
    }
    pushUndoState();
    if (tracks_[trackIndex].path.isEmpty()) {
        loadPathIntoTrack(audioUrl.toLocalFile(), trackIndex);
    } else {
        Track clip = makeClip(audioUrl.toLocalFile(), trackIndex);
        clip.timelineStartMs = laneEndMs(trackIndex);
        selectOnly(clip.clipId);
        extraClips_.push_back(std::move(clip));
        requestWaveformAnalysis(extraClips_.back().clipId,
                                extraClips_.back().path);
        emit selectedClipChanged();
    }
    emit tracksChanged();
    if (trackIndex == selectedTrack_) {
        emit inputFileChanged();
        emit waveformPeaksChanged();
    }
}

bool LightEditor::relinkClipById(const QString& clipId, const QUrl& url,
                                 bool relinkMatchingSources)
{
    if (busy_.load(std::memory_order_acquire)) {
        return false;
    }
    Track* selected = findClip(clipId);
    if (selected == nullptr) {
        emit errorOccurred(tr("未找到需要重新定位的片段"));
        return false;
    }
    const QUrl audioUrl = agplayer::qt::firstAudioUrl(url);
    if (audioUrl.isEmpty()) {
        emit errorOccurred(tr("请选择受支持的音频文件"));
        return false;
    }
    const QString replacementPath = audioUrl.toLocalFile();
    Track replacement = makeClip(replacementPath, selected->laneIndex);
    if (replacement.durationMs <= 0 || replacement.sampleRate <= 0
        || replacement.channels <= 0) {
        emit errorOccurred(tr("无法读取重新定位的音频文件"));
        return false;
    }

    const QString missingPath = selected->path;
    pushUndoState();
    auto applyReplacement = [&replacement, &replacementPath](Track& clip) {
        const qint64 previousDuration = clip.durationMs;
        const bool usedWholeSource = clip.inMs <= 0
            && (clip.outMs <= 0 || clip.outMs >= previousDuration);
        clip.path = replacementPath;
        clip.renderPath.clear();
        clip.name = replacement.name;
        clip.format = replacement.format;
        clip.durationMs = replacement.durationMs;
        clip.sampleRate = replacement.sampleRate;
        clip.channels = replacement.channels;
        clip.peaks = replacement.peaks;
        clip.inMs = std::clamp<qint64>(clip.inMs, 0, clip.durationMs);
        clip.outMs = usedWholeSource
            ? clip.durationMs
            : std::clamp<qint64>(clip.outMs, clip.inMs, clip.durationMs);
        if (clip.outMs <= clip.inMs) {
            clip.inMs = 0;
            clip.outMs = clip.durationMs;
        }
        if (clip.loopMode == QStringLiteral("OneShot")) {
            clip.timelineDurationMs = std::max<qint64>(
                1, clip.outMs - clip.inMs);
        }
    };

    int relinked = 0;
    for (Track& clip : tracks_) {
        if (clip.clipId == clipId
            || (relinkMatchingSources && clip.path == missingPath)) {
            applyReplacement(clip);
            ++relinked;
        }
    }
    for (Track& clip : extraClips_) {
        if (clip.clipId == clipId
            || (relinkMatchingSources && clip.path == missingPath)) {
            applyReplacement(clip);
            ++relinked;
        }
    }
    if (relinked == 0) {
        undoStack_.pop_back();
        return false;
    }
    for (Track& clip : tracks_) {
        if (clip.clipId == clipId
            || (relinkMatchingSources && clip.path == replacementPath)) {
            requestWaveformAnalysis(clip.clipId, clip.path);
        }
    }
    for (Track& clip : extraClips_) {
        if (clip.clipId == clipId
            || (relinkMatchingSources && clip.path == replacementPath)) {
            requestWaveformAnalysis(clip.clipId, clip.path);
        }
    }
    emitEditorStateChanged();
    return true;
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

int LightEditor::appendFilesToTimeline(const QList<QUrl>& files)
{
    int loaded = 0;
    for (const QUrl& file : files) {
        const auto empty = std::find_if(
            tracks_.cbegin(), tracks_.cend(),
            [](const Track& track) { return track.path.isEmpty(); });
        if (empty != tracks_.cend()) {
            const int trackIndex =
                static_cast<int>(std::distance(tracks_.cbegin(), empty));
            loadPathIntoTrack(file.toLocalFile(), trackIndex);
        } else {
            const int trackIndex = std::clamp(selectedTrack_, 0, kTrackCount - 1);
            Track clip = makeClip(file.toLocalFile(), trackIndex);
            clip.timelineStartMs = laneEndMs(trackIndex);
            selectOnly(clip.clipId);
            extraClips_.push_back(std::move(clip));
            requestWaveformAnalysis(extraClips_.back().clipId,
                                    extraClips_.back().path);
        }
        ++loaded;
    }
    if (loaded > 0) {
        emit tracksChanged();
        emit inputFileChanged();
        emit waveformPeaksChanged();
        emit selectedClipChanged();
    }
    return loaded;
}

int LightEditor::loadFiles(const QList<QUrl>& urls)
{
    if (busy_.load(std::memory_order_acquire)) {
        return 0;
    }
    const QList<QUrl> files = agplayer::qt::expandAudioUrls(urls);
    if (files.isEmpty()) {
        emit errorOccurred(tr("未找到支持的音频文件"));
        return 0;
    }
    pushUndoState();
    return appendFilesToTimeline(files);
}

void LightEditor::queueFiles(const QList<QUrl>& urls)
{
    if (urls.isEmpty()) {
        return;
    }
    pendingImportUrls_.append(urls);
    if (!importBusy_) {
        setImportBusy(true);
        beginQueuedImportDiscovery();
    }
}

void LightEditor::beginQueuedImportDiscovery()
{
    if (importDiscoveryWatcher_ != nullptr || pendingImportUrls_.isEmpty()) {
        return;
    }
    QList<QUrl> urls = std::move(pendingImportUrls_);
    pendingImportUrls_.clear();

    auto* watcher = new QFutureWatcher<QList<QUrl>>(this);
    importDiscoveryWatcher_ = watcher;
    connect(watcher, &QFutureWatcher<QList<QUrl>>::finished, this,
            [this, watcher]() {
        const QList<QUrl> files = watcher->result();
        if (importDiscoveryWatcher_ == watcher) {
            importDiscoveryWatcher_ = nullptr;
        }
        watcher->deleteLater();

        if (files.isEmpty()) {
            emit errorOccurred(tr("未找到支持的音频文件"));
        } else {
            pendingImportFiles_.append(files);
            pendingImportUndo_ = true;
            QTimer::singleShot(0, this,
                               &LightEditor::processQueuedImportChunk);
            return;
        }
        if (!pendingImportUrls_.isEmpty()) {
            beginQueuedImportDiscovery();
        } else {
            setImportBusy(false);
        }
    });
    watcher->setFuture(agplayer::qt::expandAudioUrlsAsync(std::move(urls)));
}

void LightEditor::processQueuedImportChunk()
{
    constexpr qsizetype kFilesPerTurn = 3;
    if (pendingImportFiles_.isEmpty()) {
        if (!pendingImportUrls_.isEmpty()) {
            beginQueuedImportDiscovery();
        } else {
            setImportBusy(false);
        }
        return;
    }
    if (pendingImportUndo_) {
        pushUndoState();
        pendingImportUndo_ = false;
    }

    const qsizetype count = std::min(kFilesPerTurn, pendingImportFiles_.size());
    QList<QUrl> files;
    files.reserve(count);
    for (qsizetype index = 0; index < count; ++index) {
        files.push_back(pendingImportFiles_.takeFirst());
    }
    const int loaded = appendFilesToTimeline(files);
    if (loaded > 0) {
        emit filesQueued(loaded);
    }
    QTimer::singleShot(0, this, &LightEditor::processQueuedImportChunk);
}

void LightEditor::analyzeTrackBpm(int trackIndex)
{
    if (!isValidTrackIndex(trackIndex) || tracks_[trackIndex].path.isEmpty()) {
        emit trackError(trackIndex, tr("No input file loaded"));
        return;
    }
    analyzeClipBpmById(tracks_[trackIndex].clipId);
}

void LightEditor::analyzeClipBpmById(const QString& clipId)
{
    const Track* clip = findClip(clipId);
    const int trackIndex = clip != nullptr ? clip->laneIndex : -1;
    if (busy_.load(std::memory_order_acquire)) {
        emit trackError(trackIndex, tr("Editor is busy"));
        return;
    }
    if (clip == nullptr || clip->path.isEmpty()) {
        emit trackError(trackIndex, tr("No input file loaded"));
        return;
    }

    const QString inputPath = clip->path;
    auto result = std::make_shared<AnalyzeResult>();
    setBusy(true);
    setProgress(0.0);

    auto* watcher = new QFutureWatcher<int>(this);
    watcher_ = watcher;
    connect(watcher, &QFutureWatcher<int>::finished, this,
        [this, watcher, result, trackIndex, clipId, inputPath]() {
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

            Track* track = findClip(clipId);
            if (track == nullptr || track->path != inputPath) {
                return;
            }
            track->originalBpm = result->bpm.bpm;
            track->bpmConfidence = result->bpm.confidence;
            track->targetBpm = result->bpm.bpm;
            track->speedRatio = 1.0;
            track->aligned = false;
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
    const auto appendJob = [&jobs, &cacheRoot](const Track& track) {
        if (track.path.isEmpty()) {
            return;
        }
        BpmJob job;
        job.clipId = track.clipId;
        job.laneIndex = track.laneIndex;
        job.inputPath = track.path;
        job.outputPath = QDir(cacheRoot).filePath(
            QUuid::createUuid().toString(QUuid::WithoutBraces)
            + QStringLiteral(".wav"));
        job.originalBpm = track.originalBpm;
        job.confidence = track.bpmConfidence;
        jobs.push_back(std::move(job));
    };
    for (const Track& track : tracks_) {
        appendJob(track);
    }
    for (const Track& clip : extraClips_) {
        appendJob(clip);
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
                Track* track = findClip(item.clipId);
                if (track == nullptr || track->path != item.inputPath) {
                    continue;
                }
                track->originalBpm = item.originalBpm;
                track->bpmConfidence = item.confidence;
                track->targetBpm = targetBpm_;
                track->speedRatio = item.speedRatio;
                track->keepPitch = keepPitch_;
                track->renderPath = item.outputPath;
                track->inMs = qRound64(track->inMs / item.speedRatio);
                track->outMs = qRound64(track->outMs / item.speedRatio);
                track->durationMs = qRound64(
                    track->durationMs / item.speedRatio);
                if (alignBeats) {
                    track->timelineStartMs =
                        snapMs(track->timelineStartMs, targetBpm_, snapDivision_);
                }
                track->aligned = true;
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
                if (job.originalBpm < 40.0 || job.originalBpm > 300.0
                    || job.confidence < kReliableBpmConfidence) {
                    ag_bpm_result bpm{};
                    const QByteArray inputPath = job.inputPath.toUtf8();
                    const ag_result analysis =
                        ag_bpm_analyze(inputPath.constData(), &bpm);
                    if (analysis != AG_OK
                        || bpm.confidence < kReliableBpmConfidence) {
                        result->status = analysis == AG_OK
                            ? AG_DECODE_ERROR : analysis;
                        result->failureIndex = job.laneIndex;
                        result->message = tr("Reliable BPM was not detected");
                        break;
                    }
                    job.originalBpm = bpm.bpm;
                    job.confidence = bpm.confidence;
                }

                const double speedRatio = targetBpm / job.originalBpm;
                if (speedRatio < 0.5 || speedRatio > 2.0) {
                    result->status = AG_INVALID_ARGUMENT;
                    result->failureIndex = job.laneIndex;
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
                    result->failureIndex = job.laneIndex;
                    result->message = shift == AG_CANCELLED
                        ? tr("BPM alignment cancelled")
                        : tr("Failed to adjust track BPM");
                    break;
                }

                createdFiles.push_back(job.outputPath);
                result->tracks.push_back(BpmJobResult{
                    job.clipId, job.laneIndex, job.inputPath, job.outputPath,
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

void LightEditor::exportProjectScope(const QString& scope,
                                     qint64 rangeStartMs,
                                     qint64 rangeEndMs,
                                     const QString& outputDir,
                                     const QString& outputFormat,
                                     int outputSampleRate,
                                     int outputChannels)
{
    const QString normalized = scope.trimmed().toLower();
    if (normalized != QStringLiteral("project")
        && normalized != QStringLiteral("loop")
        && normalized != QStringLiteral("selected")) {
        emit errorOccurred(tr("未知的导出范围"));
        return;
    }
    if (normalized == QStringLiteral("loop")
        && (rangeStartMs < 0 || rangeEndMs <= rangeStartMs)) {
        emit errorOccurred(tr("请先设置有效的循环区域"));
        return;
    }
    pendingExportScope_ = normalized;
    pendingExportRangeStartMs_ = rangeStartMs;
    pendingExportRangeEndMs_ = rangeEndMs;
    exportProject(outputDir, outputFormat, outputSampleRate, outputChannels);
}

void LightEditor::exportProject(const QString& outputDir,
                                const QString& outputFormat,
                                int outputSampleRate,
                                int outputChannels)
{
    if (busy_.load(std::memory_order_acquire)) {
        return;
    }

    const QString exportScope = pendingExportScope_;
    const qint64 exportRangeStartMs = pendingExportRangeStartMs_;
    const qint64 exportRangeEndMs = pendingExportRangeEndMs_;
    pendingExportScope_ = QStringLiteral("project");
    pendingExportRangeStartMs_ = 0;
    pendingExportRangeEndMs_ = 0;
    const bool exportSelected = exportScope == QStringLiteral("selected");
    const bool exportLoopRange = exportScope == QStringLiteral("loop");

    std::vector<const Track*> allClips;
    allClips.reserve(static_cast<std::size_t>(clipCount()));
    for (const Track& track : tracks_) {
        if (!track.path.isEmpty()) {
            allClips.push_back(&track);
        }
    }
    for (const Track& clip : extraClips_) {
        if (!clip.path.isEmpty()) {
            allClips.push_back(&clip);
        }
    }
    const bool hasSolo = std::any_of(
        allClips.cbegin(), allClips.cend(),
        [this, exportSelected](const Track* track) {
            return (!exportSelected || selectedClipIds_.contains(track->clipId))
                && !track->muted && track->solo;
        });
    std::vector<const Track*> included;
    for (const Track* track : allClips) {
        if (exportSelected && !selectedClipIds_.contains(track->clipId)) {
            continue;
        }
        if (track->muted || (hasSolo && !track->solo)) {
            continue;
        }
        included.push_back(track);
    }

    if (included.empty()) {
        emit errorOccurred(exportSelected
                               ? tr("没有可导出的选中片段")
                               : tr("没有可导出的可听轨道"));
        return;
    }

    if (outputSampleRate < 0 || outputChannels < 0 || outputChannels > 2) {
        emit errorOccurred(tr("导出音频参数无效"));
        return;
    }
    if (!outputDir.isEmpty() && !QDir().mkpath(outputDir)) {
        emit errorOccurred(tr("无法创建导出目录"));
        return;
    }

    std::vector<QByteArray> pathBytes;
    std::vector<long long> timelineStarts;
    std::vector<long long> trimStarts;
    std::vector<long long> trimEnds;
    std::vector<int> fadeIns;
    std::vector<int> fadeOuts;
    std::vector<int> fadeInCurves;
    std::vector<int> fadeOutCurves;
    std::vector<double> gains;
    std::vector<double> pans;
    std::vector<long long> timelineDurations;
    std::vector<int> loopFlags;
    std::vector<double> speedRatios;
    std::vector<int> keepPitchFlags;
    std::vector<int> sourceRenderedFlags;
    std::vector<int> pitchCents;
    std::vector<int> formantModes;
    std::vector<double> transientProtections;
    std::vector<int> highQualityFlags;
    pathBytes.reserve(included.size());
    timelineStarts.reserve(included.size());
    trimStarts.reserve(included.size());
    trimEnds.reserve(included.size());
    fadeIns.reserve(included.size());
    fadeOuts.reserve(included.size());
    fadeInCurves.reserve(included.size());
    fadeOutCurves.reserve(included.size());
    gains.reserve(included.size());
    pans.reserve(included.size());
    timelineDurations.reserve(included.size());
    loopFlags.reserve(included.size());
    speedRatios.reserve(included.size());
    keepPitchFlags.reserve(included.size());
    sourceRenderedFlags.reserve(included.size());
    pitchCents.reserve(included.size());
    formantModes.reserve(included.size());
    transientProtections.reserve(included.size());
    highQualityFlags.reserve(included.size());

    qint64 timelineOriginMs = 0;
    if (exportSelected) {
        timelineOriginMs = std::numeric_limits<qint64>::max();
        for (const Track* track : included) {
            timelineOriginMs = std::min(timelineOriginMs, track->timelineStartMs);
        }
    }
    QString firstSource;
    int projectSampleRate = 0;
    for (const Track* trackPointer : included) {
        const Track& track = *trackPointer;
        const bool sourceRendered = !track.renderPath.isEmpty()
            && QFileInfo::exists(track.renderPath);
        const QString source = sourceRendered ? track.renderPath : track.path;
        if (firstSource.isEmpty()) {
            firstSource = source;
            projectSampleRate = track.sampleRate;
        }
        pathBytes.push_back(source.toUtf8());
        timelineStarts.push_back(track.timelineStartMs - timelineOriginMs);
        trimStarts.push_back(track.inMs);
        trimEnds.push_back(track.outMs);
        fadeIns.push_back(track.fadeInMs);
        fadeOuts.push_back(track.fadeOutMs);
        const auto fadeCurveValue = [](const QString& curve) {
            if (curve == QStringLiteral("Linear")) return 0;
            if (curve == QStringLiteral("Smooth")) return 2;
            return 1;
        };
        fadeInCurves.push_back(fadeCurveValue(track.fadeInCurve));
        fadeOutCurves.push_back(fadeCurveValue(track.fadeOutCurve));
        const int lane = std::clamp(track.laneIndex, 0, kTrackCount - 1);
        gains.push_back(track.gain * tracks_[lane].volume);
        pans.push_back(tracks_[lane].pan);
        timelineDurations.push_back(track.timelineDurationMs > 0
                                        ? track.timelineDurationMs
                                        : std::max<qint64>(1, track.outMs - track.inMs));
        loopFlags.push_back(track.loopMode == QStringLiteral("OneShot") ? 0 : 1);
        speedRatios.push_back(std::clamp(track.speedRatio, 0.5, 2.0));
        keepPitchFlags.push_back(track.keepPitch ? 1 : 0);
        sourceRenderedFlags.push_back(sourceRendered ? 1 : 0);
        pitchCents.push_back(std::clamp(
            qRound(track.pitchSemitones * 100.0 + track.finePitchCents),
            -1200, 1200));
        formantModes.push_back(std::clamp(track.formantMode, 0, 2));
        transientProtections.push_back(
            std::clamp(track.transientProtection, 0.0, 1.0));
        highQualityFlags.push_back(track.highQuality ? 1 : 0);
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
                emit errorOccurred(tr("导出已取消"));
            } else {
                emit errorOccurred(
                    tr("导出失败（错误 %1）").arg(result));
            }
        });

    watcher->setFuture(QtConcurrent::run(
        [this, pathBytes, timelineStarts, trimStarts, trimEnds, fadeIns,
         fadeOuts, fadeInCurves, fadeOutCurves, gains, pans,
         timelineDurations, loopFlags, speedRatios,
         keepPitchFlags, sourceRenderedFlags, pitchCents, formantModes,
         transientProtections, highQualityFlags, outputPath, mixPath,
         outputSampleRate, outputChannels, codecName, token,
          projectSampleRate, exportLoopRange, exportRangeStartMs,
          exportRangeEndMs]() mutable {
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

            std::vector<QString> dspPaths;
            dspPaths.reserve(pathBytes.size());
            const auto clearDspPaths = [&dspPaths]() {
                for (const QString& path : dspPaths) {
                    QFile::remove(path);
                }
            };
            for (std::size_t index = 0; index < pathBytes.size(); ++index) {
                const double ratio = sourceRenderedFlags[index] != 0
                    ? 1.0 : speedRatios[index];
                const bool needsDsp = std::abs(ratio - 1.0) >= 0.0001
                    || pitchCents[index] != 0 || formantModes[index] != 0
                    || transientProtections[index] > 0.0
                    || highQualityFlags[index] != 0;
                if (!needsDsp) {
                    continue;
                }
                const QString dspPath = mixPath
                    + QStringLiteral(".dsp-%1.wav").arg(index);
                const QByteArray dspPathUtf8 = dspPath.toUtf8();
                ag_pitch_shift_options options{};
                options.vocal_protection = formantModes[index] > 0 ? 1 : 0;
                options.smooth_transition =
                    transientProtections[index] > 0.0
                        || highQualityFlags[index] != 0 ? 1 : 0;
                options.output_sample_rate = projectSampleRate;
                const ag_result dspResult = ag_pitch_shift_ex(
                    pathBytes[index].constData(), dspPathUtf8.constData(),
                    pitchCents[index],
                    keepPitchFlags[index], 1.0 / ratio, "pcm_s16le",
                    &options, token, nullptr, nullptr);
                if (dspResult != AG_OK) {
                    QFile::remove(dspPath);
                    clearDspPaths();
                    return static_cast<int>(dspResult);
                }
                dspPaths.push_back(dspPath);
                pathBytes[index] = dspPathUtf8;
                trimStarts[index] = qRound64(trimStarts[index] / ratio);
                trimEnds[index] = qRound64(trimEnds[index] / ratio);
                if (loopFlags[index] == 0) {
                    timelineDurations[index] = qRound64(
                        timelineDurations[index] / ratio);
                }
            }

            ProgressContext mixProgress{this, 0.0, 0.7};
            ag_result mixResult = AG_INTERNAL_ERROR;
            if (projectSampleRate > 0) {
                const auto toSamples = [projectSampleRate](long long ms) {
                    return qRound64(static_cast<double>(ms)
                                    * projectSampleRate / 1000.0);
                };
                std::vector<ag_multitrack_track_v3> renderTracks(
                    pathBytes.size());
                for (std::size_t index = 0; index < pathBytes.size(); ++index) {
                    ag_multitrack_track_v3& renderTrack = renderTracks[index];
                    renderTrack.struct_size = sizeof(ag_multitrack_track_v3);
                    renderTrack.api_version = 1;
                    renderTrack.input_path = pathBytes[index].constData();
                    renderTrack.timeline_start_sample =
                        toSamples(timelineStarts[index]);
                    renderTrack.trim_start_sample =
                        toSamples(trimStarts[index]);
                    renderTrack.trim_end_sample = trimEnds[index] > 0
                        ? toSamples(trimEnds[index]) : -1;
                    renderTrack.fade_in_samples = toSamples(fadeIns[index]);
                    renderTrack.fade_out_samples = toSamples(fadeOuts[index]);
                    renderTrack.fade_in_curve = fadeInCurves[index];
                    renderTrack.fade_out_curve = fadeOutCurves[index];
                    renderTrack.gain = gains[index];
                    renderTrack.pan = pans[index];
                    renderTrack.timeline_duration_samples =
                        timelineDurations[index] > 0
                            ? toSamples(timelineDurations[index]) : -1;
                    renderTrack.loop = loopFlags[index];
                }
                mixResult = ag_multitrack_edit_v3(
                    renderTracks.size(), renderTracks.data(),
                    mixUtf8.constData(), token, callback, &mixProgress);
            } else {
                std::vector<ag_multitrack_track_v2> renderTracks(
                    pathBytes.size());
                for (std::size_t index = 0; index < pathBytes.size(); ++index) {
                    ag_multitrack_track_v2& renderTrack = renderTracks[index];
                    renderTrack.struct_size = sizeof(ag_multitrack_track_v2);
                    renderTrack.api_version = 1;
                    renderTrack.input_path = pathBytes[index].constData();
                    renderTrack.timeline_start_ms = timelineStarts[index];
                    renderTrack.trim_start_ms = trimStarts[index];
                    renderTrack.trim_end_ms = trimEnds[index];
                    renderTrack.fade_in_ms = fadeIns[index];
                    renderTrack.fade_out_ms = fadeOuts[index];
                    renderTrack.gain = gains[index];
                    renderTrack.pan = pans[index];
                    renderTrack.timeline_duration_ms = timelineDurations[index];
                    renderTrack.loop = loopFlags[index];
                }
                mixResult = ag_multitrack_edit_v2(
                    renderTracks.size(), renderTracks.data(),
                    mixUtf8.constData(), token, callback, &mixProgress);
            }
            clearDspPaths();
            if (mixResult != AG_OK) {
                QFile::remove(mixPath);
                return static_cast<int>(mixResult);
            }
            QString transcodeInputPath = mixPath;
            QString rangePath;
            if (exportLoopRange) {
                rangePath = mixPath + QStringLiteral(".range.wav");
                const QByteArray rangeUtf8 = rangePath.toUtf8();
                ProgressContext rangeProgress{this, 0.7, 0.1};
                const ag_result rangeResult = ag_light_edit(
                    mixUtf8.constData(), rangeUtf8.constData(),
                    exportRangeStartMs, exportRangeEndMs, 0, 0, 1.0,
                    token, callback, &rangeProgress);
                if (rangeResult != AG_OK) {
                    QFile::remove(mixPath);
                    QFile::remove(rangePath);
                    return static_cast<int>(rangeResult);
                }
                transcodeInputPath = rangePath;
            }

            ProgressContext transcodeProgress{
                this, exportLoopRange ? 0.8 : 0.7,
                exportLoopRange ? 0.2 : 0.3};
            const QByteArray transcodeInputUtf8 = transcodeInputPath.toUtf8();
            const ag_result transcodeResult = ag_transcode(
                transcodeInputUtf8.constData(), outputUtf8.constData(),
                codecName.isEmpty() ? nullptr : codecName.constData(), 0,
                outputSampleRate, outputChannels, token, callback,
                &transcodeProgress);
            QFile::remove(mixPath);
            if (!rangePath.isEmpty()) {
                QFile::remove(rangePath);
            }
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
    for (int index = 0; index < kTrackCount; ++index) {
        tracks_[index].laneIndex = index;
        tracks_[index].trackName = QStringLiteral("Track %1").arg(index + 1);
        tracks_[index].color = defaultTrackColor(index);
    }
    extraClips_.clear();
    selectedTrack_ = 0;
    selectedClipId_.clear();
    setProgress(0.0);
    emit tracksChanged();
    emit selectedTrackChanged();
    emit selectedClipChanged();
    emit inputFileChanged();
    emit waveformPeaksChanged();
}

void LightEditor::clearTrack(int trackIndex)
{
    if (busy_.load(std::memory_order_acquire)) {
        return;
    }
    if (!isValidTrackIndex(trackIndex)) {
        emit errorOccurred(tr("轨道编号无效"));
        return;
    }
    pushUndoState();
    tracks_[trackIndex] = Track();
    tracks_[trackIndex].laneIndex = trackIndex;
    tracks_[trackIndex].trackName = QStringLiteral("Track %1").arg(trackIndex + 1);
    tracks_[trackIndex].color = defaultTrackColor(trackIndex);
    extraClips_.erase(std::remove_if(
                          extraClips_.begin(), extraClips_.end(),
                          [trackIndex](const Track& clip) {
                              return clip.laneIndex == trackIndex;
                          }),
                      extraClips_.end());
    if (selectedTrack_ == trackIndex) {
        selectedClipId_.clear();
        emit selectedClipChanged();
    }
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
        ? snapMs(timelineStartMs, targetBpm_, snapDivision_)
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
    const qint64 timelineDuration = track.loopMode == QStringLiteral("OneShot")
        ? bounds.outMs - bounds.inMs : track.timelineDurationMs;
    if (bounds.inMs == track.inMs && bounds.outMs == track.outMs
        && timelineDuration == track.timelineDurationMs) {
        return true;
    }

    pushUndoState();
    track.inMs = bounds.inMs;
    track.outMs = bounds.outMs;
    track.timelineDurationMs = timelineDuration;
    emit tracksChanged();
    if (trackIndex == selectedTrack_) {
        emit inputFileChanged();
    }
    return true;
}

bool LightEditor::moveClipById(const QString& clipId, qint64 timelineStartMs)
{
    Track* clip = findClip(clipId);
    if (clip == nullptr || clip->locked) {
        return false;
    }
    const qint64 start = snapEnabled_
        ? snapMs(timelineStartMs, targetBpm_, snapDivision_)
        : std::max<qint64>(0, timelineStartMs);
    if (clip->timelineStartMs == start) {
        return true;
    }
    pushUndoState();
    clip = findClip(clipId);
    clip->timelineStartMs = start;
    applyAutoCrossfadesForLane(clip->laneIndex);
    selectOnly(clipId);
    selectedTrack_ = clip->laneIndex;
    emitEditorStateChanged();
    return true;
}

bool LightEditor::moveClipToTrackById(const QString& clipId,
                                      int targetTrackIndex,
                                      qint64 timelineStartMs)
{
    if (!isValidTrackIndex(targetTrackIndex)) {
        return false;
    }
    Track* clip = findClip(clipId);
    if (clip == nullptr || clip->locked) {
        return false;
    }
    const qint64 start = snapEnabled_
        ? snapMs(timelineStartMs, targetBpm_, snapDivision_)
        : std::max<qint64>(0, timelineStartMs);
    if (clip->laneIndex == targetTrackIndex
        && clip->timelineStartMs == start) {
        return true;
    }

    pushUndoState();
    clip = findClip(clipId);
    const int sourceTrackIndex = clip->laneIndex;
    Track moved = *clip;
    moved.laneIndex = targetTrackIndex;
    moved.timelineStartMs = start;

    const Track destinationLane = tracks_[targetTrackIndex];
    moved.trackName = destinationLane.trackName;
    moved.color = destinationLane.color;
    moved.volume = destinationLane.volume;
    moved.pan = destinationLane.pan;
    moved.collapsed = destinationLane.collapsed;
    moved.muted = destinationLane.muted;
    moved.solo = destinationLane.solo;

    bool wasPrimary = false;
    for (int index = 0; index < kTrackCount; ++index) {
        if (tracks_[index].clipId != clipId) {
            continue;
        }
        wasPrimary = true;
        const Track sourceLane = tracks_[index];
        tracks_[index] = Track();
        tracks_[index].laneIndex = index;
        tracks_[index].trackName = sourceLane.trackName;
        tracks_[index].color = sourceLane.color;
        tracks_[index].volume = sourceLane.volume;
        tracks_[index].pan = sourceLane.pan;
        tracks_[index].collapsed = sourceLane.collapsed;
        tracks_[index].muted = sourceLane.muted;
        tracks_[index].solo = sourceLane.solo;
        break;
    }
    if (!wasPrimary) {
        extraClips_.erase(std::remove_if(
                              extraClips_.begin(), extraClips_.end(),
                              [&clipId](const Track& candidate) {
                                  return candidate.clipId == clipId;
                              }),
                          extraClips_.end());
    }

    if (tracks_[targetTrackIndex].path.isEmpty()) {
        tracks_[targetTrackIndex] = std::move(moved);
    } else {
        extraClips_.push_back(std::move(moved));
    }
    applyAutoCrossfadesForLane(sourceTrackIndex);
    applyAutoCrossfadesForLane(targetTrackIndex);
    selectedTrack_ = targetTrackIndex;
    selectOnly(clipId);
    emitEditorStateChanged();
    return true;
}

bool LightEditor::selectClip(const QString& clipId, bool additive,
                             bool rangeSelection)
{
    const Track* clip = findClip(clipId);
    if (clip == nullptr) {
        return false;
    }

    QStringList next;
    if (rangeSelection && !selectedClipId_.isEmpty()) {
        const QStringList ordered = clipIdsInTimelineOrder();
        const qsizetype anchor = ordered.indexOf(selectedClipId_);
        const qsizetype current = ordered.indexOf(clipId);
        if (anchor >= 0 && current >= 0) {
            const qsizetype first = std::min(anchor, current);
            const qsizetype last = std::max(anchor, current);
            for (qsizetype index = first; index <= last; ++index) {
                next.append(ordered.at(index));
            }
        }
    } else if (additive) {
        next = selectedClipIds_;
        const qsizetype position = next.indexOf(clipId);
        if (position >= 0 && next.size() > 1) {
            next.removeAt(position);
        } else if (position < 0) {
            next.append(clipId);
        }
    }
    if (next.isEmpty()) {
        next.append(clipId);
    }

    selectedClipIds_ = next;
    selectedClipId_ = clipId;
    if (!selectedClipIds_.contains(selectedClipId_)) {
        selectedClipId_ = selectedClipIds_.constFirst();
    }
    selectedTrack_ = clip->laneIndex;
    emit selectedTrackChanged();
    emit selectedClipChanged();
    emit inputFileChanged();
    emit waveformPeaksChanged();
    return true;
}

int LightEditor::selectClipsInRange(qint64 startMs, qint64 endMs,
                                    int firstTrack, int lastTrack,
                                    bool additive)
{
    if (startMs > endMs) {
        std::swap(startMs, endMs);
    }
    startMs = std::max<qint64>(0, startMs);
    endMs = std::max<qint64>(startMs, endMs);
    if (firstTrack > lastTrack) {
        std::swap(firstTrack, lastTrack);
    }
    firstTrack = std::clamp(firstTrack, 0, kTrackCount - 1);
    lastTrack = std::clamp(lastTrack, 0, kTrackCount - 1);

    QStringList matches;
    const auto collect = [&](const Track& clip) {
        if (clip.path.isEmpty() || clip.laneIndex < firstTrack
            || clip.laneIndex > lastTrack) {
            return;
        }
        const qint64 clipStart = clip.timelineStartMs;
        const qint64 clipEnd = clipStart + std::max<qint64>(
            0, clip.timelineDurationMs > 0
                   ? clip.timelineDurationMs : clip.outMs - clip.inMs);
        if (clipStart < endMs && clipEnd > startMs) {
            matches.append(clip.clipId);
        }
    };
    for (const Track& clip : tracks_) {
        collect(clip);
    }
    for (const Track& clip : extraClips_) {
        collect(clip);
    }

    QStringList next = additive ? selectedClipIds_ : QStringList{};
    for (const QString& clipId : matches) {
        if (!next.contains(clipId)) {
            next.append(clipId);
        }
    }
    selectedClipIds_ = next;
    selectedClipId_ = selectedClipIds_.isEmpty()
        ? QString{} : selectedClipIds_.constLast();
    if (const Track* clip = findClip(selectedClipId_)) {
        selectedTrack_ = clip->laneIndex;
    }
    emit selectedTrackChanged();
    emit selectedClipChanged();
    emit inputFileChanged();
    emit waveformPeaksChanged();
    return matches.size();
}

void LightEditor::selectAllClips()
{
    const QStringList ids = clipIdsInTimelineOrder();
    if (ids.isEmpty()) {
        clearClipSelection();
        return;
    }
    selectedClipIds_ = ids;
    selectedClipId_ = ids.constFirst();
    if (const Track* clip = findClip(selectedClipId_)) {
        selectedTrack_ = clip->laneIndex;
    }
    emit selectedTrackChanged();
    emit selectedClipChanged();
    emit inputFileChanged();
    emit waveformPeaksChanged();
}

void LightEditor::clearClipSelection()
{
    if (selectedClipIds_.isEmpty() && selectedClipId_.isEmpty()) {
        return;
    }
    selectOnly(QString{});
    emit selectedClipChanged();
    emit inputFileChanged();
    emit waveformPeaksChanged();
}

bool LightEditor::moveSelectedClips(qint64 deltaMs, int trackDelta)
{
    QStringList ids = selectedClipIds_;
    if (ids.isEmpty() && !selectedClipId_.isEmpty()) {
        ids.append(selectedClipId_);
    }
    if (ids.isEmpty()) {
        return false;
    }

    std::vector<Track> moved;
    moved.reserve(static_cast<std::size_t>(ids.size()));
    qint64 earliest = std::numeric_limits<qint64>::max();
    for (const QString& id : ids) {
        const Track* clip = findClip(id);
        if (clip == nullptr || clip->locked
            || !isValidTrackIndex(clip->laneIndex + trackDelta)) {
            return false;
        }
        earliest = std::min(earliest, clip->timelineStartMs);
        moved.push_back(*clip);
    }
    qint64 effectiveDelta = deltaMs;
    const qint64 requestedAnchor = std::max<qint64>(0, earliest + deltaMs);
    if (snapEnabled_) {
        effectiveDelta = snapMs(requestedAnchor, targetBpm_, snapDivision_)
            - earliest;
    } else if (earliest + effectiveDelta < 0) {
        effectiveDelta = -earliest;
    }
    if (effectiveDelta == 0 && trackDelta == 0) {
        return true;
    }

    pushUndoState();
    const auto resetLane = [this](int lane) {
        const Track settings = tracks_[lane];
        tracks_[lane] = Track{};
        tracks_[lane].laneIndex = lane;
        tracks_[lane].trackName = settings.trackName;
        tracks_[lane].color = settings.color;
        tracks_[lane].volume = settings.volume;
        tracks_[lane].pan = settings.pan;
        tracks_[lane].collapsed = settings.collapsed;
        tracks_[lane].muted = settings.muted;
        tracks_[lane].solo = settings.solo;
        tracks_[lane].locked = settings.locked;
    };
    for (int lane = 0; lane < kTrackCount; ++lane) {
        if (ids.contains(tracks_[lane].clipId)) {
            resetLane(lane);
        }
    }
    extraClips_.erase(std::remove_if(
                          extraClips_.begin(), extraClips_.end(),
                          [&ids](const Track& clip) {
                              return ids.contains(clip.clipId);
                          }),
                      extraClips_.end());

    for (Track& clip : moved) {
        clip.laneIndex += trackDelta;
        clip.timelineStartMs = std::max<qint64>(
            0, clip.timelineStartMs + effectiveDelta);
        const Track& lane = tracks_[clip.laneIndex];
        clip.trackName = lane.trackName;
        clip.color = lane.color;
        clip.volume = lane.volume;
        clip.pan = lane.pan;
        clip.collapsed = lane.collapsed;
        clip.muted = lane.muted;
        clip.solo = lane.solo;
        clip.locked = lane.locked;
        if (tracks_[clip.laneIndex].path.isEmpty()) {
            tracks_[clip.laneIndex] = clip;
        } else {
            extraClips_.push_back(clip);
        }
    }
    selectedClipIds_ = ids;
    selectedClipId_ = ids.constFirst();
    if (const Track* clip = findClip(selectedClipId_)) {
        selectedTrack_ = clip->laneIndex;
    }
    emitEditorStateChanged();
    return true;
}

bool LightEditor::trimClipById(const QString& clipId, qint64 inMs,
                               qint64 outMs)
{
    Track* clip = findClip(clipId);
    if (clip == nullptr || clip->locked) {
        return false;
    }
    const ClipBounds bounds = normalizeClip(
        ClipBounds{clip->timelineStartMs, inMs, outMs}, clip->durationMs);
    const qint64 timelineDuration = clip->loopMode == QStringLiteral("OneShot")
        ? bounds.outMs - bounds.inMs : clip->timelineDurationMs;
    if (bounds.inMs == clip->inMs && bounds.outMs == clip->outMs
        && timelineDuration == clip->timelineDurationMs) {
        return true;
    }
    pushUndoState();
    clip = findClip(clipId);
    clip->inMs = bounds.inMs;
    clip->outMs = bounds.outMs;
    clip->timelineDurationMs = timelineDuration;
    selectOnly(clipId);
    selectedTrack_ = clip->laneIndex;
    applyAutoCrossfadesForLane(clip->laneIndex);
    emitEditorStateChanged();
    return true;
}

bool LightEditor::trimClipEdgeById(const QString& clipId, qint64 inMs,
                                   qint64 outMs, bool trimLeft,
                                   bool bypassSnap)
{
    Track* clip = findClip(clipId);
    if (clip == nullptr || clip->locked) {
        return false;
    }
    ClipBounds bounds = normalizeClip(
        ClipBounds{clip->timelineStartMs, inMs, outMs}, clip->durationMs);
    if (snapEnabled_ && !bypassSnap) {
        if (trimLeft) {
            const qint64 desiredStart = clip->timelineStartMs
                + bounds.inMs - clip->inMs;
            const qint64 snappedStart = snapMs(
                desiredStart, targetBpm_, snapDivision_);
            bounds = normalizeClip(ClipBounds{
                clip->timelineStartMs,
                clip->inMs + snappedStart - clip->timelineStartMs,
                bounds.outMs}, clip->durationMs);
        } else {
            const qint64 desiredEnd = clip->timelineStartMs
                + bounds.outMs - clip->inMs;
            const qint64 snappedEnd = snapMs(
                desiredEnd, targetBpm_, snapDivision_);
            bounds = normalizeClip(ClipBounds{
                clip->timelineStartMs, bounds.inMs,
                clip->inMs + snappedEnd - clip->timelineStartMs},
                clip->durationMs);
        }
    }
    const qint64 newTimelineStart = trimLeft
        ? std::max<qint64>(0, clip->timelineStartMs + bounds.inMs - clip->inMs)
        : clip->timelineStartMs;
    const qint64 sourceDuration = bounds.outMs - bounds.inMs;
    const qint64 newTimelineDuration = clip->loopMode == QStringLiteral("OneShot")
        ? sourceDuration : clip->timelineDurationMs;
    if (bounds.inMs == clip->inMs && bounds.outMs == clip->outMs
        && newTimelineStart == clip->timelineStartMs
        && newTimelineDuration == clip->timelineDurationMs) {
        return true;
    }

    pushUndoState();
    clip = findClip(clipId);
    clip->inMs = bounds.inMs;
    clip->outMs = bounds.outMs;
    clip->timelineStartMs = newTimelineStart;
    clip->timelineDurationMs = newTimelineDuration;
    selectOnly(clipId);
    selectedTrack_ = clip->laneIndex;
    applyAutoCrossfadesForLane(clip->laneIndex);
    emitEditorStateChanged();
    return true;
}

bool LightEditor::setClipLoopById(const QString& clipId,
                                   const QString& loopMode,
                                   qint64 timelineDurationMs)
{
    static const QStringList supportedModes{
        QStringLiteral("OneShot"), QStringLiteral("Loop"),
        QStringLiteral("FollowProjectBPM")};
    Track* clip = findClip(clipId);
    if (clip == nullptr || clip->locked || !supportedModes.contains(loopMode)) {
        return false;
    }
    const bool followProjectBpm = loopMode == QStringLiteral("FollowProjectBPM");
    const double followRatio = followProjectBpm
        ? targetBpm_ / clip->originalBpm : clip->speedRatio;
    if (followProjectBpm && (clip->originalBpm < 40.0
                             || followRatio < 0.5 || followRatio > 2.0)) {
        emit errorOccurred(tr("请先检测素材 BPM，且项目 BPM 必须在素材 BPM 的 0.5–2 倍内"));
        return false;
    }
    const qint64 sourceDuration = std::max<qint64>(1, clip->outMs - clip->inMs);
    const qint64 requestedDuration = loopMode == QStringLiteral("OneShot")
        ? sourceDuration : std::max<qint64>(200, timelineDurationMs);
    if (clip->loopMode == loopMode
        && clip->timelineDurationMs == requestedDuration) {
        return true;
    }
    pushUndoState();
    clip = findClip(clipId);
    clip->loopMode = loopMode;
    clip->timelineDurationMs = requestedDuration;
    if (followProjectBpm) {
        discardRenderedCache(clip->renderPath);
        clip->targetBpm = targetBpm_;
        clip->speedRatio = followRatio;
        clip->aligned = false;
    }
    selectOnly(clipId);
    selectedTrack_ = clip->laneIndex;
    emitEditorStateChanged();
    return true;
}

bool LightEditor::setClipFadesById(const QString& clipId,
                                   int fadeInMs, int fadeOutMs)
{
    Track* clip = findClip(clipId);
    if (clip == nullptr || clip->locked || fadeInMs < 0 || fadeOutMs < 0) {
        return false;
    }
    const qint64 clipDuration = std::max<qint64>(0, clip->outMs - clip->inMs);
    if (static_cast<qint64>(fadeInMs) + fadeOutMs > clipDuration) {
        return false;
    }
    if (clip->fadeInMs == fadeInMs && clip->fadeOutMs == fadeOutMs) {
        return true;
    }
    pushUndoState();
    clip = findClip(clipId);
    clip->fadeInMs = fadeInMs;
    clip->fadeOutMs = fadeOutMs;
    clip->autoFadeIn = false;
    clip->autoFadeOut = false;
    selectedTrack_ = clip->laneIndex;
    selectOnly(clipId);
    emitEditorStateChanged();
    return true;
}

bool LightEditor::setClipFadeCurvesById(const QString& clipId,
                                        const QString& fadeInCurve,
                                        const QString& fadeOutCurve)
{
    const auto validCurve = [](const QString& curve) {
        return curve == QStringLiteral("Linear")
            || curve == QStringLiteral("EqualPower")
            || curve == QStringLiteral("Smooth");
    };
    Track* clip = findClip(clipId);
    if (clip == nullptr || clip->locked
        || !validCurve(fadeInCurve) || !validCurve(fadeOutCurve)) {
        return false;
    }
    if (clip->fadeInCurve == fadeInCurve
        && clip->fadeOutCurve == fadeOutCurve) {
        return true;
    }
    pushUndoState();
    clip = findClip(clipId);
    clip->fadeInCurve = fadeInCurve;
    clip->fadeOutCurve = fadeOutCurve;
    selectedTrack_ = clip->laneIndex;
    selectOnly(clipId);
    emitEditorStateChanged();
    return true;
}

bool LightEditor::setClipGainById(const QString& clipId, double gain)
{
    Track* clip = findClip(clipId);
    if (clip == nullptr || clip->locked || gain < 0.0 || gain > 4.0) {
        return false;
    }
    if (qFuzzyCompare(clip->gain, gain)) {
        return true;
    }
    pushUndoState();
    clip = findClip(clipId);
    clip->gain = gain;
    selectedTrack_ = clip->laneIndex;
    selectOnly(clipId);
    emitEditorStateChanged();
    return true;
}

bool LightEditor::setClipMutedById(const QString& clipId, bool muted)
{
    Track* clip = findClip(clipId);
    if (clip == nullptr || clip->locked) {
        return false;
    }
    if (clip->muted == muted) {
        return true;
    }
    pushUndoState();
    clip = findClip(clipId);
    clip->muted = muted;
    selectedTrack_ = clip->laneIndex;
    selectOnly(clipId);
    emitEditorStateChanged();
    return true;
}

bool LightEditor::setClipPitchById(const QString& clipId,
                                   double semitones,
                                   double cents)
{
    Track* clip = findClip(clipId);
    if (clip == nullptr || clip->locked || semitones < -12.0
        || semitones > 12.0 || cents < -100.0 || cents > 100.0) {
        return false;
    }
    if (qFuzzyCompare(clip->pitchSemitones + 1.0, semitones + 1.0)
        && qFuzzyCompare(clip->finePitchCents + 1.0, cents + 1.0)) {
        return true;
    }
    pushUndoState();
    clip = findClip(clipId);
    discardRenderedCache(clip->renderPath);
    clip->pitchSemitones = semitones;
    clip->finePitchCents = cents;
    selectedTrack_ = clip->laneIndex;
    selectOnly(clipId);
    emitEditorStateChanged();
    return true;
}

bool LightEditor::setClipFormantModeById(const QString& clipId, int mode)
{
    Track* clip = findClip(clipId);
    if (clip == nullptr || clip->locked || mode < 0 || mode > 2) {
        return false;
    }
    if (clip->formantMode == mode) {
        return true;
    }
    pushUndoState();
    clip = findClip(clipId);
    discardRenderedCache(clip->renderPath);
    clip->formantMode = mode;
    selectedTrack_ = clip->laneIndex;
    selectOnly(clipId);
    emitEditorStateChanged();
    return true;
}

bool LightEditor::setClipTransientProtectionById(const QString& clipId,
                                                 double amount)
{
    Track* clip = findClip(clipId);
    if (clip == nullptr || clip->locked || amount < 0.0 || amount > 1.0) {
        return false;
    }
    if (qFuzzyCompare(clip->transientProtection + 1.0, amount + 1.0)) {
        return true;
    }
    pushUndoState();
    clip = findClip(clipId);
    discardRenderedCache(clip->renderPath);
    clip->transientProtection = amount;
    selectedTrack_ = clip->laneIndex;
    selectOnly(clipId);
    emitEditorStateChanged();
    return true;
}

bool LightEditor::setClipHighQualityById(const QString& clipId, bool enabled)
{
    Track* clip = findClip(clipId);
    if (clip == nullptr || clip->locked) {
        return false;
    }
    if (clip->highQuality == enabled) {
        return true;
    }
    pushUndoState();
    clip = findClip(clipId);
    discardRenderedCache(clip->renderPath);
    clip->highQuality = enabled;
    selectedTrack_ = clip->laneIndex;
    selectOnly(clipId);
    emitEditorStateChanged();
    return true;
}

bool LightEditor::setClipTargetBpmById(const QString& clipId, double value)
{
    Track* clip = findClip(clipId);
    if (clip == nullptr || clip->locked || value < 40.0 || value > 300.0
        || clip->originalBpm < 40.0) {
        return false;
    }
    const double ratio = value / clip->originalBpm;
    if (ratio < 0.5 || ratio > 2.0) {
        return false;
    }
    if (qFuzzyCompare(clip->targetBpm, value)
        && qFuzzyCompare(clip->speedRatio, ratio)) {
        return true;
    }
    pushUndoState();
    clip = findClip(clipId);
    discardRenderedCache(clip->renderPath);
    clip->targetBpm = value;
    clip->speedRatio = ratio;
    clip->aligned = false;
    selectedTrack_ = clip->laneIndex;
    selectOnly(clipId);
    emitEditorStateChanged();
    return true;
}

bool LightEditor::setClipKeepPitchById(const QString& clipId, bool value)
{
    Track* clip = findClip(clipId);
    if (clip == nullptr || clip->locked) {
        return false;
    }
    if (clip->keepPitch == value) {
        return true;
    }
    pushUndoState();
    clip = findClip(clipId);
    discardRenderedCache(clip->renderPath);
    clip->keepPitch = value;
    selectedTrack_ = clip->laneIndex;
    selectOnly(clipId);
    emitEditorStateChanged();
    return true;
}

bool LightEditor::setClipBeatAlignedById(const QString& clipId, bool value)
{
    Track* clip = findClip(clipId);
    if (clip == nullptr || clip->locked) {
        return false;
    }
    const double bpm = clip->targetBpm > 0.0 ? clip->targetBpm : targetBpm_;
    const qint64 snappedStart = value
        ? snapMs(clip->timelineStartMs, bpm, snapDivision_)
        : clip->timelineStartMs;
    if (clip->aligned == value && clip->timelineStartMs == snappedStart) {
        return true;
    }
    pushUndoState();
    clip = findClip(clipId);
    clip->aligned = value;
    clip->timelineStartMs = snappedStart;
    selectedTrack_ = clip->laneIndex;
    selectOnly(clipId);
    emitEditorStateChanged();
    return true;
}

bool LightEditor::deleteClipById(const QString& clipId)
{
    Track* clip = findClip(clipId);
    if (clip == nullptr || clip->locked) {
        return false;
    }
    const qint64 removedStart = clip->timelineStartMs;
    const qint64 removedDuration = std::max<qint64>(0, clip->outMs - clip->inMs);
    const qint64 removedEnd = removedStart + removedDuration;
    pushUndoState();
    auto shiftForRipple = [&](Track& candidate) {
        if (!rippleEditing_ || candidate.path.isEmpty()
            || candidate.timelineStartMs < removedEnd) {
            return;
        }
        candidate.timelineStartMs =
            std::max<qint64>(removedStart,
                             candidate.timelineStartMs - removedDuration);
    };
    for (int index = 0; index < kTrackCount; ++index) {
        if (tracks_[index].clipId == clipId) {
            tracks_[index] = Track();
            tracks_[index].laneIndex = index;
            tracks_[index].trackName = QStringLiteral("Track %1").arg(index + 1);
            tracks_[index].color = defaultTrackColor(index);
            break;
        }
    }
    extraClips_.erase(std::remove_if(
                          extraClips_.begin(), extraClips_.end(),
                          [&clipId](const Track& candidate) {
                              return candidate.clipId == clipId;
                          }),
                      extraClips_.end());
    for (Track& candidate : tracks_) {
        shiftForRipple(candidate);
    }
    for (Track& candidate : extraClips_) {
        shiftForRipple(candidate);
    }
    selectOnly(QString{});
    emitEditorStateChanged();
    return true;
}

bool LightEditor::copySelectedClip()
{
    if (busy_.load(std::memory_order_acquire)
        || !isValidTrackIndex(selectedTrack_)) {
        return false;
    }
    const Track* track = findClip(selectedClipId_);
    if (track == nullptr) {
        track = &tracks_[selectedTrack_];
    }
    if (track->path.isEmpty()) {
        return false;
    }
    clipboard_ = *track;
    emit clipboardChanged();
    return true;
}

bool LightEditor::cutSelectedClip()
{
    if (busy_.load(std::memory_order_acquire)
        || !isValidTrackIndex(selectedTrack_)) {
        return false;
    }
    if (!copySelectedClip()) {
        return false;
    }
    return deleteClipById(selectedClipId_);
}

bool LightEditor::pasteClip()
{
    if (busy_.load(std::memory_order_acquire) || !clipboard_.has_value()) {
        return false;
    }

    int destination = selectedTrack_;
    if (!isValidTrackIndex(destination)) {
        return false;
    }
    if (!tracks_[destination].path.isEmpty()) {
        pushUndoState();
        Track pasted = *clipboard_;
        pasted.clipId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        pasted.laneIndex = destination;
        pasted.timelineStartMs = laneEndMs(destination);
        selectOnly(pasted.clipId);
        extraClips_.push_back(std::move(pasted));
        emitEditorStateChanged();
        return true;
    }
    if (tracks_[destination].path.isEmpty()) {
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
    tracks_[destination].clipId =
        QUuid::createUuid().toString(QUuid::WithoutBraces);
    tracks_[destination].laneIndex = destination;
    selectedTrack_ = destination;
    selectOnly(tracks_[destination].clipId);
    emitEditorStateChanged();
    return true;
}

bool LightEditor::duplicateSelectedClip(qint64 timelineStartMs)
{
    if (busy_.load(std::memory_order_acquire)
        || !isValidTrackIndex(selectedTrack_)) {
        return false;
    }
    const Track* selected = findClip(selectedClipId_);
    if (selected == nullptr) {
        selected = &tracks_[selectedTrack_];
    }
    const qint64 requestedStart = timelineStartMs >= 0
        ? timelineStartMs
        : selected->timelineStartMs + selected->outMs - selected->inMs;
    return duplicateClipToTrackById(selected->clipId, selected->laneIndex,
                                    requestedStart);
}

bool LightEditor::duplicateClipToTrackById(const QString& clipId,
                                           int targetTrackIndex,
                                           qint64 timelineStartMs)
{
    if (busy_.load(std::memory_order_acquire)
        || !isValidTrackIndex(targetTrackIndex)) {
        return false;
    }
    const Track* source = findClip(clipId);
    if (source == nullptr || source->path.isEmpty() || source->locked) {
        return false;
    }

    pushUndoState();
    Track duplicate = *source;
    duplicate.clipId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    duplicate.laneIndex = targetTrackIndex;
    duplicate.timelineStartMs = snapEnabled_
        ? snapMs(timelineStartMs, targetBpm_, snapDivision_)
        : std::max<qint64>(0, timelineStartMs);
    selectedTrack_ = targetTrackIndex;
    selectOnly(duplicate.clipId);
    extraClips_.push_back(std::move(duplicate));
    applyAutoCrossfadesForLane(targetTrackIndex);
    emitEditorStateChanged();
    return true;
}

bool LightEditor::deleteSelectedClip()
{
    if (busy_.load(std::memory_order_acquire)
        || !isValidTrackIndex(selectedTrack_)) {
        return false;
    }
    if (selectedClipIds_.size() <= 1 && !selectedClipId_.isEmpty()) {
        return deleteClipById(selectedClipId_);
    }
    if (selectedClipIds_.isEmpty()) {
        return false;
    }

    struct RemovedRange {
        qint64 start = 0;
        qint64 end = 0;
    };
    std::vector<RemovedRange> ranges;
    ranges.reserve(static_cast<std::size_t>(selectedClipIds_.size()));
    for (const QString& id : selectedClipIds_) {
        const Track* clip = findClip(id);
        if (clip == nullptr || clip->locked) {
            return false;
        }
        ranges.push_back(RemovedRange{
            clip->timelineStartMs,
            clip->timelineStartMs
                + std::max<qint64>(0, clip->outMs - clip->inMs)});
    }
    std::sort(ranges.begin(), ranges.end(), [](const RemovedRange& left,
                                                const RemovedRange& right) {
        return left.start < right.start;
    });

    pushUndoState();
    const QStringList ids = selectedClipIds_;
    for (int lane = 0; lane < kTrackCount; ++lane) {
        if (!ids.contains(tracks_[lane].clipId)) {
            continue;
        }
        const Track settings = tracks_[lane];
        tracks_[lane] = Track{};
        tracks_[lane].laneIndex = lane;
        tracks_[lane].trackName = settings.trackName;
        tracks_[lane].color = settings.color;
        tracks_[lane].volume = settings.volume;
        tracks_[lane].pan = settings.pan;
        tracks_[lane].collapsed = settings.collapsed;
        tracks_[lane].muted = settings.muted;
        tracks_[lane].solo = settings.solo;
        tracks_[lane].locked = settings.locked;
    }
    extraClips_.erase(std::remove_if(
                          extraClips_.begin(), extraClips_.end(),
                          [&ids](const Track& clip) {
                              return ids.contains(clip.clipId);
                          }),
                      extraClips_.end());

    if (rippleEditing_) {
        const auto shift = [&ranges](Track& clip) {
            if (clip.path.isEmpty()) {
                return;
            }
            qint64 removedBefore = 0;
            for (const RemovedRange& range : ranges) {
                if (range.end <= clip.timelineStartMs) {
                    removedBefore += std::max<qint64>(0, range.end - range.start);
                }
            }
            clip.timelineStartMs = std::max<qint64>(
                0, clip.timelineStartMs - removedBefore);
        };
        for (Track& clip : tracks_) {
            shift(clip);
        }
        for (Track& clip : extraClips_) {
            shift(clip);
        }
    }
    selectedClipId_.clear();
    selectedClipIds_.clear();
    emitEditorStateChanged();
    return true;
}

bool LightEditor::splitSelectedClip(qint64 projectPositionMs)
{
    if (busy_.load(std::memory_order_acquire)
        || !isValidTrackIndex(selectedTrack_)) {
        return false;
    }
    Track* first = findClip(selectedClipId_);
    if (first == nullptr) {
        first = &tracks_[selectedTrack_];
    }
    if (first->path.isEmpty() || first->locked) {
        return false;
    }
    const qint64 sourcePosition =
        first->inMs + projectPositionMs - first->timelineStartMs;
    if (sourcePosition - first->inMs < 200
        || first->outMs - sourcePosition < 200) {
        return false;
    }
    const QString firstId = first->clipId;
    pushUndoState();
    first = findClip(firstId);
    Track second = *first;
    second.clipId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    first->outMs = sourcePosition;
    second.inMs = sourcePosition;
    second.timelineStartMs = projectPositionMs;
    selectOnly(second.clipId);
    extraClips_.push_back(std::move(second));
    emitEditorStateChanged();
    return true;
}

bool LightEditor::mergeSelectedClip()
{
    if (busy_.load(std::memory_order_acquire)
        || !isValidTrackIndex(selectedTrack_)) {
        return false;
    }
    Track* selected = findClip(selectedClipId_);
    if (selected == nullptr) {
        selected = &tracks_[selectedTrack_];
    }
    if (selected->path.isEmpty() || selected->locked) {
        return false;
    }

    const QString selectedId = selected->clipId;
    const int laneIndex = selected->laneIndex;
    QString neighborId;
    bool selectedFirst = false;
    auto considerCandidate = [&](const Track& candidate) {
        if (!neighborId.isEmpty() || candidate.clipId == selectedId
            || candidate.path != selected->path || candidate.locked
            || candidate.laneIndex != laneIndex) {
            return;
        }
        const qint64 selectedEnd = selected->timelineStartMs
            + selected->outMs - selected->inMs;
        const qint64 candidateEnd = candidate.timelineStartMs
            + candidate.outMs - candidate.inMs;
        if (selectedEnd == candidate.timelineStartMs
            && selected->outMs == candidate.inMs) {
            neighborId = candidate.clipId;
            selectedFirst = true;
        } else if (candidateEnd == selected->timelineStartMs
                   && candidate.outMs == selected->inMs) {
            neighborId = candidate.clipId;
            selectedFirst = false;
        }
    };
    for (const Track& candidate : tracks_) {
        considerCandidate(candidate);
    }
    for (const Track& candidate : extraClips_) {
        considerCandidate(candidate);
    }
    if (neighborId.isEmpty()) {
        return false;
    }

    auto isPrimary = [&](const QString& clipId) {
        return std::any_of(tracks_.cbegin(), tracks_.cend(),
                           [&clipId](const Track& candidate) {
                               return candidate.clipId == clipId;
                           });
    };
    const bool selectedPrimary = isPrimary(selectedId);
    const bool neighborPrimary = isPrimary(neighborId);
    const QString keeperId = neighborPrimary && !selectedPrimary
        ? neighborId
        : selectedId;
    const QString removedId = keeperId == selectedId ? neighborId : selectedId;

    const Track selectedSnapshot = *selected;
    const Track* neighbor = findClip(neighborId);
    if (neighbor == nullptr) {
        return false;
    }
    const Track neighborSnapshot = *neighbor;

    pushUndoState();
    Track* keeper = findClip(keeperId);
    if (keeper == nullptr) {
        return false;
    }
    const Track& first = selectedFirst ? selectedSnapshot : neighborSnapshot;
    const Track& second = selectedFirst ? neighborSnapshot : selectedSnapshot;
    keeper->timelineStartMs = first.timelineStartMs;
    keeper->inMs = first.inMs;
    keeper->outMs = second.outMs;

    for (int index = 0; index < kTrackCount; ++index) {
        if (tracks_[index].clipId != removedId) {
            continue;
        }
        const Track lane = tracks_[index];
        tracks_[index] = Track();
        tracks_[index].laneIndex = index;
        tracks_[index].trackName = lane.trackName;
        tracks_[index].color = lane.color;
        tracks_[index].volume = lane.volume;
        tracks_[index].pan = lane.pan;
        tracks_[index].collapsed = lane.collapsed;
        break;
    }
    extraClips_.erase(std::remove_if(
                          extraClips_.begin(), extraClips_.end(),
                          [&removedId](const Track& candidate) {
                              return candidate.clipId == removedId;
                          }),
                      extraClips_.end());
    selectedTrack_ = laneIndex;
    selectOnly(keeperId);
    emitEditorStateChanged();
    return true;
}

bool LightEditor::cropSelectedClip(qint64 projectPositionMs)
{
    if (!isValidTrackIndex(selectedTrack_)) {
        return false;
    }
    const Track* track = findClip(selectedClipId_);
    if (track == nullptr) {
        track = &tracks_[selectedTrack_];
    }
    if (track->path.isEmpty()) {
        return false;
    }
    const qint64 sourcePosition =
        track->inMs + projectPositionMs - track->timelineStartMs;
    return trimClipById(track->clipId, track->inMs, sourcePosition);
}

void LightEditor::setTrackMuted(int trackIndex, bool value)
{
    if (!isValidTrackIndex(trackIndex) || tracks_[trackIndex].muted == value) {
        return;
    }
    pushUndoState();
    tracks_[trackIndex].muted = value;
    for (Track& clip : extraClips_) {
        if (clip.laneIndex == trackIndex) {
            clip.muted = value;
        }
    }
    emit tracksChanged();
}

void LightEditor::setTrackSolo(int trackIndex, bool value)
{
    if (!isValidTrackIndex(trackIndex) || tracks_[trackIndex].solo == value) {
        return;
    }
    pushUndoState();
    tracks_[trackIndex].solo = value;
    for (Track& clip : extraClips_) {
        if (clip.laneIndex == trackIndex) {
            clip.solo = value;
        }
    }
    emit tracksChanged();
}

void LightEditor::setTrackLocked(int trackIndex, bool value)
{
    if (!isValidTrackIndex(trackIndex) || tracks_[trackIndex].locked == value) {
        return;
    }
    pushUndoState();
    tracks_[trackIndex].locked = value;
    for (Track& clip : extraClips_) {
        if (clip.laneIndex == trackIndex) {
            clip.locked = value;
        }
    }
    emit tracksChanged();
}

bool LightEditor::setTrackVolume(int trackIndex, double value)
{
    if (!isValidTrackIndex(trackIndex) || !std::isfinite(value)) {
        return false;
    }
    value = std::clamp(value, 0.0, 2.0);
    if (qFuzzyCompare(tracks_[trackIndex].volume, value)) {
        return true;
    }
    pushUndoState();
    tracks_[trackIndex].volume = value;
    for (Track& clip : extraClips_) {
        if (clip.laneIndex == trackIndex) {
            clip.volume = value;
        }
    }
    emit tracksChanged();
    return true;
}

bool LightEditor::setTrackPan(int trackIndex, double value)
{
    if (!isValidTrackIndex(trackIndex) || !std::isfinite(value)) {
        return false;
    }
    value = std::clamp(value, -1.0, 1.0);
    if (qFuzzyCompare(tracks_[trackIndex].pan + 1.0, value + 1.0)) {
        return true;
    }
    pushUndoState();
    tracks_[trackIndex].pan = value;
    for (Track& clip : extraClips_) {
        if (clip.laneIndex == trackIndex) {
            clip.pan = value;
        }
    }
    emit tracksChanged();
    return true;
}

bool LightEditor::setTrackName(int trackIndex, const QString& value)
{
    const QString name = value.trimmed();
    if (!isValidTrackIndex(trackIndex) || name.isEmpty()) {
        return false;
    }
    if (tracks_[trackIndex].trackName == name) {
        return true;
    }
    pushUndoState();
    tracks_[trackIndex].trackName = name.left(80);
    emit tracksChanged();
    return true;
}

bool LightEditor::setTrackColor(int trackIndex, const QString& value)
{
    const QString color = value.trimmed().toUpper();
    static const QRegularExpression pattern(
        QStringLiteral("^#[0-9A-F]{6}$"));
    if (!isValidTrackIndex(trackIndex) || !pattern.match(color).hasMatch()) {
        return false;
    }
    if (tracks_[trackIndex].color == color) {
        return true;
    }
    pushUndoState();
    tracks_[trackIndex].color = color;
    emit tracksChanged();
    return true;
}

bool LightEditor::setTrackCollapsed(int trackIndex, bool value)
{
    if (!isValidTrackIndex(trackIndex)) {
        return false;
    }
    if (tracks_[trackIndex].collapsed == value) {
        return true;
    }
    pushUndoState();
    tracks_[trackIndex].collapsed = value;
    for (Track& clip : extraClips_) {
        if (clip.laneIndex == trackIndex) {
            clip.collapsed = value;
        }
    }
    emit tracksChanged();
    return true;
}

bool LightEditor::moveTrack(int from, int to)
{
    if (!isValidTrackIndex(from) || !isValidTrackIndex(to) || from == to) {
        return false;
    }
    pushUndoState();
    std::swap(tracks_[from], tracks_[to]);
    tracks_[from].laneIndex = from;
    tracks_[to].laneIndex = to;
    for (Track& clip : extraClips_) {
        if (clip.laneIndex == from) {
            clip.laneIndex = to;
        } else if (clip.laneIndex == to) {
            clip.laneIndex = from;
        }
    }
    if (selectedTrack_ == from) {
        selectedTrack_ = to;
    } else if (selectedTrack_ == to) {
        selectedTrack_ = from;
    }
    emitEditorStateChanged();
    return true;
}

void LightEditor::markProjectDirty()
{
    if (loadingProject_) {
        return;
    }
    if (!projectDirty_) {
        projectDirty_ = true;
        emit projectStateChanged();
    }
    if (!projectPath_.isEmpty()) {
        autosaveTimer_.start();
    }
}

bool LightEditor::writeProject(const QString& path) const
{
    if (path.isEmpty()) {
        return false;
    }
    const QFileInfo projectInfo(path);
    const QDir projectDirectory(projectInfo.absolutePath());
    const auto portablePath = [&projectDirectory](const QString& source) {
        if (source.isEmpty() || !QFileInfo(source).isAbsolute()) {
            return source;
        }
        return QDir::cleanPath(projectDirectory.relativeFilePath(source));
    };
    auto serialize = [this, &portablePath](const Track& track) {
        QVariantMap values = clipMap(track);
        values.insert(QStringLiteral("path"), portablePath(track.path));
        values.insert(QStringLiteral("renderPath"), portablePath(track.renderPath));
        values.remove(QStringLiteral("peaks"));
        values.remove(QStringLiteral("waveformPending"));
        values.remove(QStringLiteral("hasFile"));
        return QJsonObject::fromVariantMap(values);
    };
    QJsonArray lanes;
    for (const Track& track : tracks_) {
        lanes.append(serialize(track));
    }
    QJsonArray extra;
    for (const Track& clip : extraClips_) {
        extra.append(serialize(clip));
    }
    QJsonArray selectedClips;
    for (const QString& clipId : selectedClipIds_) {
        selectedClips.append(clipId);
    }
    QJsonArray assets;
    QSet<QString> seenAssets;
    const auto appendAsset = [&assets, &seenAssets, &portablePath](const Track& clip) {
        if (clip.path.isEmpty() || seenAssets.contains(clip.path)) {
            return;
        }
        seenAssets.insert(clip.path);
        assets.append(QJsonObject{{QStringLiteral("id"), clip.path},
                                  {QStringLiteral("path"), portablePath(clip.path)}});
    };
    for (const Track& clip : tracks_) {
        appendAsset(clip);
    }
    for (const Track& clip : extraClips_) {
        appendAsset(clip);
    }
    const QJsonObject loopRegion{{QStringLiteral("enabled"), loopEnabled_},
                                 {QStringLiteral("startMs"), loopStartMs_},
                                 {QStringLiteral("endMs"), loopEndMs_}};
    const QJsonObject exportPreset{{QStringLiteral("scope"), pendingExportScope_},
                                   {QStringLiteral("format"), QStringLiteral("wav")},
                                   {QStringLiteral("sampleRate"), 0},
                                   {QStringLiteral("channels"), 0}};
    const QJsonObject viewState{{QStringLiteral("selectedTrack"), selectedTrack_},
                                {QStringLiteral("selectedClipId"), selectedClipId_},
                                {QStringLiteral("selectedClipIds"), selectedClips}};
    QJsonObject root{
        {QStringLiteral("version"), 2},
        {QStringLiteral("formatVersion"), 2},
        {QStringLiteral("projectName"), projectInfo.completeBaseName()},
        {QStringLiteral("sampleRate"), 48000},
        {QStringLiteral("channelLayout"), QStringLiteral("stereo")},
        {QStringLiteral("projectBpm"), targetBpm_},
        {QStringLiteral("gridDivision"), snapDivision_},
        {QStringLiteral("snap"), snapEnabled_},
        {QStringLiteral("loopRegion"), loopRegion},
        {QStringLiteral("assetReferences"), assets},
        {QStringLiteral("exportPreset"), exportPreset},
        {QStringLiteral("viewState"), viewState},
        {QStringLiteral("targetBpm"), targetBpm_},
        {QStringLiteral("snapEnabled"), snapEnabled_},
        {QStringLiteral("snapDivision"), snapDivision_},
        {QStringLiteral("timeSignature"), timeSignature_},
        {QStringLiteral("projectKey"), projectKey_},
        {QStringLiteral("loopEnabled"), loopEnabled_},
        {QStringLiteral("loopStartMs"), loopStartMs_},
        {QStringLiteral("loopEndMs"), loopEndMs_},
        {QStringLiteral("rippleEditing"), rippleEditing_},
        {QStringLiteral("autoCrossfade"), autoCrossfade_},
        {QStringLiteral("keepPitch"), keepPitch_},
        {QStringLiteral("selectedTrack"), selectedTrack_},
        {QStringLiteral("selectedClipId"), selectedClipId_},
        {QStringLiteral("selectedClipIds"), selectedClips},
        {QStringLiteral("tracks"), lanes},
        {QStringLiteral("extraClips"), extra}};
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0) {
        return false;
    }
    return file.commit();
}

bool LightEditor::saveProject(const QUrl& url)
{
    QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (path.isEmpty()) {
        return false;
    }
    if (QFileInfo(path).suffix().isEmpty()) {
        path += QStringLiteral(".agproj");
    }
    if (!writeProject(path)) {
        return false;
    }
    autosaveTimer_.stop();
    projectPath_ = path;
    projectDirty_ = false;
    QFile::remove(path + QStringLiteral(".autosave"));
    emit projectStateChanged();
    return true;
}

bool LightEditor::autosaveNow()
{
    if (!projectDirty_ || projectPath_.isEmpty()
        || busy_.load(std::memory_order_acquire)) {
        return false;
    }
    const QString path = projectPath_ + QStringLiteral(".autosave");
    if (!writeProject(path)) {
        emit errorOccurred(tr("无法自动保存工程"));
        return false;
    }
    emit autosaveWritten(path);
    return true;
}

bool LightEditor::loadProject(const QUrl& url)
{
    if (busy_.load(std::memory_order_acquire)) {
        return false;
    }
    const QString requestedPath =
        url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (requestedPath.isEmpty()) {
        return false;
    }
    autosaveTimer_.stop();
    QScopedValueRollback<bool> loadingGuard(loadingProject_, true);

    QString sourcePath = requestedPath;
    const QFileInfo autosaveInfo(requestedPath + QStringLiteral(".autosave"));
    if (autosaveInfo.exists()) {
        sourcePath = autosaveInfo.absoluteFilePath();
    }

    auto readDocument = [](const QString& path, QJsonDocument* result) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }
        QJsonParseError error{};
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            return false;
        }
        *result = document;
        return true;
    };
    QJsonDocument document;
    if (!readDocument(sourcePath, &document)) {
        if (sourcePath == requestedPath || !readDocument(requestedPath, &document)) {
            return false;
        }
        sourcePath = requestedPath;
    }
    if (!document.isObject()) {
        return false;
    }
    const QJsonObject root = document.object();
    const int projectVersion = root.value(QStringLiteral("formatVersion")).toInt(
        root.value(QStringLiteral("version")).toInt());
    if (projectVersion < 1 || projectVersion > 2) {
        return false;
    }

    const QDir projectDirectory(QFileInfo(requestedPath).absolutePath());
    const auto resolveProjectPath = [&projectDirectory](const QString& storedPath) {
        if (storedPath.isEmpty() || QFileInfo(storedPath).isAbsolute()) {
            return storedPath;
        }
        return QDir::cleanPath(projectDirectory.absoluteFilePath(storedPath));
    };

    auto restoreClip = [this, &resolveProjectPath](const QJsonObject& object,
                                                    int fallbackLane) {
        const int lane = std::clamp(
            object.value(QStringLiteral("trackIndex")).toInt(fallbackLane),
            0, kTrackCount - 1);
        Track track = makeClip(resolveProjectPath(
                                   object.value(QStringLiteral("path")).toString()),
                               lane);
        track.clipId = object.value(QStringLiteral("clipId")).toString(track.clipId);
        track.renderPath = resolveProjectPath(
            object.value(QStringLiteral("renderPath")).toString());
        track.name = object.value(QStringLiteral("name")).toString(track.name);
        track.timelineStartMs = object.value(QStringLiteral("timelineStartMs")).toInteger();
        track.inMs = object.value(QStringLiteral("inMs")).toInteger();
        track.outMs = object.value(QStringLiteral("outMs")).toInteger(track.outMs);
        const QString restoredLoopMode =
            object.value(QStringLiteral("loopMode")).toString(QStringLiteral("OneShot"));
        track.loopMode = restoredLoopMode == QStringLiteral("Loop")
                || restoredLoopMode == QStringLiteral("FollowProjectBPM")
            ? restoredLoopMode : QStringLiteral("OneShot");
        track.timelineDurationMs = object.value(QStringLiteral("timelineDurationMs"))
            .toInteger(std::max<qint64>(1, track.outMs - track.inMs));
        track.fadeInMs = object.value(QStringLiteral("fadeInMs")).toInt();
        track.fadeOutMs = object.value(QStringLiteral("fadeOutMs")).toInt();
        const auto restoredFadeCurve = [&object](const QString& key) {
            const QString curve = object.value(key).toString(
                QStringLiteral("EqualPower"));
            return curve == QStringLiteral("Linear")
                    || curve == QStringLiteral("Smooth")
                ? curve : QStringLiteral("EqualPower");
        };
        track.fadeInCurve = restoredFadeCurve(QStringLiteral("fadeInCurve"));
        track.fadeOutCurve = restoredFadeCurve(QStringLiteral("fadeOutCurve"));
        track.autoFadeIn = object.value(QStringLiteral("autoFadeIn")).toBool();
        track.autoFadeOut = object.value(QStringLiteral("autoFadeOut")).toBool();
        track.gain = object.value(QStringLiteral("gain")).toDouble(1.0);
        track.originalBpm = object.value(QStringLiteral("originalBpm")).toDouble();
        track.bpmConfidence = object.value(QStringLiteral("bpmConfidence")).toDouble();
        track.targetBpm = object.value(QStringLiteral("targetBpm")).toDouble();
        track.speedRatio = object.value(QStringLiteral("speedRatio")).toDouble(1.0);
        track.keepPitch = object.value(QStringLiteral("keepPitch")).toBool(true);
        track.pitchSemitones = std::clamp(
            object.value(QStringLiteral("pitchSemitones")).toDouble(),
            -12.0, 12.0);
        track.finePitchCents = std::clamp(
            object.value(QStringLiteral("finePitchCents")).toDouble(),
            -100.0, 100.0);
        track.formantMode = std::clamp(
            object.value(QStringLiteral("formantMode")).toInt(), 0, 2);
        track.transientProtection = std::clamp(
            object.value(QStringLiteral("transientProtection")).toDouble(),
            0.0, 1.0);
        track.highQuality = object.value(QStringLiteral("highQuality")).toBool();
        track.muted = object.value(QStringLiteral("muted")).toBool();
        track.solo = object.value(QStringLiteral("solo")).toBool();
        track.locked = object.value(QStringLiteral("locked")).toBool();
        track.aligned = object.value(QStringLiteral("aligned")).toBool();
        track.trackName = object.value(QStringLiteral("trackName")).toString(
            QStringLiteral("Track %1").arg(lane + 1));
        track.color = object.value(QStringLiteral("color")).toString(
            defaultTrackColor(lane)).toUpper();
        track.volume = std::clamp(
            object.value(QStringLiteral("volume")).toDouble(1.0), 0.0, 2.0);
        track.pan = std::clamp(
            object.value(QStringLiteral("pan")).toDouble(), -1.0, 1.0);
        track.collapsed = object.value(QStringLiteral("collapsed")).toBool();
        return track;
    };

    pushUndoState();
    tracks_.assign(kTrackCount, Track{});
    for (int index = 0; index < kTrackCount; ++index) {
        tracks_[index].laneIndex = index;
        tracks_[index].trackName = QStringLiteral("Track %1").arg(index + 1);
        tracks_[index].color = defaultTrackColor(index);
    }
    const QJsonArray lanes = root.value(QStringLiteral("tracks")).toArray();
    const int laneCount = std::min(kTrackCount, static_cast<int>(lanes.size()));
    for (int index = 0; index < laneCount; ++index) {
        tracks_[index] = restoreClip(lanes.at(index).toObject(), index);
        tracks_[index].laneIndex = index;
    }
    extraClips_.clear();
    const QJsonArray extras = root.value(QStringLiteral("extraClips")).toArray();
    extraClips_.reserve(static_cast<std::size_t>(extras.size()));
    for (const QJsonValue& value : extras) {
        extraClips_.push_back(restoreClip(value.toObject(), 0));
    }
    targetBpm_ = std::clamp(
        root.value(QStringLiteral("projectBpm")).toDouble(
            root.value(QStringLiteral("targetBpm")).toDouble(128.0)),
        40.0, 300.0);
    snapEnabled_ = root.value(QStringLiteral("snap")).toBool(
        root.value(QStringLiteral("snapEnabled")).toBool(true));
    setSnapDivision(root.value(QStringLiteral("gridDivision")).toInt(
        root.value(QStringLiteral("snapDivision")).toInt(4)));
    setTimeSignature(
        root.value(QStringLiteral("timeSignature")).toString(QStringLiteral("4/4")));
    setProjectKey(
        root.value(QStringLiteral("projectKey")).toString(QStringLiteral("C")));
    const QJsonObject loopRegion = root.value(QStringLiteral("loopRegion")).toObject();
    setLoopEnabled(loopRegion.value(QStringLiteral("enabled")).toBool(
        root.value(QStringLiteral("loopEnabled")).toBool()));
    loopStartMs_ = std::max<qint64>(
        0, loopRegion.value(QStringLiteral("startMs")).toInteger(
               root.value(QStringLiteral("loopStartMs")).toInteger()));
    loopEndMs_ = std::max<qint64>(
        0, loopRegion.value(QStringLiteral("endMs")).toInteger(
               root.value(QStringLiteral("loopEndMs")).toInteger()));
    if (loopEndMs_ > 0 && loopEndMs_ <= loopStartMs_) {
        loopEndMs_ = loopStartMs_ + 1;
    }
    setRippleEditing(root.value(QStringLiteral("rippleEditing")).toBool());
    autoCrossfade_ = root.value(QStringLiteral("autoCrossfade")).toBool(true);
    keepPitch_ = root.value(QStringLiteral("keepPitch")).toBool(true);
    const QJsonObject viewState = root.value(QStringLiteral("viewState")).toObject();
    selectedTrack_ = std::clamp(viewState.value(QStringLiteral("selectedTrack")).toInt(
                                       root.value(QStringLiteral("selectedTrack")).toInt()),
                                  0, kTrackCount - 1);
    selectedClipId_ = viewState.value(QStringLiteral("selectedClipId")).toString(
        root.value(QStringLiteral("selectedClipId")).toString());
    selectedClipIds_.clear();
    for (const QJsonValue& value
         : (viewState.contains(QStringLiteral("selectedClipIds"))
                ? viewState.value(QStringLiteral("selectedClipIds")).toArray()
                : root.value(QStringLiteral("selectedClipIds")).toArray())) {
        const QString clipId = value.toString();
        if (!clipId.isEmpty() && findClip(clipId) != nullptr
            && !selectedClipIds_.contains(clipId)) {
            selectedClipIds_.append(clipId);
        }
    }
    if (selectedClipIds_.isEmpty() && findClip(selectedClipId_) != nullptr) {
        selectedClipIds_.append(selectedClipId_);
    } else if (!selectedClipIds_.isEmpty()
               && !selectedClipIds_.contains(selectedClipId_)) {
        selectedClipId_ = selectedClipIds_.constFirst();
    }
    for (Track& clip : tracks_) {
        if (!clip.path.isEmpty() && QFileInfo::exists(clip.path)) {
            requestWaveformAnalysis(clip.clipId, clip.path);
        }
    }
    for (Track& clip : extraClips_) {
        if (!clip.path.isEmpty() && QFileInfo::exists(clip.path)) {
            requestWaveformAnalysis(clip.clipId, clip.path);
        }
    }
    emitEditorStateChanged();
    emit targetBpmChanged();
    emit snapEnabledChanged();
    emit keepPitchChanged();
    projectPath_ = requestedPath;
    projectDirty_ = sourcePath != requestedPath;
    emit projectStateChanged();
    if (projectDirty_) {
        autosaveTimer_.start();
        emit autosaveRecovered(sourcePath);
    }
    return true;
}

bool LightEditor::setTrackTargetBpm(int trackIndex, double value)
{
    if (!isValidTrackIndex(trackIndex) || tracks_[trackIndex].path.isEmpty()) {
        return false;
    }
    return setClipTargetBpmById(tracks_[trackIndex].clipId, value);
}

bool LightEditor::setTrackKeepPitch(int trackIndex, bool value)
{
    if (!isValidTrackIndex(trackIndex) || tracks_[trackIndex].path.isEmpty()) {
        return false;
    }
    return setClipKeepPitchById(tracks_[trackIndex].clipId, value);
}

bool LightEditor::setTrackBeatAligned(int trackIndex, bool value)
{
    if (!isValidTrackIndex(trackIndex) || tracks_[trackIndex].path.isEmpty()) {
        return false;
    }
    return setClipBeatAlignedById(tracks_[trackIndex].clipId, value);
}

void LightEditor::undo()
{
    if (undoStack_.empty()) {
        return;
    }
    redoStack_.push_back(
        EditorState{tracks_, extraClips_, selectedTrack_, selectedClipId_,
                    selectedClipIds_});
    EditorState previous = std::move(undoStack_.back());
    undoStack_.pop_back();
    restoreState(std::move(previous));
}

void LightEditor::redo()
{
    if (redoStack_.empty()) {
        return;
    }
    undoStack_.push_back(
        EditorState{tracks_, extraClips_, selectedTrack_, selectedClipId_,
                    selectedClipIds_});
    EditorState next = std::move(redoStack_.back());
    redoStack_.pop_back();
    restoreState(std::move(next));
}
