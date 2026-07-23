// Test files deliberately keep assert() active even in Release builds: many
// test cases embed function calls with side effects inside assert(), and
// silencing them under NDEBUG would skip those calls and crash on cleanup.
#undef NDEBUG

#include "waveform_cache.hpp"

#include <algorithm>
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
#include <system_error>
#include <thread>
#include <vector>

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
    const std::filesystem::path equivalent_path =
        case_dir / "nested" / ".." / source_path.filename();
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

    // The read-only-file rejection test is POSIX-only. On Windows,
    // MoveFileExW(MOVEFILE_REPLACE_EXISTING) - used by atomic_replace() - can
    // substitute a read-only destination when the process runs with
    // Administrator privileges, so the assertion would not hold in CI on that
    // platform. The impossible-path case below still exercises the
    // "save cannot write the destination" branch on Windows.
#ifndef _WIN32
    std::filesystem::permissions(
        cache_path,
        std::filesystem::perms::owner_read
            | std::filesystem::perms::group_read
            | std::filesystem::perms::others_read,
        std::filesystem::perm_options::replace);
    assert(!agplayer::WaveformCache::save(cache_path, source_path, {1.0F}));
    assert(!path_exists(temporary_path_for(cache_path)));
    std::filesystem::permissions(
        cache_path, std::filesystem::perms::owner_all,
        std::filesystem::perm_options::replace);
#endif

    const std::filesystem::path impossible_cache =
        source_path / "child" / "cache.agwf";
    assert(!agplayer::WaveformCache::save(impossible_cache, source_path, peaks));
    assert(!path_exists(temporary_path_for(impossible_cache)));

    robust_remove_all(case_dir);
}
