# 2026-09-01 隐藏式基础视频播放能力

## 当前状态

- 方案 A 已按确认规格实现。
- 正式规格：
  `docs/superpowers/specs/2026-09-01-hidden-basic-video-playback-design.md`
- 功能保持“音频播放器优先”：没有新增视频入口、视频模式、视频媒体库或视频设置页。
- 实现复用现有 FFmpeg、播放队列、音频时钟和 Qt Quick Scene Graph；没有引入
  Qt Multimedia、VLC、mpv 或独立运行环境。
- 2026-09-02：MSVC Release 全量构建、QML lint、160/160 CTest、9/9
  真实媒体验收和 Windows 桌面视觉/交互验收通过。

## 需求追踪

| ID | 必须行为 | 验收证据 | 状态 |
| --- | --- | --- | --- |
| V01 | 双击或单实例转发视频路径后导入并播放 | `single_instance_test`、`import_controller_test`、`windows_shell_runtime_test`；QA 进程入口真实路径播放 | 通过 |
| V02 | 单个视频拖入播放器或现有播放列表 | `native_drop_router_test`、`qml_main_window_test` 的原生/QML drop 合同 | 通过 |
| V03 | 自动识别真实视频轨，封面流不误判 | `video_decoder_test`、`audio_file_discovery_test`、`library_model_test`；attached-picture/multi-video fixtures | 通过 |
| V04 | 不新增视频模式、入口、列表或媒体库 | `qml_video_playback_view_test` 禁止项合同；最终依赖和 diff 审查 | 通过 |
| V05 | 复用现有队列和全部播放模式 | `playback_controller_test`、`queue_gapless_test`、混合音视频队列回归 | 通过 |
| V06 | 复用暂停、Seek、音量、倍速、上一首/下一首 | `video_playback_controller_test`、`qml_video_playback_view_test`；Windows 实机暂停 | 通过 |
| V07 | 视频画面上方、基础控制栏下方 | QML 窄/宽窗几何合同；窗口截图 | 通过 |
| V08 | 全屏、退出全屏、Esc | `qml_main_window_test`；Windows 实机全屏/Esc；全屏截图 | 通过 |
| V09 | 返回时停止视频并恢复音频界面 | `qml_main_window_test` 验证停止、显式 dismiss、Loader 销毁及原音频壳层复用；Windows 实机暂停后返回复验 | 通过 |
| V10 | 切到纯音频时自动恢复原界面 | `video_playback_controller_test` 的视频/音频切换和 50 次循环 | 通过 |
| V11 | 离开视频后释放解码器、帧和 GPU 纹理 | decoder/controller/frame-item 生命周期、取消和重复销毁测试；返回实机复验 | 通过 |
| V12 | 纯音频时无视频持续 CPU/GPU 负载 | `audioOnlyPlaybackAllocatesNoVideoResources`：零 worker、零队列、零帧；纯音频不实例化视频视图 | 通过 |
| V13 | 不引入独立运行环境或大型播放器框架 | CMake/依赖扫描；沿用现有 FFmpeg 与 Qt Quick | 通过 |
| V14 | 无音轨视频仍有可 Seek 的静音时间轴 | silent-clock 核心测试；`video-only.avi` 真实运行 | 通过 |
| V15 | 网络、字幕、编辑、滤镜、画质、HDR 均不扩展 | 公共接口、QML 禁止项和依赖审查 | 通过 |

## 实现摘要

- 文件发现、导入、单实例转发和 Windows 文件关联支持
  `.mp4/.mkv/.webm/.mov/.avi/.m4v`，但 UI 不暴露独立视频入口。
- 媒体探测持久化音频/视频流类型；attached picture 不计作可播放视频轨。
- 有音轨视频沿用现有音频引擎作为主时钟；无音轨视频使用轻量静音时钟。
- FFmpeg 视频解码器提供取消/Seek/旋转/SAR 信息；单 worker、最多 3 帧且最多
  64 MiB 的有界队列。
- `VideoFrameItem` 在渲染线程上传纹理并保持纵横比；离开视频后释放纹理和帧。
- 视频视图覆盖在现有音频壳层上方；返回时先请求底层停止，再显式释放视频控制器，
  即使底层停止返回错误也不会遗留视频层；Error 状态不会重新启动视频 worker。

## 自动化验证

在 `D:\ai\AgPlayer\.worktrees\hidden-basic-video-playback` 执行：

```powershell
cmake --build --preset windows-msvc-release --config Release --parallel 4
cmake --build --preset windows-msvc-release --target all_qmllint --config Release --parallel 4
ctest --test-dir build/release -C Release --output-on-failure -j4
```

结果：

- MSVC Release 全量构建通过。
- QML lint 通过；仅报告两个既有 unused-import 信息：
  `WaveformSession.qml`、`SharedWaveformView.qml`。
- CTest：160/160 通过，0 失败，总耗时 129.88 秒。
- 修复“暂停后返回且底层 stop 失败”后，聚焦回归
  `video_playback_controller_test`、`qml_main_window_test` 为 2/2 通过。
- `git diff --check` 通过；仅有仓库既有的 LF/CRLF 转换提示。

## 真实媒体与视觉验收

验收脚本：`tests/scripts/video_playback_acceptance.ps1`。脚本先验证所有必需样本，
任一样本缺失立即失败，不启动播放器；每次运行同时校验截图、视频可见性、worker、
帧序号和队列上限。

真实运行 9/9 通过：

| 运行 | 结果 |
| --- | --- |
| MP4 / H.264 / AAC | 通过，连续帧与音频主时钟路径 |
| MKV | 通过 |
| WebM | 通过 |
| MOV（旋转元数据） | 通过，竖向显示和黑边正确 |
| AVI（含音轨） | 通过 |
| M4V | 通过 |
| AVI（无音轨） | 通过，静音时间轴 |
| 中文、空格路径 MP4 | 通过 |
| MP4 全屏 | 通过 |

汇总：
`build/acceptance-evidence/summary.json`。所有运行均为一个 worker、3 帧队列；
实测队列内存为 691,200 至 4,055,040 字节，低于 64 MiB 上限。

关键截图：

- `build/acceptance-evidence/mp4-h264-aac.png`
- `build/acceptance-evidence/mp4-h264-aac-fullscreen.png`
- `build/acceptance-evidence/mov.png`
- `build/acceptance-evidence/unicode-space-path.png`

Windows 桌面实机观察确认：自动进入隐藏视频视图、画面更新、暂停、全屏、Esc、
返回音频播放器。首次返回复验发现 WebM 暂停后底层 `stop()` 可返回 `AG_DECODE_ERROR`；
新增显式 dismiss 和 Error 状态门控后，复验已恢复同一音频壳层并释放视频视图。

媒体来源用于兼容性验收：FFmpeg 官方 H.264 样本库、FFmpeg H.264+AAC A/V sync
样本目录，以及 MediaElement 示例媒体仓库。

## 已知验证边界

- 自动化真实运行走默认音频后端，MP4/H.264/AAC 等含音轨样本已进入 Playing；
  当前代理不能以人耳确认扬声器实际可闻，因此不把“主观听感”写成已验证。
- 文件关联注册/恢复由隔离注册表测试覆盖；没有在用户真实 Explorer 上改写关联后再双击，
  避免污染用户默认应用设置。
- 真实 WebM 在暂停后请求底层 stop 时仍可能记录既有解码器 `AG_DECODE_ERROR`；
  视频层现已可靠释放并恢复音频 UI，但该底层容错日志不等同于已修复音频解码器本身。
