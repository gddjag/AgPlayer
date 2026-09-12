#pragma once

#include <cstddef>
#include <memory>

namespace agplayer {

enum class TimePitchEngineKind {
    None,
    Signalsmith,
    SoundTouch,
};

// Shared realtime/offline time and pitch processing boundary.  Callers express
// their intent in ratios and cents; only this implementation knows SoundTouch.
class ITimePitchEngine {
public:
    virtual ~ITimePitchEngine() = default;

    virtual bool configure(int sampleRate, int channels) = 0;
    virtual bool setTempoRatio(double ratio) = 0;
    virtual bool setPitchCents(double cents) = 0;
    virtual bool setRateRatio(double ratio) = 0;
    virtual bool setFormantPreservation(bool enabled) = 0;
    virtual void put(const float* samples, std::size_t frames) = 0;
    [[nodiscard]] virtual std::size_t receive(float* samples,
                                               std::size_t frames) = 0;
    virtual void flush() = 0;
    virtual void reset() = 0;
    // The fixed internal FIFO requires callers to interleave put()/receive().
    // A full FIFO is a processing failure, never an end-of-stream indication.
    [[nodiscard]] virtual bool failed() const noexcept = 0;
    [[nodiscard]] virtual TimePitchEngineKind kind() const noexcept = 0;
};

using TimePitchEngineFactory = std::unique_ptr<ITimePitchEngine> (*)();

[[nodiscard]] std::unique_ptr<ITimePitchEngine> create_time_pitch_engine();
[[nodiscard]] std::unique_ptr<ITimePitchEngine>
create_preferred_time_pitch_engine(std::unique_ptr<ITimePitchEngine> primary);
// Explicit fallback for diagnostics and for the primary factory when a
// Signalsmith instance rejects its configuration.
[[nodiscard]] std::unique_ptr<ITimePitchEngine>
create_soundtouch_time_pitch_engine();

// Kept separate so the primary factory can guard configuration failures while
// callers continue to depend only on ITimePitchEngine.
[[nodiscard]] std::unique_ptr<ITimePitchEngine>
create_signalsmith_time_pitch_engine();

} // namespace agplayer
