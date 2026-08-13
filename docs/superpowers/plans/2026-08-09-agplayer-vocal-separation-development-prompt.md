# AgPlayer 人声/伴奏分离功能 Codex 开发提示词

> 用法：把本文件从“开发任务”开始的全部内容交给 Codex。设计已经批准，无需重新讨论产品方向；先检查现状和写实施计划，再按阶段开发、测试和验收。

## 开发任务

你正在 AgPlayer 仓库中开发“人声/伴奏分离”功能。

先完整阅读：

- `docs/research/2026-08-09-agplayer-vocal-separation-feasibility.md`
- 仓库内现有架构设计、构建说明、测试约定和 `AGENTS.md`

不要假设仓库根目录就是当前代码实现。先确认真实活动 checkout/worktree、分支、未提交修改和构建入口；保留用户现有修改，不覆盖、不回退、不顺手重构无关代码。代码发现优先使用 codebase-memory-mcp 图工具。

## 目标

为 AgPlayer 增加可真实使用的离线人声/伴奏分离功能：

- Qt 6 / QML + C++17。
- Windows 10/11 首发，架构允许后续覆盖 macOS、Linux、Android、iOS、HarmonyOS。
- 主程序保持轻量，不携带 AI 模型、PyTorch、ONNX Runtime、CUDA 等大依赖。
- 分离在独立 Worker 进程中运行，Worker 崩溃不能导致播放器崩溃。
- “人声/伴奏分离”集成在现有“音频工具”窗口中，在当前 4 个工具基础上增加第 5 个真实工具页面；现有 4 个工具的行为、顺序和状态不得回归。
- 模型逐个按需下载；用户只下载需要的模型。
- 默认面向新手，只展示 `htdemucs_ft` 的简化入口；高级用户可展开全部已验证 UVR 模型。
- 支持 CPU 或 GPU，默认“自动（推荐）”，任何硬件能力必须以真实探测和完整任务验证为准。

## 已批准的技术路线：双引擎

### 稳定兼容路线

- 默认 `htdemucs_ft（Trama 模式）`先使用外置 Demucs/PyTorch Compatibility Engine。
- CPU 必须可用。
- Windows/Linux 可提供 NVIDIA CUDA GPU 加速包；未安装或不可用时回退 CPU。
- Compatibility Engine、CUDA 运行时和模型都不进入 AgPlayer 主安装包。

### 原生迁移路线

- 同时保留 C++17 + ONNX Runtime 的 Native ORT Engine。
- Native ORT 在与官方 UVR/Demucs 完成数值、整曲听感、边界和性能等价验证前，不得替换默认稳定引擎，也不得向新手宣称完全兼容。
- 原生路线通过验证后，可以由模型 Catalog 调整首选 Engine；UI 模型名、用户缓存和主程序接口保持不变。

### 必须诚实处理的边界

“服务器新增模型时主程序、Worker 不升级”只适用于：

1. 模型属于 Worker 已支持的 Engine Adapter 和架构 profile。
2. 当前 manifest schema 足以描述其采样率、STFT、segment、overlap、stems 和前后处理。
3. 模型已通过质量、许可证、平台与安全验证。

新增神经网络架构、新算子或新前后处理时，允许更新独立 Engine Pack；协议破坏性变化时才更新 Worker/主程序。禁止实现一个靠文件扩展名猜参数的“万能加载器”。

## 不可违反的工程约束

- 生产代码使用 C++17；不得把 Python 逻辑、Qt 类型或平台路径规则放进可复用核心 ABI。
- C++ 核心只暴露版本化 C ABI；HarmonyOS 未来通过独立 Node-API/NAPI Adapter 接入 ArkUI。
- QML 只负责显示和交互，不直接下载模型、启动进程、解析 manifest 或访问推理引擎。
- 播放线程和音频回调线程绝不等待下载、解压、磁盘 I/O、模型加载或推理。
- 不允许虚假占位按钮、假进度、虚构下载大小、假 GPU 状态或模拟成功。
- 支持的目标必须零编译错误、零警告；新增 target 启用 warnings-as-errors。
- 没有真实 UI、真实音频、Worker 崩溃隔离和硬件播放证据前，不打包发布 EXE。
- 不执行来源不可信的 `.pth/.th/.ckpt`；子进程隔离崩溃不等于安全沙箱。

## 音频工具入口与首次引导

采用 **同一窗口第五页**，不新建第二套独立工具窗口：

- 在现有音频工具导航中新增稳定 ID 的 `人声/伴奏分离`；不得依赖易变化的裸数组下标完成路由。
- 音频工具总数必须从 4 个变为 5 个；原有 4 个页面继续复用现有组件和 Controller，不复制页面、不改写无关逻辑。
- 新入口切换到独立 `SeparationPage`，页面通过专用 Controller 消费 Separation Core；不得把下载和进程逻辑塞进现有总窗口或应用入口文件。

用户第一次点击 `人声/伴奏分离`：

1. 立即执行真实 `probe`，检查 Worker Host、稳定 Compatibility Engine、协议与安装目录。
2. 插件缺失时弹出“需要下载人声分离插件”，展示准确下载字节数、解压占用、CPU 基础包、可选 GPU 加速包、版本、来源、许可证和安装路径。
3. 用户确认后下载、校验、原子安装并实际启动 Worker 完成握手；握手失败不能显示安装成功。
4. 用户取消时仍停留在真实插件说明/安装状态页，可以再次下载；不能显示不可操作的空白占位页。
5. 插件可用后进入模型页面，并明确提示：`模型需要按需下载，只下载你需要的即可。`
6. 默认 `htdemucs_ft` 卡片保持选中；未下载时置灰，只有“下载该模型”可用。

## UI 设计与中文文案

### 默认简化模式

首次使用、恢复默认设置或没有有效偏好时：

- “更多专业模型”保持收起。
- 当前模型默认选中 `htdemucs_ft`。
- 页面只展示一个模型卡片：

```text
标准音质                         推荐
Demucs · htdemucs_ft · Trama 同款
适合日常歌曲，乐器保留完整
```

- 模型未下载时：卡片主体和开始按钮置灰；“下载该模型”按钮保持可点击。
- 下载完成并通过校验、Worker 实际加载成功后，才启用“开始分离”。
- 下载状态显示准确字节数、速度、进度、剩余时间、暂停/继续、取消和错误原因。
- 页面下方提供小字按钮：`更多专业模型`；展开后文案变为 `收起专业模型`。

### 高级展开模式

展开后按以下三组显示所有 **Catalog 中已验证** 的模型：

1. `通用音质（Demucs）`
2. `极致消人声（MDX）`
3. `低配快速（VR）`

三类默认场景提示：

```text
标准音质（Trama 同款）：适合日常歌曲，乐器保留完整
KTV 纯净伴奏（MDX）：强力消除人声，适合翻唱 K 歌
极速轻量（VR）：低配电脑快速处理
```

每个模型必须显示：

- 模型名称与家族。
- 一句话 `scene_hint`；由 Catalog 提供，不把未来模型文案硬编码在 QML。
- 输出 stems。
- 下载大小与预计磁盘占用。
- CPU/GPU 支持状态。
- `未下载 / 下载中 / 校验中 / 已安装 / 更新可用 / 损坏 / 不兼容 / 实验性`状态。
- 独立的“下载该模型”“暂停”“继续”“取消”“删除”“重新校验”动作。

未下载模型的选择控件置灰，但其下载按钮必须可用。用户显式选择高级模型后保留选择；如果收起高级区域，顶部摘要必须继续显示真实当前模型，不能让隐藏状态与实际任务不一致。

### 计算设备

提供 `计算设备` 下拉框：

- `自动（推荐）`：默认值；选择已安装、已验证且适合当前模型的最快后端，否则使用 CPU。
- `CPU`：所有正式模型都必须有可工作的 CPU 回退。
- 真实探测到的 GPU，例如 `NVIDIA GeForce ...（CUDA）`。

规则：

- 不显示 Worker 未探测到的 GPU。
- GPU 存在但加速 Engine Pack 未安装时，显示灰色状态和独立的“下载 GPU 加速组件”，不得自动下载。
- 自动模式下 GPU 初始化或完整任务失败时，记录原因并回退 CPU，UI 明确提示。
- 用户强制选择 GPU 时失败，不得静默切换；提示“改用 CPU”操作。
- AMD、Intel、Apple GPU 只有在对应模型完成真实验证后才展示；不要用理论支持代替产品支持。

## 推荐模块边界

### Player Integration Module

负责：

- QML 状态绑定。
- 安装/模型/设备状态协调。
- Worker 生命周期。
- 分离结果注册到播放器、缓存和导出。

不得链接推理运行时。

### Separation Core Module

纯 C++17，负责：

- `CapabilityState`、`ModelDescriptor`、`DeviceDescriptor`、`SeparationJob`、`ProgressEvent`、`SeparationResult`、稳定错误码。
- 模型选择、任务状态机、缓存键、结果验证规则。
- 版本化 C ABI。

不得依赖 QObject、QString、QProcess、QNetworkAccessManager 或平台 UI。

### Desktop Process Adapter

- Qt 侧通过 `QProcess` 启动 `ag-separation-worker`。
- 每个分离任务使用独立 Worker 进程，首版不做常驻模型服务。
- stdin/stdout 使用版本化 NDJSON 或长度前缀 JSON；stderr 只写诊断日志。
- Windows 使用 Job Object，确保播放器退出后回收 Worker。

### Worker Host 与 Engine Adapter

- Worker Host 只负责协议、任务、权限、日志、Engine 发现和错误转换。
- Engine Pack 独立于 Worker：`compat-demucs`、`native-ort`、可选 GPU Pack。
- Engine ABI 必须版本化；不同版本 side-by-side 安装，可原子切换和回滚。
- 模型目录禁止参与动态库搜索，防止模型包投放 DLL。

## Worker 协议

首版只实现以下命令：

- `hello`：协商协议、Worker、Engine ABI 和平台能力。
- `probe`：返回安装状态、模型、设备、后端、可用空间和错误。
- `start`：提交完全解析的单个分离任务。
- `cancel`：幂等取消。
- `shutdown`：正常退出。

事件：

- `accepted`
- `progress`
- `log`
- `completed`
- `failed`
- `cancelled`

所有消息包含 `protocol_major`、`protocol_minor`、`request_id`；同一 major 内忽略未知字段并保持向后兼容。不要把本地化中文错误文本作为协议判断条件，协议传稳定错误码和结构化参数，UI 再本地化。

## 模型 Catalog 与模型包

### Catalog

服务器发布签名 Catalog。新增同架构模型只能通过更新 Catalog 和模型包完成，主程序与 Worker 无需重新发布。

模型下载源规则：

- 默认且优先指向 **模型作者或项目方可验证的 Hugging Face 官方开源仓库**。
- Catalog 必须保存 Hugging Face `repo_id`、仓库类型、不可变 commit revision、文件路径、文件字节数和 SHA-256；生产下载不得跟随可变的 `main` 分支。
- 一个模型需要多个权重或配置时，“下载该模型”一次下载该 revision 下 manifest 声明的全部必需文件。例如 `htdemucs_ft` 是 4 个权重加 YAML 的逻辑模型包，不能只下载 YAML。
- 如果上游没有官方 Hugging Face 仓库，只能使用模型作者/项目方的官方 GitHub、Meta/CDN 等一手发布地址，或暂不提供一键下载；不得把社区镜像标记成“官方”。
- 第三方镜像只能作为用户主动开启的备用源，并明确标记“非官方镜像”；首版不实现备用镜像。
- Catalog 只负责描述和签名来源，不代理、重打包或镜像权重，除非逐模型许可证明确允许。

每个模型至少包含：

```text
model_id
version
display_name
family                 demucs | mdx | vr
ui_group
scene_hint
stems[]
source_provider        huggingface | github | publisher_cdn
repository_id
repository_revision   immutable commit SHA
artifacts[]            url/path/bytes/sha256/relative_path/archive_type
download_bytes
installed_bytes
publisher
source_url
source_verified
model_license
license_url
engine_candidates[]
engine_min_version
profile_schema_version
sample_rate
channels
segment
overlap
shifts
stft/profile parameters
validation_status
validated_platforms[]
```

Catalog 与更新 manifest 使用 HTTPS、Ed25519 签名和固定公钥。签名、固定 revision、逐文件哈希、大小、许可证、官方来源验证或兼容字段缺失时，模型不得进入一键下载列表。

### 模型压缩包

- UI 把一次安装所需的全部官方文件称为一个“模型包”，但不要假设上游必然提供单一 ZIP。
- 官方 Hugging Face 仓库提供压缩包时，下载并验证该压缩包；提供多个独立文件时，一键下载 manifest 中的全部 artifacts，完成后组装到同一个模型版本目录。
- 所有文件先下载到 `downloads/<model-id>/<version>/*.partial`，支持 Range 断点续传和逐文件重试。
- 压缩文件解压前校验 SHA-256；解压时防目录穿越、符号链接、解压炸弹和覆盖现有版本。
- 下载/解压到临时目录，逐文件校验并验证必需文件集合后，原子移动到正式目录。
- Hugging Face LFS/Xet 重定向必须限制到允许的官方域名，并保持原始 `repo_id + revision + path` 审计记录。
- 模型权重通常已经高度压缩，UI 必须显示所有 artifacts 的真实总大小，不承诺明显压缩率。

### 手动模型

- 用户手动放入 `models/inbox`。
- 哈希命中 Catalog 或带有可信 sidecar manifest 才能进入已安装列表。
- 未识别文件显示“无法识别”，不得猜测参数或调用 `torch.load`。

## 推荐目录

```text
<UserData>/AgPlayer/separation/
  host/<version>/
  engines/<engine-id>/<version>/
  models/<model-id>/<version>/
  models/inbox/
  cache/<source-hash>/<profile-hash>/
  downloads/*.partial
  logs/
  active.json
```

- Windows 默认 `<UserData>` 为 `%LOCALAPPDATA%`。
- 便携版可使用 `<AgPlayer>/data/separation`，但必须先确认可写。
- 用户可以把 `models` 和 `cache` 移到其他磁盘。
- 路径从当前应用根目录和配置解析，不保存开发机或旧目录的绝对路径。

## 首次使用流程

1. 用户打开现有“音频工具”，看到总计 5 个工具，并点击第五项“人声/伴奏分离”。
2. 主程序执行 `probe`。
3. Worker Host 或稳定 Compatibility Engine 缺失时，提示需要下载插件，并展示准确下载大小、磁盘占用、版本、来源、许可证和路径。
4. 用户确认后下载、校验、安装、启动并完成 `hello/probe`；只有握手成功才显示插件可用。
5. 页面提示“模型需要按需下载，只下载你需要的即可”。
6. 简化页面默认选中但置灰 `htdemucs_ft`，用户点击“下载该模型”。
7. 下载器从 Catalog 中已验证的官方 Hugging Face 仓库固定 revision 获取全部必需 artifacts；若该模型没有官方 Hugging Face 源，则按 Catalog 使用已验证的一手官方源。
8. 模型文件下载、校验、原子安装，Worker 完成真实加载探测。
9. 用户选择计算设备并开始分离。
10. Worker 输出真实进度；完成后主程序验证输出并写入缓存。
11. 播放器允许在原曲、人声、伴奏之间共享时间轴切换，并提供导出。

## 缓存与结果

- 缓存键至少包含源音频完整 SHA-256、模型 SHA-256、Engine 版本和全部影响输出的参数。
- Worker 输出到 staging 目录；验证文件可解码、stem 数量、采样率、时长和非空后再原子提交。
- 默认缓存格式使用无损 FLAC；基准测试可保留 32-bit float WAV。
- 原曲、人声、伴奏共享播放位置；切换使用短交叉淡化，不重新起播。
- 删除模型不能破坏已生成的分离缓存；清理缓存和卸载模型是两个独立动作。

## 性能要求

- 默认同时只运行一个分离任务。
- Worker 使用低于正常优先级和有界线程数，避免抢占实时播放。
- 按 segment 处理，禁止把整曲 PCM 一次性加载到内存。
- UI、播放、下载、解压和推理线程彻底分离。
- 记录模型加载时间、首个进度时间、整曲耗时、峰值 RAM/VRAM、平均 CPU/GPU 利用率。
- 主程序未安装插件时，启动时间、空闲内存和基础安装包体积不得因 AI 依赖显著增加。

## 安全与恢复

- Worker 默认禁网；网络下载由主程序受控下载模块完成。
- Worker 只获得输入文件只读权限和任务 staging 目录写权限。
- 下载失败、签名错误、哈希错误、磁盘不足、无权限、取消和崩溃都必须有稳定错误码。
- Worker 异常退出后播放器保持正常播放，显示可重试错误，并清理或保留可恢复的 staging 状态。
- 更新采用 side-by-side 目录、原子 `active.json` 和上一版本回滚。
- 日志不得记录用户完整媒体路径、令牌、下载签名密钥或敏感配置。

## 实施顺序

不要把全部功能塞进一个提交。先根据仓库真实结构写详细实施计划，拆成以下可独立验收阶段：

### 阶段 1：协议、状态机和测试替身

- 定义纯 C++17 数据类型、稳定错误码、C ABI 和 Worker 协议。
- 创建只供自动化测试的 Fake Engine，不出现在生产 UI。
- 覆盖安装状态、取消、超时、崩溃和协议不兼容测试。

### 阶段 2：Catalog 与模型管理

- 实现签名 Catalog、Hugging Face 官方仓库 revision 固定、官方源验证、单模型多 artifact 下载、断点续传、校验、安全解压、原子安装、删除和重新校验。
- 用本地测试服务器证明：只更新 Catalog/同 profile 模型包，播放器和 Worker 二进制不变即可出现新模型。
- 用未知 profile 测试证明其被拒绝并提示需要新 Engine。

### 阶段 3：QML 简化/高级 UI

- 在现有音频工具窗口新增第五个 `人声/伴奏分离` 页面，并证明原有四个工具没有行为回归。
- 完成默认 `htdemucs_ft`、三组高级模型、状态、进度、设备选择和全部中文文案。
- 无 Worker、无模型、无 GPU、下载失败等状态必须真实可操作。
- 做键盘、屏幕阅读器、缩放和不同窗口尺寸检查。

### 阶段 4：Compatibility Engine

- 接入外置 Demucs/PyTorch Engine，真实支持 `htdemucs_ft` CPU。
- 再提供独立 NVIDIA CUDA Engine Pack；强制 GPU、自动选择和 CPU 回退分别测试。
- 模型权重与运行时彻底分离。

### 阶段 5：播放与缓存闭环

- 真实歌曲分离。
- 验证缓存命中、原曲/人声/伴奏同步切换、导出、删除模型后缓存仍可播放。
- Worker 崩溃时播放器继续播放。

### 阶段 6：Native ORT 实验路线

- 转换并验证候选模型。
- 与官方实现比较逐 stem 数值、整曲边界、听感、耗时和内存。
- 未通过全部闸门时保持实验性，不进入默认新手路径。

### 阶段 7：跨平台

- Windows 10/11 验收后再做 macOS/Linux。
- Android/iOS 复用 C ABI，但采用各自生命周期 Adapter。
- HarmonyOS 必须先在 arm64 真机完成推理运行时、内存、温升、后台限制和 N-API POC，之后才列为支持平台。

## 测试与发布闸门

至少覆盖：

- 音频工具从 4 项准确变为 5 项，第五项可真实进入分离页面；原有 4 项的选择、状态和功能无回归。
- 第一次点击分离入口时，缺少插件会显示真实安装弹框；取消后可恢复，安装并完成 Worker 握手后才能继续。
- 插件可用但没有模型时显示按需下载提示，不自动下载任何模型。
- 默认简化模式只显示标准模型。
- 展开/收起高级模型与真实选择一致。
- 三个分组、三条默认场景文案完全正确。
- 未下载模型置灰但下载按钮可用。
- 单模型下载，不触发其他模型下载。
- Hugging Face 下载固定不可变 revision，逐 artifact 校验 SHA-256，并保留 `repo_id/revision/path` 审计信息。
- `htdemucs_ft` 一次下载完整 4 权重与 YAML；缺失任何必需 artifact 时不得标记已安装。
- 没有官方 Hugging Face 源的模型只能使用验证过的一手官方源，第三方镜像不得显示“官方”。
- Catalog 新增同 profile 模型无需重建主程序/Worker。
- 未知 profile 被明确拒绝。
- CPU 强制运行。
- 自动模式 GPU 成功、GPU 失败回退 CPU。
- 强制 GPU 失败不静默回退。
- Worker 崩溃不影响播放器。
- 下载中断、继续、取消、错误签名、错误哈希、解压攻击、磁盘不足、无权限。
- 缓存键随模型/Engine/参数变化。
- 真实歌曲输出可播放、时长对齐、stem 切换不跳时轴。
- Windows 10/11 真机与实际声卡播放。
- 主程序基础包体积、启动时间和空闲内存对比。
- Release 构建零错误零警告。

完成条件不是“编译成功”或“CLI 输出文件”。必须同时具备：自动化测试记录、真实 UI 截图、真实音频分离日志、CPU/GPU 设备证据、播放器内切换与导出证据。

## 明确禁止

- 不允许把全部 UVR 文件扩展名当作“全部兼容”。
- 不允许把 Trama 当作独立模型家族；正确名称是 `htdemucs_ft（Trama 同款）`。
- 不允许在 QML 中硬编码未来模型清单。
- 不允许主程序直接链接 PyTorch、CUDA 或模型推理代码。
- 不允许未下载模型仍可点击“开始分离”。
- 不允许下载按钮一次性下载全套模型。
- 不允许跟随 Hugging Face 可变 `main` 分支作为生产模型版本。
- 不允许把第三方 Hugging Face 用户仓库或社区镜像标记为官方模型仓库。
- 不允许为了强制使用 Hugging Face 而放弃模型作者真实的一手官方来源；没有可验证官方来源时不提供一键下载。
- 不允许用固定绝对路径寻找 Worker、Engine 或模型。
- 不允许打包未经许可确认的权重。
- 不允许为了赶进度制作假成功、假进度、占位页面或跳过真实音频验收。

## Codex 工作方式与交付格式

1. 先报告真实仓库结构、当前实现切入点、未提交修改和风险。
2. 写出按文件、测试和小提交拆分的实施计划；不得留下任何占位词、待定项或空泛步骤。
3. 采用测试先行：失败测试 → 最小实现 → 通过 → 重构 → 验证。
4. 每个阶段完成后做独立复核，不跨阶段宣称完成。
5. 不修改无关文件，不覆盖用户改动，不在用户确认 UI 与真实硬件 QA 前打包 EXE。
6. 最终汇报：实际实现、测试结果、真实包体积、CPU/GPU 验证矩阵、尚未支持内容、许可证状态和下一阶段。

如果仓库现状与本提示词冲突，优先保留已验证的现有行为，并明确报告冲突；不得静默改变产品要求。
