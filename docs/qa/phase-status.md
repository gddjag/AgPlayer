# AgPlayer 阶段验收状态

日期：2026-07-28
分支：`codex/revised-ui`
当前阶段：Phase 0 — 真实运行基线恢复

## 本阶段已完成

- Qt 6 `FileDialog` / `FolderDialog` 属性统一修正，覆盖主窗口、独立歌单和五个音频工具。
- 生产程序通过真实控制器完成 WAV 导入、播放和波形生成。
- 品牌封面回退资源路径修正。
- 设置页和音频工具页布局告警根因修正。
- 删除未引用的 `ComingSoonPage.qml` 占位页。
- 新增隔离用户数据、显式日志路径的生产 EXE 冒烟脚本。
- 运行日志在无消息时也会创建，便于自动验收。

## 本轮新鲜验证

- 全量 Debug 构建：通过，编译器警告视为错误。
- 常规测试：37/37 通过，20.69 秒。
- 生产 EXE 冒烟：真实导入 `sine-440hz.wav`，进入播放状态并生成主界面截图。
- 冒烟截图：47,307 字节；可见曲目信息、暂停状态和真实波形。
- 冒烟日志：无 `WARN`、`ERROR`、`FATAL`。
- `git diff --check`：通过。

复现命令：

```powershell
cmake --build build/msvc-debug --config Debug
ctest --test-dir build/msvc-debug -C Debug --output-on-failure -E '^library_stress_test$'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/qa-main-smoke.ps1
git diff --check
```

## 尚未完成

- 本轮未重跑 1 万曲目压力测试。
- 尚未完成真实多格式文件与目标声卡/扬声器的人工听感验收。
- 各音频工具仍需逐页执行真实文件导入、处理、导出和回放验收。
- 主题、四语言、窗口磁吸、迷你播放器仍需按新门槛重新验收。
- 未封装 EXE；仅在全部阶段通过且用户明确要求后打包。

## 下一阶段

Phase 1：完成导入、目录扫描、媒体库持久化、重复文件处理和重启恢复的真实文件矩阵。
