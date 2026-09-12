#include "audio_visual_feature_controller.hpp"

#include "playback_controller.hpp"

#include <agplayer/c_api.h>

#include <algorithm>
#include <cmath>

namespace {

double normalizedLevel(const double value) noexcept
{
    return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.0;
}

double smoothedLevel(const double current, const double target) noexcept
{
    if (target >= current) return target;
    constexpr double DecayRetention = 0.82;
    const double next = current * DecayRetention
        + target * (1.0 - DecayRetention);
    return next < 0.001 ? 0.0 : next;
}

} // namespace

AudioVisualFeatureController::AudioVisualFeatureController(
    PlaybackController* playback, QObject* parent)
    : QObject(parent), playback_(playback)
{
    outputLevelTimer_.setInterval(PlaybackController::PollIntervalMs);
    outputLevelTimer_.setTimerType(Qt::PreciseTimer);
    connect(&outputLevelTimer_, &QTimer::timeout,
            this, &AudioVisualFeatureController::pollOutputLevels);
}

bool AudioVisualFeatureController::active() const noexcept { return active_; }
AudioVisualFeatureController::~AudioVisualFeatureController()
{
    setVisualPcmEnabled(false);
}

void AudioVisualFeatureController::setVisualPcmEnabled(bool enabled)
{
    if (playback_ && playback_->playerHandle())
        ag_player_set_visual_pcm_enabled(playback_->playerHandle(), enabled ? 1 : 0);
}

void AudioVisualFeatureController::resetVisualPcm()
{
    ++visualPcmEpoch_;
    const bool hadPcm = visualPcmSize_ != 0;
    visualAnalyzer_.reset();
    visualFeatureAnalyzer_.reset();
    visualKickResponse_.reset();
    visualFeatures_ = {};
    visualKick_ = {};
    visualSamplesSinceUpdate_ = 0;
    visualFramePhase_ = 0;
    visualAnalysisTimer_.invalidate();
    visualPcm_.fill(0.0f);
    visualPcmFrames_ = {};
    visualPcmFrameCount_ = 0;
    visualPcmFrameWriteIndex_ = 0;
    visualSpectrum_.fill(0);
    visualPcmSize_ = 0;
    visualSampleRate_ = 0;
    visualGeneration_ = 0;
    visualNextIndex_ = 0;
    // Separate from a completed FFT: consumers clear cached output without
    // treating activation or repeated empty resets as new audio frames.
    if (hadPcm) emit visualStateReset();
}

void AudioVisualFeatureController::setVisualKickSensitivity(int sensitivity)
{
    sensitivity = std::clamp(sensitivity, 0, 100);
    if (visualKickSensitivity_ == sensitivity) return;
    visualKickSensitivity_ = sensitivity;
    emit visualKickSensitivityChanged();
}

void AudioVisualFeatureController::ingestVisualPcm(const ag_visual_pcm_snapshot& pcm,
                                                bool deferAnalysis)
{
    if (!active_) return;
    // Empty reads carry the epoch but no sample rate or sample index.
    if (pcm.sample_count == 0) {
        if (pcm.generation != visualGeneration_) {
            resetVisualPcm();
            visualGeneration_ = pcm.generation;
        }
        return;
    }
    if (pcm.sample_count > visualPcm_.size() || pcm.sample_rate <= 0) {
        resetVisualPcm();
        return;
    }
    if (pcm.generation != visualGeneration_ || pcm.sample_rate != visualSampleRate_
        || pcm.first_sample_index != visualNextIndex_) {
        resetVisualPcm();
    }
    visualGeneration_ = pcm.generation;
    visualSampleRate_ = pcm.sample_rate;
    visualSamplesSinceUpdate_ += pcm.sample_count;

    const auto enqueueFrame = [&](std::uint64_t endSampleIndex) {
        agplayer::VisualAudioFrameAnalyzer::Snapshot frame;
        frame.pcm = visualPcm_;
        frame.sampleRate = visualSampleRate_;
        frame.epoch = visualPcmEpoch_;
        frame.firstSampleIndex = endSampleIndex - visualPcm_.size();
        frame.sequence = ++visualPcmFrameSequence_;
        frame.valid = true;
        visualPcmFrames_[visualPcmFrameWriteIndex_] = frame;
        visualPcmFrameWriteIndex_ = (visualPcmFrameWriteIndex_ + 1)
            % visualPcmFrames_.size();
        visualPcmFrameCount_ = std::min(visualPcmFrameCount_ + 1,
                                        visualPcmFrames_.size());
    };

    std::size_t sourceOffset = 0;
    if (visualPcmSize_ < visualPcm_.size()) {
        const auto copied = std::min<std::size_t>(
            pcm.sample_count, visualPcm_.size() - visualPcmSize_);
        std::copy_n(pcm.samples, copied, visualPcm_.begin() + visualPcmSize_);
        visualPcmSize_ += copied;
        sourceOffset += copied;
        if (visualPcmSize_ == visualPcm_.size()) {
            enqueueFrame(pcm.first_sample_index + sourceOffset);
            visualFramePhase_ = 0;
        }
    }
    while (sourceOffset < pcm.sample_count) {
        const auto unitsUntilFrame = std::uint64_t(visualSampleRate_)
            - visualFramePhase_;
        const auto samplesUntilFrame = std::max<std::uint64_t>(
            1, (unitsUntilFrame + 59) / 60);
        const auto copied = std::min<std::size_t>(
            pcm.sample_count - sourceOffset,
            std::size_t(samplesUntilFrame));
        for (std::size_t index = 0; index + copied < visualPcm_.size(); ++index)
            visualPcm_[index] = visualPcm_[index + copied];
        std::copy_n(pcm.samples + sourceOffset, copied,
                    visualPcm_.end() - copied);
        sourceOffset += copied;
        visualFramePhase_ += std::uint64_t(copied) * 60;
        if (visualFramePhase_ >= std::uint64_t(visualSampleRate_)) {
            enqueueFrame(pcm.first_sample_index + sourceOffset);
            visualFramePhase_ -= std::uint64_t(visualSampleRate_);
        }
    }
    visualNextIndex_ = pcm.first_sample_index + pcm.sample_count;
    if (!deferAnalysis) analyzeVisualPcm();
}

agplayer::VisualAudioFrameAnalyzer::Snapshot
AudioVisualFeatureController::visualPcmSnapshot() const noexcept
{
    constexpr qint64 VisualReleaseMilliseconds = 1600;
    const bool releasing = visualPaused_ && visualReleaseTimer_.isValid()
        && visualReleaseTimer_.elapsed() < VisualReleaseMilliseconds;
    agplayer::VisualAudioFrameAnalyzer::Snapshot result;
    result.pcm = visualPcm_;
    result.sampleRate = visualSampleRate_;
    result.epoch = visualPcmEpoch_;
    result.firstSampleIndex = visualPcmSize_ == visualPcm_.size()
        ? visualNextIndex_ - visualPcm_.size() : 0;
    result.sequence = visualPcmFrameSequence_;
    result.valid = active_ && !visualPaused_
        && visualPcmSize_ == visualPcm_.size() && visualSampleRate_ > 0;
    result.paused = visualPaused_;
    result.releasing = releasing;
    return result;
}

agplayer::VisualAudioFrameAnalyzer::Batch
AudioVisualFeatureController::visualPcmBatch() const noexcept
{
    agplayer::VisualAudioFrameAnalyzer::Batch result;
    result.count = visualPcmFrameCount_;
    const std::size_t oldest = (visualPcmFrameWriteIndex_
        + visualPcmFrames_.size() - visualPcmFrameCount_)
        % visualPcmFrames_.size();
    for (std::size_t index = 0; index < result.count; ++index)
        result.frames[index] = visualPcmFrames_[
            (oldest + index) % visualPcmFrames_.size()];
    return result;
}

void AudioVisualFeatureController::updateVisualPlaybackState()
{
    bool paused = false;
    if (active_ && playback_ && playback_->playerHandle()) {
        ag_playback_snapshot snapshot{};
        paused = ag_player_snapshot(playback_->playerHandle(), &snapshot) == AG_OK
            && snapshot.state != AG_PLAYING;
    }
    if (paused == visualPaused_) return;
    visualPaused_ = paused;
    if (visualPaused_) visualReleaseTimer_.start();
    else visualReleaseTimer_.invalidate();
}

void AudioVisualFeatureController::acquireRenderFrameAnalysis()
{
    ++renderFrameConsumers_;
}

void AudioVisualFeatureController::releaseRenderFrameAnalysis()
{
    if (renderFrameConsumers_ == 0) return;
    if (--renderFrameConsumers_ == 0) {
        // The GUI fallback must not resume a stale pre-render analysis history.
        visualAnalyzer_.reset();
        visualFeatureAnalyzer_.reset();
        visualKickResponse_.reset();
        visualFeatures_ = {};
        visualKick_ = {};
        visualAnalysisTimer_.invalidate();
    }
}

void AudioVisualFeatureController::analyzeVisualPcm()
{
    if (renderFrameConsumers_ == 0 && active_ && visualSamplesSinceUpdate_ != 0
        && visualPcmSize_ == visualPcm_.size()) {
        visualSpectrum_ = visualAnalyzer_.process(visualPcm_);
        // Analysis advances once per GUI poll, independently of PCM batching.
        // This is a polling cadence, not the renderer's RAF-equivalent clock.
        const double dt = visualAnalysisTimer_.isValid()
            ? std::clamp(double(visualAnalysisTimer_.nsecsElapsed()) / 1e9, 0.0, .25)
            : 1.0 / 60.0;
        visualAnalysisTimer_.start();
        visualFeatures_ = visualFeatureAnalyzer_.update(visualSpectrum_, true, false);
        visualKick_ = visualKickResponse_.process(visualSpectrum_, dt, visualKickSensitivity_);
        visualSamplesSinceUpdate_ = 0;
        ++visualSpectrumUpdateCount_;
        emit visualSpectrumReady();
    }
}
QVariantList AudioVisualFeatureController::bands() const { return bands_; }
double AudioVisualFeatureController::energy() const noexcept { return energy_; }
double AudioVisualFeatureController::spectralFlux() const noexcept { return spectralFlux_; }
bool AudioVisualFeatureController::kickPulse() const noexcept { return kickPulse_; }
bool AudioVisualFeatureController::snarePulse() const noexcept { return snarePulse_; }
quint64 AudioVisualFeatureController::beatRevision() const noexcept
{
    return beatRevision_;
}
double AudioVisualFeatureController::beatStrength() const noexcept
{
    return beatStrength_;
}
quint64 AudioVisualFeatureController::impactRevision() const noexcept
{
    return impactRevision_;
}
double AudioVisualFeatureController::impactStrength() const noexcept
{
    return impactStrength_;
}
bool AudioVisualFeatureController::beatReliable() const noexcept
{
    return beatReliable_;
}
quint64 AudioVisualFeatureController::derivedUpdateCount() const noexcept
{
    return derivedUpdateCount_;
}
double AudioVisualFeatureController::leftPeak() const noexcept { return leftPeak_; }
double AudioVisualFeatureController::rightPeak() const noexcept { return rightPeak_; }
double AudioVisualFeatureController::leftRms() const noexcept { return leftRms_; }
double AudioVisualFeatureController::rightRms() const noexcept { return rightRms_; }

void AudioVisualFeatureController::setPlaybackController(PlaybackController* playback)
{
    if (playback_ == playback) return;
    setVisualPcmEnabled(false);
    resetVisualPcm();
    disconnectPlaybackSignals();
    resetOutputLevels();
    visualPaused_ = false;
    visualReleaseTimer_.invalidate();
    playback_ = playback;
    resetBeatPosition();
    if (active_) connectPlaybackSignals();
    setVisualPcmEnabled(active_);
    updateOutputLevelPolling();
}

void AudioVisualFeatureController::setActive(bool active)
{
    if (active_ == active) return;
    active_ = active;
    setVisualPcmEnabled(active_);
    resetVisualPcm();
    if (active_) {
        previousSpectrum_.clear();
        resetTransientHistory();
        resetBandEnvelopes();
        connectPlaybackSignals();
    } else {
        disconnectPlaybackSignals();
        resetOutputLevels();
        visualPaused_ = false;
        visualReleaseTimer_.invalidate();
    }
    updateOutputLevelPolling();
    emit activeChanged();
}

void AudioVisualFeatureController::setWaveformTiming(
    const QString& trackId, double bpm, qint64 durationMs,
    const QVariantList& mixPeaks)
{
    bool hasAudiblePeak = false;
    for (const QVariant& peak : mixPeaks) {
        bool ok = false;
        const double value = peak.toDouble(&ok);
        if (ok && std::isfinite(value) && std::abs(value) > 1e-4) {
            hasAudiblePeak = true;
            break;
        }
    }
    const bool matchesPlayback = playback_ == nullptr
        || playback_->currentTrackId().isEmpty()
        || playback_->currentTrackId() == trackId;
    const bool reliable = !trackId.isEmpty() && std::isfinite(bpm)
        && bpm >= 40.0 && bpm <= 300.0 && durationMs > 0
        && hasAudiblePeak && matchesPlayback;
    const bool reliabilityChanged = beatReliable_ != reliable;
    timingTrackId_ = trackId;
    bpm_ = reliable ? bpm : 0.0;
    durationMs_ = reliable ? durationMs : 0;
    beatReliable_ = reliable;
    fallbackDebounce_.invalidate();
    previousSpectrum_.clear();
    resetTransientHistory();
    resetBandEnvelopes();
    resetBeatPosition();
    if (reliabilityChanged) emit beatReliableChanged();
}

void AudioVisualFeatureController::processPlaybackPosition(qint64 positionMs)
{
    if (!active_ || !beatReliable_ || positionMs < 0
        || positionMs > durationMs_) {
        return;
    }

    const double beatMs = 60000.0 / bpm_;
    const qint64 beat = static_cast<qint64>(std::floor(
        (double(positionMs) - beatPhaseOffsetMs_) / beatMs));
    const qint64 group = static_cast<qint64>(std::floor(double(beat) / 8.0));
    if (lastPositionMs_ < 0) {
        lastPositionMs_ = positionMs;
        lastBeatIndex_ = beat;
        lastImpactGroup_ = group;
        return;
    }

    const qint64 delta = positionMs - lastPositionMs_;
    constexpr qint64 SeekThresholdMs = 750;
    if (delta < 0 || delta > SeekThresholdMs) {
        lastPositionMs_ = positionMs;
        lastBeatIndex_ = beat;
        lastBeatEventPositionMs_ = -1;
        lastImpactGroup_ = group;
        return;
    }

    const bool majorImpact = group > 0 && group > lastImpactGroup_;
    constexpr qint64 BeatDeduplicationMs = 160;
    if (beat > lastBeatIndex_
        && (lastBeatEventPositionMs_ < 0
            || positionMs - lastBeatEventPositionMs_ >= BeatDeduplicationMs)) {
        triggerBeat(audibleSpectrum_ ? std::clamp(0.42 + energy_ * 0.34, 0.0, 0.76) : 0.0,
                    !majorImpact);
        lastBeatEventPositionMs_ = positionMs;
    }
    if (majorImpact) {
        triggerImpact(audibleSpectrum_ ? std::clamp(0.68 + energy_ * 0.32, 0.0, 1.0) : 0.0);
    }
    lastPositionMs_ = positionMs;
    lastBeatIndex_ = beat;
    lastImpactGroup_ = group;
}

void AudioVisualFeatureController::processSpectrum(const QVariantList& spectrum)
{
    if (!active_ || spectrum.size() != 128) return;

    constexpr std::array<int, 9> bandEdges{0, 3, 7, 13, 22,
                                           36, 56, 84, 128};
    constexpr std::array<double, 8> energyWeights{
        0.24, 0.19, 0.15, 0.13, 0.11, 0.08, 0.06, 0.04};
    // Match the kick flux to the same two bass bands used by its energy gate.
    // Including midrange bins both diluted narrow kicks and let mid notes
    // retrigger the kick while bass was merely sustaining.
    constexpr int kickBinCount = bandEdges[2];
    // PlaybackController::pollSpectrum publishes only bin changes > 0.002F.
    // Its final fade-out sample can therefore retain this much residue even
    // after the audio reaches zero. Use the same float boundary for the gate.
    constexpr double spectrumSilenceFloor = double(0.002F);
    std::array<double, 128> current{};
    double flux = 0.0;
    double lowFlux = 0.0;
    double highFlux = 0.0;
    audibleSpectrum_ = false;
    for (int index = 0; index < 128; ++index) {
        const double value = normalizedValue(spectrum.at(index));
        // Gate only the visual strength, not the BPM grid or eight-beat count.
        // Do not mistake the smoothed envelope's release tail for current audio.
        audibleSpectrum_ = audibleSpectrum_
            || (std::isfinite(value) && value > spectrumSilenceFloor);
        current[std::size_t(index)] = value;
        const double previous = previousSpectrum_.size() == 128
            ? normalizedValue(previousSpectrum_.at(index)) : 0.0;
        const double delta = std::max(0.0, value - previous);
        flux += delta;
        if (index < kickBinCount) lowFlux += delta;
        if (index >= 36 && index < 96) highFlux += delta;
    }

    QVariantList nextBands;
    nextBands.reserve(8);
    double weightedEnergy = 0.0;
    for (int band = 0; band < 8; ++band) {
        double sumSquares = 0.0;
        for (int index = bandEdges[std::size_t(band)];
             index < bandEdges[std::size_t(band + 1)]; ++index) {
            const double sample = current[std::size_t(index)];
            sumSquares += sample * sample;
        }
        const double target = std::sqrt(
            sumSquares / double(bandEdges[std::size_t(band + 1)]
                                - bandEdges[std::size_t(band)]));
        if (!bandsInitialized_) {
            smoothedBands_[std::size_t(band)] = target;
        } else {
            const double previous = smoothedBands_[std::size_t(band)];
            const double coefficient = target > previous ? 0.78 : 0.30;
            smoothedBands_[std::size_t(band)] = previous
                + (target - previous) * coefficient;
        }
        nextBands.append(smoothedBands_[std::size_t(band)]);
        weightedEnergy += smoothedBands_[std::size_t(band)]
            * energyWeights[std::size_t(band)];
    }
    bandsInitialized_ = true;

    bands_ = std::move(nextBands);
    energy_ = std::clamp(weightedEnergy, 0.0, 1.0);
    spectralFlux_ = flux / 128.0;
    const double normalizedLowFlux = lowFlux / double(kickBinCount);
    const double normalizedHighFlux = highFlux / 60.0;
    // The user-facing rhythm sensitivity must affect the spectrum path too,
    // not only the PCM analyzer. A higher setting lowers both onset and energy
    // gates while the adaptive history continues to reject a steady bass bed.
    const double sensitivity = double(visualKickSensitivity_) / 100.0;
    const double sensitivityScale = 1.5 - sensitivity;
    const double kickThreshold = adaptiveThreshold(
        lowFluxHistory_, transientSampleCount_, 0.05 * sensitivityScale);
    const double snareThreshold = adaptiveThreshold(
        highFluxHistory_, transientSampleCount_, 0.04 * sensitivityScale);
    kickPulse_ = normalizedLowFlux >= kickThreshold
        && bands_.at(0).toDouble() + bands_.at(1).toDouble()
               >= 0.20 * sensitivityScale;
    snarePulse_ = normalizedHighFlux >= snareThreshold
        && bands_.at(5).toDouble() + bands_.at(6).toDouble()
               + bands_.at(7).toDouble() >= 0.20 * sensitivityScale;
    appendTransientSample(lowFluxHistory_, transientSampleCount_,
                          transientWriteIndex_, normalizedLowFlux);
    const int highWriteIndex = (transientWriteIndex_ + 23) % 24;
    highFluxHistory_[std::size_t(highWriteIndex)] = normalizedHighFlux;
    previousSpectrum_ = spectrum;
    ++derivedUpdateCount_;
    constexpr qint64 BeatDeduplicationMs = 160;
    constexpr qint64 FallbackDebounceMs = 180;
    const bool transient = kickPulse_ || snarePulse_;
    const bool reliableTransient = beatReliable_ && transient
        && lastPositionMs_ >= 0
        && (lastBeatEventPositionMs_ < 0
            || lastPositionMs_ - lastBeatEventPositionMs_ >= BeatDeduplicationMs);
    const bool fallbackTransient = !beatReliable_ && transient
        && (!fallbackDebounce_.isValid()
            || fallbackDebounce_.elapsed() >= FallbackDebounceMs);
    if (reliableTransient || fallbackTransient) {
        const double strength = std::clamp(0.42 + energy_ * 0.38
                                               + (kickPulse_ ? 0.10 : 0.0),
                                           0.0, 0.82);
        triggerBeat(strength, false);
        if (reliableTransient) {
            lastBeatEventPositionMs_ = lastPositionMs_;
            if (!beatPhaseLocked_) {
                const double beatMs = 60000.0 / bpm_;
                beatPhaseOffsetMs_ = std::fmod(double(lastPositionMs_), beatMs);
                lastBeatIndex_ = static_cast<qint64>(std::floor(
                    (double(lastPositionMs_) - beatPhaseOffsetMs_) / beatMs));
                lastImpactGroup_ = static_cast<qint64>(
                    std::floor(double(lastBeatIndex_) / 8.0));
                beatPhaseLocked_ = true;
            }
        } else {
            ++fallbackBeatCount_;
            if (fallbackBeatCount_ % 8 == 0) {
                triggerImpact(std::clamp(0.68 + energy_ * 0.32, 0.0, 1.0),
                              false);
            }
            fallbackDebounce_.restart();
        }
    }
    emit featuresChanged();
    emit derivedUpdateCountChanged();
}

void AudioVisualFeatureController::connectPlaybackSignals()
{
    if (playback_ == nullptr) return;
    if (!playbackDestroyedConnection_) {
        playbackDestroyedConnection_ = connect(
            playback_, &PlaybackController::aboutToBeDestroyed, this, [this] {
                setVisualPcmEnabled(false);
                spectrumConnection_ = {};
                positionConnection_ = {};
                trackConnection_ = {};
                playbackDestroyedConnection_ = {};
                playback_ = nullptr;
                resetVisualPcm();
                updateOutputLevelPolling();
            });
    }
    if (!spectrumConnection_) {
        spectrumConnection_ = connect(
            playback_, &PlaybackController::spectrumChanged, this, [this] {
                if (playback_ != nullptr) processSpectrum(playback_->spectrum());
            });
    }
    if (!positionConnection_) {
        positionConnection_ = connect(
            playback_, &PlaybackController::positionMsChanged, this, [this] {
                if (playback_ != nullptr) {
                    processPlaybackPosition(playback_->positionMs());
                }
            });
    }
    if (!trackConnection_) {
        trackConnection_ = connect(
            playback_, &PlaybackController::currentTrackIdChanged, this, [this] {
                resetBeatPosition();
                if (playback_ != nullptr
                    && playback_->currentTrackId() != timingTrackId_
                    && beatReliable_) {
                    beatReliable_ = false;
                    emit beatReliableChanged();
                }
            });
    }
    processPlaybackPosition(playback_->positionMs());
}

void AudioVisualFeatureController::disconnectPlaybackSignals()
{
    if (spectrumConnection_) {
        disconnect(spectrumConnection_);
        spectrumConnection_ = {};
    }
    if (positionConnection_) {
        disconnect(positionConnection_);
        positionConnection_ = {};
    }
    if (trackConnection_) {
        disconnect(trackConnection_);
        trackConnection_ = {};
    }
    if (playbackDestroyedConnection_) {
        disconnect(playbackDestroyedConnection_);
        playbackDestroyedConnection_ = {};
    }
}

void AudioVisualFeatureController::updateOutputLevelPolling()
{
    if (active_ && playback_ != nullptr) {
        if (!outputLevelTimer_.isActive()) outputLevelTimer_.start();
        pollOutputLevels();
        return;
    }
    outputLevelTimer_.stop();
    if (active_) resetOutputLevels();
}

void AudioVisualFeatureController::pollOutputLevels()
{
    if (active_ && playback_ && playback_->playerHandle()) {
        updateVisualPlaybackState();
        if (!visualPaused_) {
            // Drain the finite tap backlog after GUI scheduling delays. Each
            // contiguous chunk is retained in order; never stitch over
            // a seek/overflow epoch or manufacture missing audio. The cap
            // covers the tap's 32 x 512 samples without an unbounded catch-up.
            constexpr int MaxVisualReadsPerPoll = 16;
            for (int read = 0; read < MaxVisualReadsPerPoll; ++read) {
                if (!active_ || !playback_ || !playback_->playerHandle()) break;
                ag_visual_pcm_snapshot pcm{};
                if (ag_player_read_visual_pcm(playback_->playerHandle(), &pcm)
                    != AG_OK) break;
                ingestVisualPcm(pcm, true);
                if (pcm.sample_count == 0) break;
            }
            analyzeVisualPcm();
        }
    }
    ag_output_levels levels{};
    if (active_ && playback_ != nullptr
        && playback_->playerHandle() != nullptr
        && ag_player_output_levels(playback_->playerHandle(), &levels)
            == AG_OK) {
        applyOutputLevels(levels.left_peak, levels.right_peak,
                          levels.left_rms, levels.right_rms);
        return;
    }
    applyOutputLevels(0.0, 0.0, 0.0, 0.0);
}

void AudioVisualFeatureController::applyOutputLevels(
    const double leftPeak, const double rightPeak,
    const double leftRms, const double rightRms)
{
    const double nextLeftPeak = smoothedLevel(
        leftPeak_, normalizedLevel(leftPeak));
    const double nextRightPeak = smoothedLevel(
        rightPeak_, normalizedLevel(rightPeak));
    const double nextLeftRms = smoothedLevel(
        leftRms_, normalizedLevel(leftRms));
    const double nextRightRms = smoothedLevel(
        rightRms_, normalizedLevel(rightRms));
    if (nextLeftPeak == leftPeak_ && nextRightPeak == rightPeak_
        && nextLeftRms == leftRms_ && nextRightRms == rightRms_) {
        return;
    }
    leftPeak_ = nextLeftPeak;
    rightPeak_ = nextRightPeak;
    leftRms_ = nextLeftRms;
    rightRms_ = nextRightRms;
    emit outputLevelsChanged();
}

void AudioVisualFeatureController::resetOutputLevels()
{
    if (leftPeak_ == 0.0 && rightPeak_ == 0.0
        && leftRms_ == 0.0 && rightRms_ == 0.0) {
        return;
    }
    leftPeak_ = 0.0;
    rightPeak_ = 0.0;
    leftRms_ = 0.0;
    rightRms_ = 0.0;
    emit outputLevelsChanged();
}

void AudioVisualFeatureController::resetBeatPosition() noexcept
{
    audibleSpectrum_ = false;
    lastPositionMs_ = -1;
    lastBeatIndex_ = -1;
    lastBeatEventPositionMs_ = -1;
    beatPhaseOffsetMs_ = 0.0;
    beatPhaseLocked_ = false;
    lastImpactGroup_ = -1;
    fallbackBeatCount_ = 0;
}

void AudioVisualFeatureController::resetTransientHistory() noexcept
{
    lowFluxHistory_.fill(0.0);
    highFluxHistory_.fill(0.0);
    transientSampleCount_ = 0;
    transientWriteIndex_ = 0;
}

void AudioVisualFeatureController::resetBandEnvelopes() noexcept
{
    smoothedBands_.fill(0.0);
    bandsInitialized_ = false;
}

double AudioVisualFeatureController::adaptiveThreshold(
    const std::array<double, 24>& history, const int sampleCount,
    const double minimum) noexcept
{
    const int count = std::clamp(sampleCount, 0, int(history.size()));
    if (count < 3) return minimum;
    double sum = 0.0;
    for (int index = 0; index < count; ++index) {
        sum += history[std::size_t(index)];
    }
    const double mean = sum / double(count);
    double variance = 0.0;
    for (int index = 0; index < count; ++index) {
        const double delta = history[std::size_t(index)] - mean;
        variance += delta * delta;
    }
    const double deviation = std::sqrt(variance / double(count));
    return std::clamp(mean + deviation * 1.20 + 0.008,
                      minimum, minimum * 3.20);
}

void AudioVisualFeatureController::appendTransientSample(
    std::array<double, 24>& history, int& sampleCount, int& writeIndex,
    const double value) noexcept
{
    history[std::size_t(writeIndex)] = std::clamp(value, 0.0, 1.0);
    writeIndex = (writeIndex + 1) % int(history.size());
    sampleCount = std::min(sampleCount + 1, int(history.size()));
}

void AudioVisualFeatureController::triggerImpact(double strength, bool notify)
{
    impactStrength_ = std::clamp(strength, 0.0, 1.0);
    ++impactRevision_;
    if (notify) emit featuresChanged();
}

void AudioVisualFeatureController::triggerBeat(double strength, bool notify)
{
    beatStrength_ = std::clamp(strength, 0.0, 1.0);
    ++beatRevision_;
    if (notify) emit featuresChanged();
}

double AudioVisualFeatureController::normalizedValue(const QVariant& value) noexcept
{
    bool ok = false;
    const double parsed = value.toDouble(&ok);
    return ok ? std::clamp(parsed, 0.0, 1.0) : 0.0;
}
