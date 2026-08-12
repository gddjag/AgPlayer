#include "playback_session.hpp"

#include <algorithm>
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
    index_.store(start_index, std::memory_order_release);
    state_ = PlaybackState::Stopped;
    error_.clear();
}

bool PlaybackSession::queue_next(std::string path)
{
    if (path.empty() || paths_.empty()) {
        return false;
    }

    std::size_t current = index_.load(std::memory_order_acquire);
    const auto found = std::find(paths_.begin(), paths_.end(), path);
    if (found != paths_.end()) {
        const std::size_t existing =
            static_cast<std::size_t>(std::distance(paths_.begin(), found));
        if (existing == current) {
            return false;
        }
        paths_.erase(found);
        if (existing < current) {
            --current;
        }
    }

    paths_.insert(paths_.begin() + static_cast<std::ptrdiff_t>(current + 1U),
                  std::move(path));
    index_.store(current, std::memory_order_release);
    return true;
}

void PlaybackSession::clear() noexcept
{
    paths_.clear();
    index_.store(0U, std::memory_order_release);
    state_ = PlaybackState::Stopped;
    error_.clear();
}

void PlaybackSession::set_mode(const PlaybackMode mode) noexcept
{
    mode_.store(mode, std::memory_order_release);
}

void PlaybackSession::set_index(const std::size_t index) noexcept
{
    if (index < paths_.size()) {
        index_.store(index, std::memory_order_release);
    }
}

std::size_t PlaybackSession::next_index() const
{
    return next_index_from(index_.load(std::memory_order_acquire));
}

std::size_t PlaybackSession::next_index_from(const std::size_t current) const
{
    if (paths_.empty()) {
        return npos;
    }
    if (current >= paths_.size()) {
        return npos;
    }
    const PlaybackMode mode = mode_.load(std::memory_order_acquire);
    if (mode == PlaybackMode::RepeatOne) {
        return current;
    }
    if (mode == PlaybackMode::Shuffle) {
        if (!shuffle_index_) {
            return npos;
        }
        std::size_t candidate = shuffle_index_(current, paths_.size());
        if (candidate >= paths_.size()) {
            return npos;
        }
        if (paths_.size() > 1U && candidate == current) {
            candidate = (candidate + 1U) % paths_.size();
        }
        return candidate;
    }
    if (mode == PlaybackMode::RepeatAll) {
        return (current + 1U) % paths_.size();
    }
    return current + 1U < paths_.size() ? current + 1U : npos;
}

std::size_t PlaybackSession::previous_index() const noexcept
{
    if (paths_.empty()) {
        return npos;
    }
    const std::size_t current = index_.load(std::memory_order_acquire);
    const PlaybackMode mode = mode_.load(std::memory_order_acquire);
    if (mode == PlaybackMode::RepeatOne) {
        return current;
    }
    if (mode == PlaybackMode::RepeatAll) {
        return current > 0U ? current - 1U : paths_.size() - 1U;
    }
    return current > 0U ? current - 1U : npos;
}

const std::string& PlaybackSession::current_path() const
{
    if (paths_.empty()) {
        throw std::logic_error("Playback queue is empty");
    }
    return paths_[index_.load(std::memory_order_acquire)];
}

const std::string& PlaybackSession::path_at(const std::size_t index) const noexcept
{
    return paths_[index];
}

std::size_t PlaybackSession::index() const noexcept
{
    return index_.load(std::memory_order_acquire);
}

std::size_t PlaybackSession::size() const noexcept
{
    return paths_.size();
}

PlaybackMode PlaybackSession::mode() const noexcept
{
    return mode_.load(std::memory_order_acquire);
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
