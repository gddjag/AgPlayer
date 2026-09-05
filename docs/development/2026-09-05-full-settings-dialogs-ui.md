# 设置、均衡器与非播放对话框 UI 验收记录（2026-09-05）

## 范围与依据

本轮按 `2026-09-03-ui-design-system-acceptance.md` 已确认的暗紫/浅蓝、紧凑桌面方向，审计设置 0～6、均衡器、标签管理，以及本范围内的确认/错误弹窗。业务绑定、控制器接口和菜单动作均保留。产品已删除“曲库管理”和“文件详情”页，本轮曾由新增 QA 枚举误触发的两个遗留页面不属于产品验收范围，相关生产修改和专用测试已全部撤销。

视觉基线来自 `D:/ai/AgPlayer/build/qa/full-ui-baseline/`：设置页为 860×900，均衡器为 1080×480，标签为 863×888，均包含暗色和亮色。

## 逐页结论

| 页面 | 基线检查 | 本轮处理 | 复核结论 |
| --- | --- | --- | --- |
| 设置 0：常规 | 搜索框只有空白轮廓，文件关联呈现为连续的 `✓MP3✓WAV…`，选项辨识度不足 | 搜索框加入 15px 搜索图标、34px 左内边距和无障碍名称；文件关联 Flow 提升到 32px 高、选项间距改为 12px | 840×640 实际 QML 布局中全部格式仍在边界内，搜索提示和选项间距可辨识 |
| 设置 1：播放与音频 | 860×900 暗/亮图中标题、分组、输出选择、开关和响度设置层级清楚，无裁切 | 无源码调整 | 保持现有紧凑密度和功能顺序 |
| 设置 2：外观与波形 | 主题、列表与波形颜色设置分组清晰；长页在滚动容器中，无横向溢出 | 无源码调整 | 暗/亮对比和控件对齐符合现有 Theme |
| 设置 3：音频工具预设 | 通用导出、转码、变调/变速分组宽度一致，数值控件未被压缩 | 无源码调整 | 860×900 下信息层级完整 |
| 设置 4：快捷键设置 | 全局与应用内快捷键行保持统一高度，键位列对齐 | 无源码调整 | 未发现字号、颜色、裁切或操作入口问题 |
| 设置 5：缓存与数据 | 路径、自动清理和清理动作层级明确；危险动作与普通信息区分清楚 | 无源码调整 | 暗/亮状态均可读，确认清空缓存功能保留 |
| 设置 6：关于 | 产品名、版本和说明集中且留白稳定 | 无源码调整 | 860×900 下无裁切；没有恢复已移除的反馈入口 |
| 均衡器 | 1080×480 暗/亮图完整呈现 18 段、响应曲线、预设区和底部输出信息 | 无源码调整 | `qml_equalizer_visual_test` 通过；可见控件驱动控制器，窄窗口底部输出可滚动到达 |
| 标签管理 | 863×888 暗/亮图中左导航、中央歌曲区、右侧标签区三列边界明确；标签计数保持自然宽度 | 无源码调整 | 既有 Flow/Flickable、28px 胶囊、选择/悬停/拖放状态契约通过 |

## 对话框与功能保留检查

- 设置页“确认清空缓存”仍使用既有共享对话框；共享 `ThemedDialog.qml` 不在本子任务中修改。
- 当前设置页按既有产品决定不显示反馈入口；本轮没有新增外部发送或伪造反馈状态。

## 实施文件

- `app/qml/AgPlayer/SettingsPage.qml`
- `app/qml/AgPlayer/components/LyricsPanel.qml`
- `app/qml/AgPlayer/components/SearchFilter.qml`
- `app/qml/AgPlayer/components/FallbackCoverImage.qml`
- `app/qml/AgPlayer/components/ThemedTextField.qml`
- `tests/qml/tst_error_states.qml`
- `tests/qml/tst_integrated_theme.qml`（歌词、搜索和封面回退当前源码测试）
- `tests/qml/tst_design_system.qml`（共享输入框 placeholder 状态）

`SettingsWindow.qml`、六个其余设置分区实现、`EqualizerWindow.qml`、均衡器子组件、`TagManagementPanel.qml` 以及共享 `ThemedDialog.qml` 已逐页或逐组件检查，本轮没有为统一 token 而机械改动。

## 验证证据

- 共享 placeholder 合入后的统一编译、QML lint 与 QML/合同回归共 26/26 通过，用时 78.90 秒；删除遗留详情测试后的最终资源双构建由主任务统一复跑。
- `ctest --test-dir build/release -R ^qml_equalizer_visual_test$`：1/1 通过。
- `qmllint`：`SettingsPage.qml`、`ThemedTextField.qml`、`tst_error_states.qml` 均通过。
- `tag_management_layout_contract_test.ps1`：通过。
- 全部已有暗/亮设置 0～6、均衡器和标签基线均已逐图检查。

## 首轮整窗故障与修复

首轮以隔离 `AgPlayer.exe` 捕获时，部分窗口在截图前返回 0×0，另有部分窗口在截图后以 `0xc0000005` 退出；当时日志只有视频诊断 INFO，因此没有把组件级截图冒充为整窗通过。后续转储定位到 Qt 6.7.0 Windows 窗口销毁期间的 DPI 事件路径，并在 `app/main.cpp` 增加仅针对已销毁窗口内容的退出保护。最终 `full-ui-accepted` 捕获 46/46 均正常保存 PNG、退出码 0 且日志干净；其中两张已取消“文件详情”页只记作误覆盖历史，现行产品矩阵为其余 44 张。首轮异常不再代表当前状态。

## 最终整窗矩阵复核

已逐图复核 `D:/ai/AgPlayer/build/qa/full-ui-accepted/` 中暗/亮设置 0～6、均衡器与标签：

- 设置 0 的搜索图标清晰可见，文件关联选项保持可辨间距；设置 1～6 的分组标题、控件列和底部操作区均未出现横向越界或文字遮挡。设置 2 的内容超过当前视口时仍由既有滚动容器承载。
- 均衡器暗/亮图均完整显示 18 段、响应曲线、预设和输出区，没有频段缺失或底部遮挡。
- 标签页暗/亮图的三列结构、标签胶囊和错误反馈均可读；中央“部分文件无法导入”来自矩阵的失败导入 fixture，属于被验证的错误状态。
- 设置 0～6、均衡器和标签暗/亮矩阵项均为退出码 0、尺寸有效、日志干净。设置 0 为 860×900，搜索图标与 MP3～OGG 关联间距可见。
- `full-ui-verified`、`full-ui-final-recheck` 中的 `library-manager` 图片，以及 `full-ui-accepted` 中的 `details` 图片，来自新增 QA 枚举误触发的遗留页面；它们只保留为问题调查历史，不代表产品仍有这些入口，也不计入现行产品的 44 张通过项。两页生产文件与本轮专用测试均已恢复到任务开始时的基线树状态，QA 枚举已由主任务删除。

`full-ui-input-dpi-1.25/matrix.csv` 和 `full-ui-input-dpi-1.5/matrix.csv` 各有 10/10 项通过。日志记录的实际 DPR 分别为 1.25 和 1.5，而非仅依赖请求的缩放参数：设置 0 的 860×900 逻辑窗口分别输出 1075×1125 与 1290×1350 原始像素。两档缩放的暗/亮设置 0 中，搜索占位提示、放大镜、恢复默认和关闭入口均清晰，文件关联选项与底部操作区没有遮挡或裁切。

## 歌词、搜索与封面回退补充

最终集成说明：上文“恢复到基线树”仅指隔离 UI 任务撤销误修改的历史阶段。主线随后已经实际删除这两个废弃页面、专用控制器和运行入口，未将其重新纳入产品；见 `2026-09-05-player-runtime-polish.md`。

- `ThemedTextField.qml` 直接基于 `QtQuick.Templates.TextField`，此前只有输入框边框与错误页脚，没有 Controls 样式额外提供的 placeholder 绘制，因此工具页搜索、路径和前后缀输入在空值时会显示为空框。共享组件现补充单行 placeholder：位置和尺寸跟随输入 padding 与扣除错误页脚后的可用区域，继承输入字体，长文本右侧省略，禁用时使用禁用文本色；有正文或输入法预编辑文本时隐藏，清空后恢复。当前源码动态测试覆盖空值、输入、清空、长文本省略、禁用色与错误页脚边界，3 项通过、0 失败。
- `LyricsPanel.qml` 的普通窄面板保留四个底部动作；这些动作与 28px 关闭按钮统一提供 hover、pressed 和键盘焦点状态。歌词来源、无时间轴提示、线路提示和尝试记录均显式使用 Theme 字体。线路提示从可见来源标题与关闭按钮两者完整底边的较大值之后开始，避免长线路名覆盖关闭按钮。
- 280×360 暗/亮真实 QML 捕获位于 `D:/ai/AgPlayer/build/qa/lyrics-final/dark-lyrics-route-focus.png` 与 `light-lyrics-route-focus.png`。两图均显示两行长线路提示、两条尝试记录、完整四按钮和导入按钮焦点环；提示框从关闭按钮下方开始。捕获宿主 4 项通过、0 失败。
- `SearchFilter.qml` 在 543px 宽布局保留关键词、评分、BPM 范围和清除操作，收紧标签与滑轨宽度以适应播放器中心列；对应当前源码 QML 测试通过。
- `FallbackCoverImage.qml` 将图片解码失败后的回退切换延后一拍，并在执行前核对失败请求仍是当前 `requestedSource`、状态仍为 `Image.Error` 且尚未进入 fallback。空请求直接使用 fallback 时不会因 fallback 自身失败重复排队，旧图片错误也不会覆盖随后设置的有效图片。
- 当前源码动态加载用例使用 QML 文件作为真实不可解码图片，并以两个不同品牌 PNG 分别作为 fallback 和新有效图：损坏图成功进入有效 fallback；缓存错误后立即切换新有效图时旧回调未覆盖新图。两项各 3 项通过、0 失败；日志保留预期的 `Unsupported image format`，没有 `Binding loop detected`。
- `qmllint` 对 `ThemedTextField.qml`、`FallbackCoverImage.qml`、`LyricsPanel.qml` 与相应测试检查通过；`tst_design_system.qml` 仅报告原有未使用 `QtQuick.Controls` import 信息。
