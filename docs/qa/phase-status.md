# AgPlayer 阶段验收状态

日期：2026-07-29
分支：`codex/revised-ui`
当前阶段：Phase 3 — 歌单、搜索与大媒体库

## 本阶段已完成

- 新增轻量 `PlaylistModel`：歌单创建、重命名、删除、曲目加入/移除、稳定 ID、原子 JSON 持久化。
- 收藏、0–5 星评分和播放历史全部写回 `LibraryModel`；历史按最近播放时间排序并持久化。
- `trackId` 查找改为哈希索引，播放队列切歌可正确记录历史。
- 组合筛选覆盖歌曲、艺术家、专辑、收藏、评分、BPM 与自定义歌单。
- BPM 双范围支持手动输入和 200 ms 防抖；无结果提示与一键清空已接通。
- 筛选栏按最新版 UI 移到歌曲列表底部；匹配文本在歌曲、艺术家和专辑列高亮。
- 歌单侧栏使用真实模型数量，移除健身/车载/网络等伪造硬编码数据。
- 曲目行支持星级编辑和歌单右键操作。
- 全局热键单元测试改用内存后端，消除其他正在运行播放器占用系统热键造成的环境性假失败。

## 本轮新鲜验证

- 全量 Debug 构建：通过；零编译错误。
- 常规测试：40/40 通过，总耗时 25.51 秒。
- 10,000 曲模型压力测试：通过，总测试进程约 1.35 秒。
- 主播放冒烟：真实生产 EXE 导入并播放 WAV，生成波形截图。
- 媒体库冒烟：8 种格式导入、持久化、列表渲染和重启恢复通过。
- 主播放及媒体库日志：无 `WARN`、`ERROR`、`FATAL`。
- 列表截图人工目检：侧栏、歌曲虚拟列表和底部组合筛选栏均正确显示。
- `git diff --check`：通过。

复现命令：

```powershell
cmake --build build/msvc-debug --config Debug
ctest --test-dir build/msvc-debug -C Debug -E stress --output-on-failure
ctest --test-dir build/msvc-debug -C Debug -R library_model_stress_test --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/qa-main-smoke.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/qa-library-smoke.ps1
git diff --check
```

## 尚未完成

- 真实声卡/扬声器的可听人工验收仍需用户在场确认；自动化已证明播放状态和时间推进。
- 设置页所有控件的应用、取消、恢复默认和重启恢复尚未完成。
- 深色、浅色、跟随系统及图标的逐页视觉验收尚未完成。
- 中文、英文、泰语、越南语仍需逐页编译与乱码扫描。
- 窗口磁吸、迷你播放器和五个音频工具仍按后续阶段验收。
- 未封装 EXE；仅在全部阶段通过且用户明确要求后打包。

## 下一阶段

Phase 4：设置持久化、深色/浅色/跟随系统主题与中英泰越四语言完整性。
