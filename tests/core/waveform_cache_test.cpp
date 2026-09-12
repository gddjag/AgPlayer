// Test files deliberately keep assert() active even in Release builds: many
// test cases embed function calls with side effects inside assert(), and
// silencing them under NDEBUG would skip those calls and crash on cleanup.
#undef NDEBUG

#include "waveform_cache.hpp"

#include <agplayer/c_api.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

std::filesystem::path temporary_path_for(const std::filesystem::path& path)
{
    return std::filesystem::path(path.string() + ".tmp");
}

// On Windows, file deletion is asynchronous: a file removed via remove_all
// may remain in a "pending deletion" state while an antivirus or the search
// indexer holds an open handle. Subsequent filesystem operations on the same
// path (create_directories, copy_file) can then throw filesystem_error,
// crashing the test with an uncaught exception. These helpers use the
// error_code overloads plus a short retry loop to ride through that window.
void robust_remove_all(const std::filesystem::path& dir)
{
    for (int attempt = 0; attempt < 5; ++attempt) {
        std::error_code ec;
        const std::uintmax_t removed = std::filesystem::remove_all(dir, ec);
        if (!ec) {
            (void)removed;
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    // Last attempt: if it still fails the test will surface a clear assert.
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

bool robust_copy_file(const std::filesystem::path& from,
                      const std::filesystem::path& to)
{
    for (int attempt = 0; attempt < 5; ++attempt) {
        std::error_code ec;
        if (std::filesystem::copy_file(from, to, ec)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

void robust_set_mtime(const std::filesystem::path& path,
                      std::filesystem::file_time_type mtime)
{
    for (int attempt = 0; attempt < 5; ++attempt) {
        std::error_code ec;
        std::filesystem::last_write_time(path, mtime, ec);
        if (!ec) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void robust_resize(const std::filesystem::path& path, std::uintmax_t size)
{
    for (int attempt = 0; attempt < 5; ++attempt) {
        std::error_code ec;
        std::filesystem::resize_file(path, size, ec);
        if (!ec) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

bool path_exists(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

void overwrite_bytes(const std::filesystem::path& path,
                     const std::streamoff offset,
                     const std::vector<unsigned char>& bytes)
{
    std::fstream stream(path, std::ios::binary | std::ios::in | std::ios::out);
    assert(stream);
    stream.seekp(offset);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    stream.flush();
    assert(stream);
}

std::vector<unsigned char> read_bytes(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    assert(stream);
    return {std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
}

void hash_schema4_byte(std::uint64_t& hash, const unsigned char byte)
{
    constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;
    hash ^= byte;
    hash *= fnv_prime;
}

template <typename Value>
void hash_schema4_integer(std::uint64_t& hash, Value value)
{
    for (std::size_t index = 0U; index < sizeof(Value); ++index) {
        hash_schema4_byte(hash, static_cast<unsigned char>(value & 0xFFU));
        value >>= 8U;
    }
}

std::string recorded_schema_key_fixture(const std::filesystem::path& path,
                                        const std::uint64_t schema)
{
    constexpr std::uint64_t fnv_offset = 14'695'981'039'346'656'037ULL;
    std::error_code error;
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(path, error);
    assert(!error);
    const std::uint64_t size = std::filesystem::file_size(path, error);
    assert(!error);
    const std::int64_t mtime_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::filesystem::last_write_time(path, error).time_since_epoch())
            .count();
    assert(!error);

    std::uint64_t hash = fnv_offset;
    for (const unsigned char byte : canonical.u8string()) {
        hash_schema4_byte(hash, byte);
    }
    hash_schema4_byte(hash, 0U);
    hash_schema4_integer(hash, size);
    hash_schema4_integer(hash, static_cast<std::uint64_t>(mtime_ns));
    hash_schema4_integer(hash, schema);

    std::ostringstream key;
    key << std::hex << std::setfill('0') << std::setw(16) << hash;
    return key.str();
}

void test_maximum_six_layer_cache_io(const std::filesystem::path& cache_path,
                                   const std::filesystem::path& source_path)
{
    constexpr std::size_t count = 524288U;
    agplayer::WaveformCacheData data;
    data.mix.assign(count, 0.25F);
    data.bass.assign(count, 0.125F);
    data.mid.assign(count, 0.0625F);
    data.high.assign(count, 0.5F);
    data.peak.assign(count, 0.75F);
    data.rms.assign(count, 0.5F);
    data.duration_ms = 2000;
    data.total_samples = 88200;
    data.sample_rate = 44100;
    std::array<double, 5U> write_ms{}, read_ms{};
    for (std::size_t run = 0; run < write_ms.size(); ++run) {
        auto start = std::chrono::steady_clock::now();
        assert(agplayer::WaveformCache::save_v4(cache_path, source_path, data));
        write_ms[run] = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        agplayer::WaveformCacheData loaded;
        start = std::chrono::steady_clock::now();
        assert(agplayer::WaveformCache::load_v4(cache_path, source_path, loaded));
        read_ms[run] = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        assert(loaded.mix == data.mix && loaded.bass == data.bass
               && loaded.mid == data.mid && loaded.high == data.high
               && loaded.peak == data.peak && loaded.rms == data.rms);
    }
    std::sort(write_ms.begin(), write_ms.end());
    std::sort(read_ms.begin(), read_ms.end());
    std::cout << "waveform_cache_6x524288 write_median_ms=" << write_ms[2]
              << " read_median_ms=" << read_ms[2] << '\n';

    // Reject a corrupt final layer after reading earlier complete layers.
    // IEEE-754 +infinity and NaN are written explicitly in the file format's
    // little-endian order, independently of the production serializer.
    const auto rms_start = static_cast<std::streamoff>(104U + 5U * count * sizeof(float));
    for (const auto& bytes : {std::vector<unsigned char>{0, 0, 128, 127},
                              std::vector<unsigned char>{1, 0, 192, 127}}) {
        overwrite_bytes(cache_path, rms_start + 65536, bytes);
        agplayer::WaveformCacheData loaded;
        assert(!agplayer::WaveformCache::load_v4(cache_path, source_path, loaded));
        assert(loaded.mix.empty() && loaded.rms.empty());
    }
}

} // namespace

int main(const int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path fixture_path = argv[1];
    const std::filesystem::path case_dir =
        fixture_path.parent_path() / "waveform-cache-case";
    robust_remove_all(case_dir);
    {
        std::error_code ec;
        std::filesystem::create_directories(case_dir, ec);
        assert(!ec);
    }
    const std::filesystem::path source_path = case_dir / "source.wav";
    assert(robust_copy_file(fixture_path, source_path));

    const std::string first_key = agplayer::WaveformCache::key_for(source_path);
    assert(!first_key.empty());
    assert(first_key != recorded_schema_key_fixture(source_path, 4U));
    // Increasing foreground analysis density changes every peak bucket. Keep
    // old schema-5 files out of the provider so rolling views cannot stretch
    // their sparse 2,000-point payload across a high-detail viewport.
    assert(first_key != recorded_schema_key_fixture(source_path, 5U));
    const std::string legacy_v2_key =
        agplayer::WaveformCache::legacy_v2_key_for(source_path);
    assert(!legacy_v2_key.empty());
    assert(legacy_v2_key != first_key);
    const std::filesystem::path equivalent_path =
        case_dir / "nested" / ".." / source_path.filename();
    // POSIX resolves each path component; nested/.. is only a valid alias
    // when nested exists (Windows normalises it before opening the file).
    assert(std::filesystem::create_directory(case_dir / "nested"));
    assert(agplayer::WaveformCache::key_for(equivalent_path) == first_key);

    const auto original_mtime = std::filesystem::last_write_time(source_path);
    robust_set_mtime(source_path, original_mtime + std::chrono::seconds(1));
    assert(agplayer::WaveformCache::key_for(source_path) != first_key);
    robust_set_mtime(source_path, original_mtime);

    {
        std::ofstream append(source_path, std::ios::binary | std::ios::app);
        append.put('\0');
    }
    robust_set_mtime(source_path, original_mtime);
    assert(agplayer::WaveformCache::key_for(source_path) != first_key);
    robust_resize(source_path, std::filesystem::file_size(fixture_path));
    robust_set_mtime(source_path, original_mtime);
    assert(agplayer::WaveformCache::key_for(source_path) == first_key);

    const std::vector<float> peaks{0.25F, 0.5F, 1.0F};
    const std::filesystem::path cache_path = case_dir / "valid.agwf";
    assert(agplayer::WaveformCache::save(cache_path, source_path, peaks));
    assert(!path_exists(temporary_path_for(cache_path)));

    std::vector<float> loaded{9.0F};
    assert(agplayer::WaveformCache::load(cache_path, source_path, loaded));
    assert(loaded == peaks);

    const std::vector<unsigned char> raw = read_bytes(cache_path);
    assert(raw.size() == 32U + peaks.size() * sizeof(float));
    assert(std::string(raw.begin(), raw.begin() + 4) == "AGWF");
    assert(raw[4] == 1U && raw[5] == 0U && raw[6] == 0U && raw[7] == 0U);
    assert(raw[32] == 0U && raw[33] == 0U && raw[34] == 128U
           && raw[35] == 62U);

    const auto expect_rejected = [&](const std::filesystem::path& corrupt) {
        loaded = {9.0F};
        assert(!agplayer::WaveformCache::load(corrupt, source_path, loaded));
        assert(loaded.empty());
    };

    const std::filesystem::path bad_magic = case_dir / "bad-magic.agwf";
    assert(robust_copy_file(cache_path, bad_magic));
    overwrite_bytes(bad_magic, 0, {'B'});
    expect_rejected(bad_magic);
    assert(agplayer::WaveformCache::save(bad_magic, source_path, peaks));
    assert(agplayer::WaveformCache::load(bad_magic, source_path, loaded));
    assert(loaded == peaks);

#ifdef _WIN32
    // Windows scanners and indexers can briefly hold the old cache without
    // FILE_SHARE_DELETE. A save must survive that bounded sharing violation.
    const std::filesystem::path locked_cache =
        case_dir / "temporarily-locked.agwf";
    assert(robust_copy_file(cache_path, locked_cache));
    const HANDLE locked_handle =
        CreateFileW(locked_cache.c_str(), GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(locked_handle != INVALID_HANDLE_VALUE);
    std::thread unlocker([locked_handle]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        assert(CloseHandle(locked_handle) != FALSE);
    });
    assert(agplayer::WaveformCache::save(locked_cache, source_path, peaks));
    unlocker.join();
    assert(agplayer::WaveformCache::load(locked_cache, source_path, loaded));
    assert(loaded == peaks);
#endif

    const std::filesystem::path bad_version = case_dir / "bad-version.agwf";
    assert(robust_copy_file(cache_path, bad_version));
    overwrite_bytes(bad_version, 4, {2U, 0U, 0U, 0U});
    expect_rejected(bad_version);

    const std::filesystem::path truncated = case_dir / "truncated.agwf";
    assert(robust_copy_file(cache_path, truncated));
    robust_resize(truncated, 34U);
    expect_rejected(truncated);

    const std::filesystem::path non_finite = case_dir / "non-finite.agwf";
    assert(robust_copy_file(cache_path, non_finite));
    overwrite_bytes(non_finite, 32, {0U, 0U, 128U, 127U});
    expect_rejected(non_finite);

    robust_set_mtime(source_path, original_mtime + std::chrono::seconds(2));
    expect_rejected(cache_path);
    robust_set_mtime(source_path, original_mtime);

    const std::filesystem::path bad_source_size =
        case_dir / "bad-source-size.agwf";
    assert(robust_copy_file(cache_path, bad_source_size));
    overwrite_bytes(bad_source_size, 8, {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U});
    expect_rejected(bad_source_size);

    const std::filesystem::path missing_source = case_dir / "missing.wav";
    expect_rejected(cache_path.parent_path() / "missing-cache.agwf");
    loaded = {9.0F};
    assert(!agplayer::WaveformCache::load(cache_path, missing_source, loaded));
    assert(loaded.empty());

    assert(!agplayer::WaveformCache::save(
        case_dir / "nan.agwf", source_path,
        {0.5F, std::numeric_limits<float>::quiet_NaN()}));
    assert(!path_exists(case_dir / "nan.agwf.tmp"));

    // v2 round-trip: mix/bass/mid/high peaks, BPM, and CUE metadata.
    {
        agplayer::WaveformCacheData data;
        data.mix = {0.1F, 0.2F, 0.3F};
        data.bass = {0.05F, 0.15F};
        data.mid = {0.08F, 0.18F, 0.28F};
        data.high = {0.12F};
        data.bpm = 128.5;
        data.duration_ms = 1000U;
        data.total_samples = 48'000U;
        data.sample_rate = 48'000U;
        data.cues.push_back({1200U, "Intro"});
        data.cues.push_back({5000U, "Drop"});

        const std::filesystem::path v2_cache = case_dir / "v2.agwf";
        assert(agplayer::WaveformCache::save_v2(v2_cache, source_path, data));

        agplayer::WaveformCacheData loaded_v2;
        assert(agplayer::WaveformCache::load_v2(v2_cache, source_path, loaded_v2));
        assert(loaded_v2.mix == data.mix);
        assert(loaded_v2.bass == data.bass);
        assert(loaded_v2.mid == data.mid);
        assert(loaded_v2.high == data.high);
        assert(std::fabs(loaded_v2.bpm - data.bpm) < 1e-9);
        assert(loaded_v2.duration_ms == data.duration_ms);
        assert(loaded_v2.total_samples == data.total_samples);
        assert(loaded_v2.sample_rate == data.sample_rate);
        assert(loaded_v2.cues.size() == data.cues.size());
        for (std::size_t i = 0U; i < data.cues.size(); ++i) {
            assert(loaded_v2.cues[i].position_ms == data.cues[i].position_ms);
            assert(loaded_v2.cues[i].label == data.cues[i].label);
        }
    }

    // v2 with missing layers: empty bass/mid/high are preserved.
    {
        agplayer::WaveformCacheData data;
        data.mix = {0.5F, 0.6F};

        const std::filesystem::path v2_sparse = case_dir / "v2-sparse.agwf";
        assert(agplayer::WaveformCache::save_v2(v2_sparse, source_path, data));

        agplayer::WaveformCacheData loaded_v2;
        assert(agplayer::WaveformCache::load_v2(v2_sparse, source_path, loaded_v2));
        assert(loaded_v2.mix == data.mix);
        assert(loaded_v2.bass.empty());
        assert(loaded_v2.mid.empty());
        assert(loaded_v2.high.empty());
        assert(loaded_v2.bpm == 0.0);
        assert(loaded_v2.cues.empty());
    }

    // v4 stores amplitude plus the three frequency-energy layers in one .agwf
    // file. Colors and playback presentation are deliberately absent.
    {
        agplayer::WaveformCacheData data;
        data.mix = {0.1F, 0.4F, 0.8F, 0.2F};
        data.bass = {0.2F, 0.3F, 0.4F, 0.5F};
        data.mid = {0.3F, 0.4F, 0.5F, 0.6F};
        data.high = {0.4F, 0.5F, 0.6F, 0.7F};
        data.peak = {0.8F, 0.7F, 0.9F, 0.3F};
        data.rms = {0.3F, 0.4F, 0.5F, 0.2F};
        data.bpm = 120.0;
        data.duration_ms = 2'000U;
        data.total_samples = 96'000U;
        data.sample_rate = 48'000U;

        const std::filesystem::path v4_cache = case_dir / "v4.agwf";
        assert(agplayer::WaveformCache::save_v4(v4_cache, source_path, data));
        const std::uintmax_t complete_bytes =
            std::filesystem::file_size(v4_cache);

        agplayer::WaveformCacheData loaded_v4;
        assert(agplayer::WaveformCache::load_v4(
            v4_cache, source_path, loaded_v4));
        assert(loaded_v4.mix == data.mix);
        assert(loaded_v4.bass == data.bass);
        assert(loaded_v4.mid == data.mid);
        assert(loaded_v4.high == data.high);
        assert(loaded_v4.peak == data.peak);
        assert(loaded_v4.rms == data.rms);
        assert(loaded_v4.duration_ms == data.duration_ms);
        assert(loaded_v4.total_samples == data.total_samples);
        assert(loaded_v4.sample_rate == data.sample_rate);

        // Known little-endian amplitudes in all six layers must remain
        // byte-compatible, including readers of caches created before bulk I/O.
        data.mix = {0.25F, 0.5F, 1.0F, 0.0F};
        const auto wire_cache = case_dir / "v4-wire.agwf";
        assert(agplayer::WaveformCache::save_v4(wire_cache, source_path, data));
        const auto wire = read_bytes(wire_cache);
        const std::vector<unsigned char> expected_mix{
            0, 0, 128, 62, 0, 0, 0, 63, 0, 0, 128, 63, 0, 0, 0, 0};
        assert(std::equal(expected_mix.begin(), expected_mix.end(), wire.begin() + 104));
        const std::vector<unsigned char> replacement_mix{
            0, 0, 0, 63, 0, 0, 128, 62, 0, 0, 128, 63, 0, 0, 0, 0};
        overwrite_bytes(wire_cache, 104, replacement_mix);
        assert(agplayer::WaveformCache::load_v4(wire_cache, source_path, loaded_v4));
        assert(loaded_v4.mix == std::vector<float>({0.5F, 0.25F, 1.0F, 0.0F}));

        data.high.pop_back();
        assert(!agplayer::WaveformCache::save_v4(
            case_dir / "v4-mismatch.agwf", source_path, data));

        assert(robust_copy_file(v4_cache, case_dir / "v4-truncated.agwf"));
        robust_resize(case_dir / "v4-truncated.agwf", complete_bytes - 1U);
        loaded_v4 = {};
        assert(!agplayer::WaveformCache::load_v4(
            case_dir / "v4-truncated.agwf", source_path, loaded_v4));
        assert(loaded_v4.mix.empty());
    }

    // Corrupted v2 cache is rejected.
    {
        agplayer::WaveformCacheData data;
        data.mix = {0.1F};
        data.bass = {0.2F};

        const std::filesystem::path v2_bad = case_dir / "v2-corrupt.agwf";
        assert(agplayer::WaveformCache::save_v2(v2_bad, source_path, data));
        robust_resize(v2_bad, 88U);

        agplayer::WaveformCacheData loaded_v2;
        assert(!agplayer::WaveformCache::load_v2(v2_bad, source_path, loaded_v2));
        assert(loaded_v2.mix.empty() && loaded_v2.bass.empty());
    }

    // C API layer helpers: analyze produces mix + bass/mid/high layers.
    {
        ag_waveform* waveform = nullptr;
        const ag_result result = ag_waveform_analyze(
            source_path.string().c_str(), 8U, nullptr, nullptr, nullptr,
            &waveform);
        assert(result == AG_OK);
        assert(waveform != nullptr);
        assert(ag_waveform_count(waveform) == 8U);
        assert(ag_waveform_layer_count(waveform, AG_WAVEFORM_LAYER_MIX) == 8U);
        assert(ag_waveform_layer_count(waveform, AG_WAVEFORM_LAYER_BASS) == 8U);
        assert(ag_waveform_layer_count(waveform, AG_WAVEFORM_LAYER_MID) == 8U);
        assert(ag_waveform_layer_count(waveform, AG_WAVEFORM_LAYER_HIGH) == 8U);
        assert(std::isfinite(ag_waveform_layer_peak(waveform, AG_WAVEFORM_LAYER_HIGH, 0U)));
        assert(ag_waveform_bpm(waveform) == 0.0);
        assert(ag_waveform_duration_ms(waveform) == 2000U);
        assert(ag_waveform_total_samples(waveform) == 88'200U);
        assert(ag_waveform_sample_rate(waveform) == 44'100);
        ag_waveform_destroy(waveform);
    }

    // C API combined analysis: waveform + BPM.
    {
        ag_waveform* waveform = nullptr;
        double bpm = -1.0;
        const ag_result result = ag_track_analysis(
            source_path.string().c_str(), 16U, nullptr, nullptr, nullptr,
            &waveform, &bpm);
        assert(result == AG_OK);
        assert(waveform != nullptr);
        assert(ag_waveform_count(waveform) == 16U);
        assert(ag_waveform_layer_count(waveform, AG_WAVEFORM_LAYER_MIX) == 16U);
        assert(ag_waveform_layer_count(waveform, AG_WAVEFORM_LAYER_BASS) == 16U);
        assert(ag_waveform_layer_count(waveform, AG_WAVEFORM_LAYER_MID) == 16U);
        assert(ag_waveform_layer_count(waveform, AG_WAVEFORM_LAYER_HIGH) == 16U);
        assert(ag_waveform_bpm(waveform) == bpm);
        assert(bpm > 0.0);
        ag_waveform_destroy(waveform);

        // out_bpm may be NULL.
        waveform = nullptr;
        const ag_result no_bpm_result = ag_track_analysis(
            source_path.string().c_str(), 8U, nullptr, nullptr, nullptr,
            &waveform, nullptr);
        assert(no_bpm_result == AG_OK);
        assert(waveform != nullptr);
        assert(ag_waveform_count(waveform) == 8U);
        assert(ag_waveform_bpm(waveform) > 0.0);
        ag_waveform_destroy(waveform);

        // Cancellation is honored.
        ag_cancel_token* token = ag_cancel_token_create();
        ag_cancel_token_cancel(token);
        waveform = nullptr;
        bpm = -1.0;
        const ag_result cancelled_result = ag_track_analysis(
            source_path.string().c_str(), 16U, token, nullptr, nullptr,
            &waveform, &bpm);
        assert(cancelled_result == AG_CANCELLED);
        assert(waveform == nullptr);
        assert(bpm == 0.0);
        ag_cancel_token_destroy(token);
    }

    // Replacing a directory with a file must fail on every platform, even
    // with elevated privileges. A read-only file is not sufficient: POSIX
    // rename checks the containing directory's permissions, not the file's.
    const auto blocked_cache = case_dir / "blocked-cache.agwf";
    std::filesystem::create_directory(blocked_cache);
    const auto preserved_entry = blocked_cache / "keep.txt";
    { std::ofstream marker(preserved_entry); marker << "keep"; }
    assert(!agplayer::WaveformCache::save(blocked_cache, source_path, {1.0F}));
    assert(!path_exists(temporary_path_for(blocked_cache)));
    assert(path_exists(preserved_entry));

    const std::filesystem::path impossible_cache =
        source_path / "child" / "cache.agwf";
    assert(!agplayer::WaveformCache::save(impossible_cache, source_path, peaks));
    assert(!path_exists(temporary_path_for(impossible_cache)));

    test_maximum_six_layer_cache_io(case_dir / "v4-maximum.agwf", source_path);
    robust_remove_all(case_dir);
}
