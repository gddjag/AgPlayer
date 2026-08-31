// Candidate-only contract for the frozen decoder timeline split.  The
// baseline intentionally does not build this because its public decoder API
// predates DecoderOpenOptions.
#undef NDEBUG

#include "decoder.hpp"

#include <cassert>

int main(const int argc, char** argv)
{
    assert(argc == 2);
    const std::uint64_t opens_before = agplayer::Decoder::threadOpenCount();

    agplayer::Decoder preserve;
    assert(preserve.open(argv[1]) == AG_OK);
    const agplayer::DecodedAudioFormat& preserve_format =
        preserve.output_format();
    assert(preserve_format.sample_rate > 0);
    assert(preserve_format.channels > 0);
    assert(!preserve_format.has_timeline);
    assert(preserve_format.timeline_frames == 0U);
    assert(agplayer::Decoder::threadOpenCount() == opens_before + 1U);

    agplayer::DecoderOpenOptions analysis_options;
    analysis_options.downmix = agplayer::DecoderDownmix::AnalysisMono;
    agplayer::Decoder analysis_mono;
    assert(analysis_mono.open(argv[1], analysis_options) == AG_OK);
    const agplayer::DecodedAudioFormat& analysis_format =
        analysis_mono.output_format();
    assert(analysis_format.sample_rate > 0);
    assert(analysis_format.channels == 1);
    assert(analysis_format.has_timeline);
    assert(analysis_format.timeline_frames > 0U);
    assert(agplayer::Decoder::threadOpenCount() == opens_before + 2U);
    return 0;
}
