#pragma once

#include <cstddef>
#include <memory>

namespace agplayer {

// Shared realtime/offline time and pitch processing boundary.  Callers express
// their intent in ratios and cents; only this implementation knows SoundTouch.
class ITimePitchEngine {
public:
    virtual ~ITimePitchEngine() = default;

    virtual bool configure(int sampleRate, int channels) = 0;
    virtual bool setTempoRatio(double ratio) = 0;
    virtual bool setPitchCents(double cents) = 0;
    virtual bool setRateRatio(double ratio) = 0;
    virtual void put(const float* samples, std::size_t frames) = 0;
    [[nodiscard]] virtual std::size_t receive(float* samples,
                                               std::size_t frames) = 0;
    virtual void flush() = 0;
    virtual void reset() = 0;
};

[[nodiscard]] std::unique_ptr<ITimePitchEngine> create_time_pitch_engine();

} // namespace agplayer
