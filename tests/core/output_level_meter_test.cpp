// Keep assertions active in Release: this test intentionally uses assert as
// its lightweight test harness.
#undef NDEBUG

#include "../../core/src/audio_engine.hpp"
#include "../../core/src/audio_stream_source.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace agplayer {

class AudioEngineTestAccess final {
public:
    static void publishOutputLevels(AudioEngine& engine,
                                    const float* samples,
                                    const std::size_t frames,
                                    const std::size_t channels) noexcept
    {
        engine.publish_output_levels_for_testing(samples, frames, channels);
    }
};

} // namespace agplayer

namespace {

class ConstantStereoStream final : public agplayer::IAudioStreamSource {
public:
    ConstantStereoStream()
    {
        metadata_.sample_rate = 48'000;
        metadata_.channels = 2;
        metadata_.duration_ms = 2'000;
    }

    const agplayer::MediaMetadata& metadata() const noexcept override
    {
        return metadata_;
    }

    ag_result read(agplayer::DecodedAudioBlock& block) noexcept override
    {
        constexpr std::int64_t totalFrames = 96'000;
        const std::size_t frames = static_cast<std::size_t>(
            (std::min)(std::int64_t{1'024}, totalFrames - position_));
        block = {};
        block.frames = frames;
        block.timestamp_frame = position_;
        block.timestamp_ms = position_ * 1'000 / metadata_.sample_rate;
        block.samples.resize(frames * 2U);
        for (std::size_t frame = 0U; frame < frames; ++frame) {
            block.samples[frame * 2U] = 0.25F;
            block.samples[frame * 2U + 1U] = 0.50F;
        }
        position_ += static_cast<std::int64_t>(frames);
        block.end_of_stream = position_ >= totalFrames;
        return AG_OK;
    }

    ag_result seek(const std::int64_t positionMs) noexcept override
    {
        if (positionMs < 0 || positionMs > metadata_.duration_ms) {
            return AG_INVALID_ARGUMENT;
        }
        position_ = positionMs * metadata_.sample_rate / 1'000;
        return AG_OK;
    }

private:
    agplayer::MediaMetadata metadata_;
    std::int64_t position_ = 0;
};

class ShortBlockingStereoStream final : public agplayer::IAudioStreamSource {
public:
    ShortBlockingStereoStream()
    {
        metadata_.sample_rate = 48'000;
        metadata_.channels = 2;
        metadata_.duration_ms = 2'000;
    }

    const agplayer::MediaMetadata& metadata() const noexcept override
    {
        return metadata_;
    }

    ag_result read(agplayer::DecodedAudioBlock& block) noexcept override
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!served_) {
            served_ = true;
            block = {};
            block.frames = 2U;
            block.samples = {0.25F, 0.50F, 0.25F, 0.50F};
            return AG_OK;
        }
        condition_.wait(lock, [this] { return released_; });
        block = {};
        block.end_of_stream = true;
        return AG_OK;
    }

    ag_result seek(std::int64_t) noexcept override { return AG_OK; }

    void release()
    {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            released_ = true;
        }
        condition_.notify_all();
    }

private:
    agplayer::MediaMetadata metadata_;
    std::mutex mutex_;
    std::condition_variable condition_;
    bool served_ = false;
    bool released_ = false;
};

bool near(const float actual, const float expected,
          const float tolerance = 1.0e-5F)
{
    return std::abs(actual - expected) <= tolerance;
}

void requireLevels(const agplayer::OutputLevels& levels,
                   const float leftPeak, const float rightPeak,
                   const float leftRms, const float rightRms,
                   const float tolerance = 1.0e-5F)
{
    if (!near(levels.left_peak, leftPeak, tolerance)
        || !near(levels.right_peak, rightPeak, tolerance)
        || !near(levels.left_rms, leftRms, tolerance)
        || !near(levels.right_rms, rightRms, tolerance)) {
        std::cerr << "actual=" << levels.left_peak << ','
                  << levels.right_peak << ',' << levels.left_rms << ','
                  << levels.right_rms << " expected=" << leftPeak << ','
                  << rightPeak << ',' << leftRms << ',' << rightRms << '\n';
    }
    assert(near(levels.left_peak, leftPeak, tolerance));
    assert(near(levels.right_peak, rightPeak, tolerance));
    assert(near(levels.left_rms, leftRms, tolerance));
    assert(near(levels.right_rms, rightRms, tolerance));
}

agplayer::OutputLevels measuredLevels(const std::vector<float>& samples,
                                      const std::size_t frames)
{
    float leftPeak = 0.0F;
    float rightPeak = 0.0F;
    double leftSquares = 0.0;
    double rightSquares = 0.0;
    for (std::size_t frame = 0U; frame < frames; ++frame) {
        const float left = samples[frame * 2U];
        const float right = samples[frame * 2U + 1U];
        leftPeak = (std::max)(leftPeak, std::abs(left));
        rightPeak = (std::max)(rightPeak, std::abs(right));
        leftSquares += static_cast<double>(left) * left;
        rightSquares += static_cast<double>(right) * right;
    }
    return {
        (std::min)(leftPeak, 1.0F),
        (std::min)(rightPeak, 1.0F),
        (std::min)(static_cast<float>(
            std::sqrt(leftSquares / static_cast<double>(frames))), 1.0F),
        (std::min)(static_cast<float>(
            std::sqrt(rightSquares / static_cast<double>(frames))), 1.0F),
    };
}

void waitForBufferedFrames(agplayer::AudioEngine& engine,
                           const std::size_t minimum)
{
    for (int attempt = 0; attempt < 2'000; ++attempt) {
        if (engine.buffered_frames() >= minimum) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(false && "audio engine did not buffer deterministic stream");
}

} // namespace

int main()
{
    agplayer::AudioEngine engine(agplayer::AudioBackend::Manual, 1'024U);
    assert(engine.load_stream(std::make_shared<ConstantStereoStream>(), true)
           == AG_OK);
    assert(engine.play() == AG_OK);
    requireLevels(engine.output_levels(), 0.0F, 0.0F, 0.0F, 0.0F);

    constexpr float leftOnly[] = {
        1.0F, 0.0F,
        0.5F, 0.0F,
    };
    agplayer::AudioEngineTestAccess::publishOutputLevels(
        engine, leftOnly, 2U, 2U);
    requireLevels(engine.output_levels(), 1.0F, 0.0F,
                  std::sqrt(0.625F), 0.0F);

    constexpr float rightOnly[] = {
        0.0F, -0.25F,
        0.0F, 0.75F,
    };
    agplayer::AudioEngineTestAccess::publishOutputLevels(
        engine, rightOnly, 2U, 2U);
    requireLevels(engine.output_levels(), 0.0F, 0.75F, 0.0F,
                  std::sqrt(0.3125F));

    constexpr float mono[] = {0.25F, -0.75F};
    agplayer::AudioEngineTestAccess::publishOutputLevels(
        engine, mono, 2U, 1U);
    requireLevels(engine.output_levels(), 0.75F, 0.75F,
                  std::sqrt(0.3125F), std::sqrt(0.3125F));

    constexpr float clippedStereo[] = {
        -2.0F, 1.5F,
        2.0F, -1.5F,
    };
    agplayer::AudioEngineTestAccess::publishOutputLevels(
        engine, clippedStereo, 2U, 2U);
    requireLevels(engine.output_levels(), 1.0F, 1.0F, 1.0F, 1.0F);

    constexpr float unsupported[] = {0.5F, 0.5F, 0.5F};
    agplayer::AudioEngineTestAccess::publishOutputLevels(
        engine, unsupported, 1U, 3U);
    requireLevels(engine.output_levels(), 0.0F, 0.0F, 0.0F, 0.0F);

    constexpr float nonFinite[] = {
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        0.5F, -0.5F,
    };
    agplayer::AudioEngineTestAccess::publishOutputLevels(
        engine, nonFinite, 2U, 2U);
    requireLevels(engine.output_levels(), 0.5F, 0.5F,
                  std::sqrt(0.125F), std::sqrt(0.125F));

    agplayer::AudioEngineTestAccess::publishOutputLevels(
        engine, nullptr, 0U, 2U);
    requireLevels(engine.output_levels(), 0.0F, 0.0F, 0.0F, 0.0F);

    agplayer::AudioEngine rendered(agplayer::AudioBackend::Manual, 4'096U);
    assert(rendered.load_stream(std::make_shared<ConstantStereoStream>(), true)
           == AG_OK);
    assert(rendered.play() == AG_OK);
    agplayer::GraphicEqSettings disabledEq;
    disabledEq.enabled = false;
    assert(rendered.set_equalizer(disabledEq, 1U) == AG_OK);
    waitForBufferedFrames(rendered, 256U);
    std::vector<float> output(256U * 2U, 0.0F);
    rendered.render(output.data(), 256U);
    const agplayer::OutputLevels expected = measuredLevels(output, 256U);
    requireLevels(rendered.output_levels(), expected.left_peak,
                  expected.right_peak, expected.left_rms,
                  expected.right_rms);
    assert(expected.left_peak > 0.0F);
    assert(expected.right_peak > expected.left_peak);

    assert(rendered.set_volume(0.5F) == AG_OK);
    rendered.render(output.data(), 256U);
    requireLevels(rendered.output_levels(), 0.125F, 0.25F,
                  0.125F, 0.25F);
    assert(rendered.set_volume(1.0F) == AG_OK);
    assert(rendered.set_replay_gain(-6.0205999F, 1.0F, false) == AG_OK);
    rendered.render(output.data(), 256U);
    requireLevels(rendered.output_levels(), 0.125F, 0.25F,
                  0.125F, 0.25F, 2.0e-5F);
    assert(rendered.set_replay_gain(0.0F, 1.0F, false) == AG_OK);

    rendered.set_muted(true);
    rendered.render(output.data(), 256U);
    requireLevels(rendered.output_levels(), 0.0F, 0.0F, 0.0F, 0.0F);

    rendered.set_muted(false);
    waitForBufferedFrames(rendered, 256U);
    rendered.render(output.data(), 256U);
    assert(rendered.output_levels().right_peak > 0.0F);
    assert(rendered.pause() == AG_OK);
    requireLevels(rendered.output_levels(), 0.0F, 0.0F, 0.0F, 0.0F);
    rendered.render(output.data(), 256U);
    requireLevels(rendered.output_levels(), 0.0F, 0.0F, 0.0F, 0.0F);

    assert(rendered.play() == AG_OK);
    waitForBufferedFrames(rendered, 256U);
    rendered.render(output.data(), 256U);
    assert(rendered.output_levels().right_peak > 0.0F);
    assert(rendered.stop() == AG_OK);
    requireLevels(rendered.output_levels(), 0.0F, 0.0F, 0.0F, 0.0F);

    agplayer::AudioEngine lost(agplayer::AudioBackend::Manual, 4'096U);
    assert(lost.load_stream(std::make_shared<ConstantStereoStream>(), true)
           == AG_OK);
    assert(lost.play() == AG_OK);
    waitForBufferedFrames(lost, 256U);
    lost.render(output.data(), 256U);
    assert(lost.output_levels().right_peak > 0.0F);
    lost.simulate_device_loss();
    requireLevels(lost.output_levels(), 0.0F, 0.0F, 0.0F, 0.0F);

    auto shortStream = std::make_shared<ShortBlockingStereoStream>();
    agplayer::AudioEngine shortRead(agplayer::AudioBackend::Manual, 64U);
    assert(shortRead.load_stream(shortStream, true) == AG_OK);
    assert(shortRead.play() == AG_OK);
    waitForBufferedFrames(shortRead, 2U);
    std::vector<float> shortOutput(8U, 1.0F);
    shortRead.render(shortOutput.data(), 4U);
    const agplayer::OutputLevels shortLevels = shortRead.output_levels();
    shortStream->release();
    assert(shortRead.stop() == AG_OK);
    assert(shortOutput[4] == 0.0F && shortOutput[5] == 0.0F
           && shortOutput[6] == 0.0F && shortOutput[7] == 0.0F);
    requireLevels(shortLevels, 0.25F, 0.50F,
                  0.25F / std::sqrt(2.0F),
                  0.50F / std::sqrt(2.0F));
    return 0;
}
