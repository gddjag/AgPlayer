# AgPlayer Player Window and Waveform Repair Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复用户列出的 10 项播放器窗口、列表、波形、频谱、菜单、Windows 身份和 EQ 问题，并在真实 UI/音频门禁通过后生成可验证的 EXE。

**Architecture:** 保留现有 Qt 6/C++/QML 分层。QML 负责响应式尺寸和交互，`PlaybackController` 提供唯一播放时间轴，`WaveformItem` 负责绘制，`WindowController` 负责 Win32 跨屏和层级，现有打包脚本负责发布。

**Tech Stack:** C++20、Qt 6 Quick/Controls/Test、CMake/CTest、Win32、PowerShell。

## Global Constraints

- 工作树固定为 `D:\ai\AgPlayer\.worktrees\revised-ui`，保留所有既有未提交修改。
- “默认显示 10 首”是 10 条数据行，不含表头、搜索栏和分页栏。
- 主窗口默认 `1104x342`，最小 `612x270`；EQ 为 `520x307`。
- 频谱柱宽 `5px`、间距 `2px`、Attack `0.02s`、Decay `0.10s`、Peak Fall `0.35s`。
- 播放竖线为克莱因蓝 `#002FA7`、宽 `1px`。
- 不引入依赖，不重构无关模块，不清除用户设置。
- 真实 UI、真实歌曲播放、跨屏层级和安装后启动验证全部通过前不得宣称 EXE 完成。

---

### Task 1: 建立当前失败基线并锁定测试目标

**Files:**
- Test: `tests/qml/tst_main_window.qml`
- Test: `tests/qt/playback_controller_test.cpp`
- Test: `tests/qt/waveform_item_test.cpp`
- Test: `tests/qt/window_controller_test.cpp`

**Interfaces:**
- Consumes: 当前 56 个 CTest 目标和既有 QML QA 命令行入口。
- Produces: 本轮每项缺陷的可重复失败证据；已满足的行为只保留验证，不重复修改。

- [ ] **Step 1: 运行相关基线**

```powershell
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug -R "qml_main_window|playback_controller|waveform_item|window_controller" --output-on-failure
```

Expected: 记录实际通过/失败项；不得用历史结果替代。

- [ ] **Step 2: 为没有失败证据的用户症状补精确断言**

```cpp
// tests/qt/playback_controller_test.cpp
void PlaybackControllerTest::seekUsesDecoderDurationAndPublishesPositionImmediately()
{
    // 加载真实测试 WAV，应用一个不同的 waveform duration hint。
    // seek(750) 后立即断言 positionMs()==750；核心 duration 保持解码器值。
}
```

```qml
// tests/qml/tst_main_window.qml
function test_default_windows_show_exactly_ten_rows_without_forcing_resize() {
    compare(trackList.rowHeight, 42)
    compare(libraryManager.visibleRowCount, 10)
    verify(listWindow.height >= listWindow.defaultTenRowHeight)
    compare(listWindow.minimumHeight, listWindow.userResizableMinimumHeight)
}
```

- [ ] **Step 3: 运行新测试并确认正确失败**

```powershell
cmake --build --preset windows-msvc-debug --target playback_controller_test qml_main_window_test
ctest --preset windows-msvc-debug -R "playback_controller|qml_main_window" --output-on-failure
```

Expected: Seek 时钟或列表强制高度断言失败，失败原因是当前行为而非编译错误。

---

### Task 2: 修复普通列表、曲库管理和响应式窗口尺寸

**Files:**
- Modify: `app/qml/AgPlayer/Main.qml:7-15`
- Modify: `app/qml/AgPlayer/ListWindow.qml:7-65`
- Modify: `app/qml/AgPlayer/components/TrackList.qml:15-36`
- Modify: `app/qml/AgPlayer/components/LibraryManagerPage.qml:7-22, 640-772`
- Modify: `app/qml/AgPlayer/components/PlayerPane.qml:7-30, 160-550`
- Modify: `app/qml/AgPlayer/components/PlayerControls.qml`
- Modify: `app/qml/AgPlayer/EqualizerWindow.qml:7-15`
- Test: `tests/qml/tst_main_window.qml`

**Interfaces:**
- Consumes: `TrackList.rowHeight`, `LibraryManagerPage.preferredWindowHeight`。
- Produces: `ListWindow.defaultTenRowHeight`, `ListWindow.userResizableMinimumHeight` 和 `LibraryManagerPage.visibleRowCount`。

- [ ] **Step 1: 写响应式尺寸失败测试**

```qml
function test_main_and_equalizer_use_compact_contract() {
    compare(mainWindow.width, 1104)
    compare(mainWindow.height, 342)
    compare(mainWindow.minimumWidth, 612)
    compare(mainWindow.minimumHeight, 270)
    mainWindow.openEqualizer()
    tryCompare(equalizerWindow, "visible", true)
    compare(equalizerWindow.width, 520)
    compare(equalizerWindow.height, 307)
}
```

```qml
function test_ten_row_contract_keeps_scrolling_enabled() {
    compare(trackList.rowHeight, 42)
    compare(libraryManager.visibleRowCount, 10)
    verify(trackList.contentHeight > trackList.height)
    verify(libraryManagerTrackList.contentHeight > libraryManagerTrackList.height)
}
```

- [ ] **Step 2: 验证 RED**

```powershell
cmake --build --preset windows-msvc-debug --target qml_main_window_test
ctest --preset windows-msvc-debug -R qml_main_window --output-on-failure
```

Expected: 当前 `1228x380`、`780x460` 或曲库 `840px` 强制高度导致失败。

- [ ] **Step 3: 实现最小尺寸计算**

```qml
// Main.qml
width: 1104
height: 342
minimumWidth: 612
minimumHeight: 270
```

```qml
// LibraryManagerPage.qml
readonly property int visibleRowCount: 10
readonly property int trackRowHeight: 42
readonly property int preferredWindowHeight:
    fixedChromeHeight + visibleRowCount * trackRowHeight
```

`ListWindow.ensureLibraryManagerHeight()` 只设置首次默认高度，不再提高 `minimumHeight` 或覆盖用户已经调整的 `height`。播放器封面、间距、文字和控制按钮绑定 `compactHeight`/可用宽高，最小尺寸下保留核心播放控件。

- [ ] **Step 4: 验证 GREEN**

```powershell
cmake --build --preset windows-msvc-debug --target qml_main_window_test
ctest --preset windows-msvc-debug -R "qml_main_window|qml_mini_player" --output-on-failure
```

Expected: 精确尺寸、10 行和最小尺寸控件可见测试通过。

---

### Task 3: 统一播放器时钟、波形 Seek 和进度绘制

**Files:**
- Modify: `qt/src/playback_controller.cpp:165-198, 566-640`
- Modify: `qt/src/playback_controller.hpp`
- Modify: `app/qml/AgPlayer/components/PlayerPane.qml:9-24, 360-530, 627-642`
- Test: `tests/qt/playback_controller_test.cpp`
- Test: `tests/qml/tst_main_window.qml`
- Test: `tests/qml/tst_waveform.qml`

**Interfaces:**
- Consumes: `ag_player_snapshot()`, `ag_player_seek()`、`PlaybackController.positionMs`、`durationMs`。
- Produces: `PlaybackController::seek(qint64)` 立即发布受限位置；解码器快照时长保持权威。

- [ ] **Step 1: 写失败测试**

```cpp
void PlaybackControllerTest::waveformHintDoesNotReplaceDecoderTimeline()
{
    controller.applyWaveformDuration(trackId, decodedDuration + 5000);
    QCOMPARE(controller.durationMs(), decodedDuration);
    controller.seek(decodedDuration / 2);
    QCOMPARE(controller.positionMs(), decodedDuration / 2);
}
```

```qml
function test_waveform_seek_updates_played_clip_without_poll_delay() {
    var targetX = waveformFrame.width * 0.73
    mouseClick(mainWaveform, targetX, mainWaveform.height / 2)
    compare(waveformPlaybackGuide.x,
            Math.round(waveformFrame.width * 0.73) - waveformPlaybackGuide.width)
}
```

- [ ] **Step 2: 验证 RED**

```powershell
cmake --build --preset windows-msvc-debug --target playback_controller_test qml_main_window_test
ctest --preset windows-msvc-debug -R "playback_controller|qml_main_window|qml_waveform" --output-on-failure
```

Expected: 当前 `applyWaveformDuration()` 改写核心时长或 Seek 等待轮询导致失败。

- [ ] **Step 3: 实现唯一时钟与即时 UI 更新**

```cpp
void PlaybackController::seek(qint64 requestedMs)
{
    ag_playback_snapshot snapshot{};
    if (player_ == nullptr || ag_player_snapshot(player_, &snapshot) != AG_OK) return;
    const qint64 target = std::clamp(requestedMs, qint64{0},
                                     std::max<qint64>(0, snapshot.duration_ms));
    const ag_result result = ag_player_seek(player_, target);
    runCommand(result);
    if (result == AG_OK && positionMs_ != target) {
        positionMs_ = target;
        emit positionMsChanged();
    }
}
```

`applyWaveformDuration()` 只保存图形元数据，不再调用 `ag_player_set_duration_ms()`，`pollSnapshot()` 始终用 `snapshot.duration_ms` 更新播放时长。QML 的底图、已播放裁剪、Seek 和标签共用 `PlaybackController.durationMs`。

- [ ] **Step 4: 修改播放竖线**

```qml
width: 1
color: "#002FA7"
```

- [ ] **Step 5: 验证 GREEN**

```powershell
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug -R "playback_controller|qml_main_window|qml_waveform|waveform_item" --output-on-failure
```

Expected: Seek 立即同步，窗口缩放后时间与像素映射保持一致。

---

### Task 4: 固化动态频谱参数和 Peak Hold 同色绘制

**Files:**
- Modify: `qt/src/waveform_item.hpp:90-101, 140-176`
- Modify: `qt/src/waveform_item.cpp:181-192, 680-1020`
- Modify: `app/qml/AgPlayer/components/PlayerPane.qml:98-131, 380-450`
- Test: `tests/qt/waveform_item_test.cpp`
- Test: `tests/qml/tst_main_window.qml`

**Interfaces:**
- Consumes: `PlaybackController.spectrum`。
- Produces: `renderedSpectrumBarCount(qreal,qreal)` 和固定参数只读属性。

- [ ] **Step 1: 写失败测试**

```cpp
void WaveformItemTest::spectrumFillsCanvasWithFivePlusTwoLayout()
{
    WaveformItem item;
    QCOMPARE(item.spectrumBarWidth(), 5.0);
    QCOMPARE(item.spectrumBarGap(), 2.0);
    QCOMPARE(item.spectrumAttackSeconds(), 0.02);
    QCOMPARE(item.spectrumDecaySeconds(), 0.10);
    QCOMPARE(item.spectrumPeakFallSeconds(), 0.35);
    QCOMPARE(renderedSpectrumBarCount(700.0, 1.0), std::size_t{100});
}
```

增加渲染顶点断言：每个 Peak Hold 顶点 RGB 与对应柱体顶点相同。

- [ ] **Step 2: 验证 RED**

```powershell
cmake --build --preset windows-msvc-debug --target waveform_item_test
ctest --preset windows-msvc-debug -R waveform_item --output-on-failure
```

Expected: 任一自适应数量、时间常量或 Peak Hold 色彩不一致时失败；如果现有实现已经满足，则保留测试并不改生产代码。

- [ ] **Step 3: 实现缺失行为**

```cpp
static constexpr qreal spectrumBarWidth() noexcept { return 5.0; }
static constexpr qreal spectrumBarGap() noexcept { return 2.0; }
static constexpr qreal spectrumAttackSeconds() noexcept { return 0.02; }
static constexpr qreal spectrumDecaySeconds() noexcept { return 0.10; }
static constexpr qreal spectrumPeakFallSeconds() noexcept { return 0.35; }
```

柱数使用 `ceil(physicalWidth / 7.0)`，输入频谱在绘制时重采样；Peak Hold 调用与柱体相同的 `mixColor()`。

- [ ] **Step 4: 验证 GREEN**

```powershell
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug -R "waveform_item|qml_main_window" --output-on-failure
```

---

### Task 5: 修复磁吸跨屏尺寸与辅助窗口 Z 序

**Files:**
- Modify: `qt/src/window_controller.cpp:718-920, 1010-1086, 1158-1229`
- Modify: `qt/src/window_controller.hpp`
- Test: `tests/qt/window_controller_test.cpp`

**Interfaces:**
- Consumes: `QWindow::geometry()`、所有 `QScreen::availableGeometry()`、Win32 `SetWindowPos()`。
- Produces: 移动磁吸组只改变位置；原生层级稳定为主播放器、列表、活动辅助窗口。

- [ ] **Step 1: 写失败测试**

```cpp
void WindowControllerTest::dockedMoveAcrossScreensPreservesBothSizes()
{
    QWindow main;
    main.setGeometry(200, 150, 420, 220);
    QWindow list;
    list.setGeometry(0, 0, 360, 180);
    WindowController controller;
    controller.setWindows(&main, nullptr);
    controller.setListWindow(&list);
    controller.snapListWindow(QStringLiteral("right"));
    const QSize mainBefore = main.size();
    const QSize listBefore = list.size();
    const QRect available = main.screen()->availableGeometry();
    main.setPosition(available.right() - main.width() + 1,
                     available.top() + 80);
    QCoreApplication::processEvents();
    QCOMPARE(main.size(), mainBefore);
    QCOMPARE(list.size(), listBefore);
}
```

```cpp
void WindowControllerTest::activeAuxiliaryStaysAboveDockedGroup()
{
    if (QGuiApplication::platformName().compare(QStringLiteral("windows"),
                                                Qt::CaseInsensitive) != 0)
        QSKIP("requires the Windows native z-order stack");
    QWindow main;
    QWindow list;
    QWindow settings;
    WindowController controller;
    controller.setWindows(&main, nullptr);
    controller.setListWindow(&list);
    controller.registerSettingsWindow(&settings);
    controller.showListWindow();
    controller.snapListWindow(QStringLiteral("bottom"));
    main.show();
    settings.show();
    QEvent activation(QEvent::WindowActivate);
    QCoreApplication::sendEvent(&settings, &activation);
    const HWND listHandle = reinterpret_cast<HWND>(list.winId());
    const HWND settingsHandle = reinterpret_cast<HWND>(settings.winId());
    bool settingsAboveList = false;
    for (HWND current = GetWindow(listHandle, GW_HWNDPREV);
         current != nullptr; current = GetWindow(current, GW_HWNDPREV)) {
        if (current == settingsHandle) settingsAboveList = true;
    }
    QVERIFY(settingsAboveList);
}
```

- [ ] **Step 2: 验证 RED**

```powershell
cmake --build --preset windows-msvc-debug --target window_controller_test
ctest --preset windows-msvc-debug -R window_controller --output-on-failure
```

Expected: `repositionDockedListWindow()` 的同步尺寸或 Z 序断言失败。

- [ ] **Step 3: 删除移动路径中的隐式缩放**

`repositionDockedListWindow()` 在移动、跨屏和显式磁吸时只调用 `setPosition()`；不调用主窗口或列表窗口 `resize()`。恢复几何只把完全不可见的窗口移回虚拟工作区，合法尺寸原样保留。

- [ ] **Step 4: 统一 Z 序刷新**

```cpp
raiseNative(mainWindow_);
raiseNative(listWindow_);
raiseNative(lastAuxiliaryWindow_);
```

在辅助窗口显示/激活、列表激活和主窗口激活事件中走同一条防重入路径。

- [ ] **Step 5: 验证 GREEN**

```powershell
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug -R "window_controller|qml_main_window" --output-on-failure
```

---

### Task 6: 修复歌曲右键子菜单交互和视觉

**Files:**
- Modify: `app/qml/AgPlayer/components/TrackList.qml:132-172, 540-634`
- Modify: `app/qml/AgPlayer/components/LibraryManagerPage.qml:90-123, context-menu section`
- Test: `tests/qml/tst_main_window.qml`

**Interfaces:**
- Consumes: `PlaylistModel.addTracks/moveTracks`、四个音频工具 `loadFiles/queueFiles`、`WindowController.showAudioTools()`。
- Produces: 真实鼠标点击可验证的“加入歌单”和“使用音频工具打开”子菜单。

- [ ] **Step 1: 写失败的真实鼠标测试**

```qml
function test_context_submenus_route_frozen_selection() {
    openTrackMenuWithRightClick(firstRow)
    mouseClick(addToPlaylistMenu)
    mouseClick(playlistTargetItem)
    verify(playlistModel.trackIdsForPlaylist(targetId).indexOf(firstTrackId) >= 0)

    openTrackMenuWithRightClick(firstRow)
    mouseClick(audioToolsMenu)
    mouseClick(formatConverterItem)
    compare(AudioToolsController.currentTool, 1)
    verify(WindowController.audioToolsVisible)
}
```

- [ ] **Step 2: 验证 RED**

```powershell
cmake --build --preset windows-msvc-debug --target qml_main_window_test
ctest --preset windows-msvc-debug -R qml_main_window --output-on-failure
```

- [ ] **Step 3: 实现稳定菜单生命周期与路由**

动态歌单项使用菜单拥有的 `Instantiator`，打开时冻结 `targetTrackIds.slice()`；触发后调用模型并关闭菜单。音频工具项在 URL 为空时禁用，否则按装载、选择页、显示窗口的顺序执行。两个子菜单复用 `SystemMenuItem` 的 230px 宽度、主题色和悬停背景。

- [ ] **Step 4: 验证 GREEN**

```powershell
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug -R "qml_main_window|playlist_model|audio_tools_end_to_end" --output-on-failure
```

---

### Task 7: 固化 EQ 图标和 Windows 应用身份

**Files:**
- Modify: `app/qml/AgPlayer/components/PlayerControls.qml`
- Modify: `app/main.cpp:65-80`
- Modify: `app/agplayer.rc`
- Modify: `app/CMakeLists.txt`
- Test: `tests/qml/tst_main_window.qml`
- Test: `tests/scripts/installer_contract.Tests.ps1`

**Interfaces:**
- Consumes: `Theme.isLight`/当前主题属性、`assets/brand/agplayer.ico`。
- Produces: 竖向 EQ 图标、`AgPlayer.Desktop` AppUserModelID、QApplication/PE 图标一致性。

- [ ] **Step 1: 写失败测试**

```qml
function test_eq_icon_is_vertical_and_theme_colored() {
    compare(eqButton.contentItem.rotation, 90)
    Theme.mode = 0
    compare(eqButton.icon.color, "#ffffff")
    Theme.mode = 1
    compare(eqButton.icon.color, "#000000")
}
```

PowerShell 契约测试读取 `app/main.cpp`、`.rc` 和打包输出，分别断言 `AgPlayer.Desktop`、ICO 资源和 EXE 图标存在。

- [ ] **Step 2: 验证 RED**

```powershell
cmake --build --preset windows-msvc-debug --target qml_main_window_test
ctest --preset windows-msvc-debug -R "qml_main_window|installer_contract" --output-on-failure
```

- [ ] **Step 3: 实现最小修复**

```qml
contentItem.rotation: 90
icon.color: Theme.isLight ? "#000000" : "#ffffff"
```

进程在创建窗口前调用 `SetCurrentProcessExplicitAppUserModelID(L"AgPlayer.Desktop")`，`QApplication::setWindowIcon()` 与 RC 的 `IDI_ICON1 ICON` 指向同一 ICO。

- [ ] **Step 4: 验证 GREEN**

```powershell
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug -R "qml_main_window|installer_contract|runtime_deployment" --output-on-failure
```

---

### Task 8: 全量验证、真实 UI/音频 QA 和 EXE 打包

**Files:**
- Modify: `design-qa.md`
- Modify: `docs/qa/audio-tools-windows-mvp.md`
- Use: `scripts/qa-final-ui-matrix.ps1`
- Use: `scripts/package-windows.ps1`
- Use: `tools/stage_release.ps1`

**Interfaces:**
- Consumes: Tasks 2-7 的可执行程序和测试。
- Produces: Debug/Release 证据、真实交互记录和最终 EXE 路径。

- [ ] **Step 1: 运行完整自动化门禁**

```powershell
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug --output-on-failure
cmake --build --preset windows-msvc-release
ctest --preset windows-msvc-release --output-on-failure
git diff --check
```

Expected: 56/56 Debug 和 56/56 Release 通过，零构建错误，`git diff --check` 无输出。

- [ ] **Step 2: 运行 UI 矩阵并查看截图**

```powershell
powershell -ExecutionPolicy Bypass -File scripts/qa-final-ui-matrix.ps1
```

检查主窗口最小/默认尺寸、普通列表 10 行、曲库管理 10 行、EQ、深浅主题图标和设置层级。

- [ ] **Step 3: 执行真实歌曲与跨屏 QA**

用一首真实 WAV/FLAC/MP3：连续点击波形 10 个位置，拖动 5 次，播放中改变窗口尺寸；磁吸主/列表后跨过扩展屏边界；打开设置、EQ、音频工具；确认尺寸不变、颜色与实际音频位置一致、辅助窗口始终在最上层、任务栏图标稳定。结果写入 QA 文档。

- [ ] **Step 4: 打包 EXE**

```powershell
powershell -ExecutionPolicy Bypass -File scripts/package-windows.ps1
```

Expected: 脚本返回 0，并打印 staging/installer 的绝对路径。

- [ ] **Step 5: 验证干净目录中的 EXE**

从打包输出启动 `AgPlayer.exe`，重复导入、播放、Seek、列表滚动、设置层级和任务栏图标抽查；确认无缺失 DLL、无 QML 警告、无残留进程。

- [ ] **Step 6: 最终提交范围审计**

```powershell
git status --short
git diff --name-only 59f442e..HEAD
```

只提交本轮实际修改文件，不清理或覆盖其他既有未提交工作。
