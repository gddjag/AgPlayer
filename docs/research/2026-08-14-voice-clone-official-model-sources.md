# 人声克隆官方模型源与许可核验（2026-08-14）

## 结论

四个模型均有官方公开仓库。Qwen3-TTS 与 Fun-CosyVoice3 的模型/代码标注 Apache-2.0；IndexTTS-2.5 主模型使用 bilibili 自定义许可，且官方推理代码引用的 MaskGCT 资产为 CC-BY-NC-4.0，因此 Index 组合不能作为商业用途模型提供，除非使用者另获授权。

## 固定身份

| 模型 | 官方模型仓库 | 固定 commit | 文件数 / 字节 |
| --- | --- | --- | ---: |
| Qwen3-TTS 0.6B Base | [Qwen/Qwen3-TTS-12Hz-0.6B-Base](https://huggingface.co/Qwen/Qwen3-TTS-12Hz-0.6B-Base) | `5d83992436eae1d760afd27aff78a71d676296fc` | 13 / 2,516,106,051 |
| Qwen3-TTS 1.7B Base | [Qwen/Qwen3-TTS-12Hz-1.7B-Base](https://huggingface.co/Qwen/Qwen3-TTS-12Hz-1.7B-Base) | `fd4b254389122332181a7c3db7f27e918eec64e3` | 13 / 4,544,229,700 |
| IndexTTS-2.5（含辅助资产） | [IndexTeam/IndexTTS-2.5](https://huggingface.co/IndexTeam/IndexTTS-2.5) | `c39ce5ba981572cb187443877ff559dfb246ce63` | 32 / 10,796,836,165 |
| Fun-CosyVoice3 | [FunAudioLLM/Fun-CosyVoice3-0.5B-2512](https://huggingface.co/FunAudioLLM/Fun-CosyVoice3-0.5B-2512) | `29e01c4e8d000f4bcd70751be16fa94bf3d85a18` | 20 / 9,747,516,745 |

文件图来自 Hugging Face 官方模型 API 的 pinned sibling 列表。Git LFS 文件 SHA-256 使用官方 LFS 对象身份；Git 管理的小文件从 immutable `resolve/<commit>/...` 下载并计算真实 SHA-256。每个结果同时核对 API `size`，没有下载或伪造权重哈希。

## Index 官方辅助资产

Index 官方代码固定点 [model_download.py](https://github.com/index-tts/index-tts/blob/4f8792ff120cd3ea470dd511e997a17c86cddd10/indextts/utils/model_download.py) 声明四类下载：

- [facebook/w2v-bert-2.0](https://huggingface.co/facebook/w2v-bert-2.0/tree/da985ba0987f70aaeb84a80f2851cfac8c697a7b)，完整 snapshot，MIT；
- [amphion/MaskGCT](https://huggingface.co/amphion/MaskGCT/tree/265c6cef07625665d0c28d2faafb1415562379dc)，`semantic_codec/model.safetensors`，CC-BY-NC-4.0；
- [funasr/campplus](https://huggingface.co/funasr/campplus/tree/e4b6ede7ce16997aff4ae69fbca1f0175e2afede)，`campplus_cn_common.bin`，Apache-2.0；
- [nvidia/bigvgan_v2_22khz_80band_256x](https://huggingface.co/nvidia/bigvgan_v2_22khz_80band_256x/tree/633ff708ed5b74903e86ff1298cf4a98e921c513)，配置与生成器权重，MIT。

许可集合保持各自身份。需要显式接受的是 [Index 主模型许可](https://huggingface.co/IndexTeam/IndexTTS-2.5/blob/c39ce5ba981572cb187443877ff559dfb246ce63/LICENSE) 和 [MaskGCT CC-BY-NC-4.0 声明](https://huggingface.co/amphion/MaskGCT/blob/265c6cef07625665d0c28d2faafb1415562379dc/README.md)，没有把多份许可合成虚构的单一许可。

## 能力与硬件口径

- [Qwen3-TTS 官方项目](https://github.com/QwenLM/Qwen3-TTS/tree/022e286b98fbec7e1e916cb940cdf532cd9f488e) 和固定模型卡支持 10 种语言、短参考音频零样本克隆和流式生成。Base 模型卡不支持把“文字描述造音色”宣传为 Base 能力。
- [IndexTTS 官方项目](https://github.com/index-tts/index-tts/tree/4f8792ff120cd3ea470dd511e997a17c86cddd10) 和固定模型卡列出五种语言、情绪/语速/发音控制、22.05 kHz；上游给出约 6 GB 显存的估计，但这不是 AG Player 的实测最低配置。
- [CosyVoice 官方项目](https://github.com/FunAudioLLM/CosyVoice/tree/074ca6dc9e80a2f424f1f74b48bdd7d3fea531cc) 和固定模型卡列出 9 种语言、18 种以上中文方言/口音、跨语言零样本克隆、指令控制和流式输出。

“低配电脑 / 手机”“4–6 GB 显存”及所有设备上的确定延迟没有足够官方或本项目真机证据，不写入产品保证。
