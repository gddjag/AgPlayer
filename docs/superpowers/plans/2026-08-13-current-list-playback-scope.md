# Current-list Playback Scope Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Keep playback within the selected visible list, with controlled fallback only for lists shorter than five songs.

**Architecture:** The list view supplies its ordered visible track IDs. The controller builds one scoped queue, while the core playback session enforces its boundary consistently for automatic transitions and transport buttons.

**Tech Stack:** C++20, Qt 6/QML, C API, CMake/CTest.

## Global Constraints

- Five or more playable songs never leave the starting list.
- Fewer than five playable songs may enter the rest of the library only after the starting scope is exhausted.
- Sequential, RepeatOne, Shuffle, and RepeatAll must all honor the scope.
- Do not modify audio tools, waveform rendering, or unrelated dirty files.

---

### Task 1: Core queue-scope navigation

**Files:**
- Modify: `tests/core/playback_session_test.cpp`
- Modify: `core/src/playback_session.hpp`
- Modify: `core/src/playback_session.cpp`

**Interfaces:**
- Produces: `PlaybackSession::set_queue(paths, start_index, scope_size, allow_fallback)`.

- [ ] Add failing tests for Sequential, RepeatOne, Shuffle, and RepeatAll with 4-song and 5-song scopes.
- [ ] Run `playback_session_test` and verify the boundary assertions fail.
- [ ] Store the scope boundary and Shuffle visit state; enforce wrap/fallback rules in `next_index_from()` and `previous_index()`.
- [ ] Run `playback_session_test` and verify it passes.

### Task 2: Carry the scope through the playback API

**Files:**
- Modify: `core/src/audio_engine.hpp`
- Modify: `core/src/audio_engine.cpp`
- Modify: `core/src/core_context.hpp`
- Modify: `core/src/core_context.cpp`
- Modify: `core/include/agplayer/c_api.h`
- Modify: `core/src/c_api.cpp`

**Interfaces:**
- Produces: `ag_player_set_scoped_queue(player, paths, count, start_index, scope_size, allow_fallback)`.

- [ ] Add a C API contract test that rejects invalid scope sizes and accepts a valid scoped queue.
- [ ] Run the C API test and verify the new entry point is missing.
- [ ] Add the scoped overload through AudioEngine, CoreContext, and the C API.
- [ ] Run the C API and playback-session tests.

### Task 3: Start playback from the visible list

**Files:**
- Modify: `tests/qt/library_filter_model_test.cpp`
- Modify: `tests/qt/playback_controller_test.cpp`
- Modify: `qt/src/library_filter_model.hpp`
- Modify: `qt/src/library_filter_model.cpp`
- Modify: `qt/src/playback_controller.hpp`
- Modify: `qt/src/playback_controller.cpp`
- Modify: `app/qml/AgPlayer/components/TrackList.qml`

**Interfaces:**
- Produces: `TrackList.visibleTrackIds()` and `PlaybackController::playTrackIds(track_ids, current_track_id)`.

- [ ] Add failing tests for a five-song strict queue and a four-song queue whose fallback follows the complete visible scope.
- [ ] Run both Qt tests and verify failure from the missing APIs.
- [ ] Implement `visibleTrackIds()` and `playTrackIds()` with validation, deduplication, rotation, and fallback construction.
- [ ] Route double-click and context-menu Play through the scoped-list API.
- [ ] Run the focused Qt and QML tests, then the full existing test suite.
