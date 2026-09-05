# 播放器性能与 UI 专项优化

## 范围与基线

- 当前源码：`D:/ai/AgPlayer`，`main`，起始 HEAD `b1c8a7c`。该主目录现已包含完整 Qt/QML 与 C++ 源码，不能套用早期稀疏主目录的结论。
- 开始时已有 34 个已跟踪文件修改及 2 个未跟踪文件。本轮保留这些工作，不提交、打包或安装。
- 延续 `2026-09-03-ui-design-system-acceptance.md` 已批准方向：暗色紫色、浅色蓝色、紧凑列表、现有 Theme 与公共组件。保留所有播放器外壳、音频工具、现有图标素材及业务入口。

## 检查结果与本轮修改

1. **预取阻塞前台波形分析**：当前曲目和后台预取共用单线程池，运行中的预取原先没有取消令牌。清除排队任务不会终止正在运行的解码；切歌和析构可能等待整首预取完成。本轮沿用现有取消机制，为预取批次设置独立令牌，在切歌和析构时取消。线程池数量、解码算法、缓存格式与公开接口不变。
2. **按钮文字缺少有效宽度与字体继承**：公共按钮的内部 Text 固定字体且没有宽度约束，窄按钮的 elide 实际不起作用。本轮把字体默认值设在控件上，让标签继承字体并按可用宽度截断。无障碍名称仍保留完整操作文本。
3. **字体与间距不统一**：真实音频工具窗口标题使用另一套字体，中文与设置标题风格不一致。改用 Theme.fontPrimary；列表筛选栏的评分、BPM、清空文字统一平台字体与字号，模块间距使用既有 8px token。
4. **图标平台依赖**：文件名处理的状态图标原先混合 Segoe UI Symbol 字符、警告字符和矩形拼接。改为已有 SVG 图标与语义状态色；公共图标按钮与标签页使用 Theme.iconSizeMd。没有新增资源依赖或改变用户指定的导航素材。

## 视觉基线

当前构建的设置与五个音频工具页面，中文深浅主题共 12 张截图及运行日志检查通过：`build/qa/professional-polish-before/`。本次实际查看了设置、编辑、分离、转换、元数据与文件名页面的代表状态。人声分离 QA 配置显示运行时未就绪，这是测试配置下的真实状态，不能据此推断用户安装的模型状态。

列表基线位于 `build/qa/professional-polish-before-library/zh-dark-list.png`。真实图像为 863×592、内容完整；现有矩阵脚本仅接受 590/604/888/906 高度，因此该次自动几何检查失败。不能把它记录为矩阵通过，也未为了通过而扩大脚本白名单。

用户正在播放的安装版仅用于观察，截图为 `build/qa/professional-polish-before/live-installed-*.png`。安装版观察与当前源码构建的验证分开记录。

## 跨平台剩余工作

- `GlobalHotkeyManager::registerNativeHotkey` 的真实系统注册仅实现 Windows；非 Windows 分支返回失败。
- `FileAssociationController` 的非 Windows 分支明确返回不支持，尚非完整平台适配。
- 多窗口磁吸依赖顶层窗口绝对位置。在 Wayland 下不能直接保证相同行为。推荐保留 Windows 行为，为 macOS/Linux 增加各自系统后端；Wayland 的停靠列表使用主窗口内面板，同时保留独立列表入口。此项涉及平台架构，需要确认后实施及原生环境验收。
- 缓存命中读取、分析结果保存仍有 GUI 线程 I/O，属于后续性能工作；本轮没有把这一项声称为已解决。
- 不能从 Windows 编译、离屏测试或 DPI 截图推断 macOS/Linux 原生可用，也不能以截图替代真实听音。

Qt 依据：[控件字体](https://doc.qt.io/qt-6/qml-qtquick-controls-control.html)、[线程池清队列与运行任务](https://doc.qt.io/qt-6/qthreadpool.html)、[高 DPI](https://doc.qt.io/qt-6/highdpi.html)、[Window/Wayland 位置限制](https://doc.qt.io/qt-6.8/qml-qtquick-window.html)。

## 验证记录

实施前：公共设计系统与源码使用合同 2/2 通过。新增按钮字体继承测试在旧构建中失败（14px 与预期 18px 不符），证明原问题可检验。

- 预取回归先红后绿：取消状态测试原结果 `AG_OK`、期望 `AG_CANCELLED`；真实运行中的预取测试原发布两次缓存、期望仅当前曲目的一次。修复后 `waveform_provider_test` 14 个 QtTest 项通过，0 失败、0 跳过。日志：`build/release/prefetch-red.txt`、`prefetch-behavior-red.txt`、`waveform-provider-green.txt`。
- 本机构建目录的 MSVC `/showIncludes` 仍输出中文，旧对象没有随新增头文件字段自动重编，造成中间测试的对象布局不一致和进程崩溃。本轮显式重新编译了所有直接包含相关头文件/测试访问头的 C++ 消费者，再完成所有目标链接。此前的中间测试结果不作为最终验收。日志：`build/qa/professional-polish-rebuild.log`。
- Release 所有目标重建通过；`all_qmllint` 通过；`git diff --check` 通过（仓库行尾转换提示不属于空白错误）。
- 排除一项审计误报：虽然 Qt 6.8 的 QML 文档标注方法版本，本机 Qt 6.7.0 离屏运行实测 `startSystemMove` 与 `startSystemResize` 均为 function，退出码 0；未因此升级依赖。探测文件：`build/qa/qml-window-method-probe.qml`。

- 全量 CTest 首轮：160/163 通过，638.21 秒；失败为 `format_matrix_test`、`video_playback_controller_test`、`qml_audio_editor_test`。完整日志：`build/qa/professional-polish-ctest.log`。前两项随后独立诊断运行分别 10/10、19/19 通过，未复现首轮失败，尚未证实波动原因。
- 编辑器失败可复现且属于本轮修改：控件层默认 font.weight 覆盖了导出按钮的 font.bold。已从控件层移除字重绑定，在标签层为普通字重应用 primary 默认值，保留调用方 Bold/DemiBold；保留原编辑器断言并新增加粗兼容回归。
- 独立 Review 又发现分析完成后取消、通知仍排队的边界。补入原子取消状态与 worker/GUI 回调检查；真实完成预取后切曲的新增测试先红后绿。最终波形目标为 15/15 通过，0 跳过；日志 `build/release/waveform-provider-final-green.txt`，独立复核无阻断问题。已完成的有效缓存允许保留，进入中的文件 I/O 无法强制中断。
- 验证期间另一个主线任务开始调整播放区、滚动波形、默认亮度及分离运行时。本轮与其协调文件所有权及 build/release 独占时段；后续构建属于共享主线快照，不声称包含的其他未完成工作均已验收。

- 字体修复过程中出现过 QML 整体 `font` 与子属性重复赋值错误；实际 QML 测试捕获该错误，改为分别继承 family/pixelSize/weight/italic/underline 后重新验证。不能仅以 qmllint 通过替代实际实例化。
- 最终字体修复后的所有目标构建通过；六项定向 CTest 全部通过，28.79 秒：波形提供器、音频编辑器、整合主题、设计系统、文件名处理、播放器控制布局。日志 `build/qa/professional-polish-ui-fix-build.log`、`professional-polish-ui-fix-tests.log`。
- 格式矩阵与视频控制器也在后续原 CTest 入口通过，见 `professional-polish-final-tests.log`；该轮另外五项 QML 失败属于上述重复赋值问题，已由最新六项回归覆盖。没有再次跑全量 163 项，不能声称最终全量全绿。

最终截图：设置、编辑器、文件名处理中文深浅主题六张矩阵通过，见 `build/qa/professional-polish-after/matrix.csv`。125%/150% 的深浅设置四张矩阵通过，见 `build/qa/professional-polish-hidpi/matrix.csv`；这是 Windows 缩放因子验证，不等同于跨真实显示器切换。列表深浅两张实际捕获均退出 0、无运行警告，保留 863×592 的原生布局，位于 `build/qa/professional-polish-after-library/`，人工查看筛选栏无溢出；没有声称旧几何白名单已通过。

SVG 补充显式 sourceSize 后，设计系统和文件名处理再次定向回归 2/2 通过（2.36 秒），见 `professional-polish-svg-tests.log`。共享资源重新编译后，文件名处理深浅两张最终矩阵通过，见 `build/qa/professional-polish-final-icons/matrix.csv`；实际查看暗色最终图确认摘要警告图标按 36px 槽缩放，与成功状态层级协调。先前六张图中的该页来自 sourceSize 修改前的二进制，以 final-icons 目录为最终证据。

最终 Diff Review 与 `git diff --check` 通过；预取取消经独立审查、UI 公共组件经独立审查及真实实例化回归。最终新增的 SVG sourceSize 用两项实际 QML 回归及两张最终截图验证；本轮 all_qmllint 的已通过记录早于最后字体/图标修整，不将其表述为最后源码快照的检查。

本轮不报告没有测量依据的 CPU/内存降幅，也不宣称平台后端或真实听音验收已经完成。
