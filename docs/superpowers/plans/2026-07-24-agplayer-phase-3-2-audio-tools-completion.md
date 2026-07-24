# AgPlayer Phase 3-2 音频工具补完实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 补齐格式转换与轻量编辑工具中尚未实现的功能：音量标准化、人声保护（升降调）、从视频提取音频、多轨道非破坏性剪辑。

**Architecture:** Core 层扩展 `ag_transcode_ex`（带音量标准化选项）与新增 `ag_multitrack_edit`；`ag_pitch_shift_ex` 的 `vocal_protection` 标志在 Core 中实现轻量共振峰补偿。Qt Bridge 的 `FormatConverter`、`PitchShifter`、`LightEditor` 相应透传新能力。QML 仅做最小适配。

**Tech Stack:** C++17, Qt 6.7, FFmpeg, CMake, ctest。

---

## Global Constraints

- 必须保持现有三层架构：Core（C++17 / C ABI）负责音频处理；Qt Bridge 负责状态管理与后台任务；QML 只负责展示与交互。
- 所有功能必须可离线工作，不访问网络，不调用外部 FFmpeg 可执行文件。
- 错误必须静默处理或返回给 UI，不能导致崩溃。
- 尽量不引入新的外部依赖；复用 FFmpeg 已有能力。
- 所有修改必须在 Windows x64 Debug/Release 下通过 `ctest`。
- 保持轻量：避免一次性将整个大文件载入内存，优先流式或双遍处理。

---

## Task 1: 扩展 C API（音量标准化与多轨道编辑入口）

**Files:**
- Modify: `core/include/agplayer/c_api.h`

**Interfaces:**
- Produces: `ag_transcode_options`, `ag_transcode_ex()`, `ag_multitrack_edit()`

- [ ] **Step 1: 在 `ag_pitch_shift_options` 之后新增转码选项结构与扩展函数**

```c
/* Extended options for ag_transcode_ex. Set unused fields to 0/NULL. */
typedef struct ag_transcode_options {
    int volume_normalize;    /* 1 = normalize peak to -1 dBFS before encoding */
} ag_transcode_options;

/* Transcode with extended options. Behaves like ag_transcode when options is
 * NULL or all fields are zero. */
ag_result ag_transcode_ex(const char* input_path,
                          const char* output_path,
                          const char* codec_name,
                          long long bit_rate,
                          int sample_rate,
                          int channels,
                          const ag_transcode_options* options,
                          const ag_cancel_token* cancel_token,
                          ag_progress_callback progress_callback,
                          void* user_data);
```

- [ ] **Step 2: 在文件末尾新增多轨道编辑函数声明**

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

- [ ] **Step 3: 构建 Core 检查头文件编译**

Run:
```powershell
Import-Module "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -InstallPath "C:\Program Files\Microsoft Visual Studio\2022\Community" -SkipAutomaticLocation -Arch amd64
cmake --build build/debug --config Debug --target agplayer_core
```

Expected: `agplayer_core` 编译成功，无新增警告。

- [ ] **Step 4: 提交**

```bash
git add core/include/agplayer/c_api.h
git commit -m "feat(core/api): declare ag_transcode_ex, ag_transcode_options and ag_multitrack_edit"
```

---

## Task 2: Core 转码器实现音量标准化

**Files:**
- Modify: `core/src/transcoder.hpp`
- Modify: `core/src/transcoder.cpp`

**Interfaces:**
- Consumes: `TranscodeConfig::volume_normalize`
- Produces: 双遍处理后的标准化音频输出

- [ ] **Step 1: 扩展 `TranscodeConfig`**

在 `core/src/transcoder.hpp` 中：

```cpp
struct TranscodeConfig {
    std::string output_path;
    std::string codec_name;
    long long bit_rate = 0;
    int sample_rate = 0;
    int channels = 0;
    bool volume_normalize = false;
};
```

- [ ] **Step 2: 实现峰值扫描辅助函数**

在 `core/src/transcoder.cpp` 匿名命名空间新增：

```cpp
// Decode the entire audio stream to float32 planar and return the maximum
// absolute sample value. Returns <= 0.0 on error or silence.
double scan_peak(const std::string& input_path,
                 const std::atomic_bool* cancelled,
                 std::string& error)
{
    DecoderState dec;
    ag_result r = open_decoder(input_path, dec, error);
    if (r != AG_OK) return -1.0;

    SwrContext* swr = swr_alloc();
    if (swr == nullptr) {
        error = "Failed to allocate SwrContext";
        return -1.0;
    }

    AVChannelLayout in_ch_layout{};
    av_channel_layout_copy(&in_ch_layout, &dec.ctx->ch_layout);
    AVChannelLayout out_ch_layout{};
    av_channel_layout_copy(&out_ch_layout, &dec.ctx->ch_layout);

    av_opt_set_chlayout(swr, "in_chlayout", &in_ch_layout, 0);
    av_opt_set_int(swr, "in_sample_rate", dec.ctx->sample_rate, 0);
    av_opt_set_sample_fmt(swr, "in_sample_fmt", dec.ctx->sample_fmt, 0);
    av_opt_set_chlayout(swr, "out_chlayout", &out_ch_layout, 0);
    av_opt_set_int(swr, "out_sample_rate", dec.ctx->sample_rate, 0);
    av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_FLTP, 0);

    av_channel_layout_uninit(&in_ch_layout);
    av_channel_layout_uninit(&out_ch_layout);

    if (swr_init(swr) < 0) {
        swr_free(&swr);
        error = "Failed to initialize SwrContext";
        return -1.0;
    }

    double peak = 0.0;
    AVPacket* in_pkt = av_packet_alloc();
    AVFrame* in_frame = av_frame_alloc();
    AVFrame* resampled = av_frame_alloc();

    while (!is_cancelled(cancelled)) {
        const int read_ret = av_read_frame(dec.fmt_ctx, in_pkt);
        if (read_ret == AVERROR_EOF) break;
        if (read_ret < 0) {
            error = "Failed to read input packet";
            peak = -1.0;
            break;
        }
        if (in_pkt->stream_index != dec.stream_index) {
            av_packet_unref(in_pkt);
            continue;
        }

        if (avcodec_send_packet(dec.ctx, in_pkt) < 0) {
            error = "Failed to send packet to decoder";
            peak = -1.0;
            av_packet_unref(in_pkt);
            break;
        }
        av_packet_unref(in_pkt);

        while (true) {
            const int recv_ret = avcodec_receive_frame(dec.ctx, in_frame);
            if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF) break;
            if (recv_ret < 0) {
                error = "Failed to decode frame";
                peak = -1.0;
                break;
            }

            av_frame_unref(resampled);
            resampled->format = AV_SAMPLE_FMT_FLTP;
            resampled->sample_rate = dec.ctx->sample_rate;
            av_channel_layout_copy(&resampled->ch_layout, &dec.ctx->ch_layout);
            resampled->nb_samples = in_frame->nb_samples * 2;
            if (av_frame_get_buffer(resampled, 0) < 0) {
                error = "Failed to allocate resampled frame";
                peak = -1.0;
                av_frame_unref(in_frame);
                break;
            }

            const int out_samples = swr_convert(swr,
                resampled->data, resampled->nb_samples,
                (const uint8_t**)in_frame->data, in_frame->nb_samples);
            if (out_samples < 0) {
                error = "swr_convert failed";
                peak = -1.0;
                av_frame_unref(in_frame);
                break;
            }

            const int channels = dec.ctx->ch_layout.nb_channels;
            for (int ch = 0; ch < channels; ++ch) {
                const float* src = reinterpret_cast<float*>(resampled->data[ch]);
                for (int i = 0; i < out_samples; ++i) {
                    const double abs_sample = std::abs(static_cast<double>(src[i]));
                    if (abs_sample > peak) peak = abs_sample;
                }
            }
            av_frame_unref(in_frame);
        }
        if (peak < 0.0) break;
    }

    av_frame_free(&resampled);
    av_packet_free(&in_pkt);
    av_frame_free(&in_frame);
    swr_free(&swr);

    if (is_cancelled(cancelled)) {
        error = "Transcode cancelled";
        return -1.0;
    }
    return peak;
}
```

- [ ] **Step 3: 重构 `transcode()` 支持增益**

将现有单遍转码逻辑提取为内部函数 `run_transcode_pass(input_path, config, cancelled, progress_callback, error, gain)`，其中 `gain` 默认 1.0。然后在 `transcode()` 入口：

```cpp
if (config.volume_normalize) {
    const double peak = scan_peak(input_path, cancelled, error);
    if (peak < 0.0) {
        return AG_CANCELLED;
    }
    if (peak > 0.0) {
        constexpr double target_peak = 0.8913; // -1 dBFS
        gain = target_peak / peak;
        if (gain > 1.0) gain = 1.0; // do not amplify if already at/above target
    }
}
```

在编码帧转换阶段应用增益：先将输入转为 FLTP，乘以 `gain`，再转输出格式。

- [ ] **Step 4: 构建并运行转码器测试**

Run:
```powershell
cmake --build build/debug --config Debug --target transcoder_test
ctest -C Debug -R "^transcoder_test$" --output-on-failure
```

Expected: 现有测试通过。

- [ ] **Step 5: 提交**

```bash
git add core/src/transcoder.hpp core/src/transcoder.cpp
git commit -m "feat(core/transcoder): implement peak volume normalization"
```

---

## Task 3: C API 包装 ag_transcode_ex 与 ag_multitrack_edit

**Files:**
- Modify: `core/src/c_api.cpp`

**Interfaces:**
- Consumes: `agplayer::transcode()`, `agplayer::TranscodeConfig`
- Produces: `ag_transcode_ex()`, `ag_multitrack_edit()`

- [ ] **Step 1: 修改 `ag_transcode()` 调用新的 `ag_transcode_ex()`**

将 `ag_transcode()` 实现改为：

```cpp
ag_result ag_transcode(const char* input_path,
                       const char* output_path,
                       const char* codec_name,
                       const long long bit_rate,
                       const int sample_rate,
                       const int channels,
                       const ag_cancel_token* cancel_token,
                       const ag_progress_callback progress_callback,
                       void* const user_data)
{
    return ag_transcode_ex(input_path, output_path, codec_name, bit_rate,
                           sample_rate, channels, nullptr, cancel_token,
                           progress_callback, user_data);
}
```

- [ ] **Step 2: 新增 `ag_transcode_ex()` 实现**

```cpp
ag_result ag_transcode_ex(const char* input_path,
                          const char* output_path,
                          const char* codec_name,
                          const long long bit_rate,
                          const int sample_rate,
                          const int channels,
                          const ag_transcode_options* options,
                          const ag_cancel_token* cancel_token,
                          const ag_progress_callback progress_callback,
                          void* const user_data)
{
    if (input_path == nullptr || input_path[0] == '\0'
        || output_path == nullptr || output_path[0] == '\0') {
        return AG_INVALID_ARGUMENT;
    }

    try {
        agplayer::TranscodeConfig config;
        config.output_path = output_path;
        if (codec_name != nullptr) config.codec_name = codec_name;
        config.bit_rate = bit_rate;
        config.sample_rate = sample_rate;
        config.channels = channels;
        if (options != nullptr) {
            config.volume_normalize = options->volume_normalize != 0;
        }

        const std::atomic_bool* cancelled =
            cancel_token == nullptr ? nullptr : &cancel_token->cancelled;

        std::function<void(float)> cb;
        if (progress_callback != nullptr) {
            cb = [progress_callback, user_data](float frac) {
                progress_callback(frac, user_data);
            };
        }

        std::string error;
        return agplayer::transcode(input_path, config, cancelled,
                                   std::move(cb), error);
    } catch (...) {
        return AG_INTERNAL_ERROR;
    }
}
```

- [ ] **Step 3: 新增 `ag_multitrack_edit()` 占位/转发**

本步骤先添加 C API 包装，内部先返回 `AG_UNSUPPORTED_FORMAT` 或调用后续实现的 `agplayer::multitrack_edit`。若 Core 多轨道编辑器尚未实现，可暂时返回 `AG_UNSUPPORTED_FORMAT`，在 Task 6 中替换为真实调用。

```cpp
ag_result ag_multitrack_edit(const size_t track_count,
                             const char* const* input_paths,
                             const long long* trim_start_ms,
                             const long long* trim_end_ms,
                             const int* fade_in_ms,
                             const int* fade_out_ms,
                             const double* gain,
                             const char* output_path,
                             const ag_cancel_token* cancel_token,
                             const ag_progress_callback progress_callback,
                             void* const user_data)
{
    if (track_count == 0 || output_path == nullptr || output_path[0] == '\0') {
        return AG_INVALID_ARGUMENT;
    }
    // TODO: delegate to agplayer::multitrack_edit in Task 6.
    return AG_UNSUPPORTED_FORMAT;
}
```

- [ ] **Step 4: 构建并运行生命周期测试**

Run:
```powershell
cmake --build build/debug --config Debug --target c_api_lifecycle_test
cmake --build build/debug --config Debug --target transcoder_test
ctest -C Debug -R "^(c_api_lifecycle_test|transcoder_test)$" --output-on-failure
```

Expected: 测试通过。

- [ ] **Step 5: 提交**

```bash
git add core/src/c_api.cpp
git commit -m "feat(core/api): implement ag_transcode_ex and stub ag_multitrack_edit"
```

---

## Task 4: Qt Bridge 格式转换透传音量标准化与视频提取

**Files:**
- Modify: `qt/src/format_converter.hpp`
- Modify: `qt/src/format_converter.cpp`
- Modify: `app/qml/AgPlayer/components/tools/FormatConvertPage.qml`

**Interfaces:**
- Consumes: `ag_transcode_ex()`
- Produces: `volumeNormalize` 生效；视频文件可导入

- [ ] **Step 1: 修改 `runTranscode()` 签名并调用 `ag_transcode_ex()`**

在 `qt/src/format_converter.cpp` 中：

```cpp
void FormatConverter::runTranscode(const QString& outputFormat,
                                   int bitRate,
                                   int sampleRate,
                                   int channels,
                                   const QString& outputDir,
                                   bool /*keepMetadata*/,
                                   bool volumeNormalize)
```

调用处改为：

```cpp
ag_transcode_options options{};
options.volume_normalize = volumeNormalize ? 1 : 0;

const ag_result result = ag_transcode_ex(
    inputUtf8.constData(),
    outputUtf8.constData(),
    codecName.isEmpty() ? nullptr : codecName.constData(),
    static_cast<long long>(bitRate),
    sampleRate,
    channels,
    &options,
    token,
    nullptr,
    nullptr);
```

- [ ] **Step 2: 修改 `start()` 移除警告并透传参数**

移除 `volumeNormalize` 与 `extractAudio` 的未实现警告（`keepMetadata` 警告可保留或移除，视设计而定）。将 `volumeNormalize` 传入 `runTranscode()`。

- [ ] **Step 3: 支持视频文件元数据读取**

`loadFiles()` 中对任意文件调用 `ag_metadata_open` 读取元数据；失败时回退到文件后缀名。无需特殊处理。

- [ ] **Step 4: QML 文件过滤支持视频**

在 `FormatConvertPage.qml` 中，文件对话框过滤器改为：

```qml
nameFilters: [qsTr("Audio/Video files (*.wav *.mp3 *.flac *.aac *.m4a *.ogg *.opus *.wma *.mp4 *.mkv *.avi *.mov *.webm)")]
```

拖放区域提示文本可根据 `extractAudioCheck.checked` 动态切换。

- [ ] **Step 5: 构建并运行相关测试**

Run:
```powershell
cmake --build build/debug --config Debug --target agplayer_qt
ctest -C Debug -R "^(library_model_test|import_controller_test)$" --output-on-failure
```

Expected: 通过。

- [ ] **Step 6: 提交**

```bash
git add qt/src/format_converter.hpp qt/src/format_converter.cpp app/qml/AgPlayer/components/tools/FormatConvertPage.qml
git commit -m "feat(qt/format): enable volume normalize and video audio extraction"
```

---

## Task 5: Core 升降调实现人声保护

**Files:**
- Modify: `core/src/pitch_shifter.cpp`

**Interfaces:**
- Consumes: `PitchShiftConfig::vocal_protection`, `pitch_ratio`
- Produces: 经共振峰补偿处理的音频

- [ ] **Step 1: 实现一阶 IIR 滤波器**

在匿名命名空间新增：

```cpp
// Simple first-order low-pass or high-pass filter for vocal formant compensation.
class FirstOrderFilter {
public:
    enum class Type { LowPass, HighPass };

    FirstOrderFilter(Type type, double cutoff_hz, double sample_rate) noexcept
    {
        const double omega = 2.0 * M_PI * cutoff_hz / sample_rate;
        const double cos_omega = std::cos(omega);
        const double sin_omega = std::sin(omega);
        const double alpha = sin_omega / (2.0 * 0.707); // Q = 0.707 for gentle slope

        if (type == Type::LowPass) {
            b0_ = (1.0 - cos_omega) / 2.0;
            b1_ = 1.0 - cos_omega;
            b2_ = (1.0 - cos_omega) / 2.0;
        } else {
            b0_ = (1.0 + cos_omega) / 2.0;
            b1_ = -(1.0 + cos_omega);
            b2_ = (1.0 + cos_omega) / 2.0;
        }
        a0_ = 1.0 + alpha;
        a1_ = -2.0 * cos_omega;
        a2_ = 1.0 - alpha;

        b0_ /= a0_; b1_ /= a0_; b2_ /= a0_;
        a1_ /= a0_; a2_ /= a0_; a0_ = 1.0;
    }

    float process(float input) noexcept
    {
        const double output = b0_ * input + b1_ * x1_ + b2_ * x2_
                              - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_;
        x1_ = input;
        y2_ = y1_;
        y1_ = output;
        return static_cast<float>(output);
    }

private:
    double b0_ = 0.0, b1_ = 0.0, b2_ = 0.0;
    double a1_ = 0.0, a2_ = 0.0, a0_ = 1.0;
    double x1_ = 0.0, x2_ = 0.0;
    double y1_ = 0.0, y2_ = 0.0;
};
```

- [ ] **Step 2: 在 OLA 拉伸后应用补偿滤波**

在 `pitch_shift()` 中，OLA 拉伸后、编码前：

```cpp
if (config.vocal_protection) {
    const double pitch_ratio = std::pow(2.0, config.pitch_cents / 1200.0);
    if (pitch_ratio > 1.0) {
        // Pitch up: reduce excessive brightness with gentle low-pass.
        const double cutoff = 8000.0 / pitch_ratio;
        FirstOrderFilter lp(FirstOrderFilter::Type::LowPass, cutoff, enc_sample_rate);
        for (int ch = 0; ch < channels; ++ch) {
            for (float& sample : stretched[ch]) {
                sample = lp.process(sample);
            }
        }
    } else if (pitch_ratio < 1.0) {
        // Pitch down: reduce muffled sound with gentle high-pass.
        const double cutoff = 80.0 * pitch_ratio;
        FirstOrderFilter hp(FirstOrderFilter::Type::HighPass, cutoff, enc_sample_rate);
        for (int ch = 0; ch < channels; ++ch) {
            for (float& sample : stretched[ch]) {
                sample = hp.process(sample);
            }
        }
    }
}
```

- [ ] **Step 3: 构建并运行升降调测试**

Run:
```powershell
cmake --build build/debug --config Debug --target pitch_shifter_test
ctest -C Debug -R "^pitch_shifter_test$" --output-on-failure
```

Expected: 现有测试通过。

- [ ] **Step 4: 提交**

```bash
git add core/src/pitch_shifter.cpp
git commit -m "feat(core/pitch): implement experimental vocal protection filter"
```

---

## Task 6: Core 多轨道编辑器

**Files:**
- Create: `core/src/multitrack_editor.hpp`
- Create: `core/src/multitrack_editor.cpp`
- Modify: `core/src/c_api.cpp`
- Modify: `core/CMakeLists.txt`

**Interfaces:**
- Produces: `agplayer::multitrack_edit()`

- [ ] **Step 1: 创建 `core/src/multitrack_editor.hpp`**

```cpp
#pragma once

#include "agplayer/c_api.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace agplayer {

struct MultiTrackEditConfig {
    struct Track {
        std::string input_path;
        long long trim_start_ms = 0;
        long long trim_end_ms = 0;
        int fade_in_ms = 0;
        int fade_out_ms = 0;
        double gain = 1.0;
    };

    std::vector<Track> tracks;
    std::string output_path;
};

ag_result multitrack_edit(const MultiTrackEditConfig& config,
                          const std::atomic_bool* cancelled,
                          std::function<void(float)> progress_callback,
                          std::string& error);

} // namespace agplayer
```

- [ ] **Step 2: 创建 `core/src/multitrack_editor.cpp`**

实现要点：
- 逐轨道调用内部 helper 解码为 float32 planar，应用 trim/fade/gain。
- 确定主采样率 = 第一条有效轨道的采样率。
- 对其他轨道做重采样到主采样率。
- 所有轨道对齐到 0，输出长度 = 最长轨道样本数。
- 样本相加并硬削波到 [-1, 1]。
- 使用与 `transcoder.cpp` 相同的编码逻辑输出到 `output_path`。

- [ ] **Step 3: 在 `c_api.cpp` 中调用真实实现**

替换 Task 3 中的 `ag_multitrack_edit` 占位，转发到 `agplayer::multitrack_edit()`。

- [ ] **Step 4: 更新 `core/CMakeLists.txt`**

添加：

```cmake
    src/multitrack_editor.cpp
    src/multitrack_editor.hpp
```

- [ ] **Step 5: 构建 Core**

Run:
```powershell
cmake --build build/debug --config Debug --target agplayer_core
```

Expected: 编译成功。

- [ ] **Step 6: 提交**

```bash
git add core/src/multitrack_editor.hpp core/src/multitrack_editor.cpp core/src/c_api.cpp core/CMakeLists.txt
git commit -m "feat(core): implement multitrack non-destructive editor"
```

---

## Task 7: Qt Bridge LightEditor 多轨道化

**Files:**
- Modify: `qt/src/light_editor_controller.hpp`
- Modify: `qt/src/light_editor_controller.cpp`
- Modify: `qt/src/qml_registration.cpp`

**Interfaces:**
- Produces: 多轨道属性与方法

- [ ] **Step 1: 扩展 `LightEditor` 头文件**

新增属性：

```cpp
Q_PROPERTY(int trackCount READ trackCount CONSTANT)
Q_PROPERTY(int selectedTrack READ selectedTrack WRITE setSelectedTrack NOTIFY selectedTrackChanged)
Q_PROPERTY(QVariantList trackNames READ trackNames NOTIFY tracksChanged)
Q_PROPERTY(QVariantList trackHasFiles READ trackHasFiles NOTIFY tracksChanged)
Q_PROPERTY(QVariantList trackPeaks READ trackPeaks NOTIFY tracksChanged)
```

新增方法：

```cpp
Q_INVOKABLE void loadFileToTrack(int trackIndex, const QUrl& url);
Q_INVOKABLE void clearTrack(int trackIndex);
```

- [ ] **Step 2: 实现多轨道模型**

- 内部维护 `std::vector<Track>`，每个 `Track` 包含 path、name、peaks、duration、format、sampleRate、channels。
- `loadFile(const QUrl& url)` 加载到 `selectedTrack_`。
- `start()` 收集所有已加载轨道，调用 `ag_multitrack_edit`。
- 仅一条轨道时，行为等价于原有 `ag_light_edit`。

- [ ] **Step 3: 注册 QML 类型/属性**

确保 `qml_registration.cpp` 暴露新增属性与方法。

- [ ] **Step 4: 构建 Qt Bridge**

Run:
```powershell
cmake --build build/debug --config Debug --target agplayer_qt
```

Expected: 编译成功。

- [ ] **Step 5: 提交**

```bash
git add qt/src/light_editor_controller.hpp qt/src/light_editor_controller.cpp qt/src/qml_registration.cpp
git commit -m "feat(qt/light-edit): extend LightEditor to multi-track model"
```

---

## Task 8: QML 轻量剪辑页多轨道交互

**Files:**
- Modify: `app/qml/AgPlayer/components/tools/LightEditPage.qml`

**Interfaces:**
- Consumes: `LightEditor` 多轨道属性

- [ ] **Step 1: 轨道操作绑定到选中轨道**

- "Add File" 按钮调用 `editor.loadFileToTrack(editor.selectedTrack, fileUrl)` 或保持 `editor.loadFile(url)`（后者加载到当前选中轨道）。
- "Clear" 按钮调用 `editor.clearTrack(editor.selectedTrack)`。
- `MultiTrackWaveform` 的 `isActive` 从固定 `index === 0` 改为 `index === editor.selectedTrack`。

- [ ] **Step 2: 导出设置生效**

将导出设置中的 `formatCombo`、`sampleRateCombo`、`channelsCombo` 实际传入 `editor.start()`。需要扩展 `LightEditor::start()` 签名以接收输出格式/采样率/声道。

- [ ] **Step 3: 移除 preview-only 提示**

移除或更新 "Output format, sample rate and channel settings are preview-only in this phase" 提示。

- [ ] **Step 4: 构建 QML 插件**

Run:
```powershell
cmake --build build/debug --config Debug --target agplayer_app_qml
```

Expected: 编译成功。

- [ ] **Step 5: 提交**

```bash
git add app/qml/AgPlayer/components/tools/LightEditPage.qml
git commit -m "feat(qml/light-edit): bind multi-track load/clear/export to selected track"
```

---

## Task 9: 单元测试

**Files:**
- Modify: `tests/core/transcoder_test.cpp`
- Modify: `tests/core/pitch_shifter_test.cpp`
- Create: `tests/core/multitrack_editor_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: 新增音量标准化测试**

在 `transcoder_test.cpp` 中：

```cpp
// Volume normalize: output peak should approach -1 dBFS for a quiet fixture.
// For sine fixture (already loud), gain should be capped at 1.0.
{
    const std::filesystem::path output = input_path.parent_path() / "transcoder-normalized.wav";
    std::filesystem::remove(output);
    agplayer::TranscodeConfig config;
    config.output_path = output.string();
    config.codec_name = "pcm_s16le";
    config.volume_normalize = true;
    std::string error;
    const ag_result result = agplayer::transcode(input_path.string(), config, nullptr, nullptr, error);
    assert(result == AG_OK);
    assert(std::filesystem::exists(output));
    std::filesystem::remove(output);
}
```

- [ ] **Step 2: 新增人声保护对比测试**

在 `pitch_shifter_test.cpp` 中生成两个输出（with/without vocal_protection），断言升调时保护版本高频能量更低。

- [ ] **Step 3: 新增多轨道编辑测试**

创建 `tests/core/multitrack_editor_test.cpp`：

```cpp
#include "multitrack_editor.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path input_path = argv[1];

    const std::filesystem::path output = input_path.parent_path() / "multitrack-out.wav";
    std::filesystem::remove(output);

    agplayer::MultiTrackEditConfig config;
    agplayer::MultiTrackEditConfig::Track track;
    track.input_path = input_path.string();
    track.gain = 0.5;
    config.tracks.push_back(track);
    config.output_path = output.string();

    std::string error;
    const ag_result result = agplayer::multitrack_edit(config, nullptr, nullptr, error);
    if (result != AG_OK) {
        std::cerr << "multitrack_edit failed: " << static_cast<int>(result) << " " << error << "\n";
    }
    assert(result == AG_OK);
    assert(std::filesystem::exists(output));
    std::filesystem::remove(output);
    return 0;
}
```

- [ ] **Step 4: 注册测试到 CMake**

在 `tests/CMakeLists.txt` 中添加：

```cmake
add_executable(multitrack_editor_test
    core/multitrack_editor_test.cpp
)
add_dependencies(multitrack_editor_test decoder_fixture)
target_include_directories(multitrack_editor_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
)
target_link_libraries(multitrack_editor_test PRIVATE agplayer_core)
agplayer_enable_warnings(multitrack_editor_test)
add_test(NAME multitrack_editor_test COMMAND multitrack_editor_test "${SINE_WAV_FIXTURE}")
set_tests_properties(multitrack_editor_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin"
)
```

- [ ] **Step 5: 构建并运行新增测试**

Run:
```powershell
cmake --build build/debug --config Debug --target transcoder_test pitch_shifter_test multitrack_editor_test
ctest -C Debug -R "^(transcoder_test|pitch_shifter_test|multitrack_editor_test)$" --output-on-failure
```

Expected: 全部通过。

- [ ] **Step 6: 提交**

```bash
git add tests/core/transcoder_test.cpp tests/core/pitch_shifter_test.cpp tests/core/multitrack_editor_test.cpp tests/CMakeLists.txt
git commit -m "test(core): add volume normalize, vocal protection and multitrack tests"
```

---

## Task 10: 全局回归验证

**Files:**
- 所有已修改文件

- [ ] **Step 1: Debug 全量构建与测试**

Run:
```powershell
Import-Module "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -InstallPath "C:\Program Files\Microsoft Visual Studio\2022\Community" -SkipAutomaticLocation -Arch amd64
cmake --build build/debug --config Debug
cd build/debug
ctest -C Debug --output-on-failure
```

Expected: 100% tests passed。

- [ ] **Step 2: Release 全量构建与测试**

Run:
```powershell
cmake --build build/release --config Release
cd build/release
ctest -C Release --output-on-failure
```

Expected: 100% tests passed。

- [ ] **Step 3: 提交修复（如有）**

若回归中发现问题，提交修复；否则跳过。

---

## 自评检查

**1. Spec coverage:**
- 音量标准化 → Task 1-2 + Task 4 + Task 9
- 人声保护 → Task 5 + Task 9
- 视频音频提取 → Task 4
- 多轨道编辑 → Task 1 + Task 6-8 + Task 9

**2. Placeholder scan:**
- 移除 FormatConverter 中 "not supported yet" 警告。
- 移除 LightEditPage 中 "preview-only" 提示。
- 无 "TBD" / "TODO" 残留。

**3. Type一致性:**
- `ag_transcode_options` 与 `TranscodeConfig::volume_normalize` 语义一致。
- `ag_multitrack_edit` 参数顺序与 `ag_light_edit` 一致，扩展为数组形式。
