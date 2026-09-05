# 分离稳定性定向修复

范围：主检出中的分离控制器、分离页、原生设备选择、可选 Python 安装器。整合发布记录见 `2026-09-06-player-separation-release.md`。

## 根因与行为

- 波形服务发出 `_complete=false` 渐进快照；分离控制器之前将首个快照当作完成，弹出队列并取消当前解码。现在保留当前请求直到完整结果，并将下一条启动延后至下一轮事件，避免完成信号中的同步重入。
- 标准五轨受信 HTDemucs FP16 图在本机 RTX 4070 Ti SUPER 的 DirectML 路径上，2 秒输入尚未产生结果时工作集已达 21,469,745,152 字节。本任务终止了自己启动的测试进程。该资源观察来自本轮进程采样，未保存完整 GPU 运行日志。
- 同输入 CPU 路径五轨真实推理通过，用时 75.117 秒，采样工作集约 1 GB。新 Auto 路径直接选择 CPU，通过相同输出/时长/重构检查，用时 106.802 秒（同期有构建负载，不能作为性能提升对比）。本修复解决 DirectML 内存膨胀/等待，未声称 CPU 推理实时。
- 页面已有打开时 GPU 探测与自动选择。五轨和 Python VR 开始前将自动选中的 GPU 改为“自动 · CPU”；历史记录保留实际 provider 和回退原因。此模型限制不会禁用其他模型的 GPU。显式手动 GPU 仍按现有强制设备契约报具体不支持原因，提示改 Auto/CPU；Python worker 在导入重依赖前检查这一条件。
- 未知自定义模型改为“查看原因”与明确对话框；没有推理适配器的 `.pth/.th` 不再暗示安装 Python 即可执行。实际 Python 适配范围仍是已校验的 `5_HP-Karaoke-UVR.pth`。
- 配置期间模型容器增加 40 px，保留进度/暂停/取消在卡片内；未知总量显示“配置中”，不伪造百分比。
- 升级已有 Python 环境时，在运行时对象构造阶段自动同步本版本嵌入的 worker；复用原解释器、依赖与验证标记，不发网络请求。同步使用 `QSaveFile` 原子提交并禁用直接写回退，拒绝重解析根目录/文件；`ready()` 还要求当前脚本与嵌入内容完全一致，旧标记不能让之后替换的脚本继续被执行。

## 下载来源与校验

- uv：固定 `0.8.22/uv-x86_64-pc-windows-msvc.zip`，官方 GitHub URL；失败自动切 `https://ghfast.top/` 加相同官方 URL 的第三方传输中继。两路均要求 20,716,936 字节与 SHA-256 `5049375aa2a5162f132b2c1cb992e25d42d47d934cab8c174dbe6f60973dcc12`，通过后才能解压。
- Python：uv 固定版本内置的 Python 3.11.13 发行元数据；官方失败切上述中继，只改传输 URL，保留内置 SHA-256。清除继承的 `UV_PYTHON_DOWNLOADS_JSON_URL`，防止外部配置替换受信元数据。
- 依赖：固定直接依赖版本，官方 PyPI 失败切清华 HTTPS PyPI 镜像；未新增模型下载或执行未知 pickle。
- 模型/uv 请求设置 15 秒无数据超时，之后按现有重试机制自动进入备用线路。uv 子进程设置 HTTP timeout 15 秒、重试 1 次；每个安装阶段独立尝试官方和备用。
- 官方版本依据：[uv 0.8.22 环境变量定义](https://github.com/astral-sh/uv/blob/0.8.22/crates/uv-static/src/env_vars.rs#L277)、[下载 URL 替换与 SHA 校验](https://github.com/astral-sh/uv/blob/0.8.22/crates/uv-python/src/downloads.rs#L1191)。

## 验证证据

- RED：`build/release/separation-red.txt`、`separation-demucs-red.txt`、`separation-route-red.txt`、`separation-device-red.txt`。
- 最终控制器全套：`build/release/separation-controller-upgrade-full.txt`，61 通过、0 失败、1 跳过（完整 Python 安装另有 opt-in 实测记录）。此前 `separation-controller-green.txt` 为升级修复前 58 通过；删除临时测试目录触发的 Qt 文件监视通知警告没有失败断言。
- 最终联动定向：`build/release/separation-final-focused.txt`，4 项行为与初始化/清理共 6 通过。
- QML：`build/release/qml-vocal-separation.txt`，41 通过；包含下载容器与响应布局检查，不替代主程序截图验收。
- 原生设备测试通过；真实 CPU/Auto 五轨日志：`build/release/separation-demucs-cpu.txt`、`build/release/separation-demucs-auto-fixed.txt`。
- Python 首次安装验证被 QtTest 默认单函数 300 秒上限终止，日志 `build/release/separation-python-install.txt`；仅终止了本次遗留 uv/构建子进程，保留下载缓存。第二轮设置测试上限 `QTEST_FUNCTION_TIMEOUT=900000`（未改生产超时），在 229.168 秒完成安装及再次缓存修复，3 通过，日志 `build/release/separation-python-install-resume.txt`。实际导入 PyTorch/Separator/FFmpeg 两次成功，并写入就绪标记；环境位于原有 `AppData/Roaming/AgPlayer/AgPlayer/separation/runtime/python-vr-1`。
- Python 协议 GPU 真实性回归 2 通过。真实受信 5_HP 模型 + 同一 2 秒输入，修改后的源码 worker 在 22.22 秒生成两轨并通过 CPU provider、回退理由、44.1 kHz 双声道与有限采样检查（两轨时长 1.992 秒）；输出与日志目录 `C:/Users/Administrator/AppData/Local/Temp/agplayer-real-vr-lkeo6doz`。
- 最终构建后已执行缓存修复并刷新本机 worker，连同 VR/五轨设备显示断言共 5 通过，14.776 秒，日志 `build/release/separation-final-installed-worker.txt`。安装脚本与仓库源文件 SHA-256 均为 `8FB86690794A472B6E5B8538388E8105E70B5806F8D01C32B9E12500E856BB1C`；就绪标记已重新验证。
- 独立审查补充的客户升级回归：旧 worker 不同步、junction 错误就绪两项均先失败，见 `build/release/separation-upgrade-red.txt`；修复后升级/路径安全/VR显示/下载备用定向共 6 通过，见 `build/release/separation-upgrade-green.txt`。这覆盖客户已有环境升级路径，不依赖本机手动缓存修复。

这些是 Release、测试进程与真实模型文件验证，未完成长歌曲五轨、应用内五轨试听或实际听音验收。
