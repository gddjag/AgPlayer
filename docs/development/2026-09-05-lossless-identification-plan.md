# 无损鉴别实施计划

> 执行方式：superpowers:subagent-driven-development。用户已于2026-09-05确认架构，按阶段连续实施，不重复审批。

Goal：实现真实、只读、可解释的离线批量鉴别与参考图工作台。
Architecture：Core原始精度输入/证据分析，Qt有界后台模型，QML显示；SACD轨道reader独立；复用FFmpeg和现有Theme/控件。
Tech Stack：C++17、Qt6.7、FFmpeg、CMake/CTest、PowerShell测试工具。
Spec：`docs/development/2026-09-05-lossless-identification-design.md`；完整需求：`C:/Users/Administrator/Desktop/AgPlayer-无损鉴别-专业级开发提示词.md`。

## 全局约束

- 原始文件只读；不联网分析、不增加产品运行时/模型/库；保留源精度。
- 流式处理、任务额外内存目标96MiB，取消目标500ms，静止页面零持续分析。
- 默认并发2，上限4；缓存预算64MiB，计入模型保留结果估算，最多1500任务；保留256点频谱，时频图按需加载。
- 新toolId=5，不改变原工具语义；图片中的数据不可硬编码。
- 包体积增量目标3.5MiB、硬上限5MiB；Debug/Release、真实音频、UI和错误路径均需验证。
- 不提交、不合并、不发布；所有实现限本worktree，基线快照保持不动。

## 任务与所有权

### 1. Core真实分析（Core worker）

文件：`core/src/decoder.*`、`core/src/lossless/lossless_analyzer.*`、`core/src/lossless/lossless_types.hpp`、`tests/core/lossless_analyzer_test.cpp`、`cmake/LosslessCore.cmake`。

对外提供 `agplayer::lossless::analyzeFile(const std::string&, const AnalysisOptions&, const std::atomic_bool&, ProgressCallback)` 返回结构化 `AnalysisResult`。先建立类型并向Qt worker发布，再实现。Result覆盖版本、文件/源格式、判定、confidence、evidence、candidates、spectrum、按需spectrogram、覆盖率/失败。

- [ ] 用真实生成的WAV写测试：静音返回Inconclusive；32bit低位不丢失；低通不单独判转码；取消终态；不同采样率Nyquist正确。
- [ ] 运行失败测试，记录预期缺失而非语法错误。
- [ ] 共享Decoder生命周期扩展原始精度读取；复用FFT或现有av_tx；逐块汇总统计，命名阈值、证据冲突保守回退。
- [ ] 实现DSD独立证据规则，与主代理SACD输入层接线。
- [ ] 运行Core测试，发送命令与结果；不自行修改顶层CMake或其他worker文件。

### 2. Qt批处理闭环（Qt worker）

文件：`qt/src/lossless_analysis_controller.*`、`qt/src/lossless_task_model.*`、`qt/src/lossless_report.*`、`tests/qt/lossless_analysis_controller_test.cpp`、`cmake/LosslessQt.cmake`。

UI契约：全局单例名 `LosslessAnalysisController`；属性 `tasks` (QAbstractItemModel)、`selectedResult` (QVariantMap)、`running`、`stopping`、`progress` (0..1)、`completedCount`、`totalCount`、`selectedCount`、`concurrency`、`filter`、`searchText`、`statusText`、`error`、`counts` (QVariantList五类计数)。方法 `loadFiles(QVariantList urls)`、`addFolder(QUrl)`、`start()`、`cancel()`、`retrySelected()`、`removeSelected()`、`clear()`、`selectTask(QString id)`、`setChecked(QString id,bool)`、`selectAll(bool)`、`exportReport(QUrl,QString format)`。

行roles：`taskId,fileName,filePath,formatName,audioFormat,verdictCode,verdictText,confidence,state,stateText,checked,progress`。selectedResult keys：`taskId,fileName,path,formatName,codec,sampleRate,bitsPerSample,channels,durationMs,fileSize,modified,verdictCode,verdictText,confidence,coverage,cutoffHz,effectiveBits,resamplingText,holesText,evidence,candidates,chain,spectrum,spectrogram,error,warnings`；evidence为`{text,value,unit}`列表，spectrum为dB数值数组(0..Nyquist等距)，chain为字符串列表。无结果用空值，不造示例。

- [ ] QtTest测试去重、并发上限、选择筛选与源文件哈希、取消/销毁、缓存失效、JSON/CSV原子报告。
- [ ] 创建失败证据；实现有界队列/线程池、generation隔离、主线程model更新、后台发现/报告、LRU。
- [ ] 与Core类型适配，UI契约改动需通知UI worker和主代理。
- [ ] Core未就绪时允许契约测试用注入测试函数；产品不得使用假结果。
- [ ] 运行Qt测试，报告遗漏和失败，主代理负责QML注册/顶层构建接线。

### 3. 参考图工作台（UI worker）

文件：`app/qml/AgPlayer/components/tools/LosslessIdentifyPage.qml`及其子组件、`app/qml/AgPlayer/theme/Theme.qml`、`qt/src/lossless_evidence_item.*`、`tests/qml/tst_lossless_identification.qml`、`cmake/LosslessUi.cmake`。

- [ ] 先写UI契约/交互测试：空状态、选择详情、筛选、按钮启用/取消、尺寸。
- [ ] 按已确认图实现三栏/顶部操作/底栏；读取真实控制器契约；绘图用紧凑数值与按需QQuickItem，不持续重绘。
- [ ] 复用Themed控件，Theme仅新增必要语义色；不改别页默认值。
- [ ] 加文件/文件夹对话框；播放列表添加及定位向外发信号，由主代理接线。
- [ ] 验证QML和小窗口；主代理提供真实截图后修正像素偏差。

### 4. SACD、集成与验收（主代理）

文件：`core/src/lossless/sacd_reader.*`、`tests/core/sacd_reader_test.cpp`，各层CMake、qml_registration、AudioToolsController、AudioToolsWindow、ToolSidebar、app/main.cpp、测试宿主、QA/语料脚本、翻译。

- [ ] 固定feature/baseline两份相同源码及依赖；构建baseline并记录对照包，保留其他任务修改清单。
- [ ] SACD格式依据核实后写边界/坏数据测试；实现只读TOC/轨道/DSD-DST帧解析及真实样本测试。
- [ ] 接入toolId5、native drop、QA命令、当前播放列表与定位；窗口隐藏/切页请求取消。
- [ ] CMake接线，构建测试；优先局部红绿验证，集成后Debug/Release与已有工具回归。
- [ ] 生成真实转码/扩位/升频和反例语料，记录哈希/编码命令/混淆矩阵，不把合成数据当真实母带准确率证明。
- [ ] 真实应用截图1672×941完成/分析、880×560、4K200%、空/失败；对照参考并修复。
- [ ] 实测资源、源哈希、缓存/导出/定位；同配置包体积比较；最终Diff Review与独立审查。

## 进度记录

### 第二轮准确度优化（1.4 / params-7，专业验收仍未完成）

1. 已查看的b02转为诊断集，禁止复用作新盲测；预声明20个此前未用SQAM轨及编码参数，c01仅在算法冻结后扫描。
2. CELT维持120采样重叠和原判定门槛，补240/480采样帧（5/10ms）及独立逆变换、周期和实际控制回归；报告明确匹配帧长。
3. 高码率MP3先验证更长片段和稳健聚合；没有实际收益的方案不进入生产，不因漏检降低门槛。
4. 对高采样率PCM以现有Swr double流式逆采样寻找残留MDCT证据，保留原始格式和量化测量；单凭逆采样证据不宣布完整升频链。
5. 最终按来源等权报告诊断结果，不以同源变体数缩小区间替代独立证据。Debug/Release、真实界面、资源和源文件保护复核后更新验收记录。
6. params-6的c01首次盲测有损召回64.375%、FIR反例2/20误报，失败证据保留。params-7撤销缺乏独立编码证据的频谱凹口判定，新增红绿回归；同集修复结果不得重新当作盲测。交接仅可作为开发预览集成，专业准确度目标仍未完成。

### 专业准确度优化轮次（1.3 / params-5）

- 已修复平坦度误报、低电平谱测量和无证据时错误可信结论；接入多相残差、MDCT与CELT帧结构及独立数学/反例回归。
- 已建立源分组开发/留出语料、参数挑战、变换鲁棒集、版本冻结与中性文件名评估，保留命令和哈希。
- 已完成Release/Debug各18项定向回归；冻结盲测24/36隐藏异常检出，结果未达专业验收。盲测后不得继续调参并复用同集宣称未见数据。
- 后续验收必须补充独立真实来源、原生HiRes/DSD对照、不同编码器与置信度校准；数值门槛待明确。实施和验证证据统一见验证记录，不将此轮结束视为整个需求完成。

### 用户要求继续完善后的修正轮次

1. 为瞬态前置能量补真实流式测量；未观察到合格瞬态时保留未测状态，指标不单独参与来源判定。
2. 将内部分析精度与显示频谱点数隔离，修复反相/静音声道对频谱的错误影响，使用真实多声道WAV回归。
3. 修复按需时频缓存、筛选计数、删除后排序、源文件中途变化与终态摘要；报告增加协作取消并保持原子性。
4. 按参考图修正转换链、标题、指标表和操作区；依据提供可键盘访问的阈值/区间说明。
5. 统一运行两配置定向回归、9个瞬态/声道控件与已有语料、真实UI截图、资源与包体积复测。结果写入验证记录，未验证项不自动勾选通过。

- 2026-09-05：架构确认，已建立独立feature/baseline worktree并复制相同既有修改；未开始功能验证。
- 2026-09-05 实施后：Core/Qt/QML、SACD 只读路径、导航/播放列表/定位、报告与翻译已接入。Debug/Release 已构建并开展定向回归；真实生成语料、源哈希、多尺寸截图和资源测量已取得证据。上方清单为原始任务拆分，不作为验收通过清单；逐项实际状态与未完成项统一维护在 `2026-09-05-lossless-identification-validation.md`。专业识别准确率尚未达标，不能将本计划视为全部完成。

## 第三轮准确度优化（进行中）

基于主线792b732的独立工作树 `codex/lossless-accuracy-round3`，旧params7补丁与人工测试包保持不变。目标是实际减少隐藏来源漏检，同时保留既有反例和拒判规则；不通过降低门槛或重复使用盲测数据宣布达标。

1. 补齐params7在已查看c01全部220文件的回归，明确作为development，保留原params6首次盲测。
2. 独立验证MP3混合滤波器组与简化MDCT探针之间的方法缺口，拒绝无反例区分能力的周期启发式。
3. 验证CELT 120采样短变换及瞬态漏检，先建立独立样本域失败测试，再选择有真实增益的最小修复；保持原z、相干性和峰宽门槛。
4. 冻结任何新算法后再做未见来源/参数检查，并保存版本、输入哈希及原始预测。既有SQAM多变体不能替代跨语料库或独立来源置信区间。
5. 分别记录生产变更、失败原型、自动化测试和真实文件证据。专业数值标准、原生HiRes/DSD与置信度校准缺口仍保留；提交和打包另行协调主线。

2026-09-06：1.6/params9 已实现MP3按需混合变换及CELT120/48深检；新来源112首次验证73/80有损检出、FIR0/16误报。当前是专业验收未通过的开发候选，完整验证和限制见验证记录。

## 第四轮持续优化（进行中）

用户明确要求专业验收未达标时继续执行。保留第三轮1.6首次盲测、补丁和失败记录，当前工作树继续开发，不能当成第三轮冻结补丁。

1. 从已见d02漏检逐片段双频带曲线定位原因，比较预先固定的观察长度/时间分布，保留原z、相干性与峰宽门槛；同源控制按相同规则执行。
2. 测量并减少CELT重复对数计算；以冻结1.6交错运行做耗时和数值对照，实际结果决定是否保留。
3. 准备非SQAM来源与来源分组，锁定后再运行新候选；格式无损不自动视为原始无损真值，同一录音的衍生样本不能扩大独立来源数。
4. 每次新增判定需独立失败条件、反例、集成验证和冻结记录。用户未提供具体数值标准时继续按严格工程候选检查，不能自称专业认证或把样本通过代替总体结论。

2026-09-06第四/五轮：1.9/params12新增CELT192/768有界深检、单对数优化、逆采样滤波和复合链聚合、合格栅格展示；Release11/Debug5通过，142开发回归+6/0退化，新WTC5项4正确/1弃权。剩余FIR升频证据弱、DSD前史证据不足，未通过专业验收；详细输入限制与原始失败记录见validation。未提交/打包。

## 第八轮：保持鉴别结果的性能优化（2026-09-07，1.13候选已冻结）

以已冻结1.11/params14为基线，固定MP3短片段、需备用声道的原生对照、整曲音乐和64声道输入；使用GetProcessTimes记录CPU、现有QA入口记录私有提交峰，候选与基线交错复测。同批源SHA及完整导出测量前后核对，不降低阈值/锚点/深度或截短输入换速度。分别定位STFT重复Hann计算/分配与hybrid MDCT/log热点，只接入经过数值oracle和实测支持的优化；保留旧补丁，无提交/打包授权。

最终1.13保留Hann预计算与log4，舍弃1.12全scratch缓存，将缓存数组收窄至224KiB；144个实际生产窗口逐位相等，最终52次文件回归、Release三项及Debug完整控制器通过。三批整文件测量存在持续外部索引争用和相反方向结果，全部保留；局部CPU收益不作整曲提速或专业准确率验收结论。当前可审阅候选的实施与验证见validation第八轮，稳定整曲速度验收仍未通过。

## 第七轮：专业鉴别持续优化（2026-09-07，1.11候选已冻结）

保持1.10/params13冻结候选及低误判优先契约。围绕真实u04 MP3漏检进行子带/锚点诊断，独立生成固定无编码PCM分块处理反例；不直接降低门槛、不修改未知WAV来源标签。另固定24个此前未测本地MP3，先划分12开发/12留出，原件只读并记录SHA，留出预测在下一候选冻结前保持未读。修复必须有独立失败条件、反例和回归，任何新增资源成本要测量；源码变更和验证记录分别留存，不把本轮结束或单组检出率当专业验收。

## 第六轮：低误判优先（用户已授权）

用户明确没有样本，并委托采用合理工程标准、优先降低错误率。数值选择和公开/可复现样本准备由开发承担，不再把用户提供标准/母带当成继续工作的前提。项目目标：明确制作链反例误判率<=1%、高置信度(>=80)误判0；证据不足弃权，检出率及弃权率必须单报。真实总体误判率声明仍需代表性独立验证及区间，不把小样本0错误或合成种子变体当作证明。保留历史冻结报告；当前pipeline回归与代表性统计验收分开。

本轮先复核弱AM的完整pipeline、DSD低带宽来源推断及其确定构造反例；只有复现的错误才修改规则，不提高显示置信度来凑目标。
