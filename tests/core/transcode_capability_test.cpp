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
        "mp3", "flac", "wav", "aac", "opus", "ogg", "alac", "aiff"};
    constexpr std::array<std::string_view, 8> expected_codecs{
        "libmp3lame", "flac", "pcm_s16le", "aac", "libopus", "libvorbis",
        "alac", "pcm_s16be"};
    constexpr std::array<std::string_view, 8> expected_muxers{
        "mp3", "flac", "wav", "adts", "ogg", "ogg", "ipod", "aiff"};
    constexpr std::array<std::string_view, 8> expected_extensions{
        "mp3", "flac", "wav", "aac", "opus", "ogg", "m4a", "aiff"};

    const auto capabilities = agplayer::transcode_capabilities();
    assert(capabilities.size() == expected_keys.size());
    for (std::size_t index = 0; index < expected_keys.size(); ++index) {
        const auto& capability = capabilities[index];
        assert(capability.key == expected_keys[index]);
        assert(capability.codec_name == expected_codecs[index]);
        assert(capability.muxer_name == expected_muxers[index]);
        assert(capability.output_extension == expected_extensions[index]);
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
    const auto* aiff = agplayer::find_transcode_capability(capabilities, "aiff");
    const auto* alac = agplayer::find_transcode_capability(capabilities, "alac");
    const auto* wav = agplayer::find_transcode_capability(capabilities, "wav");
    const auto* flac = agplayer::find_transcode_capability(capabilities, "flac");
    const auto* ogg = agplayer::find_transcode_capability(capabilities, "ogg");
    const auto* mp3 = agplayer::find_transcode_capability(capabilities, "mp3");
    const auto* opus = agplayer::find_transcode_capability(capabilities, "opus");
    assert(aac != nullptr && aiff != nullptr && alac != nullptr
           && wav != nullptr && flac != nullptr && ogg != nullptr
           && mp3 != nullptr && opus != nullptr);
    assert(aac->codec_name == "aac");
    assert(aac->muxer_name == "adts");
    assert(aiff->codec_name == "pcm_s16be");
    assert(aiff->muxer_name == "aiff");
    assert(alac->codec_name == "alac");
    assert(alac->muxer_name == "ipod");
    assert(alac->output_extension == "m4a");
    const std::vector<int> lossySampleRates{0, 44100, 48000};
    const std::vector<int> losslessSampleRates{
        0, 44100, 48000, 88200, 96000, 176400, 192000};
    assert(mp3->sample_rate_choices == lossySampleRates);
    assert(mp3->bit_rate_choices
           == std::vector<int>({128000, 192000, 256000, 320000}));
    assert(mp3->default_bit_rate == 320000);
    assert(aac->sample_rate_choices == lossySampleRates);
    assert(aac->bit_rate_choices
           == std::vector<int>({96000, 128000, 192000, 256000, 320000}));
    assert(aac->default_bit_rate == 256000);
    assert(opus->sample_rate_choices == std::vector<int>({48000}));
    assert(opus->bit_rate_choices
           == std::vector<int>({64000, 96000, 128000, 160000,
                                192000, 256000, 320000}));
    assert(opus->default_bit_rate == 320000);
    assert(ogg->parameter_kind == "quality");
    assert(ogg->quality_choices == std::vector<int>({0, 2, 4, 6, 8, 10}));
    assert(ogg->default_quality == 8);
    assert(flac->parameter_kind == "compression");
    assert(flac->quality_choices == std::vector<int>({0, 3, 5, 8}));
    assert(flac->default_quality == 5);
    assert(flac->sample_rate_choices == losslessSampleRates);
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
    assert_depths(aiff);
    assert(agplayer::find_transcode_capability(capabilities, "missing")
           == nullptr);
    return 0;
}
