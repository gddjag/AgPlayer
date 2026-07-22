#include "waveform_cache.hpp"

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
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace agplayer {
namespace {

constexpr std::array<char, 4> cache_magic{'A', 'G', 'W', 'F'};
constexpr std::uint32_t cache_version = 1U;
constexpr std::uint64_t header_size = 32U;
constexpr std::uint64_t fnv_offset = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;

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
        bits >>= 8U;
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
    return MoveFileExW(from.c_str(), to.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
           != FALSE;
#else
    std::error_code error;
    std::filesystem::rename(from, to, error);
    return !error;
#endif
}

} // namespace

std::string WaveformCache::key_for(
    const std::filesystem::path& source_path)
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
    hash_integer(hash, cache_version);

    std::ostringstream key;
    key << std::hex << std::setfill('0') << std::setw(16) << hash;
    return key.str();
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
        if (error || file_size < header_size) {
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
            || version != cache_version || source_size != source.size
            || source_mtime != source.mtime_ns
            || point_count
                   > static_cast<std::uint64_t>(
                       std::numeric_limits<std::size_t>::max())
            || point_count
                   > (std::numeric_limits<std::uint64_t>::max() - header_size)
                         / sizeof(float)
            || file_size != header_size + point_count * sizeof(float)) {
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
        for (const float peak : peaks) {
            if (!std::isfinite(peak)) {
                return false;
            }
        }

        std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
        output.write(cache_magic.data(),
                     static_cast<std::streamsize>(cache_magic.size()));
        if (!output || !write_little_endian(output, cache_version)
            || !write_little_endian(output, source.size)
            || !write_little_endian(output, source.mtime_ns)
            || !write_little_endian(
                output, static_cast<std::uint64_t>(peaks.size()))) {
            output.close();
            std::filesystem::remove(temp_path, cleanup_error);
            return false;
        }
        for (const float peak : peaks) {
            if (!write_float(output, peak)) {
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

} // namespace agplayer
