# AgPlayer Startup, Editor, and Localization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 精确实现 1228×424 空白启动页、中文/英文/泰语/越南语切换，以及最多六轨且支持 BPM 统一、节拍对齐、拖动、裁剪、吸附和滚轮缩放的轻度剪辑。

**Architecture:** 保留现有 QML 视图、Qt 控制器和 C++17 音频核心分层。新增纯函数时间线数学模块；扩展 `LightEditor` 为六轨编辑状态与异步 BPM 处理入口；扩展多轨渲染器接受时间线偏移；使用 Qt Linguist 与 `QTranslator` 实现四语言运行时切换。

**Tech Stack:** Qt 6.7、QML、C++17、Qt Concurrent、Qt Linguist、FFmpeg、CTest、Qt Quick Test。

## Global Constraints

- 空白启动页默认窗口严格为 1228×424。
- 仅支持 `zh`、`en`、`th`、`vi`。
- 轻度剪辑最多六轨，原文件保持不变。
- 目标 BPM 范围 40–240，变速比例限定 0.5–2.0。
- 片段最短时长 200ms；默认按 1/4 拍网格吸附。
- 每项行为先写失败测试，再写最小实现。
- 每阶段完成后构建、测试、截图、自检和修复。
- 不封装 EXE。
- 保留工作区中与本计划无关的既有修改，不回退、不覆盖。

---

### Task 1: 1228×424 空白启动页与图标状态

**Files:**
- Modify: `app/qml/AgPlayer/Main.qml`
- Modify: `app/qml/AgPlayer/components/EmptyStartup.qml`
- Modify: `app/qml/AgPlayer/components/PlayerControls.qml`
- Modify: `app/qml/AgPlayer/components/TitleBar.qml`
- Modify: `tests/qml/tst_main_window.qml`
- Modify: `design-qa.md`

**Interfaces:**
- Consumes: `Theme`, `LibraryModel.count`, `WindowController`
- Produces: `Main.width == 1228`, `Main.height == 424` 的空白启动状态

- [ ] **Step 1: 写失败的窗口尺寸测试**

在 `tests/qml/tst_main_window.qml` 中将空白启动页断言改为：

```qml
compare(window.width, 1228)
compare(window.height, 424)
compare(emptyStartup.visible, true)
compare(playerControls.emptyMode, true)
```

- [ ] **Step 2: 验证测试因当前 1540 宽度而失败**

Run:

```powershell
ctest --test-dir build/msvc-debug -R "^qml_main_window_test$" --output-on-failure
```

Expected: `window.width` 期望 1228、实际 1540。

- [ ] **Step 3: 实现新窗口比例**

在 `Main.qml` 设置：

```qml
width: 1228
height: 424
minimumWidth: 800
minimumHeight: 360
```

保持 `EmptyStartup` 顶部内容居中；底部控制组宽度按可用宽度收缩，不改变 424 高度下的垂直坐标。

- [ ] **Step 4: 修复 Basic 样式的图标底色**

对启动页底部的纯图标按钮使用：

```qml
background: null
flat: true
```

用显式 `HoverHandler` 和 `activeFocus` 焦点环提供交互反馈，不允许普通状态出现灰色方块。

- [ ] **Step 5: 验证自动测试与截图**

Run:

```powershell
cmake --build build/msvc-debug --target AgPlayer qml_main_window_test -j 4
ctest --test-dir build/msvc-debug -R "^qml_main_window_test$" --output-on-failure
build/msvc-debug/app/AgPlayer.exe --qa-screenshot-main build/qa/empty-startup-dark.png
```

使用 `tools/make_comparison.py` 将最新参考图裁切为 1228×424，与实现截图并排输出至 `build/qa/empty-startup-comparison.png`。

- [ ] **Step 6: 更新视觉 QA 并提交**

`design-qa.md` 必须记录相同视口、P0/P1/P2 为零和 `final result: passed`。

Commit:

```powershell
git add app/qml/AgPlayer/Main.qml app/qml/AgPlayer/components/EmptyStartup.qml app/qml/AgPlayer/components/PlayerControls.qml app/qml/AgPlayer/components/TitleBar.qml tests/qml/tst_main_window.qml design-qa.md
git commit -m "feat: match compact empty startup"
```

---

### Task 2: 四语言运行时翻译

**Files:**
- Create: `app/translation_manager.hpp`
- Create: `app/translation_manager.cpp`
- Create: `translations/agplayer_zh.ts`
- Create: `translations/agplayer_en.ts`
- Create: `translations/agplayer_th.ts`
- Create: `translations/agplayer_vi.ts`
- Modify: `app/main.cpp`
- Modify: `app/CMakeLists.txt`
- Modify: `CMakeLists.txt`
- Modify: `app/qml/AgPlayer/SettingsPage.qml`
- Modify: `qt/src/settings_controller.cpp`
- Modify: `tests/qt/settings_controller_test.cpp`
- Create: `tests/qt/translation_manager_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `SettingsController::languageChanged`
- Produces: `TranslationManager::apply(const QString&) -> bool`

- [ ] **Step 1: 写失败的语言白名单测试**

在 `settings_controller_test.cpp` 增加：

```cpp
void SettingsControllerTest::supportsOnlyFourLanguages()
{
    SettingsController settings;
    for (const QString& code : {u"zh"_s, u"en"_s, u"th"_s, u"vi"_s}) {
        settings.setLanguage(code);
        QCOMPARE(settings.language(), code);
    }
    settings.setLanguage(QStringLiteral("ko"));
    QCOMPARE(settings.language(), QStringLiteral("zh"));
}
```

- [ ] **Step 2: 验证旧白名单使韩语测试失败**

Run:

```powershell
cmake --build build/msvc-debug --target settings_controller_test -j 4
ctest --test-dir build/msvc-debug -R "^settings_controller_test$" --output-on-failure
```

Expected: `ko` 未回退为 `zh`。

- [ ] **Step 3: 收敛语言校验与设置选项**

`validatedLanguage()` 使用：

```cpp
static const QStringList supported = {
    QStringLiteral("zh"), QStringLiteral("en"),
    QStringLiteral("th"), QStringLiteral("vi")
};
```

设置页下拉框仅显示：

```qml
[
    { text: qsTr("简体中文"), value: "zh" },
    { text: qsTr("English"), value: "en" },
    { text: qsTr("ไทย"), value: "th" },
    { text: qsTr("Tiếng Việt"), value: "vi" }
]
```

- [ ] **Step 4: 写失败的翻译管理器测试**

`translation_manager_test.cpp` 验证：

```cpp
TranslationManager manager(*qApp);
QVERIFY(manager.apply(QStringLiteral("zh")));
QVERIFY(manager.apply(QStringLiteral("en")));
QVERIFY(manager.apply(QStringLiteral("th")));
QVERIFY(manager.apply(QStringLiteral("vi")));
QVERIFY(!manager.apply(QStringLiteral("ko")));
```

- [ ] **Step 5: 实现 TranslationManager**

公开接口：

```cpp
class TranslationManager final : public QObject {
    Q_OBJECT
public:
    explicit TranslationManager(QCoreApplication& app, QObject* parent = nullptr);
    bool apply(const QString& language);
signals:
    void languageApplied(const QString& language);
private:
    QCoreApplication& app_;
    QTranslator translator_;
};
```

`apply()` 先移除旧翻译；`zh` 使用源码中文或中文 QM，其他语言从 `:/i18n/agplayer_<code>.qm` 加载。未知代码返回 `false`。

- [ ] **Step 6: 接入 QML 重翻译**

在 `main.cpp`：

```cpp
TranslationManager translations(app);
translations.apply(settings.language());
QObject::connect(&settings, &SettingsController::languageChanged, &app,
                 [&settings, &translations, &engine]() {
    translations.apply(settings.language());
    engine.retranslate();
});
```

确保连接建立在 `QQmlApplicationEngine engine` 创建之后。

- [ ] **Step 7: 配置 Qt Linguist**

根 `CMakeLists.txt` 的 Qt 组件加入 `LinguistTools`。`app/CMakeLists.txt` 增加四个 TS 文件和：

```cmake
qt_add_translations(AgPlayer
    TS_FILES ${AGPLAYER_TS_FILES}
    RESOURCE_PREFIX "/i18n"
)
```

- [ ] **Step 8: 生成并校对四语言内容**

运行 Qt `lupdate` 更新四个 TS 文件，翻译主窗口、设置、音频工具、轻度剪辑与通用错误字符串；不得保留 `unfinished`。

- [ ] **Step 9: 构建、测试并提交**

Run:

```powershell
cmake --build build/msvc-debug --target AgPlayer settings_controller_test translation_manager_test -j 4
ctest --test-dir build/msvc-debug -R "settings_controller_test|translation_manager_test" --output-on-failure
```

Commit:

```powershell
git add CMakeLists.txt app translations qt/src/settings_controller.cpp tests
git commit -m "feat: add four-language runtime translations"
```

---

### Task 3: 时间线数学与片段边界

**Files:**
- Create: `qt/src/editor_timeline_math.hpp`
- Create: `qt/src/editor_timeline_math.cpp`
- Create: `tests/qt/editor_timeline_math_test.cpp`
- Modify: `qt/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `qint64 beatGridMs(double bpm, int denominator)`
  - `qint64 snapMs(qint64 value, double bpm, int denominator)`
  - `ClipBounds normalizeClip(ClipBounds clip, qint64 sourceDurationMs)`

- [ ] **Step 1: 写失败的吸附与边界测试**

测试：

```cpp
QCOMPARE(beatGridMs(120.0, 4), 500);
QCOMPARE(snapMs(740, 120.0, 4), 500);
QCOMPARE(snapMs(760, 120.0, 4), 1000);

ClipBounds clip{1000, 900, 950};
const ClipBounds normalized = normalizeClip(clip, 1000);
QCOMPARE(normalized.inMs, 800);
QCOMPARE(normalized.outMs, 1000);
QCOMPARE(normalized.timelineStartMs, 1000);
```

- [ ] **Step 2: 验证链接因函数不存在而失败**

Run:

```powershell
cmake --build build/msvc-debug --target editor_timeline_math_test -j 4
```

Expected: 未定义 `beatGridMs`、`snapMs`、`normalizeClip`。

- [ ] **Step 3: 实现纯函数**

```cpp
qint64 beatGridMs(double bpm, int denominator)
{
    if (bpm < 40.0 || bpm > 240.0 || denominator <= 0) return 0;
    return qRound64(240000.0 / (bpm * denominator));
}

qint64 snapMs(qint64 value, double bpm, int denominator)
{
    const qint64 grid = beatGridMs(bpm, denominator);
    if (grid <= 0) return qMax<qint64>(0, value);
    return qMax<qint64>(0, qRound64(double(value) / grid) * grid);
}
```

`normalizeClip()` 将入点、出点限制到素材时长，并保证最短 200ms。

- [ ] **Step 4: 测试并提交**

Run:

```powershell
ctest --test-dir build/msvc-debug -R "^editor_timeline_math_test$" --output-on-failure
```

Commit:

```powershell
git add qt/src/editor_timeline_math.* qt/CMakeLists.txt tests/qt/editor_timeline_math_test.cpp tests/CMakeLists.txt
git commit -m "feat: add editor timeline math"
```

---

### Task 4: 六轨编辑状态控制器

**Files:**
- Modify: `qt/src/light_editor_controller.hpp`
- Modify: `qt/src/light_editor_controller.cpp`
- Create: `tests/qt/light_editor_controller_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `Q_PROPERTY(QVariantList tracks READ tracks NOTIFY tracksChanged)`
  - `Q_PROPERTY(double targetBpm READ targetBpm WRITE setTargetBpm NOTIFY targetBpmChanged)`
  - `Q_PROPERTY(bool snapEnabled READ snapEnabled WRITE setSnapEnabled NOTIFY snapEnabledChanged)`
  - `Q_INVOKABLE bool moveClip(int trackIndex, qint64 timelineStartMs)`
  - `Q_INVOKABLE bool trimClip(int trackIndex, qint64 inMs, qint64 outMs)`
  - `Q_INVOKABLE void setTrackMuted(int trackIndex, bool)`
  - `Q_INVOKABLE void setTrackSolo(int trackIndex, bool)`
  - `Q_INVOKABLE void setTrackLocked(int trackIndex, bool)`

- [ ] **Step 1: 写失败的六轨与编辑规则测试**

测试 `trackCount() == 6`，并验证：

- 未锁定片段可移动。
- 开启吸附后 740ms 在 120 BPM 下变为 500ms。
- 锁定片段移动失败且状态不变。
- 裁剪结果始终保持至少 200ms。
- 目标 BPM 低于 40 或高于 240 被拒绝。

- [ ] **Step 2: 验证当前四轨控制器失败**

Run:

```powershell
cmake --build build/msvc-debug --target light_editor_controller_test -j 4
ctest --test-dir build/msvc-debug -R "^light_editor_controller_test$" --output-on-failure
```

Expected: `trackCount()` 实际为 4。

- [ ] **Step 3: 扩展 Track**

```cpp
struct Track {
    QString path;
    QString renderPath;
    QString name;
    QString format;
    qint64 durationMs = 0;
    qint64 timelineStartMs = 0;
    qint64 inMs = 0;
    qint64 outMs = 0;
    int fadeInMs = 0;
    int fadeOutMs = 0;
    double gain = 1.0;
    double originalBpm = 0.0;
    double bpmConfidence = 0.0;
    double speedRatio = 1.0;
    bool muted = false;
    bool solo = false;
    bool locked = false;
    bool aligned = false;
    int sampleRate = 0;
    int channels = 0;
    QVariantList peaks;
};
```

将 `kTrackCount` 改为 6。`tracks()` 为每轨返回稳定键名的 `QVariantMap`。

- [ ] **Step 4: 实现移动、裁剪、静音、独奏和锁定**

所有修改通过索引校验；锁定时拒绝几何修改；成功修改仅发射一次 `tracksChanged()`。

- [ ] **Step 5: 测试并提交**

Run:

```powershell
ctest --test-dir build/msvc-debug -R "^light_editor_controller_test$" --output-on-failure
```

Commit:

```powershell
git add qt/src/light_editor_controller.* tests/qt/light_editor_controller_test.cpp tests/CMakeLists.txt
git commit -m "feat: model six-track light edits"
```

---

### Task 5: 新版轻度剪辑时间线 UI

**Files:**
- Modify: `app/qml/AgPlayer/AudioToolsWindow.qml`
- Replace: `app/qml/AgPlayer/components/tools/LightEditPage.qml`
- Replace: `app/qml/AgPlayer/components/tools/MultiTrackWaveform.qml`
- Create: `tests/qml/tst_light_editor.qml`
- Create: `tests/qt/qml_light_editor_test_main.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `app/CMakeLists.txt`
- Modify: `design-qa.md`

**Interfaces:**
- Consumes: Task 4 的 `LightEditor.tracks` 与编辑方法
- Produces: 可操作六轨时间线

- [ ] **Step 1: 写失败的 QML 交互测试**

测试对象名：

```qml
verify(findChild(page, "targetBpmField"))
verify(findChild(page, "snapGridBox"))
verify(findChild(page, "unifyBpmButton"))
verify(findChild(page, "alignBpmButton"))
compare(findChildren(page, "trackLane").length, 6)
```

模拟滚轮后断言 `zoomScale` 增大；调用片段拖动接口后断言时间线起点改变。

- [ ] **Step 2: 验证旧页面缺少新版控件而失败**

Run:

```powershell
cmake --build build/msvc-debug --target qml_light_editor_test -j 4
ctest --test-dir build/msvc-debug -R "^qml_light_editor_test$" --output-on-failure
```

- [ ] **Step 3: 按参考图重建页面骨架**

窗口尺寸 1536×1024，左侧导航 180px；顶部栏包含添加文件、主轨、目标 BPM、保持音高、吸附网格、统一 BPM、BPM+节拍对齐。

时间线包含共享双层标尺、六轨区域、拖入空轨提示、工具栏、播放控制、导出设置和项目信息。

- [ ] **Step 4: 实现片段交互**

`MultiTrackWaveform.qml` 提供：

```qml
signal clipMoveRequested(int trackIndex, int timelineStartMs)
signal clipTrimRequested(int trackIndex, int inMs, int outMs)
signal seekRequested(int positionMs)
```

主体 `DragHandler` 移动片段；左右手柄分别裁剪；锁定时禁用。QML 将像素转换为毫秒，C++ 再执行边界和吸附校验。

- [ ] **Step 5: 实现滚轮缩放和平移**

`WheelHandler` 以鼠标位置对应的毫秒为锚点：

```qml
const anchorMs = viewportOffsetMs + event.position.x / pixelsPerMs
zoomScale = Math.max(0.25, Math.min(64.0, zoomScale * factor))
viewportOffsetMs = Math.max(0, anchorMs - event.position.x / pixelsPerMs)
```

`Shift + 滚轮` 改变 `viewportOffsetMs`，不改变缩放。

- [ ] **Step 6: 使用真实图标**

所有操作按钮使用现有 Remix Icon 资源或补充同库图标；不得使用 Unicode 字符模拟图标。

- [ ] **Step 7: 测试、截图和提交**

Run:

```powershell
ctest --test-dir build/msvc-debug -R "^qml_light_editor_test$" --output-on-failure
```

生成与参考图相同 1536×1024 的实现截图和并排对比；修复 P0/P1/P2 后更新 `design-qa.md`。

Commit:

```powershell
git add app/qml/AgPlayer/AudioToolsWindow.qml app/qml/AgPlayer/components/tools app/CMakeLists.txt tests design-qa.md assets/icons
git commit -m "feat: build interactive six-track editor"
```

---

### Task 6: BPM 统一、节拍对齐与时间线导出

**Files:**
- Modify: `core/src/multitrack_editor.hpp`
- Modify: `core/src/multitrack_editor.cpp`
- Modify: `core/include/agplayer/agplayer.h`
- Modify: `core/src/c_api.cpp`
- Modify: `qt/src/light_editor_controller.hpp`
- Modify: `qt/src/light_editor_controller.cpp`
- Modify: `tests/core/multitrack_editor_test.cpp`
- Modify: `tests/qt/light_editor_controller_test.cpp`

**Interfaces:**
- Extends: `MultiTrackEditConfig::Track::timeline_start_ms`
- Produces:
  - `Q_INVOKABLE void analyzeTrackBpm(int trackIndex)`
  - `Q_INVOKABLE void unifyBpm(bool alignBeats)`
  - `Q_INVOKABLE void exportProject(const QString&, const QString&, int, int)`

- [ ] **Step 1: 写失败的时间线偏移渲染测试**

构造两条相同音频，第二轨 `timeline_start_ms = 1000`；导出文件时长必须比单轨至少长 900ms。

- [ ] **Step 2: 验证当前渲染器忽略偏移而失败**

Run:

```powershell
cmake --build build/msvc-debug --target multitrack_editor_test -j 4
ctest --test-dir build/msvc-debug -R "^multitrack_editor_test$" --output-on-failure
```

- [ ] **Step 3: 扩展多轨渲染配置**

```cpp
struct Track {
    std::string input_path;
    long long timeline_start_ms = 0;
    long long trim_start_ms = 0;
    long long trim_end_ms = 0;
    int fade_in_ms = 0;
    int fade_out_ms = 0;
    double gain = 1.0;
};
```

混音时在主时间线上插入对应静音帧；总时长取所有片段结束位置最大值。

- [ ] **Step 4: 写失败的 BPM 统一测试**

加载已知 90 BPM 测试音频，设目标 120 BPM；断言速度比例为 `120/90`，处理成功后 `aligned == true` 且 `renderPath` 指向缓存文件。

- [ ] **Step 5: 实现每轨 BPM 分析**

复用 `ag_bpm_analyze`，保存 BPM 与置信度；检测失败时保留原素材并发射：

```cpp
void trackError(int trackIndex, const QString& message);
```

- [ ] **Step 6: 实现异步 BPM 统一**

对已加载且 BPM 有效的轨道调用 `ag_pitch_shift_ex`：

```cpp
const double speedRatio = targetBpm / originalBpm;
const double tempoRatio = 1.0 / speedRatio;
```

保持音高时 `keep_tempo = 1`。缓存文件使用 WAV，全部成功后一次性更新 `renderPath`；取消或任一失败时删除本次临时文件。

- [ ] **Step 7: 实现节拍对齐**

`alignBeats == true` 时将每轨 `timelineStartMs` 通过 `snapMs(..., targetBpm, 4)` 对齐；状态标为 `aligned`。没有可靠 BPM 的轨道不标记成功。

- [ ] **Step 8: 将轨道状态映射到导出**

每轨分别传入 `renderPath/path`、时间线起点、入点、出点、淡入、淡出和有效增益；静音轨和被独奏排除的轨不进入配置。

- [ ] **Step 9: 运行核心与控制器测试并提交**

Run:

```powershell
ctest --test-dir build/msvc-debug -R "multitrack_editor_test|light_editor_controller_test|bpm_analyzer_test|pitch_shifter_test" --output-on-failure
```

Commit:

```powershell
git add core qt/src/light_editor_controller.* tests
git commit -m "feat: align bpm and render timeline clips"
```

---

### Task 7: 集成回归、真实音频验证与索引

**Files:**
- Modify: `design-qa.md`
- Modify: `docs/qa/phase-status.md`

**Interfaces:**
- Consumes: Tasks 1–6
- Produces: 可复现的阶段验收记录

- [ ] **Step 1: 全量构建**

Run:

```powershell
cmake --build build/msvc-debug -j 4
```

Expected: 0 个编译错误，0 个新增警告。

- [ ] **Step 2: 非压力测试**

Run:

```powershell
ctest --test-dir build/msvc-debug -E "^library_stress_test$" --output-on-failure -j 4
```

Expected: 全部通过。

- [ ] **Step 3: 压力测试**

Run:

```powershell
ctest --test-dir build/msvc-debug -R "^library_stress_test$" --output-on-failure
```

Expected: 通过且无崩溃、死锁或超时。

- [ ] **Step 4: 四语言与三主题视觉检查**

依次检查 `zh/en/th/vi` × `dark/light/system`；启动页和编辑器不得出现截断、重叠、错误图标颜色。

- [ ] **Step 5: 真实音频工作流**

导入三条不同 BPM 的真实音频，执行：

1. 分析 BPM。
2. 设定共同目标 BPM。
3. 执行 BPM+节拍对齐。
4. 拖动、裁剪、吸附、缩放。
5. 导出 WAV、MP3、FLAC。
6. 用 AgPlayer 重新播放三个输出并核对时长、声道和可听结果。

- [ ] **Step 6: 更新 QA 文档**

`design-qa.md` 最终必须为 `final result: passed`。`docs/qa/phase-status.md` 记录测试数、截图路径、真实音频样本和已知 P3 项。

- [ ] **Step 7: 重新索引代码图**

对 `D:\ai\AgPlayer\.worktrees\revised-ui` 执行 moderate 模式索引，项目名 `agplayer_revised_ui_dev`，并保存持久化图。

- [ ] **Step 8: 最终提交**

```powershell
git add design-qa.md docs/qa/phase-status.md
git commit -m "test: verify editor localization workflow"
```

不运行安装包或 EXE 封装步骤。
