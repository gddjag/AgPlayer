# Integrated 单窗口主题 Design QA

final result: passed

## 比对输入

- 参考：`C:/Users/Administrator/Desktop/音视频播放器/AgPlayer音频播放器完整版/AG单窗口主题.png`
- 实现：`docs/qa/evidence/integrated-theme/integrated-1672x941-selection.png`
- 合成对比：`docs/qa/evidence/integrated-theme/reference-vs-actual-1672x941.png`
- 视口：1672×941，100% DPI，Integrated 展开标签面板和有效波形选区。

## 结论

- P0：0
- P1：0
- P2：0
- 主要层级与边界通过：52px 顶栏、左/中/右三栏、全宽时间刻度和大波形、底部当前曲目与播放控制栏。
- 文档优先项通过：Integrated 左栏不显示“标签管理”，右侧标签面板默认展开且可折叠。
- 真实状态通过：歌曲列表、标签、波形、播放状态和选区均来自真实程序数据，不使用参考图里的固定内容。

## P3 后续项

- 测试媒体只有短音频且无封面，因此曲名密度、封面与元数据丰富度不与参考图逐字一致；这不是布局缺陷。
- 参考图的装饰性发光与测试主题 token 略有色差，现实现遵循项目现有 Theme token，避免额外皮肤资产和体积增长。
