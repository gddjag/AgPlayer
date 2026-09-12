#include "audio_engine.hpp"
#include "audio_stream_source.hpp"
#include "playback_time_pitch_stage.hpp"
#include "time_pitch_engine.hpp"

#include <agplayer/c_api.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace agplayer {

class AudioEngineTestAccess final {
public:
    using TimePitchTestHook = void (*)(void*, std::uint64_t, int) noexcept;

    static std::int64_t pendingBoundary(const AudioEngine& engine)
    {
        return engine.pending_boundary_for_testing();
    }

    static std::uint64_t publishedMapperGeneration(const AudioEngine& engine)
    {
        return engine.published_mapper_generation_for_testing();
    }

    static std::uint64_t pendingMapperGeneration(const AudioEngine& engine)
    {
        return engine.pending_mapper_generation_for_testing();
    }

    static std::int64_t consumedSourceFrame(const AudioEngine& engine)
    {
        return engine.consumed_source_frame_for_testing();
    }

    static bool decodeRunning(const AudioEngine& engine)
    {
        return engine.decode_running_for_testing();
    }

    static bool timePitchCancelRequested(const AudioEngine& engine)
    {
        return engine.time_pitch_cancel_requested_for_testing();
    }

    static bool timePitchRequestIdle(const AudioEngine& engine)
    {
        return engine.time_pitch_request_idle_for_testing();
    }

    static void setTimePitchTestHook(AudioEngine& engine,
                                     const TimePitchTestHook hook,
                                     void* const context)
    {
        engine.set_time_pitch_test_hook(hook, context);
    }

    static std::uint64_t requestedTimePitchGeneration(
        const AudioEngine& engine)
    {
        return engine.requested_time_pitch_generation_for_testing();
    }

    static std::uint64_t completedTimePitchGeneration(
        const AudioEngine& engine)
    {
        return engine.completed_time_pitch_generation_for_testing();
    }

    static bool hasRetiredTimePitchDecoder(const AudioEngine& engine)
    {
        return engine.has_retired_time_pitch_decoder_for_testing();
    }

    static bool timePitchMailboxClear(const AudioEngine& engine)
    {
        return engine.time_pitch_mailbox_clear_for_testing();
    }

    static void stopDecodeThread(AudioEngine& engine)
    {
        engine.stop_decode_thread_for_testing();
    }

    static void requestDecodeExit(AudioEngine& engine)
    {
        engine.request_decode_exit_for_testing();
    }

    static void markDeviceLost(AudioEngine& engine)
    {
        engine.mark_device_lost_for_testing();
    }
};

class PlaybackTimePitchStageTestAccess final {
public:
    static std::size_t pendingSize(const PlaybackTimePitchStage& stage)
    {
        return stage.pending_source_frames_.size();
    }

    static std::size_t pendingCapacity(const PlaybackTimePitchStage& stage)
    {
        return stage.pending_source_frames_.capacity();
    }

    static std::size_t pendingLive(const PlaybackTimePitchStage& stage)
    {
        return stage.pending_source_frames_.size()
            - stage.pending_source_offset_;
    }
};

} // namespace agplayer

namespace {

constexpr int sample_rate = 48'000;
constexpr double pi = 3.14159265358979323846;

[[noreturn]] void fail(const char* const message)
{
    std::cerr << message << '\n';
    std::exit(1);
}

void require(const bool condition, const char* const message)
{
    if (!condition) fail(message);
}

std::vector<float> sine(const std::size_t frames, const double frequency)
{
    std::vector<float> samples(frames);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        samples[frame] = static_cast<float>(0.5 * std::sin(
            2.0 * pi * frequency * static_cast<double>(frame) / sample_rate));
    }
    return samples;
}

double estimate_frequency(const std::vector<float>& samples)
{
    const std::size_t first = samples.size() / 5U;
    const std::size_t last = samples.size() * 4U / 5U;
    std::size_t crossings = 0U;
    for (std::size_t index = first + 1U; index < last; ++index) {
        if (samples[index - 1U] <= 0.0F && samples[index] > 0.0F) {
            ++crossings;
        }
    }
    const double seconds = static_cast<double>(last - first) / sample_rate;
    return static_cast<double>(crossings) / seconds;
}

double cents_between(const double measured, const double expected)
{
    return 1'200.0 * std::log2(measured / expected);
}

std::vector<float> process(const double ratio, const bool keepPitch,
                           agplayer::TimePitchEngineKind& kind,
                           bool& bypassed)
{
    constexpr std::size_t input_frames = 96'000U;
    const std::vector<float> input = sine(input_frames, 440.0);
    agplayer::PlaybackTimePitchStage stage;
    require(stage.configure(sample_rate, 1, {ratio, keepPitch}) == AG_OK,
            "playback stage configure failed");
    kind = stage.engine_kind();
    bypassed = stage.bypassed();
    std::vector<float> output;
    for (std::size_t offset = 0U; offset < input.size(); offset += 4'096U) {
        const std::size_t count = std::min<std::size_t>(
            4'096U, input.size() - offset);
        require(stage.process(input.data() + offset, count, output) == AG_OK,
                "playback stage process failed");
    }
    require(stage.finish(output) == AG_OK, "playback stage finish failed");
    if (ratio == 1.0) {
        require(output == input, "1.00x did not preserve exact PCM");
    }
    return output;
}

class RejectingEngine final : public agplayer::ITimePitchEngine {
public:
    bool configure(int, int) override { return false; }
    bool setTempoRatio(double) override { return false; }
    bool setPitchCents(double) override { return false; }
    bool setRateRatio(double) override { return false; }
    bool setFormantPreservation(bool) override { return false; }
    void put(const float*, std::size_t) override {}
    std::size_t receive(float*, std::size_t) override { return 0U; }
    void flush() override {}
    void reset() override {}
    bool failed() const noexcept override { return false; }
    agplayer::TimePitchEngineKind kind() const noexcept override
    {
        return agplayer::TimePitchEngineKind::None;
    }
};

class FailOnPutEngine final : public agplayer::ITimePitchEngine {
public:
    bool configure(int, int) override { return true; }
    bool setTempoRatio(double) override { return true; }
    bool setPitchCents(double) override { return true; }
    bool setRateRatio(double) override { return true; }
    bool setFormantPreservation(bool) override { return true; }
    void put(const float*, std::size_t) override { failed_ = true; }
    std::size_t receive(float*, std::size_t) override { return 0U; }
    void flush() override {}
    void reset() override { failed_ = false; }
    bool failed() const noexcept override { return failed_; }
    agplayer::TimePitchEngineKind kind() const noexcept override
    {
        return agplayer::TimePitchEngineKind::None;
    }

private:
    bool failed_{};
};

class BlockLatencyEngine final : public agplayer::ITimePitchEngine {
public:
    bool configure(int, int channels) override
    {
        channels_ = channels;
        return channels > 0;
    }
    bool setTempoRatio(double) override { return true; }
    bool setPitchCents(double) override { return true; }
    bool setRateRatio(double) override { return true; }
    bool setFormantPreservation(bool) override { return true; }
    void put(const float* samples, std::size_t frames) override
    {
        const std::size_t sampleCount = frames * static_cast<std::size_t>(channels_);
        pending_.insert(pending_.end(), samples, samples + sampleCount);
        ++put_count_;
        if (put_count_ >= 2U) {
            ready_.insert(ready_.end(), pending_.begin(), pending_.end());
            pending_.clear();
        }
    }
    std::size_t receive(float* samples, std::size_t frames) override
    {
        const std::size_t availableFrames = ready_.size()
            / static_cast<std::size_t>(channels_);
        const std::size_t count = std::min(frames, availableFrames);
        const std::size_t sampleCount = count * static_cast<std::size_t>(channels_);
        std::copy_n(ready_.begin(), sampleCount, samples);
        ready_.erase(ready_.begin(), ready_.begin()
            + static_cast<std::ptrdiff_t>(sampleCount));
        return count;
    }
    void flush() override
    {
        ready_.insert(ready_.end(), pending_.begin(), pending_.end());
        pending_.clear();
    }
    void reset() override
    {
        pending_.clear();
        ready_.clear();
        put_count_ = 0U;
    }
    bool failed() const noexcept override { return false; }
    agplayer::TimePitchEngineKind kind() const noexcept override
    {
        return agplayer::TimePitchEngineKind::None;
    }

private:
    int channels_{1};
    std::size_t put_count_{};
    std::vector<float> pending_;
    std::vector<float> ready_;
};

std::mutex recording_mutex;
std::thread::id recording_configure_thread;

std::mutex blocking_configure_mutex;
std::condition_variable blocking_configure_cv;
bool blocking_configure_entered = false;
bool blocking_configure_release = false;
std::thread::id blocking_configure_thread;

struct TimePitchAbaGate final {
    std::mutex mutex;
    std::condition_variable cv;
    std::uint64_t first_generation{};
    std::uint64_t second_generation{};
    bool first_completion_entered{};
    bool release_first_completion{};
    bool first_trailing_publish{};
    bool second_request_entered{};
    bool release_second_request{};
    bool second_decode_entered{};
    bool release_second_decode{};
    bool first_setter_returned{};
    bool second_setter_returned{};
};

struct TimePitchCommitGate final {
    std::mutex mutex;
    std::condition_variable cv;
    bool commit_entered{};
    bool release_commit{};
    bool setter_returned{};
};

struct TimePitchExecutingGate final {
    std::mutex mutex;
    std::condition_variable cv;
    bool executing_entered{};
    bool release_executing{};
    bool setter_returned{};
};

struct TimePitchPendingLossGate final {
    std::mutex mutex;
    std::condition_variable cv;
    bool request_entered{};
    bool release_request{};
};

void time_pitch_aba_hook(void* const context,
                         const std::uint64_t generation,
                         const int phase) noexcept
{
    auto& gate = *static_cast<TimePitchAbaGate*>(context);
    std::unique_lock<std::mutex> lock(gate.mutex);
    if (phase == 0) {
        if (gate.first_generation == 0U) {
            gate.first_generation = generation;
            gate.cv.notify_all();
        } else if (gate.second_generation == 0U) {
            gate.second_generation = generation;
            gate.second_request_entered = true;
            gate.cv.notify_all();
            gate.cv.wait(lock, [&gate] { return gate.release_second_request; });
        }
        return;
    }
    if (generation == gate.first_generation && phase == 1) {
        gate.first_completion_entered = true;
        gate.cv.notify_all();
        gate.cv.wait(lock, [&gate] { return gate.release_first_completion; });
        return;
    }
    if (generation == gate.first_generation && phase == 2) {
        gate.first_trailing_publish = true;
        gate.cv.notify_all();
        return;
    }
    if (generation == gate.second_generation && phase == 3) {
        gate.second_decode_entered = true;
        gate.cv.notify_all();
        gate.cv.wait(lock, [&gate] { return gate.release_second_decode; });
    }
}

void time_pitch_commit_hook(void* const context, const std::uint64_t,
                            const int phase) noexcept
{
    if (phase != 4) return;
    auto& gate = *static_cast<TimePitchCommitGate*>(context);
    std::unique_lock<std::mutex> lock(gate.mutex);
    gate.commit_entered = true;
    gate.cv.notify_all();
    gate.cv.wait(lock, [&gate] { return gate.release_commit; });
}

void time_pitch_executing_hook(void* const context, const std::uint64_t,
                               const int phase) noexcept
{
    if (phase != 5) return;
    auto& gate = *static_cast<TimePitchExecutingGate*>(context);
    std::unique_lock<std::mutex> lock(gate.mutex);
    gate.executing_entered = true;
    gate.cv.notify_all();
    gate.cv.wait(lock, [&gate] { return gate.release_executing; });
}

void time_pitch_pending_loss_hook(void* const context, const std::uint64_t,
                                  const int phase) noexcept
{
    if (phase != 0) return;
    auto& gate = *static_cast<TimePitchPendingLossGate*>(context);
    std::unique_lock<std::mutex> lock(gate.mutex);
    gate.request_entered = true;
    gate.cv.notify_all();
    gate.cv.wait(lock, [&gate] { return gate.release_request; });
}

class BlockingPassThroughEngine final : public agplayer::ITimePitchEngine {
public:
    bool configure(int, int) override
    {
        std::unique_lock<std::mutex> lock(blocking_configure_mutex);
        blocking_configure_thread = std::this_thread::get_id();
        blocking_configure_entered = true;
        blocking_configure_cv.notify_all();
        blocking_configure_cv.wait(lock, [] { return blocking_configure_release; });
        return true;
    }
    bool setTempoRatio(double) override { return true; }
    bool setPitchCents(double) override { return true; }
    bool setRateRatio(double) override { return true; }
    bool setFormantPreservation(bool) override { return true; }
    void put(const float*, std::size_t) override {}
    std::size_t receive(float*, std::size_t) override { return 0U; }
    void flush() override {}
    void reset() override {}
    bool failed() const noexcept override { return false; }
    agplayer::TimePitchEngineKind kind() const noexcept override
    {
        return agplayer::TimePitchEngineKind::None;
    }
};

class RecordingPassThroughEngine final : public agplayer::ITimePitchEngine {
public:
    bool configure(int, int channels) override
    {
        channels_ = channels;
        std::lock_guard<std::mutex> lock(recording_mutex);
        recording_configure_thread = std::this_thread::get_id();
        return channels_ > 0;
    }
    bool setTempoRatio(double) override { return true; }
    bool setPitchCents(double) override { return true; }
    bool setRateRatio(double) override { return true; }
    bool setFormantPreservation(bool) override { return true; }
    void put(const float* samples, const std::size_t frames) override
    {
        pending_.insert(pending_.end(), samples,
                        samples + frames * static_cast<std::size_t>(channels_));
    }
    std::size_t receive(float* samples, const std::size_t frames) override
    {
        const std::size_t available = pending_.size()
            / static_cast<std::size_t>(channels_);
        const std::size_t count = std::min(frames, available);
        const std::size_t sampleCount =
            count * static_cast<std::size_t>(channels_);
        std::copy_n(pending_.begin(), sampleCount, samples);
        pending_.erase(pending_.begin(), pending_.begin()
            + static_cast<std::ptrdiff_t>(sampleCount));
        return count;
    }
    void flush() override {}
    void reset() override { pending_.clear(); }
    bool failed() const noexcept override { return false; }
    agplayer::TimePitchEngineKind kind() const noexcept override
    {
        return agplayer::TimePitchEngineKind::None;
    }

private:
    int channels_{1};
    std::vector<float> pending_;
};

class RatioLatencyEngine final : public agplayer::ITimePitchEngine {
public:
    bool configure(int, int channels) override
    {
        channels_ = channels;
        return channels_ > 0;
    }
    bool setTempoRatio(const double ratio) override
    {
        ratio_ = ratio;
        return ratio_ > 0.0;
    }
    bool setPitchCents(double) override { return true; }
    bool setRateRatio(double) override { return true; }
    bool setFormantPreservation(bool) override { return true; }
    void put(const float*, const std::size_t frames) override
    {
        input_frames_ += frames;
    }
    std::size_t receive(float* samples, const std::size_t frames) override
    {
        const std::size_t eligibleInput = flushed_
            ? input_frames_
            : input_frames_ > latency_frames
                ? input_frames_ - latency_frames : 0U;
        const std::size_t targetOutput = static_cast<std::size_t>(
            std::floor(static_cast<double>(eligibleInput) / ratio_));
        const std::size_t count = std::min(
            frames, targetOutput > emitted_frames_
                ? targetOutput - emitted_frames_ : 0U);
        std::fill_n(samples,
                    count * static_cast<std::size_t>(channels_), 0.0F);
        emitted_frames_ += count;
        return count;
    }
    void flush() override { flushed_ = true; }
    void reset() override
    {
        input_frames_ = 0U;
        emitted_frames_ = 0U;
        flushed_ = false;
    }
    bool failed() const noexcept override { return false; }
    agplayer::TimePitchEngineKind kind() const noexcept override
    {
        return agplayer::TimePitchEngineKind::None;
    }

private:
    static constexpr std::size_t latency_frames = 4'096U;
    int channels_{1};
    double ratio_{1.0};
    std::size_t input_frames_{};
    std::size_t emitted_frames_{};
    bool flushed_{};
};

std::unique_ptr<agplayer::ITimePitchEngine> createBlockLatencyEngine()
{
    return std::make_unique<BlockLatencyEngine>();
}

std::unique_ptr<agplayer::ITimePitchEngine> createRejectingEngine()
{
    return std::make_unique<RejectingEngine>();
}

std::unique_ptr<agplayer::ITimePitchEngine> createFailOnPutEngine()
{
    return std::make_unique<FailOnPutEngine>();
}

std::unique_ptr<agplayer::ITimePitchEngine> createRecordingEngine()
{
    return std::make_unique<RecordingPassThroughEngine>();
}

std::unique_ptr<agplayer::ITimePitchEngine> createBlockingPassThroughEngine()
{
    return std::make_unique<BlockingPassThroughEngine>();
}

std::unique_ptr<agplayer::ITimePitchEngine> createRatioLatencyEngine()
{
    return std::make_unique<RatioLatencyEngine>();
}

class RampStream final : public agplayer::IAudioStreamSource {
public:
    RampStream()
    {
        metadata_.sample_rate = sample_rate;
        metadata_.channels = 1;
        metadata_.duration_ms = 4'000;
    }

    const agplayer::MediaMetadata& metadata() const noexcept override
    {
        return metadata_;
    }

    ag_result read(agplayer::DecodedAudioBlock& block) noexcept override
    {
        constexpr std::int64_t totalFrames = sample_rate * 4;
        const std::size_t frames = static_cast<std::size_t>(
            std::max<std::int64_t>(0, std::min<std::int64_t>(
                256, totalFrames - position_)));
        block = {};
        block.frames = frames;
        block.timestamp_frame = position_;
        block.timestamp_ms = position_ * 1'000 / sample_rate;
        block.samples.resize(frames);
        for (std::size_t frame = 0; frame < frames; ++frame) {
            block.samples[frame] = static_cast<float>(
                position_ + static_cast<std::int64_t>(frame))
                / static_cast<float>(totalFrames);
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
        seek_count_.fetch_add(1U, std::memory_order_relaxed);
        position_ = positionMs * sample_rate / 1'000;
        return AG_OK;
    }

    [[nodiscard]] std::size_t seek_count() const noexcept
    {
        return seek_count_.load(std::memory_order_relaxed);
    }

private:
    agplayer::MediaMetadata metadata_;
    std::int64_t position_{};
    std::atomic<std::size_t> seek_count_{0U};
};

class SineStream final : public agplayer::IAudioStreamSource {
public:
    SineStream()
    {
        metadata_.sample_rate = sample_rate;
        metadata_.channels = 1;
        metadata_.duration_ms = 4'000;
    }

    const agplayer::MediaMetadata& metadata() const noexcept override
    {
        return metadata_;
    }

    ag_result read(agplayer::DecodedAudioBlock& block) noexcept override
    {
        constexpr std::int64_t total_frames = sample_rate * 4;
        const std::size_t frames = static_cast<std::size_t>(
            std::max<std::int64_t>(0, std::min<std::int64_t>(
                2'048, total_frames - position_)));
        block = {};
        block.frames = frames;
        block.timestamp_frame = position_;
        block.timestamp_ms = position_ * 1'000 / sample_rate;
        block.samples.resize(frames);
        for (std::size_t frame = 0U; frame < frames; ++frame) {
            block.samples[frame] = static_cast<float>(0.5 * std::sin(
                2.0 * pi * 440.0
                * static_cast<double>(position_ + static_cast<std::int64_t>(frame))
                / sample_rate));
        }
        position_ += static_cast<std::int64_t>(frames);
        block.end_of_stream = position_ >= total_frames;
        return AG_OK;
    }

    ag_result seek(const std::int64_t positionMs) noexcept override
    {
        if (positionMs < 0 || positionMs > metadata_.duration_ms) {
            return AG_INVALID_ARGUMENT;
        }
        position_ = positionMs * sample_rate / 1'000;
        return AG_OK;
    }

private:
    agplayer::MediaMetadata metadata_;
    std::int64_t position_{};
};

class IrregularTimestampStream final : public agplayer::IAudioStreamSource {
public:
    IrregularTimestampStream()
    {
        metadata_.sample_rate = sample_rate;
        metadata_.channels = 1;
        metadata_.duration_ms = 1'000;
    }

    const agplayer::MediaMetadata& metadata() const noexcept override
    {
        return metadata_;
    }

    ag_result read(agplayer::DecodedAudioBlock& block) noexcept override
    {
        constexpr std::array<std::size_t, 3> sizes{137U, 2'003U, 17U};
        constexpr std::array<std::int64_t, 3> timestamps{0, 1'000, 4'096};
        block = {};
        if (index_ >= sizes.size()) {
            block.end_of_stream = true;
            return AG_OK;
        }
        block.frames = sizes[index_];
        block.timestamp_frame = timestamps[index_];
        block.timestamp_ms = timestamps[index_] * 1'000 / sample_rate;
        block.samples.resize(block.frames);
        for (std::size_t frame = 0U; frame < block.frames; ++frame) {
            block.samples[frame] = static_cast<float>(
                block.timestamp_frame + static_cast<std::int64_t>(frame));
        }
        ++index_;
        block.end_of_stream = index_ == sizes.size();
        return AG_OK;
    }

    ag_result seek(std::int64_t positionMs) noexcept override
    {
        if (positionMs != 0) return AG_INVALID_ARGUMENT;
        index_ = 0U;
        return AG_OK;
    }

private:
    agplayer::MediaMetadata metadata_;
    std::size_t index_{};
};

void wait_for_buffer(agplayer::AudioEngine& engine,
                     const std::size_t minimumFrames)
{
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(3);
    while (engine.buffered_frames() < minimumFrames
           && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(engine.buffered_frames() >= minimumFrames,
            "decode thread did not prepare enough PCM");
}

void write_pcm16_wav(const std::filesystem::path& path,
                     const std::vector<std::int16_t>& samples,
                     const int rate)
{
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(
        samples.size() * sizeof(std::int16_t));
    const std::uint32_t riffBytes = 36U + dataBytes;
    const std::uint16_t format = 1U;
    const std::uint16_t channels = 1U;
    const std::uint32_t byteRate = static_cast<std::uint32_t>(
        rate * channels * sizeof(std::int16_t));
    const std::uint16_t blockAlign = channels * sizeof(std::int16_t);
    const std::uint16_t bitsPerSample = 16U;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    require(output.good(), "temporary WAV could not be created");
    output.write("RIFF", 4);
    output.write(reinterpret_cast<const char*>(&riffBytes), sizeof(riffBytes));
    output.write("WAVEfmt ", 8);
    const std::uint32_t fmtBytes = 16U;
    output.write(reinterpret_cast<const char*>(&fmtBytes), sizeof(fmtBytes));
    output.write(reinterpret_cast<const char*>(&format), sizeof(format));
    output.write(reinterpret_cast<const char*>(&channels), sizeof(channels));
    output.write(reinterpret_cast<const char*>(&rate), sizeof(rate));
    output.write(reinterpret_cast<const char*>(&byteRate), sizeof(byteRate));
    output.write(reinterpret_cast<const char*>(&blockAlign), sizeof(blockAlign));
    output.write(reinterpret_cast<const char*>(&bitsPerSample),
                 sizeof(bitsPerSample));
    output.write("data", 4);
    output.write(reinterpret_cast<const char*>(&dataBytes), sizeof(dataBytes));
    output.write(reinterpret_cast<const char*>(samples.data()), dataBytes);
    require(output.good(), "temporary WAV write failed");
}

} // namespace

int main(const int argc, char** argv)
{
    require(argc == 2, "playback test fixture path missing");
    auto preferred = agplayer::create_time_pitch_engine();
    require(preferred != nullptr
                && preferred->configure(sample_rate, 1),
            "preferred time/pitch engine did not configure");
    require(preferred->kind() == agplayer::TimePitchEngineKind::Signalsmith,
            "Signalsmith is not the preferred engine");

    auto fallback = agplayer::create_preferred_time_pitch_engine(
        std::make_unique<RejectingEngine>());
    require(fallback != nullptr && fallback->configure(sample_rate, 1),
            "SoundTouch fallback did not configure");
    require(fallback->kind() == agplayer::TimePitchEngineKind::SoundTouch,
            "configure failure did not select SoundTouch fallback");

    auto overflowing = agplayer::create_signalsmith_time_pitch_engine();
    require(overflowing != nullptr
                && overflowing->configure(sample_rate, 1),
            "Signalsmith FIFO test did not configure");
    const std::vector<float> tooManyFrames(131'073U, 0.0F);
    overflowing->put(tooManyFrames.data(), tooManyFrames.size());
    require(overflowing->failed(),
            "Signalsmith FIFO overflow was not reported as failure");

    for (const std::size_t shortFrames : {5'000U, 8'192U}) {
        constexpr int shortRate = 44'100;
        constexpr int shortChannels = 2;
        std::vector<float> shortStereo(shortFrames * shortChannels);
        for (std::size_t frame = 0U; frame < shortFrames; ++frame) {
            const float sample = static_cast<float>(0.5 * std::sin(
                2.0 * pi * 440.0 * static_cast<double>(frame) / shortRate));
            shortStereo[frame * shortChannels] = sample;
            shortStereo[frame * shortChannels + 1U] = -sample;
        }
        agplayer::PlaybackTimePitchStage shortStage;
        require(shortStage.configure(shortRate, shortChannels, {0.75, true})
                    == AG_OK,
                "short stereo stage configure failed");
        std::vector<float> shortOutput;
        require(shortStage.process(shortStereo.data(), shortFrames,
                                   shortOutput) == AG_OK,
                "short stereo stage process failed");
        require(shortStage.finish(shortOutput) == AG_OK,
                "short stereo stage flush failed");
        const std::size_t expectedFrames = static_cast<std::size_t>(
            std::llround(static_cast<double>(shortFrames) / 0.75));
        const std::size_t actualFrames = shortOutput.size() / shortChannels;
        const auto shortError = std::llabs(
            static_cast<long long>(actualFrames)
            - static_cast<long long>(expectedFrames));
        if (shortError > 1LL) {
            std::cerr << "short_frames input=" << shortFrames
                      << " expected=" << expectedFrames
                      << " actual=" << actualFrames << '\n';
        }
        require(shortError <= 1LL,
                "short stereo stage truncated startup-latency audio");
    }

    agplayer::PlaybackTimePitchStage failingStage(&createFailOnPutEngine);
    require(failingStage.configure(sample_rate, 1, {0.75, true}) == AG_OK,
            "failure-propagation stage configure failed");
    const std::vector<float> failingInput(256U, 0.0F);
    std::vector<float> failingOutput;
    require(failingStage.process(failingInput.data(), failingInput.size(),
                                 failingOutput) == AG_INTERNAL_ERROR,
            "stage silently treated engine FIFO failure as EOS");

    agplayer::PlaybackTimePitchStage boundedMappingStage(
        &createRatioLatencyEngine);
    require(boundedMappingStage.configure(sample_rate, 1, {0.75, true})
                == AG_OK,
            "bounded mapping stage configure failed");
    constexpr std::size_t mappingChunk = 4'096U;
    constexpr std::size_t mappingChunks = 257U;
    std::vector<float> mappingInput(mappingChunk, 0.0F);
    std::vector<float> mappingOutput;
    std::vector<std::int64_t> longMapping;
    std::size_t maxPendingSize = 0U;
    std::size_t maxPendingCapacity = 0U;
    std::size_t maxPendingLive = 0U;
    for (std::size_t chunk = 0U; chunk < mappingChunks; ++chunk) {
        const std::int64_t start = static_cast<std::int64_t>(
            chunk * mappingChunk);
        require(boundedMappingStage.process(
                    mappingInput.data(), mappingInput.size(), start,
                    start + static_cast<std::int64_t>(mappingChunk),
                    mappingOutput, longMapping) == AG_OK,
                "long mapping stage process failed");
        maxPendingSize = std::max(
            maxPendingSize,
            agplayer::PlaybackTimePitchStageTestAccess::pendingSize(
                boundedMappingStage));
        maxPendingCapacity = std::max(
            maxPendingCapacity,
            agplayer::PlaybackTimePitchStageTestAccess::pendingCapacity(
                boundedMappingStage));
        maxPendingLive = std::max(
            maxPendingLive,
            agplayer::PlaybackTimePitchStageTestAccess::pendingLive(
                boundedMappingStage));
    }
    require(maxPendingLive <= 4'355U,
            "long mapping test did not maintain fixed algorithm latency");
    require(maxPendingSize <= 32'768U && maxPendingCapacity <= 32'768U,
            "source mapping FIFO retained consumed long-stream history");
    require(boundedMappingStage.finish(mappingOutput, longMapping) == AG_OK,
            "long mapping stage flush failed");
    require(!longMapping.empty()
                && longMapping.back()
                    == static_cast<std::int64_t>(mappingChunk * mappingChunks),
            "bounded source mapping lost final source progress");

    agplayer::TimePitchEngineKind kind{};
    bool bypassed = false;
    const auto slow = process(0.75, true, kind, bypassed);
    require(!bypassed && kind == agplayer::TimePitchEngineKind::Signalsmith,
            "0.75x did not use Signalsmith");
    require(std::llabs(static_cast<long long>(slow.size()) - 128'000LL)
                <= 1'280LL,
            "0.75x duration error exceeded one percent");

    const auto normal = process(1.0, true, kind, bypassed);
    require(bypassed && kind == agplayer::TimePitchEngineKind::None,
            "1.00x did not fully bypass the DSP");
    require(normal.size() == 96'000U,
            "1.00x duration was not exact");

    const auto fast = process(1.5, true, kind, bypassed);
    require(!bypassed && kind == agplayer::TimePitchEngineKind::Signalsmith,
            "1.50x did not use Signalsmith");
    require(std::llabs(static_cast<long long>(fast.size()) - 64'000LL)
                <= 640LL,
            "1.50x duration error exceeded one percent");
    const double preservedFrequency = estimate_frequency(fast);
    require(std::abs(cents_between(preservedFrequency, 440.0)) <= 5.0,
            "keep-pitch frequency error exceeded five cents");

    const auto vinyl = process(1.5, false, kind, bypassed);
    const double vinylFrequency = estimate_frequency(vinyl);
    require(std::abs(cents_between(vinylFrequency, 660.0)) <= 5.0,
            "vinyl rate did not shift pitch with speed");

    agplayer::AudioEngine engine(agplayer::AudioBackend::Manual, 262'144U);
    require(engine.set_time_pitch({1.5, true}) == AG_OK,
            "audio engine rejected time/pitch config");
    require(engine.load(argv[1]) == AG_OK,
            "audio engine file load failed");
    require(engine.play() == AG_OK, "audio engine play failed");
    const std::size_t engineRate = static_cast<std::size_t>(
        engine.snapshot().sample_rate);
    wait_for_buffer(engine, engineRate);
    std::vector<float> rendered(2U * engineRate);
    engine.render(rendered.data(), engineRate);
    auto snapshot = engine.snapshot();
    const std::int64_t firstSourcePosition = snapshot.position_ms;
    require(std::llabs(snapshot.position_ms - 1'500LL) <= 2LL,
            "snapshot position was derived from stretched output time");

    require(engine.seek(1'000) == AG_OK, "source-time seek failed");
    snapshot = engine.snapshot();
    const std::int64_t seekSourcePosition = snapshot.position_ms;
    require(std::llabs(snapshot.position_ms - 1'000LL) <= 2LL,
            "seek did not publish the requested source time");
    const std::size_t tenthSecond = engineRate / 10U;
    wait_for_buffer(engine, tenthSecond);
    rendered.assign(2U * tenthSecond, 0.0F);
    engine.render(rendered.data(), tenthSecond);
    snapshot = engine.snapshot();
    const std::int64_t postSeekSourcePosition = snapshot.position_ms;
    require(std::llabs(snapshot.position_ms - 1'150LL) <= 2LL,
            "post-seek source-time mapping drifted");

    agplayer::AudioEngine irregularEngine(
        agplayer::AudioBackend::Manual, 16'384U, &createBlockLatencyEngine);
    require(irregularEngine.set_time_pitch({1.5, true}) == AG_OK,
            "irregular mapping engine rejected config");
    require(irregularEngine.load_stream(
                std::make_shared<IrregularTimestampStream>()) == AG_OK,
            "irregular timestamp stream load failed");
    require(irregularEngine.play() == AG_OK,
            "irregular timestamp stream play failed");
    wait_for_buffer(irregularEngine, 2'157U);
    rendered.assign(2'157U, 0.0F);
    irregularEngine.render(rendered.data(), rendered.size());
    const auto irregularSnapshot = irregularEngine.snapshot();
    require(irregularSnapshot.position_ms == 85,
            "position ignored explicit irregular source timestamps");
    require(irregularSnapshot.duration_ms == 85,
            "EOF duration was not taken from raw source timestamps");

    auto directStreamSource = std::make_shared<RampStream>();
    agplayer::AudioEngine directStreamTempo(
        agplayer::AudioBackend::Manual, 4'096U, &createRecordingEngine);
    require(directStreamTempo.load_stream(directStreamSource) == AG_OK,
            "direct stream tempo boundary fixture failed to load");
    require(directStreamTempo.set_time_pitch({1.25, true})
                == AG_INVALID_ARGUMENT,
            "active stream accepted an unbounded dynamic tempo transaction");
    require(directStreamSource->seek_count() == 0U,
            "rejected active-stream tempo transaction invoked stream seek");
    require(directStreamTempo.time_pitch_config().speed_ratio == 1.0,
            "rejected active-stream tempo transaction changed config");

    const std::filesystem::path transactionDirectory =
        std::filesystem::temp_directory_path()
        / "agplayer-task2-fix4-transactions";
    std::filesystem::create_directories(transactionDirectory);
    const std::filesystem::path transactionRampPath =
        transactionDirectory / "transaction-ramp.wav";
    std::vector<std::int16_t> transactionRamp(4'096U);
    for (std::size_t frame = 0U; frame < transactionRamp.size(); ++frame) {
        transactionRamp[frame] = static_cast<std::int16_t>(frame);
    }
    write_pcm16_wav(transactionRampPath, transactionRamp, sample_rate);

    agplayer::AudioEngine rejectingEngine(
        agplayer::AudioBackend::Manual, 16'384U, &createRejectingEngine);
    require(rejectingEngine.load(argv[1]) == AG_OK,
            "transaction fixture did not load at bypass speed");
    const auto beforeRejectedChange = rejectingEngine.time_pitch_config();
    require(rejectingEngine.set_time_pitch({1.25, false})
                == AG_INTERNAL_ERROR,
            "failing decoder reconfigure was not propagated");
    const auto afterRejectedChange = rejectingEngine.time_pitch_config();
    require(afterRejectedChange.speed_ratio == beforeRejectedChange.speed_ratio
                && afterRejectedChange.keep_pitch
                    == beforeRejectedChange.keep_pitch,
            "failed time/pitch transaction changed published config");
    require(rejectingEngine.snapshot().state == agplayer::EngineState::Stopped,
            "failed time/pitch transaction poisoned the playback session");

    agplayer::AudioEngine threadedChange(
        agplayer::AudioBackend::Manual, 4'096U, &createRecordingEngine);
    require(threadedChange.load(transactionRampPath.string()) == AG_OK,
            "running transaction fixture failed to load");
    require(threadedChange.play() == AG_OK,
            "running transaction fixture failed to play");
    wait_for_buffer(threadedChange, 2'048U);
    const std::thread::id callerThread = std::this_thread::get_id();
    const ag_result threadedChangeResult =
        threadedChange.set_time_pitch({1.25, true});
    if (threadedChangeResult != AG_OK) {
        std::cerr << "running_time_pitch_result="
                  << static_cast<int>(threadedChangeResult) << '\n';
    }
    require(threadedChangeResult == AG_OK,
            "running time/pitch transaction failed");
    {
        std::lock_guard<std::mutex> lock(recording_mutex);
        require(recording_configure_thread != std::thread::id{}
                    && recording_configure_thread != callerThread,
                "running time/pitch configure did not execute on decode thread");
    }

    agplayer::AudioEngine startupChange(
        agplayer::AudioBackend::Manual, 4'096U, &createRecordingEngine);
    require(startupChange.load(transactionRampPath.string()) == AG_OK,
            "startup transaction fixture failed to load");
    {
        std::lock_guard<std::mutex> lock(recording_mutex);
        recording_configure_thread = {};
    }
    const std::thread::id startupCaller = std::this_thread::get_id();
    require(startupChange.set_time_pitch({1.25, true}) == AG_OK,
            "startup time/pitch transaction failed");
    {
        std::lock_guard<std::mutex> lock(recording_mutex);
        require(recording_configure_thread != std::thread::id{}
                    && recording_configure_thread != startupCaller,
                "startup time/pitch configure ran on caller thread");
    }

    agplayer::AudioEngine timedOutChange(
        agplayer::AudioBackend::Manual, 1'024U,
        &createBlockingPassThroughEngine);
    require(timedOutChange.load(transactionRampPath.string()) == AG_OK,
            "timeout transaction fixture failed to load");
    require(timedOutChange.play() == AG_OK,
            "timeout transaction fixture failed to play");
    wait_for_buffer(timedOutChange, 1'024U);
    std::vector<float> timedOutPrefix(128U);
    timedOutChange.render(timedOutPrefix.data(), timedOutPrefix.size());
    {
        std::lock_guard<std::mutex> lock(blocking_configure_mutex);
        blocking_configure_entered = false;
        blocking_configure_release = false;
        blocking_configure_thread = {};
    }
    ag_result timedOutResult = AG_OK;
    std::atomic<bool> timedOutSetterReturned{false};
    std::atomic<std::int64_t> timedOutSetterElapsedMs{0};
    const std::thread::id timedOutCaller = std::this_thread::get_id();
    std::thread timedOutSetter([&] {
        const auto started = std::chrono::steady_clock::now();
        timedOutResult = timedOutChange.set_time_pitch({1.25, true});
        timedOutSetterElapsedMs.store(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count(),
            std::memory_order_release);
        timedOutSetterReturned.store(true, std::memory_order_release);
    });
    {
        std::unique_lock<std::mutex> lock(blocking_configure_mutex);
        require(blocking_configure_cv.wait_for(
                    lock, std::chrono::seconds(2), [] {
                        return blocking_configure_entered;
                    }),
                "timed-out candidate did not enter configure");
    }
    const auto setterDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(3);
    while (!timedOutSetterReturned.load(std::memory_order_acquire)
           && std::chrono::steady_clock::now() < setterDeadline) {
        std::this_thread::yield();
    }
    const bool returnedWhileConfigureBlocked =
        timedOutSetterReturned.load(std::memory_order_acquire);
    if (returnedWhileConfigureBlocked) {
        timedOutSetter.join();
    }
    {
        std::lock_guard<std::mutex> lock(blocking_configure_mutex);
        require(!returnedWhileConfigureBlocked || !blocking_configure_release,
                "time/pitch blocker was released before the setter returned");
        blocking_configure_release = true;
    }
    blocking_configure_cv.notify_all();
    if (!returnedWhileConfigureBlocked) {
        timedOutSetter.join();
    }
    require(returnedWhileConfigureBlocked,
            "time/pitch setter waited indefinitely after its deadline");
    require(timedOutSetterElapsedMs.load(std::memory_order_acquire) < 3'000,
            "time/pitch setter exceeded its bounded return deadline");
    require(timedOutResult == AG_CANCELLED,
            "timed-out decode-thread candidate did not report cancellation");
    require(blocking_configure_thread != std::thread::id{}
                && blocking_configure_thread != timedOutCaller,
            "timed-out candidate configure ran on caller thread");
    const auto cleanupDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(2);
    while (!agplayer::AudioEngineTestAccess::timePitchRequestIdle(
               timedOutChange)
           && std::chrono::steady_clock::now() < cleanupDeadline) {
        std::this_thread::yield();
    }
    require(agplayer::AudioEngineTestAccess::timePitchRequestIdle(
                timedOutChange),
            "timed-out time/pitch request did not finish safe cleanup");
    require(timedOutChange.time_pitch_config().speed_ratio == 1.0,
            "timed-out accepting candidate committed after setter return");
    for (std::int64_t expectedFrame = 128; expectedFrame < 1'408;
         ++expectedFrame) {
        wait_for_buffer(timedOutChange, 1U);
        float sample = -1.0F;
        timedOutChange.render(&sample, 1U);
        const float expected = static_cast<float>(expectedFrame) / 32'768.0F;
        require(std::abs(sample - expected) < 0.000001F,
                "timed-out time/pitch request repeated or skipped PCM");
    }

    agplayer::AudioEngine generationMatchedChange(
        agplayer::AudioBackend::Manual, 4'096U, &createRecordingEngine);
    require(generationMatchedChange.load(transactionRampPath.string()) == AG_OK,
            "generation-matched transaction fixture failed to load");
    TimePitchAbaGate generationGate;
    agplayer::AudioEngineTestAccess::setTimePitchTestHook(
        generationMatchedChange, &time_pitch_aba_hook, &generationGate);
    ag_result firstGenerationResult = AG_INTERNAL_ERROR;
    std::thread firstGenerationSetter([&] {
        firstGenerationResult =
            generationMatchedChange.set_time_pitch({1.10, true});
        {
            std::lock_guard<std::mutex> lock(generationGate.mutex);
            generationGate.first_setter_returned = true;
        }
        generationGate.cv.notify_all();
    });
    {
        std::unique_lock<std::mutex> lock(generationGate.mutex);
        require(generationGate.cv.wait_for(
                    lock, std::chrono::seconds(3), [&generationGate] {
                        return generationGate.first_completion_entered;
                    }),
                "first generation did not reach its completion barrier");
        require(generationGate.cv.wait_for(
                    lock, std::chrono::seconds(3), [&generationGate] {
                        return generationGate.first_setter_returned;
                    }),
                "first generation setter did not observe completion");
    }
    firstGenerationSetter.join();
    const std::uint64_t requestFirstGeneration =
        generationGate.first_generation;
    const std::uint64_t completedAfterFirst =
        agplayer::AudioEngineTestAccess::completedTimePitchGeneration(
            generationMatchedChange);

    ag_result secondGenerationResult = AG_INTERNAL_ERROR;
    std::thread secondGenerationSetter([&] {
        secondGenerationResult =
            generationMatchedChange.set_time_pitch({1.20, true});
        {
            std::lock_guard<std::mutex> lock(generationGate.mutex);
            generationGate.second_setter_returned = true;
        }
        generationGate.cv.notify_all();
    });
    {
        std::unique_lock<std::mutex> lock(generationGate.mutex);
        require(generationGate.cv.wait_for(
                    lock, std::chrono::seconds(2), [&generationGate] {
                        return generationGate.second_request_entered;
                    }),
                "second generation did not reach request publication");
        generationGate.release_first_completion = true;
        generationGate.cv.notify_all();
        require(generationGate.cv.wait_for(
                    lock, std::chrono::seconds(1), [&generationGate] {
                        return generationGate.first_trailing_publish;
                    }),
                "first generation did not reach its trailing publish");
        generationGate.release_second_request = true;
        generationGate.cv.notify_all();
        require(generationGate.cv.wait_for(
                    lock, std::chrono::seconds(2), [&generationGate] {
                        return generationGate.second_decode_entered;
                    }),
                "second generation did not reach decode barrier");
    }
    bool secondReturnedBeforeItsCompletion = false;
    {
        std::unique_lock<std::mutex> lock(generationGate.mutex);
        secondReturnedBeforeItsCompletion = generationGate.cv.wait_for(
            lock, std::chrono::milliseconds(250), [&generationGate] {
                return generationGate.second_setter_returned;
            });
        generationGate.release_second_decode = true;
    }
    generationGate.cv.notify_all();
    secondGenerationSetter.join();
    const std::uint64_t requestSecondGeneration =
        generationGate.second_generation;
    const std::uint64_t completedAfterSecond =
        agplayer::AudioEngineTestAccess::completedTimePitchGeneration(
            generationMatchedChange);
    const bool idleAfterSecond =
        agplayer::AudioEngineTestAccess::timePitchRequestIdle(
            generationMatchedChange);
    const ag_result thirdGenerationResult =
        generationMatchedChange.set_time_pitch({1.30, true});
    const std::uint64_t requestThirdGeneration =
        agplayer::AudioEngineTestAccess::requestedTimePitchGeneration(
            generationMatchedChange);
    const std::uint64_t completedAfterThird =
        agplayer::AudioEngineTestAccess::completedTimePitchGeneration(
            generationMatchedChange);
    agplayer::AudioEngineTestAccess::setTimePitchTestHook(
        generationMatchedChange, nullptr, nullptr);
    require(!secondReturnedBeforeItsCompletion,
            "second time/pitch setter consumed the first generation completion");
    require(firstGenerationResult == AG_OK
                && completedAfterFirst == requestFirstGeneration,
            "first time/pitch result was not paired with its generation");
    require(secondGenerationResult == AG_OK
                && completedAfterSecond == requestSecondGeneration,
            "second time/pitch result was not paired with its generation");
    require(idleAfterSecond,
            "second time/pitch transaction did not return to Idle");
    require(thirdGenerationResult == AG_OK
                && requestThirdGeneration > requestSecondGeneration
                && completedAfterThird == requestThirdGeneration,
            "a completed second transaction did not permit a third generation");

    agplayer::AudioEngine commitMatchedChange(
        agplayer::AudioBackend::Manual, 4'096U, &createRecordingEngine);
    require(commitMatchedChange.load(transactionRampPath.string()) == AG_OK,
            "commit-matched transaction fixture failed to load");
    TimePitchCommitGate commitGate;
    agplayer::AudioEngineTestAccess::setTimePitchTestHook(
        commitMatchedChange, &time_pitch_commit_hook, &commitGate);
    ag_result commitMatchedResult = AG_INTERNAL_ERROR;
    double ratioAtCommitSetterReturn = 0.0;
    std::thread commitMatchedSetter([&] {
        commitMatchedResult =
            commitMatchedChange.set_time_pitch({1.40, true});
        ratioAtCommitSetterReturn =
            commitMatchedChange.time_pitch_config().speed_ratio;
        {
            std::lock_guard<std::mutex> lock(commitGate.mutex);
            commitGate.setter_returned = true;
        }
        commitGate.cv.notify_all();
    });
    bool returnedBeforeCommitRelease = false;
    {
        std::unique_lock<std::mutex> lock(commitGate.mutex);
        require(commitGate.cv.wait_for(
                    lock, std::chrono::seconds(2), [&commitGate] {
                        return commitGate.commit_entered;
                    }),
                "time/pitch transaction did not reach commit barrier");
        returnedBeforeCommitRelease = commitGate.cv.wait_for(
            lock, std::chrono::milliseconds(2'300), [&commitGate] {
                return commitGate.setter_returned;
            });
        commitGate.release_commit = true;
    }
    commitGate.cv.notify_all();
    commitMatchedSetter.join();
    const auto commitCleanupDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(2);
    while (!agplayer::AudioEngineTestAccess::timePitchRequestIdle(
               commitMatchedChange)
           && std::chrono::steady_clock::now() < commitCleanupDeadline) {
        std::this_thread::yield();
    }
    agplayer::AudioEngineTestAccess::setTimePitchTestHook(
        commitMatchedChange, nullptr, nullptr);
    const bool idleAfterCommit =
        agplayer::AudioEngineTestAccess::timePitchRequestIdle(
            commitMatchedChange);
    const double ratioAfterCommitCleanup =
        commitMatchedChange.time_pitch_config().speed_ratio;
    const ag_result adjacentCommitResult =
        commitMatchedChange.set_time_pitch({1.30, true});
    const double adjacentCommitRatio =
        commitMatchedChange.time_pitch_config().speed_ratio;
    require(!returnedBeforeCommitRelease || commitMatchedResult != AG_OK,
            "time/pitch setter reported success before commit publication");
    require((commitMatchedResult == AG_OK
             && std::abs(ratioAtCommitSetterReturn - 1.40) < 0.000001)
                || (commitMatchedResult == AG_CANCELLED
                    && std::abs(ratioAtCommitSetterReturn - 1.0) < 0.000001
                    && std::abs(ratioAfterCommitCleanup - 1.0) < 0.000001),
            "deadline result did not match the transaction's visible config");
    require(idleAfterCommit && adjacentCommitResult == AG_OK
                && std::abs(adjacentCommitRatio - 1.30) < 0.000001,
            "completed commit did not immediately accept the next request");

    agplayer::AudioEngine executingDeadlineChange(
        agplayer::AudioBackend::Manual, 4'096U, &createRecordingEngine);
    require(executingDeadlineChange.load(transactionRampPath.string()) == AG_OK,
            "executing deadline fixture failed to load");
    TimePitchExecutingGate executingGate;
    agplayer::AudioEngineTestAccess::setTimePitchTestHook(
        executingDeadlineChange, &time_pitch_executing_hook, &executingGate);
    ag_result executingDeadlineResult = AG_INTERNAL_ERROR;
    std::thread executingDeadlineSetter([&] {
        executingDeadlineResult =
            executingDeadlineChange.set_time_pitch({1.35, true});
        {
            std::lock_guard<std::mutex> lock(executingGate.mutex);
            executingGate.setter_returned = true;
        }
        executingGate.cv.notify_all();
    });
    bool executingSetterReturnedWhileBlocked = false;
    {
        std::unique_lock<std::mutex> lock(executingGate.mutex);
        require(executingGate.cv.wait_for(
                    lock, std::chrono::seconds(2), [&executingGate] {
                        return executingGate.executing_entered;
                    }),
                "time/pitch transaction did not reach executing barrier");
        executingSetterReturnedWhileBlocked = executingGate.cv.wait_for(
            lock, std::chrono::milliseconds(2'300), [&executingGate] {
                return executingGate.setter_returned;
            });
        executingGate.release_executing = true;
    }
    executingGate.cv.notify_all();
    executingDeadlineSetter.join();
    const auto executingCleanupDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(2);
    while (!agplayer::AudioEngineTestAccess::timePitchRequestIdle(
               executingDeadlineChange)
           && std::chrono::steady_clock::now() < executingCleanupDeadline) {
        std::this_thread::yield();
    }
    agplayer::AudioEngineTestAccess::setTimePitchTestHook(
        executingDeadlineChange, nullptr, nullptr);
    const double ratioAfterExecutingCleanup =
        executingDeadlineChange.time_pitch_config().speed_ratio;
    require(executingSetterReturnedWhileBlocked,
            "time/pitch setter waited unbounded after execution won before timeline acquisition");
    require(executingDeadlineResult == AG_CANCELLED
                && std::abs(ratioAfterExecutingCleanup - 1.0) < 0.000001,
            "executing deadline cancellation applied config after returning");

    agplayer::AudioEngine lostRequestChange(
        agplayer::AudioBackend::Manual, 4'096U, &createRecordingEngine);
    require(lostRequestChange.load(transactionRampPath.string()) == AG_OK,
            "device-loss request fixture failed to load");
    const std::uint64_t generationBeforeLoss =
        agplayer::AudioEngineTestAccess::requestedTimePitchGeneration(
            lostRequestChange);
    agplayer::AudioEngineTestAccess::markDeviceLost(lostRequestChange);
    const auto lostRequestStart = std::chrono::steady_clock::now();
    const ag_result lostRequestResult =
        lostRequestChange.set_time_pitch({1.25, true});
    const auto lostRequestElapsed = std::chrono::steady_clock::now()
        - lostRequestStart;
    const std::uint64_t generationAfterLoss =
        agplayer::AudioEngineTestAccess::requestedTimePitchGeneration(
            lostRequestChange);
    require(lostRequestResult == AG_DEVICE_ERROR
                && lostRequestElapsed < std::chrono::milliseconds(250)
                && generationAfterLoss == generationBeforeLoss,
            "device-lost time/pitch request was not rejected before publication");

    agplayer::AudioEngine abandonedLossChange(
        agplayer::AudioBackend::Manual, 4'096U, &createRecordingEngine);
    require(abandonedLossChange.load(transactionRampPath.string()) == AG_OK,
            "abandoned device-loss fixture failed to load");
    TimePitchPendingLossGate pendingLossGate;
    agplayer::AudioEngineTestAccess::setTimePitchTestHook(
        abandonedLossChange, &time_pitch_pending_loss_hook, &pendingLossGate);
    ag_result abandonedLossResult = AG_INTERNAL_ERROR;
    std::thread abandonedLossSetter([&] {
        abandonedLossResult =
            abandonedLossChange.set_time_pitch({1.25, true});
    });
    {
        std::unique_lock<std::mutex> lock(pendingLossGate.mutex);
        require(pendingLossGate.cv.wait_for(
                    lock, std::chrono::seconds(2), [&pendingLossGate] {
                        return pendingLossGate.request_entered;
                    }),
                "device-loss transaction did not reach Pending");
        agplayer::AudioEngineTestAccess::markDeviceLost(abandonedLossChange);
        pendingLossGate.release_request = true;
    }
    pendingLossGate.cv.notify_all();
    abandonedLossSetter.join();
    agplayer::AudioEngineTestAccess::setTimePitchTestHook(
        abandonedLossChange, nullptr, nullptr);
    const std::uint64_t abandonedLossGeneration =
        agplayer::AudioEngineTestAccess::requestedTimePitchGeneration(
            abandonedLossChange);
    const ag_result abandonedRecoveryResult =
        abandonedLossChange.retry_device();
    const bool abandonedMailboxClear =
        agplayer::AudioEngineTestAccess::timePitchMailboxClear(
            abandonedLossChange);
    const std::uint64_t abandonedCompletedGeneration =
        agplayer::AudioEngineTestAccess::completedTimePitchGeneration(
            abandonedLossChange);
    const ag_result recoveredSeekResult = abandonedLossChange.seek(0);
    const ag_result recoveredTempoResult =
        abandonedLossChange.set_time_pitch({1.20, true});
    require(abandonedLossResult == AG_CANCELLED
                && abandonedRecoveryResult == AG_OK
                && abandonedMailboxClear
                && abandonedCompletedGeneration == abandonedLossGeneration,
            "device recovery did not retire the abandoned time/pitch request");
    require(recoveredSeekResult == AG_OK && recoveredTempoResult == AG_OK,
            "device recovery did not accept a new seek and tempo request");

    agplayer::AudioEngine abandonedExitChange(
        agplayer::AudioBackend::Manual, 4'096U, &createRecordingEngine);
    require(abandonedExitChange.load(transactionRampPath.string()) == AG_OK,
            "abandoned decode-exit fixture failed to load");
    TimePitchPendingLossGate pendingExitGate;
    agplayer::AudioEngineTestAccess::setTimePitchTestHook(
        abandonedExitChange, &time_pitch_pending_loss_hook, &pendingExitGate);
    ag_result abandonedExitResult = AG_INTERNAL_ERROR;
    std::thread abandonedExitSetter([&] {
        abandonedExitResult =
            abandonedExitChange.set_time_pitch({1.25, true});
    });
    {
        std::unique_lock<std::mutex> lock(pendingExitGate.mutex);
        require(pendingExitGate.cv.wait_for(
                    lock, std::chrono::seconds(2), [&pendingExitGate] {
                        return pendingExitGate.request_entered;
                    }),
                "decode-exit transaction did not reach Pending");
    }
    agplayer::AudioEngineTestAccess::requestDecodeExit(abandonedExitChange);
    const auto decodeExitDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(2);
    while (agplayer::AudioEngineTestAccess::decodeRunning(abandonedExitChange)
           && std::chrono::steady_clock::now() < decodeExitDeadline) {
        std::this_thread::yield();
    }
    {
        std::lock_guard<std::mutex> lock(pendingExitGate.mutex);
        pendingExitGate.release_request = true;
    }
    pendingExitGate.cv.notify_all();
    abandonedExitSetter.join();
    agplayer::AudioEngineTestAccess::setTimePitchTestHook(
        abandonedExitChange, nullptr, nullptr);
    const std::uint64_t abandonedExitGeneration =
        agplayer::AudioEngineTestAccess::requestedTimePitchGeneration(
            abandonedExitChange);
    const bool abandonedExitMailboxClear =
        agplayer::AudioEngineTestAccess::timePitchMailboxClear(
            abandonedExitChange);
    const bool abandonedDecodeStopped =
        !agplayer::AudioEngineTestAccess::decodeRunning(abandonedExitChange);
    const std::uint64_t abandonedExitCompletedGeneration =
        agplayer::AudioEngineTestAccess::completedTimePitchGeneration(
            abandonedExitChange);
    const ag_result afterExitTempoResult =
        abandonedExitChange.set_time_pitch({1.20, true});
    require(abandonedExitResult == AG_CANCELLED
                && abandonedDecodeStopped
                && abandonedExitMailboxClear
                && abandonedExitCompletedGeneration == abandonedExitGeneration,
            "decode-thread exit did not retire the abandoned time/pitch request");
    require(afterExitTempoResult == AG_OK,
            "decode-thread restart did not accept a new tempo request");

    const std::filesystem::path retiredFirstPath =
        transactionDirectory / "retired-first.wav";
    const std::filesystem::path retiredSecondPath =
        transactionDirectory / "retired-second.wav";
    write_pcm16_wav(retiredFirstPath,
                    std::vector<std::int16_t>(
                        static_cast<std::size_t>(sample_rate) * 2U, 4'096),
                    sample_rate);
    write_pcm16_wav(retiredSecondPath,
                    std::vector<std::int16_t>(
                        static_cast<std::size_t>(sample_rate) * 2U, -4'096),
                    sample_rate);
    agplayer::AudioEngine retiredDrainChange(
        agplayer::AudioBackend::Manual, 4'096U, &createRecordingEngine);
    require(retiredDrainChange.set_queue(
                {retiredFirstPath.string(), retiredSecondPath.string()}, 0U)
                == AG_OK,
            "retired decoder fixture failed to load");
    require(retiredDrainChange.set_time_pitch({1.25, true}) == AG_OK,
            "retired decoder fixture failed to change tempo");
    require(agplayer::AudioEngineTestAccess::hasRetiredTimePitchDecoder(
                retiredDrainChange),
            "tempo commit did not retain the old decoder outside commit");
    require(retiredDrainChange.next() == AG_OK,
            "retired decoder fixture failed to switch track");
    const bool retainedAfterSwitch =
        agplayer::AudioEngineTestAccess::hasRetiredTimePitchDecoder(
            retiredDrainChange);
    std::error_code retiredRemoveError;
    const bool retiredFirstRemoved =
        std::filesystem::remove(retiredFirstPath, retiredRemoveError);
    require(!retainedAfterSwitch,
            "track switch retained the previous time/pitch decoder");
    require(retiredFirstRemoved && !retiredRemoveError
                && !std::filesystem::exists(retiredFirstPath),
            "track switch did not release the previous file for deletion");
    require(retiredDrainChange.set_time_pitch({1.25, true}) == AG_OK,
            "recovery drain fixture failed to change tempo");
    require(agplayer::AudioEngineTestAccess::hasRetiredTimePitchDecoder(
                retiredDrainChange),
            "recovery drain fixture did not retain the replaced decoder");
    retiredDrainChange.simulate_device_loss();
    require(retiredDrainChange.retry_device() == AG_OK,
            "retired decoder recovery fixture failed to recover");
    require(!agplayer::AudioEngineTestAccess::hasRetiredTimePitchDecoder(
                retiredDrainChange),
            "device recovery retained the previous time/pitch decoder");

    agplayer::AudioEngine endedDecodeChange(
        agplayer::AudioBackend::Manual, 262'144U, &createRecordingEngine);
    require(endedDecodeChange.load(transactionRampPath.string()) == AG_OK,
            "ended decode-thread fixture failed to load");
    wait_for_buffer(endedDecodeChange, transactionRamp.size());
    agplayer::AudioEngineTestAccess::stopDecodeThread(endedDecodeChange);
    require(!agplayer::AudioEngineTestAccess::decodeRunning(endedDecodeChange),
            "EOF decode thread test stop did not complete");
    require(endedDecodeChange.play() == AG_OK,
            "ended decode-thread fixture failed to play buffered PCM");
    std::vector<float> endedPrefix(144U);
    endedDecodeChange.render(endedPrefix.data(), endedPrefix.size());
    {
        std::lock_guard<std::mutex> lock(recording_mutex);
        recording_configure_thread = {};
    }
    const std::thread::id endedCaller = std::this_thread::get_id();
    require(endedDecodeChange.set_time_pitch({1.25, true}) == AG_OK,
            "ended decode-thread time/pitch transaction failed");
    {
        std::lock_guard<std::mutex> lock(recording_mutex);
        require(recording_configure_thread != std::thread::id{}
                    && recording_configure_thread != endedCaller,
                "ended-session configure ran on caller thread");
    }
    for (std::int64_t expectedFrame = 144; expectedFrame < 1'168;
         ++expectedFrame) {
        wait_for_buffer(endedDecodeChange, 1U);
        float sample = -1.0F;
        endedDecodeChange.render(&sample, 1U);
        const float expected = static_cast<float>(expectedFrame) / 32'768.0F;
        require(std::abs(sample - expected) < 0.000001F,
                "ended-session restart repeated or skipped PCM");
    }

    agplayer::AudioEngine continuousFailure(
        agplayer::AudioBackend::Manual, 1'024U, &createRejectingEngine);
    require(continuousFailure.load(transactionRampPath.string()) == AG_OK,
            "failure continuity fixture failed to load");
    require(continuousFailure.play() == AG_OK,
            "failure continuity fixture failed to play");
    wait_for_buffer(continuousFailure, 1'024U);
    std::vector<float> rampOutput(128U);
    continuousFailure.render(rampOutput.data(), rampOutput.size());
    require(continuousFailure.set_time_pitch({1.25, true})
                == AG_INTERNAL_ERROR,
            "running configure failure was not propagated");
    for (std::int64_t expectedFrame = 128; expectedFrame < 1'408;
         ++expectedFrame) {
        wait_for_buffer(continuousFailure, 1U);
        float sample = -1.0F;
        continuousFailure.render(&sample, 1U);
        const float expected = static_cast<float>(expectedFrame) / 32'768.0F;
        if (std::abs(sample - expected) >= 0.000001F) {
            std::cerr << "continuity_frame expected=" << expectedFrame
                      << " sample=" << sample
                      << " expected_sample=" << expected
                      << " position_ms="
                      << continuousFailure.snapshot().position_ms << '\n';
        }
        require(std::abs(sample - expected) < 0.000001F,
                "failed time/pitch transaction repeated or skipped PCM");
    }

    agplayer::AudioEngine editorLease(
        agplayer::AudioBackend::Manual, 1'024U);
    require(editorLease.load_stream(std::make_shared<RampStream>(), true)
                == AG_OK,
            "editor lease fixture failed to load");
    require(editorLease.play() == AG_OK,
            "editor lease fixture failed to play");
    wait_for_buffer(editorLease, 1'024U);
    require(editorLease.set_time_pitch({1.25, true}) == AG_INVALID_ARGUMENT,
            "editor lease did not reject direct tempo change");
    std::vector<float> editorDrain(1'024U);
    editorLease.render(editorDrain.data(), editorDrain.size());
    wait_for_buffer(editorLease, 512U);

    const std::filesystem::path transitionDirectory =
        std::filesystem::temp_directory_path()
        / "agplayer-task2-fix3-transition";
    std::filesystem::create_directories(transitionDirectory);
    const std::filesystem::path firstTransitionPath =
        transitionDirectory / "first-positive.wav";
    const std::filesystem::path secondTransitionPath =
        transitionDirectory / "second-negative.wav";
    write_pcm16_wav(firstTransitionPath,
                    std::vector<std::int16_t>(1'024U, 8'192), sample_rate);
    write_pcm16_wav(secondTransitionPath,
                    std::vector<std::int16_t>(16'384U, -16'384), sample_rate);

    const std::filesystem::path eqFirstTransitionPath =
        transitionDirectory / "eq-first-impulse.wav";
    const std::filesystem::path eqReferenceFirstPath =
        transitionDirectory / "eq-first-silence.wav";
    const std::filesystem::path eqSecondTransitionPath =
        transitionDirectory / "eq-second-impulse.wav";
    std::vector<std::int16_t> eqFirstSamples(64U, 0);
    eqFirstSamples.back() = 16'384;
    std::vector<std::int16_t> eqSecondSamples(128U, 0);
    eqSecondSamples.front() = 8'192;
    write_pcm16_wav(eqFirstTransitionPath, eqFirstSamples, sample_rate);
    write_pcm16_wav(eqReferenceFirstPath,
                    std::vector<std::int16_t>(64U, 0), sample_rate);
    write_pcm16_wav(eqSecondTransitionPath, eqSecondSamples, sample_rate);

    agplayer::GraphicEqSettings boundaryEqSettings{};
    boundaryEqSettings.auto_clip_protection = false;
    boundaryEqSettings.transition_ms = 1.0;
    boundaryEqSettings.band_gain_db[0] = 12.0;

    agplayer::AudioEngine eqBoundary(
        agplayer::AudioBackend::Manual, 512U);
    require(eqBoundary.set_queue(
                {eqFirstTransitionPath.string(),
                 eqSecondTransitionPath.string()}, 0U) == AG_OK,
            "EQ boundary queue load failed");
    require(eqBoundary.set_equalizer(boundaryEqSettings, 1U) == AG_OK,
            "EQ boundary setup failed");
    require(eqBoundary.play() == AG_OK,
            "EQ boundary playback failed");
    const auto eqBoundaryDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(3);
    while ((agplayer::AudioEngineTestAccess::pendingBoundary(eqBoundary)
                != 64
            || eqBoundary.buffered_frames() < 192U)
           && std::chrono::steady_clock::now() < eqBoundaryDeadline) {
        std::this_thread::yield();
    }
    require(agplayer::AudioEngineTestAccess::pendingBoundary(eqBoundary)
                == 64,
            "EQ next track was not prepared at the expected boundary");
    require(eqBoundary.buffered_frames() >= 192U,
            "EQ cross-boundary callback was not fully buffered");
    const std::uint64_t eqPendingGeneration =
        agplayer::AudioEngineTestAccess::pendingMapperGeneration(eqBoundary);
    std::vector<float> eqCrossBoundary(192U, 0.0F);
    eqBoundary.render(eqCrossBoundary.data(), eqCrossBoundary.size());

    agplayer::AudioEngine eqReference(
        agplayer::AudioBackend::Manual, 512U);
    require(eqReference.set_queue(
                {eqReferenceFirstPath.string(),
                 eqSecondTransitionPath.string()}, 0U) == AG_OK,
            "EQ boundary reference load failed");
    require(eqReference.set_equalizer(boundaryEqSettings, 1U) == AG_OK,
            "EQ boundary reference setup failed");
    require(eqReference.play() == AG_OK,
            "EQ boundary reference playback failed");
    const auto eqReferenceDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(3);
    while ((agplayer::AudioEngineTestAccess::pendingBoundary(eqReference)
                != 64
            || eqReference.buffered_frames() < 192U)
           && std::chrono::steady_clock::now() < eqReferenceDeadline) {
        std::this_thread::yield();
    }
    require(agplayer::AudioEngineTestAccess::pendingBoundary(eqReference)
                == 64,
            "EQ boundary reference did not prepare the next track");
    std::vector<float> eqFreshTrack(192U, 0.0F);
    eqReference.render(eqFreshTrack.data(), eqFreshTrack.size());
    require(std::abs(eqFreshTrack[64U]) > 0.01F,
            "EQ boundary reference did not contain next-track PCM");
    for (std::size_t frame = 0U; frame < eqSecondSamples.size(); ++frame) {
        if (std::abs(eqCrossBoundary[64U + frame]
                     - eqFreshTrack[64U + frame])
            >= 0.000001F) {
            std::cerr << "eq_boundary_frame=" << frame
                      << " cross=" << eqCrossBoundary[64U + frame]
                      << " fresh=" << eqFreshTrack[64U + frame] << '\n';
        }
        require(std::abs(eqCrossBoundary[64U + frame]
                         - eqFreshTrack[64U + frame])
                    < 0.000001F,
                "next track inherited the previous track EQ state");
    }
    require(eqBoundary.snapshot().track_index == 1U,
            "EQ cross-boundary callback did not publish the next track");
    require(agplayer::AudioEngineTestAccess::publishedMapperGeneration(
                eqBoundary) == eqPendingGeneration,
            "EQ cross-boundary callback published the wrong generation");
    require(agplayer::AudioEngineTestAccess::consumedSourceFrame(eqBoundary)
                == static_cast<std::int64_t>(eqSecondSamples.size()),
            "EQ cross-boundary callback published the wrong source frame");

    agplayer::AudioEngine prefetchedFailure(
        agplayer::AudioBackend::Manual, 2'048U, &createRejectingEngine);
    require(prefetchedFailure.set_queue(
                {firstTransitionPath.string(), secondTransitionPath.string()},
                0U) == AG_OK,
            "prefetched failure queue load failed");
    require(prefetchedFailure.play() == AG_OK,
            "prefetched failure playback failed");
    wait_for_buffer(prefetchedFailure, 2'048U);
    const auto prefetchDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(3);
    while (agplayer::AudioEngineTestAccess::pendingBoundary(prefetchedFailure)
               < 0
           && std::chrono::steady_clock::now() < prefetchDeadline) {
        std::this_thread::yield();
    }
    require(agplayer::AudioEngineTestAccess::pendingBoundary(prefetchedFailure)
                == 1'024,
            "next track was not prefetched before its render boundary");
    const std::uint64_t firstGeneration =
        agplayer::AudioEngineTestAccess::publishedMapperGeneration(
            prefetchedFailure);
    const std::uint64_t pendingGeneration =
        agplayer::AudioEngineTestAccess::pendingMapperGeneration(
            prefetchedFailure);
    require(pendingGeneration != firstGeneration,
            "prefetched track did not prepare a distinct mapper generation");
    require(prefetchedFailure.snapshot().track_index == 0U,
            "prefetch published the next track before render boundary");
    require(prefetchedFailure.set_time_pitch({1.25, true})
                == AG_INTERNAL_ERROR,
            "prefetched rejecting candidate was not propagated");
    std::vector<float> prefetchedPcm(8'192U, 0.0F);
    std::size_t renderedPrefetched = 0U;
    while (renderedPrefetched < prefetchedPcm.size()) {
        wait_for_buffer(prefetchedFailure, 1U);
        const std::size_t count = std::min<std::size_t>(
            128U, prefetchedPcm.size() - renderedPrefetched);
        prefetchedFailure.render(prefetchedPcm.data() + renderedPrefetched,
                                 count);
        renderedPrefetched += count;
    }
    for (std::size_t frame = 0U; frame < 1'024U; ++frame) {
        require(prefetchedPcm[frame] > 0.20F,
                "first track PCM was corrupted before pending boundary");
    }
    for (std::size_t frame = 1'024U; frame < prefetchedPcm.size(); ++frame) {
        if (prefetchedPcm[frame] >= -0.40F) {
            std::cerr << "prefetch_pollution_frame=" << frame
                      << " sample=" << prefetchedPcm[frame] << '\n';
        }
        require(prefetchedPcm[frame] < -0.40F,
                "failed tempo candidate replayed old-track PCM after boundary");
    }
    require(prefetchedFailure.snapshot().track_index == 1U,
            "failed tempo candidate corrupted published track index");
    require(agplayer::AudioEngineTestAccess::publishedMapperGeneration(
                prefetchedFailure) == pendingGeneration,
            "failed candidate changed the prepared boundary generation");
    require(agplayer::AudioEngineTestAccess::consumedSourceFrame(
                prefetchedFailure) == 7'168,
            "failed tempo candidate corrupted next-track source progress");

    agplayer::AudioEngine manualBoundary(
        agplayer::AudioBackend::Manual, 262'144U);
    require(manualBoundary.set_queue({argv[1], argv[1]}, 0U) == AG_OK,
            "manual boundary queue load failed");
    require(manualBoundary.set_time_pitch({1.25, false}) == AG_OK,
            "manual boundary tempo setup failed");
    require(manualBoundary.next() == AG_OK,
            "manual track transition failed");
    const auto manualBoundaryConfig = manualBoundary.time_pitch_config();
    require(manualBoundaryConfig.speed_ratio == 1.0,
            "manual track boundary did not reset tempo before decode");

    for (const agplayer::PlaybackMode mode : {
             agplayer::PlaybackMode::Sequential,
             agplayer::PlaybackMode::RepeatAll,
             agplayer::PlaybackMode::Shuffle}) {
        agplayer::AudioEngine automaticBoundary(
            agplayer::AudioBackend::Manual, 262'144U);
        require(automaticBoundary.set_queue(
                    {firstTransitionPath.string(),
                     secondTransitionPath.string()}, 0U) == AG_OK,
                "automatic boundary queue load failed");
        require(automaticBoundary.set_mode(mode) == AG_OK,
                "automatic boundary mode setup failed");
        require(automaticBoundary.set_time_pitch({1.5, true}) == AG_OK,
                "automatic boundary tempo setup failed");
        require(automaticBoundary.play() == AG_OK,
                "automatic boundary playback failed");
        const auto automaticDeadline = std::chrono::steady_clock::now()
            + std::chrono::seconds(3);
        while (agplayer::AudioEngineTestAccess::pendingBoundary(
                   automaticBoundary) < 0
               && std::chrono::steady_clock::now() < automaticDeadline) {
            std::this_thread::yield();
        }
        const std::int64_t boundary =
            agplayer::AudioEngineTestAccess::pendingBoundary(
                automaticBoundary);
        require(boundary > 0,
                "automatic next track was not prepared");
        require(!agplayer::AudioEngineTestAccess::hasRetiredTimePitchDecoder(
                    automaticBoundary),
                "automatic track preparation retained the previous decoder");
        const std::uint64_t nextGeneration =
            agplayer::AudioEngineTestAccess::pendingMapperGeneration(
                automaticBoundary);
        require(automaticBoundary.time_pitch_config().speed_ratio == 1.5,
                "automatic prefetch published next-track tempo too early");
        std::vector<float> boundaryPcm(static_cast<std::size_t>(boundary));
        automaticBoundary.render(boundaryPcm.data(), boundaryPcm.size());
        require(automaticBoundary.snapshot().track_index == 1U,
                "automatic boundary did not publish the next track");
        require(automaticBoundary.time_pitch_config().speed_ratio == 1.0,
                "automatic track boundary did not atomically publish 1.00x");
        require(agplayer::AudioEngineTestAccess::publishedMapperGeneration(
                    automaticBoundary) == nextGeneration,
                "automatic boundary published the wrong mapper generation");
        wait_for_buffer(automaticBoundary, 1U);
        float nextSample = 0.0F;
        automaticBoundary.render(&nextSample, 1U);
        require(nextSample < -0.40F,
                "automatic boundary did not start with next-track PCM");
    }

    const std::filesystem::path repeatTransitionPath =
        transitionDirectory / "repeat-ramp.wav";
    std::vector<std::int16_t> repeatRamp(4'096U);
    for (std::size_t frame = 0U; frame < repeatRamp.size(); ++frame) {
        repeatRamp[frame] = static_cast<std::int16_t>(4'096U + frame);
    }
    write_pcm16_wav(repeatTransitionPath, repeatRamp, sample_rate);
    agplayer::AudioEngine repeatOneBoundary(
        agplayer::AudioBackend::Manual, 262'144U);
    require(repeatOneBoundary.set_queue({repeatTransitionPath.string()}, 0U)
                == AG_OK,
            "repeat-one boundary queue load failed");
    require(repeatOneBoundary.set_mode(agplayer::PlaybackMode::RepeatOne)
                == AG_OK,
            "repeat-one mode setup failed");
    require(repeatOneBoundary.set_time_pitch({1.5, true}) == AG_OK,
            "repeat-one tempo setup failed");
    require(repeatOneBoundary.play() == AG_OK,
            "repeat-one playback failed");
    const auto repeatDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(3);
    while (agplayer::AudioEngineTestAccess::pendingBoundary(
               repeatOneBoundary) < 0
           && std::chrono::steady_clock::now() < repeatDeadline) {
        std::this_thread::yield();
    }
    const std::int64_t repeatBoundary =
        agplayer::AudioEngineTestAccess::pendingBoundary(repeatOneBoundary);
    require(repeatBoundary > 0,
            "repeat-one restart was not prepared");
    require(!agplayer::AudioEngineTestAccess::hasRetiredTimePitchDecoder(
                repeatOneBoundary),
            "repeat-one restart retained the previous decoder");
    const std::uint64_t repeatGeneration =
        agplayer::AudioEngineTestAccess::pendingMapperGeneration(
            repeatOneBoundary);
    std::vector<float> repeatPcm(
        static_cast<std::size_t>(repeatBoundary));
    repeatOneBoundary.render(repeatPcm.data(), repeatPcm.size());
    require(repeatOneBoundary.time_pitch_config().speed_ratio == 1.0,
            "repeat-one restart did not reset tempo at its boundary");
    require(agplayer::AudioEngineTestAccess::publishedMapperGeneration(
                repeatOneBoundary) == repeatGeneration,
            "repeat-one published the wrong mapper generation");
    wait_for_buffer(repeatOneBoundary, 1U);
    float repeatedFirstSample = 0.0F;
    repeatOneBoundary.render(&repeatedFirstSample, 1U);
    require(std::abs(repeatedFirstSample - 0.125F) < 0.0001F,
            "repeat-one restart did not begin at the first source sample");

    agplayer::AudioEngine concurrentEngine(
        agplayer::AudioBackend::Manual, 262'144U);
    require(concurrentEngine.load(argv[1]) == AG_OK,
            "concurrent snapshot fixture failed to load");
    std::atomic<bool> snapshotsDone{false};
    std::atomic<bool> snapshotTorn{false};
    std::thread snapshotReader([&] {
        while (!snapshotsDone.load(std::memory_order_acquire)) {
            const auto concurrentSnapshot = concurrentEngine.snapshot();
            const auto concurrentConfig = concurrentEngine.time_pitch_config();
            const bool configValid =
                (concurrentConfig.speed_ratio == 1.0
                 && concurrentConfig.keep_pitch)
                || (concurrentConfig.speed_ratio == 0.75
                    && !concurrentConfig.keep_pitch)
                || (concurrentConfig.speed_ratio == 1.5
                    && concurrentConfig.keep_pitch);
            if (!configValid || concurrentSnapshot.track_count != 1U
                || concurrentSnapshot.track_index != 0U
                || concurrentSnapshot.position_ms < 0
                || concurrentSnapshot.position_ms
                    > concurrentSnapshot.duration_ms) {
                snapshotTorn.store(true, std::memory_order_release);
            }
        }
    });
    for (int iteration = 0; iteration < 20; ++iteration) {
        const agplayer::PlaybackTimePitchConfig config =
            (iteration & 1) == 0
            ? agplayer::PlaybackTimePitchConfig{0.75, false}
            : agplayer::PlaybackTimePitchConfig{1.5, true};
        require(concurrentEngine.set_time_pitch(config) == AG_OK,
                "concurrent time/pitch transaction failed");
        require(concurrentEngine.seek((iteration & 1) == 0 ? 250 : 1'250)
                    == AG_OK,
                "concurrent source seek failed");
    }
    snapshotsDone.store(true, std::memory_order_release);
    snapshotReader.join();
    require(!snapshotTorn.load(std::memory_order_acquire),
            "snapshot observed a torn timeline/config transaction");

    std::cout << "duration_frames slow=" << slow.size()
              << " normal=" << normal.size()
              << " fast=" << fast.size() << '\n';
    std::cout << "frequency_hz keep_pitch=" << preservedFrequency
              << " vinyl=" << vinylFrequency << '\n';
    std::cout << "source_position_ms first=" << firstSourcePosition
              << " seek=" << seekSourcePosition
              << " post_seek=" << postSeekSourcePosition << '\n';
    std::cout << "irregular_source_position_ms="
              << irregularSnapshot.position_ms
              << " duration_ms=" << irregularSnapshot.duration_ms << '\n';
    return 0;
}
