#include "waveform_cache.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>
#include <type_traits>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace agplayer {
namespace {

constexpr std::array<char, 4> cache_magic{'A', 'G', 'W', 'F'};
constexpr std::uint32_t cache_version_v1 = 1U;
constexpr std::uint32_t cache_version_v2 = 2U;
constexpr std::uint32_t cache_version_v4 = 4U;
constexpr std::uint64_t v1_header_size = 32U;
constexpr std::uint64_t v2_header_size = 88U;
constexpr std::uint64_t v2_timeline_header_size = 104U;
constexpr std::uint64_t v2_timeline_metadata_flag = 1U;
constexpr std::uint64_t v4_header_size = 104U;
constexpr std::uint64_t fnv_offset = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;
// Schema 8 adds original full-band peak/RMS and single-pass PCM bucketing.
// v4 flag 2 carries two additional same-length layers; older v4 remains readable.
constexpr std::uint32_t analysis_schema_version = 8U;
constexpr std::uint64_t amplitude_detail_flag = 2U;

struct SourceMetadata final {
    std::uint64_t size = 0U;
    std::int64_t mtime_ns = 0;
};

bool source_metadata(const std::filesystem::path& source_path,
                     SourceMetadata& metadata) noexcept
{
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(source_path, error);
    if (error || size > std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }
    const auto mtime = std::filesystem::last_write_time(source_path, error);
    if (error) {
        return false;
    }
    metadata.size = static_cast<std::uint64_t>(size);
    metadata.mtime_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            mtime.time_since_epoch())
                            .count();
    return true;
}

std::filesystem::path canonical_path(
    const std::filesystem::path& source_path)
{
    std::error_code error;
    std::filesystem::path result =
        std::filesystem::weakly_canonical(source_path, error);
    if (!error) {
        return result;
    }
    error.clear();
    result = std::filesystem::absolute(source_path, error);
    return error ? source_path.lexically_normal() : result.lexically_normal();
}

void hash_byte(std::uint64_t& hash, const unsigned char byte) noexcept
{
    hash ^= byte;
    hash *= fnv_prime;
}

template <typename Value>
void hash_integer(std::uint64_t& hash, const Value value) noexcept
{
    using Unsigned = std::make_unsigned_t<Value>;
    Unsigned bits = static_cast<Unsigned>(value);
    for (std::size_t index = 0U; index < sizeof(Value); ++index) {
        hash_byte(hash, static_cast<unsigned char>(bits & 0xFFU));
        bits >>= 8U;
    }
}

template <typename Value>
bool write_little_endian(std::ostream& stream, const Value value)
{
    using Unsigned = std::make_unsigned_t<Value>;
    Unsigned bits = static_cast<Unsigned>(value);
    std::array<char, sizeof(Value)> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = static_cast<char>(bits & 0xFFU);
        if constexpr (sizeof(Unsigned) > 1U) {
            bits >>= 8U;
        }
    }
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(stream);
}

template <typename Value>
bool read_little_endian(std::istream& stream, Value& value)
{
    using Unsigned = std::make_unsigned_t<Value>;
    std::array<unsigned char, sizeof(Value)> bytes{};
    stream.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        return false;
    }
    Unsigned bits = 0U;
    for (std::size_t index = bytes.size(); index > 0U; --index) {
        bits = static_cast<Unsigned>((bits << 8U) | bytes[index - 1U]);
    }
    value = static_cast<Value>(bits);
    return true;
}

bool write_float(std::ostream& stream, const float value)
{
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    static_assert(std::numeric_limits<float>::is_iec559);
    std::uint32_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    return write_little_endian(stream, bits);
}

bool read_float(std::istream& stream, float& value)
{
    std::uint32_t bits = 0U;
    if (!read_little_endian(stream, bits)) {
        return false;
    }
    std::memcpy(&value, &bits, sizeof(value));
    return std::isfinite(value);
}

bool write_double(std::ostream& stream, const double value)
{
    static_assert(sizeof(double) == sizeof(std::uint64_t));
    static_assert(std::numeric_limits<double>::is_iec559);
    std::uint64_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    return write_little_endian(stream, bits);
}

bool read_double(std::istream& stream, double& value)
{
    std::uint64_t bits = 0U;
    if (!read_little_endian(stream, bits)) {
        return false;
    }
    std::memcpy(&value, &bits, sizeof(value));
    return std::isfinite(value);
}

std::filesystem::path temporary_path(const std::filesystem::path& cache_path)
{
    std::filesystem::path result = cache_path;
    result += ".tmp";
    return result;
}

bool flush_file(const std::filesystem::path& path) noexcept
{
#ifdef _WIN32
    const HANDLE handle = CreateFileW(
        path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    const bool result = FlushFileBuffers(handle) != FALSE;
    CloseHandle(handle);
    return result;
#else
    const int file = open(path.c_str(), O_RDONLY);
    if (file < 0) {
        return false;
    }
    const bool result = fsync(file) == 0;
    close(file);
    return result;
#endif
}

bool atomic_replace(const std::filesystem::path& from,
                    const std::filesystem::path& to) noexcept
{
#ifdef _WIN32
    constexpr int max_attempts = 6;
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        if (MoveFileExW(from.c_str(), to.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
            != FALSE) {
            return true;
        }
        const DWORD error = GetLastError();
        if (error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION
            && error != ERROR_LOCK_VIOLATION) {
            return false;
        }
        if (attempt + 1 < max_attempts) {
            Sleep(10);
        }
    }
    return false;
#else
    std::error_code error;
    std::filesystem::rename(from, to, error);
    return !error;
#endif
}

bool validate_layer(const std::vector<float>& layer) noexcept
{
    for (const float value : layer) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return true;
}

bool native_little_endian() noexcept
{
    const std::uint32_t value = 1U;
    return *reinterpret_cast<const unsigned char*>(&value) == 1U;
}

bool write_layer(std::ostream& stream, const std::vector<float>& layer)
{
    if (native_little_endian()) {
        // The on-disk IEEE-754 little-endian representation matches native
        // float bytes. Bound each transfer and avoid one stream call per float.
        constexpr std::size_t chunk_floats = 65536U / sizeof(float);
        for (std::size_t offset = 0; offset < layer.size();) {
            const auto count = std::min(chunk_floats, layer.size() - offset);
            stream.write(reinterpret_cast<const char*>(layer.data() + offset),
                         static_cast<std::streamsize>(count * sizeof(float)));
            if (!stream) return false;
            offset += count;
        }
        return true;
    }
    for (const float value : layer) {
        if (!write_float(stream, value)) {
            return false;
        }
    }
    return true;
}

bool read_layer(std::istream& stream,
                const std::uint64_t count,
                std::vector<float>& layer)
{
    layer.clear();
    if (count == 0U) {
        return true;
    }
    if (native_little_endian()) {
        layer.resize(static_cast<std::size_t>(count));
        constexpr std::size_t chunk_floats = 65536U / sizeof(float);
        for (std::size_t offset = 0; offset < layer.size();) {
            const auto chunk = std::min(chunk_floats, layer.size() - offset);
            stream.read(reinterpret_cast<char*>(layer.data() + offset),
                        static_cast<std::streamsize>(chunk * sizeof(float)));
            if (!stream) {
                layer.clear();
                return false;
            }
            for (std::size_t index = offset; index < offset + chunk; ++index) {
                if (!std::isfinite(layer[index])) {
                    layer.clear();
                    return false;
                }
            }
            offset += chunk;
        }
        return true;
    }
    layer.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0U; index < count; ++index) {
        float value = 0.0F;
        if (!read_float(stream, value)) {
            layer.clear();
            return false;
        }
        layer.push_back(value);
    }
    return true;
}

std::string key_for_schema(const std::filesystem::path& source_path,
                           const std::uint32_t schema_version)
{
    SourceMetadata metadata;
    if (!source_metadata(source_path, metadata)) {
        return {};
    }

    std::uint64_t hash = fnv_offset;
    const std::string canonical = canonical_path(source_path).u8string();
    for (const unsigned char byte : canonical) {
        hash_byte(hash, byte);
    }
    hash_byte(hash, 0U);
    hash_integer(hash, metadata.size);
    hash_integer(hash, metadata.mtime_ns);
    hash_integer(hash, schema_version);

    std::ostringstream key;
    key << std::hex << std::setfill('0') << std::setw(16) << hash;
    return key.str();
}

} // namespace

std::string WaveformCache::key_for(
    const std::filesystem::path& source_path)
{
    return key_for_schema(source_path, analysis_schema_version);
}

std::string WaveformCache::legacy_v2_key_for(
    const std::filesystem::path& source_path)
{
    return key_for_schema(source_path, cache_version_v2);
}

bool WaveformCache::load(const std::filesystem::path& cache_path,
                         const std::filesystem::path& source_path,
                         std::vector<float>& peaks) noexcept
{
    peaks.clear();
    try {
        SourceMetadata source;
        if (!source_metadata(source_path, source)) {
            return false;
        }

        std::error_code error;
        const std::uintmax_t file_size =
            std::filesystem::file_size(cache_path, error);
        if (error || file_size < v1_header_size) {
            return false;
        }

        std::ifstream input(cache_path, std::ios::binary);
        std::array<char, cache_magic.size()> magic{};
        input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
        std::uint32_t version = 0U;
        std::uint64_t source_size = 0U;
        std::int64_t source_mtime = 0;
        std::uint64_t point_count = 0U;
        if (!input || magic != cache_magic
            || !read_little_endian(input, version)
            || !read_little_endian(input, source_size)
            || !read_little_endian(input, source_mtime)
            || !read_little_endian(input, point_count)
            || version != cache_version_v1 || source_size != source.size
            || source_mtime != source.mtime_ns
            || point_count
                   > static_cast<std::uint64_t>(
                       std::numeric_limits<std::size_t>::max())
            || point_count
                   > (std::numeric_limits<std::uint64_t>::max() - v1_header_size)
                         / sizeof(float)
            || file_size != v1_header_size + point_count * sizeof(float)) {
            return false;
        }

        std::vector<float> loaded(static_cast<std::size_t>(point_count));
        for (float& peak : loaded) {
            if (!read_float(input, peak)) {
                return false;
            }
        }
        peaks = std::move(loaded);
        return true;
    } catch (...) {
        peaks.clear();
        return false;
    }
}

bool WaveformCache::save(const std::filesystem::path& cache_path,
                         const std::filesystem::path& source_path,
                         const std::vector<float>& peaks) noexcept
{
    const std::filesystem::path temp_path = temporary_path(cache_path);
    try {
        std::error_code cleanup_error;
        std::filesystem::remove(temp_path, cleanup_error);

        SourceMetadata source;
        if (!source_metadata(source_path, source)) {
            return false;
        }
        if (!validate_layer(peaks)) {
            return false;
        }

        std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
        output.write(cache_magic.data(),
                     static_cast<std::streamsize>(cache_magic.size()));
        if (!output || !write_little_endian(output, cache_version_v1)
            || !write_little_endian(output, source.size)
            || !write_little_endian(output, source.mtime_ns)
            || !write_little_endian(
                output, static_cast<std::uint64_t>(peaks.size()))) {
            output.close();
            std::filesystem::remove(temp_path, cleanup_error);
            return false;
        }
        if (!write_layer(output, peaks)) {
            output.close();
            std::filesystem::remove(temp_path, cleanup_error);
            return false;
        }
        output.flush();
        const bool write_succeeded = static_cast<bool>(output);
        output.close();
        if (!write_succeeded || !flush_file(temp_path)
            || !atomic_replace(temp_path, cache_path)) {
            std::filesystem::remove(temp_path, cleanup_error);
            return false;
        }
        return true;
    } catch (...) {
        std::error_code cleanup_error;
        std::filesystem::remove(temp_path, cleanup_error);
        return false;
    }
}

bool WaveformCache::save_v2(const std::filesystem::path& cache_path,
                            const std::filesystem::path& source_path,
                            const WaveformCacheData& data) noexcept
{
    const std::filesystem::path temp_path = temporary_path(cache_path);
    try {
        std::error_code cleanup_error;
        std::filesystem::remove(temp_path, cleanup_error);

        SourceMetadata source;
        if (!source_metadata(source_path, source)) {
            return false;
        }
        if (!validate_layer(data.mix) || !validate_layer(data.bass)
            || !validate_layer(data.mid) || !validate_layer(data.high)) {
            return false;
        }
        for (const auto& cue : data.cues) {
            if (cue.label.size() > std::numeric_limits<std::uint8_t>::max()) {
                return false;
            }
        }

        const auto safe_count = [](const std::vector<float>& layer) {
            return static_cast<std::uint64_t>(layer.size());
        };
        const std::uint64_t mix_count = safe_count(data.mix);
        const std::uint64_t bass_count = safe_count(data.bass);
        const std::uint64_t mid_count = safe_count(data.mid);
        const std::uint64_t high_count = safe_count(data.high);
        const std::uint64_t cue_count =
            static_cast<std::uint64_t>(data.cues.size());

        // Guard against size_t overflow when computing the payload length.
        const std::uint64_t max_floats =
            std::numeric_limits<std::uint64_t>::max() / sizeof(float);
        if (mix_count > max_floats || bass_count > max_floats
            || mid_count > max_floats || high_count > max_floats
            || mix_count + bass_count > max_floats
            || mix_count + bass_count + mid_count > max_floats
            || mix_count + bass_count + mid_count + high_count > max_floats) {
            return false;
        }
        const std::uint64_t float_bytes =
            (mix_count + bass_count + mid_count + high_count) * sizeof(float);
        if (float_bytes > std::numeric_limits<std::uint64_t>::max()
                                - v2_timeline_header_size) {
            return false;
        }

        std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
        output.write(cache_magic.data(),
                     static_cast<std::streamsize>(cache_magic.size()));
        if (!output
            || !write_little_endian(output, cache_version_v2)
            || !write_little_endian(output, source.size)
            || !write_little_endian(output, source.mtime_ns)
            || !write_little_endian(output, mix_count)
            || !write_little_endian(output, bass_count)
            || !write_little_endian(output, mid_count)
            || !write_little_endian(output, high_count)
            || !write_double(output, data.bpm)
            || !write_little_endian(output, cue_count)
            || !write_little_endian(output, v2_timeline_metadata_flag)
            || !write_little_endian(output, data.duration_ms)
            || !write_little_endian(output, data.total_samples)
            || !write_little_endian(
                output, static_cast<std::uint64_t>(data.sample_rate))) {
            output.close();
            std::filesystem::remove(temp_path, cleanup_error);
            return false;
        }

        if (!write_layer(output, data.mix) || !write_layer(output, data.bass)
            || !write_layer(output, data.mid)
            || !write_layer(output, data.high)) {
            output.close();
            std::filesystem::remove(temp_path, cleanup_error);
            return false;
        }

        for (const auto& cue : data.cues) {
            if (!write_little_endian(output, cue.position_ms)
                || !write_little_endian(
                    output, static_cast<std::uint8_t>(cue.label.size()))) {
                output.close();
                std::filesystem::remove(temp_path, cleanup_error);
                return false;
            }
            if (!cue.label.empty()) {
                output.write(cue.label.data(),
                             static_cast<std::streamsize>(cue.label.size()));
            }
            if (!output) {
                output.close();
                std::filesystem::remove(temp_path, cleanup_error);
                return false;
            }
        }

        output.flush();
        const bool write_succeeded = static_cast<bool>(output);
        output.close();
        if (!write_succeeded || !flush_file(temp_path)
            || !atomic_replace(temp_path, cache_path)) {
            std::filesystem::remove(temp_path, cleanup_error);
            return false;
        }
        return true;
    } catch (...) {
        std::error_code cleanup_error;
        std::filesystem::remove(temp_path, cleanup_error);
        return false;
    }
}

bool WaveformCache::load_v2(const std::filesystem::path& cache_path,
                            const std::filesystem::path& source_path,
                            WaveformCacheData& data) noexcept
{
    data = WaveformCacheData{};
    try {
        SourceMetadata source;
        if (!source_metadata(source_path, source)) {
            return false;
        }

        std::error_code error;
        const std::uintmax_t file_size =
            std::filesystem::file_size(cache_path, error);
        if (error || file_size < v2_header_size) {
            return false;
        }

        std::ifstream input(cache_path, std::ios::binary);
        std::array<char, cache_magic.size()> magic{};
        input.read(magic.data(), static_cast<std::streamsize>(magic.size()));

        std::uint32_t version = 0U;
        std::uint64_t source_size = 0U;
        std::int64_t source_mtime = 0;
        std::uint64_t mix_count = 0U;
        std::uint64_t bass_count = 0U;
        std::uint64_t mid_count = 0U;
        std::uint64_t high_count = 0U;
        double bpm = 0.0;
        std::uint64_t cue_count = 0U;
        std::uint64_t flags = 0U;
        std::uint64_t reserved = 0U;

        if (!input || magic != cache_magic
            || !read_little_endian(input, version)
            || !read_little_endian(input, source_size)
            || !read_little_endian(input, source_mtime)
            || !read_little_endian(input, mix_count)
            || !read_little_endian(input, bass_count)
            || !read_little_endian(input, mid_count)
            || !read_little_endian(input, high_count)
            || !read_double(input, bpm)
            || !read_little_endian(input, cue_count)
            || !read_little_endian(input, flags)
            || !read_little_endian(input, reserved)
            || version != cache_version_v2 || source_size != source.size
            || source_mtime != source.mtime_ns
            || !std::isfinite(bpm)) {
            return false;
        }

        const bool has_timeline_metadata =
            (flags & v2_timeline_metadata_flag) != 0U;
        const std::uint64_t header_size = has_timeline_metadata
            ? v2_timeline_header_size : v2_header_size;
        std::uint64_t total_samples = 0U;
        std::uint64_t sample_rate = 0U;
        if (has_timeline_metadata
            && (!read_little_endian(input, total_samples)
                || !read_little_endian(input, sample_rate)
                || sample_rate > std::numeric_limits<std::uint32_t>::max())) {
            return false;
        }

        const std::uint64_t max_count =
            std::numeric_limits<std::uint64_t>::max() / sizeof(float);
        if (mix_count > max_count || bass_count > max_count
            || mid_count > max_count || high_count > max_count
            || mix_count + bass_count > max_count
            || mix_count + bass_count + mid_count > max_count
            || mix_count + bass_count + mid_count + high_count > max_count) {
            return false;
        }
        const std::uint64_t float_bytes =
            (mix_count + bass_count + mid_count + high_count) * sizeof(float);
        if (float_bytes > std::numeric_limits<std::uint64_t>::max()
                                - header_size
            || file_size < header_size + float_bytes) {
            return false;
        }

        WaveformCacheData loaded;
        loaded.bpm = bpm;
        loaded.duration_ms = reserved;
        loaded.total_samples = total_samples;
        loaded.sample_rate = static_cast<std::uint32_t>(sample_rate);
        if (!read_layer(input, mix_count, loaded.mix)
            || !read_layer(input, bass_count, loaded.bass)
            || !read_layer(input, mid_count, loaded.mid)
            || !read_layer(input, high_count, loaded.high)) {
            return false;
        }

        const std::uint64_t payload_bytes = header_size + float_bytes;
        const std::uint64_t remaining =
            static_cast<std::uint64_t>(file_size) - payload_bytes;

        // Each cue needs at least an 8-byte position and a 1-byte label length.
        if (cue_count > remaining / 9U) {
            return false;
        }

        std::uint64_t cue_consumed = 0U;
        for (std::uint64_t index = 0U; index < cue_count; ++index) {
            WaveformCacheCue cue;
            std::uint8_t label_length = 0U;
            constexpr std::uint64_t cue_header_size =
                sizeof(std::uint64_t) + sizeof(std::uint8_t);
            if (cue_consumed + cue_header_size > remaining
                || !read_little_endian(input, cue.position_ms)
                || !read_little_endian(input, label_length)) {
                return false;
            }
            cue_consumed += cue_header_size;
            if (cue_consumed + label_length > remaining) {
                return false;
            }
            if (label_length > 0U) {
                cue.label.resize(static_cast<std::size_t>(label_length));
                input.read(cue.label.data(),
                           static_cast<std::streamsize>(label_length));
                if (!input) {
                    return false;
                }
            }
            cue_consumed += label_length;
            loaded.cues.push_back(std::move(cue));
        }

        if (cue_consumed != remaining) {
            return false;
        }

        data = std::move(loaded);
        return true;
    } catch (...) {
        data = WaveformCacheData{};
        return false;
    }
}

bool WaveformCache::save_v4(const std::filesystem::path& cache_path,
                            const std::filesystem::path& source_path,
                            const WaveformCacheData& data) noexcept
{
    const std::filesystem::path temp_path = temporary_path(cache_path);
    try {
        const bool has_detail = !data.peak.empty() || !data.rms.empty();
        if (has_detail && (data.peak.size() != data.mix.size()
            || data.rms.size() != data.mix.size()
            || !validate_layer(data.peak) || !validate_layer(data.rms))) return false;
        std::error_code cleanup_error;
        std::filesystem::remove(temp_path, cleanup_error);
        SourceMetadata source;
        if (!source_metadata(source_path, source)
            || !validate_layer(data.mix) || !validate_layer(data.bass)
            || !validate_layer(data.mid) || !validate_layer(data.high)
            || data.mix.empty() || data.bass.size() != data.mix.size()
            || data.mid.size() != data.mix.size()
            || data.high.size() != data.mix.size()) {
            return false;
        }
        for (const auto& cue : data.cues) {
            if (cue.label.size() > std::numeric_limits<std::uint8_t>::max()) {
                return false;
            }
        }

        const std::uint64_t mix_count = data.mix.size();
        const std::uint64_t bass_count = data.bass.size();
        const std::uint64_t mid_count = data.mid.size();
        const std::uint64_t high_count = data.high.size();
        const std::uint64_t cue_count = data.cues.size();
        const std::uint64_t max_floats =
            std::numeric_limits<std::uint64_t>::max() / sizeof(float);
        if (mix_count > max_floats || bass_count > max_floats
            || mid_count > max_floats || high_count > max_floats
            || mix_count + bass_count > max_floats
            || mix_count + bass_count + mid_count > max_floats
            || mix_count + bass_count + mid_count + high_count > max_floats) {
            return false;
        }

        std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
        output.write(cache_magic.data(),
                     static_cast<std::streamsize>(cache_magic.size()));
        if (!output
            || !write_little_endian(output, cache_version_v4)
            || !write_little_endian(output, source.size)
            || !write_little_endian(output, source.mtime_ns)
            || !write_little_endian(output, mix_count)
            || !write_little_endian(output, bass_count)
            || !write_little_endian(output, mid_count)
            || !write_little_endian(output, high_count)
            || !write_double(output, data.bpm)
            || !write_little_endian(output, cue_count)
            || !write_little_endian(output, v2_timeline_metadata_flag
                | (has_detail ? amplitude_detail_flag : 0U))
            || !write_little_endian(output, data.duration_ms)
            || !write_little_endian(output, data.total_samples)
            || !write_little_endian(
                output, static_cast<std::uint64_t>(data.sample_rate))
            || !write_layer(output, data.mix)
            || !write_layer(output, data.bass)
            || !write_layer(output, data.mid)
            || !write_layer(output, data.high)
            || (has_detail && (!write_layer(output, data.peak)
                               || !write_layer(output, data.rms)))) {
            output.close();
            std::filesystem::remove(temp_path, cleanup_error);
            return false;
        }
        for (const auto& cue : data.cues) {
            if (!write_little_endian(output, cue.position_ms)
                || !write_little_endian(
                    output, static_cast<std::uint8_t>(cue.label.size()))) {
                output.close();
                std::filesystem::remove(temp_path, cleanup_error);
                return false;
            }
            if (!cue.label.empty()) {
                output.write(cue.label.data(),
                             static_cast<std::streamsize>(cue.label.size()));
            }
        }
        output.flush();
        const bool write_succeeded = static_cast<bool>(output);
        output.close();
        if (!write_succeeded || !flush_file(temp_path)
            || !atomic_replace(temp_path, cache_path)) {
            std::filesystem::remove(temp_path, cleanup_error);
            return false;
        }
        return true;
    } catch (...) {
        std::error_code cleanup_error;
        std::filesystem::remove(temp_path, cleanup_error);
        return false;
    }
}

bool WaveformCache::load_v4(const std::filesystem::path& cache_path,
                            const std::filesystem::path& source_path,
                            WaveformCacheData& data) noexcept
{
    data = WaveformCacheData{};
    try {
        SourceMetadata source;
        if (!source_metadata(source_path, source)) {
            return false;
        }
        std::error_code error;
        const std::uintmax_t file_size =
            std::filesystem::file_size(cache_path, error);
        if (error || file_size < v4_header_size) {
            return false;
        }
        std::ifstream input(cache_path, std::ios::binary);
        std::array<char, cache_magic.size()> magic{};
        input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
        std::uint32_t version = 0U;
        std::uint64_t source_size = 0U;
        std::int64_t source_mtime = 0;
        std::uint64_t mix_count = 0U;
        std::uint64_t bass_count = 0U;
        std::uint64_t mid_count = 0U;
        std::uint64_t high_count = 0U;
        double bpm = 0.0;
        std::uint64_t cue_count = 0U;
        std::uint64_t flags = 0U;
        std::uint64_t duration_ms = 0U;
        std::uint64_t total_samples = 0U;
        std::uint64_t sample_rate = 0U;
        if (!input || magic != cache_magic
            || !read_little_endian(input, version)
            || !read_little_endian(input, source_size)
            || !read_little_endian(input, source_mtime)
            || !read_little_endian(input, mix_count)
            || !read_little_endian(input, bass_count)
            || !read_little_endian(input, mid_count)
            || !read_little_endian(input, high_count)
            || !read_double(input, bpm)
            || !read_little_endian(input, cue_count)
            || !read_little_endian(input, flags)
            || !read_little_endian(input, duration_ms)
            || !read_little_endian(input, total_samples)
            || !read_little_endian(input, sample_rate)
            || version != cache_version_v4 || source_size != source.size
            || source_mtime != source.mtime_ns
            || (flags != v2_timeline_metadata_flag
                && flags != (v2_timeline_metadata_flag | amplitude_detail_flag))
            || !std::isfinite(bpm)
            || sample_rate > std::numeric_limits<std::uint32_t>::max()
            || mix_count == 0U || bass_count != mix_count
            || mid_count != mix_count || high_count != mix_count) {
            return false;
        }
        const std::uint64_t max_count =
            std::numeric_limits<std::uint64_t>::max() / sizeof(float);
        if (mix_count > max_count || bass_count > max_count
            || mid_count > max_count || high_count > max_count
            || mix_count + bass_count > max_count
            || mix_count + bass_count + mid_count > max_count
            || mix_count + bass_count + mid_count + high_count > max_count) {
            return false;
        }
        const bool has_detail = (flags & amplitude_detail_flag) != 0U;
        const std::uint64_t layer_count = has_detail ? 6U : 4U;
        if (mix_count > max_count / layer_count) return false;
        const std::uint64_t float_bytes = mix_count * layer_count * sizeof(float);
        if (float_bytes > std::numeric_limits<std::uint64_t>::max()
                                - v4_header_size
            || file_size < v4_header_size + float_bytes) {
            return false;
        }

        WaveformCacheData loaded;
        loaded.bpm = bpm;
        loaded.duration_ms = duration_ms;
        loaded.total_samples = total_samples;
        loaded.sample_rate = static_cast<std::uint32_t>(sample_rate);
        if (!read_layer(input, mix_count, loaded.mix)
            || !read_layer(input, bass_count, loaded.bass)
            || !read_layer(input, mid_count, loaded.mid)
            || !read_layer(input, high_count, loaded.high)
            || (has_detail && (!read_layer(input, mix_count, loaded.peak)
                               || !read_layer(input, mix_count, loaded.rms)))) {
            return false;
        }
        const std::uint64_t payload_bytes =
            v4_header_size + float_bytes;
        const std::uint64_t remaining =
            static_cast<std::uint64_t>(file_size) - payload_bytes;
        if (cue_count > remaining / 9U) {
            return false;
        }
        std::uint64_t cue_consumed = 0U;
        for (std::uint64_t index = 0U; index < cue_count; ++index) {
            WaveformCacheCue cue;
            std::uint8_t label_length = 0U;
            constexpr std::uint64_t cue_header_size = 9U;
            if (cue_consumed + cue_header_size > remaining
                || !read_little_endian(input, cue.position_ms)
                || !read_little_endian(input, label_length)) {
                return false;
            }
            cue_consumed += cue_header_size;
            if (cue_consumed + label_length > remaining) {
                return false;
            }
            if (label_length > 0U) {
                cue.label.resize(label_length);
                input.read(cue.label.data(), label_length);
                if (!input) {
                    return false;
                }
            }
            cue_consumed += label_length;
            loaded.cues.push_back(std::move(cue));
        }
        if (cue_consumed != remaining) {
            return false;
        }
        data = std::move(loaded);
        return true;
    } catch (...) {
        data = WaveformCacheData{};
        return false;
    }
}

} // namespace agplayer
