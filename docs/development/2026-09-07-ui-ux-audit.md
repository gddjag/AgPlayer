# AgPlayer 全模块 UI/UX 审查与优化建议

日期：2026-09-07。范围：`D:\ai\AgPlayer`，Qt/QML，`main@0f23817` 加当前未提交修改。

结论：保留现有深浅主题、紧凑桌面布局、三种播放器模式和公共控件。优先解决操作含义不清、键盘入口缺失与窄窗信息丢失，再改善参数密度、重复说明和视觉层级。无需引入新 UI 框架或更换渲染路线。

## 审查依据与边界

- 使用用户指定的 `ui-ux-pro-max` 技能。检索 `keyboard focus visible` 命中通用焦点规则；桌面密度检索及一次改写未找到合适桌面规则，因此密度建议来自当前产品与截图分析，不冒充技能库推荐。
- 沿用项目已批准的设计方向：清晰的窗口层级、紧凑列表、深色紫色强调及浅色蓝色强调。当前 Theme 的正文为 14 DIP、辅助文字 12 DIP、常规控件 32 DIP。不会机械套用移动端 44px 全控件尺寸。
- 使用现有 `build/release/app/AgPlayer.exe`（本次检查时修改时间 21:34），未重新构建。通过程序自带 `--qa-test-mode` 截取并逐张查看 **26 张当前运行截图**；另检查当前源码与设置窗口可访问树。
- 播放器截图使用仓库内 2 秒正弦波测试文件，只用于观察布局。大块平直波形、未知艺术家、无真实封面及少量曲目来自样本，不能据此评价真实音乐波形质量、曲库性能或音频准确性。
- 页面级审查覆盖播放器、列表/标签、六个工具、七类设置和 EQ。歌词、视频、队列、导入反馈及部分二级弹窗主要为静态审查；没有逐个运行所有成功、失败、下载、覆盖、长文件名、HiDPI、英文状态。
- 桌面自动化可读取设置窗口的控件树，但工具将设置子窗口点击映射到主窗口坐标，出现越界；重新选择后仍仅返回主窗口，因此没有继续盲点。设置搜索输入未确认生效，搜索缺陷以下按源码证据陈述。
- 经典模式截图成功后该 QA 进程退出码为 `-1073741819`（`0xC0000005`）；其余首轮五个播放器/EQ/沉浸捕获及后续七个补充捕获退出为 0。此异常单列为待排查项，不代表截图失败，也不能宣称运行稳定性通过。
- 本次只新增审查记录和 QA 产物，未修改生产代码、未提交、未打包；保留原工作区修改。

证据目录：[当前运行截图与日志](D:/ai/AgPlayer/build/qa/uiux-audit-20260907)。下文中的截图和源码链接均指向本次工作区。

## 优先处理的问题

P1：影响理解、定位或安全操作，建议第一轮处理。P2：效率与视觉优化，建议随后按模块实施。

| 优先级 | 发现与依据 | 推荐修改 | 验收方式 |
|---|---|---|---|
| P1 | 无损鉴别窄窗状态筛选只剩五个数字，依赖颜色判断。[截图](D:/ai/AgPlayer/build/qa/uiux-audit-20260907/lossless-compact.png)、[源码](D:/ai/AgPlayer/app/qml/AgPlayer/components/tools/LosslessTaskPanel.qml:160) | 保留“全部 / 无损 / 转码 / 升频 / 未知”等短标签及数量；宽度不足时改为有名称的筛选下拉。完整可访问名称不能跟着变成数字。 | 880×560 下无需悬停或辨色即可知道每个筛选的含义。 |
| P1 | 设置搜索只过滤分类名称及别名，不搜索具体选项；无匹配时保留旧内容。[源码](D:/ai/AgPlayer/app/qml/AgPlayer/SettingsPage.qml:181) | 加轻量关键词映射，搜索结果定位对应设置行；明确显示无结果。无需引入搜索依赖。 | “采样率”“BPM”“缓存上限”可定位；不存在的词出现空结果提示。 |
| P1 | 元数据字段点击“清空”后输入禁用，保留/设为/清除切换控件被隐藏。[源码](D:/ai/AgPlayer/app/qml/AgPlayer/components/tools/MetadataEditPage.qml:1185) | 字段末尾提供“清空 / 恢复原值”，显式区分保留、将修改、将清除。 | 清空一个字段后，可以直接恢复且不丢失其他字段草稿；确认前清晰显示删除意图。 |
| P1 | 文件名页“添加前缀/后缀”下写着“留空则清理已有前缀/后缀”，操作名称与后果相反。[截图](D:/ai/AgPlayer/build/qa/uiux-audit-20260907/tool-3.png)、[源码](D:/ai/AgPlayer/app/qml/AgPlayer/components/tools/FilenameProcessPage.qml:640) | 将添加、删除指定文字、清理前后缀明确区分；建议空输入默认不修改，保留改名前后预览和撤销。此项涉及交互语义，实施前纳入批准范围。 | 空输入、添加和删除三种操作的预览与文案一致。 |
| P1 | 转换预检只说检测到参数调整或冲突，没有列出具体变化。[源码](D:/ai/AgPlayer/app/qml/AgPlayer/components/tools/FormatPreflightDialog.qml:42) | 既有确认框内显示实际输出参数、调整项、冲突数量与处理方式；详情可展开。 | 用户能判断采样率是否改变、重名如何处理，再决定确认。 |
| P1 | 设置分类使用 Rectangle + MouseArea；当前可访问树没有七个分类入口。曲库评分星星使用 15×16 DIP 图标与 TapHandler。[设置源码](D:/ai/AgPlayer/app/qml/AgPlayer/SettingsPage.qml:333)、[评分源码](D:/ai/AgPlayer/app/qml/AgPlayer/components/SearchFilter.qml:135) | 复用公共可聚焦控件，补 Enter/Space、方向键及可访问名称；星星视觉大小可保留，扩大有效点击区域。 | 键盘可选择设置分类、评分与清除评分；每个焦点可见，读屏能辨认作用。 |
| P1 | 快捷键冲突仅红边提示，无冲突动作/原因说明；Tab/Esc 也被录入逻辑接收。[源码](D:/ai/AgPlayer/app/qml/AgPlayer/SettingsPage.qml:2242) | 显示“与××冲突”等就地说明；明确录制状态、结束/取消入口。Tab/Esc 是否影响退出需要运行验证。 | 不靠颜色判断错误；能取消录制并继续键盘导航。 |
| P1 | 语义颜色直接用于小字号文字：warning `#F59E0B` 对白底约 2.15:1；深色 accent `#7657E8` 对 elevated `#25282E` 约 3.02:1。[Theme](D:/ai/AgPlayer/app/qml/AgPlayer/theme/Theme.qml:60)、[提示使用处](D:/ai/AgPlayer/app/qml/AgPlayer/SettingsPage.qml:1276) | 区分文字色与填充色，例如 warningText、accentLabel；保留原按钮/波形配色。 | 在实际底色上复核启用状态小字的可读性；普通正文目标 4.5:1，禁用文字另行评估。这里是源色值计算，不是截图取色结论。 |

## 播放器及公共界面建议

| 界面 | 保留 | 具体优化建议 |
|---|---|---|
| 经典播放器 | 封面、标题、波形与主播放键的布局简洁，尺寸紧凑。[截图](D:/ai/AgPlayer/build/qa/uiux-audit-20260907/classic.png) | P2：六个技术信息胶囊可收敛为一行次级信息；BPM 等当前任务相关信息保留突出，文件大小等低频信息放详情。避免每个值都用边框争夺注意力。 |
| 一体式播放器 | 三栏结构和固定底部播放控制明确。[深色](D:/ai/AgPlayer/build/qa/uiux-audit-20260907/integrated.png)、[浅色](D:/ai/AgPlayer/build/qa/uiux-audit-20260907/integrated-light.png) | P2：歌曲搜索目前位于列表底部，建议移到列表上方并与列表标题对齐；评分/BPM 可作为次级筛选。标签数为 0 时显示简短“添加标签后可为歌曲分类”引导，保留现有折叠按钮与用户展开偏好。 |
| 专业/滚动模式 | 保留 CUE、速度、BPM、节拍与双波形工作方式，不削弱专业能力。[截图](D:/ai/AgPlayer/build/qa/uiux-audit-20260907/rolling.png) | P2：将工具栏按“播放 / 网格 / 速度与调性 / 显示”分组，以留白分隔；网格校准高级操作按状态出现。保持数字与单位对齐，避免增加卡片边框。 |
| 独立歌曲列表 | 表头、行密度、选中/正在播放状态应保留。[截图](D:/ai/AgPlayer/build/qa/uiux-audit-20260907/list.png) | P2：同步一体式搜索布局，搜索框文字不要被默认宽度长期压缩；输入非空时提供就近清除，与“清除全部筛选”区分。长标题与大曲库另做验证，不能依据单曲截图判断。 |
| 标签管理 | 复用当前标签组件、计数与颜色能力。[截图](D:/ai/AgPlayer/build/qa/uiux-audit-20260907/tags.png) | P2：区分“没有标签”“搜索无结果”“标签内没有歌曲”；用选中标记配合颜色，避免把用户自选红色标签误当报错。减少空侧栏的解释缺失，不按测试数据空白面积盲目缩小列表。 |
| 迷你播放器 | 当前结构已经简洁，封面、标题、进度和基础控制足够。[截图](D:/ai/AgPlayer/build/qa/uiux-audit-20260907/mini.png) | 保留；只做小控制点命中范围、键盘焦点、长歌名及 125%/150% DPI 验证。暂不增加常驻设置入口。 |
| 沉浸模式 | 保留原生场景及配色/性能预设。[截图](D:/ai/AgPlayer/build/qa/uiux-audit-20260907/immersive.png) | P2：设置面板收起后记住用户状态，预设选择后减少常驻操作干扰；播放队列目前从右侧 20 DIP 隐形悬停区域触发，补明确队列按钮及键盘入口，保留现有悬停方式。[队列源码](D:/ai/AgPlayer/app/qml/AgPlayer/components/ImmersiveQueueDrawer.qml:50) |
| 歌词（静态） | 保留来源标注、纯文本状态、时间偏移与 LRC 导入。 | P2：失败状态优先显示可读原因与重试/导入入口，各提供方尝试明细收进“详情”；当前错误时会列出多条来源尝试。[源码](D:/ai/AgPlayer/app/qml/AgPlayer/components/LyricsPanel.qml:605) |
| 视频（静态） | 已有加载/错误文字、共享播放控制、速度、全屏及返回音频入口。 | 保留基础能力；验证窄窗控件空间、退出全屏焦点和错误后的恢复，不扩展为复杂视频产品。[源码](D:/ai/AgPlayer/app/qml/AgPlayer/components/VideoPlaybackView.qml:32) |
| 空库/导入反馈（静态） | 空库已有导入按钮、支持格式与导入进度/错误列表。 | 复用这些现有组件来补工具空态；错误信息保留可行动的文件/原因，不只给失败数量。[空库](D:/ai/AgPlayer/app/qml/AgPlayer/components/EmptyLibrary.qml:61)、[导入反馈](D:/ai/AgPlayer/app/qml/AgPlayer/components/ImportStatusPanel.qml:60) |

## 六个音频工具建议

| 界面 | 视觉与交互建议 |
|---|---|
| 音频编辑 | P2：顶部 64 DIP 左右的大命令块、底部播放条及两行常驻快捷键占用较多高度；保留编辑命令，缩短常驻帮助为一行，将“全部快捷键”展开，为波形留空间。[截图](D:/ai/AgPlayer/build/qa/uiux-audit-20260907/tool-0.png)、[代码](D:/ai/AgPlayer/app/qml/AgPlayer/components/tools/AudioEditorPage.qml:945) |
| 格式转换 | P1 先补确认内容。P2：空任务区补就地添加引导；常规参数与编码器等高级选项分层。失败筛选时显示“重试失败项”，目前单项重试藏在文件名列右键菜单中。[截图](D:/ai/AgPlayer/build/qa/uiux-audit-20260907/tool-1.png)、[重试入口](D:/ai/AgPlayer/app/qml/AgPlayer/components/tools/FormatTaskTable.qml:255) |
| 元数据编辑 | P1 先补清空恢复。P2：修改摘要只突出真正变化；目前尚未修改也逐行显示九个绿色“保留原值”，易产生已成功处理的视觉印象。未修改时一句“尚未修改字段”即可。[已加载截图](D:/ai/AgPlayer/build/qa/uiux-audit-20260907/metadata-loaded.png) |
| 文件名处理 | P1 先解决添加/删除语义。P2：右侧冲突统计与底部三块统计重复，合成紧凑摘要，将空间让给原名→新名对照；高亮发生变化的文字，提供完整文件名查看。[截图](D:/ai/AgPlayer/build/qa/uiux-audit-20260907/tool-3.png)、[重复统计](D:/ai/AgPlayer/app/qml/AgPlayer/components/tools/FilenameProcessPage.qml:1103) |
| 人声伴奏分离 | P2，视觉优化收益最高：默认主流程突出“选文件→选模型/音轨→开始”；模型仓库、提供商、目录和运行环境移入可展开的模型管理。初次未配置用清晰设置引导，实际安装失败才突出红色错误。保留一键配置和真实下载状态，不添加装饰卡片。[默认截图](D:/ai/AgPlayer/build/qa/uiux-audit-20260907/tool-4.png) |
| 无损鉴别 | P1 先修窄窗状态标签。P2：顶部与底部“开始分析”入口可合并，形成固定执行位置；保留任务→证据→结论结构。评分旁放“非正确率”短注，避免必须读到底部才理解数值边界。[默认截图](D:/ai/AgPlayer/build/qa/uiux-audit-20260907/tool-5.png)、[评分说明](D:/ai/AgPlayer/app/qml/AgPlayer/components/tools/LosslessConclusionPanel.qml:558) |

880×560 的额外发现：[分离页](D:/ai/AgPlayer/build/qa/uiux-audit-20260907/separation-compact.png) 已做工作台/输出/记录分页，应保留；但首屏模型卡区域被底部试听/导出区压缩，顶部名称也被省略。建议减少未产生结果时的导出区占用，提供明显滚动提示或模型选择器。本次截图不能证明滚动后无法访问，因此不将裁切直接定性为功能不可用。

## 设置、EQ 与弹窗

| 界面 | 建议 |
|---|---|
| 常规 | 保留分类与保存/取消流程；将说明性括号作为次级帮助，降低长标签负担。[截图](D:/ai/AgPlayer/build/qa/uiux-audit-20260907/settings-0.png) |
| 播放与音频 | 将系统默认应用入口与输出设备设置的关系解释清楚；保留独占、自动采样率和响度分组；运行时警告使用可读文字色。[深色](D:/ai/AgPlayer/build/qa/uiux-audit-20260907/settings-1.png)、[浅色](D:/ai/AgPlayer/build/qa/uiux-audit-20260907/settings-light.png) |
| 外观与波形 | 保留四种波形的预览缩略图；将采样密度、聚合算法等放进高级波形参数，减少设置页纵向长度；步进器补焦点与“增加/减少××”名称。[截图](D:/ai/AgPlayer/build/qa/uiux-audit-20260907/settings-2.png)、[步进器](D:/ai/AgPlayer/app/qml/AgPlayer/SettingsPage.qml:718) |
| 音频工具预设 | 文件覆盖策略默认显示被截断，建议选择框只显示短策略名，把示例放在下方帮助或展开项；保留导出路径与参数默认值。[截图](D:/ai/AgPlayer/build/qa/uiux-audit-20260907/settings-3.png) |
| 键盘 | 除 P1 冲突与录制问题，将 MediaPlayPause 等机器名称显示成“媒体播放/暂停”，保留实际按键；解释组合动作的两组键值。[截图](D:/ai/AgPlayer/build/qa/uiux-audit-20260907/settings-4.png) |
| 缓存与数据 | 将“清理按钮”标题改为“清理缓存”；按钮可配对应占用大小或解释清理对象，明确与歌曲文件的区别，保留全部清理的危险动作视觉。[截图](D:/ai/AgPlayer/build/qa/uiux-audit-20260907/settings-5.png) |
| 关于 | 保留轻量页面、官网与真实更新状态；服务未配置时已有诚实提示，应保持。大面积留白无需用营销文案填充。[截图](D:/ai/AgPlayer/build/qa/uiux-audit-20260907/settings-6.png) |
| 18 段 EQ | 保留现有频段、频响曲线、前级和键盘精细/粗调，主界面无需重排。为双击归零补可发现的提示/键盘操作，数值可编辑作为可选改进。[截图](D:/ai/AgPlayer/build/qa/uiux-audit-20260907/equalizer.png)、[单段控件](D:/ai/AgPlayer/app/qml/AgPlayer/components/EqualizerBandSlider.qml:78) |
| EQ 预设弹窗（静态） | 复用 ThemedDialog/TextField/IconButton；补字段持久标签、关闭按钮名称及空预设/保存失败提示。当前自设圆角、原生输入框与公共样式存在分叉。[源码](D:/ai/AgPlayer/app/qml/AgPlayer/EqualizerWindow.qml:788) |
| 歌单/导出/覆盖/丢弃弹窗（静态） | 保留已有确认与未保存保护；统一标题、主次按钮、默认焦点和 Esc 行为。具体文件操作保持既有授权边界，不为了本轮查看而执行写入/删除。 |

## 推荐实施顺序与停止条件

1. **先修可用性**：窄窗状态名称、设置搜索/分类键盘、元数据恢复、文件名语义、转换确认、焦点与文字色。以操作后果清楚、键盘可到达为验收。
2. **再做视觉整理**：人声分离信息分层、播放器筛选栏、元数据摘要、文件名重复统计、编辑器帮助区域；每轮只改变批准区域，对照前后截图。
3. **最后统一边缘状态**：EQ 弹窗、歌词失败、队列入口、长标题/路径、英文与 125%/150% DPI。无需重做 Theme、引入新框架或新增持续动画。

相关公共组件变动后优先运行现有 `qml_design_system_test`、`ui_design_system_usage_contract_test` 和受影响页面测试，并按变动范围补真实截图/交互。本次没有执行这些回归测试，因此不提供测试通过或发布就绪结论。

Qt 的可访问性实现应继续利用现有 Controls 和 Accessible 的名称/角色等属性，可参考 [Qt 官方可访问性说明](https://doc.qt.io/qt-6/accessible-qtquick.html)。适配项目实际 Qt 版本，不为此引入新版专属 API。

## 截图索引

所有 26 张图片均由本次现有 Release 程序生成并已查看。

- 播放器：`classic.png`、`integrated.png`、`integrated-light.png`、`rolling.png`、`mini.png`、`list.png`、`tags.png`。
- 其他模式：`equalizer.png`、`immersive.png`。
- 六工具：`tool-0.png`（编辑）、`tool-1.png`（转换）、`tool-2.png`（元数据）、`tool-3.png`（文件名）、`tool-4.png`（分离）、`tool-5.png`（鉴别）。
- 补充工具状态：`metadata-loaded.png`、`separation-compact.png`、`lossless-compact.png`。
- 设置：`settings-0.png` 至 `settings-6.png`，以及 `settings-light.png`。

本报告是优化建议清单，不代表所有建议均已获批准实施，也不代表“全部 UI 的所有状态已验收”。
