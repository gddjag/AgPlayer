# AgPlayer Phase 3-3 精确 BPM 检测实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将当前基于文件大小的占位 BPM 检测替换为基于音频内容的离线精确 BPM 分析，通过 Core C API 暴露，集成到 Qt Bridge 导入流程并缓存到曲库。

**Architecture:** 新增 Core 层 `BpmAnalyzer`，复用现有 `Decoder` 解码为单声道 16 kHz 浮点 PCM，计算 onset envelope 后通过自相关估计 BPM。结果经 `ag_bpm_analyze()` 暴露给 Qt Bridge。Qt Bridge 的 `bpm_analyzer.hpp` 改为调用 C API 的薄封装。`probeMetadata()` 在 `autoReadBpm` 启用时顺带分析 BPM。

**Tech Stack:** C++17, Qt 6.7, FFmpeg（通过现有 Decoder）, CMake, ctest。

## Global Constraints

- 必须保持现有三层架构：Core（C++17 / C ABI）负责音频分析；Qt Bridge 负责调用与缓存；QML 只负责展示。
- 所有功能必须可离线工作，不访问网络。
- 错误必须静默处理（通过 `RuntimeLog`），不能阻断导入或弹出未请求的错误窗。
- 不改变 `core/include/agplayer/c_api.h` 现有公开函数签名，只新增函数/结构。
- 分析过程必须是离线/异步的，不能在播放线程中执行实时 FFT。
- 所有修改必须在 Windows x64 Debug/Release 下通过 `ctest`。

---

## Task 1: 扩展 C API 头文件

**Files:**
- Modify: `core/include/agplayer/c_api.h`

**Interfaces:**
- Produces: `ag_bpm_result` 结构体，`ag_bpm_analyze()` 声明

- [ ] **Step 1: 在 `ag_waveform_destroy` 之后新增 BPM 类型与函数**

```c
typedef struct ag_bpm_result {
    double bpm;
    double confidence;
} ag_bpm_result;

/* Analyze the BPM of an audio file offline.
 * Returns AG_OK on success and fills `out`. The caller does not free `out`.
 * Returns AG_INVALID_ARGUMENT if file_path or out is NULL.
 * Returns AG_DECODE_ERROR if the file cannot be decoded or is too short. */
ag_result ag_bpm_analyze(const char* file_path, ag_bpm_result* out);
```

- [ ] **Step 2: 构建 Core 检查头文件编译**

Run:
```powershell
Import-Module "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -InstallPath "C:\Program Files\Microsoft Visual Studio\2022\Community" -SkipAutomaticLocation -Arch amd64
cmake --build build/debug --config Debug --target agplayer_core
```

Expected: `agplayer_core` 编译成功，无新增警告。

- [ ] **Step 3: 提交**

```bash
git add core/include/agplayer/c_api.h
git commit -m "feat(core/api): declare ag_bpm_analyze and ag_bpm_result"
```

---

## Task 2: 实现 Core BPM 分析器

**Files:**
- Create: `core/src/bpm_analyzer.hpp`
- Create: `core/src/bpm_analyzer.cpp`

**Interfaces:**
- Consumes: `agplayer::Decoder`, `ag_result`
- Produces: `agplayer::BpmAnalyzeInput`, `agplayer::BpmAnalyzeOutput`, `agplayer::analyze_bpm()`

- [ ] **Step 1: 创建 `core/src/bpm_analyzer.hpp`**

```cpp
#pragma once

#include <agplayer/c_api.h>

namespace agplayer {

struct BpmAnalyzeInput {
    const char* file_path = nullptr;
    int max_duration_seconds = 90;
};

struct BpmAnalyzeOutput {
    double bpm = 0.0;
    double confidence = 0.0;
};

// Offline BPM analysis. Returns AG_OK on success.
ag_error_code analyze_bpm(const BpmAnalyzeInput& input, BpmAnalyzeOutput* out);

} // namespace agplayer
```

- [ ] **Step 2: 创建 `core/src/bpm_analyzer.cpp`**

```cpp
#include "bpm_analyzer.hpp"

#include "decoder.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <vector>

namespace agplayer {
namespace {

constexpr int kTargetSampleRate = 16000;
constexpr int kFrameSize = 512;   // 32 ms at 16 kHz
constexpr int kHopSize = 256;     // 16 ms hop
constexpr double kMinBpm = 45.0;
constexpr double kMaxBpm = 210.0;
constexpr std::size_t kMinFrames = 2 * kTargetSampleRate; // 2 seconds

class LowPassFilter {
public:
    explicit LowPassFilter(double alpha) noexcept
        : alpha_(alpha)
    {
    }

    float process(float input) noexcept
    {
        state_ = static_cast<float>((1.0 - alpha_) * state_ + alpha_ * input);
        return state_;
    }

private:
    double alpha_;
    float state_ = 0.0f;
};

std::vector<float> compute_onset_envelope(const std::vector<float>& pcm)
{
    LowPassFilter envelope_lp(0.15);
    std::vector<float> envelope;
    envelope.reserve(pcm.size() / kHopSize + 1);

    float prev_envelope = 0.0f;
    for (std::size_t i = 0; i + kFrameSize <= pcm.size(); i += kHopSize) {
        float frame_energy = 0.0f;
        for (int j = 0; j < kFrameSize; ++j) {
            frame_energy += std::abs(pcm[i + j]);
        }
        const float smoothed = envelope_lp.process(frame_energy);
        float novelty = smoothed - prev_envelope;
        if (novelty < 0.0f) {
            novelty = 0.0f;
        }
        envelope.push_back(novelty);
        prev_envelope = smoothed;
    }

    if (envelope.empty()) {
        return envelope;
    }

    // Remove DC.
    const float mean = std::accumulate(envelope.begin(), envelope.end(), 0.0f)
                       / static_cast<float>(envelope.size());
    for (float& v : envelope) {
        v = std::max(0.0f, v - mean);
    }

    // Normalize.
    const float max_val = *std::max_element(envelope.begin(), envelope.end());
    if (max_val > 0.0f) {
        for (float& v : envelope) {
            v /= max_val;
        }
    }

    return envelope;
}

std::vector<double> autocorrelate(const std::vector<float>& signal)
{
    const std::size_t n = signal.size();
    std::vector<double> ac(n, 0.0);
    for (std::size_t lag = 0; lag < n; ++lag) {
        double sum = 0.0;
        for (std::size_t i = 0; i + lag < n; ++i) {
            sum += static_cast<double>(signal[i]) * static_cast<double>(signal[i + lag]);
        }
        ac[lag] = sum;
    }
    return ac;
}

} // namespace

ag_error_code analyze_bpm(const BpmAnalyzeInput& input, BpmAnalyzeOutput* out)
{
    if (out == nullptr || input.file_path == nullptr || input.file_path[0] == '\0') {
        return AG_INVALID_ARGUMENT;
    }
    *out = {0.0, 0.0};

    Decoder decoder;
    if (decoder.open(input.file_path, kTargetSampleRate, 1) != AG_OK) {
        return AG_DECODE_ERROR;
    }

    std::vector<float> pcm;
    pcm.reserve(static_cast<std::size_t>(input.max_duration_seconds) * kTargetSampleRate);

    DecodedAudioBlock block;
    const std::int64_t max_frames = static_cast<std::int64_t>(input.max_duration_seconds)
                                    * kTargetSampleRate;
    while (decoder.read(block) == AG_OK) {
        if (block.end_of_stream) {
            break;
        }
        for (float sample : block.samples) {
            if (!std::isfinite(sample)) {
                return AG_DECODE_ERROR;
            }
            pcm.push_back(sample);
            if (static_cast<std::int64_t>(pcm.size()) >= max_frames) {
                break;
            }
        }
        if (static_cast<std::int64_t>(pcm.size()) >= max_frames) {
            break;
        }
    }

    if (pcm.size() < kMinFrames) {
        return AG_DECODE_ERROR;
    }

    const std::vector<float> envelope = compute_onset_envelope(pcm);
    if (envelope.size() < 10) {
        return AG_DECODE_ERROR;
    }

    const std::vector<double> ac = autocorrelate(envelope);

    const double frames_per_second = static_cast<double>(kTargetSampleRate)
                                     / static_cast<double>(kHopSize);
    const std::size_t min_lag = static_cast<std::size_t>(
        frames_per_second * 60.0 / kMaxBpm);
    const std::size_t max_lag = static_cast<std::size_t>(
        frames_per_second * 60.0 / kMinBpm);

    if (max_lag >= ac.size() || min_lag >= max_lag) {
        return AG_DECODE_ERROR;
    }

    std::size_t best_lag = min_lag;
    double best_value = ac[min_lag];
    for (std::size_t lag = min_lag + 1; lag <= max_lag; ++lag) {
        if (ac[lag] > best_value) {
            best_value = ac[lag];
            best_lag = lag;
        }
    }

    // Parabolic interpolation around the best lag.
    double interpolated_lag = static_cast<double>(best_lag);
    if (best_lag > min_lag && best_lag + 1 < ac.size()) {
        const double y0 = ac[best_lag - 1];
        const double y1 = ac[best_lag];
        const double y2 = ac[best_lag + 1];
        const double denom = 2.0 * (2.0 * y1 - y0 - y2);
        if (denom != 0.0) {
            interpolated_lag += (y0 - y2) / denom;
        }
    }

    out->bpm = frames_per_second * 60.0 / interpolated_lag;
    out->bpm = std::clamp(out->bpm, kMinBpm, kMaxBpm);

    double sum = 0.0;
    for (std::size_t lag = min_lag; lag <= max_lag; ++lag) {
        sum += ac[lag];
    }
    const double mean = sum / static_cast<double>(max_lag - min_lag + 1);
    if (mean > 0.0) {
        out->confidence = std::min(100.0, (best_value / mean) * 8.0);
    }

    return AG_OK;
}

} // namespace agplayer
```

- [ ] **Step 3: 将 `bpm_analyzer.cpp` 加入 `core/CMakeLists.txt`**

在 `src/waveform_cache.cpp` 之前插入：

```cmake
    src/bpm_analyzer.cpp
    src/bpm_analyzer.hpp
```

- [ ] **Step 4: 构建 Core**

Run:
```powershell
cmake --build build/debug --config Debug --target agplayer_core
```

Expected: `agplayer_core` 编译成功，无新增警告。

- [ ] **Step 5: 提交**

```bash
git add core/src/bpm_analyzer.hpp core/src/bpm_analyzer.cpp core/CMakeLists.txt
git commit -m "feat(core): implement offline BPM analyzer with onset envelope and autocorrelation"
```

---

## Task 3: 在 C API 中包装 BPM 分析器

**Files:**
- Modify: `core/src/c_api.cpp`

**Interfaces:**
- Consumes: `agplayer::analyze_bpm()`
- Produces: `ag_bpm_analyze()` implementation

- [ ] **Step 1: 包含头文件并添加 C 函数实现**

在 `core/src/c_api.cpp` 顶部现有 include 之后添加：

```cpp
#include "bpm_analyzer.hpp"
```

在文件末尾、所有现有 C API 实现之后（`ag_waveform_destroy` 之后）添加：

```cpp
ag_result ag_bpm_analyze(const char* file_path, ag_bpm_result* out)
{
    return guard_result([&]() noexcept -> ag_result {
        if (file_path == nullptr || out == nullptr) {
            return AG_INVALID_ARGUMENT;
        }
        agplayer::BpmAnalyzeInput input;
        input.file_path = file_path;
        agplayer::BpmAnalyzeOutput output{};
        const ag_result result = agplayer::analyze_bpm(input, &output);
        if (result == AG_OK) {
            out->bpm = output.bpm;
            out->confidence = output.confidence;
        } else {
            out->bpm = 0.0;
            out->confidence = 0.0;
        }
        return result;
    });
}
```

- [ ] **Step 2: 构建并运行依赖测试**

Run:
```powershell
cmake --build build/debug --config Debug --target c_api_lifecycle_test dependency_smoke_test
ctest -C Debug -R "^(c_api_lifecycle_test|dependency_smoke_test)$" --output-on-failure
```

Expected: 两个测试均通过。

- [ ] **Step 3: 提交**

```bash
git add core/src/c_api.cpp
git commit -m "feat(core/api): implement ag_bpm_analyze wrapper"
```

---

## Task 4: 生成 BPM 测试 fixture

**Files:**
- Create: `tests/core/bpm_fixture.hpp`

**Interfaces:**
- Produces: `writeClickTrackWav(const QString& path, int bpm, int duration_seconds)`

- [ ] **Step 1: 创建测试 fixture helper**

```cpp
#pragma once

#include <QDataStream>
#include <QFile>
#include <QString>

namespace agplayer {
namespace test {

// Writes a mono 16-bit 16 kHz WAV file with a sine-burst click every 60/bpm seconds.
// Suitable for verifying BPM analysis accuracy.
inline bool writeClickTrackWav(const QString& path, int bpm, int duration_seconds)
{
    if (bpm <= 0 || duration_seconds <= 0) {
        return false;
    }

    constexpr int sample_rate = 16000;
    constexpr int channels = 1;
    constexpr int bits_per_sample = 16;
    const int total_samples = sample_rate * duration_seconds;
    const int byte_rate = sample_rate * channels * bits_per_sample / 8;
    const int block_align = channels * bits_per_sample / 8;
    const int data_size = total_samples * block_align;
    const int file_size = 44 + data_size - 8;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);

    // RIFF header
    stream.writeRawData("RIFF", 4);
    stream << file_size;
    stream.writeRawData("WAVE", 4);

    // fmt chunk
    stream.writeRawData("fmt ", 4);
    stream << 16;                              // Subchunk1Size
    stream << static_cast<qint16>(1);          // AudioFormat = PCM
    stream << static_cast<qint16>(channels);
    stream << sample_rate;
    stream << byte_rate;
    stream << static_cast<qint16>(block_align);
    stream << static_cast<qint16>(bits_per_sample);

    // data chunk
    stream.writeRawData("data", 4);
    stream << data_size;

    const double period_seconds = 60.0 / static_cast<double>(bpm);
    const int period_samples = static_cast<int>(period_seconds * sample_rate);
    constexpr int click_duration = 400; // samples (~25 ms)
    constexpr double click_freq = 1000.0 * 2.0 * M_PI / sample_rate;

    for (int i = 0; i < total_samples; ++i) {
        const int within_period = i % period_samples;
        qint16 sample = 0;
        if (within_period < click_duration) {
            const double envelope = 1.0 - static_cast<double>(within_period) / click_duration;
            sample = static_cast<qint16>(
                std::sin(click_freq * i) * envelope * 32767.0 * 0.9);
        }
        stream << sample;
    }

    return file.error() == QFile::NoError;
}

} // namespace test
} // namespace agplayer
```

- [ ] **Step 2: 提交**

```bash
git add tests/core/bpm_fixture.hpp
git commit -m "test(core): add click-track WAV fixture helper for BPM tests"
```

---

## Task 5: 添加 Core BPM 单元测试

**Files:**
- Create: `tests/core/bpm_analyzer_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `ag_bpm_analyze()`, `writeClickTrackWav()`
- Produces: passing unit tests

- [ ] **Step 1: 创建测试文件**

```cpp
#include <QDir>
#include <QTemporaryFile>
#include <QTest>

#include <agplayer/c_api.h>

#include "bpm_fixture.hpp"

class BpmAnalyzerTest : public QObject {
    Q_OBJECT

private slots:
    void analyzeKnownBpm120()
    {
        QTemporaryFile tempFile(QDir::temp().filePath(QStringLiteral("ag_bpm_120_XXXXXX.wav")));
        QVERIFY(tempFile.open());
        tempFile.close();

        QVERIFY(agplayer::test::writeClickTrackWav(tempFile.fileName(), 120, 8));

        ag_bpm_result result{};
        QCOMPARE(ag_bpm_analyze(tempFile.fileName().toUtf8().constData(), &result), AG_OK);
        QVERIFY(std::abs(result.bpm - 120.0) < 1.0);
        QVERIFY(result.confidence > 80.0);
    }

    void analyzeKnownBpm90()
    {
        QTemporaryFile tempFile(QDir::temp().filePath(QStringLiteral("ag_bpm_90_XXXXXX.wav")));
        QVERIFY(tempFile.open());
        tempFile.close();

        QVERIFY(agplayer::test::writeClickTrackWav(tempFile.fileName(), 90, 10));

        ag_bpm_result result{};
        QCOMPARE(ag_bpm_analyze(tempFile.fileName().toUtf8().constData(), &result), AG_OK);
        QVERIFY(std::abs(result.bpm - 90.0) < 1.0);
        QVERIFY(result.confidence > 80.0);
    }

    void analyzeKnownBpm140()
    {
        QTemporaryFile tempFile(QDir::temp().filePath(QStringLiteral("ag_bpm_140_XXXXXX.wav")));
        QVERIFY(tempFile.open());
        tempFile.close();

        QVERIFY(agplayer::test::writeClickTrackWav(tempFile.fileName(), 140, 8));

        ag_bpm_result result{};
        QCOMPARE(ag_bpm_analyze(tempFile.fileName().toUtf8().constData(), &result), AG_OK);
        QVERIFY(std::abs(result.bpm - 140.0) < 1.0);
        QVERIFY(result.confidence > 80.0);
    }

    void invalidArgumentsReturnError()
    {
        ag_bpm_result result{};
        QCOMPARE(ag_bpm_analyze(nullptr, &result), AG_INVALID_ARGUMENT);
        QCOMPARE(ag_bpm_analyze("some/path.wav", nullptr), AG_INVALID_ARGUMENT);
    }

    void missingFileReturnsError()
    {
        ag_bpm_result result{};
        QCOMPARE(ag_bpm_analyze("/nonexistent/file/that/does/not/exist.wav", &result),
                 AG_IO_ERROR);
        QCOMPARE(result.bpm, 0.0);
        QCOMPARE(result.confidence, 0.0);
    }
};

QTEST_MAIN(BpmAnalyzerTest)

#include "bpm_analyzer_test.moc"
```

- [ ] **Step 2: 在 `tests/CMakeLists.txt` 注册测试**

在 `dependency_smoke_test` 注册之后（或其他 Core 测试附近）添加：

```cmake
add_executable(bpm_analyzer_test
    core/bpm_analyzer_test.cpp
)
set_target_properties(bpm_analyzer_test PROPERTIES AUTOMOC ON)
target_include_directories(bpm_analyzer_test PRIVATE
    "${CMAKE_SOURCE_DIR}/core/src"
    "${CMAKE_SOURCE_DIR}/tests/core"
)
target_link_libraries(bpm_analyzer_test PRIVATE agplayer_core Qt6::Test)
agplayer_enable_warnings(bpm_analyzer_test)

add_test(NAME bpm_analyzer_test COMMAND bpm_analyzer_test)
set_tests_properties(bpm_analyzer_test PROPERTIES
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin"
)
```

- [ ] **Step 3: 构建并运行测试**

Run:
```powershell
cmake --build build/debug --config Debug --target bpm_analyzer_test
ctest -C Debug -R "^bpm_analyzer_test$" --output-on-failure
```

Expected: 5 个测试全部通过。

- [ ] **Step 4: 提交**

```bash
git add tests/core/bpm_analyzer_test.cpp tests/core/bpm_fixture.hpp tests/CMakeLists.txt
git commit -m "test(core): add BPM analyzer unit tests with synthetic click tracks"
```

---

## Task 6: Qt Bridge 替换占位 BPM 实现

**Files:**
- Modify: `qt/src/bpm_analyzer.hpp`
- Create: `qt/src/bpm_analyzer.cpp`
- Modify: `qt/CMakeLists.txt`

**Interfaces:**
- Consumes: `ag_bpm_analyze()`
- Produces: `BpmAnalyzeResult analyze_bpm(const QString& filePath)`

- [ ] **Step 1: 修改 `qt/src/bpm_analyzer.hpp` 删除占位实现**

替换整个文件内容为：

```cpp
#pragma once

#include <QString>

struct BpmAnalyzeResult {
    double bpm = 0.0;
    double confidence = 0.0;
};

BpmAnalyzeResult analyze_bpm(const QString& filePath);
```

- [ ] **Step 2: 创建 `qt/src/bpm_analyzer.cpp`**

```cpp
#include "bpm_analyzer.hpp"

#include "runtime_log.hpp"

#include <agplayer/c_api.h>

BpmAnalyzeResult analyze_bpm(const QString& filePath)
{
    BpmAnalyzeResult result;
    ag_bpm_result api_result{};
    const QByteArray path = filePath.toUtf8();
    if (ag_bpm_analyze(path.constData(), &api_result) != AG_OK) {
        RuntimeLog::log(AG_IO_ERROR, QStringLiteral("BPM"),
            QStringLiteral("Failed to analyze BPM for %1").arg(filePath));
        return result;
    }
    result.bpm = api_result.bpm;
    result.confidence = api_result.confidence;
    return result;
}
```

- [ ] **Step 3: 将 `bpm_analyzer.cpp` 加入 `qt/CMakeLists.txt`**

在 `src/audio_tools_controller.cpp` 之前（或 `src` 文件列表的合适位置）插入：

```cmake
    src/bpm_analyzer.cpp
    src/bpm_analyzer.hpp
```

- [ ] **Step 4: 构建 Qt Bridge**

Run:
```powershell
cmake --build build/debug --config Debug --target agplayer_qt
```

Expected: `agplayer_qt` 编译成功。

- [ ] **Step 5: 提交**

```bash
git add qt/src/bpm_analyzer.hpp qt/src/bpm_analyzer.cpp qt/CMakeLists.txt
git commit -m "feat(qt): replace placeholder BPM analyzer with Core API wrapper"
```

---

## Task 7: 导入流程集成 `autoReadBpm`

**Files:**
- Modify: `qt/src/import_controller.cpp`
- Modify: `app/main.cpp`

**Interfaces:**
- Consumes: `SettingsController::autoReadBpm()`, `analyze_bpm()`
- Produces: 导入时自动写入 `TrackRecord::bpm`

- [ ] **Step 1: 修改 `probeMetadata` 支持可选 BPM 分析**

在 `qt/src/import_controller.cpp` 中，将 `probeMetadata` 函数签名从：

```cpp
ProbeResult probeMetadata(const QString& requestedPath)
```

改为：

```cpp
ProbeResult probeMetadata(const QString& requestedPath, bool analyzeBpm)
```

在函数末尾、`track.coverUrl = cacheCover(...)` 之后、`return {AG_OK, std::move(track), {}}` 之前添加：

```cpp
    if (analyzeBpm) {
        const BpmAnalyzeResult bpm = analyze_bpm(path);
        track.bpm = bpm.bpm;
    }
```

- [ ] **Step 2: 更新 `ImportController` 构造函数中的默认 probe**

将：

```cpp
ImportController::ImportController(LibraryModel* model, QObject* parent)
    : ImportController(model, probeMetadata, parent)
{
}
```

改为：

```cpp
ImportController::ImportController(LibraryModel* model, QObject* parent)
    : ImportController(model, [](const QString& path) {
          return probeMetadata(path, false);
      }, parent)
{
}
```

- [ ] **Step 3: 修改 `app/main.cpp` 在构造 `ImportController` 时传入 `autoReadBpm`**

找到：

```cpp
        ImportController importer(&library);
```

替换为：

```cpp
        const bool autoReadBpm = settings.autoReadBpm();
        ImportController importer(&library, [autoReadBpm](const QString& path) {
            return probeMetadata(path, autoReadBpm);
        });
```

- [ ] **Step 4: 构建并运行相关测试**

Run:
```powershell
cmake --build build/debug --config Debug --target import_controller_test library_model_test qml_main_window_test
ctest -C Debug -R "^(import_controller_test|library_model_test|qml_main_window_test)$" --output-on-failure
```

Expected: 全部通过。

- [ ] **Step 5: 提交**

```bash
git add qt/src/import_controller.cpp app/main.cpp
git commit -m "feat(qt/import): analyze BPM during import when autoReadBpm is enabled"
```

---

## Task 8: 全局回归验证

**Files:**
- 所有已修改文件

**Interfaces:**
- 全项目构建与测试

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

- [ ] **Step 3: 提交（如有未提交的变更）**

如果回归过程中没有产生新提交，则跳过；否则提交修复。

---

## 自评检查

**1. Spec coverage:**
- Core 离线 BPM 分析器 → Task 2
- C API 扩展 → Task 1 + Task 3
- Qt Bridge 替换占位实现 → Task 6
- 导入时自动分析 → Task 7
- 结果缓存到曲库 → 沿用现有 `TrackRecord::bpm` 与 `library.json`
- 单元测试 → Task 4 + Task 5

**2. Placeholder scan:**
- 无 "TBD" / "TODO" / "implement later"
- 每个步骤包含具体文件路径、代码、命令

**3. Type一致性:**
- `ag_bpm_result` 与 `BpmAnalyzeOutput` 字段一致（`bpm`, `confidence`）
- `ag_bpm_analyze()` 签名在头文件与实现中一致
- `analyze_bpm(const QString&)` 返回类型保持 `BpmAnalyzeResult` 不变，消费者无需修改
