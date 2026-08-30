// Test files deliberately keep assert() active in Release builds.
#undef NDEBUG

#include "frequency_color_waveform_cache.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

template <typename Value>
Value read_little_endian(const Bytes& bytes, const std::size_t offset)
{
    using Unsigned = std::make_unsigned_t<Value>;
    assert(offset + sizeof(Value) <= bytes.size());
    Unsigned value = 0U;
    for (std::size_t index = sizeof(Value); index > 0U; --index) {
        value = static_cast<Unsigned>(
            (value << 8U) | bytes[offset + index - 1U]);
    }
    return static_cast<Value>(value);
}

template <typename Value>
void write_little_endian(Bytes& bytes,
                         const std::size_t offset,
                         const Value value)
{
    using Unsigned = std::make_unsigned_t<Value>;
    assert(offset + sizeof(Value) <= bytes.size());
    Unsigned remaining = static_cast<Unsigned>(value);
    for (std::size_t index = 0U; index < sizeof(Value); ++index) {
        bytes[offset + index] =
            static_cast<std::uint8_t>(remaining & 0xFFU);
        remaining >>= 8U;
    }
}

std::uint32_t independent_crc32(const std::uint8_t* data,
                                const std::size_t size)
{
    std::uint32_t crc = 0xFFFF'FFFFU;
    for (std::size_t index = 0U; index < size; ++index) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U)
                  ^ ((crc & 1U) == 0U ? 0U : 0xEDB8'8320U);
        }
    }
    return crc ^ 0xFFFF'FFFFU;
}

std::uint64_t independent_path_fingerprint(
    const std::filesystem::path& path)
{
    constexpr std::uint64_t offset = 14'695'981'039'346'656'037ULL;
    constexpr std::uint64_t prime = 1'099'511'628'211ULL;
    const std::string normalized =
        std::filesystem::weakly_canonical(path).lexically_normal()
            .generic_u8string();
    std::uint64_t hash = offset;
    for (const unsigned char byte : normalized) {
        hash ^= byte;
        hash *= prime;
    }
    return hash;
}

void rewrite_header_crc(Bytes& bytes)
{
    assert(bytes.size() >= 64U);
    write_little_endian<std::uint32_t>(bytes, 60U, 0U);
    write_little_endian<std::uint32_t>(
        bytes, 60U, independent_crc32(bytes.data(), 64U));
}

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

bool has_temporary_for(const std::filesystem::path& cache)
{
    const std::string prefix = cache.filename().string() + ".tmp-";
    std::error_code error;
    for (const auto& entry :
         std::filesystem::directory_iterator(cache.parent_path(), error)) {
        if (entry.path().filename().string().compare(
                0U, prefix.size(), prefix) == 0) {
            return true;
        }
    }
    return false;
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
    const std::filesystem::path source =
        case_dir / std::filesystem::u8path(u8"source-\u97f3\u9891.wav");
    const Bytes source_bytes{1U, 2U, 3U, 4U, 5U};
    write_bytes(source, source_bytes);

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

    assert(!FrequencyColorWaveformCache::save_atomic(
        source, source, original));
    assert(read_bytes(source) == source_bytes);
    assert(!has_temporary_for(source));

    const std::filesystem::path dotted_source =
        case_dir / "." / source.filename();
    assert(!FrequencyColorWaveformCache::save_atomic(
        dotted_source, source, original));
    assert(read_bytes(source) == source_bytes);
    assert(!has_temporary_for(dotted_source));

    const std::filesystem::path hardlink_source = case_dir / "source-link.wav";
    std::error_code hardlink_error;
    std::filesystem::create_hard_link(source, hardlink_source, hardlink_error);
    if (!hardlink_error) {
        assert(!FrequencyColorWaveformCache::save_atomic(
            hardlink_source, source, original));
        assert(read_bytes(source) == source_bytes);
        assert(!has_temporary_for(hardlink_source));
    }

#ifdef _WIN32
    const std::filesystem::path case_alias =
        case_dir / std::filesystem::u8path(u8"SOURCE-\u97f3\u9891.WAV");
    assert(!FrequencyColorWaveformCache::save_atomic(
        case_alias, source, original));
    assert(read_bytes(source) == source_bytes);
    assert(!has_temporary_for(case_alias));
#endif

    const std::filesystem::path temp_collision_cache =
        case_dir / "temp-collision.fcw1";
    agplayer::testing::force_next_frequency_color_cache_temp(source);
    assert(!FrequencyColorWaveformCache::save_atomic(
        temp_collision_cache, source, original));
    assert(read_bytes(source) == source_bytes);
    assert(!std::filesystem::exists(temp_collision_cache));

    if (!hardlink_error) {
        agplayer::testing::force_next_frequency_color_cache_temp(
            hardlink_source);
        assert(!FrequencyColorWaveformCache::save_atomic(
            temp_collision_cache, source, original));
        assert(read_bytes(source) == source_bytes);
        assert(read_bytes(hardlink_source) == source_bytes);
        assert(!std::filesystem::exists(temp_collision_cache));
    }

    const std::filesystem::path unrelated_temp =
        case_dir / "preexisting-unrelated.tmp";
    const Bytes unrelated_bytes{'u', 'n', 'r', 'e', 'l', 'a', 't', 'e', 'd'};
    write_bytes(unrelated_temp, unrelated_bytes);
    agplayer::testing::force_next_frequency_color_cache_temp(unrelated_temp);
    assert(FrequencyColorWaveformCache::save_atomic(
        temp_collision_cache, source, original));
    assert(read_bytes(unrelated_temp) == unrelated_bytes);
    assert(read_bytes(source) == source_bytes);

    const std::filesystem::path unrelated_directory =
        case_dir / "preexisting-directory.tmp";
    std::filesystem::create_directories(unrelated_directory);
    const std::filesystem::path unrelated_child =
        unrelated_directory / "keep.bin";
    write_bytes(unrelated_child, unrelated_bytes);
    agplayer::testing::force_next_frequency_color_cache_temp(
        unrelated_directory);
    assert(FrequencyColorWaveformCache::save_atomic(
        temp_collision_cache, source, original));
    assert(std::filesystem::is_directory(unrelated_directory));
    assert(read_bytes(unrelated_child) == unrelated_bytes);
    assert(read_bytes(source) == source_bytes);

    const std::filesystem::path cache = case_dir / "waveform.fcw1";
    assert(FrequencyColorWaveformCache::save_atomic(cache, source, original));
    assert(std::filesystem::file_size(cache) == 64U + 3U * 2'000U);
    assert(std::filesystem::file_size(cache) < 8'192U);

    FrequencyColorCacheData loaded;
    assert(FrequencyColorWaveformCache::load(
        cache, source, original.algorithm_version, loaded));
    assert(same_data(loaded, original));

#ifdef _WIN32
    const std::filesystem::path source_case_alias =
        case_dir / std::filesystem::u8path(u8"SOURCE-\u97f3\u9891.WAV");
    loaded = {};
    assert(FrequencyColorWaveformCache::load(
        cache, source_case_alias, original.algorithm_version, loaded));
    assert(same_data(loaded, original));
#endif

    const Bytes raw = read_bytes(cache);
    assert(raw.size() == 6'064U);
    assert(std::string(raw.begin(), raw.begin() + 4) == "FCW1");
    assert(raw[4] == 1U && raw[5] == 0U);
    assert(raw[6] == 64U && raw[7] == 0U);
    assert(read_little_endian<std::uint32_t>(raw, 8U)
           == original.point_count);
    assert(read_little_endian<std::uint32_t>(raw, 12U)
           == original.sample_rate);
    assert(read_little_endian<std::uint16_t>(raw, 16U)
           == original.crossover_low_hz);
    assert(read_little_endian<std::uint16_t>(raw, 18U)
           == original.crossover_high_hz);
    assert(read_little_endian<std::uint32_t>(raw, 20U)
           == original.algorithm_version);
    assert(read_little_endian<std::uint64_t>(raw, 24U)
           == source_bytes.size());
    const auto source_mtime = std::filesystem::last_write_time(source);
    const auto source_mtime_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            source_mtime.time_since_epoch())
            .count();
    assert(read_little_endian<std::int64_t>(raw, 32U) == source_mtime_ns);
    assert(read_little_endian<std::uint64_t>(raw, 40U)
           == original.timeline_frames);
    assert(read_little_endian<std::uint64_t>(raw, 48U)
           == independent_path_fingerprint(source));
    assert(read_little_endian<std::uint32_t>(raw, 56U)
           == independent_crc32(raw.data() + 64U, raw.size() - 64U));
    Bytes zeroed_header(raw.begin(), raw.begin() + 64U);
    write_little_endian<std::uint32_t>(zeroed_header, 60U, 0U);
    assert(read_little_endian<std::uint32_t>(raw, 60U)
           == independent_crc32(zeroed_header.data(), zeroed_header.size()));
    assert(std::equal(original.low.begin(), original.low.end(), raw.begin() + 64));
    assert(std::equal(original.mid.begin(), original.mid.end(),
                      raw.begin() + 64 + original.point_count));
    assert(std::equal(original.high.begin(), original.high.end(),
                      raw.begin() + 64 + 2U * original.point_count));

    std::filesystem::create_directories(case_dir / "nested");
    const std::filesystem::path unicode_canonical_alias =
        case_dir / "nested" / ".." / source.filename();
    loaded = {};
    assert(FrequencyColorWaveformCache::load(
        cache, unicode_canonical_alias, original.algorithm_version, loaded));
    assert(same_data(loaded, original));

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

    const std::filesystem::path trailing_payload = case_dir / "payload-trailing.fcw1";
    Bytes trailing = raw;
    trailing.push_back(0U);
    write_bytes(trailing_payload, trailing);
    assert_rejected_without_output_pollution(
        trailing_payload, source, original.algorithm_version);

    const std::filesystem::path excessive_points = case_dir / "too-many-load.fcw1";
    Bytes excessive = raw;
    write_little_endian<std::uint32_t>(excessive, 8U, 1'000'001U);
    rewrite_header_crc(excessive);
    write_bytes(excessive_points, excessive);
    assert_rejected_without_output_pollution(
        excessive_points, source, original.algorithm_version);

    assert_rejected_without_output_pollution(
        cache, source, original.algorithm_version + 1U);

    auto distinguishable_mtime = source_mtime;
    auto tick = std::filesystem::file_time_type::duration{1};
    for (int attempt = 0; attempt < 32; ++attempt) {
        std::error_code tick_error;
        std::filesystem::last_write_time(
            source, source_mtime + tick, tick_error);
        if (!tick_error) {
            distinguishable_mtime = std::filesystem::last_write_time(source);
            if (distinguishable_mtime != source_mtime) {
                break;
            }
        }
        tick *= 2;
    }
    assert(distinguishable_mtime != source_mtime);
    assert_rejected_without_output_pollution(
        cache, source, original.algorithm_version);
    std::filesystem::last_write_time(source, source_mtime);

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

    const std::filesystem::path agwf_peer = case_dir / "waveform.agwf";
    const Bytes agwf_bytes{'A', 'G', 'W', 'F', 9U};
    write_bytes(agwf_peer, agwf_bytes);
    agplayer::testing::fail_next_frequency_color_cache_replace();
    assert(!FrequencyColorWaveformCache::save_atomic(cache, source, original));
    assert(!has_temporary_for(cache));
    assert(read_bytes(source) == source_bytes);
    assert(read_bytes(agwf_peer) == agwf_bytes);
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
    assert(FrequencyColorWaveformCache::save_atomic(
        concurrent_cache, source, concurrent_a));
    const Bytes concurrent_old_bytes = read_bytes(concurrent_cache);
    const std::filesystem::path concurrent_new_fixture =
        case_dir / "concurrent-new.fcw1";
    assert(FrequencyColorWaveformCache::save_atomic(
        concurrent_new_fixture, source, concurrent_b));
    const Bytes concurrent_new_bytes = read_bytes(concurrent_new_fixture);
    assert(concurrent_old_bytes != concurrent_new_bytes);
    std::filesystem::remove(concurrent_new_fixture);
    std::mutex reader_mutex;
    std::condition_variable reader_condition;
    bool reader_saw_old = false;
    bool reader_saw_new = false;
    bool replacement_finished = false;
    std::atomic_bool stop_reader{false};
    std::atomic_int reader_failures{0};
    std::atomic_int reader_successes{0};
    std::atomic_int reader_open_misses{0};
    std::thread reader([&] {
        while (!stop_reader.load(std::memory_order_relaxed)) {
            Bytes observed;
            bool opened = false;
            bool read_failed = false;
            {
                std::ifstream input(concurrent_cache, std::ios::binary);
                opened = input.is_open();
                if (opened) {
                    observed.assign(std::istreambuf_iterator<char>(input),
                                    std::istreambuf_iterator<char>());
                    read_failed = input.bad();
                }
            }
            if (!opened) {
                reader_open_misses.fetch_add(1, std::memory_order_relaxed);
            } else if (read_failed
                       || (observed != concurrent_old_bytes
                           && observed != concurrent_new_bytes)) {
                reader_failures.fetch_add(1, std::memory_order_relaxed);
            } else {
                reader_successes.fetch_add(1, std::memory_order_relaxed);
            }
            {
                std::unique_lock<std::mutex> lock(reader_mutex);
                reader_saw_old = reader_saw_old
                                 || observed == concurrent_old_bytes;
                reader_saw_new = reader_saw_new
                                 || observed == concurrent_new_bytes;
                reader_condition.notify_all();
                if (reader_saw_old && !replacement_finished) {
                    reader_condition.wait(lock, [&] {
                        return replacement_finished
                               || stop_reader.load(std::memory_order_relaxed);
                    });
                }
            }
            reader_condition.notify_all();
        }
    });
    {
        std::unique_lock<std::mutex> lock(reader_mutex);
        assert(reader_condition.wait_for(
            lock, std::chrono::seconds(2), [&] { return reader_saw_old; }));
    }
    assert(FrequencyColorWaveformCache::save_atomic(
        concurrent_cache, source, concurrent_b));
    {
        std::lock_guard<std::mutex> lock(reader_mutex);
        replacement_finished = true;
    }
    reader_condition.notify_all();
    {
        std::unique_lock<std::mutex> lock(reader_mutex);
        assert(reader_condition.wait_for(
            lock, std::chrono::seconds(2), [&] { return reader_saw_new; }));
    }
    std::atomic_int save_successes{0};
    const auto save_many = [&](const FrequencyColorCacheData& data) {
        for (int attempt = 0; attempt < 4; ++attempt) {
            if (FrequencyColorWaveformCache::save_atomic(
                    concurrent_cache, source, data)) {
                save_successes.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };
    std::thread writer_a(save_many, std::cref(concurrent_a));
    std::thread writer_b(save_many, std::cref(concurrent_b));
    writer_a.join();
    writer_b.join();
    stop_reader.store(true, std::memory_order_relaxed);
    reader.join();
    assert(save_successes.load(std::memory_order_relaxed) > 0);
    assert(reader_successes.load(std::memory_order_relaxed) > 0);
    assert(reader_failures.load(std::memory_order_relaxed) == 0);
#ifndef _WIN32
    assert(reader_open_misses.load(std::memory_order_relaxed) == 0);
#endif
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
