#undef NDEBUG

#include "transcode_capability.hpp"

#include <array>
#include <cassert>
#include <string>
#include <string_view>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace {

void assert_choices(
    const std::vector<agplayer::TranscodeOptionChoice>& actual,
    const std::vector<agplayer::TranscodeOptionChoice>& expected)
{
    assert(actual.size() == expected.size());
    for (std::size_t index = 0; index < actual.size(); ++index) {
        assert(actual[index].key == expected[index].key);
        assert(actual[index].label == expected[index].label);
        assert(actual[index].is_default == expected[index].is_default);
    }
}

} // namespace

int main()
{
    constexpr std::array<std::string_view, 8> expected_keys{
        "mp3", "flac", "wav", "aac", "opus", "ogg", "alac", "aiff"};
    constexpr std::array<std::string_view, 8> expected_codecs{
        "libmp3lame", "flac", "pcm_s16le", "aac", "libopus", "libvorbis",
        "alac", "pcm_s16be"};
    constexpr std::array<std::string_view, 8> expected_muxers{
        "mp3", "flac", "wav", "adts", "opus", "ogg", "ipod", "aiff"};
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
        assert(!capability.output_extension.empty());
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
    assert(aac != nullptr && aiff != nullptr && alac != nullptr);
    assert(agplayer::find_transcode_capability(capabilities, "m4a") == nullptr);
    assert(aac->codec_name == "aac");
    assert(aac->muxer_name == "adts");
    assert(aac->output_extension == "aac");
    assert(aiff->codec_name == "pcm_s16be");
    assert(aiff->muxer_name == "aiff");
    assert(aiff->output_extension == "aiff");
    assert(alac->codec_name == "alac");
    assert(alac->muxer_name == "ipod");
    assert(alac->output_extension == "m4a");

    const auto* mp3 = agplayer::find_transcode_capability(capabilities, "mp3");
    const auto* flac = agplayer::find_transcode_capability(capabilities, "flac");
    const auto* wav = agplayer::find_transcode_capability(capabilities, "wav");
    const auto* opus = agplayer::find_transcode_capability(capabilities, "opus");
    const auto* ogg = agplayer::find_transcode_capability(capabilities, "ogg");
    assert(mp3 != nullptr && flac != nullptr && wav != nullptr
           && opus != nullptr && ogg != nullptr);

    const std::vector<int> lossy_sample_rates{0, 44100, 48000};
    const std::vector<int> lossless_sample_rates{
        0, 44100, 48000, 88200, 96000, 176400, 192000};
    assert(mp3->parameter_kind == "bitrate");
    assert(mp3->sample_rate_choices == lossy_sample_rates);
    assert(mp3->bit_rate_choices
           == std::vector<int>({128000, 192000, 256000, 320000}));
    assert(mp3->default_bit_rate == 320000);

    assert(aac->parameter_kind == "bitrate");
    assert(aac->sample_rate_choices == lossy_sample_rates);
    assert(aac->bit_rate_choices
           == std::vector<int>({96000, 128000, 192000, 256000, 320000}));
    assert(aac->default_bit_rate == 256000);

    assert(opus->parameter_kind == "bitrate");
    assert(opus->sample_rate_choices == std::vector<int>({48000}));
    assert(opus->bit_rate_choices
           == std::vector<int>({64000, 96000, 128000, 160000, 192000, 256000}));
    assert(opus->default_bit_rate == 192000);
    assert(!opus->supports_cover);

    assert(ogg->parameter_kind == "quality");
    assert(ogg->sample_rate_choices == lossy_sample_rates);
    assert(ogg->quality_choices
           == std::vector<int>({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10}));
    assert(ogg->default_quality == 6);
    assert(!ogg->supports_cover);

    assert(flac->parameter_kind == "compression");
    assert(flac->sample_rate_choices == lossless_sample_rates);
    assert(flac->quality_choices == std::vector<int>({0, 3, 5, 8}));
    assert(flac->default_quality == 5);
    assert_choices(flac->bit_depth_choices,
                   {{"", "Source (Auto)", true},
                    {"s16", "16-bit", false},
                    {"s24", "24-bit", false}});

    assert(wav->parameter_kind == "none");
    assert(wav->sample_rate_choices == lossless_sample_rates);
    assert_choices(wav->bit_depth_choices,
                   {{"", "Source (Auto)", true},
                    {"s16", "16-bit PCM", false},
                    {"s24", "24-bit PCM", false},
                    {"flt", "32-bit Float", false}});
    assert(alac->parameter_kind == "none");
    assert(alac->sample_rate_choices == lossless_sample_rates);
    assert_choices(alac->bit_depth_choices,
                   {{"", "Source (Auto)", true},
                    {"s16", "16-bit", false},
                    {"s24", "24-bit", false}});
    assert(aiff->parameter_kind == "none");
    assert(aiff->sample_rate_choices == lossless_sample_rates);
    assert_choices(aiff->bit_depth_choices,
                   {{"", "Source (Auto)", true},
                    {"s16", "16-bit PCM", false},
                    {"s24", "24-bit PCM", false},
                    {"s32", "32-bit PCM", false}});
    assert(agplayer::find_transcode_capability(capabilities, "missing")
           == nullptr);
    return 0;
}
