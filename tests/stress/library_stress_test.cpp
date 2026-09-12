// Stress test: simulate a 10,000-track library by generating short WAV files,
// analyzing each one offline, and persisting waveform caches. The test records
// memory usage samples and verifies cache count, total size, and stability.
//
// The track count can be overridden with AGPLAYER_STRESS_COUNT for quick runs.
#undef NDEBUG

#include "waveform_cache.hpp"

#include <agplayer/c_api.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <limits>
#include <random>
#include <sstream>
#include <thread>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#else
#include <unistd.h>
#endif

namespace {

constexpr std::size_t default_track_count = 10'000U;
constexpr std::size_t target_points = 200U;
constexpr std::uint64_t cache_size_limit_bytes = 100ULL * 1024ULL * 1024ULL;
constexpr std::size_t memory_sample_interval = 100U;
constexpr std::size_t random_access_sample_count = 100U;

struct TrackParameters final {
    std::uint32_t sample_rate = 44100U;
    std::uint16_t channels = 2U;
    std::uint32_t duration_seconds = 1U;
};

void write_u16(std::ofstream& output, const std::uint16_t value)
{
    const char bytes[] = {
        static_cast<char>(value & 0xFFU),
        static_cast<char>((value >> 8U) & 0xFFU),
    };
    output.write(bytes, sizeof(bytes));
}

void write_u32(std::ofstream& output, const std::uint32_t value)
{
    const char bytes[] = {
        static_cast<char>(value & 0xFFU),
        static_cast<char>((value >> 8U) & 0xFFU),
        static_cast<char>((value >> 16U) & 0xFFU),
        static_cast<char>((value >> 24U) & 0xFFU),
    };
    output.write(bytes, sizeof(bytes));
}

std::size_t parse_track_count()
{
    std::string value;
#ifdef _WIN32
    char* buffer = nullptr;
    std::size_t length = 0U;
    if (_dupenv_s(&buffer, &length, "AGPLAYER_STRESS_COUNT") != 0
        || buffer == nullptr) {
        return default_track_count;
    }
    const std::unique_ptr<char, decltype(&std::free)> guard(buffer, &std::free);
    value = buffer;
#else
    const char* env = std::getenv("AGPLAYER_STRESS_COUNT");
    if (env == nullptr || env[0] == '\0') {
        return default_track_count;
    }
    value = env;
#endif
    if (value.empty()) {
        return default_track_count;
    }
    try {
        const unsigned long parsed = std::stoul(value);
        if (parsed == 0U
            || parsed > std::numeric_limits<std::size_t>::max()) {
            return default_track_count;
        }
        return static_cast<std::size_t>(parsed);
    } catch (...) {
        return default_track_count;
    }
}

std::uint64_t working_set_bytes()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))
        != FALSE) {
        return static_cast<std::uint64_t>(pmc.WorkingSetSize);
    }
#else
    // Non-Windows platforms: memory sampling is informational only for this
    // Windows-first stress test. Returning zero keeps the CSV output uniform.
#endif
    return 0U;
}

void generate_wav(const std::filesystem::path& path,
                  const TrackParameters& params)
{
    constexpr std::uint16_t bits_per_sample = 16U;
    constexpr double amplitude = 0.25;
    constexpr double frequency = 440.0;
    constexpr double pi = 3.14159265358979323846;

    const std::uint32_t frame_count =
        params.sample_rate * params.duration_seconds;
    const std::uint32_t bytes_per_frame =
        static_cast<std::uint32_t>(params.channels)
        * (bits_per_sample / 8U);
    const std::uint64_t data_size_wide =
        static_cast<std::uint64_t>(frame_count) * bytes_per_frame;
    assert(data_size_wide <= std::numeric_limits<std::uint32_t>::max());
    const std::uint32_t data_size = static_cast<std::uint32_t>(data_size_wide);

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output);

    output.write("RIFF", 4);
    write_u32(output, 36U + data_size);
    output.write("WAVE", 4);
    output.write("fmt ", 4);
    write_u32(output, 16U);
    write_u16(output, 1U); // PCM
    write_u16(output, params.channels);
    write_u32(output, params.sample_rate);
    write_u32(output, params.sample_rate * bytes_per_frame);
    write_u16(output, static_cast<std::uint16_t>(bytes_per_frame));
    write_u16(output, bits_per_sample);
    output.write("data", 4);
    write_u32(output, data_size);

    for (std::uint32_t frame = 0U; frame < frame_count; ++frame) {
        const double phase =
            2.0 * pi * frequency * static_cast<double>(frame)
            / static_cast<double>(params.sample_rate);
        const auto sample = static_cast<std::int16_t>(
            std::lround(std::sin(phase) * amplitude * 32767.0));
        for (std::uint16_t channel = 0U; channel < params.channels;
             ++channel) {
            write_u16(output, static_cast<std::uint16_t>(sample));
        }
    }

    output.flush();
    output.close();
    assert(output);
}

TrackParameters random_parameters(std::mt19937& rng)
{
    static const std::uint32_t sample_rates[] = {22050U, 32000U, 44100U};
    static const std::uint16_t channel_choices[] = {1U, 2U};
    static const std::uint32_t duration_choices[] = {1U, 2U};

    std::uniform_int_distribution<std::size_t> rate_dist(
        0U, std::size(sample_rates) - 1U);
    std::uniform_int_distribution<std::size_t> channel_dist(
        0U, std::size(channel_choices) - 1U);
    std::uniform_int_distribution<std::size_t> duration_dist(
        0U, std::size(duration_choices) - 1U);

    TrackParameters params;
    params.sample_rate = sample_rates[rate_dist(rng)];
    params.channels = channel_choices[channel_dist(rng)];
    params.duration_seconds = duration_choices[duration_dist(rng)];
    return params;
}

std::filesystem::path cache_path_for(
    const std::filesystem::path& cache_dir,
    const std::filesystem::path& source_path)
{
    const std::string key = agplayer::WaveformCache::key_for(source_path);
    assert(!key.empty());
    return cache_dir / (key + ".agwf");
}

bool robust_remove_all(const std::filesystem::path& dir)
{
    for (int attempt = 0; attempt < 5; ++attempt) {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        if (!ec) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

std::uint64_t total_cache_size(const std::filesystem::path& cache_dir)
{
    std::uint64_t total = 0U;
    std::error_code ec;
    for (const auto& entry :
         std::filesystem::directory_iterator(cache_dir, ec)) {
        if (entry.is_regular_file(ec)
            && entry.path().extension() == ".agwf") {
            total += static_cast<std::uint64_t>(entry.file_size(ec));
        }
    }
    return total;
}

std::size_t count_cache_files(const std::filesystem::path& cache_dir)
{
    std::size_t count = 0U;
    std::error_code ec;
    for (const auto& entry :
         std::filesystem::directory_iterator(cache_dir, ec)) {
        if (entry.is_regular_file(ec)
            && entry.path().extension() == ".agwf") {
            ++count;
        }
    }
    return count;
}

std::string format_bytes(const std::uint64_t bytes)
{
    constexpr double kib = 1024.0;
    constexpr double mib = kib * 1024.0;
    constexpr double gib = mib * 1024.0;
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(2);
    if (bytes >= gib) {
        out << (static_cast<double>(bytes) / gib) << " GiB";
    } else if (bytes >= mib) {
        out << (static_cast<double>(bytes) / mib) << " MiB";
    } else if (bytes >= kib) {
        out << (static_cast<double>(bytes) / kib) << " KiB";
    } else {
        out << bytes << " B";
    }
    return out.str();
}

std::uint32_t current_process_id()
{
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return static_cast<std::uint32_t>(getpid());
#endif
}

} // namespace

int main()
{
    using namespace std::chrono;

    const std::size_t track_count = parse_track_count();
    std::cout << "library_stress_test: track_count=" << track_count
              << " target_points=" << target_points << std::endl;

    std::ostringstream base_name;
    base_name << "agplayer-library-stress-test-" << current_process_id();
    const std::filesystem::path base_dir =
        std::filesystem::temp_directory_path() / base_name.str();
    const std::filesystem::path source_dir = base_dir / "sources";
    const std::filesystem::path cache_dir = base_dir / "cache";

    assert(robust_remove_all(base_dir));
    {
        std::error_code ec;
        assert(!std::filesystem::exists(base_dir, ec));
        std::filesystem::create_directories(source_dir, ec);
        assert(!ec);
        std::filesystem::create_directories(cache_dir, ec);
        assert(!ec);
    }

    std::mt19937 rng(42U); // deterministic for reproducibility
    std::vector<std::filesystem::path> source_paths;
    source_paths.reserve(track_count);

    const auto total_start = steady_clock::now();
    auto generation_start = steady_clock::now();

    // Phase 1: generate fixtures.
    for (std::size_t index = 0U; index < track_count; ++index) {
        const TrackParameters params = random_parameters(rng);
        std::ostringstream name;
        name << "track_" << std::setfill('0') << std::setw(5) << index
             << ".wav";
        const std::filesystem::path source_path = source_dir / name.str();
        generate_wav(source_path, params);
        source_paths.push_back(source_path);
    }

    const auto generation_elapsed =
        duration_cast<milliseconds>(steady_clock::now() - generation_start)
            .count();
    std::cout << "generated " << track_count << " WAV files in "
              << generation_elapsed << " ms" << std::endl;

    // Phase 2: analyze each track and persist cache.
    auto analysis_start = steady_clock::now();
    std::size_t success_count = 0U;
    std::size_t fail_count = 0U;
    std::uint64_t peak_working_set = 0U;

    std::cout << "memory_sample,processed,memory_bytes" << std::endl;

    for (std::size_t index = 0U; index < track_count; ++index) {
        const std::filesystem::path& source_path = source_paths[index];
        ag_waveform* waveform = nullptr;
        const ag_result result = ag_track_analysis(
            source_path.string().c_str(),
            target_points,
            nullptr,
            nullptr,
            nullptr,
            &waveform,
            nullptr);

        if (result != AG_OK || waveform == nullptr) {
            ++fail_count;
            std::cerr << "analysis failed for " << source_path.string()
                      << " result=" << result << std::endl;
            continue;
        }

        agplayer::WaveformCacheData data;
        const std::size_t count = ag_waveform_count(waveform);
        data.mix.resize(count);
        for (std::size_t i = 0U; i < count; ++i) {
            data.mix[i] = ag_waveform_peak(waveform, i);
        }
        data.bpm = ag_waveform_bpm(waveform);
        ag_waveform_destroy(waveform);

        const std::filesystem::path cache_path =
            cache_path_for(cache_dir, source_path);
        const bool saved = agplayer::WaveformCache::save_v2(
            cache_path, source_path, data);
        assert(saved);

        ++success_count;

        if ((index + 1U) % memory_sample_interval == 0U) {
            const std::uint64_t memory = working_set_bytes();
            peak_working_set = std::max(peak_working_set, memory);
            std::cout << (index + 1U) << "," << memory << std::endl;
        }
    }

    const auto analysis_elapsed =
        duration_cast<milliseconds>(steady_clock::now() - analysis_start)
            .count();
    std::cout << "analyzed " << success_count << "/" << track_count
              << " tracks in " << analysis_elapsed << " ms ("
              << fail_count << " failures)" << std::endl;
    assert(fail_count == 0U);
    assert(success_count == track_count);

    // Phase 3: random access - load a sample of caches back.
    auto access_start = steady_clock::now();
    std::size_t access_hits = 0U;
    if (!source_paths.empty()) {
        std::mt19937 access_rng(123U);
        std::uniform_int_distribution<std::size_t> dist(
            0U, source_paths.size() - 1U);
        for (std::size_t i = 0U; i < random_access_sample_count; ++i) {
            const std::filesystem::path& source_path =
                source_paths[dist(access_rng)];
            const std::filesystem::path cache_path =
                cache_path_for(cache_dir, source_path);
            agplayer::WaveformCacheData data;
            if (agplayer::WaveformCache::load_v2(cache_path, source_path,
                                                 data)) {
                ++access_hits;
                assert(!data.mix.empty());
            }
        }
    }
    const auto access_elapsed =
        duration_cast<milliseconds>(steady_clock::now() - access_start)
            .count();
    std::cout << "random access sample " << access_hits << "/"
              << random_access_sample_count << " hits in " << access_elapsed
              << " ms" << std::endl;
    assert(access_hits == random_access_sample_count);

    // Phase 4: verify cache count and total size.
    const std::size_t cache_count = count_cache_files(cache_dir);
    const std::uint64_t cache_size = total_cache_size(cache_dir);
    std::cout << "cache_files=" << cache_count
              << " cache_size=" << format_bytes(cache_size) << std::endl;
    assert(cache_count == track_count);
    assert(cache_size <= cache_size_limit_bytes);

    const auto total_elapsed =
        duration_cast<milliseconds>(steady_clock::now() - total_start)
            .count();
    std::cout << "peak_working_set=" << format_bytes(peak_working_set)
              << " total_time_ms=" << total_elapsed << std::endl;

    // Phase 5: cleanup.
    robust_remove_all(base_dir);

    std::cout << "library_stress_test PASSED" << std::endl;
    return 0;
}
