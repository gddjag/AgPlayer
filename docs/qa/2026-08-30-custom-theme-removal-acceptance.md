# 自定义主题功能移除验收记录

日期：2026-08-30

## 验收范围

- 彻底移除自定义主题皮肤、推荐色、单色/渐变色配置与运行时主题生成逻辑。
- 设置页仅保留“跟随系统 / 浅色 / 深色（默认）”三种主题。
- 默认主题值改为深色；非法或缺失主题值回退到深色。
- 恢复媒体颜色字段原有的 Qt 原生 `ColorDialog` 交互，不保留皮肤功能引入的自绘取色器。
- 不删除旧版磁盘中的皮肤键，避免对降级版本造成破坏；当前版本不再读取或写入这些键。

## 自动化验证

| 项目 | 命令/证据 | 结果 |
| --- | --- | --- |
| Release 配置与构建 | CMake Release；`AgPlayer`、设置与 QML 测试目标 | 通过 |
| Debug 配置与构建 | CMake Debug；`AgPlayer`、设置与 QML 测试目标 | 通过 |
| QML 静态检查 | `all_qmllint` | 通过 |
| Release 全量测试 | `ctest --test-dir build/release --output-on-failure` | 106/106 通过 |
| Debug 聚焦测试 | 设置、音频编辑器、移除契约 | 通过 |
| 移除契约 | `removed_custom_theme_contract_test` | 通过；运行时代码不再包含皮肤 API/资源，固定三主题 Token 与原生取色器契约成立 |
| 真实 WAV 烟测 | `scripts/qa-main-smoke.ps1 -BuildDirectory build/release` | 通过；播放 `sine-440hz.wav`、主窗口截图生成、日志无 WARN/ERROR/FATAL |

## 视觉验收

使用 `scripts/qa-final-ui-matrix.ps1` 生成并人工检查中文设置页：

- `build/qa/theme-removal/zh-dark-settings.png`
- `build/qa/theme-removal/zh-light-settings.png`
- `build/qa/theme-removal/zh-system-settings.png`

检查结果：三种主题按钮顺序正确；深色明确标注为默认；自定义皮肤、推荐色、渐变和自定义取色入口均不存在；浅色、深色和当前系统主题的界面层级及蓝色控件状态正常。

## 已知基线与未执行项

- Debug `qml_main_window_test` 直接运行结果为 98 通过、9 失败、1 跳过，耗时约 60 秒；CTest 的 35 秒限制也会超时。9 项失败集中在曲目状态、波形定位和拖放/重排交互，主题模式用例通过。本次删除主题功能未扩大范围修复这些既有问题。
- 原生颜色对话框已由源码恢复和静态契约覆盖；本轮自动化没有模拟 Windows 原生对话框内部交互。
- 未执行 macOS/Linux 实机视觉验证。
- 按用户要求未整合其他分支、未推送、未打包 EXE。
