# AgPlayer Phase 3-3 精确 BPM 检测设计文档

> **目标**：将当前基于文件大小的占位 BPM 检测替换为基于音频内容的离线精确 BPM 分析，分析结果缓存到曲库避免重复计算。
> **范围**：Core 层分析器、C API 扩展、Qt Bridge 集成、曲库缓存、单元测试。不包含新的 QML/UI 设计稿（沿用现有 BPM 显示与 `autoReadBpm` 设置）。
> **平台**：以 Windows x64 为主实现平台，算法纯 C++17，跨平台可复用。

---

## 1. 设计约束

- 必须保持现有三层架构：Core（C++17 / C ABI）负责音频分析；Qt Bridge 负责调用与缓存；QML 只负责展示。
- 所有功能必须可离线工作，不访问网络。
- 错误必须静默处理（通过 `RuntimeLog`），不能阻断导入或弹出未请求的错误窗。
- 不改变 `core/include/agplayer/c_api.h` 现有公开函数签名，只新增函数/结构。
- 分析过程必须是离线/异步的，不能在播放线程中执行实时 FFT。
- 所有修改必须在 Windows x64 Debug/Release 下通过 `ctest`。

---

## 2. 文件结构

### 2.1 新增文件

| 文件 | 职责 |
|------|------|
| `core/src/bpm_analyzer.hpp` | BPM 分析内部接口与数据结构 |
| `core/src/bpm_analyzer.cpp` | 基于 FFmpeg 解码 + onset + 自相关的 BPM 算法实现 |
| `tests/core/bpm_analyzer_test.cpp` | 使用已知 BPM fixture 的单元测试 |

### 2.2 修改文件

| 文件 | 修改内容 |
|------|----------|
| `core/include/agplayer/c_api.h` | 新增 `ag_bpm_result`、`ag_bpm_analyze()` |
| `core/src/c_api.cpp` | 实现 `ag_bpm_analyze()`，包装 Core 分析器 |
| `qt/src/bpm_analyzer.hpp` | 删除占位实现，改为调用 Core C API |
| `qt/src/import_controller.cpp` / `library_model.cpp` | 在 `autoReadBpm` 启用时调用真实分析并缓存结果 |
| `core/CMakeLists.txt` | 添加 `bpm_analyzer.cpp` |
| `tests/CMakeLists.txt` | 添加 `bpm_analyzer_test` |

---

## 3. Core BPM 分析器

### 3.1 算法流程

1. **解码音频**：使用现有 FFmpeg 解码器将音频解码为单声道、16 kHz（或原采样率）浮点 PCM，最多取前 90 秒。
2. **短时傅里叶变换（STFT）**：分帧计算频谱幅值。
3. **Onset Strength（频谱通量）**：计算相邻帧频谱差异，得到 onset envelope。
4. **自相关 + 梳状滤波**：在 45–210 BPM 范围内搜索最佳候选节拍周期。
5. **峰值细化**：对候选周期做抛物线插值，提高分辨率。
6. **置信度**：最佳峰值与次佳峰值比值映射到 0–100。

### 3.2 内部接口

```cpp
namespace agplayer {

struct BpmAnalyzeInput {
    const char* file_path = nullptr;
    int max_duration_seconds = 90;
};

struct BpmAnalyzeOutput {
    double bpm = 0.0;
    double confidence = 0.0;
};

// Returns AG_OK on success, or an error code if analysis fails.
ag_error_code analyze_bpm(const BpmAnalyzeInput& input, BpmAnalyzeOutput* out);

} // namespace agplayer
```

### 3.3 C API

```c
// In core/include/agplayer/c_api.h

typedef struct ag_bpm_result {
    double bpm;
    double confidence;
} ag_bpm_result;

// Analyzes the audio file and fills `out`.
// Returns AG_OK on success. The caller does not need to free `out`.
AGPLAYER_API ag_error_code ag_bpm_analyze(const char* file_path, ag_bpm_result* out);
```

---

## 4. Qt Bridge 集成

### 4.1 替换占位实现

`qt/src/bpm_analyzer.hpp` 当前返回基于文件大小的伪 BPM。改为：

```cpp
#pragma once

#include <QString>

struct BpmAnalyzeResult {
    double bpm = 0.0;
    double confidence = 0.0;
};

BpmAnalyzeResult analyze_bpm(const QString& filePath);
```

实现位于 `qt/src/bpm_analyzer.cpp`，调用 `ag_bpm_analyze()`：

```cpp
#include "bpm_analyzer.hpp"
#include "runtime_log.hpp"
#include <agplayer/c_api.h>

BpmAnalyzeResult analyze_bpm(const QString& filePath)
{
    ag_bpm_result result{};
    const QByteArray path = filePath.toUtf8();
    if (ag_bpm_analyze(path.constData(), &result) != AG_OK) {
        RuntimeLog::log(AG_IO_ERROR, QStringLiteral("BPM"),
            QStringLiteral("Failed to analyze BPM for %1").arg(filePath));
        return {0.0, 0.0};
    }
    return {result.bpm, result.confidence};
}
```

### 4.2 导入时自动分析

在 `ImportController` 完成导入后，如果 `SettingsController::autoReadBpm()` 为 true，则对新增曲目调用 `analyze_bpm()`，并将结果写入 `TrackRecord::bpm`。`LibraryModel` 负责更新对应行的 `BpmRole`。

---

## 5. 缓存策略

- BPM 值持久化在 `library.json` 的 `bpm` 字段中（已存在）。
- 当曲目已存在非零 BPM 时，不再重新分析，除非用户触发重新分析（本阶段不提供重新分析 UI，后续可扩展）。
- 分析失败时保留 `0.0` 并记录 `RuntimeLog`，下次导入不再重试。

---

## 6. 测试策略

### 6.1 Core 单元测试

创建 `tests/core/bpm_analyzer_test.cpp`：

- 使用已生成的测试 fixture（正弦波扫频或已知 BPM 的合成节拍音频）。
- 断言检测 BPM 与期望值误差在 ±1 BPM 以内。
- 断言置信度 > 80。
- 断言无效/空文件返回错误且 `bpm == 0.0`。

### 6.2 Qt Bridge 测试

扩展 `library_model_test` 或 `import_controller_test`：

- 验证导入已知 BPM fixture 后 `TrackRecord::bpm` 接近预期值。
- 验证 `autoReadBpm` 关闭时不进行分析。

---

## 7. 风险与回退

- **分析过慢**：若 90 秒音频分析耗时超过 2 秒，后续可缩短采样时长或加入下采样。
- **精度不足**：电子/古典音乐可能检测不准。本阶段只返回单一主导 BPM 和置信度，不处理变速。
- **编译依赖**：Core 已依赖 FFmpeg，无新增外部依赖。

---

## 8. 不在本阶段范围

- 手动 Tap Tempo UI
- 变速/动态 BPM 检测
- 节拍网格可视化增强
- 批量重新分析工具

---

## 9. 验收标准

- Windows x64 Debug/Release 全量构建通过。
- `ctest` 全部通过（含新增 `bpm_analyzer_test`）。
- 对已知 BPM 的测试 fixture，检测结果误差 ≤ 1 BPM。
- `autoReadBpm` 关闭时，导入不触发分析。
