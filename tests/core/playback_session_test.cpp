// Test files deliberately keep assert() active even in Release builds: many
// test cases embed function calls with side effects inside assert(), and
// silencing them under NDEBUG would skip those calls and crash on cleanup.
#undef NDEBUG

#include "playback_session.hpp"

#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>

int main()
{
    agplayer::PlaybackSession session(
        [](const std::size_t, const std::size_t) { return std::size_t{2}; });
    session.set_queue({"a", "b", "c"}, 0);
    assert(session.size() == 3);
    assert(session.index() == 0);
    assert(session.path_at(1) == "b");
    assert(session.current_path() == "a");
    assert(session.next_index() == 1);
    assert(session.previous_index() == agplayer::PlaybackSession::npos);
    assert(session.queue_next("c"));
    assert(session.path_at(1) == "c");
    assert(session.next_index() == 1);
    assert(session.queue_next("b"));
    assert(session.path_at(1) == "b");
    assert(!session.queue_next("a"));

    session.set_mode(agplayer::PlaybackMode::RepeatOne);
    assert(session.mode() == agplayer::PlaybackMode::RepeatOne);
    assert(session.next_index() == 0);
    assert(session.previous_index() == 0);

    session.set_mode(agplayer::PlaybackMode::Shuffle);
    assert(session.next_index() == 2);

    session.set_index(2);
    session.set_mode(agplayer::PlaybackMode::RepeatAll);
    assert(session.next_index() == 0);
    session.set_index(0);
    assert(session.previous_index() == 2);

    session.set_index(1);
    assert(session.index() == 1);
    assert(session.current_path() == "b");

    session.mark_error("decode failed");
    assert(session.state() == agplayer::PlaybackState::Error);
    assert(session.error_message() == "decode failed");

    bool rejected_index = false;
    try {
        session.set_queue(std::vector<std::string>{"only"}, 1);
    } catch (const std::out_of_range&) {
        rejected_index = true;
    }
    assert(rejected_index);

    session.clear();
    assert(session.size() == 0);
    assert(session.next_index() == agplayer::PlaybackSession::npos);
}
