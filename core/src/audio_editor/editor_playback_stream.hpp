#pragma once

#include "audio_document.hpp"
#include "../audio_stream_source.hpp"
#include "../time_pitch_engine.hpp"

#include <memory>
#include <string>

namespace agplayer::editor {

struct EditorPlaybackParameters final {
    double speed_ratio{1.0};
    bool keep_pitch{true};
    int pitch_cents{};
    bool formant_preservation{};
};

// Incrementally decodes an EventTimeline into interleaved float PCM. It owns
// decoder/DSP state only; output-device ownership stays in AudioEngine.
class EditorPlaybackStream final : public agplayer::IAudioStreamSource {
public:
    ~EditorPlaybackStream() override;

    EditorPlaybackStream(const EditorPlaybackStream&) = delete;
    EditorPlaybackStream& operator=(const EditorPlaybackStream&) = delete;

    [[nodiscard]] static std::shared_ptr<EditorPlaybackStream> create(
        TimelineSnapshot snapshot, const EditorPlaybackParameters& parameters,
        std::string& error,
        agplayer::TimePitchEngineFactory engine_factory =
            &agplayer::create_time_pitch_engine);

    [[nodiscard]] const agplayer::MediaMetadata& metadata() const noexcept override;
    [[nodiscard]] ag_result read(agplayer::DecodedAudioBlock& block) noexcept override;
    [[nodiscard]] ag_result seek(std::int64_t position_ms) noexcept override;

private:
    class Impl;
    explicit EditorPlaybackStream(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

} // namespace agplayer::editor
