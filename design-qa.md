# AgPlayer 设计验收

日期：2026-07-28
环境：Windows 11、Qt 6.7、100% 缩放

## 空白启动页

- 参考图：`C:\Users\Administrator\Desktop\音视频播放器\AgPlayer音频播放器完整版\空白启动页.png`
- 参考裁切：`build/qa/empty-startup-reference-1228x424.png`
- 最新实现：`build/qa/theme-language-matrix/main-zh-dark.png`
- 并排对比：`build/qa/startup-latest-comparison.jpg`
- 视口：参考与实现均为 1228 × 424
- 状态：空媒体库、未播放

对照结果：

- 窗口比例、圆角边框、品牌区、窗口控制：通过
- 标题、说明、导入按钮、格式提示：通过
- 底部控制区分布、透明图标、RGB 播放按钮：通过
- 深色、浅色、跟随系统语义色与图标：通过
- P0/P1/P2：0
- P3：参考图展示背景纹理，程序使用稳定主题表面；不影响布局、可读性或交互。

## 轻度剪辑

- 参考图：`C:\Users\Administrator\Desktop\音视频播放器\AgPlayer音频播放器完整版\音频工具 剪辑.png`
- 参考裁切：`build/qa/light-editor-reference-1536x1024.png`
- 最新实现：`build/qa/theme-language-matrix/tools-zh-dark.png`
- 并排对比：`build/qa/editor-latest-comparison.jpg`
- 视口：1536 × 1024
- 状态：六条空轨、默认 BPM 128、1/4 拍吸附

对照结果：

- 左侧工具导航、顶部 BPM/吸附控制、六轨时间线：通过
- 拖动、裁剪、吸附、滚轮缩放、静音/独奏/锁定：通过
- 统一 BPM、保持音高、节拍对齐、导出设置：通过
- 空状态自然不显示参考图中的三条已载入波形；轨道结构与滚动行为完整。
- P0/P1/P2：0

## 四语言 × 三主题矩阵

- 启动页矩阵：`build/qa/theme-language-matrix/main-matrix-contact-sheet.jpg`
- 剪辑页矩阵：`build/qa/theme-language-matrix/tools-matrix-contact-sheet.jpg`
- 语言：中文、英文、泰语、越南语
- 主题：深色、浅色、跟随系统
- 截图：24/24 成功；尺寸分别稳定为 1228 × 424、1536 × 1024
- 未发现裁切、重叠、溢出、不可读图标或错误主题色。
- 跟随系统在当前 Windows 深色外观下正确呈现系统暗色语义。

final result: passed
