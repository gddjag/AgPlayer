# AgPlayer 阶段验收状态

日期：2026-07-28
分支：`codex/revised-ui`
当前阶段：Phase 1 — 导入与媒体库闭环

## 本阶段已完成

- 构建时自动生成 WAV、MP3、FLAC、AAC、M4A、OGG、Opus、WMA 八种真实测试文件。
- 八种格式均通过公共 C API 完成打开、解码、播放推进和跳转测试，不再因缺少夹具而跳过。
- 修复实验性 FFmpeg 编码器启用和 Opus 采样率自动适配，不新增编解码依赖。
- 生产程序支持文件夹导入、媒体库 JSON 持久化和重启恢复。
- 独立音乐列表窗口修复缺失的对话框依赖、布局警告、计数绑定和深色主题图标。
- 主窗口与空媒体库共用一个导入进度/错误组件，删除重复状态界面。
- 新增 `qa-library-smoke.ps1`，通过真实 EXE 验证导入、持久化、重启和列表渲染。

## 本轮新鲜验证

- 全量 Debug 构建：通过；项目编译警告按错误处理。
- 常规测试：37/37 通过，总耗时 25.87 秒。
- 格式矩阵：8/8 格式通过。
- 主播放冒烟：真实导入并播放 WAV，生成主窗口截图；日志无 `WARN`、`ERROR`、`FATAL`。
- 媒体库冒烟：首次导入 8 条记录，第二次启动恢复同一 8 条记录；两次均生成真实列表截图。
- 媒体库两次运行日志：无 `WARN`、`ERROR`、`FATAL`。
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

- 未执行一万首曲目的压力测试。
- 未完成真实声卡/扬声器的人工听感验收。
- 音频工具仍需逐页完成真实导入、处理、导出和回放验收。
- 歌单增删改、四语言完整性、浅色/跟随系统、窗口磁吸和迷你播放器仍需按后续阶段验收。
- 未封装 EXE；仅在全部阶段通过且用户明确要求后打包。

## 下一阶段

Phase 2：播放队列、歌单 CRUD、收藏/评分/搜索筛选与大媒体库性能闭环。
