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
- Catalog entries are exact:
  - `uvr-mdxnet-kara`: `UVR_MDXNET_KARA.onnx`, `https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models/UVR_MDXNET_KARA.onnx`, 29,704,436 bytes, SHA-256 `e3167c87333a48548413e972a286bf40bf5694001d2853861eb1435953f02d63`, MDX, vocals/instrumental.
  - `uvr-mdx-net-inst-hq3`: `UVR-MDX-NET-Inst_HQ_3.onnx`, `https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models/UVR-MDX-NET-Inst_HQ_3.onnx`, 66,759,214 bytes, SHA-256 `317554b07fe1ea5279a77f2b1520a41ea4b93432560c4ffd08792c30fddf9adc`, MDX, vocals/instrumental.
  - `htdemucs-ft-fp16`: one Demucs model card with four 165,612,636-byte files from `https://huggingface.co/StemSplitio/htdemucs-ft-onnx/resolve/main/`: `htdemucs_ft_bass_fp16weights.onnx` / `b533037176b14b2df31c92a5d5b3d5660d0811b9b360d3db761964768b079961`, `htdemucs_ft_drums_fp16weights.onnx` / `047764dff888cfb87da917013377d4ec7a134f7419cbe486d9c339aa17975ddd`, `htdemucs_ft_other_fp16weights.onnx` / `b739171a7057b3107bb0711c6222d4a619b41b13a8f04026431d30f32ad2bd71`, and `htdemucs_ft_vocals_fp16weights.onnx` / `0cbe651f535415c9d26a7bb614f7d322dd5a080fa0298f2e50f478030a994dce`; provenance must say original model Meta Demucs, ONNX conversion StemSplit; stems vocals/instrumental/drums/bass/other.
  - Runtime `onnxruntime-directml-1.24.4`: `https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime.directml/1.24.4/microsoft.ml.onnxruntime.directml.1.24.4.nupkg`, 12,458,649 bytes, SHA-256 `57e9f11b73437bef7a309496135d4c1f96b1a8e9ddba60013fa27bfc1d788681`.
- Add safe custom-manifest validation for known MDX/Demucs ONNX families only; reject scripts, executables, dynamic custom ops, traversal, unknown files, invalid shapes, missing hashes, and unsupported stems.
- Implement Qt Network Range download with `.part`, pause/resume/cancel, disk-space preflight, SHA-256 verification, retry, and atomic activation. Preserve valid partials and never treat an unverified file as installed.
- Extract only the required native x64 runtime files from the pinned NuGet package into a versioned runtime directory; do not modify `vcpkg.json` or link ORT into AgPlayer.
- Tests cover catalog values, state transitions, malformed manifests, traversal, resume semantics, wrong hashes, atomic activation, and Unicode paths.

## Task 2: Native worker and versioned process protocol

- Add a small `AgSeparationWorker` executable that loads the external ORT library dynamically and never runs until separation starts.
- Implement versioned NDJSON messages `hello`, `probe`, `start`, `progress`, `cancel`, `result`, `error`, and `shutdown`, each with protocol version and request ID.
- Add exactly two trusted adapters:
  - MDX uses 44.1kHz stereo, periodic Hann STFT/ISTFT (`n_fft=6144`, `hop=1024`, `center=true`), channel packing `[L.real,L.imag,R.real,R.imag]`, three cleared low bins, 25% overlap, and weighted overlap-add. KARA uses `[N,4,2048,256]`, chunk 261120, trim 3072, vocals output and compensation 1.035. HQ3 uses `[N,4,3072,256]`, the same chunk/trim, instrumental output and compensation 1.022. The complementary stem is the original mix minus the primary stem.
  - Demucs uses four sequential sessions with `mix` FLOAT `[1,2,343980]` and `stems` FLOAT `[1,4,2,343980]`, output order `[drums,bass,other,vocals]`, overlap 85995 and stride 257985. Each specialized file contributes only its named row. The fifth accompaniment track is derived as `drums+bass+other` with final clipping protection; four sessions never remain resident together.
- Keep the first release on the built-in SHA-256 allowlist (or an application-signed manifest). ONNX graph checks are defense-in-depth only because the approved graphs do not embed the sampling rate, STFT, compensation, or stem semantics.
- Add bounded threading/chunking, structured errors, provider probing, Auto/CPU/GPU behavior, and CPU fallback for Auto. DirectML sessions disable memory patterns and use sequential execution. GPU is available only after enumerating DXGI adapters and completing a real minimal inference probe; provider presence or session creation alone is insufficient.
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
