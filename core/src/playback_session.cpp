#include "playback_session.hpp"

#include <stdexcept>
#include <utility>

namespace agplayer {

PlaybackSession::PlaybackSession(ShuffleIndexFunction shuffle_index)
    : shuffle_index_(std::move(shuffle_index))
{
}

void PlaybackSession::set_queue(std::vector<std::string> paths,
                                const std::size_t start_index)
{
    if ((!paths.empty() && start_index >= paths.size())
        || (paths.empty() && start_index != 0U)) {
        throw std::out_of_range("Playback queue start index is out of range");
    }

    paths_ = std::move(paths);
    index_ = start_index;
    state_ = PlaybackState::Stopped;
    error_.clear();
}

void PlaybackSession::set_mode(const PlaybackMode mode) noexcept
{
    mode_ = mode;
}

std::size_t PlaybackSession::next_index() const
{
    if (paths_.empty()) {
        return npos;
    }
    if (mode_ == PlaybackMode::RepeatOne) {
        return index_;
    }
    if (mode_ == PlaybackMode::Shuffle) {
        if (!shuffle_index_) {
            return npos;
        }
        const std::size_t candidate = shuffle_index_(index_, paths_.size());
        return candidate < paths_.size() ? candidate : npos;
    }
    return index_ + 1U < paths_.size() ? index_ + 1U : npos;
}

std::size_t PlaybackSession::previous_index() const noexcept
{
    if (paths_.empty()) {
        return npos;
    }
    if (mode_ == PlaybackMode::RepeatOne) {
        return index_;
    }
    return index_ > 0U ? index_ - 1U : npos;
}

const std::string& PlaybackSession::current_path() const
{
    if (paths_.empty()) {
        throw std::logic_error("Playback queue is empty");
    }
    return paths_[index_];
}

PlaybackState PlaybackSession::state() const noexcept
{
    return state_;
}

const std::string& PlaybackSession::error_message() const noexcept
{
    return error_;
}

void PlaybackSession::mark_error(std::string message)
{
    error_ = std::move(message);
    state_ = PlaybackState::Error;
}

} // namespace agplayer
