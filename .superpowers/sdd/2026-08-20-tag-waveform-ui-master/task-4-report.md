# Task 4 Report — Reference tag-management QML workspace

## Status

COMPLETE. Implementation commit: `8d8f674`.

The existing detachable/dockable `ListWindow` now presents one
continuous, rounded three-column QML workspace without changing
`WindowController`, the application window graph, or any C++ model/provider.

## Implementation

- Replaced the former lower card layout with one shared surface and 1 px
  dividers: 256 px navigation, a fill-width center with a real 680 px minimum,
  and a 328 px tag panel. The 1284 px window minimum accounts for the surface
  margins and border as well as the three content columns.
- Reworked `SideNavigation` to use the registered `LibraryNavigationModel` in
  one reusable `ListView`, including real counts, resource-root expansion,
  playlist actions/drop targets, and mutually exclusive library/tag/folder/
  playlist selected states.
- Kept exactly one shared `TrackList`/`LibraryFilterModel`. Navigation and tags
  only change the existing `category`, `resourceFolder`, and `tagKey` filters.
  The header order is now `# | 歌曲 | 收藏 | 艺术家 | 专辑 | 评分 | BPM | 时长`.
- Added the real `TagManagementPanel`: its top row contains only search and add,
  its virtual `GridView` uses exactly three columns via `cellWidth: width / 3`,
  and its 28 px pills support ellipsis/tooltip, real `TagModel.createTag`, and
  selected-tag filtering. Task 5 rename/delete/color menus remain deferred.
- Added the lightweight QML thumbnail wrapper. Track rows are 62 px with the
  setting enabled and immediately return to 42 px when disabled; covers are
  34×34 and visible row waveforms are 9 px. Delegate reuse assigns a new
  generation, requests/cancels by `(trackId, sourcePath, generation)`, rejects
  stale callbacks, and changes mode by color binding without another request.
  Disabled rows instantiate no wrapper or render item.
- Added semantic dark/light workspace, divider, selected, secondary-text, add,
  and monochrome-waveform tokens to `Theme.qml`; no new dependency was added.

## TDD evidence

The initial focused QuickTest RED run completed with 59 passed, 3 failed and 1
platform skip. The three intended failures were the absent `listWorkspace`,
absent `TagManagementPanel`, and absent thumbnail integration/count contract.

The final QML behavior suite passed 63 tests with 0 failures and 1 existing
platform skip. It covers:

- one shared list and exact 256/680/328 layout at the minimum width;
- header order and 56 px header / 66 px bottom filter dimensions;
- real tag add, case-insensitive search, selection and filter updates;
- 62/42 row switching and 34×34 cover sizing;
- disabled zero wrapper/item/cache-read behavior and visible-row requests;
- generation cancellation, stale callback rejection, and mode changes without
  another provider request;
- existing selection, playback, favorite/rating, context-menu, delete and drag
  interactions through the complete QML regression.

## Verification

Visual Studio 2022 x64 environment, `windows-msvc-debug` preset:

```text
qml_main_window_test ..................... passed
track_waveform_thumbnail_item_test ....... passed
track_waveform_thumbnail_provider_test ... passed
settings_controller_test ................. passed
tag_model_test ........................... passed
library_navigation_model_test ............ passed
library_filter_model_test ................ passed
tag_management_layout_contract_test ...... passed
8/8 tests passed
```

The requested targets compiled successfully with Qt 6.7/MSVC. `git diff
--check` passed. The supplemental PowerShell contract is registered with CTest;
the QML behavior suite remains the primary acceptance evidence.

## Scope and remaining acceptance

No C++ model, provider, item, settings, registration, native-drop, playback, or
main-waveform source was changed. Per the master plan, tag/folder context menus,
system-drop expansion and one-time drag preview remain Task 5, while real
1447×1087 screenshot comparison and the 100–200% DPI visual P0/P1/P2 loop
remain Task 7. This report therefore makes no final pixel-acceptance claim.
