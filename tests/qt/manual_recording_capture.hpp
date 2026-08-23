#pragma once

#include "audio_editor/recording_session.hpp"

#include <cstddef>
#include <vector>

class ManualRecordingCapture final
    : public agplayer::editor::RecordingCapture {
public:
    bool start(const agplayer::editor::RecordingConfig& config) override
    {
        return session_.startManual(config);
    }

    bool pause() noexcept override { return session_.pause(); }
    bool resume() noexcept override { return session_.resume(); }
    agplayer::editor::RecordingResult stop() override
    {
        return session_.stop();
    }
    bool cancel() override { return session_.cancel(); }
    agplayer::editor::RecordingState state() const noexcept override
    {
        return session_.state();
    }
    agplayer::editor::SampleFrame framesCaptured() const noexcept override
    {
        return session_.framesCaptured();
    }
    agplayer::editor::RecordingLiveSnapshot takeLiveSnapshot(
        const std::size_t maximum) override
    {
        return session_.takeLiveSnapshot(maximum);
    }
    std::string lastError() const override { return session_.lastError(); }

    std::size_t feed(const std::vector<float>& interleaved,
                     const std::size_t frames)
    {
        return session_.pushCapturedFrames(interleaved.data(), frames);
    }

private:
    agplayer::editor::RecordingSession session_;
};
