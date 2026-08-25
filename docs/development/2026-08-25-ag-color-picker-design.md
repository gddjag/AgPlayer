# AgPlayer 公共颜色选择器设计（2026-08-25）

## 目标

用唯一的原生 QML 公共组件 `AgColorPicker.qml` 替换播放器当前的系统 `ColorDialog`。以后只修改该组件即可统一更新播放器内全部颜色选择界面。

本次只改变颜色选择 UI，不改变任何颜色用途、QML 属性、信号、QSettings 键、默认颜色、加载/保存流程、实时预览、设置页事务、恢复默认或主题管理机制。

## 扫描基线

- 当前目标 checkout：`D:\ai\AgPlayer`，分支 `main`，扫描时 HEAD 为 `0218107`。
- 唯一旧实现：`app/qml/AgPlayer/components/ColorField.qml` 内的 `QtQuick.Dialogs.ColorDialog`。
- 唯一调用区域：`app/qml/AgPlayer/SettingsPage.qml` 的外观与波形设置，共 10 个 `ColorField` 实例。
- 未发现其他 `ColorDialog`、`QColorDialog`、ColorPicker、自绘颜色弹窗或可配置主题强调色入口。
- 低/中/高频层颜色是 C++ 编译期常量；强拍/弱拍和标签没有颜色选择入口，不在本次范围内。

## 范围锁定

以下 10 个入口继续使用原有属性和默认值：

| 入口 | 属性 | 默认值 |
| --- | --- | --- |
| 波形底色 | `waveformSolidBaseColor` | `#9098a6` |
| 波形进度色 | `waveformSolidProgressColor` | `#d27722` |
| 波形 RGB 基色 | `waveformRgbBaseColor` | `#00b4a0` |
| 当前普通 RGB 行起始色 | `spectrumRgbStartColor` | `#00d4ff` |
| 当前普通 RGB 行中间色 | `spectrumRgbMiddleColor` | `#7b2ff7` |
| 当前普通 RGB 行结束色 | `spectrumRgbEndColor` | `#e62e9b` |
| 频谱单色 | `spectrumSolidColor` | `#0078d4` |
| 当前频谱 RGB 行起始色 | `waveformRgbStartColor` | `#00d4ff` |
| 当前频谱 RGB 行中间色 | `waveformRgbMiddleColor` | `#7b2ff7` |
| 当前频谱 RGB 行结束色 | `waveformRgbEndColor` | `#e62e9b` |

扫描发现两组 RGB 属性的 UI/渲染使用关系存在既有交叉，局部重置默认也与全局默认存在既有差异。用户明确要求保持各板块当前设置好的默认颜色不变、只改选择器，因此本次不修正、不迁移、不合并这些属性或默认值。

## 文件与职责

### `app/qml/AgPlayer/components/AgColorPicker.qml`

- 唯一颜色选择器 UI 实现。
- 提供 Popup 生命周期、编辑控件、候选色展示、接受/取消语义和现有主题绑定。
- 公开接口：

```qml
property color baseColor: "#63316B"
property color selectedColor: "#63316B"
signal colorAccepted(color color)
function openForColor(initialColor)
```

- `openForColor(initialColor)` 每次打开都用调用方当前值同时初始化 `baseColor` 与 `selectedColor`。
- 修改 HEX、RGB 或 Slider 只改变 `baseColor`，不发出接受信号，也不写设置。
- 点击候选色才设置 `selectedColor`、发出 `colorAccepted(selectedColor)` 并立即关闭。
- `×`、Escape、点击外部关闭均不发接受信号。

### `app/qml/AgPlayer/components/ColorScale.js`

- 只包含无状态纯函数：HEX/RGB 解析与格式化、RGB/HSL 转换、十色生成和相对亮度计算。
- 不读取 Theme、不持有选择状态、不使用 Timer、线程、缓存或磁盘。
- 十色曲线与参考 HTML 一致：5 个浅色、1 个基色、4 个深色。

### `app/qml/AgPlayer/components/ColorField.qml`

- 保留现有 `colorValue`、`targetProperty`、`colorEdited` 接口及动态写回逻辑。
- 移除 `QtQuick.Dialogs` 和内部系统 `ColorDialog`。
- 入口保持当前紧凑尺寸，显示色样和规范化 HEX；点击整个入口打开 `AgColorPicker`。
- `AgColorPicker.colorAccepted` 继续通过 `colorEdited` 写入原目标属性。
- 不触碰 `SettingsPage.qml` 的 10 个现有属性绑定。

### 构建与测试清单

- `app/CMakeLists.txt`：将 `AgColorPicker.qml` 与 `ColorScale.js` 加入现有 `AgPlayer` QML module。
- `tests/qml/tst_color_picker.qml`：组件行为与算法测试。
- `tests/CMakeLists.txt`：复用现有 QML Test 可执行程序注册独立 `qml_color_picker_test`。
- `docs/development/2026-08-25-ag-color-picker.md`：实现完成后记录需求到证据、构建/测试和视觉验收结果。

## 视觉合同

- 原生 `QtQuick.Controls.Popup`，宽度以 360 px 为目标，并限制在可用窗口宽度内。
- 约 16 px 大圆角、1 px 轻边框、柔和阴影，内容边距约 13 px。
- 顶部为 24–26 px 圆形基色色样、HEX 输入和唯一的关闭 `×`。
- RGB 数值位于同一个约 34 px 高的三等分圆角容器。
- 三条 7 px 高 RGB 渐变轨道；手柄使用约 26 × 17 px 的白色双圆胶囊。
- 候选色固定 5 × 2，间距约 5 px，每块约 44 px 高，显示编号与大写 HEX。
- 当前候选色显示清晰双层描边和右上角 `✓`。
- 不显示模式下拉框、“确定”按钮、独立主题按钮或演示状态文字。

Popup Chrome 直接绑定现有 `Theme` 单例：

- 背景：`Theme.elevated` / `Theme.panel`
- 主文字：`Theme.primaryText`
- 次要文字：`Theme.secondaryText`
- 边框和分隔线：`Theme.border`
- Hover：`Theme.hoverSurface`
- 当前主题：`Theme.isLight`

主题切换只更新 Popup 外围 UI；十个候选颜色值由 `baseColor` 决定，不能随主题变化。

## 色阶合同

参考基色 `#63316B` 必须精确生成：

```text
#F8EBFA #E9D2EC #D6B9DB #C09CC6 #A76BB0
#63316B #512C57 #432248 #341938 #251028
```

Slider 渐变使用当前 `baseColor = (r,g,b)`：

- R：`(0,g,b)` 到 `(255,g,b)`
- G：`(r,0,b)` 到 `(r,255,b)`
- B：`(r,g,0)` 到 `(r,g,255)`

HEX 输入兼容可选 `#`、三位和六位格式；显示与发出值统一为大写六位 `#RRGGBB`。非法 HEX 不改变最后有效 `baseColor`，失焦或 Enter 时恢复最后有效值。RGB 输入限制为整数 `0..255`。

## 数据流

```text
ColorField 点击
  -> AgColorPicker.openForColor(ColorField.colorValue)
  -> HEX / RGB / Slider 只修改 baseColor
  -> baseColor 变化时同步控件并重建十色
  -> 用户点击候选色
  -> selectedColor = candidateColor
  -> colorAccepted(selectedColor)
  -> ColorField.colorEdited("#RRGGBB")
  -> SettingsController[targetProperty] = value
  -> 现有 C++ setter、Changed signal、预览、事务和 QSettings 流程
  -> Popup 立即关闭
```

取消路径在 `colorAccepted` 之前终止，因此不会修改设置。设置页自身的保存/取消事务继续按原代码处理已经接受的颜色。

## 性能与平台约束

- 不引入 WebView、WebEngine、HTML/CSS/浏览器组件或第三方 UI 框架。
- 不增加 C++ 音频层改动、FFmpeg 调用、后台线程、Timer、ShaderEffect、逐帧计算或缓存。
- 十色仅在 `baseColor` 有效变化时同步生成；Popup 关闭后没有持续计算。
- 仅使用 Qt 6 / Qt Quick / QML 公共代码，不增加 Windows 专用实现；同一 QML 代码供 Windows 和 macOS 使用。

## 测试与验收

### 自动化

- `#63316B` 十色结果逐项精确匹配。
- 三位/六位 HEX、大小写、可选 `#`、非法值恢复。
- RGB 数字和三个 Slider 与 HEX/baseColor 双向同步并限制在 `0..255`。
- 编辑 baseColor 不改变 selectedColor、不发 `colorAccepted`。
- 点击候选色只发一次准确颜色并立即关闭。
- `×`、Escape、点击外部不发接受信号。
- 每次打开从当前设置颜色重新初始化。
- Popup 打开时切换 `Theme.mode`，Chrome 立即变化而候选数组不变。
- 一个现有颜色设置项验证接受后立即预览、取消不写回；公共入口接入后核对全部 10 个原 targetProperty 未改变。

### 构建与静态检查

- 配置并构建现有 Qt 6 Release 目标和 QML 测试目标。
- 运行 `qml_color_picker_test`、相关 `qml_main_window_test` 和 `settings_controller_test`。
- 对新增/修改 QML 运行项目现有 QML lint。
- 搜索确认生产代码不再存在 `ColorDialog`、`QColorDialog` 或第二套 Picker 实现。
- Review 最终 diff，确认没有颜色属性、默认值、设置键或播放器渲染路径变化。

### 视觉与人工验证

- Windows 实机分别采集浅色和深色 Popup 截图，与用户提供 PNG 对照尺寸、层级、色块、描边、阴影和排版。
- Popup 打开状态切换浅色/深色，候选色值保持不变。
- 验证设置保存后重启读取、设置页取消恢复和恢复默认按钮仍保持当前行为。
- Windows 可提供真实运行证据；当前环境不能将静态共用代码检查冒充 macOS 实机验证，macOS 运行验收需要 macOS 环境。

## 非目标

- 不修正两组 RGB 属性现有交叉绑定。
- 不改变任何默认色或恢复默认行为。
- 不新增低/中/高频、强拍/弱拍、标签或主题强调色设置。
- 不修改波形 SceneGraph、音频解码、播放线程、FFmpeg 或播放器核心逻辑。
- 不删除仍由预览、渲染、设置或测试使用的颜色属性和公共代码。
