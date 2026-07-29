# AgPlayer 阶段验收状态

日期：2026-07-29
分支：`codex/revised-ui`
当前阶段：Phase 10 实时动态频谱自动化软件门禁完成

## 当前结论

Phase 8 已完成一组可重复的 Windows 自动化门禁。Phase 9 的播放设备链路及 Phase 10 的实时 PCM/FFT 频谱已通过软件测试，但真实声卡、热插拔和听感仍需人工验收。**不能据此声明原始需求或 Windows MVP 全部完成**；异步分页/60FPS、手动切歌完整淡化和部分系统集成仍有缺口。

## 已验证的软件子集

- 本地解码、播放状态、暂停、Seek、音量、静音和无间隙队列。
- MP3/WAV/FLAC/AAC/M4A/OGG/Opus/WMA 导入、持久化与恢复。
- 顺序、随机、单曲循环、列表循环。
- 曲库、歌单、收藏、评分、历史、排序和组合筛选。
- 空白启动页、播放页、主/列表/迷你窗口及磁吸控制逻辑。
- QSG GPU 纯色/RGB/离线三频段波形、透明自适应画布、Hover 与 Seek。
- 波形平均绝对值/RMS 聚合、独立缓存、实时设置预览、颜色/高度/密度/粗细参数。
- 可听 PCM 无锁采样、512 点 radix-2 FFT、64 个实时频谱 bin、约 29 FPS Qt/QML 刷新。
- 格式转换、六轨轻度剪辑、调速/BPM、升降调和基础信息修改。
- 中文、英文、泰语、越南语以及深浅主题 UI 矩阵。
- 设置持久化、缓存管理、C++ 核心/C API 边界和本地隐私约束。

## 仅有源码、尚未完成真实系统验证

- Windows 全局快捷键：生产后端使用 `RegisterHotKey`，测试只覆盖内存后端。
- 开机启动：存在注册表写入代码，测试模式不修改真实注册表。
- 系统托盘：存在托盘恢复/退出代码，测试只调用假回调。
- 文件关联：存在 Windows 控制器，尚未完成真实注册表与双击启动门禁。
- 窗口原生最小化/最大化/关闭和应用内快捷键的完整副作用矩阵。
- 后台转码与同时播放的持续音频稳定性。

## 未实现或当前表述不真实

- 指定的 kissfft 依赖未引入；当前为轻量固定 512 点 radix-2 FFT。
- 曲库没有 `canFetchMore()`/`fetchMore()`、后台持久化加载和真实 ListView 60FPS 门禁。
- 输出设备按稳定 ID 枚举/切换、独占回退和设备丢失恢复已完成软件链路，仍需真实多声卡/热插拔兼容性门禁。
- 自动队列边界已有 0/200/500 ms 淡出/淡入，手动切歌仅完成淡入，仍需异步淡出切换。
- 自动逐曲采样率开关已贯通 C API、Qt、设置持久化和 Null/Manual 后端；不同采样率曲目会在边界排空缓冲并重建设备，关闭时保留固定会话无缝策略；真实 WASAPI 多声卡仍待人工。
- SoundTouch 指定依赖未引入。
- 迷你播放器毛玻璃仅调整透明度，不是 DWM Acrylic/Blur。
- “所有按钮和图标均有真实功能”尚不成立，现有 UI 测试没有验证全量点击副作用。
- 批量信息修改缺少选择子集、100+ 文件、冲突和部分失败恢复。
- macOS、Linux、HarmonyOS 构建、音频后端、文件关联和分发。

## Phase 8 已有自动化记录

以下证据只证明对应子集，不外推到缺失需求：

- Debug `/W4 /WX` 历史门禁通过；CTest 44/44。
- Release `/W4 /WX` 当前构建通过；CTest 44/44，10.07 秒。
- Release 真实 WAV 打开、播放状态推进、截图和有序退出；日志无警告。
- 8 格式曲库导入、持久化、重启恢复 8/8。
- 四语言 × 深浅主题 × 10 页面 UI 截图矩阵 80/80；缺图 0，运行时告警 0；四个 TS 文件各 463 条、未完成 0。
- 10,000 行内存曲库搜索、排序和随机访问压力测试通过；这不是分页或 60FPS 测试。
- 10,000 波形缓存压力：10,000/10,000、0 失败、306.307 秒、缓存 8.47 MiB、该工具峰值工作集 16.04 MiB。
- Seek 基准：100/100、0 失败、中位 0.0142 ms、P95 0.0222 ms。
- 隐私扫描未发现网络客户端、遥测、统计、SMTP、私有邮箱或凭据。
- 未部署的 Release `AgPlayer.exe` 为 3.38 MiB；不能代表最终安装包大小。

## 下一阶段顺序

1. **Phase 8.2：Windows 原生门禁**
   全局快捷键、开机启动、托盘、文件关联和可见控件副作用测试。
2. **Phase 9：播放设备与采样率（软件门禁完成）**
   保留真实多声卡、热插拔和听感人工验收。
3. **Phase 10：实时动态频谱（软件门禁完成）**
   可听 PCM tap、FFT、刷新节流和 500 次频谱快照预算测试已完成。
4. **Phase 11：大曲库加载与渲染性能（下一阶段）**
   后台持久化加载、分批模型提交、`fetchMore()` 与真实 QML 60FPS 门禁。
5. **Phase 12：跨平台**
   macOS/Linux CI 与分发、HarmonyOS NAPI/ArkUI 边界。

## 发布前门禁

- 真实声卡/扬声器可听播放、Seek、切歌、音量、静音和工具处理听感。
- Windows 多声卡、蓝牙断连/重连、设备热插拔及系统功能验收。
- 完整 GUI 播放时的内存测量。
- macOS、Linux、HarmonyOS 平台验证。
- EXE/安装器、DMG、AppImage 打包；仅在用户单独下达打包指令后执行。

## Phase 8 证据复现

```powershell
cmake --build build/msvc-debug --config Debug --parallel
ctest --test-dir build/msvc-debug -C Debug --parallel 8 --output-on-failure
cmake --build build/msvc-release --config Release --parallel
ctest --test-dir build/msvc-release -C Release --parallel 8 --output-on-failure
scripts\qa-main-smoke.ps1 -BuildDirectory build/msvc-release
scripts\qa-library-smoke.ps1 -BuildDirectory build/msvc-release
scripts\qa-final-ui-matrix.ps1 -BuildDirectory build/msvc-release
```
