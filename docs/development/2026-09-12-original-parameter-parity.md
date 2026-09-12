# 2026-09-12 原版参数及计算链路对齐

## 范围与参考

- 用户反馈：人工同曲对比的顶起节奏、内部发光、预设、光感、环境、角度仍不同。
- 参考仓库：<https://github.com/yin-yizhen/sonic-topography>，本轮核实 HEAD 为 `ec8ecbaec0c9c5094b6b1480df0d6b2d32d6349b`。
- 保留原生 QRhi。只调整沉浸反应堆相关链路，不更换播放核心，不新增播放器运行依赖。
- 本记录取代上一记录中的“保留原版事件并额外恢复/过滤”的验收口径。现在要求相同输入、时间步下的原版事件及包络一致；不能用理论 BPM 拍数证明与原版一致。

## 已修正的实际差异

1. **节拍和频谱**：移除主显示链路的额外恢复、资格过滤及脉冲/顶起耦合，分别执行原版 Kick、Pulse、Snare、Meteor。恢复 Pulse 的 15 帧冷却、可不相邻的最高两个频点和毫秒追踪时钟；暂停时保留原版历史状态和衰减行为。静音音量不再被误当成暂停。
2. **更新节奏**：每个真实渲染帧取最新 PCM 分析一次，不先批量消耗多个历史 60 Hz 窗口再显示最后一个包络。标准预设的自动画质采用原版完整档，帧更新同步 GUI 状态，避免原来的 45 FPS 降档及脱离 GUI 同步的渲染。
3. **内部发光与几何**：标准材质保留原版静态微动，不再乘自定义静音压制；中心距离使用旋转前的实例坐标，避免旋转后内光偏移。保留原版锐度大于 1 的计算。网格先按 JavaScript 双精度计算再转 Float32，并恢复 x 外层/z 内层的实例提交顺序，保证透明叠加与原版一致。
4. **预设和环境**：13 套颜色的工作线性空间数值及发光强度用实际原版模块生成的断言核对。页面背景与预设卡片采用原版 Fog 色，不再误用 BasePrimary；标准场景移除原版不存在的旧星空球，其无作用的“星尘喷发”开关只在标准预设隐藏，自定义模式保留。
5. **相机**：保留核实一致的 45° FOV 和初始位置；距离范围改为 5–120，俯仰范围改为 0.1–接近 π/2；滚轮恢复指数缩放和正确方向，拖拽角度按窗口高度计算。标准预设仍是场景自转，不使用自定义相机巡游。
6. **旧配置**：原版默认值迁移版本提高至 2，避免历史高度、亮度、旋转、列宽/不透明度、材质和特效开关覆盖新默认。仅迁移沉浸参数，后续用户调整仍保存；不清空音乐库或其它设置。

## 独立基准与回归结果

### 音频数值

- `scripts/generate-sonic-audio-oracle.cjs` 执行原版 `AudioEngine.ts`、节拍模块及 `themes.ts`，不是再写一套原版算法。只替代输入频谱和时钟等运行边界，输出带源文件 SHA-256 的 fixture。
- `scripts/capture-sonic-fft-oracle.cjs` 使用真实 Chrome Web Audio `OfflineAudioContext`/`AnalyserNode`，采集 64 个 1024 点 PCM 窗口与 512 字节频谱；原生结果本次 **所有频点逐字节一致**。测试允许跨实现 1 字节取整误差。
- `sonic_audio_parity_test`：30/60/120 FPS 共 **1680 帧**，描述符、八频段、Kick 事件/门槛/包络、Pulse/Snare/Meteor 事件与强度、追踪频点全部对齐（浮点容差 1e-9）；**13 套预设**的线性颜色和发光强度通过（容差 2e-7）。
- 测试 fixture 为 `tests/fixtures/sonic-audio-ec8ecbae.json`。生成顺序必须先 audio oracle，再 FFT capture；缺少 64 窗 FFT 数据时测试明确失败。

### 用户歌曲与长输入

只读解码用户提供的两首歌曲至尾段，逐帧比较原生显示分析器与已由独立 TypeScript fixture 校验的参考模式：

| 歌曲 | 解码时长 | 分析窗口 | 原版/本版顶起事件 |
| --- | ---: | ---: | ---: |
| Bad Habits - Kan.mp3 | 307.714 s | 18462 | 472 / 472 |
| BLACK WINDOW.mp3 | 253.531 s | 15211 | 311 / 311 |

- 每帧事件与包络相等，无额外事件、无过滤掉的原版事件。日志：`build/release/tests/sonic-bad-habits.txt`、`sonic-black-window.txt`。
- 5 分钟、60 分钟及不同采样率/增益的合成 PCM 检查通过，日志 `sonic-long-pcm.txt`。60 分钟原版检测到 14398 次，并非理论 14400 拍；本版逐帧一致。不把原版未检测的理论拍子自动补出来。
- 原版在某些柔和低音合成输入也会触发；本轮不再用不同算法额外否决。这是“按原版对齐”与“理想鼓点分类”的区别。

### GPU、界面和构建

- 实际原版 Three.js 材质源码与原生材质在相同时间、频谱描述符、网格、相机和波纹参数下，比较 minimal-monochrome / neon-tokyo / glacier-day 的静态与激活状态，共 6 组 1920×1080 图像。
- 单采样对照隔离不同 GPU MSAA resolve 行为；两种背景合成比较的 RGB 平均误差为 **0.00396–0.00596 / 255**，只有两组浅色各有 1 像素误差超过 8。截图及数据在 `build/qa/sonic-parity-20260912/original-1x`、`native`。这是受控材质/几何对比，不是整个应用画面逐像素验收。
- 实际播放器保持 4x MSAA。GPU 定向测试通过：每次绘制分析一次、隐藏停算、暂停/恢复、密度与画质调节、相机稳定、3D 歌词及指针波纹实际像素变化。日志 `build/release/tests/sonic-production-gpu-final.txt`；静音地形/闪光检查 `sonic-idle-gpu.txt` 通过。
- 相关 CPU/QML CTest 7/7 通过，日志 `sonic-local-final.txt`。Release 主程序及相关目标构建通过，日志 `sonic-build-final3.log`。移除无效开关先增加断言并确认失败，再修改后定向 QML 回归通过（`sonic-control-final.txt`）；最新主程序/QML 目标构建通过（`sonic-build-controls-final.log`）。QML 软件后端测试中的 GPU 退出用例按条件跳过，不能当作该用例已通过；实际 GPU 验证范围见上一条。

## 复现入口与依赖边界

测试专用依赖安装在忽略的 `build/qa/sonic-reference-tools/node_modules`：esbuild 0.25.12、three 0.184.0、playwright-core 1.55.0、pngjs 7.0.0；不会打入播放器。Web Audio 本次浏览器版本 152.0.7977.83。

```text
node scripts/generate-sonic-audio-oracle.cjs <原版检出目录> <测试 node_modules/esbuild> tests/fixtures/sonic-audio-ec8ecbae.json
node scripts/capture-sonic-fft-oracle.cjs <测试 node_modules> tests/fixtures/sonic-audio-ec8ecbae.json
node scripts/capture-sonic-terrain-oracle.cjs <原版检出目录> <测试 node_modules> <原版截图输出目录>
```

原生图像回放使用现有 `terrain_column_material_test nativeZeroInputReplay`，指定原版捕获 JSON，`AGPLAYER_PARITY_SAMPLES=1`、`AGPLAYER_PARITY_RASTER_MIRROR=1`、`AGPLAYER_PARITY_RUNTIME_MATERIAL=defaults`。具体输出保留在上述 QA 目录。

## 边界

- 没有用扬声器人工听音；歌曲检查不是浏览器和原生解码、音频设备时钟的全链路同步录制，也不是人工标注的真实鼓点召回率。
- 鼠标完整阻尼/平移、按压时长波纹及页面背景切换过渡不属于本轮已证明一致的项目。不能把当前结果扩大成所有设备、任意歌曲、整个应用绝对无差别。
- 本轮没有提交、打包或发布。现有桌面人工测试安装包不包含本轮改动。
