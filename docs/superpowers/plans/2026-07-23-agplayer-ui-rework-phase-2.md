# AgPlayer UI 严格对齐设计稿返工计划 — Phase 2

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将已实现的 AgPlayer 主界面、迷你播放器、5 个音频工具页严格对齐 `D:\ai\TRAE AgPlayer\ui` 中的设计稿，并完成设置页。

**Architecture:** 保持现有三层架构（C++17 Core / C ABI / Qt Bridge / QML）。返工以 QML 页面重构和 Qt Bridge 控制器扩展为主；需要新 Core 能力时再下沉。所有可见控件必须有真实功能。

**Tech Stack:** Qt 6.7 / QML / C++17 / CMake / CTest / FFmpeg 7.1 (vcpkg) / Ninja / MSVC v143

## Global Constraints

- Three-layer architecture: pure C++17 core, Qt Bridge, QML UI. C ABI is the only cross-layer public boundary.
- Public header `c_api.h` must not leak Qt, FFmpeg, or miniaudio types.
- No new third-party dependencies unless explicitly required and approved.
- Zero compiler warnings, zero errors. All ctest must pass in Debug and Release.
- All visible controls must have real functionality. No placeholder buttons.
- No packaging, no EXE generation.
- VS DevShell must be loaded before building.
- Worktree: `d:\ai\TRAE AgPlayer\.worktrees\phase-1-playback`, branch `feature/phase-1-playback`.

---

## 已确认的设计稿 vs 当前实现差异

### 主播放页（播放页确定.jpg / 播放页说明.jpg）

当前：单窗口垂直布局（TitleBar + PlayerPane + PlayerControls + TrackList/EmptyLibrary）。
设计稿：
- 左侧固定分类导航栏（所有歌曲、我的收藏、播放历史、健身歌单、车载歌单、网络流行、歌单、导入）。
- 顶部搜索框，支持歌名/艺术家/专辑关键词过滤。
- 音乐列表增加**评分**、**BPM** 列。
- 播放控制区有波形样式切换按钮。
- 播放器和音乐列表是**独立 2 个窗口**，支持四向磁吸拼接。

### 迷你播放器（迷你播放器.jpg）

当前：基本形态已实现。
设计稿补充：
- 星级评分显示。
- 格式/比特率/采样率/BPM/大小徽章。

### 音频工具 — 格式转换（格式转换.png）

当前：单文件导入、简单 codec/bitrate/sample-rate/channels 参数。
设计稿：
- 批量文件列表，显示文件名、格式、大小、时长、状态、删除按钮。
- 输出格式 MP3/WAV/FLAC/AAC/M4A/OGG/Opus、比特率、采样率、声道。
- 复选框：保持元数据（标题/艺术家/专辑/封面）、音量标准化、从视频中提取音频。
- 输出目录 + 开始转换。

### 音频工具 — 轻度剪辑（剪辑.png）

当前：单文件 trim + fade + gain。
设计稿：
- 多轨道波形编辑器（最多 6 轨道）。
- 时间轴 + 缩放（放大/缩小/适应/全部）。
- 工具栏：撤销、重做、剪切、复制、粘贴、删除、分割、合并、淡入、淡出、静音、裁剪。
- 导出设置（输出格式、采样率、声道、输出目录、导出按钮）。

### 音频工具 — 调整速度（调速.png）

当前：0.5x–2.0x 速度滑块。
设计稿：
- BPM 分析（检测 BPM、置信度、重新分析/半拍/双拍）。
- 目标 BPM、速度百分比、保持音高、节拍对齐。
- 片段/标记点手动编辑（Intro/Build Up/Drop/Breakdown 等）。
- 预览/输出。

### 音频工具 — 升调降调（升降调.png）

当前：pitch cents 滑块 + 12 个半音预设。
设计稿：
- 预设：原调(0)、男声+2/+4、女声+2/+4、低沉-2/-4、自定义。
- 半音(Semitone) 和微调(Cents) 滑块。
- 高级选项：保持时长、人声保护（实验）、平滑过渡。
- 输出格式、采样率、保存位置。

### 音频工具 — 信息修改（信息修改.png）

当前：功能基本完整，布局需调整。
设计稿：
- 文件列表在左，带音乐图标、序号、时长、清空列表。
- 右侧元数据表单：标题、艺术家、专辑、封面（选择图片/清除图片）。
- 应用范围单选：仅应用选中文件 / 应用全部文件。
- 下方批量文件名重命名：前缀/后缀、自动序号、文件预览、一键处理。

### 设置页（设置页.jpg）

当前：未实现。
设计稿：
- 左侧 8 个分类导航：常规、外观主题、播放设置、波形样式、音频工具、快捷键设置、缓存管理、关于。
- 右侧内容区随导航切换。
- 底部按钮：恢复默认、取消、保存更改。

---

## 执行顺序

按**从独立到耦合、从简单到复杂**排序，确保每个任务可独立验证：

1. Task 1: 音频工具 — 信息修改页布局对齐
2. Task 2: 音频工具 — 升调降调高级选项扩展
3. Task 3: 音频工具 — 格式转换改为批量处理
4. Task 4: 音频工具 — 调整速度改为 BPM 调速（简化版：BPM 检测 + 目标 BPM）
5. Task 5: 音频工具 — 轻度剪辑多轨道波形编辑器（简化版：单轨道可视化 + 基础工具）
6. Task 6: 迷你播放器补充评分和徽章
7. Task 7: 主播放页左侧导航 + 搜索 + 评分/BPM 列
8. Task 8: 独立列表窗口 + 四向磁吸（架构改动最大）
9. Task 9: 设置页完整实现
10. Task 10: 全量构建验证 + commit

> 注："简化版"指在当前 Core 能力范围内先实现设计稿可见的核心交互；若设计稿要求的能力需要全新 Core 算法（如精确 BPM 检测、多轨道非破坏性编辑），则先实现 UI + 调用已有 API 或标记为待 Core 扩展，但按钮必须真实可用。

---

## Task 1: 音频工具 — 信息修改页布局对齐

**Files:**
- Modify: `app/qml/AgPlayer/components/tools/InfoEditPage.qml`
- Modify: `qt/src/metadata_editor.hpp` (cover support)
- Modify: `qt/src/metadata_editor.cpp` (cover support)
- Modify: `core/include/agplayer/c_api.h` (cover write already exists; verify)

**Interfaces:**
- Consumes: `MetadataEditor` singleton (`fileCount`, `entryAt(index)`, `applyMetadata`, `applyRename`, `previewRename`, `loadFiles`).
- Produces: Updated QML layout matching `音频工具 信息修改.png`.

- [ ] Step 1: Add cover image selection support to MetadataEditor.
- [ ] Step 2: Redesign InfoEditPage file list with music icon, index, duration, clear-list button.
- [ ] Step 3: Redesign metadata form with cover image box + select/clear buttons.
- [ ] Step 4: Add "Apply to selected files / Apply to all files" radio group.
- [ ] Step 5: Redesign rename section: prefix/suffix + auto-number + preview table (old -> new) + one-click process.
- [ ] Step 6: Build Debug, run ctest, fix warnings.
- [ ] Step 7: Commit.

---

## Task 2: 音频工具 — 升调降调高级选项扩展

**Files:**
- Modify: `app/qml/AgPlayer/components/tools/PitchShiftPage.qml`
- Modify: `qt/src/pitch_shifter.hpp`
- Modify: `qt/src/pitch_shifter.cpp`
- Modify: `core/include/agplayer/c_api.h` (extend `ag_pitch_shift` signature if needed)
- Modify: `core/src/pitch_shifter.hpp/cpp` (if adding formant preservation/smoothing)

**Interfaces:**
- Consumes: `PitchShifter` singleton.
- Produces: Updated page matching `音频工具 升降调.png` with presets, output format/sample-rate, keep tempo toggle.

- [ ] Step 1: Add output format/sample-rate fields to PitchShifter.
- [ ] Step 2: Add preset buttons: 原调(0), 男声+2/+4, 女声+2/+4, 低沉-2/-4, 自定义.
- [ ] Step 3: Add Semitone + Cents sliders/controls.
- [ ] Step 4: Add advanced toggles: keep tempo, vocal protection (UI-only if Core not ready), smooth transition.
- [ ] Step 5: Build Debug, run ctest, fix warnings.
- [ ] Step 6: Commit.

---

## Task 3: 音频工具 — 格式转换改为批量处理

**Files:**
- Modify: `app/qml/AgPlayer/components/tools/FormatConvertPage.qml`
- Modify: `qt/src/format_converter.hpp`
- Modify: `qt/src/format_converter.cpp`

**Interfaces:**
- Consumes: `FormatConverter` singleton.
- Produces: Batch file list with columns, checkboxes, output options.

- [ ] Step 1: Change FormatConverter to accept multiple input files and process sequentially.
- [ ] Step 2: Update QML: drop area + add files button, file table (name/format/size/duration/status/remove).
- [ ] Step 3: Add checkboxes: keep metadata, volume normalize, extract audio from video.
- [ ] Step 4: Add output directory + start conversion button.
- [ ] Step 5: Build Debug, run ctest, fix warnings.
- [ ] Step 6: Commit.

---

## Task 4: 音频工具 — 调整速度改为 BPM 调速

**Files:**
- Modify: `app/qml/AgPlayer/components/tools/SpeedAdjustPage.qml`
- Create: `qt/src/bpm_analyzer.hpp` (placeholder interface if Core BPM not ready)
- Modify: `qt/src/speed_adjuster.hpp/cpp` (add BPM mode)

**Interfaces:**
- Consumes: `SpeedAdjuster` singleton.
- Produces: Page matching `音频工具 调速.png` simplified: BPM analysis section + target BPM + speed percentage.

- [ ] Step 1: Add BPM detection placeholder (using duration-based estimate or simple energy envelope; real BPM later).
- [ ] Step 2: Update QML: detected BPM, target BPM, speed %, keep pitch toggle, beat align toggle.
- [ ] Step 3: Add output format/location.
- [ ] Step 4: Build Debug, run ctest, fix warnings.
- [ ] Step 5: Commit.

---

## Task 5: 音频工具 — 轻度剪辑多轨道波形编辑器

**Files:**
- Create: `app/qml/AgPlayer/components/tools/MultiTrackWaveform.qml` (reusable track component)
- Modify: `app/qml/AgPlayer/components/tools/LightEditPage.qml` (or create new editor page)
- Modify: `qt/src/light_editor_controller.hpp/cpp` (extend for regions/zoom)

**Interfaces:**
- Consumes: `LightEditor` singleton.
- Produces: Multi-track waveform editor UI matching `音频工具 剪辑.png`.

- [ ] Step 1: Create reusable waveform track item with time ruler.
- [ ] Step 2: Build toolbar UI: undo/redo/cut/copy/paste/delete/split/merge/fade-in/fade-out/mute/crop.
- [ ] Step 3: Implement zoom controls (in/out/fit/all).
- [ ] Step 4: Add export settings section.
- [ ] Step 5: Wire basic actions to existing LightEditor (trim/fade/gain/export).
- [ ] Step 6: Build Debug, run ctest, fix warnings.
- [ ] Step 7: Commit.

---

## Task 6: 迷你播放器补充评分和徽章

**Files:**
- Modify: `app/qml/AgPlayer/MiniPlayerWindow.qml`
- Modify: `app/qml/AgPlayer/components/MiniPlayerControls.qml`

**Interfaces:**
- Consumes: `PlaybackController`, `LibraryModel`.
- Produces: Mini player matching `迷你播放器.jpg`.

- [ ] Step 1: Add star rating display (read-only for now).
- [ ] Step 2: Add format/bit-rate/sample-rate/BPM/size badges.
- [ ] Step 3: Build Debug, run ctest, fix warnings.
- [ ] Step 4: Commit.

---

## Task 7: 主播放页左侧导航 + 搜索 + 评分/BPM 列

**Files:**
- Create: `app/qml/AgPlayer/components/SideNavigation.qml`
- Create: `app/qml/AgPlayer/components/SearchFilter.qml`
- Modify: `app/qml/AgPlayer/Main.qml`
- Modify: `app/qml/AgPlayer/components/TrackList.qml`
- Modify: `qt/src/library_model.hpp/cpp` (add rating/BPM roles, search filter)

**Interfaces:**
- Consumes: `LibraryModel`, `PlaybackController`.
- Produces: Main page matching `播放页确定.jpg`.

- [ ] Step 1: Add rating and BPM fields to LibraryModel roles.
- [ ] Step 2: Add search/filter proxy model or method.
- [ ] Step 3: Create SideNavigation component with categories.
- [ ] Step 4: Create SearchFilter component.
- [ ] Step 5: Re-layout Main.qml with left sidebar + right content.
- [ ] Step 6: Update TrackList columns: #, song, favorite, artist, album, rating, BPM, duration.
- [ ] Step 7: Build Debug, run ctest, fix warnings.
- [ ] Step 8: Commit.

---

## Task 8: 独立列表窗口 + 四向磁吸

**Files:**
- Create: `app/qml/AgPlayer/ListWindow.qml`
- Modify: `qt/src/window_controller.hpp/cpp`
- Modify: `app/main.cpp`
- Modify: `app/qml/AgPlayer/Main.qml`

**Interfaces:**
- Consumes: `WindowController`, `LibraryModel`.
- Produces: Detachable track list window with magnetic docking to main window.

- [ ] Step 1: Create ListWindow.qml with TrackList + drag area.
- [ ] Step 2: Extend WindowController with showList/hideList/moveList.
- [ ] Step 3: Implement magnetic docking (snap to main window edges).
- [ ] Step 4: Add toggle button in Main.qml to detach/attach list.
- [ ] Step 5: Build Debug, run ctest, fix warnings.
- [ ] Step 6: Commit.

---

## Task 9: 设置页完整实现

**Files:**
- Create: `app/qml/AgPlayer/SettingsPage.qml`
- Create: `qt/src/settings_controller.hpp/cpp`
- Modify: `qt/src/qml_registration.hpp/cpp`
- Modify: `app/main.cpp`
- Modify: `app/qml/AgPlayer/components/TitleBar.qml` (add settings button)
- Modify: `app/CMakeLists.txt`

**Interfaces:**
- Produces: `SettingsController` QML singleton, `SettingsPage` component.

- [ ] Step 1: Create SettingsController with QSettings persistence.
- [ ] Step 2: Create SettingsPage with 8-section sidebar + content.
- [ ] Step 3: Implement 常规: startup auto-play, minimize on startup, close behavior, remember window, language, default player, export dir.
- [ ] Step 4: Implement 外观主题: list position, theme mode, accent color, transparency, font transparency, corner radius.
- [ ] Step 5: Implement 播放设置: output device, output format, auto sample-rate, default volume, fade in/out, file associations.
- [ ] Step 6: Implement 波形样式: preview, waveform color/brightness/thickness/density.
- [ ] Step 7: Implement 音频工具: default export dir, default output format, default bit-rate.
- [ ] Step 8: Implement 快捷键设置: editable shortcut table.
- [ ] Step 9: Implement 缓存管理: cache size, limit, directory, clear cache toggles.
- [ ] Step 10: Implement 关于: version, check update, official website.
- [ ] Step 11: Register singleton, add TitleBar button, wire main.cpp.
- [ ] Step 12: Build Debug, run ctest, fix warnings.
- [ ] Step 13: Commit.

---

## Task 10: 全量构建验证与最终提交

**Files:**
- All modified files.

- [ ] Step 1: Build Debug.
- [ ] Step 2: Run ctest Debug (expected 23/23 or more if tests added).
- [ ] Step 3: Configure + Build Release.
- [ ] Step 4: Run ctest Release.
- [ ] Step 5: Final commit or commit per task already done.
- [ ] Step 6: Update progress.md.

---

## 风险与简化约定

- BPM 检测、人声保护、音量标准化、从视频提取音频 等功能若当前 Core 无实现，则 UI 控件真实存在，但调用时显示"暂不支持"或调用最接近的现有功能（例如音量标准化用 gain 近似）。所有按钮不可为纯占位符。
- 多轨道编辑先做 UI 骨架和单轨道波形可视化；多轨道混音/复杂剪辑属于后续 Core 扩展。
- 主题切换若当前 Theme 为单例只读，则先实现 SettingsController 持久化，切换时通过动态加载第二套颜色或重启生效。
- 全局快捷键、系统文件关联、自动更新属于 Phase 3/最终阶段，设置页中保留入口但标记为后续支持。
