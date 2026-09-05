#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace agplayer::ui {

// Immutable summaries in source-bucket coordinates. Energy integrates the
// complete pixel interval, whereas the peak tree preserves short transients.
// Rebuilt only for new audio data, never for palette or playback changes.
class WaveformDisplaySummary {
public:
    void build(const std::vector<float>& values, bool keepPeaks = false)
    {
        energy_.assign(values.size() + 1U, 0.0);
        for (std::size_t i = 0; i < values.size(); ++i) {
            const double value = values[i];
            energy_[i + 1U] = energy_[i] + value * value;
        }
        leaves_ = keepPeaks ? values.size() : 0U;
        peaks_.assign(leaves_ * 2U, 0.0F);
        if (leaves_ == 0U) return;
        std::copy(values.begin(), values.end(), peaks_.begin() + leaves_);
        for (std::size_t i = leaves_ - 1U; i > 0U; --i) {
            peaks_[i] = std::max(peaks_[i * 2U], peaks_[i * 2U + 1U]);
        }
    }

    float rms(double begin, double end) const
    {
        if (energy_.size() <= 1U || end <= begin) return 0.0F;
        return static_cast<float>(std::sqrt(std::max(
            0.0, (integral(end) - integral(begin)) / (end - begin))));
    }

    float peak(double begin, double end) const
    {
        if (leaves_ == 0U || end <= begin) return 0.0F;
        auto left = static_cast<std::size_t>(std::floor(std::clamp(
            begin, 0.0, static_cast<double>(leaves_)))) + leaves_;
        auto right = static_cast<std::size_t>(std::ceil(std::clamp(
            end, 0.0, static_cast<double>(leaves_)))) + leaves_;
        float result = 0.0F;
        while (left < right) {
            if (left & 1U) result = std::max(result, peaks_[left++]);
            if (right & 1U) result = std::max(result, peaks_[--right]);
            left /= 2U;
            right /= 2U;
        }
        return result;
    }

private:
    double integral(double position) const
    {
        position = std::clamp(position, 0.0,
                              static_cast<double>(energy_.size() - 1U));
        const auto index = static_cast<std::size_t>(position);
        if (index + 1U >= energy_.size()) return energy_.back();
        return energy_[index] + (energy_[index + 1U] - energy_[index])
            * (position - static_cast<double>(index));
    }
    std::vector<double> energy_;
    std::vector<float> peaks_;
    std::size_t leaves_ = 0U;
};

} // namespace agplayer::ui
