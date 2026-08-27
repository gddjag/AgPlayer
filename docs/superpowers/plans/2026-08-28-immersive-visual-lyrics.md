# AgPlayer Immersive Visual, Lyrics, and Unified Experience Plan

Spec sources:
- `C:/Users/Administrator/Desktop/AgPlayer_沉浸视觉模式_歌词_主题统一_正式开发提示词.md`
- `E:/Administrator/下载/AgPlayer_声音地形反应堆.html`
- `C:/Users/ADMINI~1/AppData/Local/Temp/codex-clipboard-8c11a048-a86f-4bad-b9b7-c3b90473e894.png`

Base commit: `74232a6` (`codex/integrated-single-window-theme`).

## Global Constraints

- Qt/QML/C++ only. No WebEngine, Electron, Three.js, second player, second decoder, second FFT, or new third-party runtime.
- Reuse `PlaybackController.spectrum`, `queueTrackIds`, `playTrackIds`, `WaveformItem`, `WaveformSession`, and the existing waveform cache and seek behavior.
- The immersive bottom waveform is the original transparent player waveform; only layout, opacity, and z-order may change.
- Theme layout, immersive mode, and lyrics visibility are independent. Theme switching must not stop playback or recreate the player core.
- One renderer/resource set may be active at a time. Off/hidden/minimized means no render frames, derived spectrum work, or GPU uploads.
- Online lyrics v1 uses LRCLIB only. Do not fake a second provider or provider switching.
- Use tests first and record RED/GREEN evidence. Do not package an installer.
- The screenshot is the visual target; the HTML is interaction/parameter reference only. Do not carry its file picker or demo player into production.

## Task 1: Unified experience state and audio visual feature derivation

Implement a focused `PlayerExperienceController` singleton and a lightweight `AudioVisualFeatureController`.

- Persist independent immersive mode, host mode, lyrics visibility, panel state, desktop mouse passthrough, quality preset, colors, effect toggles, numeric parameters, and eight visual EQ gains using the existing QSettings pattern.
- Keep layout theme delegated to `SettingsController.playerShellMode`; expose a thin toggle only, never duplicate the setting.
- `AudioVisualFeatureController` subscribes to the existing 128-bin `PlaybackController.spectrum` only while immersive rendering is active and derives eight bands, energy, spectral flux, kick pulse, and snare pulse. It must not decode audio or calculate another FFT.
- Expose internal counters for tests: derived update count and active state. Counters must stop changing while inactive.
- Register the new singleton instances with the existing QML registration pattern.
- Add C++ unit tests for defaults, persistence, invalid-value fallback, independent state, theme toggling without playback commands, band derivation, event thresholds, and inactive zero-work behavior.

## Task 2: Local lyrics, cache, synchronization, and LRCLIB

Implement `LyricsService` with a QAbstractListModel-backed timed-line model and focused helpers.

- Resolution order: current embedded lyrics, same-basename `.lrc`, valid per-track cache, LRCLIB exact request, LRCLIB search, then manual import.
- Parse multiple timestamps, centiseconds/milliseconds, metadata tags, `[offset:]`, BOM, CRLF, untimed text, duplicate timestamps, and malformed lines without inventing timing.
- Cache one atomic JSON file per requested track. Key inputs: normalized source path, size, modified time, and duration; filenames use a SHA-256 of that key, not an audio hash.
- Expose Idle, Loading, Ready, NotFound, Offline, and Error; current/previous/next line; offset; retry; manual import; sanitized diagnostics.
- Use one asynchronous QNetworkAccessManager. Exact LRCLIB request precedes search; cancel stale replies on track change; at most current plus next prefetch while lyrics are enabled. Technical failures open a 20-minute degraded window after the configured consecutive threshold; NotFound is not a failure.
- Never log full local paths or query URLs.
- Add deterministic parser/cache/model tests and fake-network tests for exact, search fallback, cancellation, timeout/error, NotFound, and degraded service behavior.

## Task 3: Native Terrain Reactor renderer

Implement `TerrainReactorItem : QQuickRhiItem` and its render-thread-owned renderer.

- Use Qt 6.7 QRhi with build-time baked shaders and GPU instancing for the cube grid. The GUI thread supplies an immutable frame snapshot in `synchronize()`; renderer resources live only on the scene-graph render thread.
- Recreate the reference composition: black stage, bright cyan/white central terrain, coral/pink/cyan zoning, concentric ripple rings, sparse bright floating cubes, meteors, depth fog, and camera perspective.
- Drive terrain, ripple, particle, meteor, and camera punch parameters from `AudioVisualFeatureController`; expose deterministic seed and synthetic spectrum inputs for tests/visual QA.
- Support orbit, zoom, automatic camera recovery after four seconds, and panel idle behavior without adding a second high-frequency application timer.
- Implement quality tiers and hysteresis: downgrade after about two seconds over budget, upgrade after eight seconds under budget, five-second cooldown; reduce particles/meteors/ripples before grid/internal resolution. Desktop default is Eco/30 FPS.
- Release all pipelines, buffers, textures, and targets on scene graph invalidation/device loss. Off/hidden/minimized must stop frame scheduling and uploads.
- Add focused renderer/state tests that can run without comparing nondeterministic live-audio frames, including single-instance ownership, quality hysteresis, deterministic snapshot generation, counter suspension, and resource lifecycle hooks.

## Task 4: QML integration, immersive hosts, queue drawer, lyrics UI, and original waveform

Build the user-facing experience around the completed controllers and renderer.

- Add one shared three-action component for layout theme, immersive visual, and lyrics; reuse it from shared player controls and mini player controls without duplicating state logic.
- Windowed mode renders inside the active shell; fullscreen uses the same item and hides the panel after 2.6 seconds idle; desktop uses a separate transparent frameless window and transfers the sole renderer host. Windows/macOS use supported transparency/layering; unsupported Wayland safely falls back with a visible status message. Mouse passthrough is explicit and defaults off.
- Match the screenshot's compact dark translucent left control panel. Include quality, palette/custom colors, visual parameters, effect toggles, and eight EQ controls. Do not include file selection or demo music.
- Add the right queue drawer: 20px trigger, 140ms hover delay, two-second hide, drag/orbit guard, virtualized ListView from `queueTrackIds`, metadata via `trackForId`, full-row double click via `playTrackIds(currentQueue, trackId)`, and transform-only neighbor scaling.
- Reuse the existing `WaveformSession` and `WaveformItem` at the bottom. Do not add waveform decoding, cache, provider, model, shader, or alternate waveform styling. Ensure only the visible host owns the visual item while sharing the session.
- Add a transparent three-line normal-window lyrics panel using `LyricsService`; fullscreen keeps state but has no lyrics stage. Manual scrolling pauses follow for five seconds.
- Add QML contract/integration tests for shared actions, independent state, one host, lifecycle, drawer timers and queue scope, lyrics states, and exact reuse of the existing waveform types.
- Update `docs/development/` traceability and `docs/qa/` acceptance records.

## Task 5: Integration validation and visual QA

- Run Debug and Release builds, focused tests, full CTest, QML lint, real launch/playback/exit smoke, and `git diff --check`. Distinguish new failures from recorded baseline failures.
- Exercise 50 mode/host toggles, device-loss/recreation where available, hidden/minimized/off zero-work counters, and a 30-minute soak when the environment permits.
- Capture the real program at 2169x1131 with fixed seed and synthetic spectrum. Put source and implementation screenshots into one comparison image, fix all P0/P1/P2 findings, and update project-root `design-qa.md` to `final result: passed` only with visible evidence.
- Check 1080p, 1440p, 4K and 100/125/150/200% Windows scaling where the environment permits. macOS/Linux remain unverified unless actually run.
- Compare Release EXE/QML/runtime delta against base commit; target no new deployment DLL and at most 5 MB growth. Do not build an installer.
