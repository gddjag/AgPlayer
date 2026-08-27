#include "audio_visual_feature_controller.hpp"

#include "playback_controller.hpp"

#include <algorithm>

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
quint64 AudioVisualFeatureController::derivedUpdateCount() const noexcept
{
    return derivedUpdateCount_;
}

void AudioVisualFeatureController::setPlaybackController(PlaybackController* playback)
{
    if (playback_ == playback) return;
    disconnectSpectrum();
    playback_ = playback;
    if (active_) connectSpectrum();
}

void AudioVisualFeatureController::setActive(bool active)
{
    if (active_ == active) return;
    active_ = active;
    if (active_) {
        previousSpectrum_.clear();
        connectSpectrum();
    } else {
        disconnectSpectrum();
    }
    emit activeChanged();
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
    emit featuresChanged();
    emit derivedUpdateCountChanged();
}

void AudioVisualFeatureController::connectSpectrum()
{
    if (playback_ == nullptr || spectrumConnection_) return;
    spectrumConnection_ = connect(playback_, &PlaybackController::spectrumChanged,
                                  this, [this] {
                                      if (playback_ != nullptr) {
                                          processSpectrum(playback_->spectrum());
                                      }
                                  });
}

void AudioVisualFeatureController::disconnectSpectrum()
{
    if (spectrumConnection_) {
        disconnect(spectrumConnection_);
        spectrumConnection_ = {};
    }
}

double AudioVisualFeatureController::normalizedValue(const QVariant& value) noexcept
{
    bool ok = false;
    const double parsed = value.toDouble(&ok);
    return ok ? std::clamp(parsed, 0.0, 1.0) : 0.0;
}
