# 本地分离提速与沉浸内发光检查

日期：2026-09-23。基于 `codex/pc-six-track-editor` / `0f27c2d2`。
本轮只修改分离 Worker；视觉部分只检查。频谱间距修改作为独立改动保留。本轮未推送、打包或发布。

## 分离改动

`worker/src/native_worker_backend.cpp` 原先在设备验证中加载模型、执行一次零输入推理后销毁会话，正式分离又创建同一模型会话。

现在同一次任务保留通过验证的会话，正式分离重新读取并验证模型 SHA-256 后接续使用；哈希不符则释放会话、按现有验证路径重新加载。每次重新探测设备均释放旧会话，GPU 失败后的 CPU 回退不会复用 GPU 会话。任务退出时自动释放，不添加进程间缓存、常驻服务、模型或依赖。

- MDX：Windows CPU/DirectML 与 macOS CPU/CoreML 使用相同复用逻辑。
- Demucs：Windows 可复用首个已验证模型会话，其他模型继续顺序加载。macOS 仍逐模型验证，多个模型的探测不保留会话，避免额外并存的大模型内存。
- 模型、精度、线程数、分块、重叠和输出参数不变；保留 HTDemucs 的 DirectML 限制、CUDA 独立运行时选择、取消和事务输出逻辑。

## 实测

Windows，i5-13490F / RTX 4070 Ti SUPER，Release，ONNX Runtime DirectML 1.24.4，HQ3。输入为固定原版 `demo.mp3` 前 12 秒，降至 0.5 音量留出峰值余量，44.1 kHz 双声道 WAV，覆盖 3 个重叠分块。各配置独立串行运行 3 次，计时期间无并行构建或视觉测试。

计时来自既有 `separation_real_model_test`，包括模型校验、设备验证、分离、文件导出与结果解码检查，不是单个算子的推理耗时，也不是 UI 任务墙钟时间。

| 设备 | 修改前，毫秒 | 修改后，毫秒 | 中位数变化 |
|---|---|---|---|
| DirectML GPU | 3658 / 3623 / 3624 | 3061 / 3064 / 3030 | 3624 → 3061，缩短 15.5% |
| CPU | 14284 / 14117 / 13664 | 13460 / 13601 / 13306 | 14117 → 13460，缩短 4.7% |

同一设备下，修改前后各 3 次生成的伴奏、人声文件分别具有相同 SHA-256。12 次真实模型测试全部通过，包含帧数、双声道、非静音、有限值和重建误差检查。此次收益主要来自减少重复加载和冷启动，不能按比例外推到完整长歌曲或所有模型。

首次未衰减输入在修改前触发既有 HQ3 重建误差断言：0.000139754 > 0.0001，伴奏峰值达到 1，表现与输出削波一致。后续 A/B 使用同一有余量输入，没有放宽断言；高电平导出问题另记，不包含在本次提速修复中。

证据（相对工作区）：

- `build/separation-speed-before-{cpu,gpu}-{1,2,3}.txt`
- `build/separation-speed-after-{cpu,gpu}-{1,2,3}.txt`
- `build/separation-speed-output-hashes.json`
- `build/measure-separation-speed.ps1`：本机复测命令及输入路径。
- `build/separation-speed-native-test.txt`：18 通过、1 跳过（macOS 专属），无失败。
- `build/separation-speed-runtime-test.txt`：14 通过，无失败。
- Worker 与 AgPlayer Release 增量构建通过。

未实测 macOS、CUDA、Python VR 或完整 Demucs 任务的提速；本机应用模型目录的 Demucs vocals 文件仍为 `.part`，未擅自下载或改动安装资源。没有实际听音或应用按钮端到端验收。

## 内发光与原版的对应关系

原版为本机固定快照 `D:/ai/agplayer-reference-ec8-20260910/pristine/sonic-topography-ec8ecbaec0c9c5094b6b1480df0d6b2d32d6349b`。

| 项目 | 结论与源码 |
|---|---|
| 实际启用 | `qt/src/terrain_reactor_item.cpp` 的 `buildUniforms()` 给主题材质传入运行标记；`qt/shaders/terrain_reactor.frag` 的 `terrainMaterial()` 进入 `referenceMode`。不是只有测试才进入原版材质路径。 |
| 内发光主体 | 原版 `src/components/AudioVisualizer/CustomShaderMaterial.ts` 使用随高度变化的自发光颜色、顶部亮边、侧面从顶向下的渐隐、尖锐度收缩和波纹颜色。当前 referenceMode 对应这些公式。原版本身也不是实际体积透射或折射材质。 |
| 额外照明 | referenceMode 提前返回，不叠加后续自定义体积光、棚灯与柱间照明。原版主题下 `app/qml/AgPlayer/components/ImmersiveSurface.qml` 不创建额外的 `ImmersiveColumnGlow`；这符合原版材质路径，并非漏启用内发光。 |
| 自定义材质 | 无原版主题时仍走自定义材质与可选光晕，其效果不属于原版一致性范围。 |

## 本轮画面对照与尚缺内容

使用原版真实音频快照：霓虹东京、冰川白昼，各 1 秒和 2 秒位置；同一相机、uniform、实例排列，1920×1080，D3D11。回放启用 `AGPLAYER_PARITY_RUNTIME_MATERIAL=defaults`。本轮未重新采集原版音频，而是复用带固定提交、样本位置及音频来源记录的已有快照。

- 1 倍采样的 4 次回放通过。实际截图内发光主体接近，未见缺失整套内发光效果。双方不透明区域的平均 RGB 通道差为霓虹 1.32–1.35、冰川 2.25–2.39（0–255），局部亮边、随机高光及透明覆盖仍有差异。采样、后端光栅化和随机计算可影响这些差异，未逐项归因，不能把均值换算为“相似度百分比”。
- 生产原版主题使用 4 倍采样。补测 `neon-tokyo-48000` 的 4 倍采样静态重复回放未通过逐字节一致断言：2073600 像素中 88 个变化，87 个差不超过 1 色阶，最大差 2 色阶（含 alpha）。证据保留；不能将其夸大为肉眼卡帧，也不能宣称 4 倍采样完全稳定。未因失败修改或放宽测试。
- 相关材质测试：18 通过、1 跳过，无失败；`referenceModeDoesNotUseLegacyInteriorLight` 因未提供 `AGPLAYER_SUBBASS_REFERENCE` 专用参考夹具跳过。已执行部分覆盖主题底色、抬升发光、侧壁渐隐、配色、高频顶面闪光和无用阴影跳过合同。
- 尚缺完整歌曲逐帧同步对照，包括强鼓点、密集高频、流星白波、主题切换；当前快照回放绕过了 AgPlayer 实时音频描述量提取，不能代表该实时链路已与原版完全同步。
- 尚缺 macOS Metal 真机对照。本轮未修改任何沉浸视觉代码。

视觉证据：`build/glow-audit/comparison.json`，`build/glow-audit/*-compare.png`（左原版、右当前回放）；`build/glow-audit-material-tests.txt`；`build/glow-audit-4x-neon-tokyo-48000.txt` 及对应 `native-u-manifest.json`。
