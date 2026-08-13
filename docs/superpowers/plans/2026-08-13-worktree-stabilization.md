# Worktree Stabilization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (inline execution). Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert the mixed revised-ui diff into small, verified commits without changing other worktrees or packaging an EXE.

**Architecture:** Stabilize build metadata first, then stage files only at functional seams: playback queue, player/list UI, audio tools, and waveform/localization. Each seam has a narrow CMake build target and focused automated tests before its commit.

**Tech Stack:** C++20, Qt 6/QML, CMake, CTest, PowerShell, Git.

## Global Constraints

- Worktree: `D:\ai\AgPlayer\.worktrees\revised-ui` on `codex/revised-ui`.
- Preserve the other worktrees unchanged.
- No EXE packaging in this task.
- Do not include unverified waveform-owner changes in any non-waveform commit.

---

### Task 1: Establish a reproducible build baseline

**Files:**
- Modify: `core/CMakeLists.txt`, `qt/CMakeLists.txt`, `qt/src/filename_processor.cpp`, `qt/src/rename_journal_store.*`
- Test: `tests/qt/filename_processing_test.cpp`, `tests/qt/rename_transaction_test.cpp`

- [ ] Capture the exact all-target compiler error and the CMake source lists.
- [ ] Add a focused failing test for the missing rename-journal recovery contract if it is not already covered.
- [ ] Synchronize the header, implementation, and CMake source registration; remove only confirmed trailing whitespace.
- [ ] Build `filename_processing_test` and `rename_transaction_test`, then run their CTest entries.
- [ ] Commit only build-hygiene files as `fix: restore filename processing build contract`.

### Task 2: Verify current-list playback boundaries

**Files:**
- Modify: `core/include/agplayer/c_api.h`, `core/src/{audio_engine,core_context,c_api,playback_session}.{cpp,hpp}`, `qt/src/{playback_controller,library_filter_model}.{cpp,hpp}`, `app/qml/AgPlayer/components/TrackList.qml`
- Test: `tests/core/{playback_session_test,queue_gapless_test,c_api_lifecycle_test}.cpp`, `tests/qt/playback_controller_test.cpp`, `tests/qml/tst_main_window.qml`

- [ ] Run the scope tests against the current dirty state and record failures.
- [ ] Keep only the scope queue and visible-list entry-point changes required for list repeat, sequential, single repeat, and shuffle.
- [ ] Verify strict scope for five tracks and permitted fallback after a four-track scope is exhausted.
- [ ] Build and run the five focused playback test targets plus the QML context-menu play test.
- [ ] Commit only the playback-scope files as `fix: keep playback within active list`.

### Task 3: Isolate player, list, and shell UI behavior

**Files:**
- Modify: `app/qml/AgPlayer/{ListWindow.qml,components/MiniPlayerControls.qml,components/PlayerControls.qml,components/PlayerPane.qml,components/SideNavigation.qml,components/TrackList.qml}`, `qt/src/{settings_controller,qml_registration,waveform_item}.{cpp,hpp}`, `tests/qml/{tst_main_window.qml,tst_mini_player.qml}`, `tests/qt/{settings_controller_test,qml_main_window_test_main,qml_mini_player_test_main}.cpp`, `tests/scripts/installer_contract_test.ps1`

- [ ] Separate files belonging to player/list interactions from waveform-owner changes; leave the latter unstaged until Task 5.
- [ ] Run the focused QML and installer contract tests; fix only failures proven to belong to this group.
- [ ] Confirm no control clips at supported minimum dimensions and no context menu command is rendered with unreadable active-state colors.
- [ ] Commit verified player/list/shell files as `fix: stabilize player and list interactions`.

### Task 4: Isolate audio-editor, filename, and metadata tools

**Files:**
- Modify/Create: `app/qml/AgPlayer/components/audioeditor/**`, `app/qml/AgPlayer/components/tools/**`, `core/src/audio_editor/**`, `core/src/{metadata_writer,decoder}.{cpp,hpp}`, `qt/src/{audio_editor,audio_preview,filename_processor,filename_transform_engine,metadata_editor,rename_transaction}.{cpp,hpp}`
- Test: corresponding `tests/core`, `tests/qt`, `tests/qml/tst_filename_process.qml`, and layout contract scripts

- [ ] Run document, recording, time/pitch, metadata, filename, and rename tests independently.
- [ ] Remove only dead light-editor, multitrack, and library-manager references whose sources and test targets have been deleted together.
- [ ] Verify tools build without pulling player/waveform behavior into the commit.
- [ ] Commit each independently verifiable tool seam; do not combine audio editor, metadata, and rename changes if their tests are separable.

### Task 5: Finish waveform, translations, and repository gate

**Files:**
- Modify: `qt/src/waveform_item.*`, `app/qml/AgPlayer/components/PlayerPane.qml`, `translations/agplayer_{en,th,vi,zh}.ts`
- Test: `tests/qt/waveform_item_test.cpp`, `tests/qml/tst_main_window.qml`, `tests/qt/translation_manager_test.cpp`

- [ ] Take only the waveform changes approved by the waveform task; do not rewrite them during stabilization.
- [ ] Run waveform synchronization and translation tests and repair only failures rooted in this group.
- [ ] Run `git diff --check` and a full CMake build plus full CTest.
- [ ] Commit verified waveform/localization files as `fix: finalize waveform and localization behavior`.
