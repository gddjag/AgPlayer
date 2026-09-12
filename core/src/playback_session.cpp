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
    scope_enabled_ = false;
    scope_size_ = paths_.size();
    allow_fallback_ = false;
    reset_scope_visits(start_index);
    state_ = PlaybackState::Stopped;
    error_.clear();
}

void PlaybackSession::set_scoped_queue(std::vector<std::string> paths,
                                       const std::size_t start_index,
                                       const std::size_t scope_size,
                                       const bool allow_fallback)
{
    if (paths.empty() || start_index >= paths.size() || scope_size == 0U
        || scope_size > paths.size() || start_index >= scope_size) {
        throw std::out_of_range("Playback queue scope is out of range");
    }

    paths_ = std::move(paths);
    index_.store(start_index, std::memory_order_release);
    scope_enabled_ = true;
    scope_size_ = scope_size;
    allow_fallback_ = allow_fallback && scope_size < paths_.size();
    reset_scope_visits(start_index);
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
        if (scope_enabled_ && existing < scope_size_) {
            --scope_size_;
        }
        if (existing < current) {
            --current;
        }
    }

    paths_.insert(paths_.begin() + static_cast<std::ptrdiff_t>(current + 1U),
                  std::move(path));
    if (scope_enabled_ && current < scope_size_) {
        ++scope_size_;
        reset_scope_visits(current);
    }
    index_.store(current, std::memory_order_release);
    return true;
}

void PlaybackSession::clear() noexcept
{
    paths_.clear();
    index_.store(0U, std::memory_order_release);
    scope_enabled_ = false;
    scope_size_ = 0U;
    allow_fallback_ = false;
    reset_scope_visits(0U);
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
        if (scope_enabled_ && index < scope_size_) {
            const std::lock_guard<std::mutex> lock(scope_mutex_);
            if (index < scope_visited_.size()) {
                scope_visited_[index] = true;
            }
        }
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
        return next_shuffle_index(current);
    }
    if (scope_enabled_ && current < scope_size_) {
        if (current + 1U < scope_size_) {
            return current + 1U;
        }
        if (allow_fallback_ && scope_size_ < paths_.size()) {
            return scope_size_;
        }
        return 0U;
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
    if (scope_enabled_ && current < scope_size_) {
        return current > 0U ? current - 1U : scope_size_ - 1U;
    }
    if (mode == PlaybackMode::RepeatAll) {
        return current > 0U ? current - 1U : paths_.size() - 1U;
    }
    return current > 0U ? current - 1U : npos;
}

std::size_t PlaybackSession::next_shuffle_index(const std::size_t current) const
{
    if (!shuffle_index_) {
        return npos;
    }
    if (!scope_enabled_ || current >= scope_size_) {
        std::size_t candidate = shuffle_index_(current, paths_.size());
        if (candidate >= paths_.size()) {
            return npos;
        }
        if (paths_.size() > 1U && candidate == current) {
            candidate = (candidate + 1U) % paths_.size();
        }
        return candidate;
    }

    const std::lock_guard<std::mutex> lock(scope_mutex_);
    if (current < scope_visited_.size()) {
        scope_visited_[current] = true;
    }
    const auto unvisited = [this](const std::size_t index) {
        return index < scope_visited_.size() && !scope_visited_[index];
    };
    const bool scope_complete =
        std::none_of(scope_visited_.begin(), scope_visited_.end(),
                     [](const bool visited) { return !visited; });
    if (scope_complete) {
        if (allow_fallback_ && scope_size_ < paths_.size()) {
            return scope_size_;
        }
        std::fill(scope_visited_.begin(), scope_visited_.end(), false);
        scope_visited_[current] = true;
    }

    for (std::size_t attempt = 0U; attempt < scope_size_ * 2U; ++attempt) {
        const std::size_t candidate = shuffle_index_(current, scope_size_);
        if (candidate < scope_size_ && unvisited(candidate)) {
            return candidate;
        }
    }
    const auto found = std::find(scope_visited_.begin(), scope_visited_.end(), false);
    return found != scope_visited_.end()
        ? static_cast<std::size_t>(std::distance(scope_visited_.begin(), found))
        : current;
}

void PlaybackSession::reset_scope_visits(const std::size_t current) noexcept
{
    const std::lock_guard<std::mutex> lock(scope_mutex_);
    scope_visited_.assign(scope_enabled_ ? scope_size_ : 0U, false);
    if (current < scope_visited_.size()) {
        scope_visited_[current] = true;
    }
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
