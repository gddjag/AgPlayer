# AG Player《音频编辑》V2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在现有 AG Player 中以生产级原生轻量单轨《音频编辑》完整替换旧《轻度剪辑》，严格实现上传 UI 与 V3 开发文档，并在验收后删除全部旧多轨代码。

**Architecture:** 新增纯 C++ `AudioDocument`、`EditorViewport`、`DocumentWriter`、`RecordingSession`、`TimePitchSession` 深模块；Qt Bridge 只聚合状态与 Action，QML 只负责绑定和布局。新模块先以索引 0 并行接入，通过自动化、真实 UI、真实音频与硬件录音验收后，再删除旧 `LightEditor`/multitrack 路径。

**Tech Stack:** Qt 6.7 / QML / C++17 / CMake / FFmpeg / SoundTouch / Windows WASAPI / Qt Test / Quick Test

## Global Constraints

- 顶部顺序固定为：`音频编辑 / 格式转换 / 元数据修改 / 文件名处理`；这是用户本次文字要求，覆盖参考图和 V3 文档中的冲突顺序。
- 严格使用上传 UI 的无左栏、中央大波形、右侧仅“录音/速度与音高”、底部 Transport/状态栏布局。
- 保留 `PlayerPane.qml`、`TrackList.qml`、`tst_main_window.qml` 的现有未提交修改，不重置、不覆盖、不混入本计划提交。
- 不引入 Qt Multimedia、Electron、WebView、Python、AI 模型、完整 DAW、VST/MIDI 或新的大型依赖。
- 主时间坐标只用 `int64_t SampleFrame`；不得用浮点毫秒保存文档、选区、标记或播放头。
- 打开大文件不完整解码到 RAM；剪辑不重写完整文件；缩放不重新扫描整文件。
- QML 不承载音频业务逻辑，不用 Canvas/大量 Rectangle 渲染完整波形。
- 录音仅 Windows WASAPI Shared Event-Driven；未实现平台由 CMake Feature Flag 和 UI 明确关闭。
- 保存/导出/录音源使用临时文件、完整校验与原子提交；失败不得损坏源文件或显示成功。
- 所有新增行为使用 RED → GREEN → REFACTOR；每个阶段都重新加载 VS 开发环境并执行 Release 构建和对应测试。
- 本轮不封装 EXE；完成声明必须提供真实 UI、播放、录音、处理、保存与导出证据。
- 同口径安装体积增量目标不超过 5 MiB，不包含现有 FFmpeg 与调试符号。

## Shared Build Commands

在 `D:/ai/AgPlayer/.worktrees/revised-ui` 中运行：

```powershell
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build build\release-msvc --config Release --parallel 4'
```

```powershell
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ctest --test-dir build\release-msvc -C Release --output-on-failure -j 1'
```

---

### Task 1: 固化基线、Feature Flags 与迁移契约

**Files:**
- Create: `docs/qa/audio-editor-v2-baseline.md`
- Create: `tests/cmake/audio_editor_feature_options_test.cmake`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `AG_ENABLE_AUDIO_EDITOR=ON`、`AG_ENABLE_NATIVE_RECORDING=ON`、`AG_ENABLE_TIME_PITCH=ON`。
- Produces: CMake 行为测试，验证 Feature Flag 的实际配置结果。

- [ ] **Step 1: 记录不可变基线**

在 `docs/qa/audio-editor-v2-baseline.md` 记录：当前提交、工作树 3 个用户修改文件、Release 构建结果、`60/56/4` 测试基线、4 个失败名称、`build/package` 的 237 文件与 111.32 MiB，以及上传 UI/文档绝对路径。

- [ ] **Step 2: 写失败的 Feature Flag 契约**

创建 `tests/cmake/audio_editor_feature_options_test.cmake`，由测试执行独立 CMake configure，并读取生成的 `CMakeCache.txt` 验证三个选项实际为 `BOOL=ON`；再以 `-DAG_ENABLE_NATIVE_RECORDING=OFF` 配置一次，验证显式关闭生效。测试若 configure 失败或 Cache 中值错误，使用 `message(FATAL_ERROR ...)` 返回失败。

- [ ] **Step 3: 运行并确认 RED**

```powershell
cmake -DSOURCE_DIR="$PWD" -DTEST_BINARY_DIR="$PWD/build/feature-options-red" -P tests/cmake/audio_editor_feature_options_test.cmake
```

Expected: FAIL，首个错误为 `Missing required feature option: AG_ENABLE_AUDIO_EDITOR`。

- [ ] **Step 4: 最小实现 Feature Flags**

在根 `CMakeLists.txt` 增加：

```cmake
option(AG_ENABLE_AUDIO_EDITOR "Enable audio editor" ON)
option(AG_ENABLE_NATIVE_RECORDING "Enable native recording" ON)
option(AG_ENABLE_TIME_PITCH "Enable time and pitch processing" ON)

if(NOT WIN32)
    set(AG_ENABLE_NATIVE_RECORDING OFF CACHE BOOL
        "Native recording is currently implemented on Windows" FORCE)
endif()
```

并在 `tests/CMakeLists.txt` 注册 `audio_editor_feature_options_test`。

- [ ] **Step 5: 验证 GREEN 与构建**

运行 CMake 行为测试和 Shared Build Commands；本任务只要求当前 56 个既有通过项继续通过，不把旧布局测试误报为新模块回归。

- [ ] **Step 6: Commit**

```powershell
git add CMakeLists.txt tests/CMakeLists.txt tests/cmake/audio_editor_feature_options_test.cmake docs/qa/audio-editor-v2-baseline.md
git commit -m "chore: establish audio editor v2 migration baseline"
```

---

### Task 2: 建立纯 C++ 单轨文档深模块

**Files:**
- Create: `core/src/audio_editor/audio_document.hpp`
- Create: `core/src/audio_editor/audio_document.cpp`
- Create: `core/src/audio_editor/edit_command.hpp`
- Create: `tests/core/audio_document_test.cpp`
- Modify: `core/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `using SampleFrame = std::int64_t`。
- Produces: `AudioSource`、`AudioSpan`、`Selection`、`Marker`、`AudioDocument`。
- `AudioDocument::apply(EditCommand)` 是所有剪辑的唯一提交 seam。

- [ ] **Step 1: 写文档模型 RED 测试**

```cpp
#include "audio_editor/audio_document.hpp"
#include <QtTest>

using namespace agplayer::editor;

class AudioDocumentTest final : public QObject {
    Q_OBJECT
private slots:
    void deleteSelectionUsesHalfOpenSampleFrames() {
        AudioDocument doc = AudioDocument::fromSource(
            AudioSource{"fixture.wav", 48'000, 2, 480'000});
        QVERIFY(doc.setSelection(Selection{48'000, 96'000}));
        QVERIFY(doc.apply(EditCommand::deleteSelection()));
        QCOMPARE(doc.totalFrames(), SampleFrame{432'000});
        QCOMPARE(doc.spans().size(), std::size_t{2});
        QVERIFY(doc.undo());
        QCOMPARE(doc.totalFrames(), SampleFrame{480'000});
        QVERIFY(doc.redo());
        QCOMPARE(doc.totalFrames(), SampleFrame{432'000});
    }

    void failedEditLeavesDocumentUnchanged() {
        AudioDocument doc = AudioDocument::fromSource(
            AudioSource{"fixture.wav", 44'100, 2, 44'100});
        const auto before = doc.snapshot();
        QVERIFY(!doc.apply(EditCommand::cropToSelection()));
        QCOMPARE(doc.snapshot(), before);
    }
};
```

再覆盖插入、替换、裁剪、静音、淡入/淡出、增益、归一化参数、剪贴板、标记移动、100 次撤销/重做和溢出边界。

- [ ] **Step 2: 运行并确认 RED**

```powershell
cmake --build build/release-msvc --target audio_document_test --config Release
```

Expected: FAIL，缺少 `audio_editor/audio_document.hpp`。

- [ ] **Step 3: 实现最小稳定接口**

```cpp
namespace agplayer::editor {
using SampleFrame = std::int64_t;

struct AudioSource {
    std::filesystem::path path;
    std::uint32_t sample_rate{};
    std::uint32_t channels{};
    SampleFrame total_frames{};
};

struct AudioSpan {
    std::shared_ptr<const AudioSource> source;
    SampleFrame source_start{};
    SampleFrame frame_count{};
    bool silent{};
    float gain_start{1.0F};
    float gain_end{1.0F};
};

struct Selection {
    SampleFrame start{};
    SampleFrame end{};
    [[nodiscard]] bool valid() const noexcept { return start >= 0 && end > start; }
};

class AudioDocument final {
public:
    static AudioDocument fromSource(AudioSource source);
    bool setSelection(Selection selection) noexcept;
    bool clearSelection() noexcept;
    bool apply(const EditCommand& command);
    bool undo();
    bool redo();
    [[nodiscard]] DocumentSnapshot snapshot() const;
    [[nodiscard]] SampleFrame totalFrames() const noexcept;
    [[nodiscard]] const std::vector<AudioSpan>& spans() const noexcept;
};
}
```

实现命令时先构造候选 Span 列表，全部校验通过后一次交换提交；Undo 记录结构差异，不复制 PCM。

- [ ] **Step 4: 验证 GREEN、边界与构建**

```powershell
ctest --test-dir build/release-msvc -C Release -R "audio_document_test" --output-on-failure
```

Expected: PASS；随后运行 Shared Build Commands。

- [ ] **Step 5: Commit**

```powershell
git add core/src/audio_editor core/CMakeLists.txt tests/core/audio_document_test.cpp tests/CMakeLists.txt
git commit -m "feat: add non-destructive single-track audio document"
```

---

### Task 3: 统一时间映射、峰值金字塔与视口

**Files:**
- Create: `core/src/audio_editor/time_pixel_mapper.hpp`
- Create: `core/src/audio_editor/peak_pyramid.hpp`
- Create: `core/src/audio_editor/peak_pyramid.cpp`
- Create: `tests/core/time_pixel_mapper_test.cpp`
- Create: `tests/core/peak_pyramid_test.cpp`
- Create: `qt/src/audio_editor/editor_viewport.hpp`
- Create: `qt/src/audio_editor/editor_viewport.cpp`
- Create: `qt/src/audio_editor/audio_editor_waveform_item.hpp`
- Create: `qt/src/audio_editor/audio_editor_waveform_item.cpp`
- Create: `tests/qt/editor_viewport_test.cpp`
- Modify: `core/CMakeLists.txt`
- Modify: `qt/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `TimePixelMapper::frameToPixel`、`pixelToFrame`、`visibleFrameRange`。
- Produces: `PeakPyramid::read(channel, start, count, pixelWidth)`。
- Produces: 单个 `EditorViewport` 实例，供主波形与缩略导航共享。

- [ ] **Step 1: 写精度 RED 测试**

```cpp
void TimePixelMapperTest::roundTripsAtFourKAndExtremeZoom() {
    TimePixelMapper mapper{SampleFrame{172'800'000}, 3'840.0, 64.0};
    for (SampleFrame frame : {0LL, 1LL, 47'999LL, 86'399'999LL, 172'799'999LL}) {
        const double pixel = mapper.frameToPixel(frame);
        QVERIFY(std::llabs(mapper.pixelToFrame(pixel) - frame) <= 1);
    }
}

void EditorViewportTest::overviewAndMainViewShareOneRange() {
    EditorViewport viewport;
    viewport.setDocumentFrames(480'000);
    viewport.setViewportWidth(1'200.0);
    viewport.setVisibleRange(120'000, 240'000);
    QCOMPARE(viewport.overviewStartRatio(), 0.25);
    QCOMPARE(viewport.overviewWidthRatio(), 0.25);
    viewport.moveOverviewWindow(0.5);
    QCOMPARE(viewport.visibleStartFrame(), SampleFrame{240'000});
}
```

- [ ] **Step 2: 运行并确认 RED**

Expected: FAIL，缺少 mapper、pyramid 和 viewport 类型。

- [ ] **Step 3: 实现统一映射和分级峰值**

`TimePixelMapper` 的构造输入只有文档帧数、逻辑像素宽度、每像素帧数；所有换算使用 checked 64-bit 整数和 double 仅作为显示中间值。`PeakPyramid` 复用 `waveform_analyzer`/`waveform_cache`，基础层分块生成，上一层每两个 bucket 合并 min/max，缓存 Key 包含规范路径、大小、mtime、版本、采样率和声道。

- [ ] **Step 4: 实现 Scene Graph 波形 Item**

`AudioEditorWaveformItem::updatePaintNode()` 每声道使用有限节点与动态顶点缓冲；接受已裁剪的可见峰值，不在渲染线程解码或扫描文件；叠加层坐标全部取自 `EditorViewport`。

- [ ] **Step 5: 验证 GREEN 与性能保护**

运行三个新测试、现有 `waveform_analyzer_test`、`waveform_cache_test`、`waveform_item_test`、`waveform_provider_test`；添加两小时虚拟时长测试，确认内存不随文档总帧数线性增长。

- [ ] **Step 6: Commit**

```powershell
git add core/src/audio_editor qt/src/audio_editor tests/core tests/qt core/CMakeLists.txt qt/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add sample-accurate editor waveform viewport"
```

---

### Task 4: Qt Session、统一 Actions 与上传 UI 骨架

**Files:**
- Create: `qt/src/audio_editor/audio_editor_controller.hpp`
- Create: `qt/src/audio_editor/audio_editor_controller.cpp`
- Create: `qt/src/audio_editor/editor_action_model.hpp`
- Create: `qt/src/audio_editor/editor_action_model.cpp`
- Create: `tests/qt/audio_editor_controller_test.cpp`
- Create: `app/qml/AgPlayer/components/tools/AudioEditorPage.qml`
- Create: `app/qml/AgPlayer/components/audioeditor/EditorCommandBar.qml`
- Create: `app/qml/AgPlayer/components/audioeditor/FileSummaryBar.qml`
- Create: `app/qml/AgPlayer/components/audioeditor/EditorWaveformCanvas.qml`
- Create: `app/qml/AgPlayer/components/audioeditor/OverviewNavigator.qml`
- Create: `app/qml/AgPlayer/components/audioeditor/RecordingInspectorSection.qml`
- Create: `app/qml/AgPlayer/components/audioeditor/TimePitchInspectorSection.qml`
- Create: `app/qml/AgPlayer/components/audioeditor/EditorTransportBar.qml`
- Create: `app/qml/AgPlayer/components/audioeditor/EditorStatusBar.qml`
- Create: `tests/qml/tst_audio_editor.qml`
- Create: `tests/qt/qml_audio_editor_test_main.cpp`
- Modify: `qt/src/qml_registration.hpp`
- Modify: `qt/src/qml_registration.cpp`
- Modify: `app/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces QML singleton: `AudioEditorController`。
- Produces list model roles: `id`, `text`, `tooltip`, `shortcut`, `icon`, `enabled`, `checked`, `busy`。
- Action IDs 完整固定：`editor.open`、`newRecording`、`save`、`undo`、`redo`、`cut`、`copy`、`paste`、`deleteSelection`、`cropToSelection`、`silenceSelection`、`fadeIn`、`fadeOut`、`moreMenu`、`export`。

- [ ] **Step 1: 写 Action 和布局 RED 测试**

```cpp
void AudioEditorControllerTest::emptyDocumentDisablesDocumentActions() {
    AudioEditorController controller;
    QVERIFY(controller.action("editor.open")->enabled());
    QVERIFY(!controller.action("editor.save")->enabled());
    QVERIFY(!controller.action("editor.cut")->enabled());
    QVERIFY(!controller.action("editor.export")->enabled());
}
```

```qml
function test_uploadedUiStructure() {
    compare(findChild(root, "audioEditorLeftSidebar"), null)
    verify(findChild(root, "editorCommandBar"))
    verify(findChild(root, "fileSummaryBar"))
    verify(findChild(root, "editorWaveformCanvas"))
    verify(findChild(root, "overviewNavigator"))
    verify(findChild(root, "recordingInspector"))
    verify(findChild(root, "timePitchInspector"))
    verify(findChild(root, "editorTransportBar"))
    verify(findChild(root, "editorStatusBar"))
}

function test_rightInspectorContainsOnlyTwoBusinessGroups() {
    var inspector = findChild(root, "editorInspector")
    compare(inspector.businessSectionCount, 2)
    compare(findChild(inspector, "inspectorRecordButton"), null)
    compare(findChild(inspector, "recordingSettingsGear"), null)
}
```

再断言 1280×720、1600×900、2560×1440、3840×2160 无页面级横向滚动；右栏折叠后波形宽度增加；空态无 Demo 值。

- [ ] **Step 2: 运行并确认 RED**

Expected: C++ 测试因类缺失失败；QML 测试因页面缺失失败。

- [ ] **Step 3: 实现 Controller 与 Action seam**

```cpp
enum class EditorSessionState {
    Empty, Ready, Playing, Recording, RecordingPaused, Finalizing,
    Previewing, Processing, Saving, Exporting, Error
};

class AudioEditorController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(EditorActionModel* actions READ actions CONSTANT)
    Q_PROPERTY(EditorViewport* viewport READ viewport CONSTANT)
    Q_PROPERTY(EditorSessionState state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool hasDocument READ hasDocument NOTIFY documentChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY documentChanged)
public:
    Q_INVOKABLE void triggerAction(const QString& id);
    Q_INVOKABLE bool openFile(const QUrl& url);
    Q_INVOKABLE bool setSelection(qint64 startFrame, qint64 endFrame);
};
```

所有 action enablement 只在 `refreshActions()` 计算一次，QML 只绑定模型角色。

- [ ] **Step 4: 严格实现上传 UI 骨架**

使用 `ColumnLayout + RowLayout`：命令栏 56 px、摘要 38 px、右栏 304 px、缩略条 72 px、Transport 104 px、状态栏 28 px；中央主波形 `Layout.fillWidth/fillHeight`。右栏仅两个折叠分组；文件摘要单行；导出不常驻右栏；L/R 与 dB 只作画布叠加。

- [ ] **Step 5: 验证 GREEN 与截图契约**

运行 `audio_editor_controller_test`、`qml_audio_editor_test` 和布局契约；通过 QA 参数抓取 4 个尺寸截图，逐项核对上传图的层级、比例、控件顺序和无左栏要求。全文残留扫描只作为清理核查，不作为自动化行为测试。

- [ ] **Step 6: Commit**

```powershell
git add qt/src/audio_editor qt/src/qml_registration.* app/CMakeLists.txt app/qml/AgPlayer/components/audioeditor app/qml/AgPlayer/components/tools/AudioEditorPage.qml tests
git commit -m "feat: build uploaded audio editor interface"
```

---

### Task 5: 事务式保存、导出与真实编辑渲染

**Files:**
- Create: `core/src/audio_editor/document_renderer.hpp`
- Create: `core/src/audio_editor/document_renderer.cpp`
- Create: `core/src/audio_editor/document_writer.hpp`
- Create: `core/src/audio_editor/document_writer.cpp`
- Create: `tests/core/document_writer_test.cpp`
- Create: `qt/src/audio_editor/export_controller.hpp`
- Create: `qt/src/audio_editor/export_controller.cpp`
- Create: `tests/qt/export_controller_test.cpp`
- Create: `app/qml/AgPlayer/components/audioeditor/ExportDialog.qml`
- Modify: `core/CMakeLists.txt`
- Modify: `qt/CMakeLists.txt`
- Modify: `app/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `DocumentSnapshot`、`Selection`、FFmpeg codec capability。
- Produces: `WritePlan validate(const WriteRequest&)`、`WriteResult execute(const WritePlan&, CancelToken&, ProgressSink&)`。

- [ ] **Step 1: 写安全保存 RED 测试**

```cpp
void DocumentWriterTest::failedValidationNeverTouchesOriginal() {
    const auto originalHash = sha256(originalPath);
    WriteRequest request{snapshot, originalPath, UnsupportedEncoder{"missing"}};
    const WriteResult result = writer.write(request);
    QCOMPARE(result.error, WriteError::UnsupportedEncoder);
    QCOMPARE(sha256(originalPath), originalHash);
    QVERIFY(!hasTemporarySibling(originalPath));
}

void DocumentWriterTest::selectionExportHasExactFrameCount() {
    WriteRequest request{snapshot, outputPath, WavPcm24{}, Selection{48'000, 96'000}};
    QVERIFY(writer.write(request).ok());
    QCOMPARE(probe(outputPath).frames, SampleFrame{48'000});
}
```

覆盖只读、磁盘不足模拟、目标占用、取消、Unicode 路径、元数据/封面策略、WAV/FLAC/MP3/AAC/OGG 当前真实能力、尾音 flush 和输出重解码。

- [ ] **Step 2: 运行并确认 RED**

Expected: FAIL，缺少 `document_writer.hpp`。

- [ ] **Step 3: 实现流式 Renderer 与原子 Writer**

Renderer 按 Span 分块读取/处理，`AVAudioFifo` 衔接编码帧；Writer 在目标同目录生成唯一临时文件，写完后 flush/close，重新 probe 并完整解码校验帧数与声道，再使用平台原子替换提交。任何错误删除临时文件并返回结构化错误。

- [ ] **Step 4: 实现独立 ExportDialog**

对话框包含范围、格式、采样率、位深/质量、声道、元数据/封面、目标路径；选项来自真实 codec capability。录音输出格式和保存目录不引用 ExportController。

- [ ] **Step 5: 验证 GREEN 与真实样本**

运行新测试和现有 `transcoder_test`、`format_matrix_test`；用真实本地 WAV/FLAC 执行剪切、淡化、增益、归一化、保存、选区导出，再用 FFmpeg 核心重新完整解码验证。

- [ ] **Step 6: Commit**

```powershell
git add core/src/audio_editor qt/src/audio_editor app/qml/AgPlayer/components/audioeditor tests core/CMakeLists.txt qt/CMakeLists.txt app/CMakeLists.txt
git commit -m "feat: add transactional audio document save and export"
```

---

### Task 6: Windows WASAPI 原生录音与恢复

**Files:**
- Create: `core/src/audio_editor/recording/recording_types.hpp`
- Create: `core/src/audio_editor/recording/spsc_audio_ring.hpp`
- Create: `core/src/audio_editor/recording/recording_journal.hpp`
- Create: `core/src/audio_editor/recording/recording_journal.cpp`
- Create: `core/src/audio_editor/recording/recording_session.hpp`
- Create: `core/src/audio_editor/recording/recording_session.cpp`
- Create: `core/src/audio_editor/recording/platform/wasapi_capture.hpp`
- Create: `core/src/audio_editor/recording/platform/wasapi_capture.cpp`
- Create: `tests/core/spsc_audio_ring_test.cpp`
- Create: `tests/core/recording_journal_test.cpp`
- Create: `tests/core/recording_session_test.cpp`
- Create: `qt/src/audio_editor/recording_controller.hpp`
- Create: `qt/src/audio_editor/recording_controller.cpp`
- Create: `tests/qt/recording_controller_test.cpp`
- Modify: `core/CMakeLists.txt`
- Modify: `qt/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `app/qml/AgPlayer/components/audioeditor/RecordingInspectorSection.qml`
- Modify: `app/qml/AgPlayer/components/audioeditor/EditorCommandBar.qml`
- Modify: `app/qml/AgPlayer/components/audioeditor/EditorTransportBar.qml`

**Interfaces:**
- Produces: `RecordingPreflightResult preflight(const RecordingRequest&)`。
- Produces: `start/pause/resume/stop/cancel/recover` 状态机。
- Produces: 真实设备、声道、采样率、电平、监听、格式和目录属性。

- [ ] **Step 1: 写无锁缓冲与状态机 RED 测试**

```cpp
void RecordingSessionTest::modeChangeDoesNotOpenDevice() {
    FakeCapture capture;
    RecordingSession session{capture, writer, journal};
    session.setMode(RecordingMode::InsertAtCursor);
    QCOMPARE(capture.openCount(), 0);
}

void RecordingSessionTest::deviceDisconnectPreservesRecoverableFrames() {
    session.start(validRequest);
    capture.push(testFrames);
    capture.disconnect();
    QCOMPARE(session.state(), RecordingState::ErrorRecoverable);
    QVERIFY(journal.recoverableFrameCount() >= testFrames.frameCount());
}
```

SPSC 测试覆盖 wrap、满/空、部分读写、双声道交错和 64-bit 累计帧数；Journal 覆盖序列化、截断尾记录与恢复。

- [ ] **Step 2: 运行并确认 RED**

Expected: FAIL，录音类型不存在。

- [ ] **Step 3: 实现 WASAPI adapter 与 Writer 线程**

使用 `IMMDeviceEnumerator`、`IAudioClient3`/`IAudioClient`、`IAudioCaptureClient`、event handle；回调线程只把已协商格式的帧写入预分配环形缓冲并更新原子峰值。Writer 线程分块编码临时录音源并同步 Journal；监听默认关闭，通过独立低延迟预览路径输出，禁止直接回授默认扬声器而不提示。

- [ ] **Step 4: 接入统一顶部/底部入口**

顶部 `editor.newRecording` 强制 `NewDocument` 请求；底部录音读取右栏当前模式。两者都调用 `RecordingController::begin(request)`，共享 preflight、session 和错误映射。右栏无录音按钮、无齿轮。

- [ ] **Step 5: 自动化与真实硬件 GREEN**

先用 FakeCapture 跑全部测试；再使用真实麦克风验证枚举、44.1/48 kHz 能力、单/立体声、电平、监听、暂停/继续、停止、Finalizing、设备拔出、权限/独占错误、恢复 WAV 与回放。记录设备名称但不硬编码到 UI。

- [ ] **Step 6: Commit**

```powershell
git add core/src/audio_editor/recording qt/src/audio_editor app/qml/AgPlayer/components/audioeditor tests core/CMakeLists.txt qt/CMakeLists.txt
git commit -m "feat: add recoverable native WASAPI recording"
```

---

### Task 7: BPM、速度与音高预览/应用

**Files:**
- Create: `core/src/audio_editor/time_pitch_session.hpp`
- Create: `core/src/audio_editor/time_pitch_session.cpp`
- Create: `tests/core/time_pitch_session_test.cpp`
- Create: `qt/src/audio_editor/time_pitch_controller.hpp`
- Create: `qt/src/audio_editor/time_pitch_controller.cpp`
- Create: `tests/qt/time_pitch_controller_test.cpp`
- Modify: `qt/src/audio_preview_controller.hpp`
- Modify: `qt/src/audio_preview_controller.cpp`
- Modify: `app/qml/AgPlayer/components/audioeditor/TimePitchInspectorSection.qml`
- Modify: `app/qml/AgPlayer/components/audioeditor/EditorStatusBar.qml`
- Modify: `core/CMakeLists.txt`
- Modify: `qt/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: 现有 BPM analyzer、SoundTouch、PreviewController、DocumentWriter。
- Produces: `TimePitchParameters`、`PreviewStatus`、`apply/cancel/resetAfterSeek`。

- [ ] **Step 1: 写参数和预览 RED 测试**

```cpp
void TimePitchSessionTest::targetBpmAndSpeedStayBidirectional() {
    TimePitchSession session;
    session.setOriginalBpm(100.0);
    session.setTargetBpm(125.0);
    QCOMPARE(session.speedPercent(), 125.0);
    session.setSpeedPercent(80.0);
    QCOMPARE(session.targetBpm(), 80.0);
}

void TimePitchSessionTest::previewNeverMutatesDocument() {
    const auto before = document.snapshot();
    session.preview(document, TimePitchParameters{1.25, true, 3, 25});
    QCOMPARE(document.snapshot(), before);
}
```

覆盖目标帧数、半音+音分换算、选区优先、Seek reset、延迟补偿、取消/失败不提交、应用后单次撤销。

- [ ] **Step 2: 运行并确认 RED**

Expected: FAIL，缺少 session。

- [ ] **Step 3: 复用现有处理能力并隔离预览**

参数变化重建内存 PreviewGraph，不写源文件和永久缓存；播放中自动预览，Seek 调用 SoundTouch clear/reset，`reportedLatencyFrames()` 供播放头补偿。应用时使用离线高质量渲染，成功后生成一个 RenderedSource Span 命令。

- [ ] **Step 4: 完成右栏真实交互**

实现 BPM 检测、原始 BPM、目标 BPM、速度、保持音调、半音、音分和“应用处理”；状态栏显示“预览中/参数待应用/处理中”；`Esc` 取消预览或可取消处理。

- [ ] **Step 5: 验证 GREEN 与音频正确性**

使用固定 BPM、1 kHz 正弦和人声/鼓点样本验证时长比例、保持音高主频、±12 半音频率比、无 NaN/Inf、尾音完整和处理后重开一致。

- [ ] **Step 6: Commit**

```powershell
git add core/src/audio_editor/time_pitch_session.* qt/src/audio_editor qt/src/audio_preview_controller.* app/qml/AgPlayer/components/audioeditor tests core/CMakeLists.txt qt/CMakeLists.txt
git commit -m "feat: add reversible BPM speed and pitch processing"
```

---

### Task 8: 切换导航入口并彻底删除旧多轨模块

**Files:**
- Modify: `app/qml/AgPlayer/AudioToolsWindow.qml`
- Modify: `app/qml/AgPlayer/components/tools/ToolSidebar.qml`
- Modify: `app/main.cpp`
- Modify: `qt/src/audio_tools_controller.*`
- Modify: `qt/src/qml_registration.*`
- Modify: `core/include/agplayer/c_api.h`
- Modify: `core/src/c_api.cpp`
- Modify: `core/CMakeLists.txt`
- Modify: `qt/CMakeLists.txt`
- Modify: `app/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `translations/agplayer_zh.ts`
- Modify: all four retained translation catalogs
- Delete: `app/qml/AgPlayer/components/tools/LightEditPage.qml`
- Delete: `app/qml/AgPlayer/components/tools/MultiTrackWaveform.qml`
- Delete: `qt/src/light_editor_controller.hpp`
- Delete: `qt/src/light_editor_controller.cpp`
- Delete: `core/src/light_editor.hpp`
- Delete: `core/src/light_editor.cpp`
- Delete: `core/src/multitrack_editor.hpp`
- Delete: `core/src/multitrack_editor.cpp`
- Delete: `core/src/timeline_preview_mixer.hpp`
- Delete: `core/src/timeline_preview_mixer.cpp`
- Delete: `tests/core/light_editor_test.cpp`
- Delete: `tests/core/multitrack_editor_test.cpp`
- Delete: `tests/core/timeline_preview_mixer_test.cpp`
- Delete: `tests/qt/light_editor_controller_test.cpp`
- Delete: `tests/qml/tst_light_editor.qml`
- Delete: `tests/qt/qml_light_editor_test_main.cpp`
- Replace: `tests/scripts/audio_tools_layout_contract_test.ps1`

**Interfaces:**
- Produces: 工具索引固定 `0=音频编辑, 1=格式转换, 2=元数据修改, 3=文件名处理`。
- Removes: 所有 `ag_light_edit*`、`ag_multitrack_edit*` 与 timeline mixer C API/注册。

- [ ] **Step 1: 写旧路径不可链接的迁移 RED**

在 `tests/core/c_api_lifecycle_test.cpp` 移除旧函数调用并增加仅使用 AudioEditorV2 seam 的链接覆盖；在 `tests/qml/tst_audio_editor.qml` 断言索引 0 的运行时页面为 `AudioEditorPage`、可访问名称为“音频编辑”、没有旧六轨对象或旧多轨 Action。测试通过的前提是应用不再构造或注册旧模块。

- [ ] **Step 2: 运行并确认 RED**

Expected: FAIL，当前索引 0 仍构造旧页面或旧 QML singleton 仍被测试夹具要求。

- [ ] **Step 3: 先切换入口与未保存保护**

`AudioToolsWindow` 索引 0 改为 `AudioEditorPage`；`ToolSidebar` 首项文字为“音频编辑”。切换前调用 `AudioEditorController.requestToolSwitch(target)`：修改文档显示保存/放弃/取消；Finalizing/原子提交不可安全取消时阻止切换并显示原因。

- [ ] **Step 4: 删除旧模块与残留**

删除清单中的源文件和测试；移除 C API、实例创建、QML singleton、CMake/qrc、翻译、设置、图标引用和旧 QA 参数。仅在 `AudioEditorV2` 覆盖对应行为后删除旧测试。

- [ ] **Step 5: 验证 GREEN、全文残留与干净构建**

```powershell
rg -n "轻度剪辑|LightEdit|light_editor|MultiTrack|multitrack|timeline_preview_mixer" core qt app tests translations CMakeLists.txt
```

Expected: 无生产残留；允许迁移设计/历史文档。删除 `build/release-msvc` 之外新建独立干净构建目录，重新 configure、Release build、serial ctest，确保不是增量构建掩盖依赖。

- [ ] **Step 6: Commit**

```powershell
git add -A core qt app tests translations CMakeLists.txt
git commit -m "refactor: replace legacy light editor with audio editor v2"
```

---

### Task 9: 完整回归、实机 UI/音频验收与交付证据

**Files:**
- Create: `docs/qa/audio-editor-v2-final-report.md`
- Create: `docs/qa/audio-editor-v2-user-guide.md`
- Create: `docs/qa/audio-editor-v2-third-party-licenses.md`
- Create: `docs/qa/screenshots/audio-editor-1280x720.png`
- Create: `docs/qa/screenshots/audio-editor-1600x900.png`
- Create: `docs/qa/screenshots/audio-editor-2560x1440.png`
- Create: `docs/qa/screenshots/audio-editor-3840x2160.png`
- Modify: `docs/qa/audio-editor-v2-baseline.md`

**Interfaces:**
- Produces: 可复核的最终证据，不新增生产接口。

- [ ] **Step 1: 运行全量静态与自动化门**

运行 `git diff --check`、干净 Release build、`ctest -j 1 --output-on-failure`；要求零编译错误、零警告、零测试失败。运行源码残留契约、翻译目录检查和安装/运行时契约。

- [ ] **Step 2: 真实 UI 验收**

使用实际 `AgPlayer.exe` 抓取四个尺寸截图；对照上传图逐项核对：导航顺序、命令栏、单行摘要、无左栏、中央大波形、缩略条、右栏仅两组、无右栏录音按钮/齿轮、独立导出、Transport、状态栏、折叠扩展和无横向滚动。

- [ ] **Step 3: 真实音频编辑验收**

使用 WAV、FLAC、MP3、AAC、OGG 样本执行打开、播放、Seek、选择、剪切、复制、粘贴、删除、裁剪、静音、淡入/淡出、插入静音、增益、归一化、撤销/重做、保存和全文/选区导出；重新解码全部输出并记录帧数、声道、采样率与错误。

- [ ] **Step 4: 真实录音与恢复验收**

使用实际麦克风验证新建/插入、暂停/继续、监听、电平、停止、Finalizing、回放；在隔离 QA 目录模拟异常终止后验证 Journal 恢复，不破坏用户真实录音目录。

- [ ] **Step 5: 性能与体积**

记录启动、5 分钟首屏波形、完整峰值、缓存命中、2 小时文件内存峰值、4K 波形 FPS、播放/录音/预览 CPU、处理倍率、导出速度、取消响应。用与基线相同的目录/配置统计文件数与 MiB；增量超过 5 MiB 时按二进制/资源逐项分析并裁剪后重测。

- [ ] **Step 6: 许可证与用户文档**

列出 Qt、FFmpeg、SoundTouch、miniaudio 及本模块实际链接方式和许可证；确认无新增 GPL 不兼容代码。用户指南覆盖打开、选择、编辑、录音、速度音高、保存导出、恢复与错误处理。

- [ ] **Step 7: 最终报告与 Commit**

报告必须包含阶段目标、复用组件、增删改文件、架构决定、命令、实际结果、限制、旧模块删除清单、体积、性能、第三方许可、回归结果和未完成项；没有未完成项时明确写“无”。

```powershell
git add docs/qa
git commit -m "docs: publish audio editor v2 verification evidence"
```

## Plan Self-Review

- Spec coverage: 设计规格第 1–11 节均由 Task 1–9 对应。
- UI coverage: 上传图八个职责区、导航冲突裁决、右栏限制、响应式尺寸和真实值均有自动化与实机门。
- Type consistency: 全计划统一使用 `SampleFrame`、`DocumentSnapshot`、`EditorViewport`、`EditorSessionState`、`WriteRequest/WriteResult`。
- Deletion safety: 新模块在 Task 2–7 验收，旧模块仅 Task 8 删除。
- Placeholder scan: 生产交付不包含未实现按钮、伪设备、Demo 数值或平台假能力。
- User changes: 三个既有未提交文件明确保留，不进入本计划提交。
