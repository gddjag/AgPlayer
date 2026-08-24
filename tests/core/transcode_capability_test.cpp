#undef NDEBUG

#include "transcode_capability.hpp"

#include <array>
#include <cassert>
#include <string_view>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

int main()
{
    constexpr std::array<std::string_view, 8> expected_keys{
        "mp3", "flac", "wav", "aac", "opus", "ogg", "alac", "m4a"};

    const auto capabilities = agplayer::transcode_capabilities();
    assert(capabilities.size() == expected_keys.size());
    for (std::size_t index = 0; index < expected_keys.size(); ++index) {
        const auto& capability = capabilities[index];
        assert(capability.key == expected_keys[index]);
        assert(!capability.label.empty());
        assert(!capability.codec_name.empty());
        assert(!capability.muxer_name.empty());
        assert(agplayer::find_transcode_capability(capabilities,
                                                   capability.key)
               == &capability);
        if (capability.available) {
            assert(avcodec_find_encoder_by_name(capability.codec_name.c_str())
                   != nullptr);
            assert(av_guess_format(capability.muxer_name.c_str(), nullptr,
                                   nullptr)
                   != nullptr);
            assert(!capability.sample_formats.empty());
            assert(!capability.channel_layouts.empty());
        } else {
            assert(!capability.unavailable_reason.empty());
        }
    }

    const auto* aac = agplayer::find_transcode_capability(capabilities, "aac");
    const auto* m4a = agplayer::find_transcode_capability(capabilities, "m4a");
    const auto* alac = agplayer::find_transcode_capability(capabilities, "alac");
    const auto* wav = agplayer::find_transcode_capability(capabilities, "wav");
    const auto* flac = agplayer::find_transcode_capability(capabilities, "flac");
    const auto* ogg = agplayer::find_transcode_capability(capabilities, "ogg");
    assert(aac != nullptr && m4a != nullptr && alac != nullptr
           && wav != nullptr && flac != nullptr && ogg != nullptr);
    assert(aac->codec_name == "aac");
    assert(aac->muxer_name == "adts");
    assert(m4a->codec_name == "aac");
    assert(m4a->muxer_name == "ipod");
    assert(alac->codec_name == "alac");
    assert(alac->muxer_name == "ipod");
    assert(ogg->parameter_kind == "quality");
    assert(ogg->quality_choices == std::vector<int>({0, 2, 4, 6, 8, 10}));
    assert(ogg->default_quality == 6);
    assert(flac->parameter_kind == "compression");
    assert(flac->quality_choices == std::vector<int>({0, 3, 5, 8}));
    assert(flac->default_quality == 5);
    const auto assert_depths = [](const auto& capability) {
        assert(capability->bit_depth_choices.size() >= 3);
        assert(capability->bit_depth_choices.front().key.empty());
        assert(capability->bit_depth_choices.front().is_default);
        assert(capability->bit_depth_choices[1].key == "s16");
        assert(capability->bit_depth_choices[2].key == "s24");
    };
    assert_depths(wav);
    assert_depths(flac);
    assert_depths(alac);
    assert(agplayer::find_transcode_capability(capabilities, "missing")
           == nullptr);
    return 0;
}
