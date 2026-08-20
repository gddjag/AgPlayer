# Task 3 Report — Single-node thumbnail rendering, settings, and runtime wiring

## Status

COMPLETE WITH BASELINE CONCERNS. Implementation commits:

- `5c4df1d` — `feat: render track waveform thumbnails with scene graph`
- `9c56194` — `feat: wire track waveform thumbnail runtime`

## Implementation

- Added `TrackWaveformThumbnailItem : QQuickItem` with only the requested
  `QByteArray peaks` and `QColor waveformColor` properties. Valid content is
  exactly 128 bytes and renders through one `QSGGeometryNode`, one embedded
  `QSGFlatColorMaterial`, and 256 `Point2D` vertices in symmetric line pairs.
- Geometry is dirtied only by changed peaks or item width/height. A color-only
  update keeps the node, geometry object, vertex allocation, and vertex bytes;
  invalid/empty peaks or non-positive dimensions delete the old node.
- Added the persisted appearance settings
  `listWaveformThumbnailEnabled` (default `true`) and
  `listWaveformThumbnailMode` (allowed `Color36`/`Mono`, default and invalid
  fallback `Color36`). Both participate in immediate persistence, edit
  snapshots, `saveAll`, `restoreDefaults`, waveform reset, and notifications.
- Added a small SettingsPage card with a real enable switch and mode combo. No
  broader layout/pixel redesign was attempted.
- `app/main.cpp` now owns exactly one `TagModel`, `LibraryNavigationModel`,
  persistent `LibraryManagerController`, and
  `TrackWaveformThumbnailProvider`. `tags.json` and `resource-roots.json` are
  siblings of the existing AppData `library.json`; the provider is constructed
  from `SettingsController::cacheDirectory()` and follows
  `cacheDirectoryChanged` through its existing setter.
- QML registration exposes the exact app-owned four objects as singleton
  instances, keeps `LibraryFilterModel` instantiable, and registers the item
  type. The qml_main harness constructs and registers the same dependency
  graph and verifies pointer identity.
- No list integration or request trigger was added. The QML integration test
  confirms provider `cacheReadAttempts == 0` and `queuedJobs == 0` before any
  TrackList loader exists.

## Dependency conflict and approved decision

`LibraryManagerPage.qml` previously constructed a local
`LibraryManagerController`, which conflicted with the required application-wide
unique controller and navigation dependency. Work paused as `NEEDS_CONTEXT`.
The parent approved scheme A: the page now binds its existing filters to the
registered singleton and retains its UI, behavior, and the existing
`libraryManagerController` object-name marker. No `TrackList.qml`,
`ListWindow.qml`, `SideNavigation.qml`, tag panel, or NativeDropRouter file was
modified.

## TDD evidence

1. Item RED: the new test failed to compile because
   `track_waveform_thumbnail_item.hpp` did not exist.
2. Settings RED: the new default/persistence/fallback/reset test failed to
   compile because the two settings APIs did not exist.
3. Item/settings GREEN: `track_waveform_thumbnail_item_test` and
   `settings_controller_test` passed 2/2 after the minimum implementation.
4. Runtime RED: qml_main reported
   `TrackWaveformThumbnailItem is not a type` before registration/wiring.
5. Runtime GREEN: the focused QuickTest selection (settings lazy-load
   prerequisite, new settings-control test, and new singleton/type/idle test)
   passed 5/5. A complete qml_main run also reached 59 passed, 0 failed,
   1 platform skip after implementation.

The item test uses a vertex-buffer sentinel to prove that a color-only render
does not rewrite geometry; it also checks the single node/no children, exactly
256 vertices, symmetric endpoints, peak/size rebuilding, invalid-content node
release, and absence of playback/progress/seek properties or signals.

## Build and focused verification

All configure/build commands loaded the Visual Studio 2022 Community x64
developer environment. `build/msvc-debug` successfully built `AgPlayer` and
the requested focused targets.

Passing verification:

```text
track_waveform_thumbnail_item_test ............... passed
settings_controller_test ......................... passed
track_waveform_thumbnail_provider_test ........... passed
track_waveform_thumbnail_stress_test ............. passed
track_waveform_thumbnail_contract_test ........... passed
waveform_provider_test ........................... passed
tag_model_test ................................... passed
library_navigation_model_test .................... passed
library_filter_model_test ........................ passed
focused qml_main runtime/settings selection ...... 5 passed, 0 failed
git diff --check .................................. passed
```

## Baseline concerns

- Repeated complete qml_main runs exposed pre-existing interaction-order
  instability outside this task: `test_track_title_surface_honors_ctrl_selection_and_double_click`
  sometimes retained a previous `drag-test-*` track, and one run also had the
  existing main-waveform click seek at 2000 ms instead of 1500 ms. The new
  Task 3 QML tests passed in those runs; the forbidden TrackList/main Waveform
  areas were not changed.
- `waveform_item_test` deterministically fails because baseline commit
  `f635258` already has `spectrumAttackSeconds() == 0.02` while its test expects
  `0.012`.
- `library_manager_controller_test` passes 11 tests but fails its existing
  Debug performance gate requiring a 10K-row filter below 100 ms.
- `git diff f635258` confirms the library-manager and main-Waveform source/test
  files involved in these failures are unchanged by Task 3. No out-of-scope
  correction is claimed.

Validation is MSVC Debug-focused. No Release, packaged-app, real playback,
hardware, or final pixel-acceptance claim is made.
