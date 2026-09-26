# Task 3 完成报告：音频工具、元数据与共享设置

## 实现

- 转码发布矩阵固定为 `MP3/FLAC/WAV/AAC/Opus/OGG/ALAC/AIFF`；移除 M4A，AIFF 使用 `pcm_s16be/aiff`。
- 转码预检的保留元数据、封面、目录结构、视频音频提取默认开启。目标不支持的附加数据会在冻结后的 profile 中降级，而不是中断有效的音频转码；封面还按每个源文件是否确有封面决议。
- 格式页使用 SettingsController 的输出格式、码率、采样率、声道、并发和保留选项作为唯一默认来源；完成/失败汇总卡可直接筛选任务状态。
- SettingsController 的并发默认值改为 5、范围 1--10；缓存默认值改为 10 GB。旧值仅在值恰为历史默认 1024 MB 时迁移，其他用户设置保留。设置持久化支持全部八种输出格式。
- 元数据界面加载已有字段时保持“未触碰”；用户清空单值已有字段时生成删除操作。预检/写入/回读仍使用同一冻结目标快照。
- 文件名自动编号和删除序号默认关闭；自动编号启用时后端强制忽略删除序号规则，防止 UI 或外部调用组合绕过互斥约束。

## TDD 与验证

先补充失败断言，随后实现：格式发布矩阵、无源封面降级、缓存迁移边界、八格式设置持久化、编号/删除序号互斥、元数据未触碰/清空 QML 语义及状态筛选交互。

```text
cmake --build build\verification-release --config Release --target \
  transcode_capability_test settings_controller_test filename_processing_test \
  audio_tools_end_to_end_test qml_audio_tools_test

ctest --test-dir build\verification-release -C Release --output-on-failure \
  -R "^(transcode_capability_test|settings_controller_test|filename_processing_test|audio_tools_end_to_end_test|qml_format_converter_test|qml_metadata_editor_test)$"
```

结果：7/7 通过。`git diff --check` 通过。

## 风险

- 本次只执行了 Task 3 聚焦 Release 构建与测试，未声称全套测试或安装包验收。
- AIFF 容器不可靠地保存通用元数据/封面，因此按能力矩阵在该目标上降级保留请求；音频转码继续成功。
