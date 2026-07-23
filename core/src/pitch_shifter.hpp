#pragma once

#include <agplayer/c_api.h>

#include <atomic>
#include <functional>
#include <string>

namespace agplayer {

// Pitch shift configuration.
// pitch_cents: pitch shift in cents (1 semitone = 100 cents). Positive = up,
//   negative = down. Range: -1200..1200 (one octave either way).
// keep_tempo: if true (default), preserve original tempo/duration by applying
//   atempo compensation after asetrate. If false, both pitch and tempo change
//   together (duration changes proportionally).
// tempo_ratio: additional tempo multiplier (1.0 = no change). Applied on top
//   of pitch shift. Range: 0.5..2.0.
//   When keep_tempo=true: atempo = tempo_ratio (independent tempo control)
//   When keep_tempo=false: atempo = 2^(-cents/1200) * tempo_ratio
struct PitchShiftConfig {
    int pitch_cents = 0;
    bool keep_tempo = true;
    double tempo_ratio = 1.0;
    std::string output_path;
};

// Pitch-shift an audio file using FFmpeg's asetrate + atempo filter graph.
// progress_callback receives a fraction in [0.0, 1.0] based on processed
// duration. cancelled (may be null) is polled between frames.
// Returns AG_OK on success, AG_CANCELLED if cancelled.
ag_result pitch_shift(const std::string& input_path,
                      const PitchShiftConfig& config,
                      const std::atomic_bool* cancelled,
                      std::function<void(float)> progress_callback,
                      std::string& error);

} // namespace agplayer
