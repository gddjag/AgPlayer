#pragma once

#include "decoder.hpp"
#include "multitrack_editor.hpp"
#include "time_pitch_engine.hpp"

#include <agplayer/c_api.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace agplayer {

// Streaming, non-destructive timeline mixer used by the editor preview path.
// It keeps decoder state per clip and never writes an intermediate audio file.
class TimelinePreviewMixer final {
public:
    TimelinePreviewMixer() = default;
    ~TimelinePreviewMixer();

    TimelinePreviewMixer(const TimelinePreviewMixer&) = delete;
    TimelinePreviewMixer& operator=(const TimelinePreviewMixer&) = delete;

    ag_result configure(const MultiTrackEditConfig& config,
                        int sample_rate,
                        int channels,
                        std::string& error);
    ag_result read(float* output, std::size_t frame_count,
                   std::string& error);
    ag_result seek(std::int64_t position_ms, std::string& error);

    [[nodiscard]] std::int64_t position_frames() const noexcept;
    [[nodiscard]] std::int64_t position_ms() const noexcept;
    [[nodiscard]] std::int64_t duration_ms() const noexcept;
    [[nodiscard]] bool at_end() const noexcept;
    [[nodiscard]] int sample_rate() const noexcept;
    [[nodiscard]] int channels() const noexcept;

private:
    struct ClipState {
        ClipState() = default;
        ~ClipState();
        ClipState(ClipState&&) noexcept;
        ClipState& operator=(ClipState&&) noexcept;
        ClipState(const ClipState&) = delete;
        ClipState& operator=(const ClipState&) = delete;

        MultiTrackEditConfig::Track config;
        std::unique_ptr<Decoder> decoder;
        DecodedAudioBlock block;
        std::size_t block_offset = 0U;
        std::int64_t next_source_frame = -1;
        std::int64_t trim_start_frame = 0;
        std::int64_t trim_end_frame = 0;
        std::int64_t timeline_start_frame = 0;
        std::int64_t timeline_duration_frames = 0;
        bool uses_time_processor = false;
        std::unique_ptr<ITimePitchEngine> processor;
        std::vector<float> processor_output;
        std::vector<float> processor_input;
        std::vector<float> processor_receive;
        std::size_t processor_output_offset = 0U;
        std::int64_t next_processed_frame = 0;
        std::int64_t next_processor_source_frame = 0;
        bool processor_flushed = false;
    };

    static float fade_gain(FadeCurve curve, float progress) noexcept;
    bool source_frame_for(const ClipState& clip,
                          std::int64_t timeline_frame,
                          std::int64_t& source_frame,
                          std::int64_t& local_frame) const noexcept;
    ag_result seek_clip(ClipState& clip, std::int64_t source_frame,
                        std::string& error);
    ag_result sample_clip(ClipState& clip, std::int64_t source_frame,
                          float& left, float& right, std::string& error);
    void reset_processor(ClipState& clip) noexcept;
    ag_result sample_processed_clip(ClipState& clip,
                                    std::int64_t local_frame,
                                    float& left, float& right,
                                    std::string& error);

    std::vector<ClipState> clips_;
    int sample_rate_ = 0;
    int channels_ = 0;
    std::int64_t position_frames_ = 0;
    std::int64_t duration_frames_ = 0;
    bool at_end_ = false;
};

} // namespace agplayer
