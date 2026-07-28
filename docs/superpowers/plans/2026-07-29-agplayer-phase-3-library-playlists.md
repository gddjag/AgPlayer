# AgPlayer Phase 3 Library, Playlists, and Search Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace placeholder playlist/history behavior with persistent real data, make rating and combined filters fully interactive, and prove that a 10,000-track library loads and filters within the product targets.

**Architecture:** Keep tracks and mutable track metadata in `LibraryModel`/`LibraryStore`. Add one small `PlaylistModel` (`QAbstractListModel`) that owns custom playlist names and stable track-id membership, persists to a local JSON file, and is shared by the existing `LibraryFilterModel` and QML windows. Record play history on the existing track record using stable IDs. Continue to use one in-memory proxy for keyword/rating/BPM/category filtering and one virtualized QML `ListView`.

**Tech Stack:** Qt 6 Core/Quick/QML, C++17, QAbstractListModel/QSortFilterProxyModel, QSaveFile JSON persistence, Qt Test/Quick Test, CMake/CTest.

---

## Task 1: Lock down rating, history, and combined filtering behavior

**Files:**
- Create: `tests/qt/library_filter_model_test.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `qt/src/library_model.hpp`
- Modify: `qt/src/library_model.cpp`
- Modify: `qt/src/library_filter_model.hpp`
- Modify: `qt/src/library_filter_model.cpp`
- Modify: `qt/src/library_store.cpp`

- [ ] Add failing tests for case-insensitive title/artist/album search, rating threshold, BPM range, favorites, history, and all filters combined.
- [ ] Add `LibraryModel::setRating()` with clamping, `markPlayed()` with play count/time, role updates, and persistence.
- [ ] Make `PlaybackController` mark the stable current track as played only after play succeeds.
- [ ] Ensure invalid rows and unchanged values do not emit writes or model churn.

## Task 2: Implement a persistent custom playlist model

**Files:**
- Create: `qt/src/playlist_model.hpp`
- Create: `qt/src/playlist_model.cpp`
- Create: `tests/qt/playlist_model_test.cpp`
- Modify: `qt/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] Test create, trim/reject duplicate names, rename, delete, add/remove stable track IDs, duplicate membership, and count roles.
- [ ] Test atomic JSON save/load and malformed-file recovery.
- [ ] Keep membership independent of library row order and remove no track files.
- [ ] Expose only the minimum QML invokables needed by the existing UI.

## Task 3: Integrate playlists with filtering and application lifetime

**Files:**
- Modify: `qt/src/library_filter_model.hpp`
- Modify: `qt/src/library_filter_model.cpp`
- Modify: `qt/src/qml_registration.hpp`
- Modify: `qt/src/qml_registration.cpp`
- Modify: `app/main.cpp`
- Modify: QML test setup files that call `register_agplayer_qml_types`

- [ ] Inject the shared `PlaylistModel` into the existing QML singleton registration.
- [ ] Let `LibraryFilterModel` filter custom playlist IDs through stable membership.
- [ ] Load playlists beside `library.json`, flush on shutdown, and never write user data during QA test mode outside the isolated path.
- [ ] Remove hard-coded playlist counts/categories that masquerade as real data.

## Task 4: Make list-window actions real and remove placeholders

**Files:**
- Modify: `app/qml/AgPlayer/components/SideNavigation.qml`
- Modify: `app/qml/AgPlayer/components/TrackList.qml`
- Modify: `app/qml/AgPlayer/components/SearchFilter.qml`
- Modify: `app/qml/AgPlayer/ListWindow.qml`
- Modify: `tests/qml/tst_main_window.qml` or add a focused list-window Quick Test

- [ ] Render All/Favorites/History plus real custom playlists from `PlaylistModel`.
- [ ] Add create/rename/delete playlist actions with validation.
- [ ] Add/remove tracks from playlists and make stars set the real rating.
- [ ] Make “clear filters” reset keyword/rating/BPM together.
- [ ] Show highlighted keyword matches where practical and a real no-result surface with one-click reset.
- [ ] Add 200 ms debounce to BPM slider changes while keeping text input immediate.

## Task 5: Prove 10,000-track performance

**Files:**
- Create: `tests/stress/library_model_stress_test.cpp`
- Modify: `tests/CMakeLists.txt`
- Reuse: `app/qml/AgPlayer/components/TrackList.qml`

- [ ] Generate 10,000 in-memory records without audio-file duplication.
- [ ] Measure model replacement, keyword filter, combined filter, category filter, and random row lookup.
- [ ] Require initial model load within 1 second and each in-memory filter operation within 200 ms on the Windows QA machine.
- [ ] Record working-set delta and confirm ListView remains virtualized (delegate count bounded by viewport).

## Task 6: Phase 3 verification and acceptance record

**Files:**
- Modify: `docs/qa/phase-status.md`
- Extend only if required: `scripts/qa-library-smoke.ps1`

- [ ] Build Debug with zero compiler warnings/errors.
- [ ] Run all non-stress tests.
- [ ] Run the new 10,000-track stress test.
- [ ] Run the real production list-window import/persistence/restart smoke.
- [ ] Inspect list screenshots and require zero `WARN`, `ERROR`, or `FATAL` log lines.
- [ ] Commit the verified Phase 3 implementation; do not package an EXE.
