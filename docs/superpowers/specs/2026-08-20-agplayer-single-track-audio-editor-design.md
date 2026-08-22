# AgPlayer《音频编辑》单轨 AudioEvent 编辑器设计规格

**日期：** 2026-08-20

**状态：** 架构已由用户确认，等待书面规格复核

**目标工程：** `D:/ai/AgPlayer`，Qt 6 / QML / C++17 桌面应用

**视觉真源：** `C:/Users/Administrator/Desktop/音视频播放器/AgPlayer音频播放器完整版/音频编辑.png`（1672×941）

**行为真源：** `C:/Users/Administrator/Desktop/AgPlayer 单轨音频编辑器开发提示词.md`

## 1. 文档优先级与替代关系

1. 用户 2026-08-20 的直接要求最高：严格按照新 UI 和新要求文档开发，保持轻量，并固定音频工具顺序。
2. 行为、数据、线程、性能和验收以最新要求文档为准。
3. 布局、比例、颜色、文字层级和控件顺序以 1672×941 参考图为准。
4. 当参考图与要求文档冲突时，行为正确性优先；视觉尽量保持，禁止复制参考图中的错误数据或假功能。
5. 本规格取代 `2026-08-12-agplayer-audio-editor-v2-design.md` 中与最新要求冲突的内容。旧规格仅保留历史记录，不再指导实现。
6. 每个必须项都要关联可复查证据；构建成功、局部测试或静态截图不能单独代表整项验收通过。

## 2. 产品边界

这是原生、单轨、非破坏性的 AudioEvent 编辑器，不是 DAW。

必须支持：

- 一条时间线轨道上的多个音频片段。
- 导入、移动、修剪、分割、删除、复制、剪切、粘贴、合并相邻片段和撤销/重做。
- 播放头、Sample 精确选区、缩放、横向平移、增益、轻量包络、淡入淡出和片段静音。
- 原生录音、BPM/速度、独立音高、变速不变调、实时预览、正式导出。
- 选区超过系统拖动阈值后，按需生成 WAV 并以标准本地文件 URI 拖出。

明确不支持：

- 多轨、Bus、路由、混音器、多轨录音、复杂交叉淡化。
- Beat Grid、小节网格、任何节拍/磁铁吸附、Loop 编曲。
- MIDI、VST/AU、插件自动化轨道、调式检测、复杂频谱/母带和 AI 音频处理。
- NLE 专属按钮、进程检测、窗口操控、OCR、Accessibility 自动控制或常驻 Bridge。
- Electron、WebView、Python、外部 `ffmpeg.exe`、QProcess 音频处理或第二套音频核心。

## 3. 最高约束

### 3.1 轻量与依赖

- 不新增第三方运行库、模型、Web Runtime、数据库、DAW SDK 或长期后台服务。
- 只复用现有 Qt 6、FFmpeg libav*、SoundTouch、miniaudio/原生后端、主题、设置和任务系统。
- Release 体积只允许由本项目功能代码产生自然增量；新增二进制依赖即视为设计失败。

### 3.2 生命周期

编辑器必须按需创建和释放：

- 主播放器运行且编辑器未打开时，不实例化编辑文档、波形视口、录音输入、BPM 任务、编辑播放定时器或 Handoff 管理器。
- 离开《音频编辑》页面或关闭音频工具窗口时，停止编辑播放和按需任务，释放录音设备、可见区 Geometry、处理器和非必要缓存。
- 不后台扫描音频，不扫描 NLE，不预生成 Handoff，不让大块 PCM 常驻。
- 主播放器启动、切歌、CPU、内存和实时输出不得因编辑器未打开而发生可测的持续增量。

### 3.3 时间与数据精度

- 核心时间坐标统一使用有符号 64 位 `SampleFrame`。
- 所有区间使用半开区间 `[start, end)`。
- UI 时间由 Sample 位置和采样率换算，不以波形像素估算。
- QML 只传递输入意图与显示绑定，编辑算法、边界规则和命令执行全部位于 C++。

## 4. 当前实现与必须纠正的偏差

现有代码提供可复用基础，但不能只换 UI：

- `AudioDocument` 当前使用连续 `AudioSpan`，没有独立 `timelineStart`，无法表达删除后保留空隙或自由移动片段。
- Undo/Redo 当前保存完整 `DocumentSnapshot`，违反轻量命令栈要求。
- 删除/裁剪当前以连续 Span 重排时间线，行为接近 Ripple，和新合同冲突。
- 编辑预览当前可能先生成完整临时 WAV，再由编辑器持有的独立 `ag_player` 播放，违反实时区间处理和复用现有输出链路要求。
- `editor_timeline_math` 中存在 Beat Grid 与 Snap 逻辑，最新合同明确禁止。
- 当前 UI 的缩略导航、两组右栏、循环/标记等结构与 1672×941 新参考图不一致。

可继续复用：

- FFmpeg Decoder/Transcoder、SoundTouch 引擎接口、现有 AudioEngine 输出与 PlaybackController。
- Peak 数据、`PeakPyramid`、`AudioEditorWaveformItem`、QQuickItem/QSGGeometryNode 渲染。
- `RecordingSession` 的固定 RingBuffer、WriterThread、临时 WAV 和恢复能力。
- `DocumentWriter` 的解码、编码、取消、验证、同目录 staging 与原子提交能力。
- `EditorViewport` 的帧/像素映射思想、QtConcurrent/QFutureWatcher、设置和 AppData 路径规则。

## 5. 核心域模型

### 5.1 `AudioEvent`

`AudioEvent` 是原始音频的非破坏性引用，不拥有 PCM：

```cpp
using EventId = std::uint64_t;
using SampleFrame = std::int64_t;

enum class FadeCurve { Linear, Smooth, Exponential };

struct EnvelopePoint final {
    SampleFrame offset{};
    float gain{1.0F};
};

struct AudioEvent final {
    EventId id{};
    std::shared_ptr<const AudioSource> source;
    SampleFrame sourceStart{};
    SampleFrame sourceEnd{};
    SampleFrame timelineStart{};
    float gain{1.0F};
    SampleFrame fadeIn{};
    SampleFrame fadeOut{};
    FadeCurve fadeInCurve{FadeCurve::Smooth};
    FadeCurve fadeOutCurve{FadeCurve::Smooth};
    double speedRatio{1.0};
    int pitchSemitone{};
    bool preservePitch{true};
    bool mute{};
    std::vector<EnvelopePoint> envelope;
};
```

约束：

- `sourceStart < sourceEnd`，且范围不能越过不可变源边界。
- `timelineStart >= 0`；首版不允许 AudioEvent 发生复杂重叠混音。
- `fadeIn + fadeOut` 不得超过事件听感时长。
- Envelope 点按事件内 Sample 偏移排序，数量设合理上限，防止误操作造成无限增长。
- Split、Copy 和 Undo 只复制小型元数据；多个 Event 可共享同一 `AudioSource`。

### 5.2 `EventTimeline`

`EventTimeline` 只管理一条轨道：

- 保存按 `timelineStart` 排序的 `AudioEvent`。
- 提供 Sample 精确的事件命中、空白区命中、区间查询、帧/像素映射输入和总时长。
- Move 只改 `timelineStart`，Trim 只改 `sourceStart/sourceEnd`。
- Delete 删除选中 Event 并保留时间线空隙，不启用 Ripple。
- Paste 在播放头放置克隆的 Event 元数据；发生冲突时采用可预期的拒绝/最近空位规则，不能隐式混音。
- 相邻片段只有在源相同、源区间连续、时间线连续、速度和音高一致时才能合并。

### 5.3 选择与播放头

- `TimelineSelection` 独立于选中的 `EventId`，使用 Sample 区间。
- 播放头也使用 SampleFrame；QML 拖动时只更新视觉候选值，松开后执行一次最终 Seek，必要时使用节流 Seek。
- 有效选区右上角显示真实时长，格式为 `MM:SS.mmm`，超过一小时使用 `HH:MM:SS.mmm`。

## 6. 轻量命令与撤销

不再保存完整时间线快照。每条命令只记录自身最小前后状态：

- `MoveEventCommand`
- `TrimEventCommand`
- `SplitEventCommand`
- `DeleteEventCommand`
- `CopyEventCommand` / `CutEventCommand` / `PasteEventCommand`
- `MergeEventsCommand`
- `GainCommand`
- `EnvelopeCommand`
- `FadeCommand`
- `MuteCommand`
- `SpeedCommand`
- `PitchCommand`

命令合同：

- `execute()` 和 `undo()` 必须是事务式操作；失败不改变 Timeline。
- Clipboard 只持有 AudioEvent 元数据，不持有 PCM 或源文件副本。
- Undo 栈设可配置的轻量条数上限；合并连续鼠标拖动为一个最终命令，不能每个像素压入一条命令。
- 每次成功修改使单调递增的 `editorRevision` 增加，用于播放/波形/Handoff 失效判断。

## 7. 时间线、波形与交互

### 7.1 统一映射

`EditorViewport` 负责：

- `timeToPixel`、`pixelToTime`、可见 Sample 范围和滚动边界。
- Ctrl+滚轮以鼠标位置为锚点缩放。
- Shift+滚轮和中键拖动只改变可见范围。
- 播放头、AudioEvent、选择框、Fade、Envelope 和标尺共享同一映射。

缩放和平移不得触发完整文件重解码、全曲波形分析或大型数组生成。

### 7.2 波形

- 继续使用 Peak 数据与 Scene Graph GPU Geometry。
- 只请求当前可见范围所需的分辨率；`PeakPyramid` 返回的点数受像素宽度约束。
- AudioEvent 只是引用同一 Peak 来源并使用 source/timeline 映射，不为 Split/Copy 生成新 Peak 或 PCM。
- 删除后空隙绘制为空白，不能把后续波形自动左移。

### 7.3 鼠标工具

- 默认选择工具，`Ctrl+1`。
- 剪刀工具，`Ctrl+2`；点击 Event 立即分割，行为固定为保持剪刀模式，Esc 返回选择模式。
- 选择工具支持 Move、Ctrl+拖动复制、左右边缘 Trim、0 dB 线 Gain、淡入/淡出控制点和 Envelope 点。
- 空白波形区拖动创建范围；边缘可再次调整。

## 8. 编辑播放链

### 8.1 唯一输出链

新增轻量 `EditorPlaybackAdapter`，接入现有 AudioEngine/PlaybackController 的输出设备与实时线程，不创建第二个播放器核心。

```text
EventTimeline
  → 找到当前 Sample 覆盖的 AudioEvent
  → 现有 FFmpeg 流式 Decoder
  → Trim/空隙/Mute
  → 现有 SoundTouch Tempo/Pitch（需要时）
  → Gain/Envelope/Fade
  → 现有 AudioEngine 输出
```

- 参数预览不生成整曲临时 WAV。
- Decoder、SoundTouch 和短缓冲按当前 Event 复用；Event 切换或 Seek 时进行受控 reset。
- 实时回调中禁止磁盘扫描、QML/UI、日志刷盘、编码、波形/BPM 分析、频繁分配和长锁。
- 主播放器与编辑器播放共用一个明确的播放所有权状态；切换时停止前一来源，不能同时争用设备。

### 8.2 参数节流

- 播放头、Speed、Pitch、Gain、Fade 拖动只提交受控频率的预览参数。
- 鼠标释放时提交一个最终命令。
- BPM 检测只在用户点击后创建低优先级任务；关闭页面时取消并释放。

## 9. 录音

继续复用 `RecordingSession`：

- Capture Callback 只将 PCM 写入固定容量 RingBuffer。
- WriterThread 将 RingBuffer 写入临时 WAV/PCM；内存不随录音时长增长。
- 停止后 finalize 文件，建立新的 Recording AudioEvent，并按需更新其 Peak。
- UI 提供输入设备、输入电平、监听、开始、停止和录音时长。
- 不新增 PortAudio；Windows 使用现有 WASAPI/miniaudio 后端。未实现平台必须明确禁用，不能假装可用。

## 10. BPM、速度、音高与 Formant

- `speedRatio = targetBPM / sourceBPM`；BPM 不生成 Beat Grid。
- 速度连续范围覆盖 0.50x–2.00x，并保留常用预设。
- Tempo 与 Pitch 是独立参数；修改一方不得修改另一方。
- 默认 `preservePitch = true`，复用 SoundTouch。
- Pitch 为 -12 至 +12 半音，UI 提供减号、滑杆、加号和数值。
- “人声保真 / Formant保护”必须与参考图一致固定显示并连接真实处理路径。现有 `vocal_protection` 只能在完成受控音频 A/B、算法路径和输出差异验证后作为该能力的实现；验证失败时必须修正算法，不能隐藏控件或用空壳开关代替。

## 11. 唯一离线渲染链

正式导出和选区 Handoff 共用扩展后的 `ExportRenderer`/`DocumentWriter`：

```text
EventTimeline + RenderRange
  → FFmpeg Decode
  → Event Trim/空隙/Mute
  → SoundTouch Tempo/Pitch
  → Gain/Envelope/Fade
  → PCM
  → FFmpeg Encode
  → 临时文件
  → 完整验证
  → 原子提交
```

- 单次渲染限制并发，工作优先级低于实时播放。
- 解码、编码和写盘不能运行在 QML/UI 线程或 Audio Realtime Thread。
- 正式导出使用右栏设置；Handoff 固定为通用 WAV/PCM，不新增独立格式 UI。
- 尽量保持源/编辑器采样率与合理位深，不强制 44.1 kHz 转为 48 kHz。

## 12. 原生“拖出片段”

### 12.1 视觉与手势

- 只要存在有效选区，左下角显示半透明“拖出片段”热区，右上角显示 Sample 精确时长。
- 单击热区不导出、不弹窗、不发送。
- Mouse Press 后只有移动距离超过 `QApplication::startDragDistance()` 才调用 `prepareSelectionHandoff()`。
- 阈值前必须保持 0 Render、0 Encode、0 Handoff File。

### 12.2 `SelectionDragController`

只负责：

- 捕获当前 Selection、editorRevision 和渲染参数。
- 请求唯一 ExportRenderer 生成选择结果。
- 完成后创建 `QMimeData`，设置 `QUrl::fromLocalFile()` URL 和 `text/uri-list`。
- 使用 `QDrag::exec(Qt::CopyAction)` 启动系统拖放。

### 12.3 `HandoffAssetManager`

- 路径固定在 `<AppData>/AudioEditor/Handoff/`。
- 缓存键为 source identity、selectionStartSample、selectionEndSample、editorRevision 和 render 参数。
- 只复用已完成且仍存在的最终 WAV 路径，不保存 PCM、Decoded Buffer、Peak 或数据库。
- 编辑修改导致 revision 变化时旧文件不再作为当前结果。
- 第一版不在 Drop 后自动删除 Handoff 文件，避免外部工程素材脱机。
- 不扫描目录、不检测目标软件、不启动常驻线程。

## 13. 项目保存

参考图中的“保存工程”必须有真实、轻量行为：

- 使用 Qt 自带 JSON 能力保存 `.agproj`。
- 只序列化版本、源文件身份、AudioEvent 元数据、选区、播放头、导出设置和必要编辑器状态。
- 不保存 PCM、Peak、Decoded Buffer、临时 Render 或 Handoff 文件内容。
- 打开工程时验证源文件身份与可读性；缺失源显示明确错误并允许用户定位，不能静默替换。
- “保存工程”不属于导出音频，不能触发编码。

## 14. 1672×941 UI 视觉合同

### 14.1 总体区域

| 区域 | 参考位置与尺寸 | 实现合同 |
|---|---:|---|
| 标题栏 | `0,0,1672,49` | 左侧现有品牌 Logo 与“AgPlayer 音频编辑”，右侧原生风格窗口控制 |
| 工具 Tab | `0,49,1672,43` | `音频编辑 / 格式转换 / 元数据修改 / 文件名处理`，音频编辑蓝色下划线 |
| 主编辑列 | `0,92,1300,849` | 约 77.75% 宽 |
| 右侧检查器 | `1300,92,372,849` | 约 22.25% 宽，A–E 分组 |
| 工具栏 | `12,105,1278,61` | 深色圆角命令按钮，选择工具高亮蓝色 |
| 文件信息条 | `12,180,1278,48` | 文件名、时长、采样率、位深、声道、BPM |
| 轨道头 | `12,294,96,284` | 单轨、声道、M/S、0 dB 推子 |
| 标尺/波形 | `118,244,1167,334` | 时间标尺、Scene Graph 波形、播放头、Event、选区、Gain/Fade |
| 时间线滚动 | `118,592,1167,16` | 轻量横向滚动反馈，不恢复旧缩略导航卡片 |
| 录音/播放 | `12,619,556,130` / `580,619,708,130` | 分离录音控制和播放控制，播放键绿色环 |
| 快捷键卡 | `12,759,1276,157` | 两行高频快捷键与鼠标说明 |

允许 ±2 logical px 的布局容差；文字栅格化和平台字体抗锯齿单独分类，不以逐像素完全相等作为唯一标准。

### 14.2 右侧 A–E 分组

- A. 录音：设备、输入电平、监听、录音格式。
- B. 速度/BPM：BPM、手动检测、速度滑杆、实时数值、复位。
- C. 升调降调：-12 至 +12 半音、减号/滑杆/加号/数值。
- D. 保持音调：变速时保持音调与“人声保真 / Formant保护”两行固定显示并真实生效。
- E. 导出设置：格式、采样率、位深、声道、比特率、输出目录和蓝色“导出音频”。

### 14.3 工具栏语义

- 工具栏固定为导入音频、保存工程、选择、分割、删除、裁剪、复制、粘贴、淡入、淡出、静音片段、降噪、清除；不得多项、少项或换序。
- 每个工具必须连接真实命令。降噪在选区存在时处理选区，否则处理当前事件/整条时间线；处理在后台执行、可取消、可撤销，并在成功验证后原子提交，不能以空壳或永久禁用代替。
- “清除”定义为清除当前选区/工具临时状态，不删除源文件。
- “裁剪”定义为非破坏性修剪到有效范围，不覆盖原始音频。

### 14.4 响应式与真实数据

- 1672×941 是像素级主验收面；同时保留 1280×720 和 880×560 可用性，不要求小尺寸逐像素同构。
- 主波形优先获得剩余空间；小尺寸可折叠右侧分组，但不能隐藏核心导出和播放路径。
- 运行时只显示真实文件、真实设备、真实 BPM、真实电平和真实选区；视觉 QA 使用真实可复现音频与真实交互状态，不得用 Demo 状态掩盖功能缺口。
- 参考图中出现的每个按键、滑杆、开关、下拉框、输入框、浏览入口和传输控制都属于核心控件，必须产生真实结果并报告真实错误，不能保留阶段占位、空壳或永久禁用。

## 15. 快捷键合同

- `Ctrl+1` 选择工具，`Ctrl+2` 剪刀工具。
- `S` 或 `Ctrl+B` 在播放头处分割。
- `Delete` 删除选中片段并保留空隙。
- `Ctrl+C/X/V` 复制、剪切、在播放头粘贴。
- `Ctrl+拖动` 快速复制片段。
- `Ctrl+Z/Y` 撤销/重做。
- `Space` 播放/暂停。
- `Ctrl+滚轮` 缩放，`Shift+滚轮` 横向平移，中键拖动平移。
- `Esc` 取消选区、当前拖动或剪刀模式，不能意外销毁文档。

底部仅显示高频项，不加入低频快捷键大全。

## 16. 删除与保留策略

删除必须基于调用链和测试证据，不按文件名猜测：

应删除或替换：

- 编辑器 Beat Grid、节拍 Snap 及仅为它们存在的测试。
- 被 EventTimeline 取代且无调用的 AudioSpan 连续/Ripple 编辑分支。
- 编辑器整曲预览 Render、独立 `ag_player` 和其临时预览定时/目录代码。
- 新 UI 不再引用的 OverviewNavigator、旧两组 Inspector、旧循环/标记 UI 和布局测试。
- 工具路由中控制器永远无法选择的无效 case 4/5 预留分支。
- 完成替代后无引用的旧 QML、CMake 注册、翻译条目和测试。

必须保留并演进：

- Decoder、Transcoder、DocumentWriter/Renderer、SoundTouch、AudioEngine、PlaybackController。
- RecordingSession、RingBuffer、PeakPyramid、Scene Graph 波形、任务取消与设置路径。
- 格式转换、元数据修改、文件名处理和主播放器的现有功能。

删除阶段必须在新路径通过等价测试后进行；不清理用户其他未提交修改，不扩大到插件路线。

## 17. 严格实施顺序与阶段门禁

顺序不可交换；每个 Phase 必须先建立失败测试，完成最小实现，再构建、运行、测试并记录证据。

1. **AudioEvent 数据模型**：模型不持有 PCM；边界、共享 Source 和 revision 测试。
2. **单轨 Timeline 时间映射**：时间空隙、Event 命中、Sample/像素映射和长文件测试。
3. **移动 / Trim**：只改 timeline/source 边界，不重算波形、不编码。
4. **Split / Delete / Copy / Paste**：源共享、删除保留空隙、粘贴位置和合并条件。
5. **Undo / Redo**：命令差量、拖动合并、栈上限和 100 次循环测试；同时接入轻量项目保存。
6. **选择框 + 缩放 + 平移**：Sample 精确时长、边缘调整、Ctrl/Shift 滚轮、中键和可见区波形。
7. **Gain / Volume Envelope**：0 dB 线、受限控制点、实时参数与命令提交。
8. **Fade In / Fade Out**：控制点、三种曲线、默认 Smooth、无复杂重叠。
9. **录音**：固定 RingBuffer、WriterThread、真实设备状态和 Recording AudioEvent。
10. **Speed / BPM**：手动启动分析、公式、低优先级任务和实时预览。
11. **Pitch Shift**：-12 至 +12，和 Tempo 解耦。
12. **Tempo Preserve Pitch**：默认开启、SoundTouch 复用、Seek/reset 和连续播放。
13. **Export Renderer**：多 Event、空隙、全部效果、选区、取消、验证和原子提交。
14. **拖出片段**：热区、阈值、WAV、QDrag/QMimeData/QUrl、缓存失效和真实 Explorer/NLE 拖放。
15. **性能验收**：关闭/空闲/播放/导出 CPU 与内存、启动和切歌、1–2 小时文件、包体积和依赖清单。

每阶段在 `docs/development/` 记录：需求编号、修改文件、RED/GREEN 命令与输出、构建配置、运行证据、失败、未验证项和回退点。

## 18. 验收矩阵

### 18.1 自动化

- Core：AudioEvent、EventTimeline、命令、映射、Envelope/Fade、渲染范围、Handoff key。
- Qt：Controller 状态、生命周期、按需任务、播放器所有权、录音、项目保存和 Drag Threshold。
- QML：13 项工具顺序、1672×941 布局合同、快捷键、全部真实 enabled 状态、固定 Formant 行、空态和小尺寸可用性。
- 回归：主播放器、格式转换、元数据修改、文件名处理、库、导入和现有音频输出。

### 18.2 真实运行

- Debug 与 Release 分开构建和运行；历史 Debug 退出访问冲突必须重新验证，不能由 Release 通过替代。
- 使用真实 WAV/FLAC/MP3 和长文件验证导入、波形、编辑、播放、速度/音高和导出。
- 使用真实输入设备验证设备枚举、电平、监听、录音、停止、文件完成和回放。
- 使用 Explorer、桌面、文件夹以及至少一个接受标准文件 URI 的 NLE 验证拖出。
- 对修改前后执行编辑器关闭、打开空闲、播放和导出四组 CPU/内存采样。

### 18.3 视觉 QA

- 固定中文、深色、`QT_SCALE_FACTOR=1` 和 1672×941。
- 捕获参考图与实现图，生成同尺寸并排图和差异蒙版。
- 分别检查字体、间距、颜色、图标/资源质量、文字内容和交互状态。
- 每次发现 P0/P1/P2 差异后修复并重新捕获；`design-qa.md` 只有在无可执行 P0/P1/P2 时写 `final result: passed`。
- 动态文件名、波形、时长、设备、电平和 BPM 必须来自真实测试素材或硬件；除这些动态内容外，参考图中的结构、控件、文案、图标、尺寸、颜色和交互状态均为复刻目标。

## 19. 完成定义

只有同时满足以下条件才能宣称完成：

- Phase 1–15 按顺序完成，且每阶段有编译、运行、测试和需求追踪证据。
- 1672×941 设计 QA 通过，核心交互与所有可见控件有真实行为。
- 主播放器、其余三个音频工具和现有库/导入流程没有明显回归。
- 无第二播放器、第二波形、第二导出链、Beat Grid、假 Formant、NLE Bridge、后台扫描或新增重量级依赖。
- 编辑器关闭时没有编辑线程、定时器、BPM/波形任务、录音资源或 Handoff 工作持续运行。
- 原始源文件保持不变；失败和取消不伪装成功，不损坏正式输出。
- Debug/Release、真实音频、真实录音、真实拖放、性能、内存和体积均有当次证据；未完成的硬件/外部软件验证必须明确报告，不能以自动化替代。

## 20. 回退与提交纪律

- 当前生产代码基线为 `827cc64`；开始每阶段前记录 HEAD、分支和工作区状态。
- 用户或其他任务已有未提交修改必须保留；阶段提交只包含本阶段明确文件。
- 每个 Phase 保持可构建、可测试、可回滚；发现架构不满足时停止扩展并回到最近门禁。
- 不封装或发布 EXE，除非用户在真实 UI、播放、录音、拖放和性能验收后另行明确授权。
