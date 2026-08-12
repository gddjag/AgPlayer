#undef NDEBUG

#include "timeline_preview_mixer.hpp"
#include "time_pitch_engine.hpp"
#include "audio_engine.hpp"

#include <agplayer/c_api.h>

#include <cassert>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_count_allocations{false};
std::atomic<std::size_t> g_allocation_count{0U};

double channel_energy(const std::vector<float>& samples, const int channel)
{
    assert(channel == 0 || channel == 1);
    double sum = 0.0;
    for (std::size_t index = static_cast<std::size_t>(channel);
         index < samples.size(); index += 2U) {
        const double sample = samples[index];
        sum += sample * sample;
    }
    return sum / static_cast<double>(samples.size() / 2U);
}

double rising_zero_cross_frequency(const std::vector<float>& samples,
                                  const int sample_rate)
{
    std::size_t crossings = 0U;
    for (std::size_t frame = 1U; frame < samples.size() / 2U; ++frame) {
        const float previous = samples[(frame - 1U) * 2U];
        const float current = samples[frame * 2U];
        if (previous <= 0.0F && current > 0.0F) {
            ++crossings;
        }
    }
    return static_cast<double>(crossings) * static_cast<double>(sample_rate)
        / static_cast<double>(samples.size() / 2U);
}

} // namespace

void* operator new(const std::size_t size)
{
    if (g_count_allocations.load(std::memory_order_relaxed)) {
        g_allocation_count.fetch_add(1U, std::memory_order_relaxed);
    }
    if (void* allocation = std::malloc(size); allocation != nullptr) {
        return allocation;
    }
    throw std::bad_alloc();
}

void operator delete(void* allocation) noexcept
{
    std::free(allocation);
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::string source = std::filesystem::path(argv[1]).string();

    // The editor preview and offline export must share one time/pitch contract.
    // This is deliberately a PCM-level check: a UI property alone is not proof
    // that a processor exists or that it emits transformed audio.
    auto time_pitch = agplayer::create_time_pitch_engine();
    assert(time_pitch != nullptr);
    assert(time_pitch->configure(48000, 2));
    assert(time_pitch->setTempoRatio(2.0));
    assert(time_pitch->setPitchCents(1200.0));
    std::vector<float> time_pitch_input(4096U * 2U, 0.0F);
    for (std::size_t frame = 0; frame < 4096U; ++frame) {
        const float sample = std::sin(static_cast<float>(frame) * 0.1F);
        time_pitch_input[frame * 2U] = sample;
        time_pitch_input[frame * 2U + 1U] = sample;
    }
    time_pitch->put(time_pitch_input.data(), 4096U);
    time_pitch->flush();
    std::vector<float> time_pitch_output(8192U * 2U);
    assert(time_pitch->receive(time_pitch_output.data(), 8192U) > 0U);

    agplayer::MultiTrackEditConfig config;
    agplayer::MultiTrackEditConfig::Track left;
    left.input_path = source;
    left.trim_end_ms = 2000;
    left.pan = -1.0;
    config.tracks.push_back(left);

    agplayer::MultiTrackEditConfig::Track right = left;
    right.timeline_start_ms = 1000;
    right.pan = 1.0;
    config.tracks.push_back(right);

    agplayer::TimelinePreviewMixer mixer;
    std::string error;
    assert(mixer.configure(config, 48000, 2, error) == AG_OK);
    assert(mixer.duration_ms() == 3000);

    std::vector<float> first_second(48000U * 2U);
    assert(mixer.read(first_second.data(), 48000U, error) == AG_OK);
    assert(channel_energy(first_second, 0) > 0.005);
    assert(channel_energy(first_second, 1) < 0.00001);

    std::vector<float> overlap(48000U * 2U);
    assert(mixer.read(overlap.data(), 48000U, error) == AG_OK);
    assert(channel_energy(overlap, 0) > 0.005);
    assert(channel_energy(overlap, 1) > 0.005);

    agplayer::MultiTrackEditConfig sixteen_track_config;
    for (int index = 0; index < 16; ++index) {
        agplayer::MultiTrackEditConfig::Track track = left;
        track.timeline_start_ms = index * 20;
        sixteen_track_config.tracks.push_back(track);
    }
    agplayer::TimelinePreviewMixer sixteen_track_mixer;
    assert(sixteen_track_mixer.configure(
               sixteen_track_config, 48000, 2, error) == AG_OK);
    std::vector<float> sixteen_track_audio(1024U * 2U);
    assert(sixteen_track_mixer.read(
               sixteen_track_audio.data(), 1024U, error) == AG_OK);
    assert(channel_energy(sixteen_track_audio, 0) > 0.005);

    assert(mixer.seek(2000, error) == AG_OK);
    std::vector<float> tail(48000U * 2U);
    assert(mixer.read(tail.data(), 48000U, error) == AG_OK);
    assert(channel_energy(tail, 0) < 0.00001);
    assert(channel_energy(tail, 1) > 0.005);

    agplayer::MultiTrackEditConfig tempo_config;
    agplayer::MultiTrackEditConfig::Track tempo_track;
    tempo_track.input_path = source;
    tempo_track.trim_end_ms = 2000;
    tempo_track.speed_ratio = 2.0;
    tempo_track.keep_pitch = true;
    tempo_config.tracks.push_back(tempo_track);
    agplayer::TimelinePreviewMixer tempo_mixer;
    assert(tempo_mixer.configure(tempo_config, 48000, 2, error) == AG_OK);
    assert(tempo_mixer.duration_ms() >= 950);
    assert(tempo_mixer.duration_ms() <= 1050);
    std::vector<float> tempo_audio(48000U * 2U);
    assert(tempo_mixer.read(tempo_audio.data(), 48000U, error) == AG_OK);
    assert(channel_energy(tempo_audio, 0) > 0.005);

    agplayer::TimelinePreviewMixer realtime_mixer;
    assert(realtime_mixer.configure(tempo_config, 48000, 2, error) == AG_OK);
    std::vector<float> realtime_block(1024U * 2U);
    assert(realtime_mixer.read(realtime_block.data(), 1024U, error) == AG_OK);
    assert(realtime_mixer.read(realtime_block.data(), 1024U, error) == AG_OK);
    g_allocation_count.store(0U, std::memory_order_relaxed);
    g_count_allocations.store(true, std::memory_order_relaxed);
    assert(realtime_mixer.read(realtime_block.data(), 1024U, error) == AG_OK);
    g_count_allocations.store(false, std::memory_order_relaxed);
    assert(g_allocation_count.load(std::memory_order_relaxed) == 0U);

    agplayer::MultiTrackEditConfig pitch_config;
    agplayer::MultiTrackEditConfig::Track pitch_track;
    pitch_track.input_path = source;
    pitch_track.trim_end_ms = 2000;
    pitch_track.pitch_cents = 1200;
    pitch_track.keep_pitch = true;
    pitch_config.tracks.push_back(pitch_track);
    agplayer::TimelinePreviewMixer pitch_mixer;
    assert(pitch_mixer.configure(pitch_config, 48000, 2, error) == AG_OK);
    std::vector<float> pitched_audio(48000U * 2U);
    assert(pitch_mixer.read(pitched_audio.data(), 48000U, error) == AG_OK);
    const double pitched_frequency = rising_zero_cross_frequency(
        pitched_audio, 48000);
    assert(pitched_frequency > 760.0 && pitched_frequency < 920.0);

    auto live_mixer = std::make_shared<agplayer::TimelinePreviewMixer>();
    assert(live_mixer->configure(config, 48000, 2, error) == AG_OK);
    agplayer::AudioEngine engine(agplayer::AudioBackend::Manual, 4096U);
    assert(engine.load_timeline(live_mixer) == AG_OK);
    assert(engine.play() == AG_OK);
    std::vector<float> rendered(1024U * 2U);
    double rendered_energy = 0.0;
    for (int iteration = 0; iteration < 200; ++iteration) {
        engine.render(rendered.data(), 1024U);
        rendered_energy += channel_energy(rendered, 0)
                           + channel_energy(rendered, 1);
        if (engine.snapshot().state == agplayer::EngineState::Stopped) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(rendered_energy > 0.02);
    assert(engine.snapshot().state == agplayer::EngineState::Stopped);

    // Timeline looping belongs to the decode path.  A QML polling timer can
    // only seek after the playhead has passed the boundary, which produces a
    // gap and makes the reported transport position inaccurate.  Exercise a
    // deliberately short range long enough to wrap several times while the
    // engine remains in the Playing state.
    auto loop_mixer = std::make_shared<agplayer::TimelinePreviewMixer>();
    assert(loop_mixer->configure(config, 48000, 2, error) == AG_OK);
    agplayer::AudioEngine loop_engine(agplayer::AudioBackend::Manual, 4096U);
    assert(loop_engine.load_timeline(loop_mixer, 500, 1000) == AG_OK);
    assert(loop_engine.seek(750) == AG_OK);
    assert(loop_engine.play() == AG_OK);

    bool reached_loop_end = false;
    bool wrapped_at_loop_start = false;
    double loop_energy = 0.0;
    for (int iteration = 0; iteration < 400; ++iteration) {
        loop_engine.render(rendered.data(), 1024U);
        loop_energy += channel_energy(rendered, 0)
                       + channel_energy(rendered, 1);
        const auto snapshot = loop_engine.snapshot();
        reached_loop_end = reached_loop_end || snapshot.position_ms >= 900;
        wrapped_at_loop_start = wrapped_at_loop_start
            || (reached_loop_end && snapshot.position_ms >= 500
                && snapshot.position_ms < 650);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(reached_loop_end);
    assert(wrapped_at_loop_start);
    assert(loop_energy > 0.02);
    assert(loop_engine.snapshot().state == agplayer::EngineState::Playing);
    assert(loop_engine.stop() == AG_OK);
    return 0;
}
