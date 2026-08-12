# AgPlayer 波形时间轴精准同步系统升级设计

## 目标与硬门禁

本设计逐条承接 `AgPlayer_波形时间轴精准同步系统升级需求.md`。完成定义不是“能够编译”，而是：统一坐标映射、自动精度测试、GPU 场景图验证、resize 不重分析、真实 DPI/多屏/播放验收全部有证据。任何未验证项必须在开发记录中标记“待验收”，不得报告为完成。

## 架构

新增无状态纯 C++ `WaveformCoordinateMapper`，成为时间、采样、Peak、逻辑像素之间的唯一转换中心。`WaveformItem` 从 `WaveformNode` 实际使用的逻辑绘制宽度更新 `renderWidth`；播放游标、鼠标 Seek、hover、已播放颜色分界和顶点 X 坐标全部调用 mapper，不再各自计算比例。

数据流固定为：播放时间 → PCM sample → peak index/continuous peak coordinate → GPU vertex logical pixel。逻辑像素交给 Qt Scene Graph/RHI 变换到物理像素，DPI 只参与顶点预算，不重复乘入时间坐标，避免双重缩放。

## 接口

`WaveformCoordinateMapper` 提供：

- `timeToPixel(positionMs, durationMs, renderWidth)`：边界夹取后返回连续逻辑像素。
- `pixelToTime(x, renderWidth, durationMs)`：与上式互逆并使用确定性取整。
- `timeToSample(positionMs, durationMs, totalSamples)`。
- `sampleToPeak(sampleIndex, totalSamples, peakCount)`。
- `timeToPeak(...)`：组合映射，供顶点和已播放分界共用。

`WaveformItem` 新增只读 `renderWidth`、`waveformCursorX` 与变更信号；QML 只显示 `waveformCursorX` 并把点击交给 `timeForX()`。`PeakSnapshot` 保存 `totalSamples`、`sampleRate`、`peakCount` 和不可变 peaks/layers；旧调用没有元数据时，以 duration 与 peakCount 构造等价的兼容时间轴。

## 渲染与性能

保留当前 `QQuickItem::updatePaintNode()`、`QSGGeometry` 和缓存体系。音频或视觉设置变化才重采样/分配顶点；普通播放推进只更新游标和颜色边界；resize 只根据同一不可变 PeakSnapshot 重新映射 GPU 顶点，不调用 WaveformProvider、WaveformCache 或分析器。

## 测试与验收

- `WaveformCoordinateMapperTest` 覆盖需求指定的 4 个 position × 3 个 width、端点、非法输入、正反映射、采样/Peak 链，误差 ≤ 0.5 logical pixel。
- `WaveformItemTest` 验证 cursor、Seek、played boundary、顶点 X 使用同一实际 renderWidth；resize 保留 snapshot revision 且不触发输入更新。
- QML 测试禁止再次出现 `position / duration * width`。
- 自动化覆盖 800、1920、3840 宽度和 DPR 1.0/1.25/1.5 的数学精度。
- 真实 QA 覆盖 800×500、1920×1080、3840×2160、多显示器、100%/125%/150% DPI、连续播放/点击/拖动/resize，并记录 CPU 对照。竞品“超越”只在相同媒体和测量方法的对比结果成立后签署。

## 需求追溯

需求第 1-8、10-11 项由 mapper、WaveformItem/Node 和自动测试直接实现；第 9、12 项分为自动数学/渲染门禁与真实设备 QA。保持 Qt6/QML + C++17 兼容语法，不更换 FFmpeg、WaveformCache 或 PeakSnapshot 架构。
