# AgPlayer 原生人声伴奏分离验收记录

日期：2026-08-30

分支：`codex/vocal-separation-native`

平台：Windows x64、Qt 6.7、VS2022 Release、ONNX Runtime DirectML 1.24.4

## 结论

功能实现、真实三档模型 CPU 推理、DirectML 完整长音频推理、主进程隔离、便携包轻量化和 88 项 Release 回归已经通过。正式安装包仍不生成：参考图是“模型已安装并完成分离”的结果态，当前实机对照是“真实音频已载入、模型未安装”的可用初始态，状态不一致，不能据此宣称最终视觉一致；真实声卡试听和人工点击全流程也仍需最终人工验收。

## 必须项映射

| 必须项 | 实现与证据 | 状态 |
|---|---|---|
| 第二个可见标签且内部 ID 稳定 | 导航顺序 `[0, 4, 1, 2, 3]`；布局契约测试覆盖 | 通过 |
| 主进程不链接 ORT | 空闲 AgPlayer 进程模块检查为 0 个 `onnxruntime*`；Worker 为独立进程 | 通过 |
| 三档模型与固定官方下载 | Catalog 固定 URL、字节数、SHA-256、来源和轨道；下载器覆盖 Range、续传、错误哈希和原子激活 | 通过 |
| CPU 与 DirectML | 三档 CPU 黄金测试通过；KARA DirectML 短音频及 5/10 分钟完整输出通过 | 通过 |
| 两个真实适配器 | Worker 只实现 MDX 与 Demucs；Demucs 伴奏由鼓、贝斯、其他合成并做削波保护 | 通过 |
| 失败、取消与事务输出 | 协议、超时、崩溃、幂等取消、路径安全、临时目录恢复和原子目录提交测试覆盖 | 通过 |
| 共享预览、波形、历史 | 单一预览控制器、共享绝对位置、有限峰值、`QSaveFile` 历史和 500 条上限测试覆盖 | 通过 |
| 响应式 UI | 1672×941、1280×720、880×560、1920×1080 自动布局测试和截图覆盖 | 通过 |
| 100%–200% DPI | 真实载入 3 分钟音频的 100%/125%/150%/200% 截图均无裁切、重叠或主操作丢失 | 通过 |
| 包体积增量 | 同提交基线重建后，主 EXE +443,904 字节、Worker +462,336 字节，总增量 906,240 字节 | 通过，低于 1MB 目标 |
| 未使用时零常驻 | 未进入工具时 Worker 数 0、ORT 模块数 0；退出后 Worker 数仍为 0 | 通过 |
| 正式安装包门禁 | 未生成安装包；保留视觉结果态、人工音频硬件与最终点击验收门禁 | 按计划阻止 |

## 真实模型与音频结果

短音频黄金测试使用固定 44.1kHz 立体声 WAV，并通过生产 `Decoder` 重新打开每条输出，验证轨道数、长度、采样率、声道、非静音、有限值和削波。

| 模型 / 设备 | 输出 | 重构归一化 RMS | 结果 |
|---|---:|---:|---|
| UVR MDX KARA / CPU | 2 轨 | `2.58737e-06` | 通过 |
| UVR MDX Inst HQ3 / CPU | 2 轨 | `2.38544e-06` | 通过；最终测试代码再次运行退出 0 |
| HTDemucs FT FP16 / CPU | 5 轨 | `0.0197768` | 通过；最终测试代码再次运行退出 0 |
| UVR MDX KARA / DirectML | 2 轨 | 短音频完整结果 | 通过，provider 为 `directml` |

公开领域验收音频来自 [Wikimedia Commons: Je te veux](https://commons.wikimedia.org/wiki/File:Je_te_Veux.ogg)，源文件 SHA-256 为 `7B0B1C89E23D678779AF59E7FAC2B06AAF143D12BF2B9F36D2CC7DB7C833D98A`。从该文件生成 44.1kHz 立体声 FLAC 压力样本：

| 时长 | 设备 | 实测耗时 | 验证 |
|---:|---|---:|---|
| 180.000 秒 | CPU | 132,252 ms | 2 轨、长度/格式/有限值/非静音/削波通过 |
| 299.988 秒 | DirectML | 14,262 ms | 2 轨完整输出通过，无 CPU 回退 |
| 599.977 秒 | DirectML | 31,203 ms | 2 轨完整输出通过，无 CPU 回退 |

对应日志位于 `build/qa/vocal-separation-final/`：`golden-kara-stream-refactor.txt`、`golden-hq3.txt`、`golden-demucs.txt`、`directml-kara.txt`、`duration-3min-kara-cpu.txt`、`duration-5min-kara-directml.txt`、`duration-10min-kara-directml.txt`。

## 体积与隔离

同一台机器、同一 VS/Qt/vcpkg 配置分别构建基线提交 `4a2f034` 和功能提交，使用同一 `tools/stage_release.ps1` 分发规则比较：

| 产物 | 基线字节 | 功能字节 | 增量 |
|---|---:|---:|---:|
| `AgPlayer.exe` | 5,624,832 | 6,068,736 | 443,904 |
| `AgSeparationWorker.exe` | 0 | 462,336 | 462,336 |
| 完整便携目录 | 122,821,084 | 123,727,324 | 906,240 |

便携目录共 513 个文件，不含 ORT DLL，不含 `.onnx` 模型。外置 `onnxruntime.dll` 为 17,328,152 字节；KARA 为 29,704,436 字节；HQ3 为 66,759,214 字节；Demucs 四文件各 165,612,636 字节。

开发要求给出的旧便携基线为 117,005,514 字节。当前同环境重建旧提交得到 122,821,084 字节，说明工具链/部署内容已有 5,815,570 字节漂移；因此功能增量采用同环境二进制差分，差分文件只有主 EXE 和新增 Worker，总计 906,240 字节。旧基线与当前包的绝对差值不能当作功能增量。

Windows 发布链已补齐两处真实缺口：`scripts/package-windows.ps1` 和 `tools/stage_release.ps1` 都会复制并验证 Worker；后者的默认路径也改为在参数解析后解析，避免 Windows PowerShell 中 `$PSScriptRoot` 在参数默认值阶段为空。

## UI 对照证据

以下对照图均为左侧参考、右侧实机，实机载入真实 3 分钟音频，不使用硬编码歌曲、模型状态、历史或完成标志：

- `build/qa/vocal-separation-final/comparison-1672x941-loaded-dpi100-v2.png`
- `build/qa/vocal-separation-final/comparison-1672x941-loaded-dpi125-v2.png`
- `build/qa/vocal-separation-final/comparison-1672x941-loaded-dpi150-v2.png`
- `build/qa/vocal-separation-final/comparison-1672x941-loaded-dpi200-v2.png`
- `build/qa/vocal-separation-final/comparison-1280x720-v2.png`
- `build/qa/vocal-separation-final/comparison-880x560-v2.png`
- `build/qa/vocal-separation-final/comparison-1920x1080-v2.png`

布局、层级、局部深蓝主题和响应式行为已实现；但参考图的已安装模型、下载进度、五轨结果和历史属于不同真实状态。未通过生产代码注入假状态，因此最终同状态视觉验收仍为阻塞项。

## 自动回归

- VS2022 x64 Release 全量构建：通过。
- CTest：`100% tests passed, 0 tests failed out of 88`，总计 205.20 秒。
- 分离相关测试覆盖 Catalog、下载、运行时、协议、Worker、事务输出、Controller、历史、QML、布局与发布契约。
- 全量回归中发现并修正两个旧测试基础设施问题：波形测试仍断言旧 attack/decay 参数且未固定软件场景图；10K 导入测试的 CTest 看门狗只比内部 30 秒性能门槛多 5 秒。业务断言未放宽。

## 未关闭风险

1. 必须用已安装模型和真实完成结果重拍与参考图相同状态，再做同输入视觉判定；当前不能写“最终视觉通过”。
2. 真实声卡/扬声器试听切换、人工点击导出/加入播放列表/打开目录仍需设备与人工验收；自动测试只证明控制器和文件路径行为。
3. 未在真实磁盘耗尽环境破坏性填盘；磁盘不足使用安全注入和事务测试覆盖。
4. 未验收 macOS ARM64/CoreML，不对外宣称支持。
5. TRvlvr 模型权重缺少单独明确的商业再分发许可；当前只按需下载不随包分发，正式商用仍需权利人/法务确认。
6. 正式安装包未生成，符合发布门禁。
