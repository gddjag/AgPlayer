# macOS 13+ Universal 开发记录

基线：Windows 1.0.3，提交 `9b90ddd1`。本记录描述开发范围，不是发布验收证明。

## 已确认要求

- 最低 macOS 13.0；一个 Universal 应用包/DMG，主程序、子进程、随包动态库均包含 `x86_64` 与 `arm64`。
- Windows 1.0.3 的功能全部纳入 Mac 验收；复用代码与 UI 布局，按 Mac 习惯适配系统行为。
- 分离功能保留模型、操作和输出；用户接受按硬件加速，Intel 提供 CPU，Apple Silicon 的 CoreML/MPS 逐模型验证。
- 环境/模型状态、进度、失败原因和一键部署留在模型卡片内。已确认不兼容的模型显示原因和官方支持链接；未验证不能标成不兼容。
- 大模型及推理运行时仍按需外置，不随基础应用打包。
- 不修改已发布的 Windows 版本号、下载配置或线上文件。用户指定沿用 `gddjag/AgPlayer`；开发构建使用 `codex/macos-universal`，仅生成测试 Artifact。本会话负责将验证后的测试 DMG 放到 `C:\Users\Administrator\Desktop`，不执行线上发布。用户人工测试通过后，由其他会话通过“版本发布”分支正式上线。

## 实施与验收

- [x] 构建：macOS Universal preset、vcpkg 双架构 triplet、部署目标 13.0、生产目标与测试目标分离。
- [x] 打包：`.app`、文档类型与图标、Qt/QML/动态依赖、Worker；逐 Mach-O 校验两种架构、最低系统版本、可迁移依赖。
- [ ] 系统适配：全局快捷键/媒体键、登录项、文件关联、Finder 打开、Dock 和窗口行为。
- [ ] 分离：按平台/架构选择可验证运行时，模型卡片提供一键部署、失败信息和官方链接；CPU/加速执行分别验证。
- [ ] 测试：本地验证配置与脚本；Mac 原生构建、两种架构运行、macOS 13 最低系统、UI 截图与真实听音。

初始构建使用现有 vcpkg baseline；其 FFmpeg 构建脚本已实现多架构编译和 lipo 合并，复用该能力，不另写依赖合并系统。
本地为 Windows、未配置 Git remote；现已使用 GitHub Actions Mac runner 执行开发构建。只在真实执行后记录 Mac 编译、启动和功能通过。

## 2026-09-13 本地实施结果

- 已实现 Universal CMake preset/triplet、`.app` 文档声明和图标、打包与逐文件 Mach-O/依赖/最低系统版本检查。无证书时只生成明确标记的 internal ad-hoc test DMG，不宣称已公证或正式可分发。
- 已实现 Finder 启动期间文件事件排队、Carbon/媒体键输入监控、Command 修饰键映射、SMAppService 登录项、文件关联及准确错误提示。保留现有 UI 布局；Mac Dock/多窗口/全屏习惯及视觉细节仍需实机验收。
- ONNX 采用官方 ORT 1.18.1 NuGet，两个架构原生库最低 macOS 11.0，使用 API18；Windows 保持 API24。1.20.1 官方 Universal 包最低 13.3，因此不能用于本项目 13.0 基线。
- 外置 VR：Mac Python 3.10.18；Intel 为 separator 0.24.1 / torch 2.2.2，ARM 为 separator 0.30.2 / torch 2.5.1。两架构纯 wheel 依赖解析通过，uv 官方文件已下载复核哈希。ARM VR 使用上游已有 polyphase 重采样路径，避开 samplerate wheel 中仅 Intel 的动态库；真实音质仍需验收。
- 原卡片保留配置、进度、暂停/继续、验证失败原因以及官方 macOS 支持链接；未验证不会直接判定为不兼容。Unix 取消会终止工作进程组，覆盖派生 FFmpeg。
- Mac 更新地址独立预留为 `https://download.agplayer.com/updates/macos/latest.json`；现有 Windows 地址不变。该 Mac 地址尚未部署。

本地证据：Windows Release 主程序与受影响目标增量编译通过；配置/Finder 事件两个 CTest 通过；系统设置、文件关联、分离安装/运行时/进程取消/控制器/卡片等重点回归通过；主窗口 QML 150 passed、0 failed、1 skipped（离屏环境不支持 WM_DROPFILES）。修复了原主窗口导入测试未清空前一拖放用例数据的隔离问题。Python 桥接 6 项通过；打包策略 22 项中 20 passed、2 skipped（Windows 无符号链接权限）。这些都不是 Mac 原生验证。

GitHub 核对：公开仓库 main 是官网与 Windows 1.0.3 发布内容；现有 R2 workflow 仅由发布事件或手工启动触发。Mac 构建源码已放在独立开发分支 `codex/macos-universal` 并保留全部现有网站文件，未合并 main、未运行 R2 发布任务。

云端进展：ARM Mac 上打包策略 22 项全部通过、Python 桥接 6 项及目标约束 5 项通过。原生构建遇到 LAME configure 将双架构编译参数传入预处理器的错误；triplet 仅为该 port 指定单次预处理命令，实际编译和链接仍为双架构。开发构建 run `34706158425`（提交 `103be58aa00e12dabaaff3e9fa3d1c5088b63a4b`）已通过 LAME 编译，进入 FFmpeg 双架构构建，但于 2026-09-13 00:52 左右被取消；同期旧 run `34705817926` 被外部重新运行为 attempt 2。用户随后确认直接打包交付，由用户拷贝到 Mac 测试；本会话已恢复最新源码的构建作业。尚未产出 DMG，未执行线上发布。

后续仍需：macOS 13.0 实机验证、真实模型 CPU/CoreML/MPS 推理、全功能人工测试和听音，以及正式分发前的 Developer ID 签名/公证与 Mac 依赖许可清单核对。

## 功能验收范围

播放/播放模式与队列、音乐库/列表/标签、搜索/导入/拖放、波形/视频/歌词、EQ/变速变调、主窗口/迷你/滚动/沉浸模式、音频编辑/转码/文件名/元数据/无损识别、人声分离、设置/主题/语言、快捷键/文件关联/登录项、更新与反馈。
每项需在后续 Mac 验收记录中给出证据；构建或局部测试通过不代表本清单全部完成。
# macOS UI pass — 2026-09-13

User requested `ui-ux-pro-max` optimisation while preserving the existing layout.
Apple HIG is the standing platform rule in `UI_DESIGN_SYSTEM.md`, section 9.

- Shared QML traffic-light controls in the existing main, audio-tools, settings,
  equalizer, list and mini-player headers. Custom frameless controls, not AppKit
  standard buttons; existing close/unsaved-work callbacks retained.
- Native macOS application/window menu with Settings, Quit, Close and Minimise;
  Command shortcuts and native shortcut labels, portable persisted values.
- Reused system fonts, theme tokens, keyboard focus and accessible button labels.
- Windows Release application build passed. Existing QML main suite: 150 passed,
  0 failed, 1 native-drop skip. New component suite: 5 passed; settings controller
  tests and design-system contract passed. Component qmllint passed. Dark/light
  header previews rendered using Windows native QPA; these are not Mac acceptance.
- Universal application and selected test binaries compiled successfully on the
  ARM Mac runner (Qt 6.8.3). Run 34713973310, commit 6ef0e52, passed 26/26 CTest
  groups in 137.59 seconds. Audio-engine and queue tests each passed three more
  consecutive runs (42.99 seconds total). These are automated checks on macOS 15;
  they do not establish macOS 13 hardware acceptance or resolve every timing risk.
- Related Windows tests passed 3/3 in 69.81 seconds; the null-backend audio-engine
  suite also passed five consecutive local runs. Playback core was not changed.
- The animated volume flyout now respects reduced room immediately during resize.
  Mac-rendered dark/light component screenshots were inspected. Physical Mac
  menu/window/model/audio acceptance remains separate from these CI results.
  No online release is authorised in this task.

## 2026-09-13 测试包交付

- 应用源码：`15a3259997bacca5e5a07ca8184390cde443fe26`。
- 构建/打包及 ARM 启动：GitHub run `34715503391` 的 build job 成功；26 组重点测试通过，播放引擎和队列各额外连续通过 3 次。
- Intel：首个检查因 `lipo` 参数顺序错误而未启动程序；修正后，run `34716248310` 复用上述同一个 Artifact，双架构、签名和 Intel 离屏启动检查全部通过。未重新编译应用。后续分支提交只涉及 CI 或验收记录。
- 打包检查：105 个 Mach-O 文件，每个均包含 `arm64` 和 `x86_64`；最低系统声明不高于 13.0，依赖与符号链接均通过包内检查。Qt 运行时插件保留原生平台、图像、网络等组件和 SQLite；不部署项目未使用的外部数据库驱动。移除重定位后指向包外的 Qt SDK 搜索路径。
- 已撤回的失败文件：`C:\Users\Administrator\Desktop\AgPlayer-1.0.3-macOS-universal-development.dmg`，89,598,909 字节；不得继续用于验收或发布。
- SHA-256：`7bbbb418311adde3426d779d365a41d42f4f374bdec0d9a3f09d93525da0fda2`。下载的 Artifact ZIP、内部 DMG 及桌面副本校验均通过。
- 桌面同时提供同名 `.sha256` 和 `AgPlayer-macOS-安装测试说明.txt`。完整机器校验报告保留在 `build/macos-delivery/candidate-15a3259/`。
- 当前为 ad-hoc 签名、未经 Apple 公证的开发测试包；自动启动检查采用 macOS 15 离屏模式，不代表 macOS 13 实机、原生桌面交互、全模型推理或听音验收。Intel 离屏字体别名提示需在实际 Cocoa 窗口中复核，本次不据此改动字体策略。
- 未执行官网、R2、GitHub Release 或“版本发布”分支上线操作；等待用户人工测试后由其他任务负责发布。

## 2026-09-13 启动闪退诊断与签名规则

- 用户在 macOS 14.8.5 ARM64 从 `/Applications` 启动后，`dyld` 拒绝加载 `QtQuickControls2.framework`，原因为主进程与映射文件的 Team ID 不一致；日志中的 `Library missing` 不是文件缺失。
- 对上述失败 DMG 挂载后的最终 App 做逐文件核对：105 个 Mach-O（主程序、Worker、Qt Frameworks、QML/Qt 插件、FFmpeg 及其他随包动态库）全部为 ad-hoc，全部 `TeamIdentifier=not set`，且全部启用了 Hardened Runtime。没有发现 Qt 官方正式签名残留；问题是没有共同 Team ID 的 ad-hoc 组件被放进了要求同 Team ID 的 Library Validation 关系。
- 内部测试包：所有随包原生代码仍由内到外统一 ad-hoc 签名，但不启用 Hardened Runtime，也不给主程序增加 Disable Library Validation 权限；文件名必须包含 `internal-adhoc-test`。它未经 Apple 公证，不能标为正式发布就绪。
- 正式发布包：完成部署、Universal 合并、strip、`install_name_tool` 和资源写入后，再用同一个 `Developer ID Application` 身份由内到外签署全部随包非系统原生代码，App 最后签名；逐文件验证 Team ID 和 Hardened Runtime，随后签署 DMG、提交公证并验证 stapling。`--deep` 只用于最终验证。
- 最终安装验收必须挂载生成的 DMG、复制到 `/Applications`，再通过 LaunchServices 启动；不能只运行打包目录或 ZIP 中间产物。

## macOS 13 至 27 兼容目标

- 部署目标固定为 macOS 13.0，单个 DMG 内的全部 Mach-O 都必须同时包含 `arm64` 与 `x86_64`，且任何 slice 的最低系统版本不得高于 13.0。
- 云端安装启动矩阵覆盖 GitHub 当前提供的 macOS 14、15、26（Apple Silicon）、macOS 15/26 Intel，以及实际运行 macOS 27.0 ARM64 的 `xcode-27` 预览环境。每个环境都挂载最终 DMG、检查签名、安装到 `/Applications` 并通过 LaunchServices 启动。
- macOS 13 当前没有标准免费 GitHub 托管运行器，必须在可用实机或专用云 Mac 上补测后才可声明通过。macOS 27 当前仍为预览系统，即使自动启动通过，正式版发布后仍需重跑签名、安装、播放和人工 UI 验收。

## 2026-09-13 macOS 14 人工反馈修复

- 双窗口：全屏与最大化期间隐藏停靠列表，恢复后重新按主窗口尺寸定位；避免把全屏/最小化几何写入正常窗口配置。Mac 停靠列表的最小宽度随主窗口约束，解决原 956 像素最小宽度阻止 Cocoa 缩回的问题。
- Dock 恢复：响应 Qt Cocoa 的应用重新激活事件，恢复被最小化或隐藏的主播放器/迷你播放器，并保留当前模式。
- 标题：主窗口和音频工具窗口的品牌组在 Mac 标题栏居中，沿用现有控件、主题和布局。
- 分离：非 Windows 平台查找 `AgSeparationWorker`，不再误用 `.exe` 文件名；最终安装验证增加 Worker 协议握手。本次没有替换模型、推理运行时或签名策略。
- 无损鉴别：用户确认原症状为“能选文件，确认后没有入列”。选择结果转成独立字符串数组后交给控制器；控制器兼容编码 file URL 和本地绝对路径，包括中文、空格、#、%。自动用例验证真实队列入列及完成鉴别；未用鼠标操作 Mac 原生文件选择面板，不将该路径写成已人工验收。
- 快捷键：统一解析组合键中加号周围的空格，验证搜索、音频工具、波形切换；编辑文字或音频工具可见时，波形快捷键不截获 Tab 导航。
- 性能：Mac 所有窗口均不暴露时，暂停频谱 QVariantList 构造和界面通知；保留音频播放及队列轮询。未新增依赖或常驻服务，未测量并宣称 CPU/内存改善百分比。

本地 Windows Release 增量构建通过。窗口控制器 60 passed / 10 skipped，无损控制器 37 passed，播放控制器 56 passed；新增 QML 回归 4 passed / 1 个 Mac 标题定位跳过，文件选择快照定向检查 3 passed。额外执行的无损工作台离屏界面套件为 13 passed / 1 failed：详情证据提示的 Tab 焦点断言未通过；文件导入、布局和批量操作用例通过。该焦点行为需人工复核，未据此扩展重构范围。

云端构建遇到两项新纳入窗口测试的移植问题：Qt 6.8 的 QSKIP 可变参数宏触发 C++17 严格警告，以及左吸附断言使用了 Windows 原生边框坐标。分别改用等价 qSkip 调用和平台明确的精确坐标断言，没有删除测试或放宽误差。另两项保存测试用固定 300 ms 等待 250 ms 防抖定时器，在云端调度下偶发未保存；改为限时等待并精确比较真实保存的 QRect，不修改生产保存逻辑。

最终源码提交：`e25df82639edab192d229733117b2e217b263a4b`，GitHub Actions run `34740973214` 全部成功。

- 原生 CTest：28/28 通过；音频引擎和队列另各连续通过 3 次。Cocoa + software rendering 界面回归：5 passed / 0 failed，覆盖居中标题、组合键触发、真实无损队列及分析。已查看 Mac 标题栏截图，品牌组居中。测试标题组件未绑定真实窗口的两个 null visibility 警告已记录，不将该孤立截图作为完整窗口控件验收。
- 同一最终 DMG 挂载、签名检查、安装到 `/Applications`、LaunchServices 离屏启动及分离 Worker 协议握手全部通过：ARM 的 macOS 14.8.9、15.7.9、26.6.2、27.0 预览版；Intel 的 macOS 15.7.9、26.6.1。该结果不等于原生文件面板点击、真实模型推理、全部 UI 交互及硬件听音完成。
- 105 个 Mach-O 均有 arm64 + x86_64，所有 slice 最低系统不高于 13.0。全部为 ad-hoc、无 Team ID、无 Hardened Runtime，签名完整性通过；未公证。macOS 13 实机验收仍待补齐。
- Artifact `10313240404` 的 ZIP SHA-256：`665a6d6c39bca5b3a87f2748bec9bbdeb463b449371589bb3e325476b38b927d`。
- 桌面新文件：`AgPlayer-1.0.3-macOS-universal-mac14-fix-internal-adhoc-test.dmg`，89,614,944 字节；SHA-256：`c3c40ac5a7af38e0e1fa6c8849eb921fb8c522b73afd09d602047c571b988578`。云端报告、下载文件及桌面副本校验一致。另提供同名校验/验证报告和更新的 `1-安装测试说明.txt`。
- 证据保留在 `build/macos-delivery/mac14-e25df82/`。没有新增依赖、常驻服务或执行线上发布；等待用户用新包人工复测七项反馈。

## 2026-09-13 无损鉴别拖入修复

用户再次报告无损鉴别无法加入音频，并明确补充“拖不进去”。本轮定位到 `NativeDropRouter` 消费 DragEnter 后未处理 DragMove，Qt Quick 又没有收到前置 DragEnter，移动期间会拒绝拖放。原有自动测试只发送 Enter/Drop，因此没有覆盖此路径；加入 DragMove 后在修复前复现 `move.isAccepted() == false`。

- 原生路由持续接受合法 URL 的 DragEnter/DragMove，保留列表目录的 QML 命中分流。没有改变文件分类、播放器或鉴别算法。
- 无损页面复用现有 `FileDropArea` 接收 Qt 拖放，沿同一个导入函数把选择结果交给真实分析控制器。
- 空选择及不存在文件的错误在页面现有文字区显示，宽窄布局均可看到；正常状态布局保持不变。
- 本地 Windows 原生拖放路由 9 passed，新增无损界面导入回归 6 passed（完整拖放、文件对话框 selectedFile/accepted 回调、真实入列和鉴别完成、空选择及路径错误），共享主窗口/资源目录拖放 6 passed。Windows WM_DROPFILES 用原生 windows QPA 复核通过；offscreen 不支持该消息，不能拿其失败当作原生回归失败。
- 文件对话框回归覆盖选择属性及确认信号的 QML 边界，没有操作系统原生面板鼠标点击；这仍需用户真实安装后确认。当前已验证的故障根因是拖动事件链中断，没有把此前路径解析修改重新宣称为本轮新根因。

本轮源代码提交：`f4fa2749f583135e8a44abd47484ce744ed8b3f0`；云端作业 `34747708307`，最终包验收结果待完成后补记。

后续验证记录：首次 Mac 作业的拖放测试暴露出测试数据使用 Windows C:/ 路径的问题；改为 QTemporaryDir 本地绝对路径后，提交 efd4d3787ebba09fafb3b23e3a885e4f5e2b9297 的作业 34748252708 首次尝试已通过 30/30 原生测试组、Cocoa 5 项窗口回归及 6 项无损导入回归。随后额外重复的 audio_engine_test 在第三次视频 seek 检查中偶发失败（snapshot.state == AG_PLAYING，实际 state=0 / position=2000 / duration=2000），队列重复测试全部通过。本次未修改播放逻辑或降低测试门槛；保留失败证据，并对相同提交重试一次云端任务。

最终交付：同一提交 efd4d3787ebba09fafb3b23e3a885e4f5e2b9297 的 run 34748252708 第 2 次尝试全部通过。30/30 原生组、音频引擎和队列各 3 次重复、Cocoa 窗口 5 项及无损导入 6 项全部通过；首次重复视频跳转失败仍记录为已有偶发问题，没有据此声称播放模块无缺陷。

最终 DMG 的 105 个 Mach-O 均包含 arm64+x86_64，所有 slice 最低系统声明不高于 13.0，统一 ad-hoc、无 Team ID、无 Hardened Runtime，完整性通过且未公证。六个环境均挂载同一 DMG、签名检查、复制 /Applications 后通过 LaunchServices 离屏启动并完成 Worker 握手：ARM macOS 14.8.9、15.7.9、26.6.2、27.0 预览；Intel macOS 15.7.9、26.6.1。Finder 实际鼠标拖放、原生文件面板点击及 macOS 13 实机仍待人工复测。

桌面交付文件 AgPlayer-1.0.3-macOS-universal-lossless-import-fix-internal-adhoc-test.dmg，89,616,142 字节；SHA-256：2d2bb542b5bc6927ef91745de6ca71f5a55e5d71e9bfa6adb7ece24684aa99ab。Artifact 10315361491 的 ZIP 摘要 ea88f0e2108f342aca03f4af5859109fb6be84d370b88720e0baf3f54b9ee46c 核对通过；报告中 DMG 摘要、下载文件及桌面副本一致。证据在 build/macos-delivery/lossless-efd4d37/，已更新桌面 1-安装测试说明.txt，保留旧包。未执行线上发布。

## 2026-09-13 用户验收后上线 1.0.3

用户明确确认人工测试通过并授权本任务发布，上线授权取代此前仅交付测试包的限制。没有重新编译或改变已验收 DMG 的字节，对外名为 AgPlayer-1.0.3-macOS-universal.dmg，89,616,142 字节，SHA-256 2d2bb542b5bc6927ef91745de6ca71f5a55e5d71e9bfa6adb7ece24684aa99ab。它仍为 ad-hoc、未公证，官网与 Release 已明确说明并链接 https://support.apple.com/zh-cn/102445；不宣称 Developer ID 签署或已公证。

沿已有 codex/publish-1-0-3 发布分支完成接入，保留原发布标签与 Windows 附件。GitHub macOS asset 561082964 上传校验通过；R2 run 34754176669 同步两个安装包成功，完整公开下载的 DMG 哈希一致。网站最终提交 546127962a275cdb696c530a8d3231a92fc75ead，Cloudflare Pages b0ce8d0b-9b6f-45bb-b8f5-6835797f1ce4 成功，线上下载页有两端链接、校验值和复制按钮。发布规则与同步脚本明确自 1.0.3 起两端同版本、同标签、两份完整包齐备才提升清单。17 项独立 Node 用例通过（同步 8 项、下载页 9 项）；中英文页面初次本地预览已检查，新增复制动作以回归用例及线上 HTML 确认。后续线上浏览器截图受工具超时影响，未伪称完成真实系统剪贴板点击验收。

源码、相同安装包、网站源码及发布记录已归档至 E:\AgPlayer 备份\2026-09-13-v1.0.3\macos；桌面提供同名 DMG。旧版本发布任务无法通过本会话工具直接发送消息，已经将交接记录写入发布分支 docs/releases/1.0.3-macos.md，不宣称已跨任务通知。
