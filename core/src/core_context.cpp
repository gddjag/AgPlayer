#include "core_context.hpp"

#include <utility>

namespace agplayer {

CoreContext::CoreContext(const AudioBackend backend,
                         const std::size_t buffer_frames)
    : audio_engine_(backend, buffer_frames)
{
}

const std::string& CoreContext::last_error() const noexcept
{
    return last_error_;
}

void CoreContext::set_error(std::string value) noexcept
{
    try {
        last_error_ = std::move(value);
    } catch (...) {
        last_error_.clear();
    }
}

ag_result CoreContext::load(const std::string& utf8_path) noexcept
{
    return record(audio_engine_.load(utf8_path), "load failed");
}

ag_result CoreContext::play() noexcept
{
    return record(audio_engine_.play(), "play failed");
}

ag_result CoreContext::pause() noexcept
{
    return record(audio_engine_.pause(), "pause failed");
}

ag_result CoreContext::stop() noexcept
{
    return record(audio_engine_.stop(), "stop failed");
}

ag_result CoreContext::seek(const std::int64_t position_ms) noexcept
{
    return record(audio_engine_.seek(position_ms), "seek failed");
}

ag_result CoreContext::set_volume(const float volume) noexcept
{
    return record(audio_engine_.set_volume(volume), "volume change failed");
}

void CoreContext::set_muted(const bool muted) noexcept
{
    audio_engine_.set_muted(muted);
    last_error_.clear();
}

EngineSnapshot CoreContext::snapshot() const noexcept
{
    return audio_engine_.snapshot();
}

ag_result CoreContext::record(const ag_result result,
                              const char* operation) noexcept
{
    try {
        last_error_ = result == AG_OK ? "" : operation;
    } catch (...) {
        last_error_.clear();
    }
    return result;
}

} // namespace agplayer
