# Format Conversion Full Replica Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver the 1672×942 reference-image format-conversion workbench with every visible control backed by real FFmpeg-library behavior and verified real-file conversion.

**Architecture:** Keep the existing synchronous `core/src/transcoder.*` pipeline, but separate its capability probing, immutable conversion planning, full-output verification, Qt task model, and bounded queue responsibilities. `FormatConverter` remains the QML facade; QML renders a virtualized `TableView` and never invents formats, parameters, progress, or success.

**Tech Stack:** C++17, Qt 6.7 Core/Concurrent/Gui/QML/Quick/QuickTest, FFmpeg libavformat/libavcodec/libavutil/libswresample, CMake/Ninja, MSVC x64, Qt Test, PowerShell contract tests.

## Global Constraints

- The reference viewport is exactly 1672×942; key region boundaries and control alignment must differ by no more than 2 px.
- Support 1280×720, 1920×1080, 4K, and high DPI without clipped controls, bitmap-scaled text, or horizontal content overflow.
- Do not invoke `ffmpeg.exe`; use only the FFmpeg libraries already linked by the project.
- Do not add third-party dependencies, demo tasks, fake progress, fake success, hard-coded capability claims, or silent codec/container/parameter fallback.
- Every displayed format requires a real compatible encoder and muxer; unsupported choices stay visible, disabled, and explain why.
- Conversion completion requires trailer write, flush, full reopen/decode validation, and atomic commit; 100% is forbidden before all four finish.
- Windows paths are UTF-8/Unicode-safe. A failed normalization cannot prove two paths are equal.
- Preserve unrelated dirty-worktree changes. Only touch format conversion, its direct integration points, tests, translations, and these documents.
- Use generated test media, not copyrighted music.
- Do not package, install, or replace an AgPlayer executable unless the user separately requests it after verification.

## 中文范围映射

- 导入：文件、文件夹、播放列表、歌曲列表右键、拖放。
- 任务：虚拟化表格、勾选、搜索、状态与格式筛选、重试、单项取消、取消全部。
- 参数：八种格式、编码器、CBR/VBR 或质量、码率、采样率、声道布局、位深或采样格式。
- 输出：自动序号、跳过、覆盖、询问；保留元数据、保留封面、保留文件夹目录结构、从视频提取并选择音轨。
- 安全：Unicode 中文路径、同目录临时文件、完整解码校验、原子提交、失败与取消清理。
- 验收：1672×942 像素级比对、其他分辨率适配、真实格式矩阵、1000 项任务性能和播放共存。

## File Structure

### Core conversion

- Create `core/src/transcode_capability.hpp/.cpp`: FFmpeg encoder/muxer capability catalogue and encoder-open parameter validation.
- Create `core/src/transcode_probe.hpp/.cpp`: input media/audio-stream/metadata/cover probe data.
- Create `core/src/transcode_verifier.hpp/.cpp`: full output reopen/decode and plan conformance validation.
- Modify `core/src/transcoder.hpp/.cpp`: Unicode-safe paths, selected audio stream, sample format/bit depth, cover handling, stage callbacks, and verifier-compatible result metrics.
- Modify `core/include/agplayer/c_api.h` and `core/src/c_api.cpp`: versioned v2 transcode request and diagnostic bridge while preserving the existing ABI entry points.
- Modify `core/CMakeLists.txt`: compile the new focused modules.

### Qt application boundary

- Create `qt/src/format_conversion_plan.hpp/.cpp`: immutable batch request, resolved task plans, conflict reservation, and confirmation differences.
- Create `qt/src/format_conversion_task_model.hpp/.cpp`: `QAbstractTableModel` with stable task IDs and fine-grained updates.
- Create `qt/src/format_conversion_filter_model.hpp/.cpp`: search/status/format proxy and visible counters.
- Modify `qt/src/format_converter.hpp/.cpp`: QML facade, discovery, preflight, queue scheduling, cancellation, ETA, diagnostics, and compatibility wrappers.
- Modify `qt/src/qml_registration.cpp` and `qt/CMakeLists.txt`: expose/model-link new types.

### UI and integrations

- Modify `app/qml/AgPlayer/components/tools/FormatConvertPage.qml`: pixel-aligned four-zone page and real interactions.
- Create `app/qml/AgPlayer/components/tools/FormatTaskTable.qml`: virtualized table/header/delegate rendering.
- Create `app/qml/AgPlayer/components/tools/FormatSettingsPanel.qml`: capability-driven A/B/C settings.
- Create `app/qml/AgPlayer/components/tools/FormatPreflightDialog.qml`: resolved plan, differences, conflicts, audio-stream choices, and confirmation.
- Create `app/qml/AgPlayer/components/tools/FormatErrorDialog.qml`: concise and copyable detailed diagnostics.
- Modify `app/CMakeLists.txt`: register the new QML files.
- Modify `app/qml/AgPlayer/components/TrackList.qml` only if its existing format-converter right-click bridge needs a minimal compatibility adjustment.

### Tests and translations

- Create `tests/core/transcode_capability_test.cpp`.
- Create `tests/core/transcode_verifier_test.cpp`.
- Create `tests/qt/format_conversion_plan_test.cpp`.
- Create `tests/qt/format_conversion_task_model_test.cpp`.
- Create `tests/qml/tst_format_converter.qml` and `tests/qt/qml_format_converter_test_main.cpp`.
- Modify `tests/core/transcoder_test.cpp`, `tests/qt/audio_tools_end_to_end_test.cpp`, `tests/qml/tst_light_editor.qml`, `tests/scripts/format_converter_reference_contract_test.ps1`, and `tests/CMakeLists.txt`.
- Modify `translations/agplayer_zh.ts`, `translations/agplayer_en.ts`, `translations/agplayer_th.ts`, and `translations/agplayer_vi.ts` for new user-facing strings.

---

### Task 1: Unicode-Safe Conversion Baseline

**Files:**
- Modify: `core/src/transcoder.cpp`
- Modify: `tests/core/transcoder_test.cpp`
- Modify: `tests/qt/audio_tools_end_to_end_test.cpp`

**Interfaces:**
- Consumes: existing `agplayer::transcode(const std::string&, const TranscodeConfig&, ...)` UTF-8 path contract.
- Produces: `std::filesystem::path path_from_utf8(std::string_view)` and `bool paths_refer_to_same_file(const path&, const path&)` internal helpers.

- [ ] **Step 1: Preserve the existing failing Chinese-path regression and add a core-level case**

Add a core test using two distinct Chinese directories and an output that does not yet exist:

```cpp
const auto input = temp / std::filesystem::u8path(u8"下载/输入.wav");
const auto output = temp / std::filesystem::u8path(u8"桌面/输出.wav");
std::filesystem::create_directories(input.parent_path());
std::filesystem::create_directories(output.parent_path());
std::filesystem::copy_file(source, input);
TranscodeConfig config;
config.output_path = output.u8string();
config.codec_name = "pcm_s16le";
std::string error;
assert(transcode(input.u8string(), config, nullptr, nullptr, error) == AG_OK);
assert(std::filesystem::exists(output));
```

- [ ] **Step 2: Run the red tests**

Run:

```powershell
cmake --build build/release-verify --target transcoder_test audio_tools_end_to_end_test
ctest --test-dir build/release-verify -R '^(transcoder_test|audio_tools_end_to_end_test)$' --output-on-failure
```

Expected: FAIL with the false `Input and output path must be different` result for distinct Unicode paths.

- [ ] **Step 3: Implement guarded Unicode path comparison**

Use `std::filesystem::u8path` for UTF-8 inputs. Check `equivalent` only when its own `error_code` is clear; compare `weakly_canonical` values only when both independent normalizations succeed:

```cpp
bool paths_refer_to_same_file(const fs::path& input, const fs::path& output)
{
    std::error_code equivalentError;
    if (fs::equivalent(input, output, equivalentError) && !equivalentError)
        return true;
    std::error_code inputError;
    std::error_code outputError;
    const fs::path normalizedInput = fs::weakly_canonical(input, inputError);
    const fs::path normalizedOutput = fs::weakly_canonical(output, outputError);
    return !inputError && !outputError && normalizedInput == normalizedOutput;
}
```

- [ ] **Step 4: Verify the focused baseline**

Run the Step 2 commands. Expected: both tests PASS, including the Qt Chinese-path case and the same-file rejection case.

- [ ] **Step 5: Commit**

```powershell
git add core/src/transcoder.cpp tests/core/transcoder_test.cpp tests/qt/audio_tools_end_to_end_test.cpp
git commit -m "fix: support unicode format conversion paths"
```

---

### Task 2: Real Encoder and Muxer Capability Catalogue

**Files:**
- Create: `core/src/transcode_capability.hpp`
- Create: `core/src/transcode_capability.cpp`
- Create: `tests/core/transcode_capability_test.cpp`
- Modify: `core/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces:

```cpp
namespace agplayer {
struct TranscodeOptionChoice { std::string key; std::string label; bool is_default; };
struct TranscodeFormatCapability {
    std::string key;
    std::string label;
    std::string muxer_name;
    std::string codec_name;
    bool available;
    std::string unavailable_reason;
    bool lossy;
    bool supports_metadata;
    bool supports_cover;
    std::vector<int> sample_rates;
    std::vector<std::string> sample_formats;
    std::vector<std::string> channel_layouts;
    std::vector<TranscodeOptionChoice> bitrate_modes;
};
std::vector<TranscodeFormatCapability> transcode_capabilities();
const TranscodeFormatCapability* find_transcode_capability(
    const std::vector<TranscodeFormatCapability>&, std::string_view key);
}
```

- [ ] **Step 1: Write the capability matrix tests**

Assert exactly the eight ordered keys `mp3, flac, wav, aac, opus, ogg, alac, m4a`; assert every available item has both `avcodec_find_encoder_by_name(codec_name)` and `av_guess_format(muxer_name, ...)`; assert missing encoders or muxers yield a non-empty `unavailable_reason`; assert AAC and ALAC keep distinct codec choices despite sharing M4A/MP4 containers.

- [ ] **Step 2: Run the red test**

```powershell
cmake --build build/release-verify --target transcode_capability_test
ctest --test-dir build/release-verify -R '^transcode_capability_test$' --output-on-failure
```

Expected: build FAIL because the new catalogue does not exist.

- [ ] **Step 3: Implement the catalogue from FFmpeg descriptors**

Build fixed product entries only for the eight approved formats, then populate their actual availability and options from FFmpeg codec/muxer structures. Open a temporary codec context for candidate combinations so advertised sample format/rate/layout values are known-openable. Do not substitute a different codec unless that codec name is explicitly returned in the entry.

- [ ] **Step 4: Run focused and existing format tests**

```powershell
cmake --build build/release-verify --target transcode_capability_test format_matrix_test
ctest --test-dir build/release-verify -R '^(transcode_capability_test|format_matrix_test)$' --output-on-failure
```

Expected: PASS; unavailable formats are reported, not treated as failures.

- [ ] **Step 5: Commit**

```powershell
git add core/src/transcode_capability.* core/CMakeLists.txt tests/core/transcode_capability_test.cpp tests/CMakeLists.txt
git commit -m "feat: expose real conversion capabilities"
```

---

### Task 3: Media Probe and Immutable Preflight Plan

**Files:**
- Create: `core/src/transcode_probe.hpp`
- Create: `core/src/transcode_probe.cpp`
- Create: `qt/src/format_conversion_plan.hpp`
- Create: `qt/src/format_conversion_plan.cpp`
- Create: `tests/qt/format_conversion_plan_test.cpp`
- Modify: `core/CMakeLists.txt`
- Modify: `qt/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces:

```cpp
namespace agplayer {
struct AudioStreamProbe {
    int stream_index;
    std::string codec;
    std::string language;
    std::string title;
    bool is_default;
    int sample_rate;
    std::string sample_format;
    std::string channel_layout;
    std::int64_t bit_rate;
    std::int64_t duration_ms;
};
struct MediaProbe {
    std::string container;
    bool is_video;
    bool has_cover;
    std::vector<AudioStreamProbe> audio_streams;
};
ag_result probe_transcode_input(std::string_view utf8_path,
                                MediaProbe&, std::string& error);
}

enum class FormatConflictPolicy { AutoNumber, Skip, Overwrite, Ask };
struct FormatConversionRequest {
    QString formatKey;
    QString codecName;
    QString bitrateMode;
    qint64 bitrate;
    int quality;
    int sampleRate;
    QString channelLayout;
    QString sampleFormat;
    QString outputDirectory;
    FormatConflictPolicy conflictPolicy;
    bool keepMetadata;
    bool keepCover;
    bool preserveDirectories;
    bool extractAudio;
};
struct FormatPlanDifference { QString field; QVariant requested; QVariant resolved; QString reason; bool requiresConfirmation; };
struct FormatTaskPlan { QUuid taskId; QString inputPath; QString outputPath; int audioStreamIndex; QVariantMap resolvedProfile; QList<FormatPlanDifference> differences; bool skipped; };
struct FormatBatchPlan { QList<FormatTaskPlan> tasks; bool ready; bool requiresConfirmation; QString fatalError; };
```

- [ ] **Step 1: Write probe and plan failure tests**

Test generated audio, a generated two-audio-stream Matroska fixture, metadata and attached cover detection, disabled video extraction, ambiguous multi-audio selection, automatic default-stream choice, automatic output numbering, skip/overwrite/ask, folder-root-relative paths, and path traversal rejection.

- [ ] **Step 2: Run the red test**

```powershell
cmake --build build/release-verify --target format_conversion_plan_test
ctest --test-dir build/release-verify -R '^format_conversion_plan_test$' --output-on-failure
```

Expected: build FAIL because the probe and planner types are absent.

- [ ] **Step 3: Implement probe and deterministic planning**

Probe with `avformat_open_input`, `avformat_find_stream_info`, stream dispositions, codec parameters, metadata dictionaries, and attached-picture disposition. Resolve all selected tasks before any worker starts. Reserve case-insensitive Windows output paths in one set, calculate folder-relative destinations only under the declared import root, and surface every requested/resolved difference.

- [ ] **Step 4: Verify the planner**

Run the Step 2 commands. Expected: PASS with no output files created by preflight.

- [ ] **Step 5: Commit**

```powershell
git add core/src/transcode_probe.* core/CMakeLists.txt qt/src/format_conversion_plan.* qt/CMakeLists.txt tests/qt/format_conversion_plan_test.cpp tests/CMakeLists.txt
git commit -m "feat: add deterministic conversion preflight"
```

---

### Task 4: Extended FFmpeg Pipeline, Cover Preservation, and Full Verification

**Files:**
- Create: `core/src/transcode_verifier.hpp`
- Create: `core/src/transcode_verifier.cpp`
- Create: `tests/core/transcode_verifier_test.cpp`
- Modify: `core/src/transcoder.hpp`
- Modify: `core/src/transcoder.cpp`
- Modify: `core/include/agplayer/c_api.h`
- Modify: `core/src/c_api.cpp`
- Modify: `core/CMakeLists.txt`
- Modify: `tests/core/transcoder_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Extends internal `TranscodeConfig` with `container_name`, `sample_format`, `channel_layout`, `audio_stream_index`, `keep_cover`, and stage callback.
- Produces versioned C ABI without changing the existing `ag_transcode_ex` layout:

```c
typedef struct ag_transcode_request_v2 {
    size_t struct_size;
    uint32_t api_version;
    const char* output_path;
    const char* muxer_name;
    const char* codec_name;
    long long bit_rate;
    int sample_rate;
    const char* channel_layout;
    const char* sample_format;
    int audio_stream_index;
    int keep_metadata;
    int keep_cover;
    int bitrate_mode;
    int quality;
} ag_transcode_request_v2;
ag_result ag_transcode_v2(const char* input_path,
                          const ag_transcode_request_v2* request,
                          const ag_cancel_token* token,
                          ag_progress_callback progress,
                          void* user_data);
```

- Produces:

```cpp
struct TranscodeVerificationPlan {
    std::int64_t expected_duration_ms;
    bool lossless;
    bool expect_metadata;
    bool expect_cover;
    int expected_audio_streams;
};
struct TranscodeVerificationResult {
    std::int64_t decoded_samples;
    std::int64_t decoded_duration_ms;
    bool metadata_present;
    bool cover_present;
};
ag_result verify_transcoded_output(std::string_view utf8_path,
                                   const TranscodeVerificationPlan&,
                                   TranscodeVerificationResult&,
                                   std::string& error);
```

- [ ] **Step 1: Write failing pipeline and verifier tests**

Cover selected-stream extraction, metadata and attached-picture preservation, explicit sample format/layout, 44.1↔48 kHz, mono/stereo, last-frame and Swr/FIFO drain, cancelled conversion cleanup, corrupt/truncated output rejection, valid lossless full decode, valid lossy duration tolerance, and existing-output preservation when verification fails.

- [ ] **Step 2: Run the red tests**

```powershell
cmake --build build/release-verify --target transcoder_test transcode_verifier_test
ctest --test-dir build/release-verify -R '^(transcoder_test|transcode_verifier_test)$' --output-on-failure
```

Expected: build or assertions FAIL for missing v2 fields, cover handling, and full verifier.

- [ ] **Step 3: Implement only the tested pipeline extensions**

Select the planned audio stream rather than calling best-stream again. Use the resolved encoder sample format/rate/layout; retain existing packet/frame reuse and `send/receive` loops; drain decoder, Swr, FIFO, and encoder completely. Copy metadata dictionaries only when requested. Copy only attached-picture packets/codec parameters when requested and compatible. Treat unconsumed encoder options as an error.

- [ ] **Step 4: Implement full-output verification before commit**

Reopen the staged file, discover stream structure, decode every audio packet through EOF, count valid samples, compare effective duration within codec delay/padding tolerance, and verify metadata/cover expectations. Return a stage-specific diagnostic and leave the pre-existing final file untouched on failure.

- [ ] **Step 5: Run core conversion regression**

```powershell
cmake --build build/release-verify --target transcoder_test transcode_verifier_test format_matrix_test
ctest --test-dir build/release-verify -R '^(transcoder_test|transcode_verifier_test|format_matrix_test)$' --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```powershell
git add core/src/transcoder.* core/src/transcode_verifier.* core/include/agplayer/c_api.h core/src/c_api.cpp core/CMakeLists.txt tests/core/transcoder_test.cpp tests/core/transcode_verifier_test.cpp tests/CMakeLists.txt
git commit -m "feat: verify and safely commit converted audio"
```

---

### Task 5: Virtualized Task and Filter Models

**Files:**
- Create: `qt/src/format_conversion_task_model.hpp`
- Create: `qt/src/format_conversion_task_model.cpp`
- Create: `qt/src/format_conversion_filter_model.hpp`
- Create: `qt/src/format_conversion_filter_model.cpp`
- Create: `tests/qt/format_conversion_task_model_test.cpp`
- Modify: `qt/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces `FormatConversionTaskModel : QAbstractTableModel` with roles:

```cpp
enum Role {
    TaskIdRole = Qt::UserRole + 1, CheckedRole, FileNameRole, PathRole,
    SourceFormatRole, DurationMsRole, SampleRateRole, BitRateRole,
    ChannelLayoutRole, SampleFormatRole, OutputFormatRole, OutputPathRole,
    StatusRole, StageRole, ProgressRole, ErrorSummaryRole, ErrorDetailRole,
    ResolvedProfileRole, ImportRootRole, AudioStreamsRole
};
Q_INVOKABLE void setChecked(const QString& taskId, bool checked);
Q_INVOKABLE void setAllVisibleChecked(bool checked);
Q_INVOKABLE void removeTasks(const QStringList& taskIds);
```

- Produces `FormatConversionFilterModel : QSortFilterProxyModel` properties `query`, `statusFilter`, `formatFilter`, `visibleCount`, `visibleCheckedCount` and invokable `sourceTaskId(int proxyRow)`.

- [ ] **Step 1: Write model tests**

Test nine table columns and role names, stable IDs after removal, checked state independent of current row, fine-grained `dataChanged`, all-visible checking through the proxy, filename/source/output/tag search, status filters, format filters, status counts, duration-weighted total progress, and 1000-row filtering.

- [ ] **Step 2: Run the red model test**

```powershell
cmake --build build/release-verify --target format_conversion_task_model_test
ctest --test-dir build/release-verify -R '^format_conversion_task_model_test$' --output-on-failure
```

Expected: build FAIL because the models are absent.

- [ ] **Step 3: Implement models with granular updates**

Store tasks by stable ID with an ID-to-row index. Emit `beginInsertRows`, `beginRemoveRows`, and role-scoped `dataChanged`; never reset the full model for progress. Filter text case-insensitively and invalidate only when relevant roles change. Throttle worker progress application at the facade boundary, not inside table delegates.

- [ ] **Step 4: Verify model behavior and performance contract**

Run the Step 2 commands. Expected: PASS; the 1000-row filter assertion completes within the test’s 100 ms budget on the release build.

- [ ] **Step 5: Commit**

```powershell
git add qt/src/format_conversion_task_model.* qt/src/format_conversion_filter_model.* qt/CMakeLists.txt tests/qt/format_conversion_task_model_test.cpp tests/CMakeLists.txt
git commit -m "feat: add virtualized conversion task model"
```

---

### Task 6: Queue Facade, Real Settings, Progress, Cancellation, and Diagnostics

**Files:**
- Modify: `qt/src/format_converter.hpp`
- Modify: `qt/src/format_converter.cpp`
- Modify: `qt/src/qml_registration.cpp`
- Modify: `tests/qt/audio_tools_end_to_end_test.cpp`

**Interfaces:**
- Replaces the QML `files` list as the primary contract with properties:

```cpp
Q_PROPERTY(QAbstractItemModel* taskModel READ taskModel CONSTANT)
Q_PROPERTY(QAbstractItemModel* filteredTaskModel READ filteredTaskModel CONSTANT)
Q_PROPERTY(QVariantList outputCapabilities READ outputCapabilities NOTIFY outputCapabilitiesChanged)
Q_PROPERTY(QVariantMap currentCapability READ currentCapability NOTIFY currentCapabilityChanged)
Q_PROPERTY(QVariantMap pendingPlan READ pendingPlan NOTIFY pendingPlanChanged)
Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
Q_PROPERTY(QString etaText READ etaText NOTIFY etaChanged)
Q_PROPERTY(int checkedCount READ checkedCount NOTIFY countsChanged)
Q_PROPERTY(int convertingCount READ convertingCount NOTIFY countsChanged)
Q_PROPERTY(int completedCount READ completedCount NOTIFY countsChanged)
Q_PROPERTY(int failedCount READ failedCount NOTIFY countsChanged)
Q_PROPERTY(int cancelledCount READ cancelledCount NOTIFY countsChanged)
```

- Produces invokables `addUrls`, `addFolder`, `removeChecked`, `clearFinished`, `buildPreflight`, `confirmPendingPlan`, `rejectPendingPlan`, `retryFailed`, `cancelTask`, and `cancelAll`.
- Keeps existing `loadFiles`, `start`, and `startSelected` wrappers only for current callers/tests; wrappers route through the new plan and queue.

- [ ] **Step 1: Write facade red tests**

Cover asynchronous discovery, import-root retention, no start before confirmed differences, all four conflict policies, bounded parallelism 1/2/4, selected-only start, legal stage sequence, real per-task progress, duration-weighted total progress, ETA non-negativity, cancellation of queued/running tasks, failure isolation, retry, complete diagnostics, and staged-output cleanup.

- [ ] **Step 2: Run the red end-to-end test**

```powershell
cmake --build build/release-verify --target audio_tools_end_to_end_test
ctest --test-dir build/release-verify -R '^audio_tools_end_to_end_test$' --output-on-failure
```

Expected: FAIL because the new models, counts, pending plan, and stage transitions are not exposed.

- [ ] **Step 3: Refactor `FormatConverter` into the tested facade**

Own the model, filter model, capabilities, current request, immutable pending plan, worker pool, task tokens, and a maximum 10 Hz GUI progress flush timer. Queue only checked ready tasks. Map core stages to `分析中/就绪/待确认/排队中/转换中/校验中/提交中/已完成/失败/已取消`; disallow illegal backward transitions. Build diagnostics with task ID, paths, stage, core code/text, requested/resolved profile, last timestamp/sample, retryability, and cleanup result.

- [ ] **Step 4: Verify the facade and legacy callers**

```powershell
cmake --build build/release-verify --target audio_tools_end_to_end_test qml_light_editor_test
ctest --test-dir build/release-verify -R '^(audio_tools_end_to_end_test|qml_light_editor_test)$' --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```powershell
git add qt/src/format_converter.* qt/src/qml_registration.cpp tests/qt/audio_tools_end_to_end_test.cpp
git commit -m "feat: add production conversion queue facade"
```

---

### Task 7: Import Paths and Player Integration

**Files:**
- Modify: `qt/src/format_converter.hpp`
- Modify: `qt/src/format_converter.cpp`
- Modify: `app/qml/AgPlayer/components/tools/FormatConvertPage.qml`
- Modify if required: `app/qml/AgPlayer/components/TrackList.qml`
- Modify: `tests/qt/audio_tools_end_to_end_test.cpp`
- Modify: `tests/qml/tst_light_editor.qml`

**Interfaces:**
- Consumes existing `FormatConverter.loadFiles(urls)` right-click bridge.
- Produces `Q_INVOKABLE void addPlaylistPaths(const QStringList& paths)` and folder-import roots retained per task.

- [ ] **Step 1: Write integration red tests**

Assert file dialog URLs, recursive folder discovery, dropped folders, current playlist path lists, current-track import, and multi-selected TrackList right-click calls all arrive in the model exactly once. Assert video files are accepted for analysis only when extraction is enabled and rejected during preflight otherwise.

- [ ] **Step 2: Run the red integration tests**

```powershell
cmake --build build/release-verify --target audio_tools_end_to_end_test qml_light_editor_test
ctest --test-dir build/release-verify -R '^(audio_tools_end_to_end_test|qml_light_editor_test)$' --output-on-failure
```

Expected: FAIL for missing playlist/folder-root behavior.

- [ ] **Step 3: Wire real import sources**

Reuse `audio_file_discovery` for background recursion. Convert local URLs with Qt, keep the root directory on folder imports, deduplicate canonical paths case-insensitively on Windows, and route playlist/current-track/right-click sources through the same facade method.

- [ ] **Step 4: Verify every import path**

Run the Step 2 commands. Expected: PASS.

- [ ] **Step 5: Commit**

```powershell
git add qt/src/format_converter.* app/qml/AgPlayer/components/tools/FormatConvertPage.qml app/qml/AgPlayer/components/TrackList.qml tests/qt/audio_tools_end_to_end_test.cpp tests/qml/tst_light_editor.qml
git commit -m "feat: connect all conversion import paths"
```

---

### Task 8: Pixel-Aligned QML Workbench and Complete Interactions

**Files:**
- Modify: `app/qml/AgPlayer/components/tools/FormatConvertPage.qml`
- Create: `app/qml/AgPlayer/components/tools/FormatTaskTable.qml`
- Create: `app/qml/AgPlayer/components/tools/FormatSettingsPanel.qml`
- Create: `app/qml/AgPlayer/components/tools/FormatPreflightDialog.qml`
- Create: `app/qml/AgPlayer/components/tools/FormatErrorDialog.qml`
- Modify: `app/CMakeLists.txt`
- Create: `tests/qml/tst_format_converter.qml`
- Create: `tests/qt/qml_format_converter_test_main.cpp`
- Modify: `tests/scripts/format_converter_reference_contract_test.ps1`
- Modify: `tests/CMakeLists.txt`
- Modify: `translations/agplayer_zh.ts`
- Modify: `translations/agplayer_en.ts`
- Modify: `translations/agplayer_th.ts`
- Modify: `translations/agplayer_vi.ts`

**Interfaces:**
- Consumes only the facade properties/models/invokables from Task 6.
- Produces stable object names for all reference controls, dialogs, zones, columns, chips, settings, and bottom metrics.

- [ ] **Step 1: Write the failing QML interaction and geometry tests**

At a 1672×942 tools window, assert title 48 px, tabs 55 px, toolbar 60 px, bottom bar 114 px, 8 px main-panel gap, left/right reference widths within 2 px, nine ordered columns, eight format buttons, dynamic parameter controls, four output checkboxes, status chips, search/filter, and all bottom controls. Interact with selection, search, filters, format change, parameters, conflict policy, checkboxes, preflight confirm/reject, error copy, start, single cancel, and cancel-all.

- [ ] **Step 2: Run the red UI tests**

```powershell
cmake --build build/release-verify --target qml_format_converter_test
ctest --test-dir build/release-verify -R '^(qml_format_converter_test|format_converter_reference_contract_test)$' --output-on-failure
```

Expected: build/test FAIL because the new page components and exact geometry contract are absent.

- [ ] **Step 3: Implement the four-zone page**

Use the shell’s existing 48 px title and 55 px sidebar. Inside the page render a 60 px toolbar, remaining main split, and 114 px bottom bar. Use `TableView` with the proxy model and reusable delegates; use real theme icons. Match the reference colors, 6 px radii, borders, padding, row heights, typography, status colors, and progress bars. At narrow sizes keep the same information hierarchy and scroll only the settings panel or table as specified.

- [ ] **Step 4: Bind every control to real state**

Capability buttons bind `enabled` and tooltip to backend capability. Parameter models bind to `currentCapability`. The start button calls preflight; only the preflight dialog’s confirmation starts work. Preserve-metadata, preserve-cover, preserve-directories, and extract-video all write the request used by the backend. Failure rows open real diagnostics; copy uses `Clipboard` support already available to Qt/QML.

- [ ] **Step 5: Verify QML, contracts, translations, and responsive sizes**

```powershell
cmake --build build/release-verify --target qml_format_converter_test qml_light_editor_test AgPlayer
ctest --test-dir build/release-verify -R '^(qml_format_converter_test|qml_light_editor_test|format_converter_reference_contract_test|translation_catalog_test|source_encoding_test)$' --output-on-failure
```

Expected: PASS at 1672×942, 1280×720, 1920×1080, and 3840×2160 test sizes.

- [ ] **Step 6: Commit**

```powershell
git add app/qml/AgPlayer/components/tools/FormatConvertPage.qml app/qml/AgPlayer/components/tools/FormatTaskTable.qml app/qml/AgPlayer/components/tools/FormatSettingsPanel.qml app/qml/AgPlayer/components/tools/FormatPreflightDialog.qml app/qml/AgPlayer/components/tools/FormatErrorDialog.qml app/CMakeLists.txt tests/qml/tst_format_converter.qml tests/qt/qml_format_converter_test_main.cpp tests/scripts/format_converter_reference_contract_test.ps1 tests/CMakeLists.txt translations/agplayer_*.ts
git commit -m "feat: replicate format conversion workbench"
```

---

### Task 9: Real Format Matrix, Visual Evidence, Performance, and Full Regression

**Files:**
- Modify: `tests/qt/audio_tools_end_to_end_test.cpp`
- Modify: `tests/core/format_matrix_test.cpp`
- Create: `docs/qa/2026-08-13-format-conversion-evidence.md`

**Interfaces:**
- Consumes the finished page, facade, planner, pipeline, verifier, and current linked FFmpeg build.
- Produces reproducible test commands, capability results, output hashes/metadata, screenshots, and explicit remaining limitations.

- [ ] **Step 1: Complete the real conversion matrix tests**

For each backend-reported available format, generate a WAV source and convert/reopen/decode it. Explicitly cover WAV→MP3/FLAC/AAC/M4A/Opus/OGG/ALAC, FLAC→MP3, MP3→FLAC, Unicode paths, folder structure, metadata, cover, selected video audio stream, concurrent auto-numbering, skip, overwrite protection, ask confirmation, individual cancel, cancel-all, failure isolation, and retry.

- [ ] **Step 2: Build all relevant targets**

```powershell
cmake --build build/release-verify --target AgPlayer transcoder_test transcode_capability_test transcode_verifier_test format_matrix_test format_conversion_plan_test format_conversion_task_model_test audio_tools_end_to_end_test qml_format_converter_test qml_light_editor_test
```

Expected: build succeeds with zero new warnings.

- [ ] **Step 3: Run focused conversion and UI suites**

```powershell
ctest --test-dir build/release-verify -R '^(transcoder_test|transcode_capability_test|transcode_verifier_test|format_matrix_test|format_conversion_plan_test|format_conversion_task_model_test|audio_tools_end_to_end_test|qml_format_converter_test|qml_light_editor_test|format_converter_reference_contract_test|translation_catalog_test|source_encoding_test)$' --output-on-failure
```

Expected: every selected test PASS.

- [ ] **Step 4: Run complete repository regression**

```powershell
ctest --test-dir build/release-verify --output-on-failure
```

Expected: all tests PASS. Any unrelated pre-existing failure must be recorded with the exact test, output, and proof it predates this work; do not hide or relabel it.

- [ ] **Step 5: Capture visual and runtime evidence without packaging**

Run the built worktree `AgPlayer.exe`, open the audio tools at 1672×942, import generated real fixtures, and capture the format page in empty, ready, converting, completed, failed, and preflight-confirmation states. Compare the 1672×942 capture with the supplied reference, inspect 1280×720/1920×1080/4K, and test real playback responsiveness during conversion. Do not overwrite the installed application.

- [ ] **Step 6: Record evidence**

Write `docs/qa/2026-08-13-format-conversion-evidence.md` with:

```markdown
- Git commit and build directory
- Linked FFmpeg version
- Actual available/disabled formats, encoders, and muxers with reasons
- Exact build and test commands plus pass/fail counts
- Each real input/output path, requested profile, resolved profile, result size, duration, and reopen/decode result
- Unicode, metadata, cover, video stream, conflict, cancellation, and cleanup results
- Screenshot paths and reference geometry comparison
- 1000-task interaction timing and conversion/playback responsiveness observations
- Explicit remaining limitations
```

- [ ] **Step 7: Run verification diff checks and commit evidence/tests**

```powershell
git diff --check
git status --short
git add tests/qt/audio_tools_end_to_end_test.cpp tests/core/format_matrix_test.cpp docs/qa/2026-08-13-format-conversion-evidence.md
git commit -m "test: verify complete format conversion workflow"
```

Expected: no whitespace errors; commit includes only final matrix/evidence changes.

## Final Completion Gate

Before claiming completion, verify all ten completion conditions in `docs/superpowers/specs/2026-08-12-format-conversion-remediation-design.md`. Report only observed facts: changed files, actual enabled formats/codecs, preflight behavior, pipeline/verification/atomic-commit behavior, exact test counts, real samples, screenshots, performance observations, and explicit limitations. Do not package or replace any installed executable.
