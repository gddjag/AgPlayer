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
QVariantList AudioVisualFeatureController::bands() const { return bands_; }
double AudioVisualFeatureController::energy() const noexcept { return energy_; }
double AudioVisualFeatureController::spectralFlux() const noexcept { return spectralFlux_; }
bool AudioVisualFeatureController::kickPulse() const noexcept { return kickPulse_; }
bool AudioVisualFeatureController::snarePulse() const noexcept { return snarePulse_; }
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
    disconnectPlaybackSignals();
    resetOutputLevels();
    playback_ = playback;
    resetBeatPosition();
    if (active_) connectPlaybackSignals();
    updateOutputLevelPolling();
}

void AudioVisualFeatureController::setActive(bool active)
{
    if (active_ == active) return;
    active_ = active;
    if (active_) {
        previousSpectrum_.clear();
        connectPlaybackSignals();
    } else {
        disconnectPlaybackSignals();
        resetOutputLevels();
    }
    updateOutputLevelPolling();
    emit activeChanged();
}

void AudioVisualFeatureController::setWaveformTiming(
    const QString& trackId, double bpm, qint64 durationMs,
    const QVariantList& mixPeaks)
{
    bool hasValidPeak = false;
    for (const QVariant& peak : mixPeaks) {
        bool ok = false;
        const double value = peak.toDouble(&ok);
        if (ok && std::isfinite(value)) {
            hasValidPeak = true;
            break;
        }
    }
    const bool matchesPlayback = playback_ == nullptr
        || playback_->currentTrackId().isEmpty()
        || playback_->currentTrackId() == trackId;
    const bool reliable = !trackId.isEmpty() && std::isfinite(bpm)
        && bpm >= 40.0 && bpm <= 300.0 && durationMs > 0
        && hasValidPeak && matchesPlayback;
    const bool reliabilityChanged = beatReliable_ != reliable;
    timingTrackId_ = trackId;
    bpm_ = reliable ? bpm : 0.0;
    durationMs_ = reliable ? durationMs : 0;
    beatReliable_ = reliable;
    fallbackDebounce_.invalidate();
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
    const double groupMs = beatMs * 8.0;
    const qint64 group = static_cast<qint64>(std::floor(positionMs / groupMs));
    if (lastPositionMs_ < 0) {
        lastPositionMs_ = positionMs;
        lastImpactGroup_ = group;
        return;
    }

    const qint64 delta = positionMs - lastPositionMs_;
    constexpr qint64 SeekThresholdMs = 750;
    if (delta < 0 || delta > SeekThresholdMs) {
        lastPositionMs_ = positionMs;
        lastImpactGroup_ = group;
        return;
    }

    if (group > 0 && group > lastImpactGroup_) {
        triggerImpact(std::clamp(0.68 + energy_ * 0.32, 0.0, 1.0));
    }
    lastPositionMs_ = positionMs;
    lastImpactGroup_ = group;
}

void AudioVisualFeatureController::processSpectrum(const QVariantList& spectrum)
{
    if (!active_ || spectrum.size() != 128) return;

    QVariantList nextBands;
    nextBands.reserve(8);
    double total = 0.0;
    double flux = 0.0;
    double lowFlux = 0.0;
    double highFlux = 0.0;
    for (int band = 0; band < 8; ++band) {
        double sum = 0.0;
        for (int offset = 0; offset < 16; ++offset) {
            const int index = band * 16 + offset;
            const double value = normalizedValue(spectrum.at(index));
            sum += value;
            total += value;
            const double previous = previousSpectrum_.size() == 128
                ? normalizedValue(previousSpectrum_.at(index)) : 0.0;
            const double delta = std::max(0.0, value - previous);
            flux += delta;
            if (index < 32) lowFlux += delta;
            if (index >= 64 && index < 96) highFlux += delta;
        }
        nextBands.append(sum / 16.0);
    }

    bands_ = std::move(nextBands);
    energy_ = total / 128.0;
    spectralFlux_ = flux / 128.0;
    kickPulse_ = lowFlux / 32.0 >= 0.05
        && bands_.at(0).toDouble() + bands_.at(1).toDouble() >= 0.20;
    snarePulse_ = highFlux / 32.0 >= 0.04
        && bands_.at(4).toDouble() + bands_.at(5).toDouble() >= 0.20;
    previousSpectrum_ = spectrum;
    ++derivedUpdateCount_;
    constexpr qint64 FallbackDebounceMs = 180;
    if (!beatReliable_ && (kickPulse_ || snarePulse_)
        && (!fallbackDebounce_.isValid()
            || fallbackDebounce_.elapsed() >= FallbackDebounceMs)) {
        triggerImpact(std::clamp(0.55 + energy_ * 0.45
                                     + (kickPulse_ ? 0.12 : 0.0),
                                 0.0, 1.0),
                      false);
        fallbackDebounce_.restart();
    }
    emit featuresChanged();
    emit derivedUpdateCountChanged();
}

void AudioVisualFeatureController::connectPlaybackSignals()
{
    if (playback_ == nullptr) return;
    if (!playbackDestroyedConnection_) {
        playbackDestroyedConnection_ = connect(
            playback_, &QObject::destroyed, this, [this] {
                spectrumConnection_ = {};
                positionConnection_ = {};
                trackConnection_ = {};
                playbackDestroyedConnection_ = {};
                playback_ = nullptr;
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
    lastPositionMs_ = -1;
    lastImpactGroup_ = -1;
}

void AudioVisualFeatureController::triggerImpact(double strength, bool notify)
{
    impactStrength_ = std::clamp(strength, 0.0, 1.0);
    ++impactRevision_;
    if (notify) emit featuresChanged();
}

double AudioVisualFeatureController::normalizedValue(const QVariant& value) noexcept
{
    bool ok = false;
    const double parsed = value.toDouble(&ok);
    return ok ? std::clamp(parsed, 0.0, 1.0) : 0.0;
}
