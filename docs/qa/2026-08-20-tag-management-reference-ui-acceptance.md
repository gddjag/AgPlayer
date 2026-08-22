# 标签管理参考 UI 验收记录

执行日期：2026-08-22
参考：`design-qa/source-tag-management-1447x1087.png`
最终结论：**BLOCKED**

## 需求追踪

| 必须达到的要求 | 生产实现 / 自动验证 | 真实应用证据 | 状态 |
| --- | --- | --- | --- |
| 标签页为导航 + 共享列表 + 标签栏 | `ListWindow.tagManagementMode`; Task 7 transition test | `implementation-tag-three-column-1447.png` | 结构通过 |
| 普通库/收藏/歌单/资源页完全隐藏标签栏并回收宽度 | 同一 transition test，覆盖 tag → playlist/library/favorites/resource → tag | `implementation-non-tag-two-column.png` | 通过 |
| 仅一个共享 TrackList，切页保留相关状态 | 指针身份、selection/search/currentTrack assertions | 同一窗口结构截图 | 通过 |
| 1447×1087 同尺寸同状态并排比较 | 参考与诊断 implementation canvas 均为 1447×1087 | `combined-final-blocked-source-vs-real-app.png` | 阻塞：实现为两次 app-owned grab 的诊断组合，不是单帧桌面；状态/高度不等价 |
| 字体、间距、列宽、62/42 行高、颜色、边框、圆角、选中态、滚动态 | 列宽/62/42 有 QML contract | combined + focused region | 阻塞：P2 差异和真实滚动状态未清零 |
| 真实标签、歌单、目录及 CRUD/取消 | controller-path 自动测试存在 | 无完整外部实机操作证据 | 阻塞 |
| 标签点击筛选、搜索、添加、重命名、改色、删除 | Task 5/6 QML tests | 无外部实机操作证据 | 阻塞 |
| 歌单 CRUD/import；目录添加/移除引用且不删磁盘 | Task 5/6 QML tests | 无外部实机操作证据 | 阻塞 |
| 单/多选拖拽、预览、取消、允许 drop | Task 5 QML tests | 无外部实机操作证据 | 阻塞 |
| cache hit + miss；miss 不在滚动热路径解码 | provider/contract tests | `implementation-cache-hit-and-miss-visible.png` | 真实渲染通过 |
| Dark / Light | 真实应用主题路径 | `tag-theme-dark.png`, `tag-theme-light.png` | 渲染通过；交互未验 |
| 100/125/150/175/200% logical DPI | Qt per-process scale + real app-owned capture | `tag-dpi-*.png` | logical render 通过；物理可达性阻塞 |
| 实际播放、position、seek、next、pause/resume、同时滚动/搜索 | PlaybackController/audio output | playing main/list evidence | position/output path已执行；其余阻塞 |

## 像素比较结论

`combined-final-blocked-source-vs-real-app.png` 与 `combined-final-blocked-lower-region.png` 已用原始图打开检查。

- 参考与实现比较面板均保持 1447×1087，没有通过缩放掩盖差异。
- 已修 P1：普通页不再保留空标签栏；标签页不再被停靠同步裁回 1104px。
- 未清 P1：实现下半窗只有 570px，诊断图使用 177px 显式底部空白暴露高度差；没有同一时刻、同一外部窗口状态的完整桌面截图。
- 未清 P1：隔离库没有可安全复现参考中的 56 个真实标签、歌单树和资源目录树，因此标签胶囊、计数、滚动条及右键菜单无法像素级验收。
- 未清 P2：参考为紫色柔和选中态、较密集文字/元数据/封面层级；实现使用亮蓝播放行、fixture 标题和占位封面，视觉差异明显。
- 未观察到 P0 崩溃、全窗口白屏或关键控件不可见。

## DPI 与主题矩阵

| 状态 | Artifact | Logical size | 结论 |
| --- | --- | --- | --- |
| Dark 100% | `tag-dpi-1-dark.png` | 1447×570 | 完整 render；physical 未验 |
| Dark 125% | `tag-dpi-125-dark.png` | 1447×570 | 完整 render；physical 未验 |
| Dark 150% | `tag-dpi-15-dark.png` | 1447×570 | 完整 render；physical 未验 |
| Dark 175% | `tag-dpi-175-dark.png` | 1447×570 | 完整 render；physical 未验 |
| Dark 200% | `tag-dpi-2-dark.png` | 1447×570 | 完整 render；physical 未验 |
| Light | `tag-theme-light.png` | 1447×570 | 无白底白字/裁剪；external interaction 未验 |

## 播放证据边界

真实 Debug AgPlayer 使用真实 WAV fixture 和配置的 Windows audio output path 启动播放。主窗显示 pause 状态、`track-01`、0:01/0:02 进度、主波形和 210 BPM，证明 position 推进和真实播放控制器路径被执行。没有人工听音，因此不声称音质、鼓点、爆音或卡音通过；没有执行 seek、next、pause/resume 与播放中列表滚动/搜索，所以整体播放验收仍阻塞。

## 测试与回归

- Debug build：通过。
- focused CTest：5/5 passed。
- Task 7 共享列表专用 QML：3/3 passed。
- thumbnail delayed lifecycle 独立 RED：2 passed / 1 failed（销毁后仍 dispatch）；GREEN：3/3 passed、0 request/cancel、无 warning。
- full QML：79 passed / 0 failed / 1 skipped（native WM_DROPFILES requires qwindows，offscreen 跳过）。
- `git diff --check`：通过。

## Computer Use 恢复记录

fresh sky session、`list_apps/list_windows`、安全 isolated `.lnk` 均尝试过；真实 HWND 存在但未被枚举。`sky.launch_app` 不支持安全参数，故未对用户默认数据根启动。随后仅做 `Qt.Window` 顶层标志最小实验，最终 sky 观察被用户 Escape 中断；实验结论 inconclusive，hunk 已撤回，隔离 PID 已核验并停止，本轮未再发送 UI 输入。

## 阻塞项

1. 缺少代表性真实标签/歌单/资源目录状态下的单帧 1447×1087 外部桌面截图。
2. 缺少左右中三栏独立滚动与全部真实 CRUD/drag/drop/sort/search/filter 操作证据。
3. 缺少 100–200% physical Windows desktop reachability 证据。
4. 缺少 seek/next/pause-resume 与播放同时滚动/搜索证据。
5. 参考与实现仍有 P2 字体、密度、选中色、封面/元数据和标签胶囊差异。

因此不得标记通过；需要恢复可枚举的安全真实窗口控制后继续同状态复验。
