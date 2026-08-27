# Integrated 单窗口主题基线验证

基线提交：`514816053dd6e201a176b00f2227f269aeec7a8e`

## 工具链

- 直接执行预设时，当前普通 PowerShell 终端错误选中 MinGW 16.1.0；构建在既有 `-Werror` 警告处失败。该结果仅用于证明必须进入 Visual Studio x64 开发环境。
- 使用 VS 2022 Community `vcvars64.bat` 后重新 `--fresh` 配置，编译器为 MSVC 19.38.33133.0。

## Debug 基线

- 配置：通过。
- 完整构建：668/668，通过。
- 完整 CTest：112 项中 103 项通过，9 项失败（92%）。

基线失败项：

- `audio_editor_controller_test`
- `qml_theme_color_contract_test`：现有 `TagManagementPanel.qml` 两处未分类运行时颜色
- `qml_main_window_test`：35 秒超时
- `qml_format_converter_test`：异常退出
- `qml_format_converter_matrix_test`：异常退出
- `qml_format_converter_visual_fixture_test`：异常退出
- `qml_filename_process_test`：异常退出
- `qml_metadata_editor_test`：异常退出
- `runtime_deployment_test`：未执行部署步骤，缺少 `Qt6Core.dll`

后续验收必须把以上问题与本分支新增回归分开记录；Release 通过不能替代 Debug 退出与聚焦测试证据。

