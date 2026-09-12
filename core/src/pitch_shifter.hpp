#pragma once

#include <agplayer/c_api.h>

#include <atomic>
#include <functional>
#include <string>

namespace agplayer {

// Pitch shift configuration.
// pitch_cents: pitch shift in cents (1 semitone = 100 cents). Positive = up,
//   negative = down. Range: -1200..1200 (one octave either way).
// keep_tempo: if true (default), preserve duration while changing pitch. If
//   false, pitch changes by changing playback rate and duration changes too.
// tempo_ratio: additional tempo multiplier (1.0 = no change). Applied on top
//   of pitch shift. Range: 0.5..2.0.
//   SoundTouch applies this as an independent playback-tempo multiplier.
// output_codec_name: empty = same codec as input, else FFmpeg codec name.
// output_sample_rate: 0 = auto (follow pitch/tempo), else target output Hz.
// vocal_protection / smooth_transition apply optional output filtering/fades.
struct PitchShiftConfig {
    int pitch_cents = 0;
    bool keep_tempo = true;
    double tempo_ratio = 1.0;
    std::string output_path;
    std::string output_codec_name;
    int output_sample_rate = 0;
    bool vocal_protection = false;
    bool smooth_transition = false;
};

// Pitch-shift an audio file using SoundTouch DSP and FFmpeg I/O.
// progress_callback receives a fraction in [0.0, 1.0] based on processed
// duration. cancelled (may be null) is polled between frames.
// Returns AG_OK on success, AG_CANCELLED if cancelled.
ag_result pitch_shift(const std::string& input_path,
                      const PitchShiftConfig& config,
                      const std::atomic_bool* cancelled,
                      std::function<void(float)> progress_callback,
                      std::string& error);

} // namespace agplayer
