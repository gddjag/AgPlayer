# AgPlayer 柔和渐变毛玻璃皮肤设计

日期：2026-08-29
状态：已获产品方向批准，待书面规格确认后进入实施计划

## 1. 目标与已确认根因

本次修正的目标是让“自定义主题皮肤”成为肉眼可辨识的完整皮肤，而不是看起来只改变强调色和高亮色。用户选择推荐皮肤、自定义单色或自定义三色后，窗口背景、卡片、Elevated、Hover、Pressed、边框、Divider、文字、普通图标、强调、高亮、Focus 和播放控制环都由 Theme Engine 自动生成并保持可读。

当前实现并非只更新强调色。`ThemeManager` 已重算完整普通 Palette，主要 QML 页面也在消费动态 Token。实际问题是 Generated Palette 把所有普通表面的 HSL 饱和度统一压到 22%；在深色低明度下，不同 Seed 的主背景常只相差 1–3 个 RGB 值。播放控制环等高视觉权重颜色又保持固定，进一步掩盖了背景色相变化。现有测试只断言 Token 不相等，没有规定最小可感知差异。

颜色选择器过大的直接原因是应用内 `AgColorPicker` 又打开了不可控制尺寸的系统 `ColorDialog`。这与紧凑原生取色体验不符，也造成两套确认流程。

## 2. 产品取舍

### 2.1 保留

- 保留“跟随系统 / 浅色 / 深色”三种外观。
- 保留 Default 皮肤现有中性背景、`#007AFF` 激活色及 `#8F57C9`、alpha 约 0.34 的播放中行。
- 推荐皮肤和自定义皮肤采用整窗柔和渐变底层，并在其上使用同色系半透明玻璃表面。
- 自定义支持“单色”和“三色渐变”。单色只要求用户选择一个颜色，Theme Engine 自动扩展成同色相三段柔和明暗渐变。
- 普通图标、播放控制环、Switch、Slider、Tab、选中态和 Focus 跟随皮肤自动调整。
- 旧版保存的 10 个单色 preset 继续可加载，并以自定义单色状态呈现。

### 2.2 不纳入皮肤派生

- 波形、频谱、音频编辑器选区与游标、Equalizer 曲线、CUE 和用户标签色。
- 收藏红、录音红、错误红、成功绿、警告黄、评分金等领域或状态语义色。

这些颜色承担音频或状态含义，随皮肤任意变色会破坏识别，不属于“普通界面颜色自动适配”。

### 2.3 不实现

- 不实现实时背景模糊、Shader、平台专用材质层或第三方颜色库。
- 不新增线程、轮询、缓存、动画框架或持续 GPU 特效。
- 不把渐变逻辑分散到各 QML 页面，不全局替换媒体或语义色。

## 3. 配置模型与兼容

现有设置继续保留：

- `skinColorMode`：Default / Preset / Custom。
- `skinPreset`：稳定字符串 ID。
- `skinCustomColor`：旧自定义单色，同时作为新模型的第一个 Stop。

新增设置：

- `skinCustomKind`：Solid / Gradient。
- `skinCustomColorMiddle`。
- `skinCustomColorEnd`。

`ThemeManager::Preferences` 调整为：

- `appearanceMode`。
- `skinMode`：Default / Generated。
- `skinKind`：Solid / Gradient。
- `skinStops[3]`。

Solid 输入会在 Theme Engine 内从主色生成三个同色相 Stop；Gradient 输入使用三个用户颜色作为色相身份，再统一柔化。设置事务继续即时预览、保存持久化、取消完整恢复。三个 Stop 必须作为一个配置整体应用，不能出现只更新其中一个字段的中间 Palette。

旧 `skinPreset` ID 保留解析映射。加载旧单色 preset 时，界面进入 Custom + Solid 并显示原色；不会静默替换成新的推荐渐变。缺少、非法、半透明或不完整的颜色安全回退到默认自定义值，不写坏其他设置，也不触碰任何波形或频谱字段。

## 4. 五套推荐渐变

设置页只显示五套新推荐皮肤，顺序和 ID 稳定：

| ID | 名称 | 起点 | 中点 | 终点 |
| --- | --- | --- | --- | --- |
| `aurora` | 极光 | `#73A6FF` | `#A98BFF` | `#F0A8D8` |
| `seaGlass` | 海盐 | `#71D9D0` | `#82C9F4` | `#A7B7FF` |
| `sunset` | 日落 | `#F49BC2` | `#FF9B86` | `#FFC97A` |
| `lavenderMist` | 薰衣草 | `#8295F2` | `#B89BE8` | `#E8B7D5` |
| `morningGlow` | 晨光 | `#8EDFCB` | `#D4E9C2` | `#FFD995` |

表中的颜色是色相身份和选择器预览输入，不直接作为大面积最终背景。Theme Engine 会按 Light/Dark 调整饱和度与明度，并验证最差叠加面的对比度。

## 5. Theme Engine 输出与算法

### 5.1 新增输出

- `backdropStart / backdropMiddle / backdropEnd`：窗口底层渐变 Stop。
- `glassSurface / glassSurfaceElevated / glassSurfaceHover / glassSurfacePressed`：带 alpha 的普通 QML 玻璃表面。
- `glassBorder / glassDivider / glassInnerHighlight`：玻璃层边界。
- 保留现有不透明 `background / surface / surfaceElevated / ...`，作为原生 `QPalette`、独立 Popup/Menu/Tooltip 和不能依赖底层渐变的控件回退色。

Default 模式下，渐变 Stop 全部等于现有背景，玻璃 Token 等于现有不透明表面，因此原有三种外观不会发生视觉漂移。

### 5.2 柔化和层级

- 保留每个输入 Stop 的色相身份。
- 高饱和输入先限幅，再按 Light/Dark 求解目标明度；黑、白、灰保持无色相。
- Solid 模式围绕同一色相生成轻微冷暖与明暗差，不能自动换成其他品牌色。
- Light 使用高明度、低至中等饱和度的渐变；Dark 使用低明度但保持可感知色差的渐变。
- 玻璃层以半透明同色表面、1 px 边界和低 alpha 内侧高光形成轻微毛玻璃感，不模拟真实景深模糊。
- 只在输入实际变化时一次性替换完整 Palette 并发出一次 `paletteChanged`。

### 5.3 对比度

文字与重要 UI 的对比度以玻璃层分别叠加到三个背景 Stop 后的最差值为准：

- 主文字不低于 7:1。
- 次文字和填充按钮文字不低于 4.5:1。
- 三级文字、Focus、重要图标和边界不低于 3:1。
- Disabled 必须可辨识但不得强于正常内容。

按钮前景自动在深色和浅色候选中选择。普通图标复用对应文字或 Accent Token；Generated 皮肤的播放控制环使用皮肤 Accent，Default 保持现有行为。

## 6. QML 结构

新增一个共享 `SkinBackdrop` 组件，放在主播放器、迷你播放器、列表、设置、音频工具和 Equalizer 的最底层。组件只绘制 Theme Engine 输出的简单横向三色渐变，再叠加一个低透明度纵向亮暗层，形成柔和空间感；不使用 Shader。

普通页面继续消费 `Theme.panel / elevated / hoverSurface` 等兼容别名。Generated 模式下这些别名指向玻璃 Token；Default 模式下保持现有不透明 Token。原生 Popup/Menu/Tooltip 使用不透明回退 Palette，避免脱离主窗口后透明到桌面或失去对比度。

QML 不进行 HSL、lighter/darker、颜色混合或对比度计算。静态颜色检查继续禁止普通页面新增未归类颜色和运行时颜色派生。

## 7. 设置页与紧凑 Picker

“主题皮肤”行显示：Default、五张约 `46×30 px` 的渐变色卡、Custom。色卡使用真实三色预览，选中态包括外描边、勾选和 Focus 环，并提供名称与三个颜色值的可访问描述。

Custom 选中后展开第二行：

- `单色 / 三色渐变`分段按钮。
- Solid：一个颜色入口和自动生成后的三色预览。
- Gradient：起点、中点、终点三个独立颜色入口和完整预览。
- 修改立即预览整套皮肤；设置页取消恢复进入页面前的完整配置。

`AgColorPicker` 改为约 `292×248 px` 的应用内 Popup：

- 紧凑饱和度/明度区域。
- 细色相条。
- HEX 与 RGB 精确输入。
- 当前颜色预览。
- 明确的“应用 / 取消”按钮。

移除系统 `ColorDialog`，不再要求用户修改颜色后额外点击候选卡才能保存。Gradient 模式打开 Picker 时明确标识当前编辑的是起点、中点还是终点。Popup 继续限制在 Overlay 内，支持 Esc、外部点击、Tab、Enter 和 Space。

## 8. 验证策略

实施必须按测试先行完成：

1. `theme_manager_test`
   - 五套推荐渐变的 ID、顺序和三个 Stop。
   - Solid 自动同色渐变与 Gradient 三色输入。
   - Light/Dark 的层级、最小可感知差异和最差合成面对比度。
   - 黑/白/灰与高饱和极端输入。
   - Default 精确回归、语义/媒体色不变、一次配置只通知一次。
2. `settings_controller_test`
   - 新字段默认值、合法化、持久化和旧设置兼容。
   - 三 Stop 的 Commit/Cancel/Reset 原子性。
   - 不修改波形、频谱和其他媒体设置。
3. `tst_color_picker.qml`
   - 五张渐变色卡、键盘和可访问名称。
   - Solid/Gradient 切换、三个 Stop 编辑和预览。
   - Picker 尺寸、HEX/RGB、应用/取消、Esc 和 Overlay 边界。
4. 静态与视觉验证
   - 更新 QML 静态颜色契约和四语言翻译。
   - 五套推荐皮肤 × Light/Dark × 主要窗口和工具页面。
   - 自定义单色与三色的代表性和极端场景。
   - 当前运行截图与用户参考图并排检查柔和度、玻璃层级、可读性和 Picker 比例。
5. 交付验证
   - Debug/Release 构建、聚焦测试、全量 CTest、QML lint、`git diff --check` 和最终 Diff Review。
   - 真实 WAV 播放路径下切换皮肤，记录退出、日志、CPU 和 Working Set。
   - 重新扫描其他活动分支，合并尚未覆盖的有效提交。
   - 重新打包 Windows x64 安装器，验证独立目录启动与 SHA-256，并更新桌面 EXE。

## 9. 完成标准与诚实边界

完成必须同时满足：Generated 皮肤肉眼可见地改变整个普通 UI；五套推荐渐变和自定义 Solid/Gradient 均能保存、取消和恢复；Picker 明显小于当前系统对话框；Default 和媒体/语义色回归通过；自动测试、当前运行截图及 Windows 打包证据完整。

截图不能替代读屏器、真实声卡听音或系统主题实时切换验证。没有 macOS/Linux 主机时，只报告共享代码和自动测试覆盖，不宣称跨平台实机通过。
