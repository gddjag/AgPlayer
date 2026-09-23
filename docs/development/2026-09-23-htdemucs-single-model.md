# 五轨分离改用 HTDemucs FP16 单模型

当前内置五轨模型为 `htdemucs-fp16`，显示名称 **HTDemucs FP16**。模型卡、官方下载、HF-Mirror 备用线路、本地检测、一键配置、Worker 校验和实际推理统一使用以下文件。

| 项目 | 内容 |
| --- | --- |
| 文件 | `htdemucs_fp16weights.onnx` |
| 大小 | 165,612,636 字节，按十进制约 166 MB |
| SHA-256 | `d05c269d0178d2a72ad484b10b11dd370193fc923201c3b27a99f848745db70a` |
| 原模型 / ONNX 转换 | Meta Demucs / StemSplit |
| 官方仓库 | https://huggingface.co/StemSplitio/htdemucs-onnx |
| 固定版本下载 | https://huggingface.co/StemSplitio/htdemucs-onnx/resolve/d54ed9eb60e258ea82131c6ee14578628816456a/htdemucs_fp16weights.onnx |
| 备用下载 | https://hf-mirror.com/StemSplitio/htdemucs-onnx/resolve/d54ed9eb60e258ea82131c6ee14578628816456a/htdemucs_fp16weights.onnx |

## 使用与适配

- 在五轨模型卡点击下载或一键配置。下载只包含一个模型文件，校验完整大小和 SHA-256；已有合格文件会复用。HF-Mirror 保留相同固定版本路径和校验要求。
- 自行下载时保留原文件名，放入模型根目录或任意分类子目录，点击“检测”；内置模型不需要额外 sidecar。
- 自定义相同张量契约的 ONNX sidecar 使用 `profile: "htdemucs-fp16"`，只声明一个文件，输入 `shape: [1, 2, 343980]`，不声明单独声部 `role`。Worker 仍会检查文件指纹、opset、输入输出名称、类型及形状，不凭文件名执行未知模型。
- 标准模型每块只推理一次，输出顺序为鼓组、贝斯、其他、人声；伴奏由前三轨相加得到。仍提供人声、伴奏、鼓组、贝斯、其他五轨导出。
- 44.1 kHz、双声道、7.8 秒输入块，重叠 85,995 帧；末块补零，各声部独立重叠相加，导出采用共同增益维持各轨相对音量。FP16 是存储权重，模型输入输出仍为 float32。
- Windows 保留 CPU / NVIDIA CUDA 选择与按需配置；新模型尚未验证 DirectML 资源占用，因此延续 Demucs 的 CPU/CUDA 策略。macOS 共用模型和推理代码，Intel 可用 CPU，Apple Silicon 的 CoreML 必须逐模型验证通过后才启用。
- 模型和运行时继续外置下载，不加入主安装包。166 MB 为模型文件大小，不代表运行内存或 CUDA 组件大小。

旧 FT 四文件组合不再作为内置下载项；旧文件不删除，已有自定义 FT sidecar 保留兼容执行支持。以前文档中的 FT 性能、听感和 CUDA 实测记录属于历史版本，不能作为本单模型的验证证据。

## 本次验证

- Windows Release 增量构建应用、Worker 和相关测试通过。
- 安装/下载校验、DSP、受信模型契约、原生后端、输出事务、控制器测试通过；Windows 分离页面及 macOS 帮助链接 QML 测试通过。
- 官方固定版本文件已实际下载，大小和 SHA-256 匹配。
- 真模型 Windows CPU：12 秒音乐（两块，覆盖重叠与末块补零）导出五个 529,200 帧、44.1 kHz 双声道 WAV，均为有限样本。伴奏与三个导出乐器声部之和的最大误差为 `3.05176e-05`，处于 PCM16 量化范围内。日志 `build/htdemucs-single/real-cpu-evidence.txt`。
- 真模型 Windows 自动模式：2 秒音乐（单块补零）选择 CPU 并完成五轨导出，同样通过声部相加检查。日志 `build/htdemucs-single/real-short-auto.txt`。
- 本地单文件重复配置和自定义单文件 sidecar 分别验证；运行时与进程使用测试替身，实际推理由上述真模型测试单独证明。
- 未验证新模型的 CUDA / macOS 真机推理及整曲听感；不将历史 FT 测试结果或模型发布者的性能数字作为本次速度结论。

本轮未推送、打包或发布。
