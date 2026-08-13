# 音频编辑第三方许可

- Qt 6.7：项目现有 Qt 运行时与 QML UI；遵循 Qt 对应 LGPL/GPL 或商业许可条款。
- FFmpeg：项目现有解码、编码、探测与转码；本构建 vcpkg copyright 为 LGPL 2.1。最终分发仍须核对实际启用 codec 配置。
- SoundTouch：项目现有速度与音高处理；vcpkg copyright 为 LGPL 2.1。
- miniaudio：项目现有音频设备抽象，本模块指定 Windows WASAPI shared backend；上游提供 Public Domain 或 MIT No Attribution 双许可。

本次重构未新增外部运行时或模型。完整许可证原文位于构建树 `vcpkg_installed/x64-windows/share/*/copyright`，发布包必须随附相应通知。
