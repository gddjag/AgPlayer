# AgPlayer 标签管理与列表波形总实施计划

> 本计划是本专项唯一执行入口。三份专项计划仅作为技术细节依据；如有冲突，以已确认规格、参考图和本计划为准。

**目标：** 在不改变播放、主波形、BPM、解码和音频回调架构的前提下，实现参考图中的连续三栏列表工作区、完整标签管理、资源目录导航，以及极致轻量的歌曲列表波形缩略图。

**规格依据：**

- `docs/superpowers/specs/2026-08-20-track-waveform-tag-management-design.md`
- `C:/Users/Administrator/Desktop/AgPlayer 歌曲列表波形缩略图开发提示词｜极致轻量版_歌单标签整合版.md`
- `C:/Users/Administrator/Desktop/音视频播放器/AgPlayer音频播放器完整版/标签管理.png`（1447×1087）

**专项技术依据：**

- `docs/superpowers/plans/2026-08-20-tag-data-navigation.md`
- `docs/superpowers/plans/2026-08-20-track-waveform-thumbnails.md`
- `docs/superpowers/plans/2026-08-20-tag-management-reference-ui.md`

## 不可变约束

- 实施工作树固定为 `D:/ai/AgPlayer/.worktrees/tag-waveform-ui`，分支固定为 `codex/tag-waveform-ui`；不得在根工作树修改生产代码。
- 数据链固定为 `LibraryModel -> LibraryFilterModel -> TrackList`；全应用只保留一个歌曲表格模型和一个 `TrackList` 实例。
- `TrackRecord::tags` 与 `library.json` 仍是歌曲标签关系真相；新增 JSON 只保存标签目录元数据，使用 `QSaveFile` 原子写入。
- 删除标签或资源目录引用绝不删除歌曲记录和磁盘音频文件。
- 列表波形只读现有有效 `.agwf` v2 缓存；缓存缺失、损坏或不匹配时显示空态，绝不触发分析、解码、FFmpeg、PCM 或主波形预取。
- 波形数据固定 128 字节；缓存 LRU 不超过 256 首；同一时刻最多一个低优先级工作任务；按 `trackId + generation` 丢弃过期结果。
- 波形渲染固定一个 `QSGGeometryNode`；禁止 Canvas、`QQuickPaintedItem`、Image、shader、动画、Repeater 和每峰值 QML 项。
- 波形关闭时必须零 Loader 实例、零请求、零缓存读取、零颜色计算、零节点；行高恢复 42；开启时行高 62，封面 34×34，波形高 8–10。
- 颜色模式只有 `Color36` 和 `Mono`；Color36 使用 persistent trackId 的 FNV-1a 32 位哈希 `% 36`，不得使用行号、随机数或 `qHash`。
- 下方列表窗口是一块连续表面：左栏 256、右栏 328、中栏自适应且不小于 680、分隔线 1；不是三个独立窗口或卡片。
- 表头顺序固定为 `# | 歌曲 | 收藏 | 艺术家 | 专辑 | 评分 | BPM | 时长`；标签网格视觉固定三列，以 `GridView.cellWidth = width / 3` 表达，不使用不存在的 `GridView.columns` 属性。
- 只使用现有 Qt/C++17 技术栈和依赖；最小必要修改，不重构无关模块。
- 所有行为修改遵循 RED → GREEN → REFACTOR；测试必须验证真实行为，不以源码文本匹配代替行为测试。源码禁用项可另加静态守卫，但不能作为唯一测试。

## 文件所有权与执行顺序

### Task 1：标签数据、过滤与导航模型（C++ 专属）

**允许修改：** `qt/src/library_model.*`、`tag_store.*`、`tag_model.*`、`library_filter_model.*`、`library_navigation_model.*`、`library_manager_controller.*`、相关 CMake/QtTest 文件。

**禁止修改：** 所有 QML、波形缩略图文件、`design-qa.md`。

**验收：** 标签批量增删改只发精准信号且单次持久化；零歌曲标签可恢复；计数增量更新；标签/目录与现有搜索、评分、BPM、歌单过滤取交集；目录引用可持久化且移除不碰磁盘文件；相关 QtTest 全部通过。

### Task 2：缓存专用波形数据服务（C++ 专属）

**允许修改：** `track_waveform_thumbnail_provider.*`、相关 CMake/QtTest/压力测试和静态守卫。

**禁止修改：** `TrackList.qml`、`ListWindow.qml`、设置页、标签模型和主波形实现。

**验收：** 128 字节量化、固定 36 色、只读 `.agwf` v2、缓存 miss 不分析、单 worker、同 track 去重、generation 安全、LRU≤256、10,000 行压力边界均有真实测试。

### Task 3：单节点渲染、设置和运行时注册

**允许修改：** `track_waveform_thumbnail_item.*`、`settings_controller.*`、`qml_registration.*`、`app/main.cpp`、QML 测试装配、`SettingsPage.qml`、相关测试/CMake。

**禁止修改：** `TrackList.qml`、`ListWindow.qml`、标签面板和左侧导航。

**验收：** 一个节点、256 顶点；只改颜色不重建几何；设置默认开启/Color36，可持久化和恢复默认；类型和单例只注册一次；关闭状态尚未创建任何列表波形实例。

### Task 4：连续三栏参考 UI 与共享表格（QML 集成专属）

**允许修改：** `ListWindow.qml`、`SideNavigation.qml`、`TagManagementPanel.qml`、`TrackWaveformThumbnail.qml`、`TrackList.qml`、`SearchFilter.qml`、QML 资源和行为测试。

**前置接口：** Task 1 的 `TagModel`/`LibraryNavigationModel`/过滤属性，以及 Task 2–3 的 provider/item/settings 已稳定。

**验收：** 256/自适应/328 连续表面；唯一共享 `TrackList`；表头顺序正确；标签搜索/添加/三列虚拟网格/省略与 tooltip；Loader 仅为可见行且关闭零实例；62/42 行高即时切换；排序、收藏、评分、右键和已有播放行为不回归。

### Task 5：标签、歌单、目录操作与拖放

**允许修改：** Task 4 QML 文件、`native_drop_router.*`、`app/main.cpp` 与相应测试。

**验收：** 标签重命名/删除/改色为按需右键菜单；歌单现有菜单保留；资源目录添加/移除引用/重扫真实可用；系统目录和音频文件拖入正确分流；多选拖拽只创建一次 0.68 透明预览且取消/失败释放；所有删除文案明确“不删除磁盘文件”。

### Task 6：集成、性能和回归门禁

**允许修改：** 装配、压力测试和必要的小范围修复；不得新建第二套模型或 UI。

**验收：** 应用和 QML 测试只构造一份 TagModel、LibraryNavigationModel、thumbnail provider；1,000/10,000 行滚动请求受可见范围约束；LRU≤256、worker≤1、分析调用=0；主波形、播放、窗口控制、排序/搜索/筛选回归测试通过；Debug 与 Release 分别记录结果。

### Task 7：Windows 实机、像素级 Design QA 与开发记录

**允许修改：** `docs/development/`、`docs/qa/`、`design-qa/` 和 `design-qa.md`，以及为修复 P0/P1/P2 所需的最小生产代码。

**验收：** 使用真实 AgPlayer、真实标签/歌单/目录、缓存命中与未命中歌曲，在 1447×1087 同尺寸同状态截图；保存参考、实现和并排对比证据；检查字体、间距、列宽、行高、颜色、边框、圆角、选中态和滚动态；100/125/150/175/200% DPI 与深浅主题均可达；P0/P1/P2 全部修复后才允许 `design-qa.md` 标记 passed。真实播放/硬件未执行时必须标 BLOCKED，不得推断通过。

## 执行纪律

- 使用 `superpowers:subagent-driven-development`，严格一次只派一个实现代理；每个任务使用新代理，不能自行派生子代理。
- 每个任务完成后生成 review package，由独立 reviewer 做规格与质量双审；Critical/Important 未清零不得进入下一任务。
- 实现代理必须提交独立小提交并在报告中提供 RED/GREEN 命令和输出；主代理最终重新运行关键测试、检查完整 diff，并做整分支复审。
- 根工作树的现有未提交修改属于用户或其他任务，任何时候都不得暂存、覆盖或提交。
