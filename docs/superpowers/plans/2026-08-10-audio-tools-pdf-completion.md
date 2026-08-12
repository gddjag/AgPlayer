# AgPlayer 四模块音频工具与 EQ 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 交付 PDF 定义的四个真实音频工具，并让十段 EQ 在现有 PCM 输出链真实工作，不引入运行时外部进程或伪控件。

**Architecture:** 保持 `core` 的 libav 解码/编码与现有 `AudioEngine`。所有工具输入使用同一异步发现与任务状态；试听统一经播放会话仲裁。轻度剪辑只在非破坏性项目模型上编辑，最终才由 native 渲染器输出。

**Tech Stack:** C++17、Qt 6 / QML、CMake、libav*、已集成 SoundTouch、现有 C ABI。

## Global Constraints

- 不使用 `QProcess` 调用 ffmpeg，不调用系统全局 EQ，不更换播放器核心。
- 音频回调内不得锁、日志、文件访问或堆分配。
- 四工具固定为：轻度剪辑、格式转换、元数据修改、文件名处理；语言只保留中/英/泰/越。
- 所有外部文件操作先预览/校验；删除、覆盖、替换和移动必须可确认且失败可报告。
- 新增行为必须先加真实失败测试；全量验证前不得声称功能完成。

---

## 当前审计基线

- 已存在：顶端四工具入口、`AudioToolsController`、`FormatConverter`、`MetadataEditor`、`FilenameProcessor`、多轨编辑控制器、原生转码和十段 EQ PCM 插入点。
- 已确认问题：工具壳中有乱码文本；外部拖放、标题栏拖动和端到端工具操作的既有测试主要验证控件/路由，不能证明资源管理器真实交互；轻度剪辑模型与 PDF 的多 Clip、项目保存和操作语义尚未形成完整验收证据。
- EQ 现状：`GraphicEqualizerProcessor` 已在 `AudioEngine` 输出前处理 PCM；窗口固定为 780×460，仍需以真实声卡与扫频证据验收。

## Task 1: 统一工具壳、文本编码与真实拖放

**Files:**
- Modify: `app/qml/AgPlayer/AudioToolsWindow.qml`
- Modify: `qt/src/native_drop_router.cpp`, `qt/src/audio_tools_controller.cpp`
- Modify: `tests/qml/tst_audio_tools_window.qml`, `tests/qt/audio_tools_end_to_end_test.cpp`

**Interfaces:**
- Consumes: `NativeDropRouter::routeUrls(target, urls)` 和 `AudioToolsController::selectTool(index)`。
- Produces: `AudioToolsController::acceptExternalUrls(int tool, const QList<QUrl>& urls)`，返回接受、跳过、失败的结构化结果。

- [ ] **Step 1: Write the failing test**

```cpp
QVERIFY(controller.acceptExternalUrls(
    AudioToolsController::FormatConvert,
    {fixtureUrl("中文名称.wav"), fixtureUrl("invalid.txt")}).accepted == 1);
QCOMPARE(result.skipped, 1);
QVERIFY(result.message.contains(QStringLiteral("已跳过")));
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest --test-dir build/release-msvc -C Release -R audio_tools_end_to_end_test --output-on-failure`

Expected: FAIL because the controller has no structured external-drop result.

- [ ] **Step 3: Write minimal implementation**

```cpp
struct ToolImportResult { int accepted = 0; int skipped = 0; QStringList failures; };
Q_INVOKABLE ToolImportResult acceptExternalUrls(int tool, const QList<QUrl>& urls);
```

Route exactly once from native drop, expand directories on the worker queue, validate extension/content, and append accepted files through the target module API.

- [ ] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build/release-msvc -C Release -R "(audio_tools_end_to_end_test|qml_audio_tools_test)" --output-on-failure`

Expected: PASS with no QML encoding warning.

- [ ] **Step 5: Commit**

```bash
git add app/qml/AgPlayer/AudioToolsWindow.qml qt/src/native_drop_router.cpp qt/src/audio_tools_controller.cpp tests/qml/tst_audio_tools_window.qml tests/qt/audio_tools_end_to_end_test.cpp
git commit -m "fix: unify real audio-tool file drops"
```

## Task 2: 轻度剪辑的非破坏性多 Clip 项目

**Files:**
- Modify: `qt/src/light_editor_controller.hpp`, `qt/src/light_editor_controller.cpp`
- Modify: `core/src/multitrack_editor.hpp`, `core/src/multitrack_editor.cpp`
- Modify: `app/qml/AgPlayer/LightEditPage.qml`
- Modify: `tests/qt/light_editor_controller_test.cpp`, `tests/qml/tst_light_editor.qml`

**Interfaces:**
- Consumes: `AudioAsset`, `TrackRecord`, 现有预览会话。
- Produces: `ClipRecord { QUuid id; QUuid assetId; int track; qint64 start; qint64 in; qint64 out; qint64 fadeIn; qint64 fadeOut; double gain; bool muted; double rate; }`。

- [ ] **Step 1: Write the failing test**

```cpp
const auto first = editor.addUrls({fixtureUrl("one.wav"), fixtureUrl("two.wav")});
QCOMPARE(editor.trackClipCount(0), 1);
QCOMPARE(editor.trackClipCount(1), 1);
editor.splitClip(first.front(), 48'000);
QCOMPARE(editor.trackClipCount(0), 2);
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest --test-dir build/release-msvc -C Release -R light_editor_controller_test --output-on-failure`

Expected: FAIL because a track only owns one source/segment.

- [ ] **Step 3: Write minimal implementation**

Implement stable clip IDs, automatic next-empty-track placement (max 16 stereo tracks), and command objects for split, trim, move, delete, paste, undo and redo. Persist only project edits; never modify source media.

- [ ] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build/release-msvc -C Release -R "(light_editor_controller_test|qml_light_editor_test)" --output-on-failure`

Expected: PASS for auto-track placement, split, undo/redo and clip selection.

- [ ] **Step 5: Commit**

```bash
git add qt/src/light_editor_controller.* core/src/multitrack_editor.* app/qml/AgPlayer/LightEditPage.qml tests/qt/light_editor_controller_test.cpp tests/qml/tst_light_editor.qml
git commit -m "feat: make light editor non-destructive multi-clip"
```

## Task 3: 轻度剪辑时间线交互、BPM 和真实预览

**Files:**
- Modify: `app/qml/AgPlayer/components/tools/TimelineCanvas.qml`
- Modify: `app/qml/AgPlayer/LightEditPage.qml`
- Modify: `qt/src/light_editor_controller.cpp`, `core/src/timeline_preview_mixer.cpp`
- Test: `tests/qml/tst_light_editor.qml`, `tests/core/timeline_preview_mixer_test.cpp`

**Interfaces:**
- Consumes: `LightEditorController::selectionSet`, `ClipRecord`。
- Produces: `setSnapEnabled(bool)`, `setViewport(double secondsPerPixel, qint64 offset)`, `setProjectBpm(double)`, `setClipRate(QUuid, double)`。

- [ ] **Step 1: Write the failing test**

```qml
mouseMove(timeline, clip.x + 2, clip.y + 20)
wheel(timeline, clip.x, clip.y, 0, 120, Qt.ControlModifier)
verify(editor.viewportSecondsPerPixel < beforeZoom)
keyClick(Qt.Key_S)
compare(editor.trackClipCount(0), 2)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest --test-dir build/release-msvc -C Release -R qml_light_editor_test --output-on-failure`

Expected: FAIL because timeline gestures/shortcuts do not affect a real clip selection.

- [ ] **Step 3: Write minimal implementation**

Implement pointer-centered wheel zoom, Shift-wheel horizontal pan, Ctrl-wheel vertical scroll, middle-pan, box selection, edge trim, grid snap, and the PDF shortcuts: Space, S, Delete, F8, Ctrl+A/X/C/V/Z/Y, Home and End. Use the same preview session; preview pauses main playback before it starts.

- [ ] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build/release-msvc -C Release -R "(qml_light_editor_test|timeline_preview_mixer_test)" --output-on-failure`

Expected: PASS with preview mix changes after rate/BPM adjustment.

- [ ] **Step 5: Commit**

```bash
git add app/qml/AgPlayer/components/tools/TimelineCanvas.qml app/qml/AgPlayer/LightEditPage.qml qt/src/light_editor_controller.cpp core/src/timeline_preview_mixer.cpp tests/qml/tst_light_editor.qml tests/core/timeline_preview_mixer_test.cpp
git commit -m "feat: add real light-editor timeline gestures"
```

## Task 4: 格式转换、元数据与文件名处理的真实完成态

**Files:**
- Modify: `qt/src/format_converter.cpp`, `qt/src/metadata_editor.cpp`, `qt/src/filename_processor.cpp`
- Modify: `app/qml/AgPlayer/FormatConvertPage.qml`, `app/qml/AgPlayer/MetadataEditPage.qml`, `app/qml/AgPlayer/FilenameProcessPage.qml`
- Test: `tests/qt/audio_tools_end_to_end_test.cpp`, `tests/qt/filename_processor_test.cpp`

**Interfaces:**
- Consumes: shared tool import result and task status.
- Produces: truthful queued/running/cancelled/failed/succeeded states plus safe `RenamePlan` preview.

- [ ] **Step 1: Write the failing test**

```cpp
const auto job = converter.enqueue({fixtureUrl("source.flac")}, mp3_320_settings);
QVERIFY(waitForFinished(job));
QVERIFY(QFileInfo::exists(job.outputUrl.toLocalFile()));
QCOMPARE(probe(job.outputUrl).format, QStringLiteral("mp3"));
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest --test-dir build/release-msvc -C Release -R "(audio_tools_end_to_end_test|filename_processor_test)" --output-on-failure`

Expected: FAIL for any state that is UI-only, reports completion before re-probe, or silently overwrites.

- [ ] **Step 3: Write minimal implementation**

Validate actual encoder capability before queueing; re-probe output before success; make metadata use stream copy and report unsupported fields; build filename plan before stage/commit rename and verify size/hash unchanged.

- [ ] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build/release-msvc -C Release -R "(audio_tools_end_to_end_test|filename_processor_test)" --output-on-failure`

Expected: PASS for conversion, metadata rollback and collision-safe rename.

- [ ] **Step 5: Commit**

```bash
git add qt/src/format_converter.cpp qt/src/metadata_editor.cpp qt/src/filename_processor.cpp app/qml/AgPlayer/FormatConvertPage.qml app/qml/AgPlayer/MetadataEditPage.qml app/qml/AgPlayer/FilenameProcessPage.qml tests/qt/audio_tools_end_to_end_test.cpp tests/qt/filename_processor_test.cpp
git commit -m "fix: complete native audio-tool operations"
```

## Task 5: 十段 EQ 的实际声频验收

**Files:**
- Modify: `core/src/graphic_equalizer.cpp`, `qt/src/equalizer_controller.cpp`, `app/qml/AgPlayer/EqualizerWindow.qml`
- Test: `tests/core/graphic_equalizer_test.cpp`, `tests/qt/equalizer_controller_test.cpp`, `tests/qml/tst_main_window.qml`

**Interfaces:**
- Consumes: `GraphicEqSettings` and AudioEngine PCM render path.
- Produces: 10 independent RBJ peak filters, preamp, bypass crossfade and persisted preset state.

- [ ] **Step 1: Write the failing test**

```cpp
const double gain = measuredSineGainDb(1'000.0, 48'000, settingsWithBand(5, 6.0));
EXPECT_NEAR(gain, 6.0, 0.3);
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest --test-dir build/release-msvc -C Release -R graphic_equalizer_test --output-on-failure`

Expected: FAIL if an EQ band is disconnected from PCM or the response is outside tolerance.

- [ ] **Step 3: Write minimal implementation**

Keep the 780×460 compact window; ensure setting changes publish a complete immutable program to the lock-free audio mailbox, protect headroom from measured cascade response, and keep UI updates off the audio callback.

- [ ] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build/release-msvc -C Release -R "(graphic_equalizer_test|equalizer_controller_test|qml_main_window_test)" --output-on-failure`

Expected: PASS for frequency response, bypass smoothing, channel isolation and persisted state.

- [ ] **Step 5: Commit**

```bash
git add core/src/graphic_equalizer.cpp qt/src/equalizer_controller.cpp app/qml/AgPlayer/EqualizerWindow.qml tests/core/graphic_equalizer_test.cpp tests/qt/equalizer_controller_test.cpp tests/qml/tst_main_window.qml
git commit -m "feat: verify real ten-band graphic equalizer"
```

## Task 6: 播放器、窗口、波形与列表的 12 项回归

**Files:**
- Modify: `qt/src/window_controller.cpp`, `qt/src/import_controller.cpp`, `qt/src/waveform_provider.cpp`
- Modify: `app/qml/AgPlayer/Main.qml`, `app/qml/AgPlayer/WaveformView.qml`, `app/qml/AgPlayer/TrackList.qml`
- Test: `tests/qt/qml_main_window_test_main.cpp`, `tests/qml/tst_main_window.qml`, `tests/qt/waveform_provider_test.cpp`

**Interfaces:**
- Consumes: current playback duration, `WaveformProvider` generation result and window group controller.
- Produces: one authoritative time axis, grouped Z-order and bounded import/waveform worker behavior.

- [ ] **Step 1: Write the failing test**

```cpp
provider.loadForTrack(track.id, track.url);
QVERIFY(waitForWaveform(track.id));
QCOMPARE(provider.durationMs(), player.durationMs());
QVERIFY(std::abs(provider.timeForX(view.width()) - player.durationMs()) <= 20);
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest --test-dir build/release-msvc -C Release -R "(waveform_provider_test|qml_main_window_test)" --output-on-failure`

Expected: FAIL for tail blank, stale-track waveform or inconsistent seek mapping.

- [ ] **Step 3: Write minimal implementation**

Make waveform results track/generation/duration-versioned; map first and final samples to 0%/100%; prefetch adjacent tracks. Move batch discovery and metadata probing to bounded workers with fixed-size UI commits. Make docked player/list a single grouping for Z order and common edge geometry.

- [ ] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build/release-msvc -C Release -R "(waveform_provider_test|qml_main_window_test|import_controller_test)" --output-on-failure`

Expected: PASS for end-point mapping, autoscroll now-playing, cross-monitor undock and drag/drop without duplicate imports.

- [ ] **Step 5: Commit**

```bash
git add qt/src/window_controller.cpp qt/src/import_controller.cpp qt/src/waveform_provider.cpp app/qml/AgPlayer/Main.qml app/qml/AgPlayer/WaveformView.qml app/qml/AgPlayer/TrackList.qml tests/qt/qml_main_window_test_main.cpp tests/qml/tst_main_window.qml tests/qt/waveform_provider_test.cpp
git commit -m "fix: align player window and waveform timing"
```

## Task 7: Release evidence and manual Windows gate

**Files:**
- Modify: `docs/qa/audio-tools-windows-mvp.md`
- Test: release build and test matrix

- [ ] **Step 1: Write the failing acceptance checklist**

```markdown
- [ ] Explorer drops MP3, WAV, FLAC and a Chinese-named folder into every target.
- [ ] Each tool previews, edits/exports, then the exported file is re-opened and played.
- [ ] EQ 0 dB null behavior, plus/minus 6 dB swept response and headphone listening are recorded.
```

- [ ] **Step 2: Run the unverified checklist**

Run: `cmake --build build/release-msvc --config Release --parallel 4 && ctest --test-dir build/release-msvc -C Release --output-on-failure`

Expected: an incomplete checklist until actual Explorer, sound-card and headphone sessions are run.

- [ ] **Step 3: Write minimal documentation and evidence capture**

Document exact fixtures, screen dimensions, output hashes, test command results and remaining manual-only checks. Do not package until every checkbox has evidence.

- [ ] **Step 4: Run release verification**

Run: `cmake --build build/release-msvc --config Release --parallel 4 && ctest --test-dir build/release-msvc -C Release --output-on-failure`

Expected: zero build warnings/errors and no failed tests; any flaky existing test is recorded and fixed before packaging.

- [ ] **Step 5: Commit**

```bash
git add docs/qa/audio-tools-windows-mvp.md
git commit -m "docs: record audio-tools Windows acceptance evidence"
```

## Plan Review

- PDF coverage: tasks 1–4 cover the four required tools; task 5 covers EQ DSP and user-requested compact UI; task 6 maps the concurrently requested player/list/window/performance defects; task 7 blocks packaging behind real evidence.
- Deliberately excluded: VST/MIDI/DAW automation, system-global DSP, new database, and network services. They are outside the approved light-weight player scope.
- No generic placeholders: each task declares the tested interface, a failing test, verification command, minimal implementation boundary and evidence gate.
