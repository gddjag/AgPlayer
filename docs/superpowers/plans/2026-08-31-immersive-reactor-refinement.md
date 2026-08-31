# AgPlayer Immersive Reactor Clean-room Refinement Plan

## Authority and scope

- User-approved direction on 2026-08-31: stop matching uploaded screenshots, HTML frames, or videos; understand open-source architecture and create an original native visual treatment.
- Open-source repositories are study-only: `yin-yizhen/sonic-topography` (Non-Commercial Learning License) and `ww085213/Mineradio-LX-Music` (GPL-3.0-only). Do not copy or translate their source, shaders, algorithms, constants, parameter tables, or assets.
- Preserve the completed queue, lyrics data/service behavior, and frequency-colored waveform. The existing spatial lyric stage may receive presentation-only cinematic refinement.
- Work only in `D:/ai/AgPlayer/.worktrees/immersive-visual-lyrics`; do not touch the packaging/main worktree and do not build an installer.

## Global constraints

- Qt 6/QML/C++17/QRhi only. No WebEngine, Electron, Three.js, extra decoder, extra FFT, new runtime dependency, or long-lived audio cache.
- Reuse the existing immutable audio-feature snapshot, renderer ownership, quality governor, playback core, waveform session, lyrics model, and queue model.
- Keep one renderer/resource set and one instanced draw path. Hidden/off/minimized must remain zero-work.
- Use TDD for observable behavior. Record RED and GREEN commands/results.
- Visual acceptance is based on internally defined deterministic criteria, not comparison against uploaded media.

## Task 1: Exact player-window restoration

- Add failing C++/QML tests proving that entering and leaving independent immersive mode restores the exact previous player role, shell mode, position, and size.
- Snapshot the visible player state when immersive presentation begins and restore it when it ends without overwriting independent classic/integrated persisted geometry.
- Cover main and mini player restoration and the first-run case where a shell-specific geometry key is missing.

## Task 2: Original soft-luminous terrain renderer

- Add failing state tests for smooth bounded terrain response, distinct continuous/event controls, peak compression, and deterministic scene layout.
- Refine the existing QRhi instanced renderer and baked shaders into an original soft-luminous circular sound field:
  - low frequencies shape central weight and broad breathing;
  - mids create wide coherent ridges;
  - highs drive restrained top-face sheen and sparse detail;
  - sides remain darker and steadier than top faces;
  - distance fog/opacity removes the square-grid boundary;
  - central light, impact waves, meteors, particles, and camera impulses use bounded envelopes and cooldowns.
- Reduce coarse random towers and oversized floating cubes. Preserve the one-pass/one-instance-buffer architecture and existing auto-quality degradation order.
- Rebalance the six presets and existing settings so every exposed parameter produces a distinct, real, bounded visual result.

## Task 3: Settings and cinematic spatial lyrics presentation

- Keep the existing lyrics service, timing, placement, queue, and frequency-colored waveform logic unchanged.
- Refine only the existing spatial lyric stage presentation using original QML transforms: clear focal hierarchy, perspective depth, previous/current/next separation, readable glow, left/center/right safe zones, and damped transitions that do not inherit terrain rotation.
- Group the current visual controls by product meaning (terrain, light, motion, impact) while preserving their persisted properties and ranges unless a tested mapping change requires otherwise.
- Add QML tests for parameter wiring, lyrics display switch, placement, scale/position controls, and unchanged waveform/queue ownership.

## Task 4: Verification and evidence

- Run focused C++/QML tests, shader compilation, Debug and Release application builds, `qmllint` where available, full `ctest`, and `git diff --check`.
- Run deterministic synthetic-spectrum visual smoke checks for circular occupancy, controlled peak ratio, top/side luminance separation, sparse environment density, bounded camera motion, and real parameter feedback. Do not compare with uploaded images or video.
- Launch the actual app for a short usability check if the environment permits; record limitations honestly.
- Update `docs/development/` with requirement traceability and the clean-room source audit. Do not create an installer.
