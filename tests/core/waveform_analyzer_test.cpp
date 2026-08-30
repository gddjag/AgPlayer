// Test files deliberately keep assert() active even in Release builds: many
// test cases embed function calls with side effects inside assert(), and
// silencing them under NDEBUG would skip those calls and crash on cleanup.
#undef NDEBUG

#include <agplayer/c_api.h>

#include "decoder.hpp"
#include "waveform_analyzer.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void test_actual_frame_bucketing_and_channel_combination()
{
    agplayer::WaveformBucketizer bucketizer(8U, 3U, 2U, 48000.0F);
    const std::vector<float> samples{
        0.1F, -0.6F,
        0.2F, 0.1F,
        -0.3F, 0.2F,
        0.4F, 0.1F,
        0.2F, -0.8F,
        0.1F, 0.2F,
        -0.2F, 0.5F,
        0.1F, -0.4F,
    };
    assert(samples.size() == 16U);
    assert(bucketizer.add(samples, 8U) == AG_OK);
    std::vector<float> peaks;
    std::vector<float> bass;
    std::vector<float> mid;
    std::vector<float> high;
    assert(bucketizer.finish(peaks, bass, mid, high) == AG_OK);
    assert(peaks.size() == 3U);
    assert(std::abs(peaks[0] - 0.75F) < 0.000'001F);
    assert(std::abs(peaks[1] - 1.0F) < 0.000'001F);
    assert(std::abs(peaks[2] - 0.625F) < 0.000'001F);
    assert(bass.size() == 3U);
    assert(mid.size() == 3U);
    assert(high.size() == 3U);

    agplayer::WaveformBucketizer more_points_than_frames(3U, 10U, 1U, 48000.0F);
    assert(more_points_than_frames.add({0.2F, 0.4F, 0.8F}, 3U)
           == AG_OK);
    assert(more_points_than_frames.finish(peaks, bass, mid, high) == AG_OK);
    assert(peaks.size() == 3U);
    assert(std::abs(peaks[0] - 0.25F) < 0.000'001F);
    assert(std::abs(peaks[1] - 0.5F) < 0.000'001F);
    assert(std::abs(peaks[2] - 1.0F) < 0.000'001F);
}

void test_non_finite_pcm_is_rejected()
{
    for (const float invalid : {
             std::numeric_limits<float>::quiet_NaN(),
             std::numeric_limits<float>::infinity(),
         }) {
        agplayer::WaveformBucketizer bucketizer(1U, 1U, 2U, 48000.0F);
        assert(bucketizer.add({0.5F, invalid}, 1U) == AG_DECODE_ERROR);
        std::vector<float> peaks{1.0F};
        std::vector<float> bass{1.0F};
        std::vector<float> mid{1.0F};
        std::vector<float> high{1.0F};
        assert(bucketizer.finish(peaks, bass, mid, high) == AG_DECODE_ERROR);
        assert(peaks.empty());
        assert(bass.empty());
        assert(mid.empty());
        assert(high.empty());
    }
}

void test_average_absolute_and_rms_aggregation()
{
    const std::vector<float> samples{0.0F, 1.0F, 0.5F, 0.5F};
    std::vector<float> peaks;
    std::vector<float> bass;
    std::vector<float> mid;
    std::vector<float> high;

    agplayer::WaveformBucketizer average(
        4U, 2U, 1U, 48000.0F,
        agplayer::WaveformAggregation::AverageAbsolute);
    assert(average.add(samples, 4U) == AG_OK);
    assert(average.finish(peaks, bass, mid, high) == AG_OK);
    assert(peaks.size() == 2U);
    assert(std::abs(peaks[0] - 1.0F) < 0.000'001F);
    assert(std::abs(peaks[1] - 1.0F) < 0.000'001F);

    agplayer::WaveformBucketizer rms(
        4U, 2U, 1U, 48000.0F,
        agplayer::WaveformAggregation::Rms);
    assert(rms.add(samples, 4U) == AG_OK);
    assert(rms.finish(peaks, bass, mid, high) == AG_OK);
    assert(peaks.size() == 2U);
    assert(std::abs(peaks[0] - 1.0F) < 0.000'001F);
    assert(std::abs(peaks[1] - std::sqrt(0.5F)) < 0.000'001F);
}

void test_frequency_layers_preserve_cross_band_energy()
{
    constexpr std::size_t frames = 4'800U;
    constexpr float sample_rate = 48'000.0F;
    constexpr double pi = 3.14159265358979323846;
    std::vector<float> samples(frames);
    for (std::size_t frame = 0U; frame < frames; ++frame) {
        samples[frame] = static_cast<float>(
            std::sin(2.0 * pi * 100.0 * static_cast<double>(frame)
                     / sample_rate));
    }

    agplayer::WaveformBucketizer bucketizer(
        frames, 1U, 1U, sample_rate, agplayer::WaveformAggregation::Rms);
    assert(bucketizer.add(samples, frames) == AG_OK);
    std::vector<float> peaks;
    std::vector<float> bass;
    std::vector<float> mid;
    std::vector<float> high;
    assert(bucketizer.finish(peaks, bass, mid, high) == AG_OK);

    assert(peaks.size() == 1U);
    assert(bass.size() == 1U);
    assert(mid.size() == 1U);
    assert(high.size() == 1U);
    assert(bass[0] > 0.8F);
    assert(mid[0] < bass[0] * 0.35F);
    assert(high[0] < bass[0] * 0.1F);
}

struct ProgressState final {
    std::vector<float> values;
    ag_cancel_token* token = nullptr;
    bool cancel_during_progress = false;
};

void record_progress(const float progress, void* user_data)
{
    auto& state = *static_cast<ProgressState*>(user_data);
    assert(std::isfinite(progress));
    assert(progress >= 0.0F && progress <= 1.0F);
    if (!state.values.empty()) {
        assert(progress >= state.values.back());
    }
    state.values.push_back(progress);
    if (state.cancel_during_progress && progress >= 0.2F) {
        ag_cancel_token_cancel(state.token);
    }
}

struct AsyncProgressState final {
    std::atomic_int calls{0};
    ag_cancel_token* token = nullptr;
};

void record_async_progress(const float progress, void* user_data)
{
    assert(std::isfinite(progress));
    auto& state = *static_cast<AsyncProgressState*>(user_data);
    state.calls.fetch_add(1, std::memory_order_relaxed);
}

void pause_and_cancel_reentrantly(const float, void* user_data)
{
    auto& state = *static_cast<AsyncProgressState*>(user_data);
    state.calls.fetch_add(1, std::memory_order_relaxed);
    ag_cancel_token_set_paused(state.token, 1);
    ag_cancel_token_cancel(state.token);
}

void throw_from_progress(const float, void*)
{
    throw std::runtime_error("callback failure");
}

void test_frequency_color_c_api(const std::string& source_path)
{
    ag_waveform* waveform = reinterpret_cast<ag_waveform*>(
        static_cast<std::uintptr_t>(1U));
    assert(ag_track_frequency_color_analysis(
               nullptr, 2'000U, nullptr, nullptr, nullptr, &waveform)
           == AG_INVALID_ARGUMENT);
    assert(waveform == nullptr);
    waveform = reinterpret_cast<ag_waveform*>(static_cast<std::uintptr_t>(1U));
    assert(ag_track_frequency_color_analysis(
               "", 2'000U, nullptr, nullptr, nullptr, &waveform)
           == AG_INVALID_ARGUMENT);
    assert(waveform == nullptr);
    waveform = reinterpret_cast<ag_waveform*>(static_cast<std::uintptr_t>(1U));
    assert(ag_track_frequency_color_analysis(
               source_path.c_str(), 0U, nullptr, nullptr, nullptr, &waveform)
           == AG_INVALID_ARGUMENT);
    assert(waveform == nullptr);
    assert(ag_track_frequency_color_analysis(
               source_path.c_str(), 2'000U, nullptr, nullptr, nullptr, nullptr)
           == AG_INVALID_ARGUMENT);

    const std::uint64_t opens_before = agplayer::Decoder::threadOpenCount();
    assert(ag_track_frequency_color_analysis(
               source_path.c_str(), 2'000U, nullptr, nullptr, nullptr, &waveform)
           == AG_OK);
    assert(agplayer::Decoder::threadOpenCount() - opens_before == 1U);
    assert(waveform != nullptr);
    for (const ag_waveform_layer layer : {
             AG_WAVEFORM_LAYER_MIX,
             AG_WAVEFORM_LAYER_BASS,
             AG_WAVEFORM_LAYER_MID,
             AG_WAVEFORM_LAYER_HIGH,
         }) {
        assert(ag_waveform_layer_count(waveform, layer) == 2'000U);
        for (std::size_t index = 0U; index < 2'000U; ++index) {
            const float value = ag_waveform_layer_peak(waveform, layer, index);
            assert(std::isfinite(value));
            assert(value >= 0.0F && value <= 1.0F);
        }
    }
    assert(ag_waveform_duration_ms(waveform) > 0U);
    assert(ag_waveform_total_samples(waveform) > 0U);
    assert(ag_waveform_sample_rate(waveform) > 0);
    assert(ag_waveform_bpm(waveform) == 0.0);
    ag_waveform_destroy(waveform);

    ag_cancel_token_set_paused(nullptr, 1);
    ag_cancel_token* token = ag_cancel_token_create();
    assert(token != nullptr);
    ag_cancel_token_set_paused(token, 1);
    AsyncProgressState progress;
    std::atomic<ag_waveform*> async_waveform{reinterpret_cast<ag_waveform*>(
        static_cast<std::uintptr_t>(1U))};
    auto resumed = std::async(std::launch::async, [&] {
        ag_waveform* result = reinterpret_cast<ag_waveform*>(
            static_cast<std::uintptr_t>(1U));
        const ag_result status = ag_track_frequency_color_analysis(
            source_path.c_str(), 2'000U, token, record_async_progress,
            &progress, &result);
        async_waveform.store(result, std::memory_order_relaxed);
        return status;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(progress.calls.load(std::memory_order_relaxed) == 0);
    assert(resumed.wait_for(std::chrono::milliseconds(0))
           == std::future_status::timeout);
    ag_cancel_token_set_paused(token, 0);
    if (resumed.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
        ag_cancel_token_cancel(token);
        assert(false && "resumed analysis timed out");
    }
    assert(resumed.get() == AG_OK);
    waveform = async_waveform.load(std::memory_order_relaxed);
    assert(waveform != nullptr);
    ag_waveform_destroy(waveform);
    ag_cancel_token_destroy(token);

    token = ag_cancel_token_create();
    assert(token != nullptr);
    ag_cancel_token_set_paused(token, 1);
    auto cancelled = std::async(std::launch::async, [&] {
        ag_waveform* result = reinterpret_cast<ag_waveform*>(
            static_cast<std::uintptr_t>(1U));
        const ag_result status = ag_track_frequency_color_analysis(
            source_path.c_str(), 2'000U, token, nullptr, nullptr, &result);
        async_waveform.store(result, std::memory_order_relaxed);
        return status;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ag_cancel_token_cancel(token);
    assert(cancelled.wait_for(std::chrono::seconds(2))
           == std::future_status::ready);
    assert(cancelled.get() == AG_CANCELLED);
    assert(async_waveform.load(std::memory_order_relaxed) == nullptr);
    ag_cancel_token_destroy(token);

    token = ag_cancel_token_create();
    assert(token != nullptr);
    ag_cancel_token_set_paused(token, 1);
    auto destroyed = std::async(std::launch::async, [&] {
        ag_waveform* result = reinterpret_cast<ag_waveform*>(
            static_cast<std::uintptr_t>(1U));
        const ag_result status = ag_track_frequency_color_analysis(
            source_path.c_str(), 2'000U, token, nullptr, nullptr, &result);
        async_waveform.store(result, std::memory_order_relaxed);
        return status;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ag_cancel_token_destroy(token);
    token = nullptr;
    assert(destroyed.wait_for(std::chrono::seconds(2))
           == std::future_status::ready);
    assert(destroyed.get() == AG_CANCELLED);
    assert(async_waveform.load(std::memory_order_relaxed) == nullptr);

    token = ag_cancel_token_create();
    assert(token != nullptr);
    AsyncProgressState reentrant;
    reentrant.token = token;
    waveform = reinterpret_cast<ag_waveform*>(static_cast<std::uintptr_t>(1U));
    assert(ag_track_frequency_color_analysis(
               source_path.c_str(), 2'000U, token,
               pause_and_cancel_reentrantly, &reentrant, &waveform)
           == AG_CANCELLED);
    assert(reentrant.calls.load(std::memory_order_relaxed) == 1);
    assert(waveform == nullptr);
    ag_cancel_token_destroy(token);

    waveform = reinterpret_cast<ag_waveform*>(static_cast<std::uintptr_t>(1U));
    assert(ag_track_frequency_color_analysis(
               source_path.c_str(), 2'000U, nullptr, throw_from_progress,
               nullptr, &waveform)
           == AG_INTERNAL_ERROR);
    assert(waveform == nullptr);
}

} // namespace

int main(const int argc, char** argv)
{
    assert(argc == 2);
    test_actual_frame_bucketing_and_channel_combination();
    test_non_finite_pcm_is_rejected();
    test_average_absolute_and_rms_aggregation();
    test_frequency_layers_preserve_cross_band_energy();
    const std::string source_path = argv[1];
    test_frequency_color_c_api(source_path);

    ag_waveform* waveform = reinterpret_cast<ag_waveform*>(
        static_cast<std::uintptr_t>(1U));
    assert(ag_waveform_analyze(nullptr, 512U, nullptr, nullptr, nullptr,
                               &waveform)
           == AG_INVALID_ARGUMENT);
    assert(waveform == nullptr);
    assert(ag_waveform_analyze(source_path.c_str(), 0U, nullptr, nullptr,
                               nullptr, &waveform)
           == AG_INVALID_ARGUMENT);
    assert(waveform == nullptr);
    assert(ag_waveform_analyze(source_path.c_str(), 512U, nullptr, nullptr,
                               nullptr, nullptr)
           == AG_INVALID_ARGUMENT);
    assert(ag_track_analysis_with_aggregation(
               source_path.c_str(), 512U,
               static_cast<ag_waveform_aggregation>(99),
               nullptr, nullptr, nullptr, &waveform, nullptr)
           == AG_INVALID_ARGUMENT);
    assert(waveform == nullptr);

    ProgressState progress;
    assert(ag_waveform_analyze(source_path.c_str(), 512U, nullptr,
                               record_progress, &progress, &waveform)
           == AG_OK);
    assert(waveform != nullptr);
    assert(ag_waveform_count(waveform) > 100U);
    assert(ag_waveform_count(waveform) <= 512U);
    assert(!progress.values.empty());
    assert(progress.values.front() == 0.0F);
    assert(progress.values.back() == 1.0F);

    float maximum = 0.0F;
    for (std::size_t index = 0U; index < ag_waveform_count(waveform); ++index) {
        const float peak = ag_waveform_peak(waveform, index);
        assert(std::isfinite(peak));
        assert(peak >= 0.0F && peak <= 1.0F);
        maximum = std::max(maximum, peak);
    }
    assert(std::abs(maximum - 1.0F) < 0.000'001F);
    assert(ag_waveform_peak(waveform, ag_waveform_count(waveform)) == 0.0F);
    ag_waveform_destroy(waveform);
    waveform = nullptr;

    ag_cancel_token* token = ag_cancel_token_create();
    assert(token != nullptr);
    ag_cancel_token_cancel(token);
    assert(ag_waveform_analyze(source_path.c_str(), 512U, token, nullptr,
                               nullptr, &waveform)
           == AG_CANCELLED);
    assert(waveform == nullptr);
    ag_cancel_token_destroy(token);

    token = ag_cancel_token_create();
    assert(token != nullptr);
    ProgressState cancelling_progress;
    cancelling_progress.token = token;
    cancelling_progress.cancel_during_progress = true;
    assert(ag_waveform_analyze(source_path.c_str(), 512U, token,
                               record_progress, &cancelling_progress, &waveform)
           == AG_CANCELLED);
    assert(waveform == nullptr);
    assert(!cancelling_progress.values.empty());
    assert(cancelling_progress.values.back() < 1.0F);
    ag_cancel_token_destroy(token);

    const std::filesystem::path missing_path =
        std::filesystem::path(source_path).parent_path()
        / "waveform-missing.wav";
    std::filesystem::remove(missing_path);
    assert(ag_waveform_analyze(missing_path.string().c_str(), 512U, nullptr,
                               nullptr, nullptr, &waveform)
           == AG_IO_ERROR);
    assert(waveform == nullptr);

    ag_waveform_destroy(nullptr);
    ag_cancel_token_cancel(nullptr);
    ag_cancel_token_destroy(nullptr);
}
