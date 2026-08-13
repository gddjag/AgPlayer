# AG Player 人声克隆模型许可与定位审计

审计日期：2026-08-13  
范围：Qwen3-TTS 0.6B Base、Qwen3-TTS 1.7B Base、IndexTTS-2.5、Fun-CosyVoice3-0.5B-2512、Microsoft VibeVoice-1.5B  
方法：仅核对模型作者维护的 GitHub、Hugging Face、ModelScope 与项目自带许可证。本文是产品/工程合规审计，不代替正式法律意见。

## 结论

| 模型 | 免费获取 | 开源/源码可用判断 | 商用 | 在免费 AG Player 中按需下载 | 首发结论 |
|---|---|---|---|---|---|
| Qwen3-TTS 0.6B Base | 是 | 代码、权重均 Apache-2.0 | 允许 | 允许，保留许可与版权声明 | **纳入** |
| Qwen3-TTS 1.7B Base | 是 | 代码、权重均 Apache-2.0 | 允许 | 允许，保留许可与版权声明 | **纳入** |
| IndexTTS-2.5 | 是 | 自定义 `bilibili Model Use License`；不是标准宽松开源许可 | 有条件允许；超过许可规模阈值须另行书面授权 | 条款允许分发，但下游约束和责任很重 | **仅作为实验性、用户显式接受许可后从官方仓库直下；不镜像权重** |
| Fun-CosyVoice3-0.5B-2512 | 是 | 代码、权重均 Apache-2.0 | 允许 | 允许，保留许可与版权声明 | **纳入** |
| Microsoft VibeVoice-1.5B | 是 | 仓库及模型页标 MIT，但模型卡同时限定研究用途 | **不应按一般商用许可处理** | 许可表述冲突；且官方已移除 TTS 运行代码 | **不进入首发可用列表；仅保留“暂不可用/关注中”信息或直接移除** |

因此，“这五个都免费开源，可无条件装进免费软件”这个说法不成立。安全的首发组合是 **Qwen 两款 + Fun-CosyVoice3**；IndexTTS-2.5 需独立许可确认流程；VibeVoice-1.5B 暂缓。

## 1. Qwen3-TTS 0.6B Base

- **代码许可证：** Apache License 2.0。官方仓库的 [LICENSE](https://github.com/QwenLM/Qwen3-TTS/blob/main/LICENSE) 明确授予使用、修改、再许可与分发权。
- **模型权重许可证：** Apache-2.0。官方 [Hugging Face 模型页](https://huggingface.co/Qwen/Qwen3-TTS-12Hz-0.6B-Base) 标注 `apache-2.0`。
- **免费获取：** 是，官方 HF/ModelScope 均可直接下载，无付费门槛。
- **商用：** 允许。Apache-2.0 不限制商业使用。
- **再分发/按需下载：** 允许。AG Player 可让用户从官方仓库一键下载，也可依法镜像；产品上优先采用官方直下，以减少模型更新和供应链责任。
- **必须履行：** 随分发副本提供 Apache-2.0 文本；保留版权、专利、商标与署名声明；修改过的文件标明变更；若上游有 `NOTICE` 则一并保留。不得暗示获得 Qwen/Alibaba 商标授权。
- **能力与定位：** 官方将 Base 定义为“用户音频输入的 3 秒快速声音克隆”，支持 10 种语言与流式输出；适合 AG Player 的默认轻量克隆模型。
- **定位修正：** “低配电脑”可作为相对定位；**“手机快速试音”和“4–6 GB 显存”没有官方依据**。官方仓库未提供 Android/iOS/mobile 部署说明，也未给出 4–6 GB VRAM 保证，示例使用 CUDA、BF16 与 FlashAttention 2。UI 应显示“相对轻量/建议实测”，不能显示“支持手机”或固定显存承诺。
- **官方链接：** [项目](https://github.com/QwenLM/Qwen3-TTS) · [Hugging Face](https://huggingface.co/Qwen/Qwen3-TTS-12Hz-0.6B-Base) · [ModelScope](https://www.modelscope.cn/models/Qwen/Qwen3-TTS-12Hz-0.6B-Base)

## 2. Qwen3-TTS 1.7B Base

- **代码许可证：** Apache-2.0，[官方 LICENSE](https://github.com/QwenLM/Qwen3-TTS/blob/main/LICENSE)。
- **模型权重许可证：** Apache-2.0，[官方 Hugging Face 模型页](https://huggingface.co/Qwen/Qwen3-TTS-12Hz-1.7B-Base)。
- **免费获取、商用、再分发：** 均允许，义务与 0.6B 相同。
- **能力与定位：** 官方 Base 检查点支持 3 秒参考音频克隆、10 种语言和流式输出；更适合把它定位成“较高质量的实时/低延迟声音克隆”，但实际实时性必须由 AG Player Runtime Pack 在目标硬件上验证，不能只引用模型族的最低 97 ms 宣传值。
- **关键能力边界：**
  - `Base`：参考音频克隆，API 是 `generate_voice_clone`。
  - `VoiceDesign`：根据自然语言描述从零设计音色，API 是 `generate_voice_design`。
  - `CustomVoice`：9 个预设音色，并支持指令化风格控制，API 是 `generate_custom_voice`。
  - 因此当前确定的 **1.7B Base 不支持把“文字造音色”当作自身能力**。若产品确实要“文字造音色”，必须另加 `Qwen3-TTS-12Hz-1.7B-VoiceDesign` 作为独立可选模型；不能在 Base 的 Capability Schema 中开启该控件。
- **官方证据：** [官方模型矩阵与下载说明](https://github.com/QwenLM/Qwen3-TTS#released-models-description-and-download) · [Voice Clone 示例](https://github.com/QwenLM/Qwen3-TTS#voice-clone) · [Voice Design 示例](https://github.com/QwenLM/Qwen3-TTS#voice-design)
- **官方链接：** [项目](https://github.com/QwenLM/Qwen3-TTS) · [Hugging Face](https://huggingface.co/Qwen/Qwen3-TTS-12Hz-1.7B-Base) · [ModelScope](https://www.modelscope.cn/models/Qwen/Qwen3-TTS-12Hz-1.7B-Base)

## 3. IndexTTS-2.5

- **代码许可证：** 当前官方仓库根 [LICENSE](https://github.com/index-tts/index-tts/blob/main/LICENSE) 是自定义 `bilibili Model Use License Agreement`。其“Model”定义明确包含官方发布的 **模型权重和最终代码**，因此不能把当前代码简单写成 Apache-2.0。
- **模型权重许可证：** 同一自定义许可；HF 元数据明确为 `license: other` / `bilibili-model-license`，见 [模型许可证](https://huggingface.co/IndexTeam/IndexTTS-2.5/blob/main/LICENSE)。
- **免费获取：** 是；官方 HF/ModelScope 均为非 gated 下载。
- **商用：** 许可授予免版税的有限使用权，但若用户或关联方的产品/服务前一月 MAU 超过 1 亿，或上一年收入超过人民币 10 亿元，必须先取得单独书面许可。未达到阈值时没有单列“禁止商用”，但仍受全部用途、下游和责任条款约束。
- **再分发：** “Use”明确定义包含 distributing/publishing，因此原则上允许；但发布者必须确保下游接受同一协议并承担下游违约后果。对 AG Player 来说，镜像权重会显著扩大责任。
- **必须履行：** 每份模型/衍生品保留原版权声明与许可全文；用合适条款约束下游；衍生品发布时加入“不获原权利人背书/担保”声明；遵守违法内容、高风险场景、第三方数据/权重授权等限制；不得用其改进其他商业 AI 模型（许可列出的例外除外）。中文版本冲突时优先。
- **产品实现要求：** 仅提供“从官方仓库直接下载”；下载前展示完整许可并让用户主动勾选接受；保存接受的许可版本、模型 revision 与时间；许可入口长期可查；AG Player 官网不缓存、改包或镜像此权重，除非后续法律复核通过。
- **能力与定位：** 官方 2026-08-10 发布 2.5；约 0.8B GPT 主干，支持中/英/日/西/阿、跨语言克隆、独立情绪控制、0.5–2.0 时长/语速控制及发音控制。适合有声书和影视情绪配音，但模型卡说明长文本会分段拼接、跨段韵律不连续，不能宣传成无缝超长单次生成。
- **官方链接：** [项目](https://github.com/index-tts/index-tts) · [Hugging Face](https://huggingface.co/IndexTeam/IndexTTS-2.5) · [ModelScope](https://modelscope.cn/models/IndexTeam/IndexTTS-2.5) · [模型卡](https://huggingface.co/IndexTeam/IndexTTS-2.5/blob/main/README.md)

## 4. Fun-CosyVoice3-0.5B-2512

- **代码许可证：** Apache-2.0，[官方 LICENSE](https://github.com/FunAudioLLM/CosyVoice/blob/main/LICENSE)。
- **模型权重许可证：** Apache-2.0，[官方 HF 模型页](https://huggingface.co/FunAudioLLM/Fun-CosyVoice3-0.5B-2512)。
- **免费获取、商用、再分发：** 均允许；履行 Apache-2.0 的许可副本、声明保留、修改标注与 `NOTICE` 义务。
- **能力与定位：** 官方模型卡明确为 9 种常用语言、18+ 中文方言/口音、跨语言 zero-shot 克隆、语言/方言/情绪/语速/音量指令控制和双向流式输出，最低延迟宣传值为 150 ms。用户的“粤语/川渝/东北等方言、短视频口播”定位符合官方能力，但延迟仍需在 AG Player 目标硬件上实测。
- **风险：** 官方仓库免责声明称示例内容用于学术能力展示；正式许可证仍是 Apache-2.0。插件打包的 Python/PyTorch、ONNX、SoX、第三方子模块和可选 `ttsfrd` 需另做依赖许可证清单，不能用模型的 Apache-2.0 代替整个 Runtime Pack 的合规审计。
- **官方链接：** [项目](https://github.com/FunAudioLLM/CosyVoice) · [Hugging Face](https://huggingface.co/FunAudioLLM/Fun-CosyVoice3-0.5B-2512) · [ModelScope](https://www.modelscope.cn/models/FunAudioLLM/Fun-CosyVoice3-0.5B-2512)

## 5. Microsoft VibeVoice-1.5B

- **代码许可证：** 官方仓库根 [LICENSE](https://github.com/microsoft/VibeVoice/blob/main/LICENSE) 是 MIT。
- **模型权重许可证：** 官方 [HF 模型页](https://huggingface.co/microsoft/VibeVoice-1.5B) 元数据标 `mit`，但模型仓库没有独立 LICENSE 文件，只有模型卡。
- **许可冲突：** 同一官方模型卡又明确写明模型“limited to research purpose use”，并写出若干“not intended or licensed”用途；同时不建议在商业或真实应用中使用。该限制与页面的 MIT 元数据形成实质冲突。工程上不能选择更宽松的一段而忽略更严格的官方声明。
- **免费获取：** 权重目前仍公开、非 gated，可免费下载。
- **商用：** **暂不能判定为安全允许**。即使 MIT 文本通常允许商业使用，官方模型卡的研究用途限定和不授权表述足以阻止 AG Player 把它作为普通正式功能发布；需要 Microsoft 书面澄清。
- **再分发：** 暂不镜像、不随插件分发，也不在正式版自动拉取。
- **运行代码状态：** 官方在 2025-09-05 因发现滥用而移除了 VibeVoice-TTS 代码；当前官方 [TTS 文档](https://github.com/microsoft/VibeVoice/blob/main/docs/vibevoice-tts.md) 的“Installation and Usage”明确为 `Disabled due to widespread misuse`。权重仍在不等于存在可维护、可验证的官方集成路径。
- **能力与定位：** 模型理论定位很吻合“长篇播客/多角色对话”：官方称单次最长约 90 分钟、最多 4 个说话人；但中文稳定性较弱，官方建议只用英文标点并分段，且安装/推理入口已禁用。它不是当前可交付的人声克隆适配器。
- **产品处理：** 首发删除下载按钮和“已支持”状态；若希望保留路线图，只显示“实验模型，等待官方恢复推理支持与许可澄清”。不得接入社区 fork 代替官方来源。
- **官方链接：** [项目](https://github.com/microsoft/VibeVoice) · [Hugging Face](https://huggingface.co/microsoft/VibeVoice-1.5B) · ModelScope：未发现 Microsoft 官方发布页。

## 面向 AG Player 的最终模型文案

| 模型 | 可用的产品文案 | 禁止或需修正的文案 |
|---|---|---|
| Qwen3-TTS 0.6B Base | 相对轻量、3 秒参考音频克隆、10 语言、流式输出 | 不写“手机支持”；不写未经实测的“4–6G 显存” |
| Qwen3-TTS 1.7B Base | 较高质量参考音频克隆、10 语言、流式输出 | 不写“文字造音色”；那是 1.7B VoiceDesign，不是 Base |
| IndexTTS-2.5 | 细腻情绪克隆、独立情绪控制、五语言、语速与发音控制 | 不写“无缝超长”；标“实验性/自定义许可” |
| Fun-CosyVoice3 | 9 语言、18+ 中文方言/口音、跨语言克隆、指令控制、流式 | 不能把官方最低延迟当作所有电脑保证 |
| VibeVoice-1.5B | 路线图：超长播客、最多 4 人对话 | 当前不能显示“一键下载/已支持”；不能承诺正式商用 |

## 下载与发布规则

1. 插件本体可单独上传 AG Player 官网，但模型默认从作者官方仓库直下；Registry 同时记录官方 URL、revision/commit、文件清单、大小、hash/ETag 与许可证 URL。
2. Qwen/CosyVoice 下载前显示 Apache-2.0 摘要并提供全文入口；随插件的第三方运行库另生成 `THIRD_PARTY_NOTICES`。
3. IndexTTS-2.5 使用独立确认页，保存用户接受的许可版本；不在 AG Player CDN 镜像权重。
4. VibeVoice-1.5B 保持禁用，直到 Microsoft 恢复官方 TTS 推理路径并书面澄清模型卡与 MIT 元数据的冲突。
5. 所有模型首次使用时继续要求“仅克隆本人或已取得明确授权的声音”，并在本地记录确认；这不替代用户对肖像权、声音权、隐私和内容合法性的责任。

