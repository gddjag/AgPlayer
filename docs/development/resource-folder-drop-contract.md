# 资源文件夹拖放维护约束

用户于 2026-09-20 明确要求：后续版本更新不得无关修改、覆盖或回退已确认正常的资源文件夹拖放行为。必须修改此路径时，保留以下契约并完成相关回归，不能以普通音乐导入成功代替资源目录验收。

- 经典双窗口、单窗口、专业模式共用 `SideNavigation.submitResourceUrls`、`ResourceFolderController` 和 `LibraryNavigationModel`；不得重新分成三套目录提交或状态。
- 资源文件夹上方分隔线以下的资源区域（含侧栏留白）接受目录，注册资源根、扫描并显示真实歌曲数量；线以上音乐库/歌单保留原规则。
- 原生事件不能仅凭接收窗口判断落点。实际 Windows 拖放曾将列表窗口上的落点交给主窗口，坐标超出主窗口范围，导致只导入歌曲而不注册目录。`NativeDropRouter::windowAtDropPosition` 将坐标映射到实际注册窗口，再按共享资源区命中规则处理；Qt 与 Windows HDROP 路径均保留此校正及 DPI 换算。
- 移除资源目录不得删除磁盘音乐；扫描期间导入器忙碌不能丢弃新文件。

## 验证依据及后续修改门槛

- `native_drop_router_test::routesOwnerDeliveredDropToWindowUnderPosition`：先复现错误路由到 Main，修复后路由 ResourceFolder 且坐标正确。
- `tst_main_window.qml`：`test_classic_resource_folder_drop_routes_and_counts` 覆盖 owner-delivered、Qt、QML、HDROP、侧栏留白，验证目录出现及真实数量；`test_embedded_resource_folder_accepts_native_directory_drop` 与 `test_resource_folders_share_submission_and_counts_across_shells` 验证另外两种模式及共享状态。
- 本次 Windows 原生路由测试 10 项通过，相关 QML 测试 13 项通过（均含初始化/清理）；Windows 主程序增量构建通过。
- 用户在修正后诊断版再次从资源管理器拖入，确认“已经正常显示”。本次没有新增 macOS 实机验收。
- 今后若涉及原生窗口、拖放、侧栏布局、目录扫描或这段共享逻辑：运行受影响测试，并从 Windows 资源管理器实际拖入双窗口分隔线以下，确认目录与数量；不得只用直接调用导入函数或合成事件宣称端到端通过。
