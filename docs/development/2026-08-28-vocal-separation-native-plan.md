# AgPlayer 原生人声伴奏分离实施计划

## Global Constraints

- 在现有音频工具窗口新增第二个可见标签；保留工具 ID 0–3，新增 `Separation = 4`，显示顺序为 `[0, 4, 1, 2, 3]`。
- 主进程不得链接 ONNX Runtime；禁止 Python、PyTorch、LibTorch、CUDA、数据库和 Web 运行时。
- 仅复用 Qt、现有 Qt Network、FFmpeg、波形、预览、设置与播放列表能力；不恢复通用 `PluginInstallManager`。
- 模型与 ONNX Runtime 按需下载，主便携包相对 117,005,514 字节基线增量不超过 2MB，目标不超过 1MB。
- Windows x64 首发支持 CPU 与 DirectML；未完成 ARM64 真机验收前不得宣称 CoreML 已验证。
- 所有生产逻辑先写失败测试；不以占位按钮、假数据、假进度、假波形或 TODO 冒充功能。
- 视觉以 `C:/Users/Administrator/Desktop/音视频播放器/AgPlayer音频播放器完整版/人声伴奏分离.png` 为唯一真值；只给分离页使用局部深蓝工作台主题。

## Task 1: Model catalog, secure downloads, and runtime installation

- Add a domain-specific immutable catalog for the three approved model cards and pinned Microsoft DirectML runtime package, with exact URLs, sizes, SHA-256 values, families, stems, provenance, and resource guidance.
- Add safe custom-manifest validation for known MDX/Demucs ONNX families only; reject scripts, executables, dynamic custom ops, traversal, unknown files, invalid shapes, missing hashes, and unsupported stems.
- Implement Qt Network Range download with `.part`, pause/resume/cancel, disk-space preflight, SHA-256 verification, retry, and atomic activation. Preserve valid partials and never treat an unverified file as installed.
- Extract only the required native x64 runtime files from the pinned NuGet package into a versioned runtime directory; do not modify `vcpkg.json` or link ORT into AgPlayer.
- Tests cover catalog values, state transitions, malformed manifests, traversal, resume semantics, wrong hashes, atomic activation, and Unicode paths.

## Task 2: Native worker and versioned process protocol

- Add a small `AgSeparationWorker` executable that loads the external ORT library dynamically and never runs until separation starts.
- Implement versioned NDJSON messages `hello`, `probe`, `start`, `progress`, `cancel`, `result`, `error`, and `shutdown`, each with protocol version and request ID.
- Add MDX and Demucs adapters with bounded threading/chunking, structured errors, provider probing, Auto/CPU/GPU behavior, and CPU fallback for Auto.
- Decode and encode through existing FFmpeg facilities. Write every selected stem to a same-volume temporary directory, reopen-verify all outputs, then atomically commit; cancellation/failure removes temporary outputs and never overwrites existing results.
- Tests cover protocol parsing, incompatible versions, request routing, cancellation, timeout/crash, invalid model tensors, output transactions, and CPU fallback. Real-model tests are opt-in and use the approved catalog.

## Task 3: QML-facing controller, preview, waveforms, and history

- Add one `VocalSeparationController` deep module exposing input info, model list, selected model, devices, job state/stage/progress/error, stems, and history.
- Expose typed enums `ModelState`, `JobState`, `StemKind`, and `DeviceMode`, plus actions for input, model download lifecycle, start/cancel/retry, preview, export, playlist insertion, and opening directories.
- Reuse the single `AudioPreviewController` and preserve the shared position while switching original/stem tracks; never create five resident players.
- Analyze each result through existing waveform facilities and store bounded peak data only. Persist at most 500 history records with `QSaveFile`; results live outside the cache root.
- Tests cover state machine, idempotent cancellation, stale-worker messages, single-preview switching, corrupt history recovery, output existence checks, and Unicode paths.

## Task 4: Reference-matched responsive QML page

- Add the separation page to the existing audio-tools workbench and display it as the second navigation item while retaining stable internal IDs.
- Match the 1672×941 reference: input area/source waveform, three model cards plus custom mode, output settings, 70/30 work/history columns, stem selector, shared timeline, per-stem rows, and bottom action bar.
- Two-stem models enable only vocals/instrumental; htdemucs enables vocals/instrumental/drums/bass/other. Every status, filename, waveform, history item, action state, and progress value comes from real controller data.
- Responsive behavior: dual columns at >=1440, 336px right column at 1100–1439, settings/history tabs at 880–1099, scrolling with visible primary action at low heights.
- Reuse existing components and licensed icon assets; no emoji, text-glyph icons, handcrafted SVGs, or global theme changes.
- QML tests cover nav order, keyboard/accessibility, empty/download/error/running/completed states, 2-stem/5-stem capability, and 1672×941, 1280×720, 880×560, 1920×1080 layouts.

## Task 5: Integration, packaging evidence, visual QA, and documentation

- Extend native-drop routes, QA tool selection, shutdown handling, build targets, packaging assertions, licenses, and focused/full regression tests.
- Run real direct downloads and CPU separation for all three catalog entries; run DirectML probe and a complete result where supported. Exercise 3/5/10-minute audio, cancellation, worker termination, disk failure, Unicode/long paths, preview switching, export, playlist insertion, and directory opening.
- Measure main EXE, portable package, worker, runtime, and models; prove no worker, ORT module, model memory, GPU usage, or polling before the feature is used.
- Capture the implementation at the required sizes and Windows DPI values, compare it with the reference in one visual input, and iterate until `design-qa.md` says exactly `final result: passed` or honestly records a blocker.
- Write requirements traceability and acceptance evidence in `docs/development/`. Do not build or claim a formal installer before real audio, visual interaction, and hardware acceptance pass.
