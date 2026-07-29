# AgPlayer 原始需求追踪

日期：2026-07-29
当前交付范围：Qt 6/QML + C++17 Windows MVP；未部署运行库、未制作安装包。

## 状态定义

- **已验证**：已有与该需求同范围的自动化或可重复本机证据。
- **仅源码**：存在实现代码，但现有测试未覆盖真实操作系统、硬件或完整副作用。
- **待人工**：软件侧已具备基础能力，仍需真实设备、视觉、听感或发布验收。
- **未实现**：缺少要求中的实现，或当前控件不能兑现其文案。
- **非Windows MVP**：属于 macOS、Linux、HarmonyOS 或移动端后续范围。

## 技术架构、质量与性能

| 原始要求 | 当前证据与边界 | 状态 |
|---|---|---|
| Qt 6 / QML + C++17 | 顶层 CMake、`app/`、`qt/`、`core/` | 已验证 |
| miniaudio 音频输出 + FFmpeg 解码/转码 | `core/src/audio_engine.cpp`、`decoder.cpp`、`transcoder.cpp` 及核心测试 | 已验证 |
| SoundTouch 变速/变调 | 当前使用自研 OLA：`core/src/pitch_shifter.cpp`；构建未引入 SoundTouch | 未实现 |
| kissfft 频段/频谱分析 | 构建未引入 kissfft；当前频段数据由离线分析器生成 | 未实现 |
| C++ 核心与 Qt 解耦、稳定 C 接口 | `core/` 无 Qt 依赖；`core/include/agplayer/c_api.h`；C API 测试 | 已验证 |
| HarmonyOS NAPI 边界 | 当前只有标准 C API，无 NAPI/ArkUI 工程 | 非Windows MVP |
| 编译零错误、零警告 | Debug/Release `/W4 /WX` 已构建；CTest 44/44 的 Phase 8 记录 | 已验证 |
| 安装包尽量小且小于 20 MB | 未部署的单个 Release EXE 为 3.38 MiB；尚未制作含依赖安装包 | 待人工 |
| 完整应用运行内存小于 60 MB | 16.04 MiB 是 10,000 波形压力工具峰值，不等同完整 GUI 播放器 | 待人工 |
| Seek 延迟小于 20 ms | Release 基准 100/100，P95 0.0222 ms | 已验证 |
| 10,000+ 曲库秒级搜索、排序、随机访问 | 内存模型压力测试覆盖 10,000 行 | 已验证 |
| QML ListView 虚拟化 + C++ 异步分页/懒加载 | 无 `canFetchMore`/`fetchMore`；`LibraryStore::load()` 同步 `readAll()` | 未实现 |
| 10,000+ 首导入 0 延迟、滚动 60 FPS | 现有压力测试不渲染 ListView、不测分页或帧时间 | 未实现 |
| 所有按钮/图标都有真实副作用 | 输出设备与歌词入口已接通；UI 矩阵仍只截图，`visible_controls_have_actions` 仍未覆盖全部点击副作用 | 未实现 |

## 播放、曲库与窗口

| 原始要求 | 当前证据与边界 | 状态 |
|---|---|---|
| MP3/WAV/FLAC/AAC/M4A/OGG/Opus/WMA 解码与导入 | FFmpeg、`ImportController`、格式矩阵和 8 格式曲库冒烟 | 已验证 |
| 播放、暂停、Seek、音量、静音 | 引擎、控制器、真实 WAV 无声卡自动化冒烟 | 已验证 |
| 真实扬声器可听播放、Seek、切歌、音量、静音 | 自动化只能验证状态、进度与日志 | 待人工 |
| 顺序、随机、单曲循环、列表循环 | `PlaybackSession`、C API、Qt/QML 与队列测试 | 已验证 |
| 无间隙播放 | 预解码队列与 `queue_gapless_test` | 已验证 |
| 歌曲切换淡入淡出 0/200/500 ms | 设置页明确为“自动切歌淡入淡出”；自动队列边界已实现 0/200/500 ms 双边包络，手动切歌仅淡入，完整要求仍待异步切换状态机 | 未实现 |
| 自动匹配每首歌曲采样率 | 设置开关、C API/Qt 链路和 44.1→48 kHz 队列测试已完成；开启时在边界排空缓冲并按下一曲原生采样率重建设备，关闭时保持固定会话无缝策略；真实 WASAPI 多声卡待人工 | 已验证 |
| 输出设备选择 | miniaudio 稳定设备 ID 枚举/切换已贯通 C API、Qt 与设置页；设备丢失可恢复解码，Null 后端和 QML 回归通过；真实多声卡/热插拔待人工 | 仅源码 |
| WASAPI/ALSA 独占模式 | exclusive share mode、共享模式自动回退和有效运行态已贯通；回退不覆盖用户偏好；真实设备兼容性待人工 | 仅源码 |
| 自动读取 BPM | `ImportController` 调用 BPM 分析；真实 click-track 测试 | 已验证 |
| 拖拽/选择器批量导入、歌单、收藏、评分、历史、排序 | 曲库、歌单、导入模型及存储测试 | 已验证 |
| 空库启动页；有曲库时恢复播放页与上次位置 | `Main.qml`、启动链路、控制器和 QML 测试 | 已验证 |
| 关键词/评分/BPM 双端筛选、手输数值、200 ms 防抖、实时结果 | `LibraryFilterModel`、`SearchFilter.qml` 及模型/QML 测试 | 已验证 |
| 搜索命中高亮、无结果提示、一键清空 | `SearchFilter.qml`、列表 delegate 与 QML 测试 | 已验证 |
| 主播放器 + 独立歌单/歌词窗口 | 独立窗口、歌词面板、可见入口与开关回归均已通过 | 已验证 |
| 歌单窗口折叠、四向 15 px 磁吸、联动拖拽、单独解绑、同步最小化 | `WindowController` 与窗口控制测试 | 已验证 |
| 窗口拖拽、最小化、最大化、关闭 | QML/窗口控制器存在；真实 Win32 窗口交互未作为自动化门禁 | 仅源码 |
| 迷你播放器、置顶 | `MiniPlayerWindow.qml`、窗口控制器与 QML 测试 | 已验证 |
| 迷你播放器毛玻璃/悬浮模糊 | 当前开关仅调整透明度，没有 DWM Acrylic/Blur | 未实现 |
| 开机自动启动 | Windows 注册表写入代码存在；测试模式明确不写真实注册表 | 仅源码 |
| 最小化托盘、托盘恢复与退出 | `app/main.cpp` 有系统托盘实现；现有测试使用假回调 | 仅源码 |
| 可编辑全局快捷键及后台响应 | Windows `RegisterHotKey` 实现存在；测试仅使用内存后端 | 仅源码 |
| 应用内 Ctrl+F、Tab、Alt+D | QML Shortcut/动作绑定存在，缺少完整按键副作用矩阵 | 仅源码 |
| Windows 文件关联与自定义图标 | `FileAssociationController` 存在；仍需真实注册表和双击启动验收 | 仅源码 |

## 波形与可视化

| 原始要求 | 当前证据与边界 | 状态 |
|---|---|---|
| C++ `QQuickItem` + QSGGeometryNode GPU 波形 | `WaveformItem::updatePaintNode()` 与波形测试 | 已验证 |
| 纯色、RGB 渐变波形 | 透明自适应画布；纯色默认白色/#ffdd00；RGB 默认青蓝→蓝紫→玫红；颜色、底色/进度反转与 UI 矩阵 | 已验证 |
| 动态频谱波形 | 当前第三模式是整文件预分析的 bass/mid/high 静态层，无实时 PCM tap/FFT | 未实现 |
| 离线三频段波形 | `WaveformAnalyzer` 两遍离线分析；细条随机 RGB、中心高两侧低；`WaveformItem` 复用三层数据 | 已验证 |
| 进度跟踪、Hover 时间、拖拽/点击 Seek | `WaveformItem`、`PlayerPane.qml` 与 QML/控制器测试 | 已验证 |
| 波形缩略预览胶囊 | 未找到与原始交互描述等价的独立缩略预览实现 | 未实现 |
| 高度、密度、线条粗细与聚合算法运行时生效 | 设置绑定、平均绝对值/RMS 独立缓存、QML/UI 矩阵与核心/Qt 测试 | 已验证 |

## 五大音频工具

| 原始要求 | 当前证据与边界 | 状态 |
|---|---|---|
| 文件拖入、处理进度可视化、预览与导出 | 工具控制器、QML 页面、端到端测试 | 已验证 |
| 批量格式转换、码率/采样率/声道、元数据、视频提取、安全覆盖 | `FormatConverter`、FFmpeg 转码与端到端测试 | 已验证 |
| 转码不影响当前播放 | 后台任务代码存在，但没有同时播放+批量转码的实时音频门禁 | 仅源码 |
| 六轨轻度剪辑、缩放/拖动、剪切/裁剪/分割/合并、复制粘贴、删除、撤销重做 | `LightEditor`、`multitrack_editor`、QML 与端到端测试 | 已验证 |
| 淡入淡出、静音、预览、BPM 修改/统一/节拍吸附与导出 | 轻度剪辑控制器、DSP 与 QML/端到端测试 | 已验证 |
| 调速：BPM 检测、目标 BPM、节拍对齐、标记点、保持音高、预览与导出 | `SpeedAdjuster`、`BpmAnalyzer` 与工具测试 | 已验证 |
| 升降调：半音/Cent、人声保护、平滑、保持时长、预览与导出 | `PitchShifter` 与端到端测试；算法不是指定的 SoundTouch | 已验证 |
| 信息修改：标签、封面、文件名前后缀 | `MetadataEditor`、`metadata_writer` 与单文件端到端测试 | 已验证 |
| 信息修改：选择部分/全部并一次处理 100+ 首、部分失败恢复 | 当前批量改名总是处理全部已加载项；无 100+ 文件与部分失败测试 | 未实现 |

## 设置、主题、本地化与隐私

| 原始要求 | 当前证据与边界 | 状态 |
|---|---|---|
| 单页滚动设置；保存、取消、恢复默认 | `SettingsController`、`SettingsPage.qml` 及测试 | 已验证 |
| 现有真实设置运行时生效 | 主题、语言、波形、工具、缓存、窗口行为已有绑定测试 | 已验证 |
| 原始设置清单全部可交互 | 输出设备、独占、自动采样率和自动切歌淡化已接通；手动切歌完整淡出仍缺失 | 未实现 |
| 深色、浅色、跟随系统 | `Theme.qml`、`SettingsController` 与深浅 UI 矩阵；系统切换需人工观察 | 已验证 |
| 完美匹配 Windows/macOS/Linux/HarmonyOS 默认主题与双色图标 | Windows 有主题基础；非 Windows 平台未构建和视觉验收 | 待人工 |
| 毛玻璃设置 | 当前仅透明度效果，不是系统毛玻璃 | 未实现 |
| 中文、英文、泰语、越南语 | 4 个 TS/QM 目录；各 463 条完整、未完成 0；翻译与 80 张 UI 矩阵测试 | 已验证 |
| 缓存路径、容量限制、分项清理、全部清理、退出清理 | `CacheJanitor`、设置/关机测试 | 已验证 |
| 默认导出目录与覆盖策略 | 工具统一使用 `defaultOutputDirectory`；旧 `defaultExportDirectory` 自动迁移后删除 | 已验证 |
| 纯本地、无上传、无广告、无遥测/统计 | 源码扫描无网络客户端、遥测、SMTP 或凭据；官网仅用户点击后交给系统浏览器 | 已验证 |

## 非 Windows MVP 与发布

| 原始要求 | 当前证据与边界 | 状态 |
|---|---|---|
| macOS 桌面构建、音频、文件关联、Apple Silicon/Intel 通用 DMG | 无 macOS 构建与制品证据 | 非Windows MVP |
| Linux 桌面构建、音频、文件关联、AppImage | 无 Linux 构建与制品证据 | 非Windows MVP |
| HarmonyOS ArkUI/NAPI 与音频/文件关联 | 无 HarmonyOS 工程或 NAPI 适配 | 非Windows MVP |
| Android、iOS、HarmonyOS 手机端扩展框架 | 未进入当前 Windows MVP | 非Windows MVP |
| Windows 独立 EXE 安装包 | 用户要求最后单独下令后才封装；当前未部署运行库 | 待人工 |

## 当前优先缺口

1. 为全部可见控件建立“操作→副作用”测试。
2. 为手动切歌补齐异步淡出切换状态机。
3. 实现实时 PCM tap + FFT 动态频谱，不再把离线三频段波形称为动态频谱。
4. 实现后台曲库加载、`fetchMore()` 分页和真实 QML 帧时间门禁。
5. 对全局快捷键、开机启动、托盘和文件关联执行真实 Windows 集成/人工验收。
6. 增加 100+ 文件、选择子集、冲突与部分失败的批量信息修改测试。
7. macOS、Linux、HarmonyOS 进入各自阶段后再声明跨平台完成。
