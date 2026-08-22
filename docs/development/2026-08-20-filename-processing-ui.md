# 文件名处理板块整改记录

日期：2026-08-20

## 需求与实现

| 需求 | 实现位置 | 验收证据 |
| --- | --- | --- |
| 按参考图复刻 1672 px 桌面布局 | `app/qml/AgPlayer/components/tools/FilenameProcessPage.qml` | `docs/qa/filename-process-comparison-final.png`、`design-qa.md` |
| 前缀和后缀可添加、可删除 | QML 添加/删除单选模式；`FilenameRuleSet::removePrefix/removeSuffix` | `qml_filename_process_test`、`filename_processing_test` |
| 添加框留空时反向清理已有前后缀/序号 | `FilenameProcessPage.rules()` 与 `FilenameTransformEngine` | `blankAffixesRemoveRecognizableAffixesAndSequence` |
| 显式删除按首尾文本精确匹配且不区分大小写 | `FilenameTransformEngine::removeKnownPrefix/removeKnownSuffix` | `removesRequestedAffixesCaseInsensitively` |
| 保留扩展名、空格替换、大小写、自动序号、冲突策略 | 复用现有处理器、规划器和事务层 | 既有处理测试 + QML规则交互测试 |
| 批次中未移动的源文件仍占用目标名称 | `RenamePlanner::build` 的静止源占用检查 | `treatsNoOpSourceAsAnOccupiedTarget` |
| Skip 产生的静止源会沿依赖链继续占用原名 | `RenamePlanner::build` 的固定点传播 | `propagatesStationaryOccupancyFromSkippedSources` |
| AutoNumber/Overwrite 不破坏批内静止源或重复目标 | 静止源自动编号、批内覆盖保护 | 3 个冲突边界单元测试 |
| 取消按钮与参考图一致且始终可操作 | 忙碌时取消事务，空闲时关闭工具窗口 | `audio_tools_layout_contract_test` |

## 本轮验证

- Release 构建：`AgPlayer`、`filename_processing_test`、`qml_audio_tools_test` 成功。
- `filename_processing_test`：13/13 QtTest 检查通过。
- `qml_filename_process_test`：通过。
- `audio_tools_layout_contract_test`：通过。
- QA 实机进程：退出码 0，生成 1672 × 941 截图。
- 文件名事务端到端用例在当前受限执行环境中无法写入 `QStandardPaths::AppDataLocation/rename-transactions`，触发 `journal-write-failed`；这是沙箱写权限限制，未修改生产日志安全策略绕过验证。
