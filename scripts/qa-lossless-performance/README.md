# 无损鉴别整文件性能 QA

Windows 原生 CPU/进程计数器，Python 3.11+ 标准库；不依赖 MCP，不控制其他进程。所有命令在仓库根目录运行，输出目录须新建或为空。默认 QA 根由本脚本目录 `parents[1]/build/qa` 解析，即仓库的 `build/qa`。

```powershell
python scripts/qa-lossless-performance/runner.py --self-test --out build/qa/round9-performance/self-test-new
python scripts/qa-lossless-performance/runner.py --preflight-only --gate-timeout 30 --out build/qa/round9-performance/preflight-new
```

自检仅运行短 Python 子进程，验证 affinity、CPU/wall、超时清理、JSON 与门控，不扫描音频。`--preflight-only` 即使门控通过也不会启动 scanner。

获得安静环境并协调其他工作后，正式比较命令为：

```powershell
python scripts/qa-lossless-performance/runner.py --out build/qa/round9-performance/paired-new
```

默认冻结 1.11 对 1.13、四个完整输入、逻辑 CPU 6、Normal priority。QA runner 只短暂设置自身 affinity，使新 scanner 从启动继承，随后恢复自身；逐个核对实际设置。具体 EXE/输入、DLL SHA、拓扑、参数与固定随机种子写入 `method.json`。可用 `--baseline`、`--candidate`、`--workloads`、`--logical-cpu` 指定已有资源。不要把自检成功视为这些资源存在或长基准通过。

每输入先有一对预声明暖机；正式执行 4 个 ABBA block，8 个方向平衡的成对比较。前后要求连续 5 个约 1 秒样本：整机 busy ≤20%、选定物理核两个逻辑线程均 ≤15%、MCP CPU ≤一核 5%，且没有观察到 MCP worker/退出事件。最多等待 60 秒，无安静窗口则明确 `invalid` 并停止。运行中继续原生观测，污染、超时、输出差异或 hash 改变都不能通过；所有原始结果、反向比值及污染 block 保留，不按性能删异常值。

CPU 为子进程 kernel+user 时间；墙钟包含启动与收尾；私有提交峰来自 scanner 的 `qaMemory`。完整分析结果只排除版本/耗时，整数和布尔精确比较，浮点沿用 1e-8 并记录最大差。`stableAcceptanceEligible` 只是已完成且有效数据的分散度候选标志，不能直接当成提速结论；还需审查完整成对比值、所有波动和墙钟结果。

观测边界：运行时目标 CPU 必须承载 scanner，因此从背景忙碌率门槛排除；短暂非 MCP 进程在同一目标 CPU 的竞争不能完全排除。短命进程可能在两次快照之间退出，进程 CPU 是下界；逻辑 CPU 总计数仍含其工作。亲和性不隔离共享缓存、内存或睿频。当前脚本不提供跨任务限核/暂停功能。

本轮自检通过，30 秒实际预检未找到安静窗口，0 个音频扫描；证据保留于 `build/qa/round9-performance/preflight-v1`。稳定整曲统计验收尚未完成，不得将自检或局部微基准代替它。
