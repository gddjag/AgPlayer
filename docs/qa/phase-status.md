# AgPlayer 阶段验收状态

日期：2026-07-29
分支：`codex/revised-ui`
当前阶段：Phase 2 — 真实播放与波形闭环

## 本阶段已完成

- 播放页改用稳定 `trackId` 定位媒体库行，修复不可用曲目被排除后队列索引与元数据错位。
- `LibraryModel` 提供可观察的 `count` 和 `indexForTrackId()`，并覆盖追加、重复拒绝、重置与查找测试。
- 波形点击/拖动提交后真实调用 `PlaybackController.seek()`；悬停和取消拖动不误跳转。
- 纯色、RGB、频谱三种模式即时复用已分析数据，不重复解码当前曲目。
- 波形分层分析、缓存命中、空路径和过期异步结果测试正式注册到 CTest。
- 波形顶点预算按可见像素收敛，减少场景图几何量。
- 收藏状态在模型 `dataChanged` 后立即更新播放页图标。
- 修复播放页收藏图标的系统灰色方块和标题间距回归。

## 本轮新鲜验证

- 全量 Debug 构建：通过；零编译错误。
- 常规测试：38/38 通过，总耗时 38.78 秒。
- 播放/波形专项：6/6 通过。
- 主播放冒烟：真实生产 EXE 导入并播放 WAV，生成含波形截图。
- 媒体库冒烟：8 种格式导入、持久化和重启恢复通过。
- 主播放及媒体库日志：无 `WARN`、`ERROR`、`FATAL`。
- 生产截图人工目检：标题、收藏、元数据、波形和控制栏均可见，无平台默认方块回归。
- `git diff --check`：通过。

复现命令：

```powershell
cmake --build build/msvc-debug --config Debug
ctest --test-dir build/msvc-debug -C Debug -E stress --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/qa-main-smoke.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/qa-library-smoke.ps1
git diff --check
```

## 尚未完成

- 真实声卡/扬声器的可听人工验收仍需用户在场确认；自动化已证明播放状态和时间推进。
- 未执行一万首曲目的压力测试。
- 歌单 CRUD、组合搜索筛选与大媒体库性能进入下一阶段。
- 四语言完整性、浅色/跟随系统、窗口磁吸、迷你播放器和音频工具仍按后续阶段验收。
- 未封装 EXE；仅在全部阶段通过且用户明确要求后打包。

## 下一阶段

Phase 3：歌单 CRUD、收藏/评分/播放历史、组合搜索筛选与一万首媒体库性能闭环。
