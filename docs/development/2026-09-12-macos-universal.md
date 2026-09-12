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

- 已实现 Universal CMake preset/triplet、`.app` 文档声明和图标、打包与逐文件 Mach-O/依赖/最低系统版本检查。脚本默认 ad-hoc development DMG；无证书时不宣称已公证或正式可分发。
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
- 文件：`C:\Users\Administrator\Desktop\AgPlayer-1.0.3-macOS-universal-development.dmg`，89,598,909 字节。
- SHA-256：`7bbbb418311adde3426d779d365a41d42f4f374bdec0d9a3f09d93525da0fda2`。下载的 Artifact ZIP、内部 DMG 及桌面副本校验均通过。
- 桌面同时提供同名 `.sha256` 和 `AgPlayer-macOS-安装测试说明.txt`。完整机器校验报告保留在 `build/macos-delivery/candidate-15a3259/`。
- 当前为 ad-hoc 签名、未经 Apple 公证的开发测试包；自动启动检查采用 macOS 15 离屏模式，不代表 macOS 13 实机、原生桌面交互、全模型推理或听音验收。Intel 离屏字体别名提示需在实际 Cocoa 窗口中复核，本次不据此改动字体策略。
- 未执行官网、R2、GitHub Release 或“版本发布”分支上线操作；等待用户人工测试后由其他任务负责发布。
