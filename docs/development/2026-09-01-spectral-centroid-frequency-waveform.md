# Spectral Centroid 频彩波形替换实施计划

> **已废弃（2026-09-03）：** 本文记录的 FFT / Hann / Spectral Centroid / 8 色板方案已被三频 Low / Mid / High 方案完整替换，不再是当前实现或验收依据。当前记录见 `docs/development/2026-09-03-three-band-frequency-color-waveform.md`。

> **执行要求：** 使用测试驱动方式逐项实施；所有行为修改先观察失败测试，再写最小实现。本计划在当前会话内执行，不自动提交 Git。

**目标：** 用“现有振幅包络 + FFT 512 + Hann + Spectral Centroid + 8 色连续映射”直接替换旧三频《频彩波形》，保持纯色、RGB、柱状频谱行为不变。

**架构：** `WaveformAnalyzer` 在明确请求频彩数据时，随现有第二遍离线解码可选生成每个振幅桶对应的 `uint8_t SpectralIndex`；`WaveformCache` v3 在同一个 `.agwf` 文件中保存振幅层与 SpectralIndex。Qt 数据提供层继续使用现有单线程低优先级线程池，渲染层复用 `WaveformItem` 的单套几何，根据 SpectralIndex 和当前 8 色 Palette 生成顶点颜色；Palette 与进度透明度只属于渲染状态。

**技术栈：** C++17、Qt 6/QML/Qt Quick SceneGraph、FFmpeg 解码、CMake/CTest。

**规格：** `C:/Users/Administrator/Desktop/音视频播放器/AgPlayer_频彩波形_SpectralCentroid算法替换_轻量高性能开发提示词.md`

## 全局约束

- 最终模式只有：纯色波形、频彩波形、RGB波形、柱状频谱；内部兼容 ID 可保持，UI 顺序必须固定。
- 不新增第二套 Analyzer、Cache、Renderer、PCM 解码或常驻线程。
- FFT 固定 512 点 Hann 窗；频率使用 80 Hz 到 `min(14000 Hz, sampleRate * 0.45)` 的对数映射。
- 静音桶不产生随机索引；SpectralIndex 使用 3 点轻量平滑并量化到 0..255。
- 8 色默认值依次为 `#123ECF/#00A7BA/#00A76F/#62BB39/#D8DC2F/#FFAD22/#FF611F/#E82718`。
- 播放进度只改变 Alpha，默认差异 12%，范围 0%..40%。
- 只删除旧频彩专属的三频分析、`.fcw1` 缓存、分层材质、设置与无调用辅助代码；保留 RGB 和柱状频谱使用的 bass/mid/high 公共数据。

---

### 任务 1：统一分析数据与 Spectral Centroid

**文件：**

- 修改：`core/src/waveform_analyzer.hpp`
- 修改：`core/src/waveform_analyzer.cpp`
- 修改：`core/include/agplayer/c_api.h`
- 修改：`core/src/c_api.cpp`
- 测试：`tests/core/waveform_analyzer_test.cpp`

**接口：**

- `WaveformAnalysisOptions::include_spectral_index` 控制可选分析。
- `WaveformAnalyzer::analyze(...)` 额外输出 `std::vector<std::uint8_t>& spectral_index`。
- C API 提供 `ag_waveform_analyze_with_spectral_index(...)`、`ag_waveform_spectral_index_count(...)` 与 `ag_waveform_spectral_index_at(...)`，仍由同一个 `WaveformAnalyzer` 实现。

- [ ] 先添加 100/300/1000/3000/8000 Hz 的真实 WAV 行为测试，断言频率升高时中位 SpectralIndex 单调上升；添加静音测试，断言索引稳定且为零。
- [ ] 用 MSVC 构建并运行测试，确认因接口/行为缺失而失败。
- [ ] 在 `WaveformBucketizer` 中加入可选流式 512 点 Hann/FFT 累积器，按现有振幅桶输出能量加权质心索引；完成后做 3 点平滑。
- [ ] 运行 `waveform_analyzer_test`，确认新测试和既有峰值/RGB 频段测试通过。

### 任务 2：把 SpectralIndex 合并进现有缓存 v3

**文件：**

- 修改：`core/src/waveform_cache.hpp`
- 修改：`core/src/waveform_cache.cpp`
- 测试：`tests/core/waveform_cache_test.cpp`
- 删除：`core/src/frequency_color_waveform_cache.hpp`
- 删除：`core/src/frequency_color_waveform_cache.cpp`
- 删除：`tests/core/frequency_color_waveform_cache_test.cpp`

**接口：**

- `WaveformCacheData::spectral_index` 保存一字节一个桶的颜色索引。
- `.agwf` v3 仍使用源文件身份作为缓存键；v2 读取兼容为无 SpectralIndex，v1 兼容行为保持。

- [ ] 添加 v3 往返、CRC/截断拒绝、v2 兼容无索引、每 1800 桶只增加 1800 字节有效负载的失败测试。
- [ ] 运行 `waveform_cache_test`，确认因 v3 缺失而失败。
- [ ] 扩展现有原子写入/读取格式到 v3，不把 Palette、主题或 Alpha 写入缓存键或载荷。
- [ ] 运行缓存测试，随后删除独立 `.fcw1` 缓存实现和目标定义。

### 任务 3：统一 Provider 数据路径并删除旧 Analyzer

**文件：**

- 修改：`qt/src/waveform_provider.hpp`
- 修改：`qt/src/waveform_provider.cpp`
- 修改：`core/CMakeLists.txt`
- 修改：`tests/CMakeLists.txt`
- 测试：`tests/qt/waveform_provider_test.cpp`
- 删除：`core/src/frequency_color_waveform_analyzer.hpp`
- 删除：`core/src/frequency_color_waveform_analyzer.cpp`
- 删除：`tests/core/frequency_color_waveform_analyzer_test.cpp`

**接口：**

- `layers["spectralIndex"]` 传递同缓存中的索引；沿用 `_frequencyReady` 表示当前频彩请求可直接渲染，以保持现有 QML 会话契约稳定。
- 纯色分析不含 FFT；主波形或列表缩略图缺少 SpectralIndex 时，都在现有 `currentAnalysisPool_` 中以相应优先级调用统一 Analyzer 的可选分析。

- [ ] 添加缓存命中不解码、纯色不启动频彩分析、Palette 改变不触碰 Provider、频彩首次生成后同缓存命中的失败测试。
- [ ] 运行 `waveform_provider_test` 观察预期失败。
- [ ] 移除 `FrequencyColorWaveformAnalyzer/Cache` 引用、`.fcw1` 路径及 low/mid/high 频彩拼装；统一保存 `.agwf` v3。
- [ ] 运行 Provider 与核心测试，确认模式切换不会产生并行分析和重复缓存。

### 任务 4：单几何频彩渲染、8 色设置与列表缩略图

**文件：**

- 修改：`qt/src/waveform_item.hpp`
- 修改：`qt/src/waveform_item.cpp`
- 修改：`qt/src/frequency_color_waveform_settings.hpp`
- 修改：`qt/src/frequency_color_waveform_settings.cpp`
- 修改：`qt/src/settings_controller.hpp`
- 修改：`qt/src/settings_controller.cpp`
- 修改：`app/qml/AgPlayer/components/SharedWaveformView.qml`
- 修改：`app/qml/AgPlayer/components/TrackWaveformThumbnail.qml`
- 修改：`app/qml/AgPlayer/SettingsPage.qml`
- 修改：主播放器中引用频彩旧属性的 QML 文件
- 测试：`tests/qt/waveform_item_test.cpp`
- 测试：`tests/qt/settings_controller_test.cpp`
- 测试：`tests/qt/track_waveform_thumbnail_provider_test.cpp`
- 测试：相关 QML 测试
- 删除：`qt/src/waveform_layer_material.hpp/.cpp`
- 删除：`qt/shaders/waveform_layer.vert/.frag`

**接口：**

- `WaveformItem::spectralPalette` 为 8 个颜色，`spectralUnplayedOpacity` 为 0.6..1.0（对应播放进度明暗差 40%..0%）。
- 频彩模式复用普通波形 `mix` 几何，顶点色由连续 Palette 插值决定；进度仅用 Alpha 差异。
- 列表缩略图读取相同 `mix + spectralIndex`，降采样使用振幅加权索引；缓存缺索引时只发出共享分析请求，由 `WaveformProvider` 低优先级生成，不在缩略图 Provider 内新增解码器。

- [ ] 添加几何点数/顶点位置与纯色一致、索引端点和中间连续插值、Palette 改变只更新材质颜色、Alpha 差异范围的失败测试。
- [ ] 添加 8 色默认/校验/恢复默认/旧三频设置不迁移测试，运行观察失败。
- [ ] 实现最小 SceneGraph 顶点色路径，删除四层旧材质、旧 Shader 与旧 QML 属性。
- [ ] 更新设置页为 8 个现有颜色选择器入口和 0..40% 进度差异；保持纯色/RGB/柱状频谱配置不变。
- [ ] 更新列表为纯色/频彩两种，删除列表 36 色映射及只被它使用的设置/预览代码。
- [ ] 运行 Qt/QML 聚焦测试。

### 任务 5：清理、构建、视觉与专项审查

**文件：**

- 修改：`core/CMakeLists.txt`
- 修改：`qt/CMakeLists.txt`
- 修改：`tests/CMakeLists.txt`
- 修改：本文件，记录验收证据和未验证项。

- [ ] 全项目检索确认没有 `FrequencyColorWaveformAnalyzer`、`FrequencyColorWaveformCache`、`.fcw1`、旧 low/mid/high 频彩属性、列表 36 色入口或未引用旧辅助代码。
- [ ] 用 Visual Studio 开发环境干净配置 Release，构建 AgPlayer 和所有聚焦测试。
- [ ] 运行核心缓存/分析、Provider、WaveformItem、Settings、列表缩略图和 QML 模式顺序测试。
- [ ] 运行应用并截图对比同一音频的纯色/频彩轮廓、频率颜色、播放进度 Alpha 和列表缩略图；环境无法完成的硬件/人工项目必须单列。
- [ ] 执行 `git diff --check`、最终 Diff Review 和 20 项专项审查，记录实际证据，不以局部测试代替完整验收。

## 实施结果（2026-09-01）

### 已完成

- 旧 `FrequencyColorWaveformAnalyzer/Cache`、`.fcw1` 写入路径、四层频彩材质、专用 Shader、三频主题预设和列表 36 色映射均已删除。
- `WaveformAnalyzer` 仅在主波形或列表缩略图明确缺少频彩索引时执行 512 点 Hann FFT 与 Spectral Centroid；纯色分析和播放线程不执行 FFT。
- `.agwf` v3 同时保存 mix/bass/mid/high 与 `uint8_t spectralIndex`；v2 仍可读取并在频彩请求时原位升级。
- `WaveformItem` 频彩模式复用普通振幅几何，按八色板连续插值顶点颜色，播放进度只改变 Alpha；改色不触发解码、FFT 或缓存重建。
- 列表缩略图只保留“频彩/纯色”，直接读取同一 `.agwf`，频彩降采样采用振幅加权索引；v2/缓存缺失时通过共享 Provider 低优先级补齐 v3。
- 设置页保留四种波形模式，新增八色板（两行四列）与 0%..40% 播放进度明暗差，默认 12%。

### 验证证据

- MSVC Release：`cmake --build build/msvc-release --target AgPlayer -j 4` 通过。
- 聚焦 CTest：分析、Spectral Centroid、缓存、Provider、WaveformItem、设置、列表 Provider/Item 共 8/8 通过。
- QML 专项：频彩主窗口 4 项、滚动播放器频彩项、迷你播放器全套 17 项、共享视觉频彩项均通过。
- 实际界面验收：真实 AgPlayer 进程中确认频彩主波形与列表缩略图可见；设置页模式顺序正确，八个默认色值完整可读，滑块显示 12%，两行四列在 860x900 设置窗口内无截断。首次检查发现单行八色值被截断，已整改后复查通过；验收中的设置变更已取消还原。

### 已知基线限制

- `qml_rolling_theme_test` 全套仍有 4 项与本次频彩替换无关的既有布局/BPM 失败；本次新增的频彩主题独立性用例单独通过。
- 未进行真实声卡连续播放、长时 CPU/内存采样或安装包验收；本次交付范围为源码、Release 构建、聚焦自动测试与实际界面视觉检查。
