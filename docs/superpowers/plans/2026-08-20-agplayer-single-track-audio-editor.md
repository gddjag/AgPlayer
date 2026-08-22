# AgPlayer Single-Track Audio Editor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to execute this plan task-by-task with one implementer and one reviewer per phase.

**Goal:** Rebuild “音频编辑” as the 1672×941, sample-accurate, non-destructive single-track editor defined by the approved 2026-08-20 specification and supplied reference image.

**Architecture:** A metadata-only `AudioEvent` timeline is the editing truth. A bounded delta-command stack owns undo/redo. One frame↔pixel mapper feeds selection, hit-testing, viewport, playhead and SceneGraph waveform. Preview plugs an event source into the existing player/output chain; export and drag-out share one bounded offline renderer.

**Tech Stack:** C++17, Qt 6/QML/SceneGraph, CMake/Ninja, FFmpeg libav*, existing SoundTouch, Qt Test/Quick Test, PowerShell QA.

**Spec:** `docs/superpowers/specs/2026-08-20-agplayer-single-track-audio-editor-design.md`

## Non-Negotiable Gates

- Execute all 15 phases below strictly in order: RED → minimal GREEN → focused tests → Debug/Release build → run/smoke → review → evidence → isolated commit.
- Work only in `D:/ai/AgPlayer/.worktrees/audio-editor-20260820` on `codex/audio-editor-20260820`; preserve unrelated main-checkout changes.
- Keep tools ordered `音频编辑 / 格式转换 / 元数据修改 / 文件名处理`.
- Use signed 64-bit sample frames in the domain; deletion preserves gaps; events share immutable sources and never own PCM; undo never stores a full timeline snapshot.
- No second audio engine, Canvas waveform, heavyweight dependency, placeholder values, fake meter, or whole-song preview render.
- Match the supplied 1672x941 reference completely: all 13 toolbar commands and every A-E inspector control remain present and produce real results. Formant and noise reduction require tested processing paths; neither may be hidden, stubbed, or permanently disabled.
- Append each phase’s requirement IDs, changed files, RED/GREEN outputs, build config, run evidence, failures, unverified items and rollback commit to `docs/development/2026-08-20-single-track-audio-editor.md`.

## Baseline

Run both presets before Phase 1 and record proven pre-existing failures in the SDD ledger:

```powershell
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release --parallel 4
ctest --preset windows-msvc-release --output-on-failure
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug --parallel 4
ctest --preset windows-msvc-debug --output-on-failure
```

## Task 1 — Phase 1 — AudioEvent Data Model

**Files:** add `core/src/audio_editor/audio_event.hpp`, `tests/core/audio_event_test.cpp`; modify `edit_command.hpp`, `core/CMakeLists.txt`, `tests/CMakeLists.txt`.

Freeze `SampleFrame`, `EventId`, `AudioSource`, `EnvelopePoint`, `FadeCurve`, and:

```cpp
struct AudioEvent final {
    EventId id{};
    std::shared_ptr<const AudioSource> source;
    SampleFrame sourceStart{}, sourceEnd{}, timelineStart{};
    float gain{1.0F}; SampleFrame fadeIn{}, fadeOut{};
    double speedRatio{1.0}; int pitchSemitone{}; bool mute{};
    std::vector<EnvelopePoint> envelope;
};
```

**RED:** invalid source bounds/negative timeline; shared immutable source without PCM ownership; invalid fades/envelope; 2-hour `int64` scale.

```cpp
EXPECT_EQ(left.source.get(), right.source.get());
EXPECT_FALSE(isValid(AudioEvent{.sourceStart = 9, .sourceEnd = 8}));
```

Run `ctest --test-dir build/release -R "^audio_event_test$" --output-on-failure`; expected failure is missing `AudioEvent`. GREEN adds validation and `audibleFrames()` only. Commit `feat(editor): add metadata-only audio events`.

## Task 2 — Phase 2 — Single-Track Timeline and Time Mapping

**Files:** add `event_timeline.hpp/.cpp`, `event_timeline_test.cpp`; modify `audio_document.*`, renderer/writer/time-pitch signatures, `time_pixel_mapper.hpp`, CMake/tests.

```cpp
class EventTimeline final {
public:
    bool insert(AudioEvent); const AudioEvent* event(EventId) const noexcept;
    std::vector<EventId> eventsAt(SampleFrame) const;
    SampleFrame totalFrames() const noexcept;
    TimelineSnapshot snapshot() const; std::uint64_t revision() const noexcept;
};
```

**RED:** gaps count in total duration; half-open hit testing; overlap rejects transactionally; 44.1/48/96 kHz frame↔pixel roundtrip ≤1 frame; long-file endpoints stay precise.

```cpp
EXPECT_EQ(timeline.totalFrames(), later.timelineStart + audibleFrames(later));
EXPECT_TRUE(timeline.eventsAt(gapFrame).empty());
```

Migrate downstream snapshot inputs only enough to keep single-event behavior compiling. Run `audio_event_test|event_timeline_test|time_pixel_mapper_test|audio_document_test`. Commit `feat(editor): add gap-preserving event timeline`.

## Task 3 — Phase 3 — Move and Trim

**Files:** add `timeline_edit_command.hpp/.cpp`, `timeline_edit_command_test.cpp`; modify timeline, document, controller and CMake.

```cpp
bool EventTimeline::moveEvent(EventId, SampleFrame timelineStart);
bool EventTimeline::trimEvent(EventId, SampleFrame sourceStart,
                              SampleFrame sourceEnd, SampleFrame timelineStart);
```

**RED:** move changes only timeline start; trim changes only source bounds/anchor; collision rejection preserves state/revision; no decode, encode or source-peak rebuild.

```cpp
EXPECT_EQ(after.source, before.source);
EXPECT_EQ(peakBuildCount, 0);
```

GREEN uses tiny before/after event commands and controller seams. Run `timeline_edit_command_test|audio_editor_controller_test`, both builds and QA launch. Commit `feat(editor): move and trim audio events`.

## Task 4 — Phase 4 — Split, Delete, Copy, Cut, Paste, Merge

**Files:** extend commands/timeline/document/controller/action model/renderer compatibility and focused tests.

**RED:** split shares source; delete leaves later starts unchanged; clipboard clones metadata only; paste at playhead rejects collision or uses one deterministic nearest-gap policy; merge requires same source, adjacent source/timeline ranges and matching parameters.

```cpp
timeline.deleteEvents({first.id});
EXPECT_EQ(timeline.event(second.id)->timelineStart, originalSecondStart);
EXPECT_EQ(splitLeft.source.get(), splitRight.source.get());
```

GREEN removes Ripple logic, `AudioSpan` and `DocumentSnapshot` only after all callers compile on `TimelineSnapshot`. Run `timeline_edit_command_test|audio_document_test|document_writer_test|audio_editor_controller_test`. Commit `feat(editor): add non-ripple event editing`.

## Task 5 — Phase 5 — Delta Undo/Redo and Lightweight `.agproj`

**Files:** add `timeline_undo_stack.*`, `project_document.*` and tests; modify document/commands/controller/CMake.

```cpp
class TimelineUndoStack final {
public:
    bool executeAndPush(std::unique_ptr<TimelineEditCommand>, EventTimeline&);
    bool undo(EventTimeline&); bool redo(EventTimeline&);
    void beginCoalescedEdit(EventId);
};
```

**RED:** affected-event deltas only; 100 drag previews coalesce once; count/byte limits evict oldest; 100 undo/redo cycles exact; failed command does not mutate stacks; `.agproj` round-trips sources/events/selection/playhead/viewport with relative UTF-8 paths and no PCM/peaks/render cache.

Use `QSaveFile`, schema versioning and explicit offline-source reporting. Run `timeline_undo_stack_test|project_document_test|audio_editor_controller_test`; inspect saved JSON. Commit `feat(editor): add bounded undo and agproj persistence`.

## Task 6 — Phase 6 — Selection, Zoom, Pan, Visible Waveform and Reference Shell

**Files:** modify `editor_viewport.*`, waveform item, controller, `AudioToolsWindow.qml`, `AudioEditorPage.qml`, `ToolSidebar.qml`, audioeditor QML, tests and layout contract; delete `OverviewNavigator.qml` after replacement.

**RED:** sample-precise selection and edge adjustment; Ctrl+wheel anchored zoom; Shift+wheel/middle-drag pan; visible peak points bounded by pixel width; gaps blank; one mapping drives ticks/hit/selection/playhead; geometry matches 1672×941; all 13 tools ordered; Formant and noise reduction present with real handlers.

```cpp
EXPECT_EQ(controller.selectionFrames(), selection.end - selection.start);
EXPECT_LE(item.generatedPointCount(), viewportPixelWidth * 2);
```

GREEN reuses `PeakPyramid` and one SceneGraph buffer. Match the screenshot: title 49, tabs 43, main x=0..1299, inspector x=1300..1671, deep navy, blue primary, orange selection, green play ring; bind real values; responsive 1280×720/880×560. Run viewport/waveform/QML tests and layout contract with explicit build dir; capture and compare. Commit `feat(editor): build precise reference timeline ui`.

The desktop geometry contract (±2 px unless an exact shell boundary) is:

| objectName | global geometry |
| --- | --- |
| `audioToolsTitleBar` | `0,0,1672,49` |
| `audioToolsTopNav` | `0,49,1672,43` |
| `audioToolsContentStack` | `0,92,1672,849` |
| `editorMainColumn` | `0,92,1300,849` |
| `editorInspector` | `1300,92,372,849` |
| `editorCommandBar` | `12,105,1278,61` |
| `fileSummaryBar` | `12,180,1278,48` |
| `editorTimelineWorkspace` | `12,244,1276,364` |
| `editorTrackHeader` | `12,294,96,284` |
| `editorTimeRuler` | `118,244,1167,50` |
| `editorWaveformCanvas` | `118,294,1167,284` |
| `editorTimelineScrollbar` | `118,592,1167,16` |
| `editorRecordingTransport` | `12,619,556,130` |
| `editorPlaybackTransport` | `580,619,708,130` |
| `editorShortcutCard` | `12,759,1276,157` |
| `editorStatusBar` | `0,916,1300,25` |

The inspector owns A `inspectorRecordingGroup`, B `inspectorTempoGroup`, C `inspectorPitchGroup`, D `inspectorPreservePitchGroup`, and E `inspectorExportGroup`. `formantPreservationRow` is not instantiated unless `formantPreservationSupported == true`; the old `vocal_protection` flag is not proof.

## Task 7 — Phase 7 — Gain and Volume Envelope

**Files:** modify event/commands/timeline/controller/waveform/QML/tests.

**RED:** 0 dB=unity; invalid values reject; envelope sorted/in-event/capped; preview throttled; release pushes one undo; no source/timeline movement.

```cpp
EXPECT_FLOAT_EQ(dbToLinear(0.0F), 1.0F);
EXPECT_EQ(undoStack.size(), 1U);
```

GREEN draws 0 dB/envelope in the existing SceneGraph item and publishes preview parameters atomically. Run command/controller/QML tests and audible fixture. Commit `feat(editor): add event gain envelope`.

## Task 8 — Phase 8 — Fade In and Fade Out

**Files:** extend command/timeline/controller/waveform/QML/tests.

```cpp
enum class FadeCurve { Linear, Smooth, Exponential };
bool EventTimeline::setFade(EventId, FadeSide, SampleFrame, FadeCurve);
```

**RED:** Smooth default; handles clamp to event length; fade-in+out ≤ audible frames; three curves undo/redo exactly; no complex overlap/crossfade. GREEN adds only event-edge handles and compact metadata commands. Run command/controller/QML tests, build/run/capture. Commit `feat(editor): add bounded event fades`.

## Task 9 — Phase 9 — Fixed-Buffer Recording

**Files:** modify `recording_session.*`, `pcm_ring_buffer.*`, controller/QML/tests.

**RED:** fixed capacity under long input; callback performs no allocation/I/O; backpressure/dropped frames truthful; cancel leaves no output/event; stop atomically finalizes one WAV and inserts one event at playhead; unavailable backend explicit.

```cpp
EXPECT_EQ(session.ringCapacityFrames(), initialCapacity);
EXPECT_EQ(timeline.event(recordedId)->timelineStart, playhead);
```

Keep existing ring/writer/WASAPI direction, create lazily, join on deactivation, and bind real device/meter/monitor/format state. Run unit plus opt-in microphone smoke. Commit `feat(editor): record into bounded timeline events`.

## Task 10 — Phase 10 — Speed and BPM

**Files:** modify BPM analyzer/session, event command/timeline, controller/QML/tests.

**RED:** BPM streams frames without render file, starts manually, cancels on deactivation; ratio=`targetBpm/sourceBpm`, clamp 0.5–2.0; drag commits once; no beat grid/snap.

```cpp
EXPECT_FALSE(std::filesystem::exists(temp / "bpm-analysis.wav"));
EXPECT_DOUBLE_EQ(speedRatio, targetBpm / sourceBpm);
```

GREEN runs one low-priority cancellable task and atomic speed preview; delete beat-grid/snap code/tests. Run BPM/time-pitch/controller/QML tests plus audible preview. Commit `feat(editor): add manual bpm speed control`.

## Task 11 — Phase 11 — Pitch Shift

**Files:** modify event command/timeline, controller/QML/time-pitch tests.

**RED:** -12..+12 only; pitch and speed metadata independent; drag one command; old render-and-replace-source never called.

```cpp
EXPECT_EQ(after.speedRatio, before.speedRatio);
EXPECT_FALSE(controller.commitPitch(id, 13));
```

GREEN stores semitone metadata and updates the live adapter only; Formant stays independently controllable and is validated through the shared processing path. Commit `feat(editor): add independent pitch shift`.

## Task 12 — Phase 12 — Tempo Preserve Pitch and Shared Player Preview

**Files:** add `audio_decode_source.hpp`, `editor_playback_adapter.*`, `event_pcm_processor.*`; modify engine/context/private bridge/playback controller/editor controller/main/CMake/tests; delete obsolete preview members after migration.

```cpp
class IAudioDecodeSource {
public:
    virtual ~IAudioDecodeSource() = default;
    virtual ag_result seek(SampleFrame) noexcept = 0;
    virtual ag_result read(DecodedAudioBlock&) noexcept = 0;
};
```

**RED:** editor uses existing output owner; library stops before editor; no second `ag_player`; no temp preview WAV; gaps emit silence; seek resets decoder/SoundTouch at boundaries; preserve-pitch defaults on; callback never reads disk/allocates.

GREEN performs decode/DSP on the existing decode thread, keeps miniaudio callback unchanged, atomically swaps immutable snapshots, and removes editor player/temp-dir/render watcher/poll timer. Run engine/playback/controller/time-pitch tests and real device seek/stop. Commit `refactor(editor): preview through shared player output`.

## Task 13 — Phase 13 — One Export Renderer

**Files:** migrate `document_renderer.*` to `export_renderer.*` (one path only); modify writer/shared processor/controller/QML/CMake/tests.

**RED:** multi-event gaps; trim/mute/gain/envelope/fade/speed/pitch order; half-open selection; mixed rate/channel resampling; bounded blocks; progress/cancel; target unchanged on error; validate then atomic commit; handoff/formal export same PCM contract.

```cpp
EXPECT_EQ(renderedGap, std::vector<float>(expectedSilentSamples, 0.0F));
EXPECT_EQ(targetBytesAfterCancel, targetBytesBefore);
```

GREEN reuses FFmpeg/SoundTouch with one renderer/concurrency slot. Probe existing WAV/FLAC/MP3/AAC support. Commit `feat(editor): unify bounded event export`.

## Task 14 — Phase 14 — Native Drag-Out Handoff

**Files:** add `handoff_asset_manager.*`, `selection_drag_controller.*` and tests; modify Qt CMake/controller/QML seam.

**RED:** below system drag distance creates no file; crossing renders one selection WAV; MIME has finalized local URL and `text/uri-list`; key includes ranges/revision/parameters; stale revision never reuses; cancel publishes nothing.

```cpp
EXPECT_TRUE(mime.hasUrls());
EXPECT_TRUE(QFileInfo::exists(mime.urls().front().toLocalFile()));
```

GREEN renders lazily through Phase 13 into `QStandardPaths::AppDataLocation/AudioEditor/Handoff`, caches completed files only with bounded cleanup, then `QDrag::exec(Qt::CopyAction)`. Verify Explorer and one URI-capable audio/NLE app. Commit `feat(editor): drag rendered selections to native apps`.

## Task 15 — Phase 15 — Performance, Cleanup, Visual and Acceptance Matrix

**Files:** add performance test/measurement script; update QA build paths and 1672×941; remove dead files/build entries; complete `design-qa.md` and development evidence.

**RED contracts:** closed/idle editor has no background work; deactivation stops BPM/recording/editor ownership; real-time read allocation/filesystem counts zero; two-hour seek/render bounded; export does not block null-backend playback; no new runtime dependency; obsolete tokens absent: `AudioSpan`, `DocumentSnapshot`, `preparePlayback`, editor-owned `ag_player`, beat snap, `OverviewNavigator`, Canvas waveform and placeholders. Noise reduction and Formant remain only through their tested shared processing stages.

```powershell
cmake --build --preset windows-msvc-release --parallel 4
ctest --preset windows-msvc-release --output-on-failure
cmake --build --preset windows-msvc-debug --parallel 4
ctest --preset windows-msvc-debug --output-on-failure
powershell -ExecutionPolicy Bypass -File tests/scripts/audio_tools_layout_contract_test.ps1 -BuildDirectory build/release
powershell -ExecutionPolicy Bypass -File scripts/qa-audio-tools-matrix.ps1 -BuildDirectory build/release -Sizes 1672x941,1280x720,880x560
```

Real matrix: WAV/FLAC/MP3/AAC import; edits/shortcuts; gap playback; zoom/pan; gain/fade/mute; BPM/speed/pitch/preserve-pitch; `.agproj`; recording; export; cancel/error; input/output hardware; Explorer + external-app drag; close/idle/play/export CPU/memory; 1–2 hour file; startup, track switch, package size/dependencies.

Product Design QA: capture reference and implementation at 1672×941 plus two responsive sizes; create one combined comparison and difference mask; inspect hierarchy/typography/color/spacing/states/accessibility; iterate until `design-qa.md` has no P0/P1/P2 and says `final result: passed`.

Finish with Ponytail whole-diff deletion review, `git diff --check`, `superpowers:requesting-code-review`, and `superpowers:verification-before-completion`. Commit `test(editor): complete audio editor acceptance matrix`.

## Completion Rule

Completion requires all 15 gates/commits, honest Debug and Release results, real hardware/cross-app drag evidence, passed 1672×941 design QA, bounded performance, no obsolete editor path, no heavy dependency and no unrelated user changes.
