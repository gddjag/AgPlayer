# AgPlayer 沉浸视觉 V4.6 需求追踪

## 2026-09-05 主线整合说明

- 本次将沉浸工作树 `f615e023716c8d4e7fdfc2ac130339b08295e83e` 之上的最终未提交增量按文件三方整合到主线。
- 保留主线右侧面板布局、320 px 宽度、84 px 预设卡和 Theme 字号；来源中紧凑卡片的固定尺寸断言调整为主线已批准尺寸，同时保留文字边界、居中和三列布局验证。
- 保留主线歌词时间轴、导入、来源提示和自动隐藏，以及频彩波形设置接口；叠加沉浸空间歌词深度、有限入场动画和水墨材质明暗适配。歌词字号继续来自主线 Theme。
- 新材质 Shader 使用对应的新法线和观察方向接口，同时保留主线透明清屏修复和相机距离雾；CPU uniform 与 Shader 字段顺序已对照。
- 本节只记录整合取舍。五个受影响 QML 文件经 Qt 6.7 `qmlformat` 只读解析通过；当前主线构建、运行和人工听音验收以本次集成交付记录为准，不沿用以下历史工作树结果作为当前通过证明。

## 2026-09-05 续：原创晶体地形与用户反馈精修

用户认可本地 `crystal-depth.png` 的立体结构后，要求降低常亮、亮度随音乐起落、调整颜色、填充星空和压紧九个预设。以下为增量，不重做波形、歌词和歌单。工作树/HEAD 仍为 `codex/immersive-visual-lyrics` / `f615e02`，没有提交、合并或打包。

- 保留连续山脊、顶面倒角法线和侧面方向光；改善高频高度控件在真实 GPU 上的可见作用。原增益映射对中强频谱过早截顶，改为有界单调增益，保留动态层次。
- 顶面白色混合 0.32→0.10、固定冠光 0.32→0.10、地形常亮曝光系数 0.82。新增一个顶点插值量，复用已有短节拍/瞬态包络驱动亮光，律动强度零值关闭；不改变几何，不增加 FFT 或后处理通道。
- 同曲稳定调色板保留青蓝、珊瑚与冷色高光，削减浅黄；默认预设不再用时间彩虹覆盖歌曲配色。频彩波形的三种固定频率颜色不变。
- 球壳星空覆盖上下四周，距离 240–560，近远尺寸及角尺寸下限分层。Balanced 上限 960 星、High/自动预算上限 1600，Eco 保持 160；生成与 GPU 容量一致，沿用单实例缓冲与绘制。修复 Eco 降级被固定上限吞掉的问题，每级确实减少有效工作。
- 九预设保留三行三列；卡片由两行文字加内边距决定，最低 40px（此前 70px）；页面随内容收缩，质量选项完整可见。歌词和动态页高度不变。

### 增量验证

| 检查 | 证据与结果 |
| --- | --- |
| 先失败再修复 | `relief-red.txt`：高频高度只改变 471 像素；`light-red.txt`：普通节拍仅增亮约 0.3%、常亮 P90=189；`polish-state-red.txt`：配色/星空失败；`preset-compact-red-detail.txt`：旧卡高 70、旧面板 548 |
| Release 构建 | `build-polish.log`、`build-compact.log` 退出 0；离线 Shader 编译通过 |
| 聚焦回归 | `ctest-polish-final.txt`：七组 **7/7**，21.42 秒；GPU 12/12、state 49/49；QML 验证两行布局、质量控件可见及第九卡真实鼠标点击 |
| GPU 短节拍与常亮 | `gpu-polish.txt`：相同区域亮度 4,281,097→4,888,660→4,282,078（约 +14.2% 后回落）；律动强度 0 无脉冲。常亮 P90/P95/P99=161/180/208，近白 0/51,269；高频高度改变 15,833 像素，局部流光通过 |
| lint / 审查 | 三个相关 QML 组件 `qmllint-polish.txt` 退出 0；独立只读审查未发现本轮 varying、参数零值、容量、降级或布局阻断项；最终 diff-check 通过 |
| 画面 | `polish-preset-0..8.png` 九预设均正常退出；`polish-presets-contact.png` 为内部 QA 联系图；`polish-real.png` 为实际音频播放，歌词是原创排版测试样本，不是歌曲真实歌词 |

实际播放短样本：预设 0、1600×900、Windows D3D11，启动后第 4–20 秒测量 16.004 秒；单核基准 CPU 13.38%、16 逻辑核归一化约 **0.84%**；工作集 163.45 MiB、private 246.68 MiB。约 24 秒捕获累计 1072 渲染帧、资源代次 1、退出码 0，见 `performance-polish.txt`。这不是 GPU 帧时间或长期性能保证，不据单次波动宣称总体性能提升。

限制：同一合成输入开启歌曲配色时，部分预设共享颜色，九截图不等于九套独立配色；截图不替代动态审美验收。本轮未执行全量 ctest、Debug、30 分钟 soak、设备丢失恢复、全 DPI/4K、macOS/Linux 或人工听音同步验收。

## 2026-09-05 原创场景、响应与电影歌词优化

本节优先于下方历史范围说明：用户本轮重新提供视频，并要求在研究开源项目的基础上设计更好的原创 3D 场景。参考只用于研究感知与交互原则；不是源码移植，也不宣称已经客观“超过原作”。本轮位于独立 `codex/immersive-visual-lyrics` 工作树，基线 `f615e02`，保留此前尚未提交的 Reactor V2 工作，不合并、不打包、不触碰主线源码。

| 当前要求 / 问题 | 本轮原生实现 | 验证入口 |
| --- | --- | --- |
| 柱体纵深、清晰而不过曝 | 180 距离的高处斜视；低中频形成连续地形，顶面窄边缘与冠部高光、侧面有方向性的明暗；内圈实体不再被第二次透明雾化，圆形外缘仍渐隐 | GPU 高光分位数、频段分层、真实界面截图 |
| 低中高频各有作用 | 既有 128 点频谱汇总为八段 RMS；减少视觉层重复平滑的长拖尾，弱音保留形变；低频推动整体，中频形成区域起伏，高频局部表面闪光 | AudioVisualFeatureController、state、GPU 测试 |
| 节拍与空间响应 | 普通节拍启动最多八个有明确寿命的波纹，不再让正弦循环假扮节拍；大冲击与普通节拍分离，亮度与地形抬升响应可见 | GPU 显式冲击、控制器事件测试 |
| 无变化的频谱导致画面冻结 | 限帧、镜头、波纹和动画使用以 GUI 时间锚定的渲染线程单调时钟；不再依赖下一次频谱通知推进时间 | 静态输入持续渲染 GPU 回归 |
| 关闭 / 隐藏时停止工作 | GUI 在运行态边沿同步一次，稳定关闭不重复调度；运行代次用于识别未同步的隐藏→恢复，丢弃后台旧事件和残留镜头冲击 | GPU 关闭 / 隐藏停帧、事件消费回归 |
| 清晰度与低资源兼顾 | 原单次立方体实例化绘制；普通质量 4× MSAA，Eco 与自动网格/分辨率降级 1×，按既有迟滞恢复；滑杆调节不再全量重建实例布局 | 质量策略、Item 采样数测试 |
| 设置真实影响画面 | 补充起伏速度、高频细节、表面流光、镜头冲击；镜头冲击保留小数且零值关闭 FOV 冲击；可编辑背景色；预设页容纳现有九种预设 | 控制面板绑定、QML 集成、预设截图 |
| 电影歌词 | 当前行突出、前后句退后、左右镜像有界透视、两行长句适配、220ms 内完成的入场；深色主题正文混入亮色，消除前后句 alpha 重复衰减；关闭时停止动画 | QML 安全区 / 字号 / 透视 / 生命周期与三位置截图 |
| 保留播放器能力 | 不改频彩波形、歌单、播放核心和歌词获取链；不增加解码、FFT、PCM 缓存或生产依赖 | 最终 Diff Review |

边界：当前播放数据没有已验证的相位对齐 Beat Grid。已有 BPM 与非空波形提供的拍速只作为粗略时间线；没有可靠数据时使用瞬态兜底，不承诺每首歌精确对拍。CPU 地形参考函数不是生产 Shader；不能用 CPU 函数测试代替 GPU 画面证据。

来源版本和许可边界见 `docs/qa/2026-08-29-immersive-visual-license-audit.md` 的 2026-09-05 节。QA 新增预设、歌词位置、延迟参数只在 `--qa-test-mode` 写入隔离测试配置，正常启动无影响。

### 本轮验证与短时测量（2026-09-05）

- Windows / Qt 6.7 / Release / Direct3D11：干净重建通过；最终七组聚焦 `ctest` 为 **7/7**，包括音频视觉、体验控制器、地形状态、Item、GPU、迷你播放器和沉浸 QML。最后代码改动后的日志：`build/qa/immersive-20260905/ctest-bounded-clock.txt`（18.24 秒）。三个修改的 QML 组件 `qmllint` 退出码 0，最终 Diff Review 与 `git diff --check` 无未解决项。
- GPU 实测覆盖静态频谱持续出帧、关闭/隐藏停帧、显式冲击增亮、高频局部高光不抢占低中频主体、常亮区域不出现大块近白过曝、响应范围两端改变画面范围。软件后端 Item 测试与真实 GPU 测试分别运行，不混淆结果。
- 九个预设均生成 1600×900 本地截图并正常退出；默认自动配色开着，同一合成输入可能共享配色，不能把截图算作九套独立美术风格。联系图：`build/qa/immersive-20260905/native-presets-contact.png`。三种歌词位置在真实音乐播放下截图完成：`lyrics-0.png`、`lyrics-1.png`、`lyrics-2.png`。其中歌词是排版测试样本，不是歌曲的真实歌词。最后一版真实音乐画面见 `performance-bounded.png`。
- 修复渲染时钟后进一步发现限帧失败也立即自请求下一帧，形成无等待的空转。改为 Item 内仅在可运行时启用的 16ms PreciseTimer；保留 60/45/30 FPS 的原限帧器，移除渲染线程自循环。窗口隐藏、最小化、关闭均停定时器，稳定关闭期间不做周期工作。

| 同一音频 / 预设 0 / 1600×900 短时样本 | 自循环刷新 | 有界刷新 |
| --- | ---: | ---: |
| 进程 CPU（单逻辑核基准） | 133.41% | 14.72% |
| 整机 CPU（16 逻辑核归一化） | 8.34% | 0.92% |
| 工作集 | 158.3 MiB | 159.5 MiB |
| Private bytes | 247.2 MiB | 239.9 MiB |
| 约 24 秒捕获时累计渲染帧 | 1071 | 1065 |
| 渲染资源代次 / 进程退出码 | 1 / 0 | 1 / 0 |

CPU 为启动后约第 4–20 秒、16.03 秒窗口的进程时间差；包括正常播放与波形链路，不是 GPU 时间。帧数为应用计数，不是精确呈现 FPS 或 P95 帧时间。单次配对样本仅支持消除该空转的结论，不能代表其他硬件、曲目、质量档或长期稳定性；没有以此宣称整体播放器 CPU 均下降相同比例。

构建环境记录：本机中文 `/showIncludes` 前缀被 CMake 错误解码，导致头文件依赖遗漏和旧对象混用；修正当前构建缓存后执行了 clean-first 重建。外部构建目录中的 SoundTouch 消失后，使用已有相同版本包的私有 QA 副本完成链接，未写回其他任务目录。这两项不是生产源码改动。

未执行：本轮全量 ctest、Debug、30 分钟 soak、设备丢失/恢复、全 DPI/4K 矩阵、macOS/Linux 真机及人工听音同步验收。未提交、合并或制作安装包；动态美感是否达到用户目标仍须实际试听观看确认。

> [!IMPORTANT]
> **2026-08-31 覆盖性裁定（当前有效）**：用户已明确停止以此前上传的 HTML、截图、图片和视频作为最终视觉目标。它们只保留为历史输入，不再用于本轮视觉对照、相似度判断或完成声明。当前实现以 AgPlayer 自有 Qt 6 / QML / C++17 / QRhi 架构和内部可复现的产品语义为准；两个开源仓库仅登记为 study-only 来源。不得复制、翻译或改写其源码、Shader、算法、常量、参数表、布局结构或资产。

## 2026-09-03 原生 Reactor V2

- 新增独立的常规节拍事件链：优先使用播放器已有 BPM/播放位置，每拍驱动柱体和中心光的短促脉冲，每 8 拍才触发既有大型冲击；无可靠 BPM 时复用既有频谱通量瞬态兜底。没有增加解码器、FFT、PCM 缓存或音频线程。
- 128 点既有频谱改用听感分段的八频带边界，并使用快起慢落包络。低频形成中心重量与整体起伏，中频形成方向性山脊和稀疏高塔，高频主要驱动顶面流光，不再用等宽频带把高频噪声误当主体高度。
- 网格柱体横截面收缩到单元间距的 `0.82`，Balanced/High 上限分别收敛为 `112²` / `144²`，用可见侧面、暗部遮蔽和真实视线方向光照建立体积感；中心常亮白光被压低，节拍亮度与柱体高度使用同一有界事件包络。
- 星场扩大纵深并减小单颗星尺寸；材质增加顶/侧面分离、主辅光、Fresnel、镜面高光和高度遮蔽，同时减少全景雾白。保持单个 `QQuickRhiItem`、单实例缓冲和一次实例化绘制，继续使用 1x 采样以优先保证流畅与跨后端稳定。
- 两段 2026-09-03 用户视频只用于理解“柱体可读性、纵深、节奏层级”这些感知原则，不进行逐帧复刻，也没有提取代码、Shader、常量、参数或资产。歌词、歌单和频彩波形实现均未修改。

### V2 本轮验证证据

| 验证项 | 结果 | 限定说明 |
|---|---|---|
| 聚焦 C++ / QML / D3D11 GPU smoke | **PASS，6/6** | MSVC Debug；包含 BPM 每拍与八拍冲击、瞬态兜底、频带起落包络、柱体间距、事件只消费一次、中心曝光、低中高频 GPU 响应、共享体验状态和音频视觉参数连接。 |
| 沉浸 QML 集成 | **PASS，19/19** | 涉及 QObject 头文件 ABI 变化的测试目标完整重编译后，Qt Quick Test 直接运行正常退出（exit 0）。该项使用 offscreen software backend，不覆盖 QRhi 渲染；D3D11 由独立 GPU smoke 验证。 |
| 可复现视觉帧 | **PASS，仅内部合成输入** | `build/qa/reactor-v2-2026-09-03/reactor-v2-final.png`；应用 exit `0`，renderer `Ready`、稳定帧 `3`、资源实例 `1`。用于检查倾斜镜头、圆形边界、柱体间隙、暗侧面和星场，不与上传媒体做像素比较。 |

尚未执行真实音乐主观节奏验收、30 分钟 soak、设备丢失/恢复、macOS/Linux 后端和完整 DPI/分辨率矩阵；本轮不制作安装包。

## 2026-09-01 性能与稳定性修复

- 移除每帧复制整张反应堆画面的全屏 `MultiEffect` 柔光层，保留低成本环境光场；渲染仍为单个 QRhi item、单个实例缓冲与单次实例化绘制，不增加播放器、解码器、FFT 或长期 PCM 缓存。
- 自动质量不再只看 CPU 提交时间，同时观察平滑后的呈现帧间隔；严重单帧超时立即计入，持续掉帧按平滑 cadence 判定。Auto/Balanced 默认网格收敛到 128²/45 FPS，High 为 160²/60 FPS，Eco 为 96²/30 FPS；帧调度从理想时间线累进，60 Hz 上正常的 33/16/16 ms 节奏不会退化为 30 FPS，而持续 33/33/33 ms 会触发降级。质量降级先减少粒子、流星和波纹，再降低网格及内部比例；关闭波纹或降低 ripple 数量会直接跳过/缩短顶点着色器中的多波源循环。
- GUI 计数通知节流到最多约 8 Hz，避免渲染线程每帧向 GUI 线程排队；窗口隐藏、最小化及关闭时仍停止帧、频谱派生和 GPU 上传。
- 地形使用八个确定性波源和快/慢低频包络生成宽脊、尾波与中心呼吸；顶面流光只作用于朝上的表面。新增“多源霓虹 / 深海柔波 / 琥珀电影”三个真实参数预设，原有六个预设保持可用。
- 沉浸右上角按“返回 / 最小化 / 全屏”排列；返回时保留进入沉浸前的播放器 shell 和窗口几何，不再强制切回经典 shell。
- 频彩波形修复了两个真实问题：播放位置只驱动游标而未传入几何着色，以及仅标记材质脏导致 QRhi 顶点 alpha 不刷新。现在播放位置同时驱动频彩波形的已播放/未播放透明度，仍复用共享 `WaveformSession`。

### 本轮验证证据

| 验证项 | 结果 | 限定说明 |
|---|---|---|
| Debug/Release 构建 | **PASS** | MSVC DevShell 下 Debug 应用增量构建与 Release fresh configure/full build 均成功；QSB 在两种配置中离线编译。 |
| 沉浸聚焦回归 | **PASS** | Debug 10/10、Release 10/10；包含频彩波形、音频视觉、体验控制器、地形状态、RHI item、真实 D3D11 GPU smoke、QML 波形与沉浸集成。冲击测试先隔离持续背景波纹并等待对应渲染修订真正呈现；修正后 Release D3D11 GPU smoke 连续复跑 10/10。 |
| 50 次渲染开关 | **PASS** | D3D11 GPU smoke 连续切换 50 次；资源代次保持不变、渲染恢复、`liveRendererCount == 1`。这验证渲染开关，不等同于 50 次三宿主原生窗口切换。 |
| QML lint | **PASS** | exit 0；只有 `Theme.qml`、`WaveformSession.qml`、`SharedWaveformView.qml` 三条 unused-import Info，无 warning/error。 |
| 真实音频视觉截图 | **PASS，Windows 单机** | 使用用户提供视频的音轨经播放器既有解码/波形链生成 `build/qa/immersive-real-audio.png`；画面显示透明频彩波形及已播放/未播放分区，没有建立第二解码链。视频仅作动态输入，不作逐像素视觉目标。 |
| Release 90 秒 soak | **PASS，短时** | 进程未提前退出；工作集约 148.1→150.6 MB，句柄 1025→1011，无单调句柄增长。该结果不能替代 30 分钟长期运行。 |
| 完整 Debug `ctest` | **FAIL** | 114/122；本功能相关测试全过。失败为既有音频编辑/格式转换/元数据 QML 退出崩溃、`qml_main_window_test` timeout，以及固定部署路径检查，串行复跑仍失败，未标记为通过。 |
| 完整 Release `ctest` | **FAIL** | 119/121；本功能相关测试全过。失败为 `audio_editor_controller_test` 和 `qml_filename_process_test`，未标记为通过。 |

当前仍未执行 30 分钟沉浸 soak、设备丢失/恢复、50 次三宿主原生窗口切换、macOS/Linux 真机及完整 DPI/分辨率矩阵；不制作安装包。

## 2026-08-31 当前实施追踪（覆盖旧视觉目标）

代码基础为 `d8a7386`，清洁实现计划为 `d8160f4`，证据文档基线为 `e97136f`，最终代码验证 HEAD 为 `8ba8be1`。Task 1、Task 2、Task 3 的独立评审均为 Spec/Quality PASS；中心高光修复 `2b6c80f`、度量稳定化 `6cc4bab` 与后端安全衰减修复 `8ba8be1` 均通过独立复审，最新两名复审者均为 PASS、无 P0/P1/P2。下表记录当前实现合同；旧章节仍保留其当时的验证记录，但不得反向改变本表的范围或重新把上传媒体设为验收标准。

| 当前要求 | 已接受实现与边界 | 接受提交 / 证据 |
|---|---|---|
| 精确恢复播放器窗口 | `WindowController` 在进入独立沉浸展示时暂存主/迷你角色、主窗口 shell、位置和尺寸；退出时恢复原状态。首次运行缺少几何键时不因还原事件、250 ms 同步或析构凭空写入；用户后续真实移动仍可正常持久化。沉浸期间收到的最终 classic/integrated shell 请求保持权威，不把一个 shell 的几何写入另一个键。 | `334e40e`、`0cb4113`、`d48c288`；Task 1 最终独立评审 Spec/Quality PASS。 |
| 原创柔光圆形 QRhi 反应堆 | 保留单个 renderer/resource owner、单实例缓冲和单次 `drawIndexed` 路径。低频形成宽阔中心重量与呼吸，中频形成连续宽脊，高频只提供受控细节与稀疏顶面流光；侧面更暗、更稳定，外围通过雾化/透明衰减隐藏方形网格边界。浮动方块缩小并保持确定性；冲击、粒子、流星与相机脉冲均使用有界包络。中心高光修复降低近白饱和并保留亮区颜色和顶面细节；`8ba8be1` 将两处反向或在最大响应范围下可反转的 `smoothstep` 改为后端安全的升序边界。 | `796b815`、`a713c06`、`7eaf15e`、`f65096d`、`2b6c80f`、`6cc4bab`、`8ba8be1`；Task 2、高光与衰减修复独立评审均 PASS。 |
| 低中频主导、高频流光受限 | 同等输入下，高频不再制造最高塔；GPU 验收使用相对静默帧的最强 1% RGB 响应，高频必须不超过同强度低/中频的 90%。顶面流光须局部可见但不能成为全屏曝光提升；非有限音频、样式、相机和时间输入在状态、公开 API 与 uniform 上传边界均被拒绝或收敛为有限值。 | Task 2 的 D3D11 接受证据约为 31.6%–32.1%；高光 TDD 从近白 `2.20%`、P90/P95/P99 `201/231/253` 降至 `0%`、`176/190/207`。最终 GPU QtTest `9/9`；`responseRange` `0.50→2.20` 的真实 GPU 帧可见像素为 `16316→23741`（ROI `51269`）。 |
| 原生 QML 电影感空间歌词 | 不改变歌词服务、时间同步、行模型或持久化。当前行是唯一焦点；前/后行作为弱化景深上下文；左/右使用镜像且有界的透视角，中央保持正面。空间模式最多两行并省略超长文本；快速切行、服务替换、非 Ready 状态、隐藏或关闭时不会残留旧动画。 | `d8109f5`、`5a0e662`；Task 3 最终独立评审 Spec/Quality PASS。 |
| 语义化且真实生效的设置分组 | 现有 9 个滑杆和 8 个开关按“地形 / 光影 / 运动 / 冲击”分组；对象名、范围、默认值、持久化属性和 renderer/controller 绑定保持不变。没有新增仅改数字、不影响画面的空壳控件。 | `ImmersiveControlPanel.qml` 与 QML 行为测试；Task 3 最终独立评审无 blocker/P1/P2。 |
| 冻结既有共享能力 | 频彩波形继续复用 `WaveformItem`、`WaveformSession` 和已有低/中/高频峰值；队列继续复用现有 queue model；歌词继续复用现有服务、计时和模型。本轮没有修改它们的数据链，也没有增加播放器、解码器、FFT 或长期音频缓存。 | Task 2/3 范围审计与最终独立评审；生产变更不包含 waveform、queue 或 lyrics service/model/timing 文件。 |

## Task 4 最终验证结果

以下结果同时保留成功与失败。Task 1–3 的局部通过结果不用于覆盖完整测试套件的失败，隔离直跑也不用于把间歇性的 CTest harness 问题改写为全绿。

| 最终验证项 | 结果 | 严格限定的证据 |
|---|---|---|
| Fresh Debug 应用构建 | **PASS，有限定** | 在 `5a0e662` fresh configure/build `147/147`、exit `0`；QSB fresh 执行。最终 HEAD `8ba8be1` 在 VS DevShell 中全量增量构建 exit `0`。普通 PowerShell 未加载 VS SDK 时曾因找不到 `ole32.lib` / `user32.lib` exit `1`；加载 DevShell 后同目标 exit `0`，属于命令环境要求而非隐藏为成功。 |
| Fresh Release 应用构建 | **PASS，有限定** | 在 `5a0e662` fresh configure/build `65/65`、exit `0`；最终 HEAD `8ba8be1` 在 VS DevShell 中全量增量构建 exit `0`。同样要求正确加载 MSVC/Windows SDK 环境。 |
| 聚焦 C++ / QML 回归 | **Release PASS；Debug PASS** | 最终 HEAD Release 六项 `6/6`，`18.99 s`；Debug 六项 `6/6`，`21.72 s`。这份最终新鲜证据取代 `6cc4bab` 时 Debug 沉浸 QML 的 PARTIAL 记录，但不覆盖下方早先完整 Debug/Release CTest 的失败。 |
| 离线 QSB Shader 编译 | **PASS** | Fresh Debug 构建实际生成 `terrain_reactor.vert.qsb`；`8ba8be1` 后 Debug/Release 全量增量构建 exit `0`，六个 QSB 变体复审均 PASS。新增源码合同直接拒绝反向或可反转的衰减边界。 |
| `qmllint` | **PASS** | 最终 exit `0`；只有 `Theme.qml`、`WaveformSession.qml`、`SharedWaveformView.qml` 三条既有 `unused-import` Info，无 warning/error。 |
| 完整 `ctest` | **FAIL** | Release：`5a0e662` 为 `117/121`，`5b17b63` 为 `118/121`；后者失败 `import_controller_test` timeout `35.02 s`、`audio_editor_controller_test` timeout `30.03 s`、`qml_main_window_test` timeout `35.19 s`，相关沉浸 QML `8.33 s`、GPU `7.05 s` 通过。Debug：`102/122`，20 项失败；组合隔离仅 `2/20` 通过，不能宣称 Debug 全套通过。详情见 Task 4 验证报告。 |
| 内部确定性视觉 / GPU smoke | **PASS，仅 Windows D3D11** | 最终 GPU QtTest `9/9`；`responseRange` `0.50→2.20` 的真实 GPU 帧可见像素 `16316→23741`（ROI `51269`），证明端点改变实际地形占用而非仅更新 UI。Release GPU repeat `5/5`，最长 `11.58 s`；Debug GPU 通过。此前 60 秒看门狗 `10/10` 与旧 10 秒超时历史仍保留，不与上传媒体比较。 |
| 实际 GUI 短时可用性 | **自动 smoke PASS；人工交互 NOT RUN** | Release QA 以合成频谱在 `1600×900` 启动并 exit `0`，无残留进程；内部检查圆形占用、方界消隐、环境密度和中心层次 PASS。未使用真实音频验证频彩波形，也未人工执行进入/返回、全屏 Esc、拖动或滚轮。 |
| 清洁来源扫描与最终 Diff Review | **PASS，路径限定** | 在 `app/qt/core/cmake/CMakeLists.txt` 限定范围内，受限标识符 `rg` exit `1`（零匹配），依赖 diff 匹配 `0`；base→HEAD 与工作树 `git diff --check` exit `0`。未发现 WebEngine、Electron、Three.js、第二解码器、新 FFT、新依赖或第三方资产。 |

## 2026-08-31 最终证据 / 限制

- 最终代码验证 HEAD 为 `8ba8be1`，证据文档上一提交为 `e97136f`。GPU CTest 专用 60 秒看门狗是 `5b17b63`；中心高光修复为 `2b6c80f`，其度量稳定化为 `6cc4bab`；后端安全衰减与 responseRange 端点合同为 `8ba8be1`。两名最新独立复审者均为 PASS，未发现 P0/P1/P2。
- `8ba8be1` 修复 `terrain_reactor.vert` 两处反向或可反转的 `smoothstep`，新增 Shader 源码合同，并以真实 GPU 帧覆盖 `responseRange` `0.50→2.20`：可见像素 `16316→23741`（ROI `51269`）。GPU QtTest `9/9`，Release repeat `5/5`（最长 `11.58 s`），Debug GPU 通过；六个 QSB 变体复审 PASS。
- 高光 TDD RED：近白占比 `2.20%`，P90/P95/P99 为 `201/231/253`；GREEN：近白 `0%`，P90/P95/P99 为 `176/190/207`。最终 Release QA 截图为 `build/qa/immersive-backend-safe-8ba8be1/immersive-synthetic-1600x900.png`，`562046` bytes。
- 旧版 AgPlayer 内部 QA 帧到最终帧（不是上传媒体对比）：可见近白 `9.41%→3.21%`，中央近白 `11.19%→3.54%`，P90 `247→224`，亮区颜色保留约 `42.09%→62.7%`。内部判定圆形轮廓、方形边界消隐、环境密度与中心层次 PASS。
- 完整 Release 与 Debug CTest 的最近全套证据仍均为 FAIL；最终 focused Release/Debug 各 `6/6` 只能证明沉浸范围的定向回归，不能覆盖整个播放器。
- QA 模式与生产设置隔离，但 QSettings / pipeline cache 仍落在系统 `qttest` 命名空间，不能声称所有 Qt 状态都写入一次性目录。
- CMake 的 SoundTouch 当前解析到另一工作树的路径，虽未导致本轮构建失败，仍是构建可搬移风险。
- 没有制作安装包；不声明包体增量、真实音频体验、长期稳定性或跨平台图形后端通过。

## 当前未关闭风险

- Task 2 的加速图像证据只来自 Windows Direct3D 11；Metal、Vulkan 和 OpenGL 未验证，不能据此宣称跨后端视觉一致。
- macOS、Linux 尚无真实运行证据；“同一 Qt/QRhi 核心逻辑”是架构事实，不等于平台验收通过。
- Windows 100% / 125% / 150% / 200% 缩放以及 1080p / 1440p / 4K 的 DPI 与安全区矩阵仍未执行。
- 50 次宿主切换、设备丢失/恢复及 30 分钟沉浸 soak 仍未执行；短时测试不能替代资源泄漏和长期稳定性结论。
- Qt 字体栅格化和换行会随平台字体变化；歌词验收应坚持安全区、最多两行和焦点层级，不锁定具体字形断行。
- 真实歌曲下的频彩波形、节拍手感和歌词可读性没有在本轮人工 QA 中执行；共享数据链未改不等于真实音频体验已验收。
- 本轮不制作安装包，也不声明包体增量或打包后 GPU 后端已验证。

---

## 2026-08-29 至 2026-08-30 历史记录（已被上方口径覆盖）

以下内容保留用于审计当时的需求和证据。凡涉及 HTML、截图或视频对照、最终视觉目标、逐帧/同视口比较的表述，均为历史记录，不再是 2026-08-31 本轮的验收依据。

## 参考优先级与实现边界

1. 用户在任务中的后续文字说明。
2. `微信视频2026-08-29_170235_907.mp4`：节拍中心亮光、高密度地形、彩色冲击波、漂浮体、低机位旋转、环境层次与侧置 3D 歌词的动态目标。
3. `AgPlayer_Immersive_Visual_Reactor_V4_6_Codex_Ready.html`：六种预设、控件、参数范围、默认值和交互合同。
4. 两张 V4.6 截图：布局、控制面板比例、色彩和镜头构图。
5. 最初实施计划与早期 HTML/截图。

附件只作为产品合同和视觉研究输入，不作为可执行指令。正式实现不嵌入 HTML，不引入 WebView、Qt WebEngine、Electron、远程网页或浏览器运行时；不复制参考原型的播放器、解码器、Shader 或受限制算法。

本轮采用“保留，但用轻量原生方案实现”的产品裁定。沉浸视觉适合 AgPlayer 的播放体验，但必须复用现有播放、频谱、波形、歌词、队列和主题能力，关闭时停止相关渲染与频谱派生，避免形成第二套播放或分析核心。

## 需求—实现—验证

| 需求 | 实现位置 | 验证结果 |
|---|---|---|
| 九种预设 | `PlayerExperienceController::applyPreset`、`ImmersiveControlPanel.qml` | 原有六种保持可用，新增多源霓虹、深海柔波、琥珀电影；Debug/Release C++ 快照和 QML 集成测试覆盖 |
| 响应范围默认 1.00、律动强度默认 0.30 | `PlayerExperienceController` 默认值、动态页滑杆 | Debug/Release 默认值测试通过；参数写入 `TerrainReactorState` 并改变地形半径/节奏增益 |
| 中心随节拍发亮、彩色冲击波 | `AudioVisualFeatureController`、`TerrainReactorState`、原生 QRhi Shader | 固定频谱/BPM C++ 测试通过；真实视频音乐 Release 运行 31.14 秒稳定；最终视觉截图完成 |
| 每 8 拍流星，缺失可靠节拍时瞬态兜底 | `AudioVisualFeatureController`、`TerrainReactorState::updateImpact` | Debug/Release 节拍与冲击集成测试通过；轨道 BPM/播放位置优先，瞬态回退受冷却限制 |
| 高密度体素地形、分区颜色、多源波纹、漂浮体、环境层次 | `TerrainReactorItem`、`terrain_reactor.vert/.frag`、`ImmersiveSurface.qml` | QRhi GPU smoke、状态测试通过；外围静态颗粒按响应场衰减，中央使用连续簇状峰场、八个确定性波源及快/慢低频包络；已移除复制整帧的 `MultiEffect`，保留低成本环境光场 |
| 自动旋转、拖动、滚轮、4 秒恢复自动镜头 | `ImmersiveSurface.qml`、`TerrainReactorItem` | Debug/Release QML 集成与相机状态测试通过；鼠标拖动/滚轮写入真实相机参数 |
| 歌词显示开关、左/中/右 3D 布局、位置/大小调节 | `LyricsPanel.qml`、`ImmersiveControlPanel.qml`、`PlayerExperienceController` | Debug/Release QML 集成及控制器持久化测试通过；歌词开关和空间参数独立于主题/沉浸开关 |
| 普通窗口透明三行歌词 | `Main.qml` 的共享 `LyricsPanel` | QML 集成测试通过；未做 LRCLIB 线上服务实网验收 |
| 沉浸底部“频彩波形” | `SharedWaveformView.qml`、共享 `WaveformSession`/`WaveformItem` | 固定使用低频珊瑚红、中频青绿、高频蓝紫的语义混色；透明无边框，仅保留歌名/时间/波形；直接消费原有 mix/bass/mid/high 峰值，不新增解码、FFT、缓存或模型 |
| 全局四种波形与顺序 | `SettingsController`、`SettingsPage.qml`、双窗口/单窗口/迷你播放器共享控制 | 保留数值兼容：0 纯色、1 RGB、2 柱状频谱，新增 3 频彩；循环顺序固定 0→3→1→2→0；纯色仍为新安装默认 |
| 频彩低/中/高颜色自定义 | `SettingsController`、`SettingsPage.qml`、`WaveformItem` | 默认 `#FF647C` / `#3ED6AE` / `#8A7CFF`；设置页可独立修改并一键恢复，持久化/回滚/非法值修复测试覆盖 |
| 稳定逐曲配色、切歌平滑过渡、波形同步换色 | 共享波形调色板、`PlayerExperienceController`、地形调色板绑定 | Debug/Release 同曲稳定与轨道切换测试通过；520 ms 平滑过渡；手动配色会关闭歌曲自适应模式并真实写入渲染参数 |
| 三宿主共享单渲染器 | `Main.qml` immersive coordinator | Debug/Release QML 集成及 GPU smoke 通过；D3D11 已执行 50 次 active 开关并保持单 renderer/同一资源代次，尚未执行 50 次原生宿主窗口切换 |
| 渲染关闭/失败时停止无效工作 | `TerrainReactorItem::renderingRequested`、`Main.qml` | Debug/Release fail-closed 测试通过；软件/资源后端失败会停止音频视觉派生并显示非模态降级信息 |
| 自动质量与低资源策略 | `TerrainReactorItem`、`TerrainReactorState` | 状态与 GPU smoke 测试通过；真实音频 31.14 秒运行工作集快照约 171.3 MB；尚无 30 分钟性能曲线 |
| 队列抽屉保持当前队列作用域 | `ImmersiveQueueDrawer.qml` | Debug/Release QML 集成测试通过；使用现有 `queueTrackIds`/`trackForId`/`playTrackIds`，不复制队列模型 |
| 主题、沉浸视觉、歌词三项状态互不干扰 | `PlayerExperienceController`、`ExperienceActions.qml` | Debug/Release 控制器与 QML 集成测试通过；主题切换不重建播放核心 |
| 沉浸视觉作为独立主题窗口 | `ExperienceActions.qml`、`Main.qml`、`ImmersiveWindow.qml`、`WindowController` | 主题动作按经典→单窗口→沉浸视觉循环；进入时临时隐藏播放器与歌曲列表，退出后恢复此前主/迷你窗口和列表偏好；反应堆始终由独立无边框窗口承载 |
| 纯净顶层操作 | `ImmersiveSurface.qml`、`ImmersiveWindow.qml` | 已移除左上角品牌文字和面板文字按钮；右上角按“返回窗口主题 / 最小化 / 全屏”排列；最小化会暂停视觉派生，恢复窗口后继续，Esc 退出全屏的 QML 集成测试通过 |
| 最终 HTML 九项动态参数与开关 | `ImmersiveControlPanel.qml`、`PlayerExperienceController`、QRhi uniform/shader | 输入压缩、音频响应、响应范围、中心高光、律动强度、景深、主体清晰、自动旋转速度、律动灵敏度及爆发/流线开关均写入真实渲染快照；旧地形振幅等重复 UI 已移除 |

## 已执行验证

- Debug：从干净构建目录完成应用全量构建；功能聚焦目标在最终 Diff 后再次验证。
- Release：应用全量构建、10 个功能聚焦测试与 QML lint 在最终 Diff 后再次验证；Release D3D11 GPU smoke 额外连续复跑 10/10。
- Debug 全量 `ctest` 最终复跑：114/122 通过。失败为 `audio_editor_controller_test`、`qml_main_window_test` 超时、四项格式转换/文件名/元数据 QML 退出崩溃，以及 `runtime_deployment_test` 的固定部署路径检查；本功能相关测试均通过，完整套件未标记为通过。
- Release 全量 `ctest` 最终复跑：119/121 通过。功能相关 GPU smoke、波形和沉浸 QML 集成均通过；剩余失败为 `audio_editor_controller_test` 和 `qml_filename_process_test` 退出崩溃，两项均未标记通过。
- 测试覆盖：设置持久化、频彩波形几何与颜色语义、音频视觉特征、体验控制器、反应堆状态、RHI item、GPU smoke、共享波形、音频冲击 QML、迷你播放器和沉浸集成。
- GPU 冲击高光在 Debug/Release 均通过；迷你播放器测试夹具按“先销毁窗口、再派发延迟事件”的顺序清理，Release 连续三次及最终聚焦回归均通过。
- QML lint 只有 `Theme.qml`、`WaveformSession.qml`、`SharedWaveformView.qml` 的未使用 import 信息提示，无错误。
- Release 使用用户参考视频的真实音频连续运行 31.14 秒，无崩溃；进程在证据采集后主动结束。日志仅有一次剪贴板重试警告。
- 2169×1131 固定视口和固定合成频谱下完成原生截图，并把最终 HTML 截图与原生实现放入同一对比输入检查。
- 独立 Diff Review 的最终复审无剩余 Critical/Important 问题；自动质量 cadence、首次内部缩放同步和动态波源裁减均已按复审意见修正并由测试覆盖。
- `git diff --check` 和受限来源标识符扫描在最终文档更新后再次执行，结果记录于本任务最终交付。

## 尚未执行与剩余风险

- 未执行 Windows 100%/125%/150%/200% 与 1080p/1440p/4K 的完整 DPI 矩阵。
- 未执行 macOS/Linux 真实运行；跨平台结论仅限同一 Qt/QRhi 核心代码路径可编译设计，不标记平台验收通过。
- 未执行 50 次三宿主开关、设备丢失/恢复和 30 分钟沉浸 soak；31.14 秒运行不能替代长期稳定性结论。
- 未执行 LRCLIB 线上实网查询；本地歌词服务自动测试通过，不扩大为网络服务可用性结论。
- 未制作安装包。当前柔光与环境层使用轻量 QML 图元和既有 QRhi 渲染，不引入 `QtQuick.Effects` 全屏后处理或第三方运行时；安装包增量目标需在后续正式打包时测量，当前不声称已完成包体验收。
- 视觉已经降低外围颗粒噪声、增加多源宽脊、中心呼吸与顶面流光，并让波源数量受质量策略真实裁减。上传原型/视频不再作为视觉对照或逐像素验收目标。

## 2026-08-30 独立主题窗口与反应堆复核

- 当前修复只发生在 `codex/immersive-visual-lyrics` 隔离工作树，没有触碰正在打包的主线工作区。
- Release 全量构建和 `agplayer_app_qml_qmllint` 完成；QML lint 只有既有未使用 import 信息提示，无错误。
- Release 功能聚焦回归通过：体验控制器、地形状态、地形 RHI item、Direct3D 11 GPU smoke、迷你播放器、沉浸集成、主题颜色分类。
- Release 全量 `ctest` 最终为 119/121；本功能相关的 GPU smoke、主题颜色分类、沉浸 QML 集成及迷你播放器均通过。`audio_editor_controller_test` 在当前机器缺少录音后端时有 3 个录音断言失败；`windows_shell_runtime_test` 在已有安装版 AgPlayer 运行期间无法满足 Windows 前台窗口激活断言。两项限制均保留原始失败结论。
- 新增数值回归保证中心随机种子只负责细节，不再产生孤立高塔；连续空间簇峰负责成组柱体，冲击帧必须在 GPU 实测中形成宽范围增亮。
- 使用固定合成频谱在 1900×939 视口生成原生截图，并将用户最终参考截图缩放到同一 1900×939 后与原生实现放入一张 1900×1878 对比输入复核。后续用户真机试听仍是动态节奏手感的最终验收。

## 证据路径

- 原生最终固定频谱截图：`build/qa/native-visual/immersive-frequency-soft-final-v8-2169x1131.png`
- 同视口对比：`build/qa/native-visual/reference-vs-frequency-soft-final-v8-2169x1131.png`
- 真实音频波形截图：`build/qa/native-visual/immersive-v46-video-audio-2169x1131.png`
- 设置页四波形截图：`build/qa/native-visual/settings-frequency-color-waveform.png`
- Release 真实音频运行日志：`build/qa/native-visual/release-real-video-30s-pass.log`
- 视觉 QA：`docs/qa/2026-08-29-immersive-visual-design-qa.md`
- 第三方来源审计：`docs/qa/2026-08-29-immersive-visual-license-audit.md`

## 2026-08-30 本轮最终复核

- `WindowController` 新增沉浸展示生命周期，窗口切换测试覆盖主窗口、迷你播放器、歌曲列表偏好和返回恢复；没有创建第二播放器或改动播放队列。
- 主地形实例在 CPU 布局阶段裁成半径 84 的圆盘，外围星体独立分布；默认响应半径收拢为 56，默认相机距离调整为 180，并保留滚轮 42–220 的缩放范围，使黑色星空留白和中央反应堆比例更接近最终 HTML。
- 顶面流光只作用于柱体上表面，由既有高频能量、稳定单元相位和时间共同驱动；外圈透明度与亮度渐隐，避免整片方阵和全场发白。冲击波、漂浮体、流星和频彩波形的数据链未改。
- 动态页保留最终 HTML 的 9 个滑杆、8 个开关，并用现有八段频谱派生只读的 Warmth、Brightness、Sharpness、Smoothness、Density 五项反馈；低/高频占比及锐度、平滑度、密度公式按最终原型语义校准，kick 通过 520 ms 轻量衰减包络平滑影响锐度和密度，没有新增解码、FFT 或音频缓存。
- `TerrainReactorItem` 将八段频谱、能量、频谱通量和 kick/snare 作为只读 QML 属性暴露；集成回归通过真实 `TerrainReactorItem` 合成特征验证五项读数，不再由测试直接给面板赋值。`WindowController::showMain/showMini` 在沉浸展示期间只更新退出后的恢复目标，主窗口、迷你播放器和列表继续保持隐藏。
- Debug/Release 功能聚焦回归均为 13/13，QML lint 无错误。Release 全量 `ctest` 为 118/121；`import_controller_test` 隔离复跑 24/24 通过，`windows_shell_runtime_test` 在另一工作树的 AgPlayer 进程出现前曾隔离通过、之后受前台激活冲突限制；与本功能无关的 `audio_editor_controller_test` 仍在当前机器失败，因此不报告全套全绿。
- 最终固定频谱截图为 `design-qa/2026-08-30-immersive-revision-current-f.png`；最终 HTML 与原生实现的同输入对比为 `design-qa/2026-08-30-html-reference-vs-native-final.png`。

## 2026-09-01 上帝视角、频段响应与清晰度修复

> 以下历史验证仅对应当时版本；2026-09-05 材质增量的验证范围见文末，不沿用历史全套通过结论。

- 默认相机改为高处斜视且保留足够柱体侧面，不再用接近平面的低角度，也避免过度俯视把高度压扁；现有拖动、滚轮、自动旋转和四秒恢复链路未改。
- 直接复用已有八段频谱：低频继续控制中心重量，中频强化中半径环带，高频只形成局部峰值与稀疏顶面流光；瞬态兜底阈值改为短历史均值/标准差自适应，可靠 BPM/Beat Grid 的每八拍优先级保持不变，没有新增 FFT、解码器或 PCM 缓存。
- 降低常亮中心白芯、柔光叠层和全场高光覆盖，增加默认柱体振幅映射、顶/侧面对比和稀疏流光遮罩；新增“柱体跳动高度”滑杆并接入原有 `terrainAmplitude` 持久化属性，所有预设仍复用同一渲染参数链。
- 星空仍使用同一实例缓冲和单次实例化绘制：完整质量星点数由 96 提升到 144，自动质量第一级立即降到 72；星点颜色与亮度按稳定种子分层，流星头与三段尾迹改为更细的斜向渐缩形态。没有新增粒子系统或额外 draw call。
- Release 固定合成频谱截图为 `build/qa/immersive-2026-09-01/god-view-final.png`（1600×900，816640 bytes）；内部 QA 抓图先等待 `renderStatus=Ready` 且至少 3 个稳定 QRhi 帧，避免控件已出现但渲染纹理仍为空时保存无效证据。对应日志为 `build/qa/immersive-2026-09-01/runtime-final.log`，记录 `frames=3`、`stableFrames=3`、`resources=1`。
- Release 聚焦回归为 6/6；QML lint 无错误并保留 3 条既有未使用 import 信息提示。完整 Release `ctest` 为 115/121：本功能的音频视觉、体验控制器、地形状态、RHI item、D3D11 GPU smoke 与沉浸 QML 集成均通过；失败项为 `audio_editor_controller_test`、三个格式转换 QML 退出段错误、`qml_filename_process_test` 退出段错误和 `qml_metadata_editor_test` 超时，均保留失败结论，不用聚焦通过覆盖全套状态。
- 本轮视觉判断以原创原生场景的层级、清晰度、节奏语义和资源边界为准，不再与用户上传图片或视频做逐像素对照。动态节奏手感仍需用户使用真实曲目试听；当前固定合成频谱证据不能替代 30 分钟运行、设备丢失/恢复或 macOS/Linux 真机验收。

## 2026-09-05 九预设材质与波纹细化

### 同日追加：六项独立控制与层次修订

用户确认后，按控制器/QML → 原生实例与 Shader → 静止 GPU 反馈/实际截图的顺序实施。继续保留隔离工作树内原有修改，不改主线，不提交或打包。

- 新增 `columnSize`（50–200，相对尺寸）、`columnOpacity`（0–100，不透明度）和 `reactorBrightness`（0–200，整体亮度），持久化并纳入九预设；复用 `terrainAmplitude`、`subjectClarity`、`rhythmStrength` 作为高度、清晰度、节奏强度，不新增同义业务状态。
- 大小按当前质量网格 / (0.5 + 相对尺寸) 生成，地面半径不变，上限不超过当前质量预算，下限 32。独立审查发现并修复了初版细柱绕过 Eco/自动降级预算的问题。改变大小才重建实例布局，透明度/亮度不重建实例。
- 拉开侧面与顶面明暗、柱间缝隙与山脊低谷；低频形成向外传播的宽浪，中频补充有限的独立柱高差，高频继续局部顶光。没有添加 FFT、解码器或模拟物理线程。
- 透明度为既有渲染通道的整体柱体淡化，不是玻璃折射或正确排序的多层透射。中间透明值仍写深度，不能保证看见全部后方柱体/星点；保留该限制，不为本轮引入 OIT 或额外玻璃渲染通道。
- 亮度在普通与水墨材质独立生效。原清晰度在隔离静止画面测试中仅改变 57/0 个显著像素（普通/水墨），修复为真实柱面对比和水墨边缘墨线；测试不再让动画变化冒充清晰度反馈。
- 撞击波宽度沿用波纹宽度控制，碰撞环保持预设颜色；流星头/尾的网格与法线沿真实飞行方向建立同一正交坐标系，替代无关方向的剪切形变。修复冷暖环境色 QString 通道未转换的问题，使既有背景层正确响应配色。
- 本轮 Release 构建、7/7 聚焦 ctest、两个修改 QML 组件 lint、Diff Check 通过。证据：`build-depth-reviewed.log`、`ctest-depth-reviewed.txt`、`qmllint-depth.txt`；失败先验为 `columns-red.txt`、`columns-gpu-red.txt`、`clarity-isolated-red.txt`、`environment-channels-red.txt`，均在 `build/qa/immersive-20260905/`。
- 真实音频短测正常退出：`depth-final-real.png`、`depth-final-performance.txt`；16.01 秒采样为 15.13% 单核/0.95% 整机 CPU、工作集 167.5 MiB。这不是 GPU 帧时间或长期稳定性结果。`depth-final-presets-contact.png` 为九预设固定合成频谱总览，歌词使用既有原创 QA 文本。
- 没有改歌词、歌单或频彩波形。本轮未做完整套件、Debug、跨平台/DPI、30 分钟稳定性或实际听音验收，截图不能替代用户的动态感知验收。

用户批准水墨、果冻质感和参数细化，并要求每个预设具有相应特性。本轮只改隔离工作树；不提交、不合并、不打包。参考最新五张图片的材质与色彩方向，不移植第三方源码。

| 预设 | 材质与动态特性 |
| --- | --- |
| 音域回响 | 克制的晶体高光，青粉分区，均衡波纹 |
| 霓虹雨夜 | 紫粉青果冻，高弹性、较窄波纹、快速衰减 |
| 水墨 | 暖白纸面、墨色立体柱、淡青层次，柔和渐隐，关闭星点 |
| 纯净舞台 | 青绿淡紫柔润材质，中等回弹与波纹 |
| 安静 | 低扰动晶体，弱波纹与低弹性设定 |
| 星河 | 晶体材质、较强宽波纹，保留星空与空间效果 |
| 多源霓虹 | 果冻材质、窄波纹叠加与较快回弹 |
| 深海柔波 | 深青柔润材质、宽波纹、长尾衰减 |
| 琥珀电影 | 琥珀暖色、柔化晶体反光与克制波纹 |

- 七个新增持久化参数：材质 0/1/2、柔和度 0–100、果冻弹性 0–100、水墨浓度 0–100、波纹强度 0–200、宽度 20–200、衰减速度 20–200。衰减越高消失越快。后三项归一化为倍数；宽度与寿命控制普通节拍波纹，强度也影响撞击波。果冻弹性仅果冻可调、水墨浓度仅水墨可调，禁用时保留数值。
- 果冻采用既有立方体网格的受限节拍伸缩、柔润顶面与边缘光照，不增加折射、透明排序或渲染 pass。水墨通过法线/高度控制墨色浓度与边缘渐隐，不增加纹理、解码或音频缓存。
- 各预设保留自己的主配色；歌曲颜色只作 18% 辅助混合，环境底色不被歌曲覆盖。流动色沿预设配色变化，避免多个预设被同一绝对彩虹覆盖。
- 保留九个 40px 紧凑预设卡片、歌词布局/服务、队列、频彩波形数据与三色映射。水墨只调整歌名、歌词、时长和游标的深浅对比。独立审查发现并修复 QString 底色未转为 QML color 的边界问题，回归覆盖暗色预设直接切水墨及切回全部暗色预设。
- Release 应用与相关测试构建通过；最终 7/7 聚焦 ctest 通过，包含真实 D3D11 GPU、音频派生、状态、控制器、迷你播放器和沉浸集成。四个修改的 QML 组件 lint 通过。GPU 像素测试分别覆盖材质切换、晶体/果冻柔和度、水墨浓度、节拍弹性以及波纹强度/宽度/衰减，不能仅靠 UI 数值变化通过。
- 证据位于 `build/qa/immersive-20260905/`：`build-material-reviewed.log`、`ctest-material-reviewed.txt`、`qmllint-material-reviewed.txt`、`gpu-material-beat.txt`；`materials-final-presets-contact.png` 为九预设固定合成频谱总览。合成频谱截图不代表真实曲目节奏验收。
- 本轮未执行 Debug、完整 ctest、30 分钟稳定性、设备丢失恢复及多平台/DPI 矩阵，不扩大验证结论。最终感知效果仍需用户在真实曲目中试听确认。
- 两次真实音频播放短测均正常退出，截图为 `material-gel-real.png` 与 `material-ink-real.png`；显示的三行歌词是原创 QA 文本，不是从参考曲目获取的歌词。各取 16 秒 CPU 样本：果冻约 16.20% 单核/1.01% 整机、工作集 167.8 MiB；水墨约 15.13% 单核/0.95% 整机、工作集 161.9 MiB。数据是本机短样本，不是全程帧率或长期稳定性保证；日志分别为 `material-gel-performance.txt`、`material-ink-performance.txt`。
