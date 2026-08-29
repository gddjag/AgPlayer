# Integrated 单窗口主题 Design QA

## 最终比对输入

- 视觉真值：`C:/Users/Administrator/Desktop/音视频播放器/AgPlayer音频播放器完整版/AG单窗口主题.png`
- 当前实现：`build/evidence/integrated-theme-adjustments/integrated-1672x941-v4.png`
- 同画布合成：`build/evidence/integrated-theme-adjustments/comparison-source-left-impl-right-v4.png`（左参考、右实现）
- 局部合成：`comparison-right-panel-v4.png`、`comparison-wave-footer-v4.png`
- 视口与密度：两侧均为 1672×941 逻辑像素、100% DPI；实现状态为深色主题、右栏展开、标签页激活、有效波形和 400–1400ms 循环选区。

## 比对历史

1. 首次实现截图 `integrated-1672x941.png` 发现 P1：右侧页签被内容布局挤到面板中部。
2. 将右栏表头与内容改为明确的上下锚定区域后，`integrated-1672x941-v3.png` 关闭该 P1。
3. 将左右选择 Handle 的可视条收窄为 2px，保留 14px 命中区；重建后以 `integrated-1672x941-v4.png` 完成最终复核。

## 五个主要表面

- 顶栏：品牌、设置和窗口按钮完整，52px 高度与参考层级一致。
- 左栏与歌曲列表：边界稳定；Integrated 左栏按文字需求隐藏“标签管理”，歌曲列表和筛选栏没有裁切。
- 右侧面板：标签管理/歌词双页签、末端收起按钮、默认标签页和居中毛玻璃标签胶囊均可见；折叠后可恢复。
- 波形：列表与波形仅保留 4px 间距；悬停时间、毛玻璃时长/拖出胶囊、2px Handle 和底部可拖导航条均完整。
- 底部播放栏：当前歌曲信息在左、共享播放控件居中、工具/皮肤/侧栏开关在右；Classic 也使用同一皮肤切换按钮。

## 交互与响应式复核

- 框选后自动循环；选区内/外点击均立即播放，选区外点击只关闭循环并保留选区；右键选区清除。
- 波形/频谱切换都能恢复可见数据；Ctrl+滚轮缩放和导航条拖动使用同一可见时间范围。
- 标签/歌词切换、右栏收起恢复、主题切换与播放控制通过 QML 自动测试。
- 1280×720、1440×900 以及 125/150/200% DPI 证据均未见重叠、裁切或跨栏滚动。

## 结论

- P0：0
- P1：0
- P2：0
- P3：测试夹具使用短音频且没有参考图中的封面/元数据，因此列表内容密度和波形颜色不同；这是数据状态差异，不是布局缺陷。

final result: passed
