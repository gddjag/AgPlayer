#include "frequency_color_waveform_cache.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
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

constexpr std::array<std::uint8_t, 4> kMagic{'F', 'C', 'W', '1'};
constexpr std::uint16_t kFormatVersion = 1U;
constexpr std::uint16_t kHeaderSize = 64U;
constexpr std::uint32_t kMaximumPointCount = 1'000'000U;
constexpr std::uint64_t kFnvOffset = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t kFnvPrime = 1'099'511'628'211ULL;
std::atomic_bool fail_next_replace{false};

constexpr std::size_t kFormatVersionOffset = 4U;
constexpr std::size_t kHeaderSizeOffset = 6U;
constexpr std::size_t kPointCountOffset = 8U;
constexpr std::size_t kSampleRateOffset = 12U;
constexpr std::size_t kCrossoverLowOffset = 16U;
constexpr std::size_t kCrossoverHighOffset = 18U;
constexpr std::size_t kAlgorithmOffset = 20U;
constexpr std::size_t kSourceSizeOffset = 24U;
constexpr std::size_t kSourceMtimeOffset = 32U;
constexpr std::size_t kTimelineFramesOffset = 40U;
constexpr std::size_t kPathFingerprintOffset = 48U;
constexpr std::size_t kPayloadCrcOffset = 56U;
constexpr std::size_t kHeaderCrcOffset = 60U;

struct SourceIdentity final {
    std::uint64_t size = 0U;
    std::int64_t mtime_ns = 0;
    std::uint64_t path_fingerprint = 0U;
};

template <typename Value>
void store_little_endian(std::array<std::uint8_t, kHeaderSize>& bytes,
                         const std::size_t offset,
                         const Value value) noexcept
{
    using Unsigned = std::make_unsigned_t<Value>;
    Unsigned bits = static_cast<Unsigned>(value);
    for (std::size_t index = 0U; index < sizeof(Value); ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>(bits & 0xFFU);
        bits >>= 8U;
    }
}

template <typename Value>
Value load_little_endian(
    const std::array<std::uint8_t, kHeaderSize>& bytes,
    const std::size_t offset) noexcept
{
    using Unsigned = std::make_unsigned_t<Value>;
    Unsigned bits = 0U;
    for (std::size_t index = sizeof(Value); index > 0U; --index) {
        bits = static_cast<Unsigned>(
            (bits << 8U) | bytes[offset + index - 1U]);
    }
    return static_cast<Value>(bits);
}

std::uint32_t crc32(const std::uint8_t* const data,
                    const std::size_t size) noexcept
{
    std::uint32_t crc = 0xFFFF'FFFFU;
    for (std::size_t index = 0U; index < size; ++index) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask =
                0U - static_cast<std::uint32_t>(crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB8'8320U & mask);
        }
    }
    return crc ^ 0xFFFF'FFFFU;
}

std::filesystem::path canonical_path(
    const std::filesystem::path& source_path)
{
    std::error_code error;
    std::filesystem::path result =
        std::filesystem::weakly_canonical(source_path, error);
    if (error) {
        error.clear();
        result = std::filesystem::absolute(source_path, error);
        if (error) {
            result = source_path;
        }
    }
    return result.lexically_normal();
}

std::uint64_t path_fingerprint(const std::filesystem::path& path)
{
    const std::string normalized = canonical_path(path).generic_u8string();
    std::uint64_t hash = kFnvOffset;
    for (const unsigned char byte : normalized) {
        hash ^= byte;
        hash *= kFnvPrime;
    }
    return hash;
}

bool identifies_same_file(const std::filesystem::path& left,
                          const std::filesystem::path& right) noexcept
{
    try {
        std::error_code equivalent_error;
        if (std::filesystem::equivalent(left, right, equivalent_error)) {
            return true;
        }

        const std::filesystem::path normalized_left = canonical_path(left);
        const std::filesystem::path normalized_right = canonical_path(right);
#ifdef _WIN32
        const std::wstring left_native = normalized_left.native();
        const std::wstring right_native = normalized_right.native();
        if (left_native.size() > static_cast<std::size_t>(INT_MAX)
            || right_native.size() > static_cast<std::size_t>(INT_MAX)) {
            return true;
        }
        return CompareStringOrdinal(
                   left_native.c_str(), static_cast<int>(left_native.size()),
                   right_native.c_str(), static_cast<int>(right_native.size()),
                   TRUE)
               == CSTR_EQUAL;
#else
        return normalized_left == normalized_right;
#endif
    } catch (...) {
        return true;
    }
}

bool source_identity(const std::filesystem::path& source_path,
                     SourceIdentity& identity) noexcept
{
    try {
        std::error_code error;
        const std::uintmax_t size =
            std::filesystem::file_size(source_path, error);
        if (error || size > std::numeric_limits<std::uint64_t>::max()) {
            return false;
        }
        const auto mtime =
            std::filesystem::last_write_time(source_path, error);
        if (error) {
            return false;
        }
        identity.size = static_cast<std::uint64_t>(size);
        identity.mtime_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                mtime.time_since_epoch())
                .count();
        identity.path_fingerprint = path_fingerprint(source_path);
        return true;
    } catch (...) {
        return false;
    }
}

std::uint64_t process_id() noexcept
{
#ifdef _WIN32
    return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint64_t>(getpid());
#endif
}

std::filesystem::path temporary_path(
    const std::filesystem::path& cache_path)
{
    static std::atomic_uint64_t counter{0U};
    std::filesystem::path result = cache_path;
    result += ".tmp-" + std::to_string(process_id()) + "-"
              + std::to_string(counter.fetch_add(1U,
                                                  std::memory_order_relaxed));
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
    if (fail_next_replace.exchange(false, std::memory_order_relaxed)) {
        return false;
    }
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

bool same_data(const FrequencyColorCacheData& left,
               const FrequencyColorCacheData& right) noexcept
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

} // namespace

namespace testing {

void fail_next_frequency_color_cache_replace() noexcept
{
    fail_next_replace.store(true, std::memory_order_relaxed);
}

} // namespace testing

std::uint8_t quantize_frequency_color_peak(const float value) noexcept
{
    if (!std::isfinite(value) || value <= 0.0F) {
        return 0U;
    }
    const float normalized = std::min(value / 0.98F, 1.0F);
    return static_cast<std::uint8_t>(std::lround(normalized * 255.0F));
}

bool FrequencyColorWaveformCache::load(
    const std::filesystem::path& cache,
    const std::filesystem::path& source,
    const std::uint32_t expected_algorithm,
    FrequencyColorCacheData& out) noexcept
{
    try {
        SourceIdentity expected_source;
        if (!source_identity(source, expected_source)) {
            return false;
        }

        std::error_code size_error;
        const std::uintmax_t file_size =
            std::filesystem::file_size(cache, size_error);
        if (size_error || file_size < kHeaderSize) {
            return false;
        }

        std::ifstream input(cache, std::ios::binary);
        std::array<std::uint8_t, kHeaderSize> header{};
        input.read(reinterpret_cast<char*>(header.data()),
                   static_cast<std::streamsize>(header.size()));
        if (!input || !std::equal(kMagic.begin(), kMagic.end(), header.begin())
            || load_little_endian<std::uint16_t>(
                   header, kFormatVersionOffset) != kFormatVersion
            || load_little_endian<std::uint16_t>(header, kHeaderSizeOffset)
                   != kHeaderSize) {
            return false;
        }

        const std::uint32_t stored_header_crc =
            load_little_endian<std::uint32_t>(header, kHeaderCrcOffset);
        std::array<std::uint8_t, kHeaderSize> checked_header = header;
        store_little_endian(checked_header, kHeaderCrcOffset, 0U);
        if (crc32(checked_header.data(), checked_header.size())
            != stored_header_crc) {
            return false;
        }

        const std::uint32_t point_count =
            load_little_endian<std::uint32_t>(header, kPointCountOffset);
        if (point_count == 0U || point_count > kMaximumPointCount) {
            return false;
        }
        const std::uint64_t payload_size =
            static_cast<std::uint64_t>(point_count) * 3U;
        if (payload_size > std::numeric_limits<std::size_t>::max()
            || payload_size > std::numeric_limits<std::uint64_t>::max()
                                  - kHeaderSize
            || file_size != kHeaderSize + payload_size) {
            return false;
        }

        if (load_little_endian<std::uint32_t>(header, kAlgorithmOffset)
                != expected_algorithm
            || load_little_endian<std::uint64_t>(header, kSourceSizeOffset)
                   != expected_source.size
            || load_little_endian<std::int64_t>(header, kSourceMtimeOffset)
                   != expected_source.mtime_ns
            || load_little_endian<std::uint64_t>(
                   header, kPathFingerprintOffset)
                   != expected_source.path_fingerprint) {
            return false;
        }

        std::vector<std::uint8_t> payload(
            static_cast<std::size_t>(payload_size));
        input.read(reinterpret_cast<char*>(payload.data()),
                   static_cast<std::streamsize>(payload.size()));
        if (!input || crc32(payload.data(), payload.size())
                          != load_little_endian<std::uint32_t>(
                              header, kPayloadCrcOffset)) {
            return false;
        }

        FrequencyColorCacheData loaded;
        loaded.point_count = point_count;
        loaded.sample_rate =
            load_little_endian<std::uint32_t>(header, kSampleRateOffset);
        loaded.crossover_low_hz =
            load_little_endian<std::uint16_t>(header, kCrossoverLowOffset);
        loaded.crossover_high_hz =
            load_little_endian<std::uint16_t>(header, kCrossoverHighOffset);
        loaded.algorithm_version = expected_algorithm;
        loaded.timeline_frames =
            load_little_endian<std::uint64_t>(header, kTimelineFramesOffset);
        const std::size_t count = static_cast<std::size_t>(point_count);
        loaded.low.assign(payload.begin(), payload.begin() + count);
        loaded.mid.assign(payload.begin() + count,
                          payload.begin() + 2U * count);
        loaded.high.assign(payload.begin() + 2U * count, payload.end());
        out = std::move(loaded);
        return true;
    } catch (...) {
        return false;
    }
}

bool FrequencyColorWaveformCache::save_atomic(
    const std::filesystem::path& cache,
    const std::filesystem::path& source,
    const FrequencyColorCacheData& data) noexcept
{
    std::filesystem::path temp;
    try {
        if (identifies_same_file(cache, source)) {
            return false;
        }
        if (data.point_count == 0U
            || data.point_count > kMaximumPointCount
            || data.low.size() != data.point_count
            || data.mid.size() != data.point_count
            || data.high.size() != data.point_count) {
            return false;
        }

        SourceIdentity source_value;
        if (!source_identity(source, source_value)) {
            return false;
        }

        std::vector<std::uint8_t> payload;
        payload.reserve(static_cast<std::size_t>(data.point_count) * 3U);
        payload.insert(payload.end(), data.low.begin(), data.low.end());
        payload.insert(payload.end(), data.mid.begin(), data.mid.end());
        payload.insert(payload.end(), data.high.begin(), data.high.end());

        std::array<std::uint8_t, kHeaderSize> header{};
        std::copy(kMagic.begin(), kMagic.end(), header.begin());
        store_little_endian(header, kFormatVersionOffset, kFormatVersion);
        store_little_endian(header, kHeaderSizeOffset, kHeaderSize);
        store_little_endian(header, kPointCountOffset, data.point_count);
        store_little_endian(header, kSampleRateOffset, data.sample_rate);
        store_little_endian(header, kCrossoverLowOffset,
                            data.crossover_low_hz);
        store_little_endian(header, kCrossoverHighOffset,
                            data.crossover_high_hz);
        store_little_endian(header, kAlgorithmOffset,
                            data.algorithm_version);
        store_little_endian(header, kSourceSizeOffset, source_value.size);
        store_little_endian(header, kSourceMtimeOffset,
                            source_value.mtime_ns);
        store_little_endian(header, kTimelineFramesOffset,
                            data.timeline_frames);
        store_little_endian(header, kPathFingerprintOffset,
                            source_value.path_fingerprint);
        store_little_endian(header, kPayloadCrcOffset,
                            crc32(payload.data(), payload.size()));
        store_little_endian(header, kHeaderCrcOffset, 0U);
        store_little_endian(header, kHeaderCrcOffset,
                            crc32(header.data(), header.size()));

        temp = temporary_path(cache);
        std::ofstream output(temp, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(header.data()),
                     static_cast<std::streamsize>(header.size()));
        output.write(reinterpret_cast<const char*>(payload.data()),
                     static_cast<std::streamsize>(payload.size()));
        output.flush();
        const bool wrote = static_cast<bool>(output);
        output.close();
        if (!wrote || !flush_file(temp)) {
            std::error_code cleanup_error;
            std::filesystem::remove(temp, cleanup_error);
            return false;
        }

        FrequencyColorCacheData verified;
        if (!load(temp, source, data.algorithm_version, verified)
            || !same_data(verified, data)
            || !atomic_replace(temp, cache)) {
            std::error_code cleanup_error;
            std::filesystem::remove(temp, cleanup_error);
            return false;
        }
        return true;
    } catch (...) {
        if (!temp.empty()) {
            std::error_code cleanup_error;
            std::filesystem::remove(temp, cleanup_error);
        }
        return false;
    }
}

} // namespace agplayer
