# AgPlayer Phase 5 — Window System and Mini Player

**Status:** Completed on 2026-07-29. Debug build and 43/43 CTest cases pass;
production-QML captures and focused review found no remaining Critical or
Important issue.

**Goal:** Complete the independent main, playlist, and mini windows with real
15 px four-edge docking, group drag, detach, synchronized visibility/state,
persisted geometry, close-to-tray behavior, and one shared playback state.

**Constraints:** Reuse the existing `WindowController` and QML windows. Keep one
audio engine, one playback controller, and one library model. Do not package an
EXE.

## Task 1 — Lock the window contract with failing tests

**Files**

- Modify: `tests/qt/window_controller_test.cpp`
- Modify: `tests/qml/tst_mini_player.qml`

Add tests for:

- snapping updates docked/detached state and respects the 15 px threshold;
- moving/resizing the main window repositions a docked list window;
- dragging the list beyond the threshold detaches it;
- main minimize/hide restores only a list window the user requested;
- main/list/mini geometry and dock edge reload from settings;
- close behavior selects tray hiding or ordered shutdown;
- mini pin, restore, minimize, close, transport, seek, favorite, and volume
  actions still target the shared controllers.

## Task 2 — Make `WindowController` the single window-state owner

**Files**

- Modify: `qt/src/window_controller.hpp`
- Modify: `qt/src/window_controller.cpp`

Implement:

- `magneticSnapEnabled`, `preferredDockEdge`, `listDockEdge`, and
  `closeBehavior` properties;
- explicit actual visibility versus user-requested list visibility;
- edge detection and snapping with a strict 15 px threshold;
- event filtering for main move/resize/state changes;
- group repositioning while docked and detach while the list is dragged;
- geometry/dock/always-on-top persistence via the existing application
  `QSettings` namespace;
- screen-safe restored geometry;
- separate close-to-tray and forced-exit paths.

## Task 3 — Wire settings and QML gestures without duplicate state

**Files**

- Modify: `app/main.cpp`
- Modify: `app/qml/AgPlayer/ListWindow.qml`
- Modify: `app/qml/AgPlayer/components/TitleBar.qml`
- Modify: `app/qml/AgPlayer/MiniPlayerWindow.qml`

Wire `SettingsController` values/signals to `WindowController`. Keep drag deltas
anchored to the original mouse press so a list window can move beyond the snap
threshold. Route minimize/restore/close through the controller where linked
window behavior is required.

## Task 4 — Implement the real tray close path

**Files**

- Modify: `CMakeLists.txt`
- Modify: `app/CMakeLists.txt`
- Modify: `app/main.cpp`

Use Qt's native system tray integration only in the executable. Default close
behavior hides all player windows to the tray; tray activation restores the
main/list pair; the tray Exit action runs the existing ordered shutdown once.
Direct-close mode bypasses the tray.

## Task 5 — Verify, inspect, review, and commit

Run:

```powershell
cmake --build build/msvc-debug --parallel 8
ctest --test-dir build/msvc-debug -C Debug --output-on-failure
git diff --check
```

Generate production-EXE QA captures for:

- main + list docked on every edge;
- list detached;
- mini player in dark and light themes.

Inspect logs for warnings/errors, request a focused code review, fix all
Critical/Important findings, update `docs/qa/phase-status.md`, and commit Phase
5 independently.
