# Windows 沉浸反应堆帧性能检查

基线：`29db0c06`，`codex/pc-six-track-editor`。用户反馈正常播放持续不顺滑。

## 修改与证据

- 内置主题在 `terrain_reactor.frag` 的 referenceMode 分支提前返回，不读取阴影图；原先仍每帧绘制完整柱阵的阴影。共享 QRhi 阴影配置现在跳过这项绘制，保留纹理初始化和手动材质阴影。不减少柱数、分辨率、MSAA 或特效。
- 新 GPU 回归先证明启用/禁用阴影时内置材质的 framebuffer 逐像素相同，并在旧实现上因仍启用阴影而失败。修复后通过；切回手动材质会重新启用阴影。
- 另复现均衡画质切到高画质时停止刷新：定时器已停止，MSAA 未变，renderer 尚未同步新画质，无法接管下一帧。`setQuality()` 现在在可渲染时主动请求一次更新。原 GPU 切换回归失败；修复后去掉辅助窗口 resize 仍通过。
- 性能探针改用持续事件循环，避免 `QTest::qWait` 的分批睡眠干扰 GUI 驱动的帧节奏；固定主屏并启用仅测试使用的 GPU timestamps。停用后排空回报，避免高频探针退出时残留事件。

## 同条件性能比较

Windows、Qt 6.7 Release、D3D11、RTX 4070 Ti SUPER，3440×1440 实际缓冲、DPR 1、50176 根柱体、neon-tokyo、高画质、固定合成特征、关闭自动旋转。前后各三个独立进程，每轮预热 1.5 秒、测量约 3 秒。

| 指标 | 修改前 | 跳过冗余阴影后 |
| --- | ---: | ---: |
| QRhi 已完成帧 GPU 时间采样中位数（各 18 个样本） | 2.7645 ms | 1.455 ms |
| 三轮平均墙钟帧间隔的中位数 | 5.48446 ms | 5.48168 ms |

GPU 时间约减少 47.4%，平均帧间隔基本不变。这是增加渲染余量的证据，不是帧率提升或彻底消除用户卡顿的证明。`lastCompletedGpuTime()` 是周期采集的已完成帧样本，不是所有帧的 GPU 分位数。

日志：`build/reactor-before-{1,2,3}.txt` 和 `build/reactor-optimized-{1,2,3}.txt`。早期 `reactor-after-*` 不作为最终对比：恢复基线实验的文件时间戳曾使增量构建保留旧对象；已重新触发受影响目标编译后采集 optimized 结果。

## 最终验证

- Release 主程序、terrain_reactor_item_test、terrain_reactor_gpu_smoke_test 构建成功：`build/reactor-complete-build.txt`。
- CTest `terrain_reactor_item_test` 通过（沿用其 offscreen/software 环境）。
- 材质专项：新增阴影跳过、手动材质实际阴影、透明度连续性、深度材质回退，7 passed / 0 failed（含初始化/收尾与数据行）：`build/reactor-material-final.txt`。
- D3D11 专项：真实解码 PCM 与逐帧分析、暂停释放/恢复、隐藏停算、密度/画质切换、3D 歌词/点击波纹、主题材质约束，6 passed / 0 failed（含初始化/收尾）：`build/reactor-gpu-complete.txt`。
- 完整程序通过现有 QA 隔离模式播放本地依赖附带的 loop.mp3，3440×1440 全屏，分析和渲染持续推进、正常退出：`build/reactor-audio-optimized.log`。该次运行验证阴影优化，随后画质 setter 的局部修补由上述最终 GPU 回归覆盖。不是人工听音验收。
- `git diff --check` 通过。未改音频核心、资源文件夹拖入逻辑、`app/main.cpp` 内容或原有两处缓存。

尚未稳定复现用户所述正常播放持续不顺滑；没有宣称所有主题、设备、实际曲目或长时运行已验收。使用跨平台共享 QRhi 实现，macOS 真机未验证。本轮仅提交源码、测试和验证记录；不推送、打包、发布或替换已安装版本，留待下次更新发布。
