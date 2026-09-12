#pragma once

#include "visual_spectrum_analyzer.hpp"
#include "visual_spectrum_features.hpp"
#include "visual_kick_response.hpp"
#include "visual_pulse_trigger.hpp"
#include "visual_snare_trigger.hpp"

namespace agplayer {

// Value-only handoff from the GUI's PCM collector to one render owner.
// Process each monotonic PCM window once. The render owner may reuse frame()
// while collection is briefly late; no QObject, allocation or playback-core
// ownership crosses this boundary.
class VisualAudioFrameAnalyzer {
public:
    // One delayed GUI poll can drain sixteen 1024-sample core reads. At the
    // reference 60 Hz analysis cadence this bound retains every due window
    // even at the lowest supported sample rates.
    static constexpr std::size_t BatchCapacity = 32;

    struct Snapshot {
        VisualSpectrumAnalyzer::Window pcm{};
        int sampleRate = 0;
        std::uint64_t epoch = 0;
        std::uint64_t firstSampleIndex = 0;
        std::uint64_t sequence = 0;
        bool valid = false;
        bool paused = false;
        bool releasing = false;
    };
    struct Batch {
        std::array<Snapshot, BatchCapacity> frames{};
        std::size_t count = 0;
    };
    struct Frame {
        VisualSpectrumAnalyzer::Spectrum spectrum{};
        VisualSpectrumFeatures::Features descriptors{};
        visual::KickResponse::Output kick{};
        VisualPulseTrigger::Output pulse{};
        VisualSnareTrigger::Output meteor{};
        bool valid = false;
    };

    void reset() noexcept {
        spectrum_.reset();
        features_.reset();
        kick_.reset();
        pulse_.reset();
        meteor_.reset();
        frame_ = {};
        initialized_ = false;
    }

    const Frame& process(const Snapshot& snapshot, double dt,
                         int sensitivity = 100) noexcept {
        if ((!snapshot.valid || snapshot.sampleRate <= 0)
            && snapshot.paused && initialized_) {
            dt = std::isfinite(dt) ? std::clamp(dt, 0.0, .25) : 0.0;
            for (auto& value : frame_.spectrum)
                value = snapshot.releasing ? std::uint8_t(std::floor(value * .94)) : 0;
            frame_.descriptors = features_.update(frame_.spectrum, false,
                                                  snapshot.releasing);
            frame_.kick = kick_.process(frame_.spectrum, dt, sensitivity);
            frame_.pulse = pulse_.suspend(dt);
            frame_.meteor = meteor_.suspend();
            frame_.valid = true;
            return frame_;
        }
        if (!snapshot.valid || snapshot.sampleRate <= 0) {
            reset();
            return frame_;
        }
        const double trackingDt = std::isfinite(dt) ? std::clamp(dt, 0.0, .25) : 0.0;
        if (!initialized_ || epoch_ != snapshot.epoch || sampleRate_ != snapshot.sampleRate) {
            reset();
            epoch_ = snapshot.epoch;
            sampleRate_ = snapshot.sampleRate;
            initialized_ = true;
            dt = 1.0 / 60.0;
        } else {
            dt = std::isfinite(dt) ? std::clamp(dt, 0.0, .25) : 0.0;
        }
        frame_.spectrum = spectrum_.process(snapshot.pcm);
        frame_.descriptors = features_.update(frame_.spectrum, true, false);
        // Original AudioEngine: terrain kick and colored wave are independent.
        // No recovery, BPM extrapolation or melody veto modifies source events.
        frame_.kick = kick_.process(frame_.spectrum, dt, sensitivity);
        frame_.pulse = pulse_.process(frame_.spectrum, trackingDt);
        frame_.meteor = meteor_.process(frame_.spectrum);
        frame_.valid = true;
        return frame_;
    }

    const Frame& frame() const noexcept { return frame_; }

private:
    VisualSpectrumAnalyzer spectrum_;
    VisualSpectrumFeatures features_;
    visual::KickResponse kick_{visual::KickResponse::Mode::Reference};
    VisualPulseTrigger pulse_;
    VisualSnareTrigger meteor_{159, 174, .45, 241, .5};
    Frame frame_{};
    std::uint64_t epoch_ = 0;
    int sampleRate_ = 0;
    bool initialized_ = false;
};
} // namespace agplayer
