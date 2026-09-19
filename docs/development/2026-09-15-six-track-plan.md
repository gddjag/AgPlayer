# PC six-track editor — approved implementation contract

Goal: replace single-track UI with real fixed six-track, non-destructive editor on Windows/macOS. No packaging, publication, unrelated feature changes, or changes to original media. Base d29bb41f; isolated codex/pc-six-track-editor.

User specification: `AgPlayer-PC六轨音频编辑复刻说明.md` alongside this file. Approved screenshot: `C:/Users/Administrator/Desktop/音视频播放器/AgPlayer音频播放器完整版/音频编辑.png` (1672x941).

## Binding decisions from approved plan
- Screenshot controls/layout override document toolbar arrangement. Top: add audio, save project, undo, split, denoise, delete, clear. Bottom: input selector, record/pause, transport, time. Inspector A speed/BPM B pitch C keepPitch/formant D export.
- Preserve existing Qt/QML+Core architecture, miniaudio, native waveform, decoder, TimePitch (Signalsmith first). No QtMultimedia/new runtime/models.
- Six fixed tracks (indices 0..5), stable identity, colors #BD91FF/#5CDDE2/#FF9EBC/#F7CF73/#78D7A3/#8FACFF, muted and gain 0..2 (default1). Each AudioEvent owns trackIndex, source range, timeline position, gain/envelope/fades.
- Same-track overlap invalid; cross-track overlap valid. Duration=max(all ends). Atomic cross-track move; undo/redo includes tracks. Source/project frames are distinct, int64; source-to-project rational mapping. First valid source fixes project sample rate; record-first48000; new project stereo. Existing project formats preserved.
- 4096 source/events limits, 128 envelope points per clip, 65536 total. Preserve PC history budget, no new60min hard limit.
- One streaming mixer: per-source read/resample -> clip gain/envelope/fade -> track mute/gain -> sum -> session TimePitch -> finite/clamped output. No divide by track count, no silent normalization/compressor. Preview, export, handoff share algorithm. Remove old post-TimePitch automation application.
- Up to six active decoders, bounded buffers, worker-owned decoding; no I/O or allocation in realtime callback. Latest seek/revision wins. Preserve playback ownership handoff.
- Batch imports freeze cursor; one file per distinct track, empty tracks first then collision-free; individual errors, continue successes, cancel preserves successes. Audio/video formats at least old PC supported set.
- Clip upper24px moves, body creates global temporal selection; envelope controls higher priority. Native selection drag exports actual mixed WAV with effects. Clip delete/trim/copy/paste affect selected clip/track, not unrelated tracks. Snap8logicalpx; nearest legal gap, tie earlier. Esc restores gesture; one undo per gesture.
- Waveform uses real source peak pyramid/details; per-source cache reused. Shared viewport and timeline; detail debounce250ms. Playhead does not redraw waveform. Fixed readable row height, scroll when needed.
- schema3; load1/2 to track1. Legacy gain beyond2 retained internally, never clamp. Preserve source relink and settings. Recording/denoise generated media delivered into project .media on save/as atomically; cleanup only session-owned unreferenced files.
- Add miniaudio recording backend (currently absent): devices/actual route, start/pause/resume/stop, frame-based time, bounded capture ring + writer. Start freezes track+cursor+revision, pauses playback, only empty or afterlastclip. Paused time not written. Stop validates PCM WAV and inserts one undo command. Error/cancel/close never pretend success. No overdub/input monitoring/fake live waves.
- macOS QMicrophonePermission + Info.plist NSMicrophoneUsageDescription + deployed permission plugin; Windows privacy errors handled.
- Denoise selected clip or intersection with temporal selection, preserve other tracks/automation and apply gains once. Clear is undoable and retains six track shells/sessionTimePitch.
- Keep right parameter ranges/defaults/reset/BPM mapping/export options. Full export reaches whole timeline end and honors speed. Atomic output and source-path overwrite guards cover all sources.
- Pixels: screenshot static geometry <=2logicalpx at1672x941 goal, inspector~360/header~220. No hard-coded content fakewave. Small screen compact controls, readable rowheight verticalscroll, dark/light theme and mac native chrome/Command preserved. No internal logo change.

## Tasks / ownership
1. Model+persistence: core audio_event/event_timeline/audio_document/timeline command/undo plus qt project_document; tests for collision/history/schema/frame maps.
2. Shared mixer: core document_renderer/editor_playback_stream/document_render_pipeline and new mixer; tests for PCM, seek, longduration, automation order.
3. Recording: new editor input backend, permissions and packaging permission dependency; standalone tests; controller adapter after interface agreement.
4. Controller+UI integration: tracks/imports/selection/wavecache/denoise/media lifecycle, six-row QML, screenshots and native drag. Parent coordinates.
5. Verification: relevant baseline tests, TDD local units, integrated build/controller/QML/core suites, release checks only if later authorized. Real mac/device listening explicitly unverified if inaccessible.

No automatic merge into dirty HarmonyOS worktree. Preserve it throughout.
