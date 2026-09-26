#pragma once

#include "time_pitch_engine.hpp"

#include <agplayer/c_api.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace agplayer {

class PlaybackTimePitchStageTestAccess;

struct PlaybackTimePitchConfig {
    double speed_ratio{1.0};
    bool keep_pitch{true};
};

class PlaybackTimePitchStage final {
public:
    explicit PlaybackTimePitchStage(
        TimePitchEngineFactory factory = &create_time_pitch_engine) noexcept
        : factory_(factory)
    {
    }

    PlaybackTimePitchStage(PlaybackTimePitchStage&&) noexcept = default;
    PlaybackTimePitchStage& operator=(PlaybackTimePitchStage&&) noexcept = default;

    ag_result configure(int sampleRate, int channels,
                        PlaybackTimePitchConfig config) noexcept;
    ag_result process(const float* samples, std::size_t frames,
                      std::vector<float>& output) noexcept;
    ag_result process(const float* samples, std::size_t frames,
                      std::int64_t source_start_frame,
                      std::int64_t source_end_frame,
                      std::vector<float>& output,
                      std::vector<std::int64_t>& source_frame_after) noexcept;
    ag_result finish(std::vector<float>& output) noexcept;
    ag_result finish(std::vector<float>& output,
                     std::vector<std::int64_t>& source_frame_after) noexcept;
    void reset() noexcept;

    [[nodiscard]] bool bypassed() const noexcept { return processor_ == nullptr; }
    [[nodiscard]] TimePitchEngineKind engine_kind() const noexcept
    {
        return processor_ != nullptr ? processor_->kind()
                                     : TimePitchEngineKind::None;
    }

private:
    friend class PlaybackTimePitchStageTestAccess;

    ag_result drain(std::vector<float>& output,
                    std::vector<std::int64_t>& source_frame_after) noexcept;
    void append_input_mapping(std::size_t frames,
                              std::int64_t source_start_frame,
                              std::int64_t source_end_frame);
    void append_output_mapping(std::size_t frames,
                               std::vector<std::int64_t>& source_frame_after);
    void compact_consumed_mapping();

    static constexpr std::size_t work_frames = 8'192U;
    static constexpr std::size_t mapping_compaction_threshold =
        work_frames * 2U;
    TimePitchEngineFactory factory_;
    std::unique_ptr<ITimePitchEngine> processor_;
    std::vector<float> receive_buffer_;
    std::vector<std::int64_t> pending_source_frames_;
    std::size_t pending_source_offset_{};
    std::size_t source_frame_debt_{};
    double source_step_accumulator_{};
    double speed_ratio_{1.0};
    std::int64_t last_source_frame_{};
    std::int64_t last_mapped_source_frame_{};
    int channels_{};
    bool configured_{};
    bool finished_{};
};

} // namespace agplayer
