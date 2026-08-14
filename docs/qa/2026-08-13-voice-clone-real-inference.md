# 人声克隆真实推理验收（2026-08-14）

## 结论

状态：`pending_runtime_distribution`

本机硬件满足短音频验证条件，但截至本次预检，三套正式 Runtime 均未安装，三款模型未缓存，Qwen 1.7B 缓存也不完整。当前产品链只能下载模型权重，尚不能下载、校验并安装 Runtime；因此没有启动数 GB 下载，也没有把 QA Worker 的复制结果冒充真实推理。四模型正式版完成门禁尚未通过。

## 本机预检

| 项目 | 实测 |
|---|---|
| GPU | NVIDIA GeForce RTX 4070 Ti SUPER，16376 MiB，驱动 610.74 |
| RAM | 63.78 GiB；预检时可用 39.58 GiB |
| D 盘 | 可用 829.87 GiB |
| Python | uv 管理的 CPython 3.12.9、3.10 可用；这不等于插件 Runtime 已安装 |
| Runtime | `qwen-shared`、`cosyvoice3`、`indextts25` 的产品目录均不存在 |
| 预检记录 | `build/qa/voice-clone/real-inference-preflight.txt` |

## 官方模型与当前门禁

| 模型 | 固定 revision | 官方清单大小 | 本机状态 | 真实推理结果 |
|---|---|---:|---|---|
| Qwen3-TTS 0.6B Base | `5d83992436eae1d760afd27aff78a71d676296fc` | 2,516,106,051 B（2.343 GiB） | 模型缺失；`qwen-shared` Runtime 缺失 | 待 Runtime 分发链完成 |
| Qwen3-TTS 1.7B Base | `fd4b254389122332181a7c3db7f27e918eec64e3` | 4,544,229,700 B（4.232 GiB） | HF 缓存仅 3 文件、10,490,294 B，不可视为完整；Runtime 缺失 | 待 Runtime 分发链完成 |
| Fun-CosyVoice3 | `29e01c4e8d000f4bcd70751be16fa94bf3d85a18` | 9,747,516,745 B（9.078 GiB） | 模型与 `cosyvoice3` Runtime 均缺失 | 待 Runtime 分发链完成 |
| IndexTTS-2.5 | `c39ce5ba981572cb187443877ff559dfb246ce63` | 10,796,836,165 B（10.055 GiB） | 未取得用户对 bilibili 模型许可与 MaskGCT 许可的逐项明确接受；未下载 | `legal_gate_blocked` |

官方入口、许可证与固定 revision 见：

- `docs/research/2026-08-14-voice-clone-official-model-sources.md`
- `docs/voice-clone/README.zh-CN.md`
- `plugins/voice-clone/registry/downloads/*.json`

Runtime 配方已固定来源和依赖：

- Qwen：`plugins/voice-clone/runtime/qwen.lock.json`，官方代码 revision `022e286b98fbec7e1e916cb940cdf532cd9f488e`
- CosyVoice3：`plugins/voice-clone/runtime/cosyvoice3.lock.json`，官方代码 revision `074ca6dc9e80a2f424f1f74b48bdd7d3fea531cc`
- IndexTTS-2.5：`plugins/voice-clone/runtime/indextts25.lock.json`，官方代码 revision `4f8792ff120cd3ea470dd511e997a17c86cddd10`

## 阻塞证据

1. `VoiceCloneWorkspace.qml` 只调用 `VoiceCloneController::downloadModel(stableId)`。
2. `downloadModel` 只启动模型包清单下载；Controller 没有 Runtime 下载/安装入口。
3. 激活模型时若 Runtime 不存在，Controller 返回 `Adapter runtime is not installed or not ready`。
4. 独立插件包包含 Runtime lock、requirements 与离线构建脚本，但不包含已经构建的 Runtime。该脚本还要求调用者事先准备官方 Python/uv/源码/wheelhouse 缓存，不能替代面向用户的一键安装链。

Runtime 分发/安装链正在并行补齐；在其完成并通过哈希、版本、失败恢复验证前，本报告保持 pending。

## 已授权 QA 参考音频

- 来源：`D:\ai\AgPlayer\.worktrees\release-integration\build\qa\hardware-recording-30s.wav`
- 用途：只用于本机 QA 短参考音频及产品链 UI 状态验证。
- 属性：约 29.98 秒、48 kHz、立体声、24-bit PCM；峰值约 -30.27 dBFS，RMS 约 -56.75 dBFS。
- SHA-256：`D950198910B589974978010C5B877626EC2168261E851536A0E49974529ABA01`

QA Worker 已经通过真实 Host/Plugin/Controller/Worker 调用链生成 Controller 管理的 WAV，并验证参考/结果波形、保存、删除、发送编辑器等 UI 接线。其输出与输入字节一致，证明产品链和 populated 视觉状态，不证明任何模型推理、音色克隆质量或硬件播放。

## Runtime 补齐后的逐模型记录模板

每款模型必须通过 AG Player Controller/Plugin/Worker 实际链运行，并记录：

| 字段 | 必填证据 |
|---|---|
| 模型 | stable ID、固定 revision、清单校验结果 |
| Runtime | runtime ID、代码 revision、依赖锁摘要、ready marker 校验 |
| 设备 | GPU 名称、驱动、执行 provider/dtype |
| 性能 | Controller 生成起止耗时、进程峰值 Working Set、GPU 峰值显存 |
| 输出 | 文件路径、SHA-256、时长、采样率、声道、位深/编码 |
| 产品操作 | 导入参考、生成、应用内试音、保存、发送编辑器逐项结果 |
| 听感 | 由人工硬件试听记录失真、断裂、错读、音色相似度；未试听不得填写“通过” |

执行顺序：先校验剩余磁盘和官方清单总大小，再安装并验证 Runtime，然后按清单下载模型，最后用授权参考 WAV 裁取短片段完成生成。不得直接运行模型脚本绕过 AG Player，也不得以纯正弦波代替人声。

## 正式完成门禁

- [ ] Runtime 独立下载/校验/安装/恢复链通过
- [ ] Qwen3-TTS 0.6B 真实产品链推理与人工试听通过
- [ ] Qwen3-TTS 1.7B 真实产品链推理与人工试听通过
- [ ] Fun-CosyVoice3 真实产品链推理与人工试听通过
- [ ] 用户逐项接受 IndexTTS/bilibili 与 MaskGCT 许可
- [ ] IndexTTS-2.5 真实产品链推理与人工试听通过
- [ ] 四模型的性能、资源、输出、播放、保存、发送编辑器证据完整

在以上项目全部完成前，不得声明“正式版人声克隆插件完成”。
