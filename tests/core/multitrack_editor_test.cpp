#undef NDEBUG

#include "multitrack_editor.hpp"
#include "decoder.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

long long duration_ms(const std::filesystem::path& path)
{
    ag_metadata* metadata = nullptr;
    assert(ag_metadata_open(path.string().c_str(), &metadata) == AG_OK);
    assert(metadata != nullptr);
    const long long duration = ag_metadata_duration_ms(metadata);
    ag_metadata_destroy(metadata);
    return duration;
}

std::pair<double, double> stereo_rms(const std::filesystem::path& path)
{
    agplayer::Decoder decoder;
    assert(decoder.open(path.string(), 48000, 2) == AG_OK);
    double left_sum = 0.0;
    double right_sum = 0.0;
    std::size_t frame_count = 0;
    while (true) {
        agplayer::DecodedAudioBlock block;
        assert(decoder.read(block) == AG_OK);
        for (std::size_t frame = 0; frame < block.frames; ++frame) {
            const float left = block.samples[frame * 2];
            const float right = block.samples[frame * 2 + 1];
            left_sum += static_cast<double>(left) * left;
            right_sum += static_cast<double>(right) * right;
        }
        frame_count += block.frames;
        if (block.end_of_stream) {
            break;
        }
    }
    assert(frame_count > 0);
    return {std::sqrt(left_sum / static_cast<double>(frame_count)),
            std::sqrt(right_sum / static_cast<double>(frame_count))};
}

std::size_t decoded_frames(const std::filesystem::path& path)
{
    ag_metadata* metadata = nullptr;
    assert(ag_metadata_open(path.string().c_str(), &metadata) == AG_OK);
    const int sample_rate = ag_metadata_sample_rate(metadata);
    ag_metadata_destroy(metadata);
    assert(sample_rate > 0);
    agplayer::Decoder decoder;
    assert(decoder.open(path.string(), sample_rate, 2) == AG_OK);
    std::size_t frame_count = 0;
    while (true) {
        agplayer::DecodedAudioBlock block;
        assert(decoder.read(block) == AG_OK);
        frame_count += block.frames;
        if (block.end_of_stream) {
            return frame_count;
        }
    }
}

double stereo_rms_window(const std::filesystem::path& path,
                         std::size_t first_frame,
                         std::size_t frame_count)
{
    agplayer::Decoder decoder;
    assert(decoder.open(path.string(), 48000, 2) == AG_OK);
    double sum = 0.0;
    std::size_t measured = 0;
    std::size_t cursor = 0;
    while (measured < frame_count) {
        agplayer::DecodedAudioBlock block;
        assert(decoder.read(block) == AG_OK);
        for (std::size_t frame = 0; frame < block.frames; ++frame, ++cursor) {
            if (cursor < first_frame) continue;
            if (measured >= frame_count) break;
            for (int channel = 0; channel < 2; ++channel) {
                const double sample = block.samples[frame * 2 + channel];
                sum += sample * sample;
            }
            ++measured;
        }
        if (block.end_of_stream) break;
    }
    assert(measured == frame_count);
    return std::sqrt(sum / static_cast<double>(measured * 2));
}

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path input_path = argv[1];

    const std::filesystem::path output = input_path.parent_path() / "multitrack-out.wav";
    std::filesystem::remove(output);

    agplayer::MultiTrackEditConfig config;
    agplayer::MultiTrackEditConfig::Track track;
    track.input_path = input_path.string();
    track.gain = 0.5;
    config.tracks.push_back(track);
    config.output_path = output.string();

    std::string error;
    const ag_result result = agplayer::multitrack_edit(config, nullptr, nullptr, error);
    if (result != AG_OK) {
        std::cerr << "multitrack_edit failed: " << static_cast<int>(result) << " " << error << "\n";
    }
    assert(result == AG_OK);
    assert(std::filesystem::exists(output));
    const long long single_track_duration_ms = duration_ms(output);
    const std::size_t single_track_frames = decoded_frames(output);
    std::filesystem::remove(output);

    const std::filesystem::path sample_offset_output =
        input_path.parent_path() / "multitrack-sample-offset.wav";
    agplayer::MultiTrackEditConfig sample_offset_config;
    agplayer::MultiTrackEditConfig::Track sample_offset_track;
    sample_offset_track.input_path = input_path.string();
    sample_offset_track.timeline_start_sample = 17;
    sample_offset_config.tracks.push_back(sample_offset_track);
    sample_offset_config.output_path = sample_offset_output.string();
    error.clear();
    assert(agplayer::multitrack_edit(sample_offset_config, nullptr, nullptr,
                                    error) == AG_OK);
    assert(decoded_frames(sample_offset_output) == single_track_frames + 17);
    std::filesystem::remove(sample_offset_output);

    const std::filesystem::path v3_sample_output =
        input_path.parent_path() / "multitrack-v3-sample-offset.wav";
    ag_multitrack_track_v3 v3_sample_track{};
    v3_sample_track.struct_size = sizeof(v3_sample_track);
    v3_sample_track.api_version = 1;
    const std::string input_utf8_for_v3 = input_path.string();
    v3_sample_track.input_path = input_utf8_for_v3.c_str();
    v3_sample_track.timeline_start_sample = 23;
    v3_sample_track.trim_start_sample = -1;
    v3_sample_track.trim_end_sample = -1;
    v3_sample_track.fade_in_samples = -1;
    v3_sample_track.fade_out_samples = -1;
    v3_sample_track.timeline_duration_samples = -1;
    v3_sample_track.gain = 1.0;
    assert(ag_multitrack_edit_v3(
               1, &v3_sample_track, v3_sample_output.string().c_str(),
               nullptr, nullptr, nullptr) == AG_OK);
    assert(decoded_frames(v3_sample_output) == single_track_frames + 23);
    std::filesystem::remove(v3_sample_output);

    const std::filesystem::path pan_output =
        input_path.parent_path() / "multitrack-hard-left.wav";
    std::filesystem::remove(pan_output);
    agplayer::MultiTrackEditConfig pan_config;
    agplayer::MultiTrackEditConfig::Track panned_track;
    panned_track.input_path = input_path.string();
    panned_track.pan = -1.0;
    pan_config.tracks.push_back(panned_track);
    pan_config.output_path = pan_output.string();
    error.clear();
    assert(agplayer::multitrack_edit(pan_config, nullptr, nullptr, error)
           == AG_OK);
    const auto [left_rms, right_rms] = stereo_rms(pan_output);
    assert(left_rms > 0.01);
    assert(right_rms < left_rms * 0.05);
    std::filesystem::remove(pan_output);

    const std::filesystem::path api_pan_output =
        input_path.parent_path() / "multitrack-api-hard-right.wav";
    std::filesystem::remove(api_pan_output);
    const std::string input_utf8_for_pan = input_path.string();
    const char* panned_input_paths[] = {input_utf8_for_pan.c_str()};
    const double api_gains[] = {1.0};
    const double api_pans[] = {1.0};
    assert(ag_multitrack_edit_ex2(
               1, panned_input_paths, nullptr, nullptr, nullptr, nullptr,
               nullptr, api_gains, api_pans, api_pan_output.string().c_str(),
               nullptr, nullptr, nullptr)
           == AG_OK);
    const auto [api_left_rms, api_right_rms] = stereo_rms(api_pan_output);
    assert(api_right_rms > 0.01);
    assert(api_left_rms < api_right_rms * 0.05);
    std::filesystem::remove(api_pan_output);

    const std::filesystem::path loop_output =
        input_path.parent_path() / "multitrack-loop.wav";
    std::filesystem::remove(loop_output);
    agplayer::MultiTrackEditConfig loop_config;
    agplayer::MultiTrackEditConfig::Track loop_track;
    loop_track.input_path = input_path.string();
    loop_track.loop = true;
    loop_track.timeline_duration_ms = 5500;
    loop_config.tracks.push_back(loop_track);
    loop_config.output_path = loop_output.string();
    error.clear();
    assert(agplayer::multitrack_edit(loop_config, nullptr, nullptr, error)
           == AG_OK);
    const long long loop_duration = duration_ms(loop_output);
    assert(loop_duration >= 5450 && loop_duration <= 5550);
    std::filesystem::remove(loop_output);

    const std::filesystem::path v2_output =
        input_path.parent_path() / "multitrack-v2-loop.wav";
    std::filesystem::remove(v2_output);
    ag_multitrack_track_v2 v2_track{};
    v2_track.struct_size = sizeof(v2_track);
    v2_track.api_version = 1;
    v2_track.input_path = input_utf8_for_pan.c_str();
    v2_track.gain = 1.0;
    v2_track.pan = -1.0;
    v2_track.timeline_duration_ms = 5500;
    v2_track.loop = 1;
    assert(ag_multitrack_edit_v2(
               1, &v2_track, v2_output.string().c_str(), nullptr, nullptr,
               nullptr)
           == AG_OK);
    assert(duration_ms(v2_output) >= 5450);
    const auto [v2_left_rms, v2_right_rms] = stereo_rms(v2_output);
    assert(v2_left_rms > 0.01);
    assert(v2_right_rms < v2_left_rms * 0.05);
    std::filesystem::remove(v2_output);

    const std::filesystem::path linear_fade_output =
        input_path.parent_path() / "multitrack-linear-fade.wav";
    const std::filesystem::path equal_power_fade_output =
        input_path.parent_path() / "multitrack-equal-power-fade.wav";
    agplayer::MultiTrackEditConfig linear_fade_config;
    agplayer::MultiTrackEditConfig::Track linear_fade_track;
    linear_fade_track.input_path = input_path.string();
    linear_fade_track.fade_in_ms = 1000;
    linear_fade_track.fade_in_curve = agplayer::FadeCurve::Linear;
    linear_fade_config.tracks.push_back(linear_fade_track);
    linear_fade_config.output_path = linear_fade_output.string();
    error.clear();
    assert(agplayer::multitrack_edit(linear_fade_config, nullptr, nullptr,
                                    error) == AG_OK);

    agplayer::MultiTrackEditConfig equal_power_fade_config;
    agplayer::MultiTrackEditConfig::Track equal_power_fade_track;
    equal_power_fade_track.input_path = input_path.string();
    equal_power_fade_track.fade_in_ms = 1000;
    equal_power_fade_track.fade_in_curve = agplayer::FadeCurve::EqualPower;
    equal_power_fade_config.tracks.push_back(equal_power_fade_track);
    equal_power_fade_config.output_path = equal_power_fade_output.string();
    error.clear();
    assert(agplayer::multitrack_edit(equal_power_fade_config, nullptr, nullptr,
                                    error) == AG_OK);

    const double linear_half = stereo_rms_window(
        linear_fade_output, 24000 - 240, 480);
    const double equal_power_half = stereo_rms_window(
        equal_power_fade_output, 24000 - 240, 480);
    assert(equal_power_half > linear_half * 1.25);
    std::filesystem::remove(linear_fade_output);
    std::filesystem::remove(equal_power_fade_output);

    // Optional: two-track mix with the same source.
    const std::filesystem::path mix_output =
        input_path.parent_path() / "multitrack-mix.wav";
    std::filesystem::remove(mix_output);

    agplayer::MultiTrackEditConfig mix_config;
    agplayer::MultiTrackEditConfig::Track track_a;
    track_a.input_path = input_path.string();
    track_a.gain = 0.5;
    mix_config.tracks.push_back(track_a);
    agplayer::MultiTrackEditConfig::Track track_b;
    track_b.input_path = input_path.string();
    track_b.timeline_start_ms = 1000;
    track_b.gain = 0.5;
    mix_config.tracks.push_back(track_b);
    mix_config.output_path = mix_output.string();

    error.clear();
    const ag_result mix_result =
        agplayer::multitrack_edit(mix_config, nullptr, nullptr, error);
    if (mix_result != AG_OK) {
        std::cerr << "multitrack_edit two-track mix failed: "
                  << static_cast<int>(mix_result) << " " << error << "\n";
    }
    assert(mix_result == AG_OK);
    assert(std::filesystem::exists(mix_output));
    assert(duration_ms(mix_output) >= single_track_duration_ms + 900);
    std::filesystem::remove(mix_output);

    const std::filesystem::path api_output =
        input_path.parent_path() / "multitrack-api-offset.wav";
    std::filesystem::remove(api_output);
    const std::string input_utf8 = input_path.string();
    const char* input_paths[] = {input_utf8.c_str(), input_utf8.c_str()};
    const long long timeline_starts[] = {0, 1000};
    const double gains[] = {0.5, 0.5};
    const ag_result api_result = ag_multitrack_edit_ex(
        2, input_paths, timeline_starts, nullptr, nullptr, nullptr, nullptr,
        gains, api_output.string().c_str(), nullptr, nullptr, nullptr);
    assert(api_result == AG_OK);
    assert(duration_ms(api_output) >= single_track_duration_ms + 900);
    std::filesystem::remove(api_output);

    agplayer::MultiTrackEditConfig invalid_config;
    agplayer::MultiTrackEditConfig::Track invalid_track;
    invalid_track.input_path = input_path.string();
    invalid_track.timeline_start_ms = -1;
    invalid_config.tracks.push_back(invalid_track);
    const std::filesystem::path invalid_output =
        input_path.parent_path() / "multitrack-invalid.wav";
    std::filesystem::remove(invalid_output);
    invalid_config.output_path = invalid_output.string();
    error.clear();
    assert(agplayer::multitrack_edit(
               invalid_config, nullptr, nullptr, error)
           == AG_INVALID_ARGUMENT);
    assert(!std::filesystem::exists(invalid_output));

    return 0;
}
