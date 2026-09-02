# 三频频彩波形实施记录

## 最终契约

- 产品名称继续使用“频彩波形”。
- 单套离线分析同时输出原始振幅 `mix` 与 Low / Mid / High 三路能量；分频点为 250 Hz、4 kHz。
- 默认基色为 Low `#8B3DFF`、Mid `#FFB000`、High `#002FA7`，设置页只暴露这三个颜色。
- 组合颜色在渲染时以线性 RGB 混合并做有限饱和度、明度恢复，不进入缓存键或缓存载荷。
- 波形高度只由原始振幅决定；播放进度只改变 Alpha。
- 全链路只使用一套 `WaveformAnalyzer`、一个 `.agwf` v4 缓存和现有 QSG 顶点色渲染器。

## 已删除或替换

- 删除离线频彩波形的 FFT、Hann 窗、Spectral Centroid、`spectralIndex` C API、缓存字段和 QML 属性。
- 删除未再调用的旧 20 / 180 / 2800 Hz `FrequencyBandSplitter` / Linkwitz-Riley 分频实现，避免并存两套算法。
- 删除 8 色 Palette 设置与插值路径，替换为 Low / Mid / High 三个可配置基色。
- 删除主波形 Provider 的双任务分类、频彩升级任务、资源压力暂停接口及 QML 取消频彩任务接口。
- 列表缩略图与主波形均直接消费同一份 mix/bass/mid/high 缓存数据；改色只重写顶点 RGBA，不改波形几何。
- 旧 `.agwf` 分析结果通过缓存格式/算法版本 4 自动失效，不做旧 Spectral Centroid 数据迁移。

## 验证记录

- MSVC Debug 全量构建通过：`cmake --build build/msvc-debug --config Debug`。
- 15 项核心、缓存、Provider、QSG、设置、QML 与缩略图压力/源码契约测试通过；另有 6 个直接覆盖本次改动的 MainWindow / RollingTheme 用例单独运行通过。
- 播放器真实界面截图：`build/qa/three-band-frequency/player-frequency.png`。
- 设置页真实界面截图：`build/qa/three-band-frequency/settings-frequency-5.png`；确认只显示三个基色输入和未播放透明度，预览能产生自动混色。
- 已知基线限制：完整 `qml_main_window_test` 仍会在约一分钟处失败/超时；完整 `qml_rolling_theme_test` 仍有三个与本次频彩改动无关的既存布局/控件/BPM 失败。本次相关函数均已隔离验证通过。
- 尚未执行实体声卡播放、长时性能测量或发布包验收；本记录不把这些项目标记为已验证。
