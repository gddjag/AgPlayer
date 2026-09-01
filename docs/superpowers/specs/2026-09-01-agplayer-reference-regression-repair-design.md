# AgPlayer Reference Regression Repair Design

**Date:** 2026-09-01
**Status:** Approved by autonomous-execution authorization
**Parent design:** `2026-09-01-agplayer-unified-player-tools-quality-design.md`

## Goal

Repair the 19 reference-image regressions reported on 2026-09-01 while keeping
one shared playback/data contract across classic dual-window, integrated
single-window, rolling, and mini shells. The startup shell is removed so launch
always opens the normal player UI. Requirement 8 from the earlier
list (immersive rendering performance/crash work) remains owned by its dedicated
task and is not reimplemented here.

## Chosen architecture

Use shared controllers and explicit presentation profiles:

- playback actions, library data, rating, favorite, waveform data, theme tokens,
  lyrics state, and tool-preview ownership stay shared;
- `TrackList` keeps one shared delegate and exposes explicit classic-versus-
  single-window layout inputs, so integrated and rolling reuse the same row
  contract without copying business behavior;
- each player shell declares action placement/order but delegates action behavior
  to shared controls/controllers;
- theme and shell chooser popups anchor to the invoking icon;
- metadata "force" mode means a verified decoded-audio-equivalence fallback,
  never disabling backups, readback, rollback, or audio-integrity checks;
- custom separation models are detected only after ONNX compatibility probing;
  unknown or incompatible files remain untouched and receive a reason.

A single universal list layout was rejected because the supplied references
explicitly require different information density. Copying complete list/player
implementations per theme was rejected because it would recreate behavior drift.

## Player and list contract

Classic controls are ordered as follows:

- left: playlist;
- center: audio tools, EQ, waveform mode, previous, play/pause, next, playback
  mode, lyrics, volume;
- right: shell/theme chooser, immersive, mini player.

All secondary icons share one visual size and hit target. The transport group is
centered against the whole strip. The classic list uses song, duration, rating,
favorite, and BPM columns. Song/waveform width is reduced enough to create clear
duration-to-rating spacing; row rating stars are smaller than the player-header
stars. Filled and hollow header stars use identical bounds without an added
outline.

The classic window's content divider ends above the filter footer. The tag pane
gets its own bottom divider, and the filter footer spans the content area without
crossing the tag boundary. Visible list dividers use 30% emphasis.

The integrated and rolling profiles lay out cover, title plus artist/album/tags,
waveform thumbnail, duration, rating, and favorite. The thumbnail occupies its
own column, matches the cover height, and is vertically centered. Rolling shows
ten rows, has no list toggle, retains the
right tag/lyrics pane, and uses a dedicated compact control layout rather than
the classic strip. Its strip contains previous, play/pause, next, playback mode,
EQ, audio tools, shell/theme chooser, immersive, mini player, and volume in that
order; it has no redundant playlist, waveform-mode, or lyrics button because
the list and lyrics pane are already present and rolling is spectral-only.

The integrated and rolling player subtitle is produced by one shared formatter
in the order `艺术家 · 专辑 · 标签`. Empty tags are omitted together with their
separator, so an untagged track ends at the album.

## Waveform contract

- solid unplayed/base color is `#9098A6`;
- spectral progress contrast is background-aware and equally legible in every
  dark shell without degrading the already-legible light shell;
- full waveform geometry invalidates on source generation, size, visible range,
  and device-pixel-ratio changes so the right half cannot remain stale;
- rolling uses the frequency-color waveform only, normal peak density within a
  moving viewport, coalesced frame updates, an overview hover line/time capsule,
  and explicit zoom/reset controls;
- the settings palette shows eight Chinese band labels with color swatches and
  spacing, not hexadecimal text;
- the compact color picker is application-styled and bounded for HiDPI/minimum
  screens.

## Lyrics, dialogs, and shared visual surfaces

Lyrics keep the existing embedded/sidecar/cache-first flow and a three-route
free provider chain. Online failures advance automatically and report the failed
route. The lyrics window has an explicit close button. No paid API, fabricated
result, or undocumented scraping endpoint is introduced.

The file-information view is a compact portrait surface just wider than its
cover. EQ uses smaller system-like typography, reduced radii, and a smaller
window while preserving all 18 bands. All audio-tool tabs use one left-aligned
shared navigation style.

## Audio editor contract

Upward volume movement increases gain and downward movement decreases it.
Waveform zoom chrome is 50% opacity while its hit target remains accessible.
The lower transport and shortcut legend fit and vertically center within their
containers with even gaps.

## Separation contract

Model cards clip/wrap route text within their bounds. Domestic mirror is a direct
download action; only an actual unavailable route opens the fallback-address
surface. The configured model root is scanned on page entry and on `检测`.
Compatible user ONNX files become selectable custom models; incompatible files
remain listed with a rejection reason. Input/result waveforms are restored.
Stem export writes directly to the configured output directory and treats an
already-present output as success rather than opening a folder chooser.

## Metadata integrity contract

The writer first performs the current strict packet/readback validation. If a
container legally normalizes packet framing, it may retry/accept only when
decoded PCM fingerprint, codec/sample-rate/channel layout, duration tolerance,
requested tag readback, backup, and atomic replacement all pass. Any failed
audio-equivalence check preserves the original unchanged.

## Asset handling

`我的音乐库图标.svg` is imported as a static Qt-compatible SVG. Browser CSS
animation from the supplied file is intentionally removed because Qt SVG does
not reliably implement CSS keyframes and the navigation icon is a static state.

## Acceptance matrix

Each numbered request maps to at least one focused automated check and one visual
or runtime check where applicable. Final acceptance requires Release build,
QML/static checks, complete CTest, real launch, representative audio playback,
metadata write/readback on copies, separation model scan/export checks, visual
comparison in dark/light shells, final diff review, a clean worktree, and the
repository's official Windows package flow.
