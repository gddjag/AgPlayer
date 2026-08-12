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
    assert(aac != nullptr && m4a != nullptr && alac != nullptr);
    assert(aac->codec_name == "aac");
    assert(aac->muxer_name == "adts");
    assert(m4a->codec_name == "aac");
    assert(m4a->muxer_name == "ipod");
    assert(alac->codec_name == "alac");
    assert(alac->muxer_name == "ipod");
    assert(agplayer::find_transcode_capability(capabilities, "missing")
           == nullptr);
    return 0;
}
