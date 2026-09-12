#undef NDEBUG

#include <agplayer/c_api.h>

#include "waveform_analyzer.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace {

void test_actual_frame_bucketing_and_channel_combination()
{
    agplayer::WaveformBucketizer bucketizer(8U, 3U, 2U, 48'000.0F);
    const std::vector<float> samples{
        0.1F, -0.6F, 0.2F, 0.1F, -0.3F, 0.2F, 0.4F, 0.1F,
        0.2F, -0.8F, 0.1F, 0.2F, -0.2F, 0.5F, 0.1F, -0.4F,
    };
    assert(bucketizer.add(samples, 8U) == AG_OK);
    std::vector<float> mix;
    std::vector<float> bass;
    std::vector<float> mid;
    std::vector<float> high;
    assert(bucketizer.finish(mix, bass, mid, high) == AG_OK);
    assert(mix.size() == 3U);
    assert(std::abs(mix[0] - 0.75F) < 0.000'001F);
    assert(std::abs(mix[1] - 1.0F) < 0.000'001F);
    assert(std::abs(mix[2] - 0.625F) < 0.000'001F);
    assert(bass.size() == mix.size());
    assert(mid.size() == mix.size());
    assert(high.size() == mix.size());
}

void test_non_finite_pcm_is_rejected()
{
    for (const float invalid : {
             std::numeric_limits<float>::quiet_NaN(),
             std::numeric_limits<float>::infinity(),
         }) {
        agplayer::WaveformBucketizer bucketizer(1U, 1U, 2U, 48'000.0F);
        assert(bucketizer.add({0.5F, invalid}, 1U) == AG_DECODE_ERROR);
        std::vector<float> mix{1.0F};
        std::vector<float> bass{1.0F};
        std::vector<float> mid{1.0F};
        std::vector<float> high{1.0F};
        assert(bucketizer.finish(mix, bass, mid, high) == AG_DECODE_ERROR);
        assert(mix.empty() && bass.empty() && mid.empty() && high.empty());
    }
}

void test_average_absolute_and_rms_aggregation()
{
    const std::vector<float> samples{0.0F, 1.0F, 0.5F, 0.5F};
    std::vector<float> mix;
    std::vector<float> bass;
    std::vector<float> mid;
    std::vector<float> high;
    agplayer::WaveformBucketizer average(
        4U, 2U, 1U, 48'000.0F,
        agplayer::WaveformAggregation::AverageAbsolute);
    assert(average.add(samples, 4U) == AG_OK);
    assert(average.finish(mix, bass, mid, high) == AG_OK);
    assert(mix == std::vector<float>({1.0F, 1.0F}));

    agplayer::WaveformBucketizer rms(
        4U, 2U, 1U, 48'000.0F,
        agplayer::WaveformAggregation::Rms);
    assert(rms.add(samples, 4U) == AG_OK);
    assert(rms.finish(mix, bass, mid, high) == AG_OK);
    assert(std::abs(mix[0] - 1.0F) < 0.000'001F);
    assert(std::abs(mix[1] - std::sqrt(0.5F)) < 0.000'001F);
}

void test_rgb_frequency_layers_keep_existing_behavior()
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
    std::vector<float> mix;
    std::vector<float> bass;
    std::vector<float> mid;
    std::vector<float> high;
    assert(bucketizer.finish(mix, bass, mid, high) == AG_OK);
    // Unit-amplitude sine: RMS is about 0.707 before lowpass attenuation.
    assert(bass[0] > 0.65F && bass[0] < 0.72F);
    assert(mid[0] < bass[0] * 0.35F);
    assert(high[0] < bass[0] * 0.1F);
}

struct ProgressState final {
    std::vector<float> values;
    ag_cancel_token* token = nullptr;
    bool cancel = false;
    bool pause_cycle = false;
};

void test_streaming_buckets_preserve_crest_and_energy_when_duration_grows()
{
    agplayer::WaveformBucketizer bucketizer(8U, 4U, 1U, 48000.0F,
        agplayer::WaveformAggregation::AverageAbsolute, true);
    assert(bucketizer.add({1.0F, 0.0F, 0.5F, 0.5F}, 4U) == AG_OK);
    std::vector<float> preview;
    assert(bucketizer.snapshot([](const ag_waveform_snapshot* view, void* data) {
        assert(view->count == 4U && view->total_samples == 8U);
        assert(view->peak[0] == 1.0F && view->peak[1] == 0.5F);
        assert(view->peak[2] == 0.0F && view->peak[3] == 0.0F);
        assert(std::abs(view->rms[0] - 0.70710678F) < 0.00001F);
        *static_cast<std::vector<float>*>(data) = {view->peak, view->peak + view->count};
    }, &preview) == AG_OK);
    assert(bucketizer.add({0, 0, 0, 0, 0.2F, 0.2F, 0.2F, 0.2F}, 8U) == AG_OK);
    std::vector<float> mix, bass, mid, high, peak, rms;
    assert(bucketizer.finish(mix, bass, mid, high, &peak, &rms) == AG_OK);
    assert(peak.size() == 3U); // bounded storage grew in time, not memory
    assert(peak[0] == 1.0F && peak[1] == 0.0F && peak[2] == 0.2F);
    assert(std::abs(rms[0] - 0.61237244F) < 0.00001F);
    assert(std::abs(rms[2] - 0.2F) < 0.00001F);
    assert(preview == std::vector<float>({1.0F, 0.5F, 0, 0}));
}

void test_progressive_callback_can_cancel_before_completion(const std::string& path)
{
    struct State { ag_cancel_token* token; bool received = false; } state{ag_cancel_token_create()};
    ag_waveform* waveform = nullptr;
    assert(ag_waveform_analyze_progressive(path.c_str(), 512U,
        AG_WAVEFORM_AGGREGATION_RMS, state.token, nullptr, nullptr,
        [](const ag_waveform_snapshot* view, void* data) {
            auto& s = *static_cast<State*>(data);
            assert(view->count == 512U);
            assert(view->peak[0] > 0.0F && view->peak[view->count - 1U] == 0.0F);
            assert(view->total_samples >= 88200U && view->sample_rate == 44100);
            s.received = true;
            ag_cancel_token_cancel(s.token);
        }, &state, &waveform) == AG_CANCELLED);
    assert(state.received && waveform == nullptr);
    ag_cancel_token_destroy(state.token);
}

void test_preview_storage_is_bounded_independently_of_final_density()
{
    agplayer::WaveformBucketizer bucketizer(524288U, 524288U, 1U, 48000.0F,
        agplayer::WaveformAggregation::Rms, true);
    assert(bucketizer.add(std::vector<float>(128U, 0.5F), 128U) == AG_OK);
    assert(bucketizer.snapshot([](const ag_waveform_snapshot* view, void*) {
        assert(view->count <= 4096U);
        assert(view->total_samples == 524288U);
        assert(view->peak[0] == 0.5F && view->rms[0] == 0.5F);
        assert(view->peak[view->count - 1U] == 0.0F);
    }, nullptr) == AG_OK);
}

void record_progress(const float progress, void* user_data)
{
    auto& state = *static_cast<ProgressState*>(user_data);
    assert(std::isfinite(progress));
    assert(progress >= 0.0F && progress <= 1.0F);
    if (!state.values.empty()) {
        assert(progress >= state.values.back());
    }
    state.values.push_back(progress);
    if (state.pause_cycle && progress >= 0.2F) {
        state.pause_cycle = false;
        ag_cancel_token_set_paused(state.token, 1);
        ag_cancel_token_set_paused(state.token, 0);
    }
    if (state.cancel && progress >= 0.2F) {
        ag_cancel_token_cancel(state.token);
    }
}

void test_c_api_analysis_and_cancellation(const std::string& source_path)
{
    ag_waveform* waveform = reinterpret_cast<ag_waveform*>(
        static_cast<std::uintptr_t>(1U));
    assert(ag_waveform_analyze(nullptr, 512U, nullptr, nullptr, nullptr,
                               &waveform) == AG_INVALID_ARGUMENT);
    assert(waveform == nullptr);
    assert(ag_waveform_analyze(source_path.c_str(), 0U, nullptr, nullptr,
                               nullptr, &waveform) == AG_INVALID_ARGUMENT);
    assert(waveform == nullptr);

    ProgressState progress;
    assert(ag_waveform_analyze(source_path.c_str(), 512U, nullptr,
                               record_progress, &progress, &waveform) == AG_OK);
    assert(waveform != nullptr);
    assert(ag_waveform_count(waveform) > 100U);
    // Separate raw peak and RMS must survive the C API; a normalized mix
    // cannot represent crest factor or quiet sections faithfully.
    const auto peak_layer = static_cast<ag_waveform_layer>(4);
    const auto rms_layer = static_cast<ag_waveform_layer>(5);
    assert(ag_waveform_layer_count(waveform, peak_layer) == ag_waveform_count(waveform));
    assert(ag_waveform_layer_count(waveform, rms_layer) == ag_waveform_count(waveform));
    bool has_crest = false;
    for (std::size_t i = 0; i < ag_waveform_count(waveform); ++i) {
        const float peak = ag_waveform_layer_peak(waveform, peak_layer, i);
        const float rms = ag_waveform_layer_peak(waveform, rms_layer, i);
        assert(rms <= peak + 0.00001F);
        has_crest = has_crest || peak > rms * 1.1F;
    }
    assert(has_crest);
    assert(progress.values.front() == 0.0F);
    assert(progress.values.back() == 1.0F);
    ag_waveform_destroy(waveform);
    waveform = nullptr;

    ag_cancel_token* token = ag_cancel_token_create();
    assert(token != nullptr);
    ProgressState cancelling;
    cancelling.token = token;
    cancelling.cancel = true;
    assert(ag_waveform_analyze(source_path.c_str(), 512U, token,
                               record_progress, &cancelling, &waveform)
           == AG_CANCELLED);
    assert(waveform == nullptr);
    ag_cancel_token_destroy(token);

    token = ag_cancel_token_create();
    assert(token != nullptr);
    ag_cancel_token_set_paused(token, 1);
    waveform = nullptr;
    auto paused_analysis = std::async(std::launch::async, [&] {
        return ag_track_frequency_color_analysis(
            source_path.c_str(), 512U,
            token, nullptr, nullptr, &waveform);
    });
    assert(paused_analysis.wait_for(std::chrono::milliseconds(50))
           == std::future_status::timeout);
    ag_cancel_token_set_paused(token, 0);
    assert(paused_analysis.get() == AG_OK);
    assert(waveform != nullptr);
    ag_waveform_destroy(waveform);
    waveform = nullptr;
    ag_cancel_token_destroy(token);

    token = ag_cancel_token_create();
    assert(token != nullptr);
    ProgressState reentrant_pause;
    reentrant_pause.token = token;
    reentrant_pause.pause_cycle = true;
    assert(ag_track_frequency_color_analysis(
               source_path.c_str(), 512U, token,
               record_progress, &reentrant_pause, &waveform) == AG_OK);
    assert(waveform != nullptr);
    assert(!reentrant_pause.pause_cycle);
    ag_waveform_destroy(waveform);
    waveform = nullptr;
    ag_cancel_token_destroy(token);

    token = ag_cancel_token_create();
    assert(token != nullptr);
    ProgressState frequency_cancelling;
    frequency_cancelling.token = token;
    frequency_cancelling.cancel = true;
    assert(ag_track_frequency_color_analysis(
               source_path.c_str(), 512U, token,
               record_progress, &frequency_cancelling, &waveform)
           == AG_CANCELLED);
    assert(waveform == nullptr);
    ag_cancel_token_destroy(token);

    assert(ag_track_frequency_color_analysis(
               source_path.c_str(), 512U, nullptr, nullptr, nullptr,
               &waveform) == AG_OK);
    assert(waveform != nullptr);
    assert(ag_waveform_layer_count(waveform, AG_WAVEFORM_LAYER_BASS)
           == ag_waveform_count(waveform));
    assert(ag_waveform_layer_count(waveform, AG_WAVEFORM_LAYER_MID)
           == ag_waveform_count(waveform));
    assert(ag_waveform_layer_count(waveform, AG_WAVEFORM_LAYER_HIGH)
           == ag_waveform_count(waveform));
    ag_waveform_destroy(waveform);
    waveform = nullptr;

    const std::filesystem::path missing =
        std::filesystem::path(source_path).parent_path() / "missing.wav";
    std::filesystem::remove(missing);
    assert(ag_waveform_analyze(missing.string().c_str(), 512U, nullptr,
                               nullptr, nullptr, &waveform) == AG_IO_ERROR);
    assert(waveform == nullptr);
}

} // namespace

int main(const int argc, char** argv)
{
    assert(argc == 2);
    test_actual_frame_bucketing_and_channel_combination();
    test_non_finite_pcm_is_rejected();
    test_average_absolute_and_rms_aggregation();
    test_rgb_frequency_layers_keep_existing_behavior();
    test_streaming_buckets_preserve_crest_and_energy_when_duration_grows();
    test_preview_storage_is_bounded_independently_of_final_density();
    test_progressive_callback_can_cancel_before_completion(argv[1]);
    test_c_api_analysis_and_cancellation(argv[1]);
}
