# Task 4 完成报告：Windows 身份、版本与多语言安装器

实现提交：`8b50950`。

## 实现

- Windows 原生窗口统一使用 `AgPlayer.Desktop` 与品牌 ICO。主窗口是唯一
  `WS_EX_APPWINDOW`；列表、工具和设置窗口不产生独立任务栏分组。移除了
  `WM_ACTIVATE` 自动调用 `showMain()` 的回调，避免任务栏最小化被延迟恢复取消。
- 主窗口发布并回读验证 AppUserModelID、relaunch command、display name 与 icon
  resource；运行时探针同时验证唯一任务栏窗口、`WM_GETICON` 大小图标和非空属性。
- `cmake/AgPlayerVersion.cmake` 是唯一版本源。CMake、About、QApplication、PE
  资源、manifest、Inno Setup 与安装器文件名全部从该值派生；
  `scripts/release-version.ps1` 可校验并更新版本契约。
- Inno Setup 始终显示语言选择器，以简体中文为第一项/默认项，并提供中文、英文、
  泰文、越南文。文件关联、安装后启动及卸载个人数据提示均随所选语言翻译。
- 安装向导使用品牌 LOGO/图标，继续使用 `lzma2/ultra64` 与 solid compression，
  不执行自动任务栏固定。

## TDD 与验证

先扩展契约/原生测试并观察失败：缺少单一版本派生、缺少版本校验脚本、缺少 Shell
属性回读，以及 `WM_ACTIVATE` 会取消最小化。实现后执行：

```text
cmake --build build/final-release-msvc --config Release --target AgPlayer --parallel 4

ctest --test-dir build/final-release-msvc -C Release --output-on-failure \
  -R "^(release_version_contract_test|installer_contract_test|window_icon_contract_test|window_controller_test|window_taskbar_native_test|windows_shell_runtime_test|settings_controller_test)$"

powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package-windows.ps1 \
  -BuildDirectory build/final-release-msvc -Configuration Release -SkipBuild
```

结果：Release `AgPlayer` 构建通过；聚焦测试 7/7 通过；Inno Setup 6.7.3 完整
编译通过。产物为 `build/installer/AgPlayer-Setup-1.0.0-x64.exe`（32.83 MiB），
SHA-256 `5CCCC9AAA346C9EC1B592F5E8C2CC0396412F7714BFEE56534732D3FF7C24DDD`。
EXE 的 FileVersion/ProductVersion 为 `1.0.0.0`，Setup 的 FileVersion 为
`1.0.0.0`、ProductVersion 为 `1.0.0`。`git diff --check` 通过。

## 仅能人工 Shell 验证

- 在真实 Explorer 任务栏点击 AgPlayer 图标，观察整组最小化/恢复、图标分组和二级
  窗口不生成独立按钮；自动化已覆盖原生消息、样式和图标返回值，但不能替代视觉交互。
- 交互运行安装器，确认语言页默认简体中文，四种语言切换、品牌 LOGO 裁切和向导文字
  的实际显示效果。
- 在非生产 Windows 用户中完成安装/开始菜单及桌面快捷方式启动，再检查 Explorer Shell
  缓存刷新后的 AUMID/品牌图标。自动安装未在当前用户执行，以免覆盖现有快捷方式。
- 交互卸载时分别验证四种语言的个人数据提示，以及“是/否”对个人数据的实际保留/
  删除行为；音乐文件必须始终保留。

## 复审修复 round 1

修复提交：`930cb01`。

- `package-windows.ps1` 在 staging 和 ISCC 之前读取待打包 `AgPlayer.exe` 的
  FileVersion/ProductVersion，并要求二者都等于单一版本源派生的四段版本；
  `-SkipBuild` 不再能把旧 PE 包装成新版本安装器。
- 新增真实 PE 守卫测试：同一个 1.0.0.0 EXE 配合临时 2.3.4 源码必须被提前拒绝；
  恢复 1.0.0 源码后必须越过版本守卫，直到测试刻意省略的运行库检查才停止。
- About QML 断言改为 `"AgPlayer " + SettingsController.version`，不再复制发布版本；
  用例可独立加载设置窗口，并注册为 `qml_about_version_test`。

最终 Release 聚焦 CTest 9/9 通过，其中包含原 Task 4 七项测试、新 PE 守卫测试和
真实 About QML 用例。重建 `qml_main_window_test` 目标通过，About 用例 3/3 通过。
Windows PowerShell 5.1 下完整 `-SkipBuild` 打包通过；最新安装器为 34,428,837 bytes，
SHA-256 `93CA0108FF4FE4C0CD3A3AE9F40FE22B8BE635BC3FFC891AB93EB99293F40B57`。
提交级 `git diff --check` 通过。
