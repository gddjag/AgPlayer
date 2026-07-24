# AgPlayer Phase 3-2 音频工具补完设计文档

> **目标**：补齐格式转换与轻量编辑工具中尚未实现的功能：音量标准化、人声保护（升降调工具）、从视频提取音频、多轨道非破坏性剪辑。
> **范围**：Core 层转码器/编辑器扩展、C API 参数扩展、Qt Bridge 控制器更新、QML UI 微调、单元测试。
> **平台**：以 Windows x64 为主实现平台，算法纯 C++17，跨平台可复用。

---

## 1. 设计约束

- 必须保持现有三层架构：Core（C++17 / C ABI）负责音频处理；Qt Bridge 负责状态管理与后台任务；QML 只负责展示与交互。
- 所有功能必须可离线工作，不访问网络，不调用外部 FFmpeg 可执行文件。
- 错误必须静默处理或返回给 UI，不能导致崩溃。
- 尽量不引入新的外部依赖；复用 FFmpeg 已有能力。
- 所有修改必须在 Windows x64 Debug/Release 下通过 `ctest`。
- 保持轻量：避免一次性将整个大文件载入内存，优先流式或双遍处理。

---

## 2. 现状梳理

| 工具 | 已有能力 | 缺失能力 |
|------|----------|----------|
| **格式转换** (`FormatConverter` / `ag_transcode`) | 批量音频转码、码率/采样率/声道设置 | 音量标准化仅 UI 占位；视频文件过滤导致无法提取音频 |
| **升降调** (`PitchShifter` / `ag_pitch_shift_ex`) | 音调/速度调整、输出格式选择 | `vocal_protection` 选项已暴露但 Core 未实现 |
| **轻量剪辑** (`LightEditor` / `ag_light_edit`) | 单轨道修剪、淡入淡出、增益、导出 | 多轨道 UI 仅第一轨道可加载，无法多轨混音导出 |

---

## 3. 音量标准化

### 3.1 策略

采用**峰值标准化（Peak Normalization）**：将输出音频的峰值电平提升到目标值 -1 dBFS，避免削波同时统一响度。

- 目标峰值：线性振幅 `0.8913`（即 -1 dBFS）。
- 实现方式：双遍处理。
  1. **分析遍**：解码音频为 float32 planar，跟踪最大绝对样本值 `peak`。
  2. **转码遍**：重新解码，应用增益 `gain = target_peak / peak`，再按原有转码流程编码输出。
- 仅当 `peak > 0` 且 `peak < target_peak` 时应用增益；`peak >= target_peak` 时不放大，仅做限制提示。

### 3.2 Core 改动

- 扩展 `TranscodeConfig`：新增 `bool volume_normalize = false`。
- 新增 `ag_transcode_options` 选项结构与 `ag_transcode_ex`，保持 `ag_transcode` 签名不变（其内部调用 `ag_transcode_ex` 并传默认选项）。
- 在 `transcoder.cpp` 中实现分析遍与增益应用。

### 3.3 Qt Bridge 改动

- `FormatConverter::start()` 移除"Volume normalization is not supported yet"警告，将 `volumeNormalize` 传入 `runTranscode()`。
- `FormatConverter::runTranscode()` 调用 `ag_transcode` 时传入 `volumeNormalize`。

### 3.4 UI 改动

- 无结构性调整，仅移除运行时警告。

---

## 4. 人声保护

### 4.1 策略

人声保护集成在**升降调工具**中，对应 `ag_pitch_shift_options.vocal_protection` 标志。当前 Core 完全忽略该标志。本阶段实现一种轻量的**共振峰补偿**：

- 当升调（`pitch_ratio > 1.0`）时，人声容易变"尖"， because 共振峰随基频上移。补偿方法：对 OLA 拉伸后的信号应用低通滤波，截止频率随 `pitch_ratio` 提升而降低（例如 `8000 / pitch_ratio Hz`），抑制过度明亮的人声泛音。
- 当降调（`pitch_ratio < 1.0`）时，人声容易变"闷"，补偿方法：应用一阶高通/高 shelf 提升高频能量， cutoff 随 `pitch_ratio` 降低而升高（例如 `80 * pitch_ratio Hz`）。
- 该策略为实验性，保持 `vocal_protection` 注释中的 "experimental" 语义。

### 4.2 Core 改动

- 在 `pitch_shifter.cpp` 中，OLA 拉伸后、编码前，根据 `config.vocal_protection` 和 `pitch_ratio` 对每个通道应用一阶 IIR 滤波。

### 4.3 Qt Bridge / UI 改动

- 无需改动；`PitchShifter` 已将 `vocalProtection` 传入 `ag_pitch_shift_ex`。

---

## 5. 从视频提取音频

### 5.1 策略

`ag_transcode` 内部已使用 `av_find_best_stream(..., AVMEDIA_TYPE_AUDIO, ...)`，天然只处理音频流、忽略视频流。因此"从视频提取音频"的核心功能已经具备，只需解决 UI/控制器层面的限制：

1. **文件选择过滤**：格式转换页面默认只显示音频文件；需要允许选择常见视频文件（MP4、MKV、AVI、MOV、WebM 等）。
2. **元数据读取**：`ag_metadata_open` 基于 `Decoder`，会读取音频流元数据；若视频文件无音频流，则显示格式为空并自然在转码时报错。
3. **移除警告**：`FormatConverter::start()` 中删除 "Extracting audio from video is not supported yet" 警告。

### 5.2 Qt Bridge 改动

- `FormatConverter::loadFiles()` 对视频文件同样尝试 `ag_metadata_open`，失败后回退到文件后缀。
- `FormatConvertPage.qml` 文件对话框增加视频过滤器或改为"音频/视频文件"联合过滤。
- 拖放区域文案在无视频提取模式时保持"audio files"，勾选 `extractAudio` 后提示包含 video。

---

## 6. 多轨道非破坏性剪辑

### 6.1 策略

"非破坏性"指源文件永不修改，所有编辑操作（加载、修剪、增益、淡入淡出、静音等）只在导出时应用到输出文件。

实现目标：
- 支持 6 条轨道，每条轨道可独立加载/清除音频文件。
- 选中的轨道为"活动轨道"，当前的全局修剪/淡入淡出/增益控件对其生效（保持现有 UI 最小改动）。
- 导出时混合所有已加载轨道。
- 混合规则：
  - 以第一条加载轨道的采样率为输出采样率。
  - 其他轨道重采样到该采样率。
  - 各轨道样本相加后做硬削波（clamp 到 [-1, 1]），防止混音溢出。
  - 所有轨道从 0 时刻对齐，输出时长取最长轨道时长。

### 6.2 Core 改动

新增 `ag_multitrack_edit` C API：

```c
/* Multi-track non-destructive edit: mix multiple audio files with optional
 * per-track trim/fade/gain, and render to output_path.
 * track_count: number of tracks.
 * input_paths: array of UTF-8 input paths. NULL or empty string = silent track.
 * trim_start_ms / trim_end_ms / fade_in_ms / fade_out_ms / gain: per-track
 *   arrays. Values use the same semantics as ag_light_edit. Pass NULL to use
 *   defaults (no trim, no fade, gain=1.0).
 * Returns AG_OK on success, AG_CANCELLED if cancelled. */
ag_result ag_multitrack_edit(size_t track_count,
                             const char* const* input_paths,
                             const long long* trim_start_ms,
                             const long long* trim_end_ms,
                             const int* fade_in_ms,
                             const int* fade_out_ms,
                             const double* gain,
                             const char* output_path,
                             const ag_cancel_token* cancel_token,
                             ag_progress_callback progress_callback,
                             void* user_data);
```

内部实现：
- 逐轨道解码为 float32 planar，应用该轨道参数（trim/fade/gain）。
- 混音到统一采样率的 master buffer。
- 编码输出，输出编码器与容器由 `output_path` 后缀决定（与 `ag_transcode` 一致）。

### 6.3 Qt Bridge 改动

- 扩展 `LightEditor`：
  - 新增 `trackCount`、`trackNames`、`trackHasFiles`、`trackPeaks`、`selectedTrack` 等属性。
  - `loadFile(const QUrl& url)` 改为加载到当前 `selectedTrack`。
  - 新增 `loadFileToTrack(int trackIndex, const QUrl& url)`、`clearTrack(int trackIndex)`。
  - `start()` 改为收集所有轨道参数并调用 `ag_multitrack_edit`。
- 保留向后兼容：当只有一条轨道加载时，行为与现有 `ag_light_edit` 一致。

### 6.4 QML 改动

- `LightEditPage.qml`：
  - 轨道点击设置 `selectedTrack`。
  - "Add File" 按钮加载到 `selectedTrack`。
  - "Clear" 按钮清除 `selectedTrack`。
  - 多轨道工具栏中：Cut/Copy/Paste/Delete/Split/Merge 继续禁用并提示不支持；Fade In/Fade Out/Mute/Crop 仍作用于活动轨道。
  - 导出设置中的格式/采样率/声道从"preview-only"变为实际生效。

---

## 7. 文件结构

### 7.1 新增文件

| 文件 | 职责 |
|------|------|
| `core/src/multitrack_editor.hpp` | 多轨道编辑内部接口 |
| `core/src/multitrack_editor.cpp` | 多轨道解码、处理、混音、编码实现 |
| `tests/core/multitrack_editor_test.cpp` | 多轨道编辑单元测试 |

### 7.2 修改文件

| 文件 | 修改内容 |
|------|----------|
| `core/include/agplayer/c_api.h` | 新增 `ag_transcode_options` 与 `ag_transcode_ex`；新增 `ag_multitrack_edit` |
| `core/src/transcoder.hpp` | `TranscodeConfig` 新增 `volume_normalize` |
| `core/src/transcoder.cpp` | 实现音量标准化双遍处理 |
| `core/src/c_api.cpp` | 包装 `volume_normalize` 与 `ag_multitrack_edit` |
| `core/src/pitch_shifter.cpp` | 实现 `vocal_protection` 共振峰补偿 |
| `core/CMakeLists.txt` | 添加 `multitrack_editor.cpp` |
| `qt/src/format_converter.hpp/.cpp` | 透传 `volumeNormalize`；移除视频提取警告；支持视频文件元数据读取 |
| `qt/src/light_editor_controller.hpp/.cpp` | 扩展为多轨道模型 |
| `qt/src/qml_registration.cpp` | 注册新增/修改的 QML 属性与方法 |
| `app/qml/AgPlayer/components/tools/FormatConvertPage.qml` | 文件过滤支持视频；文案动态更新 |
| `app/qml/AgPlayer/components/tools/LightEditPage.qml` | 轨道加载/清除按选中轨道；导出设置生效 |
| `tests/core/transcoder_test.cpp` | 新增音量标准化测试 |
| `tests/core/pitch_shifter_test.cpp` | 新增人声保护开关对比测试 |
| `tests/core/light_editor_test.cpp` | 调整签名兼容；新增多轨道测试 |
| `tests/CMakeLists.txt` | 注册 `multitrack_editor_test` |

---

## 8. 测试策略

### 8.1 Core 单元测试

- **transcoder_test**：
  - 对 sine fixture 启用 `volume_normalize`，验证输出文件峰值接近 -1 dBFS。
  - 对已有峰值高于 -1 dBFS 的 fixture，验证不放大（不削波）。
- **pitch_shifter_test**：
  - 升调 + vocal_protection 后高频能量低于未保护版本。
  - 降调 + vocal_protection 后低频能量不过度堆积。
- **multitrack_editor_test**：
  - 两轨相同 sine 混音，输出峰值约为单轨两倍（验证混合逻辑）。
  - 空轨道与有效轨道混音结果等同于单轨。
  - 输入/输出路径相同返回 `AG_INVALID_ARGUMENT`。

### 8.2 Qt Bridge / QML 测试

- 扩展 `light_editor_controller` 相关 QML 测试（如存在）验证多轨道属性变更。
- 手动验证 FormatConvertPage 可选择视频文件并导出音频。

---

## 9. 风险与回退

- **音量标准化双遍解码性能**：大文件处理时间翻倍。若用户反馈过慢，后续可改为 RMS/EBU R128 单遍近似算法。
- **人声保护效果有限**：当前为轻量 EQ 补偿，无法替代专业音高算法。标记为 experimental 符合预期。
- **多轨道内存占用**：当前实现将各轨道解码到内存。若轨道多且文件长，可能占用较大内存。后续可改为分块流式混音。
- **视频元数据读取失败**：某些容器可能无法被 `ag_metadata_open` 识别；此时回退到后缀显示，转码阶段仍可能成功。

---

## 10. 不在本阶段范围

- 复杂的 EBU R128 / ReplayGain 响度标准化。
- 专业级共振峰保持算法（如 LPC 包络分离）。
- 视频转码、视频画面处理。
- 多轨道时间轴拖拽、对齐、变速、效果器链。
- 非破坏性工程文件持久化（如保存剪辑项目）。

---

## 11. 验收标准

- Windows x64 Debug/Release 全量构建通过。
- `ctest` 全部通过（含新增测试）。
- 格式转换启用音量标准化后输出峰值接近 -1 dBFS。
- 格式转换可从常见视频文件（MP4/MKV/AVI）提取音频。
- 升降调启用人声保护后与关闭时存在可度量的频谱差异。
- 轻量剪辑可加载多个轨道并导出混音文件。
