# 音频工具 PDF 对照审计

基准：`AGPlayer音频工具四模块统一开发指令.pdf`（22 页）。本表只依据当前代码、已运行测试和原生窗口实测；“控件存在”不等于交付。

| PDF 要求 | 当前证据 | 状态 |
|---|---|---|
| 单窗口、顶部四模块：轻度剪辑、格式转换、元数据修改、文件名处理 | `AudioToolsWindow.qml` 与 `qml_light_editor_test` | 已实现，待最终人工窗口验证 |
| C++17、Qt 6、FFmpeg 原生 libav，QML 不处理音频 | `LightEditor`、`FormatConverter`、`MetadataEditor`、`FilenameProcessor`；未发现工具链 QProcess ffmpeg | 已实现 |
| 统一 Windows 拖放、中文路径、四模块接收 | `NativeDropRouter`；原生 `WM_DROPFILES` 已分别实测四工具 | 已实现，但需安装包内真实 Explorer 拖放复验 |
| 轻度剪辑：16 轨、多 Clip、网格、吸附、撤销、BPM、Loop、工程保存 | `LightEditor` 为 16 轨；控制器/QML 回归已覆盖保存、Clip、剪辑、导出、BPM 与撤销 | 已实现，但 10 分钟 Loop、长音频手感和设备断开未验收 |
| Time/Pitch：速度、音高、保持音调、BPM 对齐 | SoundTouch 预览/离线链路及 `LightEditor` 属性已有测试 | 部分完成；真实耳机失真与不同采样率质量仍未验收 |
| 格式转换：批量、取消、重试、真实进度、临时输出验证 | `audio_tools_end_to_end_test` 覆盖转换、取消、重试、冲突与回读 | 已实现；实际构建可用编码器组合仍需动态枚举复验 |
| 元数据：Keep/Set/Clear、封面、无重编码、失败保留原文件 | `MetadataEditor`、`metadata_writer`、端到端标签测试 | 已实现；跨容器字段与封面人工复验待完成 |
| 文件名：预览、冲突、事务、Unicode、撤销、内容哈希不变 | `FilenameProcessor`、端到端重命名和内容不变测试 | 已实现；Explorer 占用/权限失败人工复验待完成 |
| 不冻结、可取消、真实任务状态、无假进度 | 后台控制器和取消测试存在 | 部分完成；大批量、内存和长时压力证据不足 |
| Debug/Release、qmllint、真实声卡、长期压力 | 本轮已跑 Release 目标测试 | 未完成：Debug、qmllint、真实声卡、8 小时和新安装包验收 |

## 已确认本轮修复

- 波形分析完成后把精确 PCM 时长回写播放器当前曲目；VBR 容器时长偏差不再让 Seek、播放进度和波形尾部使用不同时间轴。`queue_gapless_test` 与 `playback_controller_test` 已覆盖。
- Windows 原生层级：音频工具/设置窗口会被停靠列表插入遮挡的问题已复现。
- 新增 Windows 原生 Z 序测试；旧实现失败后，改为从底到顶顺序提升 `播放器 → 停靠列表 → 工具/设置`。
- 验证：离屏回归通过；Windows 原生窗口测试通过。

## 仍不可宣称完成的事项

1. 四工具在最终安装包中逐项 Explorer 拖放、试听、参数变更、导出并回放。
2. 编辑器长音频、16 轨、10 分钟 Loop、设备切换和异常取消。
3. Debug 构建、qmllint、真实声卡/WASAPI 与 8 小时压力。
4. 用户列出的播放器、列表、窗口、波形 12 项，仍按后续阶段逐项修复和验证。
