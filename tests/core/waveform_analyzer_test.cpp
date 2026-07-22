#include <agplayer/c_api.h>

#include "waveform_analyzer.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace {

void test_actual_frame_bucketing_and_channel_combination()
{
    agplayer::WaveformBucketizer bucketizer(8U, 3U, 2U);
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
    assert(bucketizer.finish(peaks) == AG_OK);
    assert(peaks.size() == 3U);
    assert(std::abs(peaks[0] - 0.75F) < 0.000'001F);
    assert(std::abs(peaks[1] - 1.0F) < 0.000'001F);
    assert(std::abs(peaks[2] - 0.625F) < 0.000'001F);

    agplayer::WaveformBucketizer more_points_than_frames(3U, 10U, 1U);
    assert(more_points_than_frames.add({0.2F, 0.4F, 0.8F}, 3U)
           == AG_OK);
    assert(more_points_than_frames.finish(peaks) == AG_OK);
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
        agplayer::WaveformBucketizer bucketizer(1U, 1U, 2U);
        assert(bucketizer.add({0.5F, invalid}, 1U) == AG_DECODE_ERROR);
        std::vector<float> peaks{1.0F};
        assert(bucketizer.finish(peaks) == AG_DECODE_ERROR);
        assert(peaks.empty());
    }
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

} // namespace

int main(const int argc, char** argv)
{
    assert(argc == 2);
    test_actual_frame_bucketing_and_channel_combination();
    test_non_finite_pcm_is_rejected();
    const std::string source_path = argv[1];

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
