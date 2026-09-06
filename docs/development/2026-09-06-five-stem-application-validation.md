# 五轨分离结果应用内验收（2026-09-06）

## 范围与结论

复用既有 CUDA 整曲分离文件，通过真实 `VocalSeparationController::handleResult`、波形读取队列、生产 QML 页面和播放控制器完成 Windows Release 验收。本轮未重新运行推理，也未使用伪造峰值。测试使用默认音频设备后端，不是 NULL 后端；这证明设备播放链路与交互行为，不替代人工听音或分离质量评价。

源曲为“阿岩 - 迪克牛仔-迪克牛仔-有多少爱可以重来（阿岩 - Bootleg）（阿岩 remix）.mp3”，长度约 237.27 秒。既有 CUDA 工程验证见 `build/qa/beat-cue-gpu-report.md` 最终记录；237 秒是曲长，不是推理耗时。

输入保留目录：`C:/Users/Administrator/AppData/Local/Temp/separation_real_model_test-fOofId/demucs-real-model`，文件为 `demucs-{vocals,instrumental,drums,bass,other}-demucs.wav`。共同输出增益为 0.674916。

## 固定保留文件清单

下表是本轮对当前保留字节建立的固定清单。测试驱动中的期望值是相同的编译期常量；运行时只计算待验文件的 SHA-256 并与常量比较，不会把当次计算结果自动提升为可信清单。源路径可由环境变量指向同一内容，但内容、格式和解码帧数必须匹配。

| 角色 | 文件 | 字节 | 格式 | 采样/声道 | 解码帧数 | SHA-256 |
| --- | --- | ---: | --- | --- | ---: | --- |
| source | `阿岩 - 迪克牛仔-迪克牛仔-有多少爱可以重来（阿岩 - Bootleg）（阿岩 remix）.mp3` | 9,923,360 | MP3 / MPEG Layer III | 44.1 kHz / 2 | 10,463,663 | `68B9C3DE962838B262106278A83B21010DD2FF88F935EF335B066E8D3AB8AB8B` |
| vocals | `demucs-vocals-demucs.wav` | 41,854,730 | WAV / PCM s16le | 44.1 kHz / 2 | 10,463,663 | `9A9D5FB3BA2D822D1015F8D06A41DF52B1F27AD5EDA45C553186AC9DDCA24718` |
| instrumental | `demucs-instrumental-demucs.wav` | 41,854,730 | WAV / PCM s16le | 44.1 kHz / 2 | 10,463,663 | `C0BB0F6D342144F2CADCC333056E62F853AA5E6E70F2A0A6CDAE3EA3505992D5` |
| drums | `demucs-drums-demucs.wav` | 41,854,730 | WAV / PCM s16le | 44.1 kHz / 2 | 10,463,663 | `B44E9C4F7E02AF4002CFD40161310D8C5E78515552BD7D5827A0A4205DC1DF86` |
| bass | `demucs-bass-demucs.wav` | 41,854,730 | WAV / PCM s16le | 44.1 kHz / 2 | 10,463,663 | `FE53353BE22AF9C63638C11CEF4229F545DB4332C991045C90FEB15B43A0A2E4` |
| other | `demucs-other-demucs.wav` | 41,854,730 | WAV / PCM s16le | 44.1 kHz / 2 | 10,463,663 | `5E4AC6F41D8E32B8CC4463EB0A440A7223DE8ACE22C7482D60316F089C4B80FD` |

`replayRealFiveStemResult` 在选择模型、发布结果或启动波形读取前校验源文件和五个固定文件名的字节数、扩展名、探测到的容器/编解码、采样率、声道、PCM 位深（适用时）、完整解码帧数及 SHA-256。任一不符即拒绝重放。回归用例还会把与 vocals 同为 PCM16/44.1 kHz/双声道/10,463,663 帧的 drums 文件传给 vocals 校验，确认仅内容哈希错配也会被拒绝。

此清单只证明本轮列出的固定文件版本与后续应用内重放一致。它不是在原 CUDA 推理发生时生成或签名的产物，因此不能追溯证明这些字节在先前某次 GPU 运行中的来源；既有 CUDA 日志仍是独立证据。

## 已执行检查

- 固定清单门禁加入后，以 `AGPLAYER_QA_REAL_SOURCE`、`AGPLAYER_QA_REAL_STEMS_DIR` 和 `AGPLAYER_QA_REAL_STEMS_GAIN` 启用 opt-in 数据，CTest 原生 NULL 后端整文件回归为 44 passed、0 failed、0 skipped，耗时 11,298 ms；当前日志为 `build/release/qml-vocal-separation.txt`。其中真实五轨应用流程与跨轨哈希拒绝用例均通过。
- opt-in `VocalSeparationWorkbench::test_retainedCudaFiveStemsRealApplicationFlow`：3 passed、0 failed、0 skipped，包含初始化/清理，耗时 5418 ms。日志：`build/qa/five-stem-app-real.txt`。
- 五轨均从真实 WAV 得到 2048 个非空峰值，且每轨有正峰值；截图直接来自生产页面 `grabImage(page)`。
- 人声、鼓、贝斯、其他四源混音播放位置推进；伴奏轨不与其组成轨重复叠加。
- 暂停位置保持、seek 至 20 秒后继续推进；五轨逐一点击波形进入 solo，位置继续推进。
- 五个音量控件均通过鼠标拖动设置至 0.45，播放不中断。
- 主播放器与分离试听双向互斥均通过。
- 默认 NULL 后端的 `qml_vocal_separation_test` CTest 回归通过 1/1；保留文件验收仅在显式提供环境变量时启用。

## 视觉证据

- `build/qa/five-stem-app-playing.png`：最终主要证据，五轨波形全部可见，播放位置约 01:11，暂停按钮与逐轨调整后的音量位置可见。
- `build/qa/five-stem-app-waveforms.png`：波形属性就绪后的较早帧，最后一轨绘制尚未完全刷新；不能单独作为五轨视觉完整的证据。
- `build/qa/five-stem-app-final-state.png`：互斥切回主播放器后的页面状态。

## 测试夹具与复现

仅修改测试驱动与 QML 集成测试，未修改生产代码。重放结果明确标记为 `retained-cuda-result` / `QA replay (no inference)`。正常分离执行、模型部署与本轮结果重放是不同验收环节。

首次重放由于源音频的延迟波形任务尚未完成就提交结果，打断了测试的结果波形队列。修正为先等待源波形完成再重放，与真实长任务的时序一致；修正前失败，修正后上述流程通过。

运行 `qml_audio_tools_test.exe -input tests/qml/tst_vocal_separation.qml VocalSeparationWorkbench::test_retainedCudaFiveStemsRealApplicationFlow`，设置以下环境：

- `AGPLAYER_QA_REAL_SOURCE`：上述真实源文件绝对路径；它只供两个 opt-in 保留文件用例使用。CTest 自带的 `AGPLAYER_TEST_AUDIO` 仍保持短 sine fixture，避免改变本文件其他回归的输入契约。
- `AGPLAYER_QA_REAL_STEMS_DIR`：上述保留目录。
- `AGPLAYER_QA_REAL_STEMS_GAIN=0.674916`。
- `AGPLAYER_QA_REAL_AUDIO=1`，`QT_QPA_PLATFORM=windows`，`QT_QUICK_CONTROLS_STYLE=Basic`。
- 可选 `AGPLAYER_VISUAL_FIXTURE_OUTPUT`：截图路径前缀，不在测试内硬编码。
- 按本机 Qt 部署设置 PATH、QT_PLUGIN_PATH、QT_QPA_PLATFORM_PLUGIN_PATH、QML_IMPORT_PATH；本轮使用 Qt 6.7.0 MSVC2019_64。

这不是对最终发行包、重新执行 CUDA 推理或主观听感的验收声明。
