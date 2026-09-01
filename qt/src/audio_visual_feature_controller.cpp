#include "audio_visual_feature_controller.hpp"

#include "playback_controller.hpp"

#include <algorithm>
#include <cmath>

AudioVisualFeatureController::AudioVisualFeatureController(
    PlaybackController* playback, QObject* parent)
    : QObject(parent), playback_(playback)
{
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

void AudioVisualFeatureController::setPlaybackController(PlaybackController* playback)
{
    if (playback_ == playback) return;
    disconnectPlaybackSignals();
    playback_ = playback;
    resetBeatPosition();
    if (active_) connectPlaybackSignals();
}

void AudioVisualFeatureController::setActive(bool active)
{
    if (active_ == active) return;
    active_ = active;
    if (active_) {
        previousSpectrum_.clear();
        resetTransientHistory();
        connectPlaybackSignals();
    } else {
        disconnectPlaybackSignals();
    }
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
    previousSpectrum_.clear();
    resetTransientHistory();
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
    const double normalizedLowFlux = lowFlux / 32.0;
    const double normalizedHighFlux = highFlux / 32.0;
    const double kickThreshold = adaptiveThreshold(
        lowFluxHistory_, transientSampleCount_, 0.05);
    const double snareThreshold = adaptiveThreshold(
        highFluxHistory_, transientSampleCount_, 0.04);
    kickPulse_ = normalizedLowFlux >= kickThreshold
        && bands_.at(0).toDouble() + bands_.at(1).toDouble() >= 0.20;
    snarePulse_ = normalizedHighFlux >= snareThreshold
        && bands_.at(4).toDouble() + bands_.at(5).toDouble() >= 0.20;
    appendTransientSample(lowFluxHistory_, transientSampleCount_,
                          transientWriteIndex_, normalizedLowFlux);
    const int highWriteIndex = (transientWriteIndex_ + 23) % 24;
    highFluxHistory_[std::size_t(highWriteIndex)] = normalizedHighFlux;
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
}

void AudioVisualFeatureController::resetBeatPosition() noexcept
{
    lastPositionMs_ = -1;
    lastImpactGroup_ = -1;
}

void AudioVisualFeatureController::resetTransientHistory() noexcept
{
    lowFluxHistory_.fill(0.0);
    highFluxHistory_.fill(0.0);
    transientSampleCount_ = 0;
    transientWriteIndex_ = 0;
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

double AudioVisualFeatureController::normalizedValue(const QVariant& value) noexcept
{
    bool ok = false;
    const double parsed = value.toDouble(&ok);
    return ok ? std::clamp(parsed, 0.0, 1.0) : 0.0;
}
