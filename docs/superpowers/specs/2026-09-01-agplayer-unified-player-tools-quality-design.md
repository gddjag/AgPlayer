# AgPlayer Unified Player, Waveform, and Audio Tools Quality Design

**Date:** 2026-09-01

**Status:** Approved on 2026-09-01

## Goal

Correct the in-scope reported regressions and consistency problems without
creating theme-specific business logic. Player actions, waveform styling, audio
preview ownership, model discovery, and tool behavior remain shared; each shell
controls only its layout profile and visual tokens.

## Scope boundary

Requirement 8 (`沉浸视觉` frame rate, reactor motion, visual quality, crash, and
immersive-waveform progress contrast) is owned by a dedicated Codex session and
is not implemented in this session. This session must not edit immersive view,
terrain reactor, immersive shader, or immersive-only test files. Shared waveform
and theme interfaces remain backward-compatible so the dedicated work can merge
cleanly. After that branch is delivered, final integration may run cross-feature
regressions but must not silently rewrite its implementation.

## Product constraints

- AgPlayer remains free, lightweight, clean, and subscription-free.
- Existing playback, library, editing, and tool data models are preserved.
- Theme shells may arrange controls differently, but they may not duplicate
  action behavior or invent separate feature implementations.
- Static UI must not add continuous CPU/GPU work. Expensive visual work must be
  demand-driven, bounded, cancelable, and able to degrade gracefully.
- Model and metadata operations must not trade file integrity for apparent
  success.
- No placeholder buttons, fake progress, silent failures, or dead compatibility
  branches may be shipped.

## Chosen approach

Use a shared-contract incremental refactor.

1. Extend the existing shared C++ settings/controllers and scene-graph items.
2. Consolidate common QML behavior into existing shared components such as
   `TransportControls.qml`, `TrackList.qml`, and `SharedWaveformView.qml`.
3. Give each shell a declarative layout profile for action visibility and
   placement. Shell profiles contain no playback logic.
4. Repair audio editor, separation, and metadata behavior at their controller or
   engine boundary, then bind the corrected state into QML.
5. Add focused regression tests before each fix and record visual/performance
   evidence for every numbered requirement.

Page-by-page theme patches were rejected because they recreate the drift this
work is intended to remove. A full UI rewrite was rejected because it adds
unnecessary regression and delivery risk.

## Architecture

### Shared player action and layout contract

`TransportControls.qml` remains the single implementation of transport actions.
The surrounding player components consume a small shell profile describing:

- leading actions;
- centered transport actions;
- trailing actions;
- whether metadata, rating, favorite, speed, BPM, zoom, and volume are visible;
- the shell-specific ordering shown in the approved reference images.

All normal shells use a 20-pixel icon visual size and a minimum 36-pixel hit
target. The main play/pause control may remain visually larger. Classic dual
window, integrated single window, mini player, and empty/startup states center
the transport group against the full player strip, not the leftover space.
Expanding the volume control reserves no space to the left; its flyout grows to
the right. The rolling/DJ shell is the explicit exception required by item 13:
its transport group is left aligned beneath the large waveform.

The theme chooser is anchored to the invoking theme icon and opens above it. It
does not migrate to a window corner or another shell. Shell-specific action
visibility is declarative, while action handlers and tooltips remain shared.

### Shared waveform visual policy

`FrequencyColorWaveformSettings`, `SettingsController`, `WaveformItem`,
`TrackWaveformThumbnailItem`, and shared QML waveform bindings form one visual
policy:

- frequency-color palette entries are labelled `最低频`, `低频`, `低中频`,
  `中频`, `中高频`, `高频`, `更高频`, and `最高频`;
- hexadecimal values are not displayed in the normal settings UI;
- solid unplayed waveform color defaults to `#9098A6`;
- played and unplayed frequency-color contrast uses one shared opacity rule in
  dark and light themes and in all shells;
- list-thumbnail brightness is a separate setting with a default of 66%;
- the list setting is consumed by every shell that hosts `TrackList.qml`;
- progress recoloring updates scene-graph vertex color data without rebuilding
  peak geometry.

The color chooser becomes a compact application-styled popup, targeted at
360 x 430 logical pixels on a normal desktop and capped to 90% of the available
screen. It retains keyboard focus, current-color preview, HSV controls, confirm,
and cancel behavior.

### Waveform thumbnail fast path

List thumbnails prioritize time-to-first-visible-row over full-player detail.

- visible rows receive higher priority than prefetch rows;
- leaving the viewport cancels pending work for that delegate generation;
- cached compact peak bytes render immediately;
- cold-cache work first produces a bounded low-resolution envelope, then adds
  optional spectral color data without blocking the initial waveform;
- the compact path uses no more geometry than required by physical row pixels;
- provider completion remains generation-checked so recycled delegates cannot
  publish stale data;
- memory and pending-work bounds remain enforced.

On the current QA machine, the first ten cached visible thumbnails must appear
within 300 ms at the reference list size. A cold list must remain interactive
and show its first visible waveform within one second. Before/after timings,
cache state, row count, and display scale are recorded with the evidence.

### Full waveform and rolling-player rendering

The rolling/DJ waveform uses the same decoded duration, peak pyramid, hover-time
mapping, and played/unplayed policy as other shells. Peak density is derived from
physical pixels and the visible time range rather than scaling a low-resolution
texture. Position-only updates do not rebuild geometry.

The rolling shell restores metadata, favorite, rating, accurate list columns,
hover time capsules on the overview waveform, tighter side margins, and a lower
overall height without distorting content. Its list uses the shared integrated
row presentation from the references.

The dual-window full-waveform binding is refreshed when source, cache generation,
shell, theme, size, or device-pixel ratio changes. A theme toggle must not be
required to make the second half appear.

### Lyrics placement and behavior

The normal lyrics panel is hosted beneath the list content and above the player
boundary, never over the player controls. Moving the pointer outside for three
seconds hides only the translucent surface and controls; lyric text remains.
Re-entry restores the controls without moving the text.

The existing free provider chain keeps at least three routes. It exposes route
name, attempt state, timeout, rejection diagnostic, and selected route. Failures
advance automatically and the user sees which route failed. Refresh uses a
recognizable circular-arrow icon, all lyric/refresh icons use the shared visual
size, and every icon has a hover tooltip.

No provider key, subscription, paid endpoint, or fabricated online result is
introduced.

### Settings, information panel, and equalizer

The song-list settings add `波形显示明亮度` immediately after thumbnail color,
defaulting to 66% and updating visible rows live.

The About section reads the application version from the same build/version
source used by the executable and installer. It displays:

```text
免费、轻便、纯净
版本号：v1.0.0
```

The version line updates automatically with future build-version changes.

`AudioFileInfoPanel.qml` becomes a narrow portrait panel. Its cover is 180
logical pixels wide, the normal panel width is 228 logical pixels, and long
metadata scrolls vertically instead of widening the panel. Labels, values,
spacing, and section baselines remain aligned.

The filter/search, rating, BPM, and clear controls use one shared compact height.

`EqualizerWindow.qml` uses restrained system-like typography, regular font
weights, small shared radii, compact spacing, and existing theme tokens. It
retains all 18 bands and readable labels without oversized bold text or large
rounded cards.

### Audio editor interaction contract

The editor implements the following pointer and keyboard rules:

- dragging the vertical timeline volume control upward increases volume;
- dragging downward decreases volume;
- wheel-up increases and wheel-down decreases using the same bounded step;
- the horizontal waveform range control represents the visible interval with
  two handles: the full document initially fills the bar, shortening the selected
  interval zooms in, and moving the interval pans without changing its length;
- `Ctrl+right-click` on an event selects that event and applies a subtle
  translucent selected highlight;
- subsequent event edits target the selected event;
- the shortcut legend includes `Ctrl+右键=选择片段`.

The lower transport is reduced and vertically centered. Shortcut separators
receive balanced spacing. Neighboring panels remove excess bottom whitespace
and vertically center their content.

Export exposes real progress. The button fill advances green from left to right,
the label is larger and semibold, and successful completion reads `✔已导出` in
green. Failure and cancellation restore an actionable state and never display
success.

### Model download and discovery

Each catalog file owns an ordered list of candidate URLs rather than one URL.

1. official source;
2. third-party public mirror;
3. domestic mirror;
4. manual file discovery in the configured directory.

Official remains the default. The user can explicitly switch to `国内镜像`, and
a failed route advances automatically when the next route exists. Hugging Face
files may use the approved HF-Mirror endpoint; GitHub Release files may use the
approved domestic proxy endpoint. Every route uses HTTPS and the existing
expected byte count plus SHA-256 verification before atomic activation.

Download state includes route name, transferred bytes, total bytes, percentage,
green progress, pause/resume state, verification, and a clear failover notice.
Changing routes retains a valid partial download only when range support and
content identity are proven; otherwise the partial file is safely restarted.

The default displayed model directory is `D:/agplayer/Models`. `打开模型目录`
opens it, `更改目录` persists a valid local directory, and the purple `检测`
button performs an immediate scan. Entering the separation page also schedules
a scan. The filesystem watcher covers the selected root and known model
subdirectories.

Known catalog models are identified by filename, expected size, and SHA-256.
User-supplied ONNX files are inspected against supported tensor profiles and are
listed only when their input/output contract is compatible. Unsupported files
remain untouched and appear with an exact rejection reason. The custom card title
is `自定义模型`, and its explanatory copy and directory path match the approved
Chinese text.

Model quality badges use a shared translucent orange/yellow glass token for
`低配优先`, `推荐`, and `高品质`.

### Separation result interaction

Preview ownership is per stem. Clicking one stem stops the prior stem or mix,
starts only the chosen stem, and moves only that row's progress guide. The UI
shows an unambiguous active stem. Each stem volume slider supports direct pointer
dragging in addition to click and wheel input.

The trailing download icon copies only that stem into the already configured
output directory with a collision-safe filename. It does not open a folder
picker. Explicit `更改输出目录` remains the only action that chooses a directory.

### Metadata write safety

The reported `音频流回读验证失败` is reproduced with the actual failing fixture
before changing acceptance rules. The writer continues to use a staged file,
backup, stream-copy or format-appropriate remux, metadata readback, audio
preservation verification, atomic replacement, and rollback.

`强行编辑` means retrying with a sanitized metadata/remux profile and accepting
container-normalized differences that do not change audio payload. It never
means bypassing audio preservation checks. Unsupported fields are reported
individually. The original file remains intact whenever audio payload,
codec/sample-rate/channel contract, duration tolerance, or requested tags cannot
be verified.

### Audio tool navigation and preview ownership

All audio-tool pages consume one shared left-aligned navigation component whose
spacing, selection state, focus, hover, typography, and height match the approved
vocal-separation navigation.

A single preview-focus coordinator arbitrates main playback, editor preview,
conversion preview, metadata preview, filename-tool preview, and separation
preview. Starting any tool preview stops main playback and the previous tool
preview before granting ownership. Closing a page or changing its input releases
ownership. This is event-driven; no polling or permanent background thread is
added.

## Requirement traceability and acceptance

| ID | Acceptance evidence |
|---:|---|
| 1 | Dark/light screenshots for dual and integrated frequency-color progress; eight frequency labels; solid base-color assertion |
| 2 | Compact color-popup geometry, keyboard, confirm, and cancel tests |
| 3 | Reference-width screenshots proving transport center remains fixed while volume opens right |
| 4 | Startup, dual, integrated, mini, and rolling action-order tests plus icon-size assertions |
| 5 | Cached/cold thumbnail timing, visible-row priority, cancellation, memory-bound, and stale-generation tests |
| 6 | Portrait file-info screenshot with long metadata and cover-width assertions |
| 7 | Lyrics host geometry, three-second chrome hide, persistent text, route failover, icon, and tooltip tests |
| 8 | External dedicated session; this session records no completion claim and only performs post-merge compatibility regression when its branch is delivered |
| 9 | Dual-window half-waveform regression test and compact filter-control geometry test |
| 10 | 66% default/persistence/live-update tests in every list-hosting shell |
| 11 | About copy and build-version contract test |
| 12 | Integrated list reference screenshot and anchored theme-popup test |
| 13 | Rolling metadata/favorite/rating tests, density/timing checks, hover capsule, margins, height, controls, and list screenshot |
| 14 | Editor drag direction, wheel, two-handle zoom/pan, event selection/highlight, edit target, and shortcut tests |
| 15 | Editor transport/panel geometry and export progress/success/failure tests |
| 16 | Official/mirror/manual routes, failover, percentage, hash rejection, page scan, directory change, ONNX detection, per-stem preview, drag volume, and stem export tests |
| 17 | Reproduction fixture, successful requested-tag readback, audio preservation, backup, atomic replace, and forced-mode rollback tests |
| 18 | Shared navigation visual and interaction contract test across every tool page |
| 19 | Main-to-tool and tool-to-tool preview arbitration tests |
| 20 | EQ deep/light screenshot, 18-band presence, typography, radius, and window-size assertions |
| 21 | Full diff review, dead-code scan, QML lint, static checks, complete CTest run, real launch, real audio, and visual acceptance report |

## Validation sequence

1. Capture reproducible baselines for every reported bug and performance issue.
2. Add focused failing C++/QML/contract tests for the current workstream.
3. Implement the smallest shared fix and run its focused test group.
4. Run dark/light and all in-scope shell visual QA at reference and minimum sizes.
5. Run thumbnail performance gates.
6. Run metadata write/readback on copied fixtures and separation preview/export on
   generated outputs.
7. Run QML lint, static/translation/installer contracts, and the complete CTest
   suite.
8. Perform final diff review for duplicated handlers, dead code, unbounded work,
   lifecycle errors, and theme drift.

Packaging is not part of this implementation unless separately requested after
the complete acceptance gate passes.

## Delivery decomposition

Implementation is divided into reviewable, independently testable commits:

1. shared waveform policy and compact settings UI;
2. thumbnail fast path and list brightness;
3. shared player actions, shell profiles, list layout, and dialogs;
4. lyrics placement and failover UI;
5. rolling player density, metadata, and layout;
6. audio editor interaction and export feedback;
7. separation routes, discovery, preview, and export semantics;
8. metadata write/readback repair and preview arbitration;
9. EQ/tool-navigation polish and final cross-shell acceptance.

Each commit must leave its focused tests passing. The final integration commit is
allowed only after the complete validation sequence and a clean diff review.
