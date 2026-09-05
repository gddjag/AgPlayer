# 沉浸视觉第三方来源与许可审计

## 2026-09-05 官方参考与材质修复

- 已检查 Qt 官方 [Custom QSGRenderNode](https://doc.qt.io/qt-6/qtquick-scenegraph-customrendernode-example.html) 及 [Qt 6.10 rhitextureitem.cpp](https://github.com/qt/qtdeclarative/blob/6.10/examples/quick/scenegraph/rhitextureitem/rhitextureitem.cpp)（文件声明 `LicenseRef-Qt-Commercial OR BSD-3-Clause`）。
- 复用官方示例说明的 QQuickRhiItem 预乘 alpha 规则：透明清屏的 RGB 同样必须为零。实际变更为 AgPlayer 自有渲染器的一行清屏参数修正，没有引入示例源码文件或额外依赖。
- 相机距离雾、观察方向高光、预设面板布局为现有 AgPlayer 实现的本地调整；没有复制第三方可视化项目的 Shader、预设表或资源。
- 明确选择预设后关闭歌曲自动换色，以保证选中的色板能实际显示。用户仍可重新启用自动配色。
- 本节只记录来源和实现边界；实时 GPU、预设文字可见性和视觉验收由本次测试结果单独判定。

## 2026-09-05 续：拒绝受限源码移植后的独立精修

用户最终选择不获取额外授权、继续原创原生实现。本次增量以 AgPlayer 自有代码和本地修改前后截图为依据，没有重新读取、复制或移植上游源码。连续山脊、分析式顶面倒角光照、有界增益、协调歌曲配色、球壳星空及节拍照明均在现有 C++/QRhi 数据路径中实现，不增加第三方依赖、素材或第二解码链。

最新用户认可结构后的精修仅涉及地形/星空/配色与紧凑预设布局，不再调整歌词、频彩波形或队列。QA 仍使用本地音乐进行播放测试，排版样本歌词与测试媒体不进入产品资源。证据见需求追踪的“原创晶体地形与用户反馈精修”节。本条是当前工程来源记录，不声称获得第三方再分发授权，也不将零标识符匹配当作完整许可证明。

## 2026-09-05 原创场景优化：最新输入与研究边界

此节覆盖下面历史记录中“不再参考视频”“本轮没有修改歌词”的范围描述。用户本轮重新提供 2026-08-29 视频，并要求结合两个仓库的研究、加入 AgPlayer 自身的视觉设计；不是复制第三方实现的授权。

| 研究来源 | 本轮核对版本 | 结论与实际使用 |
| --- | --- | --- |
| [sonic-topography](https://github.com/yin-yizhen/sonic-topography) | `ec8ecbaec0c9c5094b6b1480df0d6b2d32d6349b` | Non-Commercial Learning License；仅研究音频分层、地形空间与歌词阅读层级。 |
| [Mineradio-LX-Music](https://github.com/ww085213/Mineradio-LX-Music) | `f61ca181b5d52a20f71a952d5eef4b9d7b79b8ec` | 仓库 GPL-3.0；地形模块又注明来自前述受限来源，不把仓库级 GPL 当作所有模块可直接复用的证明。仅研究节拍事件与视觉组织。 |
| 用户视频 | 32.354 秒，1278×852 | 本地抽取 2/7/12/17/24/29 秒研究帧，观察地形纵深、不同源波纹和侧置文字。未将帧、音轨或其他素材加入产品资源。 |

- 生产代码仍是自有 Qt 6.7/C++17/QRhi/QML：没有复制、翻译或移植第三方 Shader、算法实现、常量表、布局及资产。
- 本轮独立实现了 RMS 频带汇总、弱音压缩、事件触发波纹、柱体冠部光照、旋转晶体法线、抗锯齿质量联动及有限时长歌词入场。仍复用同一播放核心、128 点频谱、歌词服务和频彩波形。
- PyAV 仅安装在本机临时 QA 目录用于读取参考视频，未加入 CMake、依赖清单、运行目录或安装包。测试歌词是明确标注的原创排版样本，不冒充视频歌曲真实歌词。
- 此文为来源工程记录，不替代许可证法律审查，也不声称获得了第三方再分发授权。

> [!IMPORTANT]
> **2026-08-31 清洁实现补充（当前有效）**：上传的 HTML、截图、图片和视频只作为历史输入，不再是最终视觉目标。`yin-yizhen/sonic-topography` 与 `ww085213/Mineradio-LX-Music` 仅登记为 study-only 来源；允许研究不等于允许复制。AgPlayer 当前接受实现是基于自身 Qt 6 / QML / C++17 / QRhi、已有音频特征、播放、波形、歌词和队列接口完成的原创原生实现，未把第三方源码、Shader、算法、常量、参数表、布局结构或资产纳入生产代码。

## 2026-09-03 Reactor V2 来源补充

- 本轮新增的两段用户视频只用于观察抽象感知特征：斜视纵深、柱体可读性、不同节奏层级与暗色环境；不作为逐帧或逐像素目标，也未从视频提取资产。
- Reactor V2 的听感频带边界、起落包络、常规节拍/八拍冲击分层、方向性地形、视线相关光照和顶面流光均在 AgPlayer 现有 C++/QRhi 数据结构中独立实现。
- 本轮生产变更只涉及 AgPlayer 自有音频视觉控制器、地形状态、RHI item、两个自有 Shader 及对应测试；没有引入第三方源码、Shader、算法、常量、参数表、布局、资产或新依赖。
- 歌词、歌单、频彩波形、播放核心和解码链没有修改；仍是单播放器、单频谱来源和单渲染宿主。

## 2026-08-31 当前来源登记与隔离边界

| 输入 / 来源 | 当前角色 | 明确禁止进入生产实现的内容 | 当前审计状态 |
|---|---|---|---|
| 用户上传 HTML、截图、图片、视频 | 历史需求与研究输入；不再是视觉验收目标 | HTML/JavaScript/GLSL、浏览器播放或分析链、常量/参数表、布局结构、资源文件，以及逐帧/逐像素复刻 | **PASS，限定范围**：Task 2/3 报告明确未打开或比较上传媒体；最终 QA 只比较 AgPlayer 修复前后内部帧，没有使用上传媒体。 |
| `yin-yizhen/sonic-topography` | study-only；其 Non-Commercial Learning License 需单独尊重 | 源码、Shader、算法、常量、参数表、布局结构、资产及其翻译/改写版本 | **PASS，生产路径扫描**：限定 `app/qt/core/cmake/CMakeLists.txt` 的受限标识符扫描零匹配；最终 diff/review 未发现其源码或资产进入实现。 |
| `ww085213/Mineradio-LX-Music` | study-only；GPL-3.0-only 来源 | 源码、预设、Shader、算法、常量、参数表、布局结构、资产及其翻译/改写版本 | **PASS，生产路径扫描**：同一受限标识符扫描零匹配；最终 diff/review 未发现 GPL 源码、预设或资产进入实现。 |
| Three.js / 浏览器后处理链 | 历史 HTML 依赖，不是 AgPlayer 运行时 | Three.js、WebView、Qt WebEngine、Electron、远程网页、浏览器后处理或 WebAudio 解码/分析链 | **PASS，生产路径与依赖 diff**：受限标识符零匹配，dependency diff 匹配 `0`；没有新增此类运行时。 |
| AgPlayer 自有代码与数据接口 | 当前实现来源 | 不适用；仍须遵守单播放器、单 renderer、无额外 FFT/长期缓存的内部边界 | **PASS，最终范围审计**：Task 1–3、高光与 `8ba8be1` 后端安全衰减修复独立复审均 PASS；base→HEAD / worktree `git diff --check` exit `0`，无第二解码器、新 FFT、新依赖或第三方资产。 |

## 当前清洁实现证据链

- `docs/superpowers/plans/2026-08-31-immersive-reactor-refinement.md` 明确将上传媒体降级为历史输入，并设定 study-only、禁止复制的来源边界。
- Task 2 报告将反应堆限定为原创、内部产品语义驱动的 QRhi/state/shader 调整；Task 3 报告将歌词限定为原创原生 QML 展示调整。两份最终独立评审均未发现外部实现或上传媒体比较进入其范围。
- 接受提交链为：窗口恢复 `334e40e` / `0cb4113` / `d48c288`，反应堆 `796b815` / `a713c06` / `7eaf15e` / `f65096d`，歌词与设置 `d8109f5` / `5a0e662`，GPU 看门狗 `5b17b63`，中心高光与度量修复 `2b6c80f` / `6cc4bab`，后端安全衰减与 responseRange 真实帧合同 `8ba8be1`。最终代码验证 HEAD 为 `8ba8be1`，证据文档上一提交为 `e97136f`。
- 频彩波形、队列、歌词服务/计时和播放核心未在本轮重写；没有新增播放器、解码器、FFT、长期音频缓存或第三方运行时。
- 最终生产源码扫描限定为 `app/**`、`qt/**`、`core/**`、`cmake/**` 和 `CMakeLists.txt`。受限标识符 pattern 无匹配，`rg` exit `1` 表示零匹配；CMake dependency diff 匹配 `0`。
- `git diff --check d8a7386..8ba8be1` 与最终工作树 diff-check 均 exit `0`。Task 1–3 独立 Spec/Quality 均 PASS；高光、度量稳定化及 `8ba8be1` 修复通过独立复审，最新两名复审者均 PASS、无 P0/P1/P2。

## 2026-08-31 最终扫描证据 / 限制

- 扫描明确限定生产源码与构建文件；文档本身必须保留第三方名称和许可证说明，因此没有用全仓库零匹配冒充生产路径零匹配。
- 受限 pattern 覆盖 `sonic-topography`、`Mineradio-LX-Music`、作者标识、Three.js、WebEngine/QWebEngine、Electron、WebView、HTML 与相关许可名；零匹配结果为 `rg` exit `1`。依赖 diff 对 WebEngine/Electron/Three、decoder/FFT、`find_package`、`FetchContent`、`target_link_libraries` 的新增匹配数为 `0`。
- 最终变更继续使用 AgPlayer 既有 QML、QRhi shader/state/item、WindowController 和测试架构；`8ba8be1` 只修改自有 `terrain_reactor.vert`、现有 GPU 测试与 CMake 测试合同，修复两处反向或可反转的 `smoothstep` 并增加 responseRange 真实帧端点验证。最终来源扫描与两名独立复审均未发现复制的第三方源码、Shader、算法、常量、布局或资产，也没有新增播放器、第二解码器、新 FFT 或大型运行时依赖。
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

## 2026-09-05 材质增量来源

本轮五张用户图片仅用于理解暖白水墨、柔润柱体和协调配色的感知方向。新增材质公式、节拍受限伸缩、波纹参数及预设表直接基于 AgPlayer 既有实例数据、法线和音频包络自行编写，没有引入上述第三方源码、shader、贴图或运行时。水墨不加载纸面素材，果冻不使用第三方折射实现；仍复用现有 QRhi pass。旧审计中的 QtQuick.Effects 描述不代表本轮增加该依赖，本轮未新增依赖或打包资源。
