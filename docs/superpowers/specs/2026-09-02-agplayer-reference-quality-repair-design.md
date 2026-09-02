# AgPlayer Reference Quality Repair Design

**Date:** 2026-09-02  
**Status:** Approved by the user on 2026-09-02  
**Parent designs:**

- `2026-09-01-agplayer-unified-player-tools-quality-design.md`
- `2026-09-01-agplayer-reference-regression-repair-design.md`

## Goal

Repair the reported classic, integrated, rolling, mini-player, audio-tool,
equalizer, separation, and metadata regressions while keeping playback state,
library data, waveform rendering policy, theme tokens, lyrics state, preview
ownership, and model discovery shared across every skin.

## Scope boundary

Immersive-only rendering, reactor animation, shader performance, and immersive
crash work remain owned by the dedicated immersive task. This repair may change
shared icon sizing and shared waveform interfaces, but it must not rewrite
immersive rendering or its tests.

The already-uncommitted theme-skin, lyrics, and immersive icon work in the main
worktree is preserved and included in later integration validation.

## Chosen architecture

Use an incremental shared-contract repair:

1. Keep one `TrackList.qml` and replace scattered presentation booleans with
   explicit classic, integrated, and rolling layout profiles.
2. Keep controllers and shared actions authoritative; shells only declare
   action order, visibility, geometry, and visual tokens.
3. Route every waveform host through the shared progress-opacity and coordinate
   mapping policy.
4. Fix editor, separation, and metadata behavior at controller or core
   boundaries; QML only presents structured state.

Per-theme copies were rejected because they recreate drift. A full rewrite was
rejected because it adds unnecessary regression risk.

## Classic window and list geometry

- A fresh classic player opens at `863 x 266` logical pixels.
- Classic, integrated, and rolling geometries remain stored independently.
- Switching back to classic restores its classic geometry; the first classic
  restore uses `863 x 266` rather than inheriting the integrated or rolling
  size.
- Existing user-resized classic geometry is not overwritten by a default-value
  migration.
- The classic waveform always clips inside its container and refreshes on
  source, cache generation, size, DPR, theme, and shell changes.

`TrackList.qml` exposes one `layoutProfile` value. Trailing columns use stable
widths while the song presentation is elastic:

- classic: song, duration, rating, favorite, BPM;
- integrated and rolling: cover, title plus artist/album, waveform thumbnail,
  duration, rating, favorite;
- tag management follows the profile of its host and shares the same column
  functions.

At wider sizes only the song/waveform region grows. At narrower supported sizes
all required columns remain present, text elides safely, and hit targets do not
overlap. Row content is vertically centered to the cover.

## Navigation, drag/drop, and player actions

Theme tokens define one navigation icon visual size and one navigation action
extent. Library, favorites, tags, resource root, and resource folder icons use
the same visual box in every shell.

Playlist drag targets use a translucent selected/hover background and preserve
readable text. External file and directory drops pass through the C++ URL
classifier and one shared submission path. A directory dropped on the resource
area is registered and imported/refreshed transactionally; audio files dropped
on a list are imported; internal tracks dropped on playlists add or move tracks;
internal tracks dropped on resource folders never move disk files.

Classic player actions are arranged as:

- left: playlist;
- center: audio tools, EQ, waveform mode, previous, play/pause, next, playback
  mode, lyrics, volume;
- right: theme, immersive, mini player.

The play control and all secondary controls share one vertical center and keep
safe top/bottom margins. Mini adds a theme chooser immediately before waveform
mode and keeps its waveform fully clipped. Integrated restores playlist and
lyrics actions. Rolling uses the requested transport and utility order while
retaining shared handlers.

Every theme chooser is anchored above the invoking icon using mapped window
coordinates and available-screen clamping.

## Shared waveform and settings policy

`SharedWaveformView` owns played/unplayed spectral opacity, continuous pixel
clipping, palette bindings, and position/duration mapping. Shells only provide
geometry. Dark, light, and system themes use the same contrast ratio. Updating
progress changes color/clip data without re-analyzing audio or rebuilding peak
geometry.

The rolling detailed waveform shows a normal-density visible time range around
the play position. It does not stretch a low-resolution full-song texture.
Overview hover, time capsule, and guide line use the same duration coordinate
mapper.

The compact application color picker is shared by normal color fields and all
eight spectral slots. It is screen-clamped and keeps preview, HSV editing,
confirm, cancel, keyboard focus, and persistence. Palette labels are ordered:
`最低频`, `低频`, `低中频`, `中频`, `中高频`, `高频`, `更高频`, `最高频`,
with uniform visual gaps between label/color groups.

## Information and lyrics

The shared audio information shell targets `130 x 438` logical pixels. Its cover
and metadata scale to the available portrait width; fields scroll vertically,
long paths elide, and the full value remains copyable. All entry points reuse the
same shell and row component.

Lyrics state lives in one service state machine. Local embedded/file/cache
sources remain first, followed by at least three free network routes. Each route
records success, no-match, timeout, TLS, rate-limit, server, and parse outcomes.
Technical failure advances automatically and is shown to the user. The close
button hides the current host without destroying lyrics state or affecting
playback. Live provider endpoints and license/attribution constraints must be
revalidated during implementation.

## Audio tools and equalizer

Audio editor, vocal separation, format conversion, metadata editing, and file
processing share one left-aligned navigation row. Audio tools and EQ share
window-chrome tokens for title height, draggable area, and equal-sized minimize,
maximize, and close controls.

The equalizer retains 18 bands and its DSP/controller contract. Default layout
uses readable band widths; a smaller window scrolls horizontally rather than
compressing nineteen columns. Frequency/value typography and thumb geometry are
uniform, with keyboard, wheel, double-click reset, preset, and enable behavior
unchanged.

## Audio editor

- Remove the left track-gain slider and label; keep mute and solo controls.
- Remove the two-handle timeline range bar; retain Ctrl+wheel zoom and
  Shift+wheel pan.
- Reclaim the removed width/height for the timeline and move remaining controls
  closer with balanced gaps.
- Reduce and vertically center transport and shortcut content.
- Display `Ctrl+右键 = 选择片段 / 双击右键取消` and keep the separate envelope
  shortcut truthful.
- Successful crop-to-selection fits the new document from frame zero through
  its new total frame count. Other edits preserve the user's viewport.
- Expose distinct full-document and selection export paths. Selection export is
  enabled only for a valid selection, and failures never show a success state.

## Separation and model discovery

Download cards reserve a single progress row: an elastic progress bar followed
by fixed percentage and route text. Official failure advances to the domestic
mirror. When all automatic routes fail, one structured event opens the backup
URL dialog; cancellation never opens it.

A shared model registry combines built-in catalog descriptors, known hashes,
sidecar manifests, and inspected compatible ONNX files. Entering the page and
pressing detect both rescan the configured directory. Unknown or incompatible
files stay untouched and display a precise rejection reason. Models are not
executed until worker-side tensor/profile validation succeeds.

Mix and solo preview share one playback position. Mix draws the same guide on
every available result track; solo still changes audio routing without hiding
the common time position. The primary separation action becomes visually
shorter and semibold while preserving start/cancel/disabled behavior.

## Metadata write safety

The staged write, requested-field readback, `.agbak`, atomic replacement, and
rollback pipeline remains mandatory. Audio equivalence is classified so legal
container normalization of time base, timestamps, or packet boundaries does not
create a false failure when codec parameters and audio payload or decoded audio
remain equivalent.

Force mode means a sanitized remux/readback fallback that accepts verified
timing normalization. It never accepts unreadable staging, requested-tag
mismatch, source mutation, decoded-audio mismatch, backup failure, or atomic
replacement failure. Every rejected case leaves the original intact.

## Validation

Implementation follows test-first repair and requires:

- focused C++ and QML regression tests for each requirement;
- Release build, QML lint, static checks, and complete CTest;
- dark/light/system screenshots at reference and minimum sizes;
- real Windows drag/drop and theme-popup interaction;
- real waveform playback/seek, lyrics failover, editor crop/export, model scan,
  mirror failover, mix/solo preview, and metadata writes on copied fixtures;
- final diff review for theme drift, duplicated handlers, dead code, lifecycle
  errors, unbounded work, and unrelated changes.

Packaging is outside this repair unless the user separately requests it after
the acceptance gate passes.
