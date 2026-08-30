// Test files deliberately keep assert() active in Release builds.
#undef NDEBUG

#include "frequency_color_waveform_cache.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

void write_bytes(const std::filesystem::path& path, const Bytes& bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    assert(output.good());
}

Bytes read_bytes(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(input),
                 std::istreambuf_iterator<char>());
}

void copy_and_mutate(const std::filesystem::path& source,
                     const std::filesystem::path& destination,
                     const std::size_t offset,
                     const std::uint8_t value)
{
    Bytes bytes = read_bytes(source);
    assert(offset < bytes.size());
    bytes[offset] = value;
    write_bytes(destination, bytes);
}

bool same_data(const agplayer::FrequencyColorCacheData& left,
               const agplayer::FrequencyColorCacheData& right)
{
    return left.low == right.low && left.mid == right.mid
           && left.high == right.high
           && left.point_count == right.point_count
           && left.sample_rate == right.sample_rate
           && left.timeline_frames == right.timeline_frames
           && left.crossover_low_hz == right.crossover_low_hz
           && left.crossover_high_hz == right.crossover_high_hz
           && left.algorithm_version == right.algorithm_version;
}

agplayer::FrequencyColorCacheData sentinel_data()
{
    agplayer::FrequencyColorCacheData value;
    value.low = {17U};
    value.mid = {18U};
    value.high = {19U};
    value.point_count = 1U;
    value.sample_rate = 22'050U;
    value.timeline_frames = 123U;
    value.crossover_low_hz = 90U;
    value.crossover_high_hz = 900U;
    value.algorithm_version = 99U;
    return value;
}

void assert_rejected_without_output_pollution(
    const std::filesystem::path& cache,
    const std::filesystem::path& source,
    const std::uint32_t algorithm)
{
    agplayer::FrequencyColorCacheData loaded = sentinel_data();
    const agplayer::FrequencyColorCacheData before = loaded;
    assert(!agplayer::FrequencyColorWaveformCache::load(
        cache, source, algorithm, loaded));
    assert(same_data(loaded, before));
}

} // namespace

int main()
{
    using agplayer::FrequencyColorCacheData;
    using agplayer::FrequencyColorWaveformCache;

    assert(agplayer::quantize_frequency_color_peak(-1.0F) == 0U);
    assert(agplayer::quantize_frequency_color_peak(0.0F) == 0U);
    assert(agplayer::quantize_frequency_color_peak(0.49F) == 128U);
    assert(agplayer::quantize_frequency_color_peak(0.98F) == 255U);
    assert(agplayer::quantize_frequency_color_peak(2.0F) == 255U);
    assert(agplayer::quantize_frequency_color_peak(
               std::numeric_limits<float>::quiet_NaN())
           == 0U);

    const std::filesystem::path case_dir =
        std::filesystem::temp_directory_path()
        / ("agplayer-fcw1-" + std::to_string(
               std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(case_dir);
    const std::filesystem::path source = case_dir / "source audio.wav";
    write_bytes(source, {1U, 2U, 3U, 4U, 5U});

    FrequencyColorCacheData original;
    original.point_count = 2'000U;
    original.sample_rate = 48'000U;
    original.timeline_frames = 960'000U;
    original.low.resize(original.point_count);
    original.mid.resize(original.point_count);
    original.high.resize(original.point_count);
    for (std::uint32_t index = 0U; index < original.point_count; ++index) {
        original.low[index] = static_cast<std::uint8_t>(index % 256U);
        original.mid[index] = static_cast<std::uint8_t>((index * 3U) % 256U);
        original.high[index] = static_cast<std::uint8_t>(255U - index % 256U);
    }
    original.low[0] = 0U;
    original.mid[0] = 1U;
    original.high[0] = 255U;
    original.high[1] = 254U;

    const std::filesystem::path cache = case_dir / "waveform.fcw1";
    assert(FrequencyColorWaveformCache::save_atomic(cache, source, original));
    assert(std::filesystem::file_size(cache) == 64U + 3U * 2'000U);
    assert(std::filesystem::file_size(cache) < 8'192U);

    FrequencyColorCacheData loaded;
    assert(FrequencyColorWaveformCache::load(
        cache, source, original.algorithm_version, loaded));
    assert(same_data(loaded, original));

    const Bytes raw = read_bytes(cache);
    assert(raw.size() == 6'064U);
    assert(std::string(raw.begin(), raw.begin() + 4) == "FCW1");
    assert(raw[4] == 1U && raw[5] == 0U);
    assert(raw[6] == 64U && raw[7] == 0U);
    assert(std::equal(original.low.begin(), original.low.end(), raw.begin() + 64));
    assert(std::equal(original.mid.begin(), original.mid.end(),
                      raw.begin() + 64 + original.point_count));
    assert(std::equal(original.high.begin(), original.high.end(),
                      raw.begin() + 64 + 2U * original.point_count));

    const std::filesystem::path bad_magic = case_dir / "bad-magic.fcw1";
    copy_and_mutate(cache, bad_magic, 0U, static_cast<std::uint8_t>('B'));
    assert_rejected_without_output_pollution(
        bad_magic, source, original.algorithm_version);

    const std::filesystem::path bad_header_crc = case_dir / "bad-header-crc.fcw1";
    copy_and_mutate(cache, bad_header_crc, 60U,
                    static_cast<std::uint8_t>(raw[60] ^ 0x80U));
    assert_rejected_without_output_pollution(
        bad_header_crc, source, original.algorithm_version);

    const std::filesystem::path bad_payload_crc = case_dir / "bad-payload-crc.fcw1";
    copy_and_mutate(cache, bad_payload_crc, 64U,
                    static_cast<std::uint8_t>(raw[64] ^ 0x80U));
    assert_rejected_without_output_pollution(
        bad_payload_crc, source, original.algorithm_version);

    const std::filesystem::path truncated_header = case_dir / "header-short.fcw1";
    write_bytes(truncated_header, Bytes(raw.begin(), raw.begin() + 63));
    assert_rejected_without_output_pollution(
        truncated_header, source, original.algorithm_version);

    const std::filesystem::path truncated_payload = case_dir / "payload-short.fcw1";
    write_bytes(truncated_payload, Bytes(raw.begin(), raw.end() - 1));
    assert_rejected_without_output_pollution(
        truncated_payload, source, original.algorithm_version);

    assert_rejected_without_output_pollution(
        cache, source, original.algorithm_version + 1U);

    const auto source_mtime = std::filesystem::last_write_time(source);
    std::filesystem::last_write_time(source, source_mtime + std::chrono::seconds(2));
    assert_rejected_without_output_pollution(
        cache, source, original.algorithm_version);
    std::filesystem::last_write_time(source, source_mtime);

    const std::filesystem::path other_source = case_dir / "other.wav";
    std::filesystem::copy_file(source, other_source);
    std::filesystem::last_write_time(other_source, source_mtime);
    assert_rejected_without_output_pollution(
        cache, other_source, original.algorithm_version);

    {
        std::ofstream append(source, std::ios::binary | std::ios::app);
        append.put('\0');
    }
    std::filesystem::last_write_time(source, source_mtime);
    assert_rejected_without_output_pollution(
        cache, source, original.algorithm_version);
    std::filesystem::resize_file(source, 5U);
    std::filesystem::last_write_time(source, source_mtime);

    FrequencyColorCacheData mismatched = original;
    mismatched.high.pop_back();
    assert(!FrequencyColorWaveformCache::save_atomic(cache, source, mismatched));
    loaded = {};
    assert(FrequencyColorWaveformCache::load(
        cache, source, original.algorithm_version, loaded));
    assert(same_data(loaded, original));

    FrequencyColorCacheData replacement = original;
    std::fill(replacement.low.begin(), replacement.low.end(),
              static_cast<std::uint8_t>(7U));
    assert(FrequencyColorWaveformCache::save_atomic(cache, source, replacement));
    loaded = {};
    assert(FrequencyColorWaveformCache::load(
        cache, source, replacement.algorithm_version, loaded));
    assert(same_data(loaded, replacement));

    FrequencyColorCacheData too_many;
    too_many.point_count = 1'000'001U;
    assert(!FrequencyColorWaveformCache::save_atomic(
        case_dir / "too-many.fcw1", source, too_many));

    FrequencyColorCacheData concurrent_a = original;
    FrequencyColorCacheData concurrent_b = original;
    std::fill(concurrent_a.mid.begin(), concurrent_a.mid.end(),
              static_cast<std::uint8_t>(31U));
    std::fill(concurrent_b.mid.begin(), concurrent_b.mid.end(),
              static_cast<std::uint8_t>(223U));
    const std::filesystem::path concurrent_cache = case_dir / "concurrent.fcw1";
    std::atomic_int save_failures{0};
    const auto save_many = [&](const FrequencyColorCacheData& data) {
        for (int attempt = 0; attempt < 4; ++attempt) {
            if (!FrequencyColorWaveformCache::save_atomic(
                    concurrent_cache, source, data)) {
                save_failures.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };
    std::thread writer_a(save_many, std::cref(concurrent_a));
    std::thread writer_b(save_many, std::cref(concurrent_b));
    writer_a.join();
    writer_b.join();
    assert(save_failures.load(std::memory_order_relaxed) == 0);
    loaded = {};
    assert(FrequencyColorWaveformCache::load(
        concurrent_cache, source, original.algorithm_version, loaded));
    assert(same_data(loaded, concurrent_a) || same_data(loaded, concurrent_b));

    for (const auto& entry : std::filesystem::directory_iterator(case_dir)) {
        assert(entry.path().filename().string().find(".tmp-")
               == std::string::npos);
    }

    std::filesystem::remove_all(case_dir);
}
