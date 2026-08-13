# AG Player 人声克隆模型指南

模型和推理运行时均为按需下载，不进入播放器主安装包。下载器只接受官方仓库、固定 commit、逐文件 SHA-256 和大小完全匹配的清单；下载中断可续传，校验失败不会替换已安装版本。

## 官方模型

| 模型 | 适合用途与官方能力 | 完整下载 | 许可 | 官方链接 |
| --- | --- | ---: | --- | --- |
| Qwen3-TTS 0.6B Base | 相对轻量；10 种语言、短参考音频零样本人声克隆。适合先试音，但官方没有承诺手机端或 4–6 GB 显存下的稳定性能。 | 2.34 GiB | Apache-2.0 | [官方项目](https://github.com/QwenLM/Qwen3-TTS) · [Hugging Face](https://huggingface.co/Qwen/Qwen3-TTS-12Hz-0.6B-Base) · [ModelScope](https://www.modelscope.cn/models/Qwen/Qwen3-TTS-12Hz-0.6B-Base) |
| Qwen3-TTS 1.7B Base | 10 种语言、流式生成、零样本人声克隆；适合更重视质量和低延迟流式输出的场景。Base 模型不等于“文字造音色”，该能力属于官方 Design 模型系列。 | 4.23 GiB | Apache-2.0 | [官方项目](https://github.com/QwenLM/Qwen3-TTS) · [Hugging Face](https://huggingface.co/Qwen/Qwen3-TTS-12Hz-1.7B-Base) · [ModelScope](https://www.modelscope.cn/models/Qwen/Qwen3-TTS-12Hz-1.7B-Base) |
| IndexTTS-2.5 | 中文、英语、日语、西班牙语、阿拉伯语；情绪、语速和发音控制，22.05 kHz 输出。适合有声内容和细腻情绪，但上游长文本会分段，不能承诺整章韵律连续。 | 10.06 GiB | bilibili 自定义许可 + MaskGCT CC-BY-NC-4.0 等依赖许可 | [官方项目](https://github.com/index-tts/index-tts) · [Hugging Face](https://huggingface.co/IndexTeam/IndexTTS-2.5) · [ModelScope](https://modelscope.cn/models/IndexTeam/IndexTTS-2.5) |
| Fun-CosyVoice3 0.5B 2512 | 9 种语言、18 种以上中文方言/口音、跨语言零样本人声克隆、指令控制和流式输出；适合方言及短视频口播。 | 9.08 GiB | Apache-2.0 | [官方项目](https://github.com/FunAudioLLM/CosyVoice) · [Hugging Face](https://huggingface.co/FunAudioLLM/Fun-CosyVoice3-0.5B-2512) · [ModelScope](https://www.modelscope.cn/models/FunAudioLLM/Fun-CosyVoice3-0.5B-2512) |

下载大小是本版本固定文件图的总和，不是运行时显存。实际速度、内存和显存取决于运行设备、音频长度、精度及 Worker；未完成真机实测前，AG Player 不承诺最低配置。

## IndexTTS-2.5 的许可门禁

官方 IndexTTS 推理代码还依赖 `facebook/w2v-bert-2.0`、`amphion/MaskGCT`、`funasr/campplus` 和 `nvidia/bigvgan_v2_22khz_80band_256x`。AG Player 将这些官方固定版本全部提前列入下载清单，避免 Worker 首次运行时隐式拉取。

使用 IndexTTS-2.5 前必须逐项阅读并接受：

1. [bilibili Model Use License Agreement](https://huggingface.co/IndexTeam/IndexTTS-2.5/blob/c39ce5ba981572cb187443877ff559dfb246ce63/LICENSE)；
2. [MaskGCT CC-BY-NC-4.0](https://huggingface.co/amphion/MaskGCT/blob/265c6cef07625665d0c28d2faafb1415562379dc/README.md)。

MaskGCT 资产仅非商业；商业用途禁用，除非另获授权。许可确认按模型、Adapter、许可 ID、许可 URL 和固定 revision 分别记录；记录损坏时下载和生成均会关闭。

## 使用边界

- 只克隆你本人或已取得明确授权的声音；不要冒充他人或规避平台、法律及内容许可要求。
- Apache-2.0、MIT 只描述对应代码/资产的许可，不自动授予参考声音、输入文本或生成内容的第三方权利。
- “一键下载”固定的是模型资产；不同推理架构由独立 Worker/Adapter Pack 提供，高级参数由对应 Adapter 声明。
