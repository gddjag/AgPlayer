# AgPlayer Phase 3-4 性能与工程收尾设计稿

## 1. 背景与目标

Phase 3-4 是 Phase 3（系统级能力、音频工具、精确 BPM）之后的工程收尾阶段，目标是在不引入新外部依赖的前提下：

1. 将波形分析结果（peaks、bass/mid/high、BPM、CUE）接入主播放流程并持久化到缓存。
2. 优化 `WaveformItem` 的 Qt Quick 渲染性能，避免大库场景下的卡顿。
3. 实现缓存目录的自动清理与大小限制。
4. 通过 10,000 首歌曲压力测试验证内存、CPU、磁盘占用达标。

## 2. 范围

### 2.1 包含
- `WaveformCache` 格式从 v1 升级到 v2，支持多维度数据。
- C API 扩展：`ag_waveform` 增加 bass/mid/high 读取接口；新增 `ag_track_analysis` 统一分析接口（可选）。
- Qt Bridge：新增 `WaveformProvider`，在后台线程按需分析当前曲目并读写缓存。
- QML：`PlayerPane.qml` 绑定当前曲目的 peaks 到 `WaveformItem`。
- `WaveformItem` 渲染优化：viewport 裁剪、按像素降采样、颜色/几何分离更新。
- `SettingsController` 自动缓存清理：按 LRU + 大小上限。
- 压力测试：`tests/stress/library_stress_test.cpp` 或 `tools/stress_generator.cpp`。

### 2.2 不包含
- 重新设计波形外观配色（继续使用现有渐变/自定义色，bass/mid/high 分层仅作为缓存数据预留）。
- 真正的 CUE 点编辑 UI（仅持久化 CUE 数据字段）。
- 网络功能或在线更新。

## 3. 技术方案

### 3.1 缓存格式 v2

文件头（小端）：

```
[0..3]   magic          "AGWF"
[4..7]   version        uint32 = 2
[8..15]  source_size    uint64
[16..23] source_mtime   int64
[24..31] flags          uint64  (bit0: has_peaks, bit1: has_bass, bit2: has_mid, bit3: has_high, bit4: has_bpm, bit5: has_cue)
[32..39] peaks_count    uint64
[40..47] bass_count     uint64
[48..55] mid_count      uint64
[56..63] high_count     uint64
[64..71] bpm            double (IEEE-754 little-endian)
[72..79] cue_count      uint64
[80..87] cues_offset    uint64  (从文件头开始的字节偏移)
```

数据区：
- peaks、bass、mid、high 依次存放 float 数组。
- cues 在 `cues_offset` 处，格式为 `uint64 count + (int64 position_ms + uint64 flags + uint32 label_len + label_bytes) * count`。

兼容策略：
- `WaveformCache::load` 先读取 version，v1 按旧格式解析；v2 按新格式解析。
- `WaveformCache::save` 只写 v2。
- 当 v1 缓存命中时，应用仍正常工作，但缺少 bass/mid/high/BPM/CUE；下次分析后自动升级为 v2。

### 3.2 C API 扩展

在 `core/include/agplayer/c_api.h` 中扩展：

```c
/* Waveform channel/layer selectors. */
typedef enum ag_waveform_layer {
    AG_WAVEFORM_LAYER_MIX = 0,
    AG_WAVEFORM_LAYER_BASS = 1,
    AG_WAVEFORM_LAYER_MID = 2,
    AG_WAVEFORM_LAYER_HIGH = 3
} ag_waveform_layer;

/* Read a specific layer from an analyzed waveform. Returns count or 0 if unavailable. */
size_t ag_waveform_layer_count(const ag_waveform* waveform, ag_waveform_layer layer);
float ag_waveform_layer_peak(const ag_waveform* waveform, ag_waveform_layer layer, size_t index);

/* Read BPM from an analyzed waveform. Returns 0.0 if unavailable. */
double ag_waveform_bpm(const ag_waveform* waveform);

/* Optional unified analysis that also produces BPM and layers. */
ag_result ag_track_analysis(const char* utf8_path,
                            size_t target_points,
                            const ag_cancel_token* cancel_token,
                            ag_progress_callback progress_callback,
                            void* user_data,
                            ag_waveform** out_waveform,
                            double* out_bpm);
```

### 3.3 Qt Bridge 数据流

新增 `qt/src/waveform_provider.{hpp,cpp}`：

```cpp
class WaveformProvider : public QObject {
    Q_OBJECT
public:
    explicit WaveformProvider(QObject* parent = nullptr);

    /* Load waveform for the given path. Returns cached data or launches async analysis. */
    Q_INVOKABLE void loadForTrack(const QString& path);

    /* Cancel any pending analysis for path. */
    Q_INVOKABLE void cancelForTrack(const QString& path);

signals:
    void waveformReady(const QString& path, const QVariantList& peaks);
    void analysisProgress(const QString& path, double progress);

private:
    struct Job {
        QString path;
        QFutureWatcher<AnalysisResult>* watcher;
    };
    QHash<QString, Job> jobs_;
};
```

工作流程：
1. `PlaybackController::trackIndexChanged` 触发时，`PlayerPane` 调用 `WaveformProvider.loadForTrack(currentPath)`。
2. `WaveformProvider` 先尝试 `WaveformCache::load`。
3. 缓存未命中则在 `QtConcurrent::run` 中调用 `ag_track_analysis`。
4. 分析完成后保存到缓存，并发射 `waveformReady`。
5. `PlayerPane` 仅在 `waveformReady.path == currentPath` 时更新 `WaveformItem.peaks`，避免旧任务结果覆盖新曲目。

### 3.4 波形渲染优化

#### 3.4.1 Viewport 裁剪
在 `WaveformItem::updatePaintNode` 中：
- 计算可见时间范围 `[viewStartMs, viewEndMs]`。当前视图即整首曲目（`0..duration`），但未来可扩展为局部缩放。
- 对于当前全览模式，可见范围即全部；裁剪主要按像素密度降采样。

#### 3.4.2 按像素降采样
- 目标：peak 数量不超过 `width() * devicePixelRatio() * 2`（Nyquist 两倍）。
- 当 `peaks.size() > maxPoints` 时，对每 `N = peaks.size() / maxPoints` 个峰值取最大绝对值合并。
- 降采样后的顶点数与屏幕像素同阶，避免 GPU 过度绘制。

#### 3.4.3 颜色/几何分离
- 将 `WaveformNode` 的顶点拆分为 played/unplayed 两段：
  - `playedCount = floor(peakCount * playedFraction)`。
  - 顶点数组前 `playedCount * 2` 个顶点使用 alpha=255，其余使用 alpha=89。
- 当只有 `position_` 变化时：
  - 如果新旧 `playedCount` 相同，不做任何更新。
  - 如果变化，仅更新受影响的顶点颜色（或重新着色两个边界 bucket），不重建 geometry。
- 当 `peaks`、尺寸变化时才重建 geometry。

### 3.5 缓存自动清理

在 `SettingsController` 中实现 `enforceCacheSizeLimit()`：

```cpp
void SettingsController::enforceCacheSizeLimit() {
    if (!autoCleanCache_ || cacheSizeLimitMB_ <= 0) return;
    const qint64 limitBytes = static_cast<qint64>(cacheSizeLimitMB_) * 1024 * 1024;
    CacheJanitor::trimToSize(cacheDir, limitBytes);
    recalculateCacheSize();
}
```

`CacheJanitor`（放在 `qt/src/cache_janitor.{hpp,cpp}` 或 core 层）：
1. 遍历缓存目录，收集所有 `.agwf` 文件的 `(path, last_access_time, size)`。
2. 按最后访问时间升序排列。
3. 当总大小 > limit 时，从 oldest 开始删除，直到大小 ≤ limit * 0.9（保留 10% 余量）。
4. 记录删除数量与释放字节数。

触发时机：
- 应用启动后延迟 10 秒执行一次。
- 每次保存新缓存文件后执行。
- 用户修改 `cacheSizeLimitMB` 或启用 `autoCleanCache` 后立即执行。

### 3.6 压力测试

新增 `tests/stress/library_stress_test.cpp`（或作为独立可执行文件）：

1. 创建临时目录，生成 10,000 个不同长度/采样率的 WAV 文件（使用 fixture generator 逻辑）。
2. 模拟导入流程：逐个生成路径、调用 `ag_track_analysis`、写入缓存。
3. 测量：
   - 总分析时间。
   - 峰值内存（通过 Windows working set 采样）。
   - 缓存目录总大小。
   - 打开应用、滚动库、切换曲目的响应延迟。
4. 断言：
   - 无崩溃、无内存异常增长。
   - 缓存大小不超过设置上限。
   - 单首分析平均时间 < 2 倍正常单文件分析时间。

由于 10,000 真实 WAV 文件体积过大，测试可采用：
- 生成短音频（1-5 秒）。
- 或复用同一 fixture 文件但复制到不同路径（缓存键不同）。

## 4. 性能指标

| 场景 | 目标 |
|------|------|
| 10,000 首库导入 | 完成时间不高于单首平均 × 12,000（允许冷启动开销） |
| 缓存磁盘占用 | 不超过 `cacheSizeLimitMB` 设置值 |
| 主界面波形更新 | position 变化时 UI 帧率不下降，CPU 增量 < 1% |
| 峰值内存 | 分析 10,000 首过程中不出现 OOM，工作集增长与缓存大小正相关 |
| 渲染 1920px 宽波形 | peaks 数 > 8,000 时仍能稳定 60 FPS |

## 5. 测试策略

- 单元测试：`waveform_cache_test` 覆盖 v1/v2 读写、兼容、损坏检测；`settings_controller_test` 覆盖清理触发；`waveform_item_test` 覆盖降采样与颜色更新。
- 集成测试：`import_controller_test` 验证分析结果进入 LibraryModel/WaveformProvider。
- 压力测试：`library_stress_test` 验证 10,000 首场景。
- 回归测试：Debug/Release 全量 `ctest`。

## 6. 文件变更清单

### 新增
- `core/src/waveform_layers.{hpp,cpp}`（bass/mid/high 分频分析，可选）
- `qt/src/waveform_provider.{hpp,cpp}`
- `qt/src/cache_janitor.{hpp,cpp}`
- `tests/core/waveform_cache_v2_test.cpp` 或扩展现有测试
- `tests/stress/library_stress_test.cpp`

### 修改
- `core/include/agplayer/c_api.h`
- `core/src/c_api.cpp`
- `core/src/waveform_cache.{hpp,cpp}`
- `core/src/waveform_analyzer.{hpp,cpp}`（可选，增加分层）
- `qt/src/waveform_item.{hpp,cpp}`
- `qt/src/settings_controller.{hpp,cpp}`
- `qt/src/qml_registration.cpp`
- `app/qml/AgPlayer/components/PlayerPane.qml`
- `app/main.cpp`（注册 WaveformProvider 单例）
- `tests/CMakeLists.txt`
- `qt/CMakeLists.txt`

## 7. 风险与回退

| 风险 | 缓解 |
|------|------|
| v2 缓存格式导致旧缓存失效 | v1 读取兼容，v2 保存后自动升级 |
| 后台分析阻塞 UI | 使用 `QtConcurrent::run` + `QFutureWatcher` |
| 大库压力测试耗时过长 | 使用短音频 fixture，设置合理超时 |
| 分层分析增加 CPU 开销 | 默认仍只生成 mix peaks，bass/mid/high 按需/后台生成 |
