#undef NDEBUG

#include "light_editor.hpp"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>

int main(const int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path input_path = argv[1];

    const std::filesystem::path output_path =
        input_path.parent_path() / "light-editor-out.wav";
    std::filesystem::remove(output_path);

    agplayer::LightEditConfig config;
    config.trim_start_ms = 0;
    config.trim_end_ms = 0;
    config.fade_in_ms = 0;
    config.fade_out_ms = 0;
    config.gain = 1.0;
    config.output_path = output_path.string();
    std::string error;
    const ag_result result =
        agplayer::light_edit(input_path.string(), config, nullptr, nullptr, error);
    if (result != AG_OK) {
        std::cerr << "light_edit failed: " << static_cast<int>(result)
                  << " " << error << "\n";
    }
    assert(result == AG_OK);
    assert(std::filesystem::exists(output_path));

    std::filesystem::remove(output_path);
    return 0;
}
