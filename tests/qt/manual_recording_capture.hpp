#pragma once

#include "audio_editor/recording_session.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

class ManualRecordingStartGate final {
public:
    void waitForRelease()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        start_attempted_ = true;
        changed_.notify_all();
        changed_.wait(lock, [this] { return released_; });
    }

    bool waitForStartAttempt(const std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, timeout,
                                 [this] { return start_attempted_; });
    }

    void release()
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        released_ = true;
        changed_.notify_all();
    }

    bool waiting() const
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        return start_attempted_ && !released_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    bool start_attempted_{};
    bool released_{};
};

class ManualRecordingCapture final
    : public agplayer::editor::RecordingCapture {
public:
    explicit ManualRecordingCapture(
        std::shared_ptr<ManualRecordingStartGate> startGate = {})
        : start_gate_(std::move(startGate))
    {
    }

    bool start(const agplayer::editor::RecordingConfig& config) override
    {
        if (start_gate_) start_gate_->waitForRelease();
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
        if (start_gate_ && start_gate_->waiting()) {
            return agplayer::editor::RecordingState::Starting;
        }
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
    agplayer::editor::RecordingPcmSnapshot takePcmSnapshot(
        const agplayer::editor::SampleFrame startFrame,
        const agplayer::editor::SampleFrame endFrame,
        const std::size_t maximumFrames) const override
    {
        return session_.takePcmSnapshot(startFrame, endFrame, maximumFrames);
    }
    std::string lastError() const override { return session_.lastError(); }

    std::size_t feed(const std::vector<float>& interleaved,
                     const std::size_t frames)
    {
        return session_.pushCapturedFrames(interleaved.data(), frames);
    }

private:
    std::shared_ptr<ManualRecordingStartGate> start_gate_;
    agplayer::editor::RecordingSession session_;
};
