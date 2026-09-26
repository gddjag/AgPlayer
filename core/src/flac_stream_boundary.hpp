#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
}

#include <cstdint>

namespace agplayer {

// RFC 9639 section 8.2: STREAMINFO's total is an exact source-sample count.
// Only a successfully decoded frame ending at that boundary allows ignoring
// trailing non-audio bytes. Unknown lengths and earlier errors remain errors.
inline bool is_final_flac_frame(const AVCodecContext& codec,
                                const AVStream& stream,
                                const AVFrame& frame) noexcept
{
    if (codec.codec_id != AV_CODEC_ID_FLAC || codec.extradata_size != 34
        || codec.extradata == nullptr || frame.nb_samples <= 0
        || codec.sample_rate <= 0) return false;
    const auto* info = codec.extradata;
    std::int64_t total = info[13] & 0x0f;
    for (int index = 14; index < 18; ++index) total = (total << 8) | info[index];
    if (total == 0) return false;
    const auto timestamp = frame.best_effort_timestamp != AV_NOPTS_VALUE
        ? frame.best_effort_timestamp : frame.pts;
    if (timestamp == AV_NOPTS_VALUE) return false;
    const auto origin = stream.start_time == AV_NOPTS_VALUE ? 0 : stream.start_time;
    if (timestamp < origin) return false;
    const auto start = av_rescale_q(timestamp - origin, stream.time_base,
                                   AVRational{1, codec.sample_rate});
    return start >= 0 && start <= total && total - start == frame.nb_samples;
}

} // namespace agplayer
