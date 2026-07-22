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
#include <vector>

namespace {

std::filesystem::path temporary_path_for(const std::filesystem::path& path)
{
    return std::filesystem::path(path.string() + ".tmp");
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
    std::filesystem::remove_all(case_dir);
    std::filesystem::create_directories(case_dir);
    const std::filesystem::path source_path = case_dir / "source.wav";
    std::filesystem::copy_file(fixture_path, source_path);

    const std::string first_key = agplayer::WaveformCache::key_for(source_path);
    assert(!first_key.empty());
    const std::filesystem::path equivalent_path =
        case_dir / "nested" / ".." / source_path.filename();
    assert(agplayer::WaveformCache::key_for(equivalent_path) == first_key);

    const auto original_mtime = std::filesystem::last_write_time(source_path);
    std::filesystem::last_write_time(
        source_path, original_mtime + std::chrono::seconds(1));
    assert(agplayer::WaveformCache::key_for(source_path) != first_key);
    std::filesystem::last_write_time(source_path, original_mtime);

    {
        std::ofstream append(source_path, std::ios::binary | std::ios::app);
        append.put('\0');
    }
    std::filesystem::last_write_time(source_path, original_mtime);
    assert(agplayer::WaveformCache::key_for(source_path) != first_key);
    std::filesystem::resize_file(source_path,
                                 std::filesystem::file_size(fixture_path));
    std::filesystem::last_write_time(source_path, original_mtime);
    assert(agplayer::WaveformCache::key_for(source_path) == first_key);

    const std::vector<float> peaks{0.25F, 0.5F, 1.0F};
    const std::filesystem::path cache_path = case_dir / "valid.agwf";
    assert(agplayer::WaveformCache::save(cache_path, source_path, peaks));
    assert(!std::filesystem::exists(temporary_path_for(cache_path)));

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
    std::filesystem::copy_file(cache_path, bad_magic);
    overwrite_bytes(bad_magic, 0, {'B'});
    expect_rejected(bad_magic);
    assert(agplayer::WaveformCache::save(bad_magic, source_path, peaks));
    assert(agplayer::WaveformCache::load(bad_magic, source_path, loaded));
    assert(loaded == peaks);

    const std::filesystem::path bad_version = case_dir / "bad-version.agwf";
    std::filesystem::copy_file(cache_path, bad_version);
    overwrite_bytes(bad_version, 4, {2U, 0U, 0U, 0U});
    expect_rejected(bad_version);

    const std::filesystem::path truncated = case_dir / "truncated.agwf";
    std::filesystem::copy_file(cache_path, truncated);
    std::filesystem::resize_file(truncated, 34U);
    expect_rejected(truncated);

    const std::filesystem::path non_finite = case_dir / "non-finite.agwf";
    std::filesystem::copy_file(cache_path, non_finite);
    overwrite_bytes(non_finite, 32, {0U, 0U, 128U, 127U});
    expect_rejected(non_finite);

    std::filesystem::last_write_time(
        source_path, original_mtime + std::chrono::seconds(2));
    expect_rejected(cache_path);
    std::filesystem::last_write_time(source_path, original_mtime);

    const std::filesystem::path bad_source_size =
        case_dir / "bad-source-size.agwf";
    std::filesystem::copy_file(cache_path, bad_source_size);
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
    assert(!std::filesystem::exists(case_dir / "nan.agwf.tmp"));

    std::filesystem::permissions(
        cache_path,
        std::filesystem::perms::owner_read
            | std::filesystem::perms::group_read
            | std::filesystem::perms::others_read,
        std::filesystem::perm_options::replace);
    assert(!agplayer::WaveformCache::save(cache_path, source_path, {1.0F}));
    assert(!std::filesystem::exists(temporary_path_for(cache_path)));
    std::filesystem::permissions(
        cache_path, std::filesystem::perms::owner_all,
        std::filesystem::perm_options::replace);

    const std::filesystem::path impossible_cache =
        source_path / "child" / "cache.agwf";
    assert(!agplayer::WaveformCache::save(impossible_cache, source_path, peaks));
    assert(!std::filesystem::exists(temporary_path_for(impossible_cache)));

    std::filesystem::remove_all(case_dir);
}
