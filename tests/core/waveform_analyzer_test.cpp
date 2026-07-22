#include <agplayer/c_api.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

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
