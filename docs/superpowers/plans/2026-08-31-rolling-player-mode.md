# AgPlayer 滚动播放模式实施计划

## Global Constraints

- 在当前 `main` 工作树原地实施；保留并兼容现有未提交的频彩波形、设置和测试改动，不重置、不清理、不提交。
- 新模式是第三套主界面外壳 `rolling-player / 2`，不是独立进程、播放器或 EXE。
- 三套主界面复用同一份 `PlayerControls` 实现；滚动模式专属速度、BPM、保持音调和波形缩放必须在公共播放条之外。
- 顶部总览点击任意位置后定位并立即播放；大波形中央播放线固定，正常波形从右向左滚动，大波形拖动为真实 Vinyl/Scratch，不允许高频 `seek()` 冒充。
- Scratch 使用有界、预分配的局部 PCM 缓存，目标前后各约 8 秒、硬上限 12 MiB；回调线程零锁、零分配、零 FFmpeg/Qt/I/O。
- Scratch 向左拖前进、向右拖倒放，超过 4 DIP 启动，缩放决定时间映射，速率 `-3.0x..+3.0x`，35 ms 平滑，松手恢复拖动前播放/暂停状态，并临时旁路保持音调。
- 普通速度/BPM 共用单一比例，范围 `0.75x..1.50x`、步进 `0.05x`、`targetBpm = sourceBpm * speedRatio`；换曲及离开滚动主题时恢复 1.00x。
- Signalsmith 使用 `codex/audio-editor-no-recording-20260828@b1dd6e1` 的最终修正实现为主、现有 SoundTouch 为回退；不整体合并该分支无关改动，不新增 DLL/模型/常驻服务。
- 同一干净 Release 部署目录前后净增不超过 1 MiB；若双引擎超限，迁移为 Signalsmith 单引擎并移除未使用的 SoundTouch 打包产物后重测。
- 视觉以 `C:/Users/Administrator/Desktop/音视频播放器/AgPlayer音频播放器完整版/滚动播放器.png` 为唯一基准；进度条使用 `Theme.waveformMagenta` (`#E62E9B`)；默认 1440x480 DIP、最小 1000x420 DIP、自由缩放。
- Mini 播放器保持现有紧凑组件。第一版不含 Slip、MIDI/DVS、跨曲搓碟、整曲 PCM 预解码、专业 ASIO 唱盘模式或安装包。
- 每个行为先写测试并确认 RED，再写最小实现；完成前分别验证 Debug/Release、QML、真实音频、视觉和尺寸，不把局部测试当作完整验收。

## Task 1: 第三外壳模式与窗口/歌单状态

- 为 `SettingsController` 增加 `Classic=0, Integrated=1, Rolling=2` 和字符串 `rolling-player`，迁移旧 0/1 和异常值。
- 扩展 `WindowController` 与 `app/main.cpp`：经典/滚动使用共享独立歌单，一体化使用内嵌列表；滚动模式保存独立主窗/歌单几何并复用现有四边磁吸、跟随、最小化和 DPI 行为。
- 先补设置持久化、切换、异常值、歌单创建/销毁与几何配置测试并验证失败。

## Task 2: 普通播放速度、BPM 与 Signalsmith

- 抽取 `b1dd6e1` 最终 Signalsmith 引擎及许可，接入公共 `ITimePitchEngine`；主播放器处理发生在解码线程，1.00x 旁路。
- 为 Core/C API/PlaybackController 增加 `speedRatio/sourceBpm/targetBpm/keepPitch` 和设置、重置接口；未知 BPM 时速度可用、目标 BPM 不可编辑。
- 先补比例、边界、重置、换曲复位、引擎选择和音高/时长测试并验证失败。

## Task 3: 局部真实 Scratch 音频核心

- 增加 `ScratchPcmWindow`、`ScratchRenderer`、无锁命令邮箱与 Core/C API/PlaybackController 的 begin/update/end/cancel 状态。
- 原始 PCM 缓存在时间伸缩之前；回调按有符号速率做固定成本插值和约 8 ms 淡化，缓存外淡出并异步重定位；松手只提交一次最终位置。
- Scratch 期间冻结 EOF/自动换曲/循环抢占；曲目、设备、输出所有权和 cache epoch 变化安全取消。
- 先补正反放、停针、边界、插值、淡化、epoch、生命周期及回调零分配/零锁/零 seek 测试并验证失败。

## Task 4: 双声道电平与频谱复用

- 在现有输出处理链增加真实 L/R Peak/RMS 原子 tap；由 `AudioVisualFeatureController` 暴露，停止/暂停时衰减归零。
- 继续复用已有 128-bin 频谱为 UI 着色，不增加 FFT。
- 先补左右声道、归零、衰减和无额外频谱分析测试并验证失败。

## Task 5: 滚动主题 UI 与公共播放条

- 新增 `RollingPlayerShell`，复用 `WaveformSession`、频彩 mix/low/mid/high 数据、元数据、封面和控制器。
- 顶部总览、紫红进度、大波形固定中央线、60..480 DIP/s 缩放、Scratch 手势/缓冲状态、L/R 分段表和底部实时控制全部绑定真实接口。
- 重排单一 `PlayerControls` 为：歌单、上一曲、播放/暂停、下一曲、播放模式、波形样式、均衡器、音频工具、换肤、歌词、沉浸视觉、小窗、音量；衣服图标弹出三项皮肤选择。
- 先补 QML 行为测试：外壳加载、公共组件、顶部 seek+play、固定播放线、拖动方向/缩放映射、专属控制可见性、换肤复位和响应式最小尺寸。

## Task 6: 集成、视觉 QA、性能与记录

- 运行目标 CTest、QML、qmllint、Debug/Release build 与导入->播放->搓碟->切肤->退出 smoke；覆盖 WAV/FLAC/MP3 VBR/AAC、44.1/48/96 kHz、WASAPI 共享/独占和设备切换。
- 验证：时长比误差 <=1%、保持音调 <=5 cents、scratch p95 <=60 ms/p99 <=90 ms、松手 p95 <=100 ms、60 秒无 underrun/爆音/增长、缓存 <=12 MiB、1x CPU 增量 <=1 个百分点、Release 净增 <=1 MiB。
- 在 100/125/150/200% DPI 和默认/最小/超宽尺寸截图；把参考图与 2172x724 实现图放进同一对照图，迭代 `design-qa.md` 到 `final result: passed`，否则如实 blocked。
- 写 `docs/development/2026-08-31-rolling-player-mode.md`，记录需求、改动、RED/GREEN 测试、失败和未验证项。
