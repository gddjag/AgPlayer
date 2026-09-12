#pragma once

#include <cstddef>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace agplayer {

enum class PlaybackState {
    Stopped,
    Loading,
    Playing,
    Paused,
    Error
};

enum class PlaybackMode {
    Sequential,
    RepeatOne,
    Shuffle,
    RepeatAll
};

class PlaybackSession final {
public:
    using ShuffleIndexFunction =
        std::function<std::size_t(std::size_t current_index, std::size_t queue_size)>;

    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    explicit PlaybackSession(ShuffleIndexFunction shuffle_index = {});

    void set_queue(std::vector<std::string> paths, std::size_t start_index);
    void set_scoped_queue(std::vector<std::string> paths,
                          std::size_t start_index,
                          std::size_t scope_size,
                          bool allow_fallback);
    bool queue_next(std::string path);
    void clear() noexcept;
    void set_mode(PlaybackMode mode) noexcept;
    void set_index(std::size_t index) noexcept;

    [[nodiscard]] std::size_t next_index() const;
    [[nodiscard]] std::size_t next_index_from(std::size_t current_index) const;
    [[nodiscard]] std::size_t previous_index() const noexcept;
    [[nodiscard]] const std::string& current_path() const;
    [[nodiscard]] const std::string& path_at(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t index() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] PlaybackMode mode() const noexcept;
    [[nodiscard]] PlaybackState state() const noexcept;
    [[nodiscard]] const std::string& error_message() const noexcept;

    void mark_error(std::string message);

private:
    [[nodiscard]] std::size_t next_shuffle_index(std::size_t current) const;
    void reset_scope_visits(std::size_t current) noexcept;

    std::vector<std::string> paths_;
    std::atomic<std::size_t> index_{0U};
    std::atomic<PlaybackMode> mode_{PlaybackMode::Sequential};
    PlaybackState state_ = PlaybackState::Stopped;
    std::string error_;
    ShuffleIndexFunction shuffle_index_;
    bool scope_enabled_ = false;
    std::size_t scope_size_ = 0U;
    bool allow_fallback_ = false;
    mutable std::mutex scope_mutex_;
    mutable std::vector<bool> scope_visited_;
};

} // namespace agplayer
