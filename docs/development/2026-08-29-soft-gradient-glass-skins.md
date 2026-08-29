# 柔和渐变毛玻璃皮肤：实现与需求追踪（2026-08-29）

## 结论与基线

本轮把原先视觉上接近“只改强调色”的单色皮肤，改为由 C++ Theme Engine 统一生成的整窗柔和渐变皮肤。用户可以选择 Default、五套推荐渐变、自定义单色或自定义三色；背景、卡片、Elevated、Hover、Pressed、边界、文字、普通图标、Accent、Highlight、Focus 和播放控制环随同一配置自动适配。

- 规格：`docs/superpowers/specs/2026-08-29-soft-gradient-glass-skins-design.md`。
- 实施计划：`docs/superpowers/plans/2026-08-29-soft-gradient-glass-skins.md`。
- 本记录基线：`codex/full-custom-theme-engine@0e6b93f`，即跨分支集成与最终打包前的 Task 8 功能/视觉基线。
- Windows 验收证据：`build/qa/2026-08-29-soft-gradient-glass-skins/`。
- 完整命令、失败边界和视觉证据见 `docs/qa/2026-08-29-soft-gradient-glass-skins-acceptance.md`。

## 架构

### C++ 是唯一颜色计算源

`ThemeManager` 以一份完整配置原子生成 `ThemePalette`：

```text
appearanceMode + skinMode + skinKind + skinStops[3]
                    |
                    v
          ThemeManager::calculatePalette
                    |
      +-------------+------------------+
      |                                |
不透明 Qt/QML 回退 Palette       三段背景与半透明玻璃 Token
      |                                |
原生控件/Popup/Menu/Tooltip       SkinBackdrop 与普通 QML 页面
```

普通主题 QML 不计算 HSL、HSV、对比度或派生色；Picker 内部仍使用 Qt HSV API 表达用户正在编辑的输入。`Theme.qml` 只代理 C++ Token 并保留旧别名；`SkinBackdrop.qml` 只绘制三段横向渐变和低透明度纵向亮暗层。这样所有窗口共用同一主题算法，Detached Popup/Menu/Tooltip 仍可使用不透明 `QPalette`，不会透明到桌面。

### 完整 Palette 与柔化策略

Generated 模式输出以下两组互补 Token：

- `backdropStart / backdropMiddle / backdropEnd`：整窗三段背景。
- `glassSurface / glassSurfaceElevated / glassSurfaceHover / glassSurfacePressed`：半透明同色表面。
- `glassBorder / glassDivider / glassInnerHighlight`：玻璃边界与低 alpha 内侧高光。
- `background / surface / surfaceElevated / surfaceHover / surfacePressed`：合成后的不透明回退色。
- 四级文字、Disabled、Accent、Highlight、Focus、重要边界、普通图标和 `currentTrackSurface`：按最差玻璃合成面求解。

输入 Stop 先规范为不透明大写 `#RRGGBB`，再保留色相身份并限制饱和度/明度。黑、白、灰保持无色相；高饱和输入会被柔化。Solid 只要求一个颜色，Engine 自动扩展为同色相三段明暗变化；Gradient 使用三个输入作为色相身份，再统一柔化。完整临时 Palette 与当前值不同时才整体替换并发出一次 `paletteChanged`。

数值合同由 C++ 表驱动测试验证：最差玻璃合成面上的主文字 `>= 7:1`，次文字与按钮文字 `>= 4.5:1`，三级文字、Focus、重要图标和边界 `>= 3:1`。这些是算法回归阈值，不等同于完整 WCAG 或辅助技术认证。

### Default 与 Generated 的边界

Default 不启用彩色渐变：三个背景 Stop 等于现有背景，玻璃 Token 等于现有不透明表面，因此 Light/Dark/System 的旧中性层级不漂移。

- Default 激活色固定为 `#007AFF`。
- Default 播放中行固定为 `QColor(143, 87, 201, 87)`，即 `rgba(#8F57C9, 0.34)`。
- Generated 的播放中表面随皮肤柔化适配；它不使用 Default 的固定紫色。
- 播放中指示条、波形和其他媒体色继续使用独立 Token，不随皮肤变化。

### 五套稳定推荐渐变

| 稳定 ID | 名称 | 起点 | 中点 | 终点 |
| --- | --- | --- | --- | --- |
| `aurora` | 极光 | `#73A6FF` | `#A98BFF` | `#F0A8D8` |
| `seaGlass` | 海盐 | `#71D9D0` | `#82C9F4` | `#A7B7FF` |
| `sunset` | 日落 | `#F49BC2` | `#FF9B86` | `#FFC97A` |
| `lavenderMist` | 薰衣草 | `#8295F2` | `#B89BE8` | `#E8B7D5` |
| `morningGlow` | 晨光 | `#8EDFCB` | `#D4E9C2` | `#FFD995` |

这些值是选择器预览和色相身份，不是未经处理的大面积背景色。旧十个单色 preset ID 只用于兼容解析，不重新显示为推荐项；旧值加载为 Custom + Solid，不被静默替换。

### 设置事务和兼容

`SettingsController` 继续使用现有 QSettings 与设置页编辑事务，持久化：

- `skinColorMode`：`0=Default / 1=Preset / 2=Custom`。
- `skinPreset`：稳定字符串 ID。
- `skinCustomKind`：`0=Solid / 1=Gradient`。
- `skinCustomColor`、`skinCustomColorMiddle`、`skinCustomColorEnd`。

选择 Default、推荐项或 Custom 配置时，三个 Stop 作为一次完整配置通知到 `ThemeSettingsSynchronizer`。预览即时生效；保存持久化；取消恢复进入设置页时的完整配置；恢复默认不会读写波形或频谱设置。非法、半透明或不完整输入整体回退到安全默认，不留下单个 Stop 的中间 Palette。

### 设置 UI 与 Picker

`ThemeColorSelector` 只显示 Default、五张真实三色渐变卡和 Custom。Custom 支持 Solid/Gradient；Gradient 分别编辑起点、中点、终点。色卡以描边、勾选和 Focus 环辅助选中表达，颜色不是唯一提示。

`AgColorPicker` 是约 `292 x 248 px` 的应用内 Popup，提供饱和度/明度区、色相条、HEX/RGB、当前色、应用和取消。它不再打开不可控尺寸的系统 `ColorDialog`；Apply 才提交当前 Stop，Cancel、Esc 或外部关闭不写入。键盘和可访问名称由 QML 测试覆盖，真实读屏器仍不在本轮实机范围。

### 窗口接入与静态约束

主播放器、迷你播放器、列表、设置、音频工具、Equalizer 和 Docked Window Frame 都在内容底层接入共享 `SkinBackdrop`，普通控件继续消费 `Theme` Token。QA 参数可以原子指定推荐皮肤、自定义 Solid、三色 Gradient 和 Picker 的 Start/Middle/End 打开态。

静态检查继续禁止普通 QML 新增未归类 Hex、`Qt.lighter/darker` 或运行时颜色计算。实现没有新增第三方颜色库、依赖、线程、轮询、缓存、Shader、真实背景模糊、平台材质层或动画框架；“毛玻璃”来自透明同色表面、边界和内侧高光，不宣称真实光学模糊。

## 不参与皮肤派生的颜色

以下色彩承担媒体或领域语义，必须保持独立：

- 主播放器、迷你播放器、列表缩略波形、频谱和 RGB 三频。
- 音频编辑器波形、选区、Marker、Beat Grid、播放游标。
- Equalizer 曲线、CUE、评分、文件格式徽标和用户标签色。
- 收藏、录音、错误、危险、成功、警告等语义色。

## 需求到证据追踪

| 已批准需求 | 实现位置/合同 | 自动或运行时证据 | 结论 |
| --- | --- | --- | --- |
| 不是只改 Accent/Highlight，而是整窗普通 UI 自动适配 | `ThemeManager` 完整 Palette、`Theme.qml` Token、共享 `SkinBackdrop` | `theme_manager_test`；五套推荐与自定义黑/白/RGB 视觉矩阵 | 通过。Generated 的背景、玻璃层、文字、普通图标和状态一起变化。 |
| 用户可选择一个主色并自动得到同色渐变 | `SkinKind::Solid` 与 Solid Stop 扩展算法 | 黑/白等极端输入表驱动测试；Custom Solid 截图 | 通过；无色相输入保持中性。 |
| 用户也可选择三色渐变 | `SkinKind::Gradient`、`skinStops[3]` | Settings 事务测试、Picker Start/Middle/End 和 RGB 矩阵 | 通过；三个 Stop 原子应用。 |
| 五套不同、柔和的推荐渐变 | `ThemeManager::presets()` 单一数据源 | 五 preset x Light/Dark x 10 surfaces，共 100 张初始矩阵图；Product Design 并排复核 | 通过。ID、顺序和 Stop 由单元测试锁定。 |
| Picker 明显缩小并留在应用内 | `AgColorPicker.qml` | `tst_color_picker.qml`；3 张 Start/Middle/End 打开态 | 通过；系统 `ColorDialog` 路径已移除。 |
| Default 三种外观不受新皮肤影响 | Default 精确 Palette 回归 | `theme_manager_test`；Default Light/Dark/System 矩阵 | 通过。蓝色激活与固定紫色播放中行单独锁定。 |
| 媒体/语义色不随皮肤变化 | 独立 Theme Token 与静态白名单 | Theme/QML 合同测试；播放中行像素和 WAV 切换证据 | 通过当前合同；未做声卡听觉验收。 |
| 设置可预览、保存、取消、恢复，并兼容旧单色 | `SettingsController` 事务、旧 ID 解析 | `settings_controller_test`、`tst_color_picker.qml` | 通过。旧 Accent/Highlight 磁盘键不迁移为皮肤。 |
| 中/英/泰/越文案与布局 | 四套 TS/QM、长标签布局修复 | 翻译测试；9 张设置图；18 张最终 SideNavigation 图 | 通过最终目检；不外推为读屏器认证。 |
| System 实时适配 | `QStyleHints`/系统色路径与启动前 Palette | 单元测试；Windows Light/Dark 广播和同进程切换 | Windows 实时切换通过。 |
| 冷启动稳定状态与首帧 | 启动前 Palette 路径 | Windows Light/Dark 冷启动 ready 后稳定截图 | 稳定状态有证据；首帧无跳色未建立，未做 first-paint 仪器测量。 |

## 主题专用提交序列

| 范围 | 提交 |
| --- | --- |
| 配置与五套推荐合同 | `9b904d6` |
| 柔和渐变、玻璃 Palette 与对比度 | `31754a2`、`52266ce` |
| 原子设置持久化与兼容 | `b0fe29f`、`926a469`、`9386e8a` |
| 紧凑 Picker | `850c293`、`801dd4a` |
| 皮肤选择器和设置页 | `7f6761d`、`2e2ed38` |
| 共享窗口背景与窗口接入 | `73b97e5`、`a7afb5f`、`93eb321` |
| 静态合同、自动 QA 与修正 | `3db58c7`、`4eb3697`、`03453f3`、`9fc4a36`、`49d2164` |
| 真实 Details 与多语言视觉修正 | `4daf3b9`、`6aaa988`、`0e6b93f` |

## 当前边界与下一步

Task 8 的主题功能和 Product Design 视觉审查已通过，但本记录不是最终 Windows 发布记录。跨分支有效提交扫描/集成、集成后全量回归、独立目录 smoke 和版本化安装器仍属于后续交付阶段。

Windows 是唯一实机平台。macOS/Linux 没有对应构建、视觉、系统主题或播放实机证据；只能说明共享 Qt 代码路径和自动测试设计。性能采样也没有等时长 A/B，不能据此断言无泄漏或量化 Theme Engine 开销。
