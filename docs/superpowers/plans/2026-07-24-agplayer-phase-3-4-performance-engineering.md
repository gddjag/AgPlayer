# AgPlayer Phase 3-4 性能与工程收尾实施计划

## 参考
- 设计稿：`docs/superpowers/specs/2026-07-24-agplayer-phase-3-4-performance-engineering-design.md`

## 全局约束
- 仅使用现有 FFmpeg、Qt 6.7、vcpkg 依赖，不引入新包。
- C ABI 是 Core 与 Qt Bridge 之间的唯一公开边界。
- `WaveformCache` v1 必须可读取；新缓存写入 v2。
- 所有新代码必须通过 Debug/Release `ctest`。

## 任务分解

### Task 1: WaveformCache v2 格式与 C API 扩展
**目标：** 扩展缓存格式，支持多维度数据；暴露新的 C API。

**文件：**
- 修改：`core/include/agplayer/c_api.h`
- 修改：`core/src/c_api.cpp`
- 修改：`core/src/waveform_cache.hpp`
- 修改：`core/src/waveform_cache.cpp`
- 修改：`tests/core/waveform_cache_test.cpp`

**接口：**
```c
typedef enum ag_waveform_layer { ... } ag_waveform_layer;
size_t ag_waveform_layer_count(const ag_waveform*, ag_waveform_layer);
float ag_waveform_layer_peak(const ag_waveform*, ag_waveform_layer, size_t);
double ag_waveform_bpm(const ag_waveform*);
ag_result ag_track_analysis(const char*, size_t, const ag_cancel_token*,
                            ag_progress_callback, void*, ag_waveform**, double*);
```

**实现要点：**
- 在 `ag_waveform` 内部结构体中新增 `bass_`、`mid_`、`high_`、`bpm_` 字段。
- `WaveformCache::save` 写入 v2 头（88 字节）+ float 数组 + 可选 cue 区。
- `WaveformCache::load` 根据 version 字段分发到 v1/v2 解析。
- v1 加载成功时，只填充 mix peaks，其他层为空。

**验收：**
- `waveform_cache_test` 新增 v2 读写、v1 兼容、损坏检测用例。
- Debug/Release 测试通过。

### Task 2: WaveformItem 渲染优化
**目标：** 实现 viewport 裁剪、按像素降采样、颜色/几何分离更新。

**文件：**
- 修改：`qt/src/waveform_item.cpp`
- 修改：`qt/src/waveform_item.hpp`（如需要）
- 修改：`tests/qt/waveform_item_test.cpp`

**实现要点：**
- 在 `updatePaintNode` 中计算 `maxPoints = width() * devicePixelRatio() * 2`。
- 当 `snapshot->values.size() > maxPoints` 时，按 bucket 取最大值合并。
- 维护 `playedCount`；仅当 playedCount 变化或进入新 bucket 时更新顶点颜色。
- 尺寸/peaks 变化时才 `allocate()` 并重建顶点位置。

**验收：**
- `waveform_item_test` 验证降采样后顶点数正确。
- `waveform_item_test` 验证只有 position 变化时几何体不被重建。
- Debug/Release 测试通过。

### Task 3: 缓存自动清理（CacheJanitor）
**目标：** 实现按 LRU 和大小上限自动清理缓存目录。

**文件：**
- 新增：`qt/src/cache_janitor.hpp`
- 新增：`qt/src/cache_janitor.cpp`
- 修改：`qt/src/settings_controller.cpp`
- 修改：`qt/src/settings_controller.hpp`
- 修改：`tests/qt/settings_controller_test.cpp`

**实现要点：**
- `CacheJanitor::trimToSize(dir, limitBytes)`：
  - 遍历 `.agwf` 文件，读取 last access time（优先）或 last write time。
  - 按时间升序排列，删除最旧的文件直到大小 ≤ limitBytes * 0.9。
- 在 `SettingsController` 中：
  - 启动后 10 秒触发一次清理（使用 `QTimer::singleShot`）。
  - 每次保存缓存文件后触发。
  - `setCacheSizeLimitMB` / `setAutoCleanCache` 变化时触发。
- 新增 `cacheTrimReport` 信号（可选），用于 QML 显示清理结果。

**验收：**
- `settings_controller_test` 验证超出限制时旧缓存被删除、新缓存保留。
- Debug/Release 测试通过。

### Task 4: Qt Bridge 波形数据流（WaveformProvider）
**目标：** 将当前曲目波形接入主界面。

**文件：**
- 新增：`qt/src/waveform_provider.hpp`
- 新增：`qt/src/waveform_provider.cpp`
- 修改：`qt/src/qml_registration.cpp`
- 修改：`app/main.cpp`
- 修改：`app/qml/AgPlayer/components/PlayerPane.qml`
- 修改：`qt/CMakeLists.txt`
- 新增/修改：集成测试

**实现要点：**
- `WaveformProvider` 作为 QML 单例注册。
- `loadForTrack(path)`：
  1. 如果 path 为空，立即发射 `waveformReady(path, {})`。
  2. 用 `WaveformCache::load` 读取缓存（使用 `SettingsController::cacheDirectory`）。
  3. 缓存命中则发射 `waveformReady`。
  4. 未命中则启动 `QtConcurrent::run` 调用 `ag_track_analysis`。
  5. 分析完成后保存缓存并发射 `waveformReady`。
- `cancelForTrack(path)` 取消未完成的 future。
- `PlayerPane.qml`：
  - 监听 `PlaybackController.trackIndex` / `currentTrackId`。
  - 当前曲目变化时调用 `WaveformProvider.loadForTrack(currentTrackValue(LibraryModel.PathRole))`。
  - 监听 `WaveformProvider.waveformReady`，仅当 path 匹配当前曲目时更新 `waveform.peaks`。
  - 绑定 `WaveformProvider.analysisProgress` 到 `waveform.analysisProgress`。

**验收：**
- 打开应用加载曲目后，主界面波形正确显示。
- 切换曲目时，旧任务结果不会覆盖新曲目波形。
- 已有缓存时波形立即显示；无缓存时显示进度。

### Task 5: 10,000 首歌曲压力测试
**目标：** 验证大库场景下的性能与稳定性。

**文件：**
- 新增：`tests/stress/library_stress_test.cpp`
- 修改：`tests/CMakeLists.txt`

**实现要点：**
- 生成 10,000 个短 WAV fixture（1-3 秒，不同采样率/通道）。
- 对每个文件调用 `ag_track_analysis` 并保存缓存。
- 每 100 首采样一次内存（Windows `GetProcessMemoryInfo`）。
- 最后验证：
  - 缓存文件数 = 10,000。
  - 缓存总大小 ≤ limit。
  - 无崩溃、无泄漏（通过 CRT 调试堆或地址清理器）。

**验收：**
- 压力测试在 Debug/Release 下均能在合理时间内完成（建议 CI 超时 10 分钟）。
- 输出 CSV/日志记录性能指标。

### Task 6: 全局回归验证
**目标：** 确保所有变更不破坏现有功能。

**步骤：**
1. `cmake --build build/debug --config Debug`
2. `ctest -C Debug --output-on-failure`
3. `cmake --build build/release --config Release`（使用 VS Dev Shell）
4. `ctest -C Release --output-on-failure`
5. 修复任何失败。
6. 提交所有变更。

## 实施顺序
1 → 2 → 3 → 4 → 5 → 6

Task 1 和 Task 2 可并行；Task 4 依赖 Task 1；Task 5 依赖 Task 1/3/4。

## 测试命令
```powershell
# 开发迭代
cmake --build build/debug --config Debug --target waveform_cache_test
ctest -C Debug -R waveform_cache_test --output-on-failure

# 全量回归
cmake --build build/debug --config Debug
ctest -C Debug --output-on-failure
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -SkipAutomaticLocation
cmake --build build/release --config Release
ctest -C Release --output-on-failure
```
