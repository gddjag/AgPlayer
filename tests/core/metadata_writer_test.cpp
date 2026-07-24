#undef NDEBUG
#include <agplayer/c_api.h>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>

int main(const int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path fixture = argv[1];
    const std::filesystem::path work_dir = fixture.parent_path();

    const std::filesystem::path src = work_dir / "meta-write-src.wav";
    std::filesystem::copy_file(fixture, src,
                               std::filesystem::copy_options::overwrite_existing);

    const ag_result result = ag_metadata_write(
        src.u8string().c_str(),
        "New Title", "New Artist", "New Album",
        "2024", "Rock", nullptr,
        nullptr, 0U, nullptr);
    assert(result == AG_OK);

    ag_metadata* metadata = nullptr;
    assert(ag_metadata_open(src.u8string().c_str(), &metadata) == AG_OK);
    assert(metadata != nullptr);
    assert(std::strcmp(ag_metadata_title(metadata), "New Title") == 0);
    assert(std::strcmp(ag_metadata_artist(metadata), "New Artist") == 0);
    ag_metadata_destroy(metadata);

    const std::filesystem::path backup = src.string() + ".agbak";
    assert(std::filesystem::exists(backup));

    std::filesystem::remove(src);
    std::filesystem::remove(backup);
    return 0;
}
