# 沉浸视觉第三方来源与许可审计

> [!IMPORTANT]
> **2026-08-31 清洁实现补充（当前有效）**：上传的 HTML、截图、图片和视频只作为历史输入，不再是最终视觉目标。`yin-yizhen/sonic-topography` 与 `ww085213/Mineradio-LX-Music` 仅登记为 study-only 来源；允许研究不等于允许复制。AgPlayer 当前接受实现是基于自身 Qt 6 / QML / C++17 / QRhi、已有音频特征、播放、波形、歌词和队列接口完成的原创原生实现，未把第三方源码、Shader、算法、常量、参数表、布局结构或资产纳入生产代码。

## 2026-08-31 当前来源登记与隔离边界

| 输入 / 来源 | 当前角色 | 明确禁止进入生产实现的内容 | 当前审计状态 |
|---|---|---|---|
| 用户上传 HTML、截图、图片、视频 | 历史需求与研究输入；不再是视觉验收目标 | HTML/JavaScript/GLSL、浏览器播放或分析链、常量/参数表、布局结构、资源文件，以及逐帧/逐像素复刻 | **PASS，限定范围**：Task 2/3 报告明确未打开或比较上传媒体；最终 QA 只比较 AgPlayer 修复前后内部帧，没有使用上传媒体。 |
| `yin-yizhen/sonic-topography` | study-only；其 Non-Commercial Learning License 需单独尊重 | 源码、Shader、算法、常量、参数表、布局结构、资产及其翻译/改写版本 | **PASS，生产路径扫描**：限定 `app/qt/core/cmake/CMakeLists.txt` 的受限标识符扫描零匹配；最终 diff/review 未发现其源码或资产进入实现。 |
| `ww085213/Mineradio-LX-Music` | study-only；GPL-3.0-only 来源 | 源码、预设、Shader、算法、常量、参数表、布局结构、资产及其翻译/改写版本 | **PASS，生产路径扫描**：同一受限标识符扫描零匹配；最终 diff/review 未发现 GPL 源码、预设或资产进入实现。 |
| Three.js / 浏览器后处理链 | 历史 HTML 依赖，不是 AgPlayer 运行时 | Three.js、WebView、Qt WebEngine、Electron、远程网页、浏览器后处理或 WebAudio 解码/分析链 | **PASS，生产路径与依赖 diff**：受限标识符零匹配，dependency diff 匹配 `0`；没有新增此类运行时。 |
| AgPlayer 自有代码与数据接口 | 当前实现来源 | 不适用；仍须遵守单播放器、单 renderer、无额外 FFT/长期缓存的内部边界 | **PASS，最终范围审计**：Task 1–3 与高光修复独立复审均 PASS；base→HEAD / worktree `git diff --check` exit `0`，无第二解码器、新 FFT、新依赖或第三方资产。 |

## 当前清洁实现证据链

- `docs/superpowers/plans/2026-08-31-immersive-reactor-refinement.md` 明确将上传媒体降级为历史输入，并设定 study-only、禁止复制的来源边界。
- Task 2 报告将反应堆限定为原创、内部产品语义驱动的 QRhi/state/shader 调整；Task 3 报告将歌词限定为原创原生 QML 展示调整。两份最终独立评审均未发现外部实现或上传媒体比较进入其范围。
- 接受提交链为：窗口恢复 `334e40e` / `0cb4113` / `d48c288`，反应堆 `796b815` / `a713c06` / `7eaf15e` / `f65096d`，歌词与设置 `d8109f5` / `5a0e662`，GPU 看门狗 `5b17b63`，中心高光与度量修复 `2b6c80f` / `6cc4bab`。最终 HEAD 为 `6cc4bab`。
- 频彩波形、队列、歌词服务/计时和播放核心未在本轮重写；没有新增播放器、解码器、FFT、长期音频缓存或第三方运行时。
- 最终生产源码扫描限定为 `app/**`、`qt/**`、`core/**`、`cmake/**` 和 `CMakeLists.txt`。受限标识符 pattern 无匹配，`rg` exit `1` 表示零匹配；CMake dependency diff 匹配 `0`。
- `git diff --check d8a7386..6cc4bab` 与最终工作树 diff-check 均 exit `0`。Task 1–3 独立 Spec/Quality 均 PASS；高光修复与测试稳定化独立复审 PASS、无 P0/P1/P2。

## 2026-08-31 最终扫描证据 / 限制

- 扫描明确限定生产源码与构建文件；文档本身必须保留第三方名称和许可证说明，因此没有用全仓库零匹配冒充生产路径零匹配。
- 受限 pattern 覆盖 `sonic-topography`、`Mineradio-LX-Music`、作者标识、Three.js、WebEngine/QWebEngine、Electron、WebView、HTML 与相关许可名；零匹配结果为 `rg` exit `1`。依赖 diff 对 WebEngine/Electron/Three、decoder/FFT、`find_package`、`FetchContent`、`target_link_libraries` 的新增匹配数为 `0`。
- 最终变更继续使用 AgPlayer 既有 QML、QRhi shader/state/item、WindowController 和测试架构；没有发现新增播放器、第二解码器、新 FFT、第三方资产或大型运行时依赖。
- 清洁扫描不等于全套测试通过：完整 Release 为 `118/121`、完整 Debug 为 `102/122`，均为 FAIL；这不改变来源扫描结果，也不能被来源扫描掩盖。
- QA 测试配置与生产设置分离，但 QSettings / pipeline cache 仍落系统 `qttest` 命名空间，是已知隔离限制。
- SoundTouch 当前从另一工作树的构建路径解析；本轮构建成功，但这是依赖可搬移风险，不是新增依赖或许可证通过证明。
- 本轮没有构建安装包，不声明包体增量、再分发物清单或安装包中的最终第三方文件集合已验证。

## 许可与平台限定

本记录是工程来源审计，不是法律意见。study-only 边界不改变外部项目原有许可，也不授予再分发权。最终图形证据仅覆盖 Windows Direct3D 11 / NVIDIA GeForce RTX 4070 Ti SUPER，不能证明 Metal、Vulkan、OpenGL、macOS 或 Linux 的运行结果；DPI、设备恢复、30 分钟 soak 和安装包同样不属于来源审计的通过项。

---

## 2026-08-29 至 2026-08-30 历史审计记录（已被上方补充覆盖）

以下描述和扫描结论按原样保留用于历史追溯。它们不替代 Task 4 对最终 HEAD 的新鲜来源扫描，也不再把 HTML 或上传媒体视为当前视觉目标。

## 审计结论

V4.6 HTML 声明其中央反应堆是对第三方浏览器实现的适配。因此该 HTML 仅用于需求、布局、参数和感知效果研究；AgPlayer 原生实现没有复制其中的 JavaScript、GLSL、常量组织或算法结构。

本分支采用 AgPlayer 自有 Qt 6.7 / QML / C++17 / QRhi 架构，从现有 `PlaybackController.spectrum`、BPM/播放位置、歌词服务和波形会话取数。仓库中未引入 Three.js、WebEngine、Electron、远程网页或第二套音频解码链。

## 来源登记

| 来源 | HTML 声明 | 官方许可依据 | 本分支处理 |
|---|---|---|---|
| `yin-yizhen/sonic-topography` | 1.1.1 / `3ff303e`，Non-Commercial Learning License | [官方 LICENSE](https://github.com/yin-yizhen/sonic-topography/blob/master/LICENSE) 仅允许学习、研究和个人非商业使用，商业或派生商业使用需明确许可 | 不复制源码、Shader、算法；只研究视频/截图呈现的产品效果 |
| `ww085213/Mineradio-LX-Music` | `public/sonic-topography-preset.js`，GPL-3.0-only | [官方 README](https://github.com/ww085213/Mineradio-LX-Music/blob/main/README.md) 声明 GPL-3.0-only | 不复制或移植该文件；避免给 AgPlayer 引入 GPL 派生义务 |
| Three.js r128 与后处理脚本 | HTML 通过 jsDelivr 远程加载 | [Three.js 官方 LICENSE](https://github.com/mrdoob/three.js/blob/dev/LICENSE) 为 MIT | 正式产品不包含或加载 Three.js；原生 QRhi 实现无需该依赖 |

## 清洁实现检查

- 最终生产代码扫描未出现 `sonic-topography`、`Mineradio`、`THREE`、`UnrealBloom`、第三方提交号或 HTML 的 `SONIC_*` 标识符。
- 原生 Shader 使用 AgPlayer 自有 uniform、实例数据、生命周期和音频特征结构；没有把 HTML 中的 GLSL 文本纳入构建。
- HTML 的本地文件选择、WebAudio analyser、整曲波形解码和 Demo 音乐未进入正式实现。
- 本分支未增加第三方依赖；柔光层使用项目固定 Qt 6.7 自带的 `QtQuick.Effects`，桌面 Eco 主动停用以控制资源。
- 人工 Diff Review 未发现参考实现的源码片段、命名结构或运行时资源进入生产代码。

## 限定说明

本记录是工程来源审计，不是法律意见。未来若直接引入参考项目源码、Shader、预设文件或算法实现，必须重新完成许可证兼容性评估，不能沿用本次“清洁原生重写”结论。
