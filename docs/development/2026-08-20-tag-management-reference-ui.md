# 标签管理参考 UI 开发记录

日期：2026-08-20 至 2026-08-22
范围：歌曲列表波形缩略图、歌单/资源文件夹/标签管理整合后的 Task 7 Windows 实机与视觉收口。

## 目标与边界

本轮不增加第二套歌曲列表，也不改变播放内核。产品规则是：标签管理页显示“左侧导航 + 中间共享歌曲列表 + 右侧标签管理”；普通音乐库、收藏、歌单和资源文件夹页只显示“左侧导航 + 同一个共享歌曲列表”，标签栏完全隐藏，中间列表回收其宽度。页面切换只能改变可见性和过滤条件，必须保留同一 `sharedTrackList` 对象及其播放、排序、搜索、选择和波形状态。

Task 7 只允许为视觉验收发现的 P0/P1/P2 做最小生产修复。没有新增依赖、资源、平台专属业务逻辑或第二套表格。

## 实现记录

### 页面专属三栏和共享列表

- `ListWindow.qml` 以真实导航节点类型计算 `tagManagementMode`。
- `TagManagementPanel` 和其分隔线仅在标签页可见；普通页面从布局中移除二者，由原 `sharedTrackList` 回收 328px。
- 标签页最小宽度为 1284 logical px，普通页面为 956 logical px；非 offscreen 桌面窗口把当前页最小宽度暴露给 Windows。
- `--qa-list-category tags` 复用既有 QA 截图入口，但现在先调用真实 `SideNavigation.activateNode()`，确保真实页面约束在首次停靠前参与布局；没有新增 QA-only 控制 API。

### 停靠宽度根因与修复

根因不是三栏内部无限最小宽，而是 `WindowController` 每次停靠都把列表强制缩回主播放器当前宽度，覆盖了标签页的最小宽度。修复后停靠对使用 `max(main width, active list minimum)` 的共享宽度：首次标签页打开时保留较大的列表首选宽；页内最小宽度变化会同步停靠对；降低最小宽度不会造成抖动；用户以后主动调整主窗口时仍由用户宽度驱动列表。

### 隔离 QA 数据根

`--qa-test-mode` 使用独立应用身份、`QStandardPaths` test mode 和清空后的隔离 `QSettings`。本轮补齐了一个安全缺口：测试模式没有显式缓存路径时，缩略图缓存默认到 test `CacheLocation/AgPlayer/Cache`，不再可能落到 Documents；正常应用的默认缓存路径不变。

### 缩略图延迟生命周期

全量 QML 暴露出真实生产路径缺陷：一次原生导入启动播放后，紧接着列表异步导入/模型变化会销毁可见 delegate；`Qt.callLater(root.performRequest)` 仍持有已失效的 QML 方法上下文，随后进入 `performRequest()` 时出现 `cancelRequest is not a function`。该问题不是 Task 7 测试清理造成：目标测试独立运行三次均通过，而包含前置真实导入的累计序列稳定失败。

最小修复把合并请求改为 wrapper 自有的 0ms `Timer`。Timer 仍只存在于可见范围附近的缩略图 wrapper 内，不增加常驻线程、解码或磁盘 I/O；wrapper/Loader 销毁会一并销毁未触发 Timer，从源头消除失效上下文回调。

## RED / GREEN

| Contract | RED | GREEN |
| --- | --- | --- |
| 右栏仅属于标签页；普通页回收宽度；共享列表身份/状态保留 | 新 Task 7 QML：77 pass / 1 fail / 1 offscreen skip，失败于右栏常驻 | 专用测试：3 pass / 0 fail；全量最终 78 pass / 0 fail / 1 offscreen skip |
| 停靠宽度遵守动态页面最小宽度且不抖动 | 新 `WindowController` contracts 在旧实现失败 | `window_controller_test` 通过 |
| 测试模式默认缓存不进入 Documents | 更新后的 `settings_controller_test` 在旧默认路径失败 | `settings_controller_test` 通过 |
| 缩略图延迟调用不会跨 delegate 生命周期 | 累计真实导入序列：17 pass / 1 fail，invalid QML context + `cancelRequest` warning | 相同序列：18 pass / 0 fail；全量 QML 0 fail |

## 真实应用证据

- Debug 可执行文件：`build/msvc-debug/app/AgPlayer.exe`。
- 隔离数据：10 个由项目 fixture generator 生成的真实 WAV、真实 ImportController 导入、真实 PlaybackController 播放；所有库/设置/缓存位于 `build/qa/task7-20260822`。
- 标签页三栏：`design-qa/implementation-tag-three-column-1447.png`。
- 非标签页两栏：`design-qa/implementation-non-tag-two-column.png`。
- 播放主/列表：`design-qa/implementation-tag-playing-main-1447.png`、`design-qa/implementation-tag-playing-list-1447.png`。
- 缓存命中/未命中：`design-qa/implementation-cache-hit-and-miss-visible.png`。命中项显示真实 v2 cache 波形；cache-only 未命中项不触发解码或写缓存。
- 等尺寸并排图：`design-qa/combined-final-blocked-source-vs-real-app.png` 已实际打开审阅；focused lower region 同样已打开。

## 验证结果

- MSVC Debug 构建 `AgPlayer`、`qml_main_window_test`、`settings_controller_test`、`window_controller_test`：通过。
- focused CTest 5/5：通过（窗口控制、设置、缩略图 provider/item/contract）。
- 全量 QML：78 passed, 0 failed, 1 skipped；唯一跳过项为 offscreen 环境不支持的真实 WM_DROPFILES。
- `git diff --check` 与最终 diff review 在提交前执行，结果记录在验收文档。

## 尚未完成的实机验收

外部 Windows UI 控制在最终恢复实验中被用户 Escape 中断，本轮按指令不再继续输入。因此独立三栏滚动、标签/歌单/目录 CRUD、拖拽、真实搜索排序，以及播放中的 seek/next/pause-resume + 同步列表交互没有完成；100–200% 只有真实应用自身 logical render 证据，没有物理桌面可达性证据。开发状态可提交，但视觉验收必须保持 blocked，详见根目录 `design-qa.md` 和验收记录。
