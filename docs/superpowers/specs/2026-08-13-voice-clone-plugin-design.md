# AG Player 人声克隆多模型插件正式版设计

## 1. 目标与批准范围

在现有 Qt 6 / QML / C++17 音频工具中新增“人声克隆”，以用户提供的 `人声克隆.png`（1672×942）为视觉基准，以《AG Player「人声克隆」多模型插件正式版开发提示词》为功能基线。主播放器继续保持轻量；未安装或未使用插件时，不加载 Python、PyTorch、CUDA、模型或 AI Worker，不扫描模型目录，不占用额外显存。

首发 Registry 只包含：

1. `Qwen/Qwen3-TTS-12Hz-0.6B-Base`；
2. `Qwen/Qwen3-TTS-12Hz-1.7B-Base`；
3. `IndexTeam/IndexTTS-2.5`，标记“实验性 / 自定义许可”；
4. `FunAudioLLM/Fun-CosyVoice3-0.5B-2512`。

Microsoft VibeVoice-1.5B 已由用户明确取消：不得出现在 Registry、UI、下载器、文案、测试夹具或打包产物中。

本轮不打包播放器 EXE。交付独立的 Voice Clone Plugin ZIP；ZIP 不包含模型权重。插件完成、视觉交互、下载校验和真实短音频推理验收前，不形成播放器发布包。

## 2. 选择的实现路径

采用“轻量基础插件 + 共享/隔离 Runtime Pack + 独立模型包”方案：

- 基础插件包含 QML 页面、C++ Controller、Registry、包管理器、IPC 客户端、许可页面和 Worker 协议。
- Qwen 0.6B 与 1.7B 共用一个 Qwen Runtime Pack。
- IndexTTS-2.5 与 Fun-CosyVoice3 各自使用隔离 Runtime Pack，避免依赖冲突。
- Runtime、模型、插件三类包分别下载、校验、安装和卸载。
- AI 推理由独立 `AGVoiceWorker` 进程执行；播放器主进程不嵌入 Python 或推理框架。

不采用单一巨型 Python 环境，也不为每个模型复制完整公共运行时。新增能力优先复用 Qt 网络、文件、进程和本地 IPC，不引入第二套通用插件框架或网页 UI。

## 3. 用户流程

1. 用户打开“音频工具 > 人声克隆”。
2. 未安装基础插件时显示原生安装弹窗，不进入残缺工作区。
3. 安装器从 AG Player 网站下载签名的轻量插件包，校验清单、哈希、签名、平台和最低播放器版本后原子安装。
4. 页面加载 Registry。用户展开顶部模型列表，查看简介、能力、预计下载量、许可证、官方项目和官方仓库链接。
5. 未安装模型时点击“一键下载”；包管理器先解析官方仓库 Metadata 和 revision，再显示真实总大小。
6. 下载对应 Runtime Pack、模型及完整依赖图，支持暂停、恢复、重试和取消；完成后逐文件校验并原子提交。
7. IndexTTS-2.5 下载前必须展示 bilibili Model Use License 全文，并记录用户接受的许可版本、revision 与时间；只允许官方直下，不使用 AG Player CDN 镜像权重。
8. 用户导入参考人声和模型要求的参考文本，输入克隆文本；页面按当前 Adapter 的 Capability Schema 显示参数。
9. Controller 启动隔离 Worker，经 `QLocalSocket / QLocalServer` 发送 JSON-RPC 风格控制消息；音频通过临时 WAV 文件传递，不在 JSON 中传 Base64 PCM。
10. 生成完成后在页面试听、保存、重新生成、删除或发送到现有音频编辑器。

首次生成前要求用户确认只克隆本人或已获得明确授权的声音；本地保存确认状态，但不上传参考音频、文本或结果。

## 4. 模型 Registry 与能力

Registry 是唯一模型产品信息源。每条记录包含稳定 ID、显示名、提供商、参数规模、简介、标签、稳定/实验状态、官方项目页、Hugging Face、ModelScope、许可证名称和 URL、模型 revision、Runtime ID、依赖图、必需文件、哈希/ETag、硬件建议、参考音频规则、输出能力和 Capability Schema。下载大小来自官方仓库 Metadata 或已验证 revision，不写演示假值。

| 模型 | 产品定位 | 首发能力 | 官方来源与许可 |
| --- | --- | --- | --- |
| Qwen3-TTS 0.6B Base | 相对轻量的默认声音克隆 | 参考音频克隆、10 种语言、参考文本 ICL / x-vector-only、速度与采样高级参数以 Adapter 实测为准 | Qwen 官方 GitHub、HF、ModelScope；Apache-2.0 |
| Qwen3-TTS 1.7B Base | 较高质量声音克隆 | 与 0.6B 同一克隆工作流；更高质量档，具体延迟和显存仅显示实测结果 | Qwen 官方 GitHub、HF、ModelScope；Apache-2.0 |
| IndexTTS-2.5 | 有声书、影视剧的细腻情绪配音 | 中/英/日/西/阿、跨语言克隆、独立情绪、0.5–2.0 时长/语速、拼音/CMU/Kana 发音控制；只开放当前官方接口已验证能力 | IndexTTS 官方 GitHub、HF、ModelScope；bilibili Model Use License，实验性 |
| Fun-CosyVoice3 | 中文方言与短视频口播 | 9 种语言、18+ 中文方言/口音、跨语言 zero-shot 克隆、语言/方言/情绪/速度/音量指令、流式能力；按 Adapter 实测启用 | FunAudioLLM 官方 GitHub、HF、ModelScope；Apache-2.0 |

Qwen Base 不显示“文字造音色”；该能力属于独立 VoiceDesign 模型，本轮不加入。Qwen 0.6B 不宣传手机支持，也不承诺固定 4–6 GB 显存。任何论文、宣传页或模型族能力，只有在当前正式模型 revision 和 Adapter 上通过探测/测试后才能进入 Capability Schema。

QML 禁止按模型名称分支。字段统一为 `supported / unsupported`、`required / optional / unsupported`、枚举选项、范围、默认值和高级/基础分组；不支持项直接隐藏。

## 5. 下载、安装与供应链安全

### 5.1 包类型

- `voice-clone-plugin`：由 AG Player 网站发布，包含 UI、Controller、Worker 启动器、Registry 与许可文件。
- `runtime-qwen`、`runtime-indextts25`、`runtime-cosyvoice3`：由 AG Player 发布的自包含 Runtime Pack；普通用户无需安装 Python、uv、pip、CUDA Toolkit、Git 或 Git LFS。
- 模型包：默认从模型作者官方 Hugging Face / ModelScope 仓库按 revision 下载；不通过任意社区镜像。

### 5.2 Manifest

每个包 Manifest 至少包含 `schemaVersion`、包 ID、版本、平台、架构、最低播放器版本、下载源、revision、文件路径、字节数、SHA-256、压缩方式、安装根目录、依赖包、许可证、发布者和签名信息。模型依赖图必须列全 tokenizer、配置、辅助模型和 Adapter 所需资源，禁止首次推理时暗中补下载。

### 5.3 下载状态机

状态为：未安装、获取元数据、等待许可、检查磁盘、下载中、已暂停、校验中、安装中、已就绪、可更新、失败、损坏。采用 `.part` 临时文件与持久化断点信息；仅在字节数、哈希、文件清单和 Manifest 全部通过后原子改名。取消不得破坏已安装版本；更新失败继续使用旧版本。路径必须限制在插件便携根目录，拒绝绝对路径、`..`、链接/联接逃逸和重复目标。

下载前检查可用磁盘、目标目录权限、平台、CPU 架构和可用设备。错误显示真实阶段、URL、HTTP 状态、校验项和可重试性，不伪造进度或成功。

## 6. 进程、IPC 与任务生命周期

播放器侧 `VoiceCloneController` 负责 UI 状态、包状态、Worker 生命周期、请求验证、进度、取消、错误恢复和结果导入；不实现具体模型推理。

`AGVoiceWorker` 每次按选中 Runtime 启动，加载一个 Engine Adapter 和一个模型。控制消息包含协议版本、请求 ID、操作、模型 ID/revision、输入/输出路径、文本、参数与取消标记。事件包含阶段、真实进度或 indeterminate、结果和结构化错误。

阶段至少为：启动 Worker、握手、加载模型、分析参考人声、生成、编码、校验、完成。若引擎不能提供真实工作量，只显示不确定进度。取消须终止当前推理并清理临时输出；Worker 崩溃、OOM、协议不兼容或超时不得导致播放器退出，Controller 提供一次明确重启操作并释放进程资源。

模型切换先取消或完成当前任务，再请求 Worker 卸载；不能证明释放成功时直接结束 Worker，由操作系统回收显存和内存。

## 7. UI 与视觉结构

页面按 1672×942 参考图还原，并使用现有 `Theme`、图标库和字体系统；不生成新的装饰性图片，不用字符或自绘 SVG 冒充图标。

- 顶部工具导航插入“人声克隆”。现有工具顺序和索引通过稳定工具 ID 迁移，避免裸整数继续扩散。人声伴奏分离若尚未在当前集成分支存在，本轮不复制其实现；合并后按最终顺序显示。
- 模型条单独一行：折叠模型选择器、提供商/规模/状态/简介、主下载按钮与全局下载进度。下拉面板锚定选择器，最大高度 320–420 px，内部滚动；每张模型卡显示简介、功能、真实大小、安装状态、许可证与可点击官方链接。
- 主区为上部三列：参考人声、克隆文本、输出设置。参考区支持点击/拖放、文件详情、波形、试听、删除及按模型动态显示参考文本；文本区支持清空、导入 TXT、字符数；输出区提供 WAV/FLAC/MP3、采样率、声道、目录和突出但不过度放大的生成按钮。
- 中部“声音参数”固定容器由 Capability Schema 动态生成语言、方言、情绪、速度等基础控件；专业参数默认折叠到高级设置。
- 底部“生成结果”显示真实波形、播放进度、文件信息、发送到剪辑、重新生成、保存和删除。空状态不伪造示例结果。
- 首次安装弹窗、IndexTTS 许可弹窗、合法使用确认、错误详情和删除确认均使用 AG Player 原生深色样式。

参考分辨率逐区比对；同时验证 1280×720、1920×1080、4K 与 Windows 100%/125%/150%/200% 缩放。小窗口可收缩和内部滚动，不遮挡主操作、不出现横向溢出。

## 8. 与现有播放器集成

- `AudioToolsWindow` 新增 `VoiceClonePage`，其生命周期由现有音频工具窗口控制。
- QML 注册新增 `VoiceCloneController` 单例；测试 Harness 同步构造，不修改播放核心。
- “发送到剪辑”调用现有 `AudioEditorController.openFile()`，不实现第二套编辑器。
- 参考人声和生成结果试听复用现有预览/播放能力，进入页面不抢占当前播放；实际行为与现有音频工具约定一致。
- 插件、Runtime 和模型根目录相对便携根解析；不保存开发机绝对路径。
- 未安装插件时 Controller 只读取轻量 Manifest 状态，不启动 Worker 或加载模型 Registry 的重量级资源。

## 9. 许可、隐私与产品声明

- Qwen 与 CosyVoice 随 Runtime/插件保留 Apache-2.0、版权和 NOTICE；所有第三方依赖生成 `THIRD_PARTY_NOTICES`。
- IndexTTS-2.5 不在 AG Player 网站镜像权重；下载前显式接受当前 bilibili 许可，并保留官方许可入口。
- 模型页面显示“官方项目”“Hugging Face”“ModelScope”“许可证”可点击链接，来源标签不得暗示 AG Player 是模型作者。
- 参考音频、文本、声音特征缓存和输出默认只保存在本地；日志不得记录音频内容或完整生成文本。
- 删除模型时保留用户输出；删除参考特征缓存和结果必须分别确认。
- 合法使用确认和免责声明不能替代真实授权；首发不提供绕过水印、身份验证或远程共享功能。

## 10. 独立插件包

插件 ZIP 采用稳定根目录：`VoiceClonePlugin/plugin`、`qml`、`bin`、`worker`、`registry`、`licenses`、`config`。Runtime Pack 和模型安装到外部 `runtime/<runtime-id>/<version>` 与 `models/voice-clone/<model-id>/<revision>`，不重复塞入基础 ZIP。

打包脚本从干净暂存目录收集白名单文件，生成 Manifest、SHA-256 清单、第三方许可清单和版本信息，再创建 ZIP；验证解压路径、必需文件、重复文件、意外模型权重、开发路径和敏感文件。产物名称包含插件版本、平台和架构。

当前阶段允许生成供测试/网站上传的插件 ZIP，但不生成或替换 AG Player EXE，也不自动上传网站。

## 11. 测试与验收

严格先写失败测试再实现：

### 11.1 Registry 与包管理

- 四个且仅四个模型；任何 VibeVoice 字符串使测试失败。
- 官方 URL、模型 ID、许可证、稳定状态、简介和能力均完整；链接为 HTTPS 且 host/组织匹配白名单。
- Metadata 总大小、revision 固定、依赖图完整、断点续传、取消、重试、磁盘不足、哈希失败、路径穿越、更新回滚和原子安装。
- IndexTTS 未接受许可时不得下载；Qwen/CosyVoice 无自定义许可阻塞。

### 11.2 Controller、Worker 与 Adapter

- 未安装、安装、启动、握手、加载、生成、取消、崩溃、OOM、超时、重启和卸载状态转换。
- 协议版本和请求 ID 校验；非法路径/参数拒绝；临时 WAV 清理；无假百分比。
- 每个 Adapter 对固定 revision 生成真实 Capability；Qwen Base 不暴露 VoiceDesign，Index/CosyVoice 只开放当前 API 可用参数。
- Worker 退出后播放器仍可播放，且无残留进程、临时文件或持久显存占用。

### 11.3 QML 与视觉

- 首次安装、模型下拉、下载/暂停/恢复/失败、许可确认、文件拖放、动态参数、生成/停止、试听、保存和发送到剪辑。
- 模型切换后不支持控件消失，必填参考文本正确启用，按钮禁用原因可见。
- 以同一 1672×942 视口同时打开参考图与实现截图做对比；字体、间距、颜色、图标、复制内容和布局无 P0/P1/P2 差异，`design-qa.md` 最终必须为 `passed`。

### 11.4 真实端到端

- 下载并校验轻量插件测试包；至少完成暂停/恢复和损坏包拒绝测试。
- 对 Qwen 0.6B、Qwen 1.7B、IndexTTS-2.5、CosyVoice3 分别使用有授权的短参考 WAV 和短文本生成结果，报告硬件、Runtime/模型 revision、耗时、峰值内存/显存、输出格式和主观异常。
- 在 AG Player 内完成导入参考音频、生成、试听、保存和发送到剪辑；单独 CLI 成功不算应用验收。
- 完整构建、相关 C++/QML 测试与 Open Code Review 覆盖所有变更；高/中问题修复后重跑验证。

## 12. 完成定义与非目标

只有四个模型的官方链接、下载、校验、许可、真实 Adapter、UI 核心流程和端到端短音频测试全部通过，才能称“正式版人声克隆插件完成”。未具备本机硬件或下载条件时必须明确标为阻塞，不能用模拟 Worker 代替真实推理结论。

非目标：VibeVoice、VoiceDesign、预设音色市场、声音训练/微调、云推理、AI 聊天、网页/Gradio、节点工作流、自动网站上传、播放器核心重构、现有人声伴奏分离功能重写和播放器 EXE 打包。
