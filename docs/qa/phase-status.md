# AgPlayer 阶段验收状态

日期：2026-07-29
分支：`codex/revised-ui`
当前阶段：Phase 8 — 最终集成与 Windows MVP 自动化验收

## 本阶段完成

- 设置项已逐项连接运行时；删除独占模式、交叉淡化等当前无法兑现的伪设置。
- 顺序、随机、单曲循环、列表循环已贯通 C API、核心引擎、Qt 与 QML。
- 格式转换、调速、升降调、轻度剪辑均执行安全覆盖策略，禁止覆盖输入源。
- 格式转换的“保留元数据”与“从视频提取音频”已分离；后台任务在启动时冻结覆盖策略，消除并发读写。
- 旧版随机/单曲循环数值已迁移到新枚举；调速与轻度剪辑在设置变化时实时同步“保留音高”。
- 波形密度、粗细、Hover、主题与发光效果均由设置实时驱动。
- 中文、英文、泰语、越南语各 457 条完成翻译，未完成 0，非中文目录汉字残留 0。
- 四语言 × 深浅主题 × 10 页面最终 UI 矩阵 80/80 通过，缺图 0，运行时告警 0。
- Windows 波形缓存原子替换加入有限重试；回归测试修复前稳定失败，修复后连续 100 次通过。

## 最终自动化证据

- Debug `/W4 /WX` 构建通过；CTest 44/44，17.52 秒。
- Release `/W4 /WX` 构建通过；CTest 44/44，5.90 秒。
- Release 播放冒烟通过：真实 WAV 打开、播放、截图和有序退出，日志无警告。
- 8 格式曲库冒烟通过：导入、持久化、重启恢复 8/8。
- 10,000 行曲库搜索、排序和随机访问压力测试通过。
- 10,000 首原生波形/缓存压力：10,000/10,000、0 失败、306.307 秒、缓存 8.47 MiB、峰值工作集 16.04 MiB。
- Seek 基准：100/100、0 失败、中位 0.0142 ms、P95 0.0222 ms，低于 20 ms 门限。
- 隐私扫描未发现网络客户端、遥测、统计、SMTP、私有邮箱或外部进程执行；仅保留用户点击后由系统浏览器打开的项目官网链接。
- 未部署的 Release `AgPlayer.exe` 为 3.38 MiB；未执行运行库部署或安装包封装。

## 结论

Windows MVP 的软件实现与本机可自动化门禁已完成。发布仍需以下人工/平台门禁，不将其误报为已完成：

- 真实声卡/扬声器的可听播放、Seek、切歌、音量与静音验收。
- Windows 多声卡、蓝牙断连/重连和设备热插拔验收。
- macOS、Linux、HarmonyOS 的构建、音频后端、文件关联与分发验证。
- EXE/安装器、DMG、AppImage 打包；仅在用户单独下达打包指令后执行。

复现：

```powershell
cmake --build build/msvc-debug --config Debug --parallel
ctest --test-dir build/msvc-debug -C Debug --parallel 8 --output-on-failure
cmake --build build/msvc-release --config Release --parallel
ctest --test-dir build/msvc-release -C Release --parallel 8 --output-on-failure
scripts\qa-main-smoke.ps1 -BuildDirectory build/msvc-release
scripts\qa-library-smoke.ps1 -BuildDirectory build/msvc-release
scripts\qa-final-ui-matrix.ps1 -BuildDirectory build/msvc-release
```
