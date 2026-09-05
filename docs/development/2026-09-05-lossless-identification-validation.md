# 无损鉴别验证记录

状态：开发预览，专业识别准确率验收未通过。以下为实测证据；不能据此宣布整个需求完成。

## 第二轮准确度优化（最终lossless-1.4 / lossless-params-7，专业验收未完成）

本轮新增CELT 240/480采样帧（5/10ms），默认960采样帧不变；保持120采样重叠及原有z/相干性/峰宽门槛。三个profile先判断是否满足完整门槛，再比较显著性，不能被高z但不合格的候选覆盖。`celtFrameSamples`在未测时导出null，测得时报告实际候选帧长。[RFC6716第4.3.7节](https://www.rfc-editor.org/rfc/rfc6716.html#section-4.3.7)及Xiph参考实现用于核实变换尺寸，不作为检测准确率证明。

高采样率PCM另开44.1/48k double分析视图，使用现有Swr、2048帧固定缓冲及既有MDCT，不改变源样本、源格式或量化统计。只在88.2–768k支持范围运行，DSD/DST隔离。逆采样视图排除未经验证的1152近似profile；证据只支持历史有损推断，不单独给出完整升频链。`mdctAnalysisSampleRate`导出分析视图采样率，原始`sampleRate`保持不变。

### 改进与诊断证据

- 旧b02已被查看，本轮仅用作诊断：5ms旧漏检新增3/4命中，剩余track10继续弃权；两个开发源10ms实际Opus均命中。18个48k PCM/FIR/dither控制 × 三profile无有损误报；120/240/480/960周期噪声及正弦反例回归通过。
- 6个开发源AAC→96k及钢琴AAC→96k均检出历史有损，6个同源PCM往返未错误判有损；C++接入结果见 `build/qa/round2-development-scan.json`。完整复合链仍不能据此宣布正确。
- 高码率MP3长片段4→12→32帧、逐帧归一化及稳健聚合未提高钢琴两项检出；不降低2dB门槛，也不把更慢但无新增命中的方案进入生产。见 `build/qa/mdct-mp3-long-findings.md`。
- 逆采样封装使用独立直接cosine IMDCT＋手写sinc上采样正例验证，另覆盖one-shot/127分块、反相、flush幂等、原生单音、静音、非法输入与取消。测试不依赖把实现本身当真值。
- 真实应用截图 `build/qa/lossless-ui/round2-{short,inverse,inverse-en}.png` 已检查；均为真实分析，新增帧长与分析视图证据可见，源文件96k信息保持。长证据在右栏滚动，底部按钮保持固定；未声称像素级完全一致。

### 新来源验收与交付

c01在首次读取预测前锁定20个此前未使用的SQAM轨道：08、11、13、16、18、20、22、25、28、32、36、39、41、43、45、48、51、55、60、65。每轨1秒起12秒，11种预声明变体共220文件。版本、源SHA、编码命令与中性文件名固定；原200条准备版本保留为historical-200，最终首次扫描使用220条。新旧轨道无重叠，但均来自同一SQAM集合，不能代替跨语料库/原生HiRes/DSD验证。

来源等权只报告每来源率的算术平均，明确保留未定义分母；它不是置信区间、校准或认证。逐文件Wilson仍仅用于诊断，同源多变体继续强制候选gate为ineligible。复合链同时报告有损family召回、完整精确及部分命中，防止把只检出一段链计为完整正确。

**冻结params-6盲测失败，未覆盖原记录。** `build/qa/evaluation-c01/evaluation.json`、`scan.json`和`frozen-artifacts.json`保存首次预测，220文件分析失败0。该轮精确103/180、部分20；有损family103/160（64.375%），明确FIR反例2/20误报。来源等权有损召回同为64.375%，FIR误报10%，不满足建议工程门槛，更不能专业认证。

| c01预声明变体 | 首次盲测结果 |
| --- | --- |
| AAC256 | 20/20 |
| AAC256→96k | 20/20只检出有损阶段，完整链0/20 |
| MP3 320k / q3 | 1/20、7/20 |
| Opus96k 5 / 10 / 20ms | 10/20、12/20、16/20 |
| Vorbis q7 | 17/20 |
| 44.1→96k | 20/20 |
| FIR低通反例 | 2/20被错误判有损64，source18与43 |

**params-7修复已观察到的反例，不重新称盲测。** 两例未满足MDCT/CELT条件，实际触发旧截止＋空洞规则。独立审查确认`codecHoleScore`只是局部FFT凹口的跨窗出现比例分位数，没有编码帧/量化栅格证据；滤波与乐音谐波能同时形成截止和凹口，不能把它们当独立证据。已撤销该独立有损分支及依赖同条件的复合链分支，测量保留为中性。`tests/scripts/qa_lossless_guard_test.ps1`先用冻结旧EXE实际失败，再用params-7通过两例；红绿数据为`round2-hole-guard-{red,green}.json`。不得将修复后的同集回归当作新的未见数据或声称已达到总体准确率。

最终params-7扩展到40个已查看控制文件（20原PCM、20FIR）均回退为无法确定，源哈希不变，见`build/qa/round2-params7-controls.json`。这只证明这些已知控制的回归修复；未用params-7重扫全部有损变体，不能沿用params-6检出率作为最终版本数字。新220源文件整体SHA复核无变化，`round2-source-hashes.json`。待进一步补足高码率MP3识别、跨语料库验证、原生HiRes/DSD和置信度校准，专业准确度目标仍未完成。

params-6通过Release19/19（151.66s）；Debug18/19，`audio_engine_test`因decode thread did not prepare PCM断言后超时，不能抹去此失败。新Core Debug实际259.36秒通过。本轮新增测试和测量序列化回归先失败后通过。第一次Core失败为旧测试比较“分析完成时钟”而非文件创建时刻，已移动取时点、保留2秒断言；第二次旧60秒整套预算超时，本目标现在包含大量额外视图扫描，套件超时设为300秒，实际Release73.29秒。未放宽任何判定断言或产品延迟指标。日志 `round2-inverse-green-tests.log`、`round2-core-time-green-test.log` 保留失败。

单任务CLI资源采样：180秒192k文件32.85秒、峰值15,622,144B；12秒48k短帧6.13秒、峰值14,233,600B。当时同时运行盲测与Debug任务，只用于当前负载资源边界，不用于声称加速；来源为 `build/qa/round2-resources.json`。本轮按主线统一交付安排不再打包，1.3安装包不能作为1.4交付产物。

最终params-7受影响回归：Release Core/Qt控制器/audio_engine三项通过（114.43秒）；Debug Core214.49秒、audio_engine1.78秒通过，Qt首次失败定位为实际DSP约11.05秒超过旧10秒等待上限。仅此真实WAV功能测试的Debug等待预算调整为30秒，Release仍10秒，完成状态、频谱、时频图和缓存断言均保留；最终单独Qt回归Release8.74秒、Debug26.15秒通过。不能把分批通过写作最终版本一次性全套19/19，也未修复或掩盖此前audio_engine负载下的失败。证据为 `round2-params7-*-tests.log`、`round2-params7-debug-controller.txt`、`round2-final-*-test.log`。

最终中英文误报控制界面 `build/qa/lossless-ui/round2-guard-fixed{,-en}.png` 已实际运行并检查；本轮五份UI日志无QML错误。当前代码可冻结为主线审阅/集成候选，专业准确度验收仍未完成，不满足主线全部完成后打包门槛。完整功能补丁和基线文件清单输出到 `build/qa/lossless-integration.patch`、`lossless-integration-files.json`、`lossless-integration-handoff.json`；补丁相对于原始基线快照，主线应用前必须检查并协调并发改动。本任务不自行提交或打包。
## 上一轮准确度历史（lossless-1.3 / lossless-params-5）

本轮已修复频谱平坦度几何/算术均值使用不同频点集合造成的误报，保留绘图下限以下的原始FFT测量，并加入多相残差、MDCT及CELT帧结构证据。静态低通不再直接等同升频，宽频带与健康量化不再直接证明原始无损。置信度仍为未校准的工程证据分数，不是正确概率。

### 冻结与未见数据验证

在读取预测前固定1.3/params-5、4个留出录音源和编码命令；最终盲测使用48个中性文件名，冻结哈希见 `build/qa/evaluation-b02/frozen-artifacts.json`。算法仅收到文件路径。原b01的32文件准备记录保留但未扫描；b02在首次扫描前扩充16个参数挑战，未根据盲测结果调参。明文真值保留在评估侧，因此这是文件名屏蔽、源分组留出的工程盲测，不是第三方双盲认证。

EBU SQAM共10个12秒录音源：6个开发、4个留出；80个标准变体和16个留出参数变体并不等于96个独立录音。官方素材仅用于本地研发，不随产品打包。原PCM的制作前史未知，保守列为未评分控制；明确FIR处理反例另计。来源：[EBU SQAM](https://qc.ebu.io/testmaterials/523/)，归档SHA256为 `7D6FCD0FC42354637291792534B61BF129612F221F8EFEF97B62E8942A8686AA`。

| 范围 | 本轮实际结果 | 证据（build/qa下） |
| --- | --- | --- |
| SQAM开发集 | 隐藏异常27/30；18控制均弃权 | `lossless-13-freeze-development-evaluation.json` |
| 留出标准参数 | 隐藏异常16/20；12控制均弃权 | `evaluation-b02/evaluation.json`、`stratified.json` |
| 留出不同参数 | 8/16；AAC256为4/4，MP3 q3为1/4，Vorbis q7为3/4，Opus128/5ms为0/4 | 同上 |
| 留出总计 | 隐藏异常24/36（66.7%）；有损20/32（62.5%）、升频4/4；12隐藏异常弃权，无分析失败 | 同上 |
| 明确反例误报 | 留出FIR反例0/4；其余8原PCM/抖动控制未评分，不能并入真实无损分母 | 同上 |
| 额外48k控制 | 有损断言0/18；这些文件有44→48重采样史，不作为无重采样负例 | `lossless-celt-48k-controls/evaluation.json` |
| 增益/裁切/加噪挑战 | 17/54检出，其余37弃权；无错误来源断言 | `lossless-13-freeze-robustness-evaluation.json` |
| 真实钢琴诊断集 | 隐藏异常从0/10提高至6/10；另6已知有损容器单独统计 | `lossless-13-freeze-music-evaluation.json` |
| 两配置定向回归 | Release18/18（66.99s）、Debug18/18（163.52s） | `lossless-accuracy-*-tests.log` |
| 数学/边界回归 | 独立直接MDCT夹具、反相/静音声道、非整块流式输入、周期音调/周期噪声、巨大有限数、取消、原生低通及有理重采样 | Core/MDCT/CELT目标，包含在18项中 |
| 资源实测 | 180秒192k双声道11.17s、峰值13,619,200B；12秒48k CELT样本1.70s、峰值13,778,944B | `lossless-accuracy-resources.json` |
| 源文件保护 | 7套语料200文件分析后哈希无变化；盲测副本另由评估器核对哈希 | `lossless-accuracy-final-source-hashes.json` |
| 最后语言修正 | 候选来源初次英文/切换回中文均翻译，原始JSON保留原文；Release1/1（8.89s）、Debug1/1（16.73s） | `lossless-accuracy-translation-*-tests.log` |
| 独立审查与评估回归 | MDCT/CELT索引、固定内存、多声道、数值及取消路径无确定阻碍；修复评估失败行被计入反例分母/TP的问题，脚本回归通过，重算盲测仍24/36 | `tests/scripts/qa_lossless_evaluation_test.ps1`、`evaluation-b02/evaluation.json` |
| 最终Diff | 既有无关文件变更0；新增50文件无空白问题；跟踪文件diff检查通过 | `lossless-accuracy-final-review.json` |
| 同配置本地预览包 | 基线35,535,894B → 本轮35,786,737B，增加250,843B（0.239223MiB），低于3.5MiB目标 | `lossless-accuracy-package-comparison.json` |
| 独立包运行 | 清空Qt/QML路径、PATH仅Windows目录，真实CELT输入完成，exit0、QML错误0 | `lossless-ui/accuracy-packaged-smoke.json`、`accuracy-packaged.png` |

本轮包SHA256为 `9505F9163980B49B7BBFB4DFCA18F0059807F7C28494824AD5A77B7F8772BE80`，仅本地开发预览，未安装、发布或合并。下方历史包记录不包含1.3优化。英文候选标题漏译已通过真实界面复核；4张新界面日志均无QML错误。首次新增翻译测试失败源于测试用QTranslator默认isEmpty返回true，修正测试夹具后保留原断言通过，失败日志 `lossless-translation-failure.txt` 未删除。

新增变换模块单元测试的取消覆盖是预取消；代码按offset协作检查，尚无这两个新模块运行中取消延迟的独立实测，不能据此宣称硬实时500ms。

运行时间受同机负载影响，本轮未做重复性能基准，不据此宣称较旧版加速。资源是CLI单任务进程私有内存采样，不能替代多任务整窗内存测量。实际界面新增MDCT、CELT及升频路径已截图：`lossless-ui/accuracy-{mdct,celt,resample}.png`；新增证据较长时右栏可滚动，底部完成度和操作保持固定。截图不能证明像素级完全一致。

### 尚未达到专业验收

有损盲测检出率62.5%低于所提80%工程候选值；精确率观察值20/20不能证明总体≥95%。脚本给出文件级Wilson区间，但同源变体相关，不能将这些区间解释为独立录音总体保证。最终评估器对同源多变体强制候选gate为ineligible，理由 `correlated_source_variants_without_cluster_validation`；保留诊断指标，不靠复制变体获得通过。相关回归已通过。4个明确反例无误报也无法证明≤1%误报率。数值门槛仍待确认，所提门槛不是行业标准。

Opus短帧、高码率MP3、额外噪声及部分复合转码仍漏检；尚缺足够真实原生高采样率、原生DSD与PCM转DSD独立来源、不同编码器实现及置信度校准。已有PCM转DSD样本仅避免误称原生DSD，不代表能检出其PCM来源。不得用调低阈值、同源大量变体或合成控件替代这些验收证据。当前仍为未合并开发预览。

## 上一轮历史记录（lossless-1.2，以下数值不代表本轮准确度）

本轮针对实际缺陷补充回归：显示点数影响判定、反相声道抵消、按需时频图空缓存、筛选数量与结果不一致、删除后排序丢失、分析过程中源身份变化、报告取消以及终态摘要。新增`scripts/qa-lossless-transients.ps1`生成9个确定性时间结构/声道控件，记录真实FFmpeg命令与哈希；它不是音乐来源准确率基准。

瞬态测量使用1ms功率包络，比较事件前3–20ms与30–50ms背景，再对攻击能量归一化。它描述前置能量，不是已校准的编码器识别器。静音、稳态和缺少足够前史的片段维持未测状态，JSON为null、CSV为空；有效事件数另行导出。算法门限是工程观察参数，并非文献认证阈值。

技术背景参考：[Opus开发者的瞬态与短块说明](https://www.opus-codec.org/static/presentations/opus_celt_aes135.pdf)和[Fraunhofer的TNS研究](https://publica.fraunhofer.de/entities/publication/bf3d4461-061b-4ac3-8df2-49a51854d570)。短块/TNS会减少攻击前量化噪声，未观察到前置能量不能证明未经过有损编码；将普通起音、混响或编辑前置能量当成编码历史也缺少依据。本文据此保留保守判定，不将新指标直接加入有损规则。

报告以64KiB为协作取消检查间隔，取消时放弃临时文件，保留现有目标。单次文件系统open/write/commit调用的设备阻塞仍无法保证500ms内打断，不将协作取消宣传为硬实时I/O取消。

本轮已取得的证据：

| 修正 | 验证 |
| --- | --- |
| 显示精度与判定隔离 | 16/64/1024/2048显示点与内部256点的证据、判定和置信度一致；`lossless-refine-core-red.log` → `lossless-refine-core-green.log` |
| 前置能量 | 真实9控件中干净起音6事件、分数0；人工前置6事件、约0.004819；MP3及其FLAC约4.78e-6，均为中性测量；静音/稳态未测 |
| 声道相位 | 修复前反相文件截止0、错误“无活动”；修复后同相/反相/仅右声道均7058.82Hz；mono与2/6声道自动回归通过。见`lossless-refine-channels-red.log`/`green.log`和`lossless-transients-after-channels.json` |
| Qt流程 | 时频图二次请求、筛选计数、排序删除、源变动失败且不缓存、终态翻译保持、报告中途取消保留原目标，均有回归；`lossless-refinement-close-*-tests.log` |
| 两配置集成 | Release16/16（27.31s）、Debug16/16（45.17s），见`lossless-refinement-*-tests.log`；页面关闭报告取消的最后接线，两配置控制器各1/1，见`lossless-refinement-close-*-tests.log` |
| 页面密度与数值 | 1672×941普通结果无需滚动可显示6行文件信息；Tooltip阈值/区间可键盘查看；小数非零值使用科学记数、小文件用KiB，避免显示伪零。最后两配置QML各1/1见`lossless-refinement-precision-*-tests.log` |
| 真实语料 | 17/8/16语料检查仍为12/17、5/8、6/16；隐藏精确分类1/6、2/5、0/10，未改善真实音乐漏检，未出现新增高置信度错判 |

首轮集成曾失败2个目标：Qt切换语言会覆盖新的终态摘要，已修复；QML测试通过页面视觉树查找Popup不可靠，改为从证据行持有的Popup引用检查。右栏无滚动断言首轮使用了776px独立页面夹具，未对应941px整窗；已按941−50−56=835px有效页面复核，保留全部6行底缘断言。失败日志保留在`lossless-refinement-release-first-failures.log`、`lossless-refinement-qml-red.txt`和`lossless-refinement-density-fixture-red.txt`。

当前功率谱按活动声道平均，不再时域混音；工作量随活动声道增加，180秒双声道扫描约从7.25s增至15.83s。不同声道独有的细小编码痕迹仍可能被汇总掩盖，未宣称逐声道来源推断完成。

## 范围与基线

- 用户已确认开发架构，参考原图为桌面 `无损鉴别.png`（1672×941）。
- 功能工作区：`.worktrees/lossless-identification-20260905`，分支 `codex/lossless-identification-20260905`。
- 对照工作区：`.worktrees/lossless-baseline-20260905`，同一 HEAD `b1c8a7c` 与相同既有未提交修改；清单见 `2026-09-05-lossless-baseline-files.json`。
- 本次不提交、不合并、不发布。测试编码器及音频夹具仅位于开发工具/构建目录，不进入产品依赖。

## 已取得的验证证据

| 检查 | 实际结果 | 证据 |
| --- | --- | --- |
| Release 安装包基线 | 35,535,894 字节；SHA256 `0D0D4FD5459637740861C8E52CF47FDE476A09A790340590458A6582DB8F48D7` | 对照区 `build/installer/AgPlayer-Setup-1.0.0-x64.exe` |
| 只读 SACD 输入层 | 15 项通过；覆盖 TOC 越界、坏包、取消、2064 字节扇区、双区/多声道、虚拟流 seek；时码跳帧/重复与 DST 缺中间包已补测修复 | `build/qa/sacd-reader-test.txt`、`build/qa-sacd/red.txt` |
| 多声道区域回归 | 先复现错误 `Invalid SACD audio area TOC`；修为按 TOC 标志识别区域类型后通过 | `build/qa/sacd-multichannel-red.txt` |
| DST 压缩帧往返 | 官方样本 → 合成 ISO → 虚拟 DFF，解码音频 MD5 均为 `ff6f0bdc80f4ee36366f7ec9894eec9d` | `tests/core/sacd_reader_test.cpp::officialDstFramesRoundTrip` |
| Qt 批处理 | 控制器测试目标通过；队列/取消/缓存/报告用注入分析器隔离测试，另有真实 WAV → Core → 256 点频谱 → 缓存重试回归 | `tests/qt/lossless_analysis_controller_test.cpp`、最终 CTest 日志 |
| 原工具回归 | 编辑输入、格式转换、文件名处理、元数据、人声分离的 5 个 QML 测试目标均通过 | `build/qa/lossless-existing-tools-tests.log`、`lossless-vocal-regression-verified.log` |
| 共享解码/播放回归 | `decoder_test`、`playback_session_test`、`audio_engine_test`、`c_api_lifecycle_test`，4/4 通过 | `build/qa/lossless-playback-tests.log` |
| Debug/Release 构建 | 两种配置的 AgPlayer 与本次测试目标构建通过，最终增量日志独立保存 | `build/qa/lossless-post-review-release-build.log`、`lossless-post-review-debug-build.log` |
| Release 定向回归 | 最新完整回归16/16目标通过，27.31 s；含新功能、原五工具、解码/播放/C API、依赖及布局契约 | `build/qa/lossless-refinement-release-tests.log` |
| Debug 定向回归 | 最新完整回归16/16通过，45.17 s；此前的 audio_engine_test 时序失败已在基线独立复现，仍作为已知不稳定保留 | `build/qa/lossless-refinement-debug-tests.log`；此前失败及基线日志见下文 |
| QML 交互 | 两种配置均12项通过（含初始化/清理）；空态、详情、筛选、批处理、播放列表信号、尺寸、时频图按需请求 | `build/release/qml-lossless-identification.txt`、对应 Debug 文件 |
| 翻译 | 中英文1105条已完成、0未完成；新页面使用独立上下文，报告中文不随界面语言变化 | TS/QM 构建输出与真实英文截图 |
| 真实应用 | 11个截图场景均正常退出、0 QML 绑定错误；含 Debug 完成态、失败重试入口与真实长轨分析中状态 | `build/qa/lossless-ui/verification.json` 及各 PNG/log/metrics.json |
| 最后视觉修正复测 | 字体/留白/20 dB 刻度修正后，两配置各 4/4；未计算截止改为 `--` 后两配置各 QML 1/1 | `build/qa/lossless-ui-polish-*-tests.log`、`lossless-final-qml-*.log` |
| 长轨资源 | 180 秒/192 kHz双声道FLAC：15,826 ms，分析进程峰值私有内存13,565,952字节（约12.9 MiB），源 SHA256 不变 | `build/qa/lossless-refine-channels-long-metrics.json` |
| 源文件只读 | 17+8+16个语料及9个时间/声道控件SHA256全部不变（50个） | `build/qa/lossless-refinement-source-hashes.json` |
| 静止资源 | 空页面约 5.9 s 静止区间累计 CPU 31.25 ms，约单核 0.53%；计量排除 PNG 编码与退出 | `build/qa/lossless-ui/idle.metrics.json` |

人声分离回归最初唯一失败为旧导航断言预期 5 项。新需求新增第 6 项，已更新为 `[0,4,1,2,3,5]`，完整目标重新通过。直接运行测试宿主而未提供 `AGPLAYER_TEST_AUDIO` 会导致夹具缺失；该次诊断输出不作为有效回归证据。

Debug 原生输入测试首轮与真实截图窗口并行运行，发生一次拖拽未形成选区；单独运行后通过，最终交互验收采取顺序执行。Debug `audio_engine_test` 在功能区分别触发 barrier 与缓冲准备断言；基线首轮通过、重复到第 8 次在并发缓冲断言失败。两区 `audio_engine.cpp/.hpp` 和该测试文件 SHA256 相同，说明存在既有调度不稳定；本次未修改引擎或放宽断言，不将后续单次通过解释为该不稳定已解决。

新增 QML 空态测试也曾出现一次时序失败：`ListModel.clear()` 后 `ListView.count` 要在 polish 阶段收敛。已将即时 `verify(visible)` 改为 `tryCompare(visible,true)` 等待同一条件，保留验收要求，未修改生产逻辑。

基线 Debug 配置曾误触 vcpkg manifest 安装并移除共享 SoundTouch。已从原 ABI `079e4cc4…629b072` 缓存恢复 2.4.0#1 的 18 个文件，逐文件 SHA256 18/18 一致，安装状态及 27 项清单恢复；未改源码、port 或下载哈希。后续配置固定 BuildTools 14.44、`VCPKG_MANIFEST_INSTALL=OFF`，恢复后已再次重链验证。

真实 WAV 的 Qt 集成测试还验证了 `requestSpectrogram()`：真实 Core 返回非空、最多 96 行/128 列且数值有限的时频矩阵；随后缓存重试仍保留 256 点频谱。主代理回读实际断言后重新编译，两配置的定向目标均通过（`lossless-final-real-matrix-*.log`）。未计算的截止显示为 `--`，频率轴零点仍保留 `0 Hz`。

最终小窗口回归保证证据/结论按钮在880px下无需横滚即可点击：小窗口的分析/取消操作使用底栏。DSD详情显示原始2.8224MHz/1-bit，未将352.8kHz解码视图与1-bit混写；PCM空洞/镜像摘要不作为DSD的来源证据展示。两配置的Core、controller、QML最终3/3见`lossless-report-final-*-tests.log`；DSD显示专项两配置2/2见`lossless-dsd-display-*-tests.log`。

最后截图复核修正了修改时间的ISO机器格式与CJK文本旁小数被拆行的问题；时间显示本地日期到秒，证据数值保持完整。两配置各QML 1/1通过，见`build/qa/lossless-text-final-*-tests.log`，并重新生成完成态与打包截图。

JSON保留分析开始时间；CSV补齐应用版本、导出时间、算法参数版本、原始分析时间及完整sourceDetails/measurements JSON列，包含DSD原始/解码采样率。缓存保留实际分析时间，未计算pre-echo在两格式均不冒充零测量。

第二轮视觉修正还包括工具栏16px间距、内容宽筛选标签、暗底细框复选框、单线表格分隔和190×54并发组合框；仅应用于工具5。最终核心/报告/视觉合并后的构建与16目标回归见 `build/qa/lossless-post-review-*-build.log`、`lossless-post-review-*-tests.log`，早期日志不能替代此轮结果。

## UI 截图与比对

下列文件均在 `build/qa/lossless-ui/`：

| 文件 | 物理像素 | 状态 |
| --- | --- | --- |
| `completed.png` | 1672×941 | 5 个实际生成文件完成态，真实频谱/证据 |
| `running.png` | 1672×941 | 180 秒 FLAC 解码分析中 |
| `empty.png`、`failed.png` | 1672×941 | 空状态、损坏 FLAC 显式失败 |
| `compact.png` | 880×560 | 任务表与证据/结论切换，局部滚动 |
| `english.png` | 1920×1080 | 英文界面，长标签省略与文件信息间距 |
| `hidpi-4k.png` | 3840×2160 | 1920×1080 逻辑尺寸、200% |
| `dpi-125.png` | 2090×1176 | 125% |
| `dpi-150.png` | 2508×1412 | 150% |
| `debug-completed.png` | 1672×941 | Debug 真实完成态、正常退出 |
| `real-dsf.png` | 1672×941 | 真实PCM→DSD，原始采样率、证据及弃权说明 |
| `pre-echo.png` | 1672×941 | 实际MP3→FLAC的非零前置能量、科学记数及弃权依据 |

三栏 622:600:416、标题 50、导航 56 的几何有 QML 断言，参考截图已逐次对照修正蓝黑配色、描边按钮、状态文字色、表格密度、标题字体与右侧留白。真实音频决定文件名、曲线、置信度、转换链与频率轴，不能复刻图片中的演示值。图片字体抗锯齿、图标及部分表格/按钮间距仍存在差异；未执行逐像素差分容差认证，不声称与参考图每个像素完全一致。

## 可追溯语料

`scripts/qa-lossless-corpus.ps1` 使用测试用 FFmpeg 6.1.1 生成 17 个实际编码文件：整数/浮点 PCM、扩位、升频、MP3/AAC/Vorbis/Opus 转 FLAC，以及静音、纯音、低通反例。种子、每条编码参数、大小和 SHA256 记录于 `build/qa/lossless-corpus/manifest.json`。它是合成回归语料，不能证明商业音乐母带来源的识别准确率。

另用 `scripts/qa-lossless-heldout.ps1` 生成 8 个未参与首轮参数检查的样本，包含 88.2/192 kHz 升频、32-bit 扩位、MP3 VBR/AAC 192k/Opus 48k 转 FLAC。这些仍来自同一合成源信号，不能称为未见音乐内容测试。

## 算法实测与未达标项

`scripts/qa-lossless-evaluate.ps1` 从 manifest 与实际扫描结果生成分类 recall、判定 precision/recall、混淆矩阵及逐文件漏检列表；无该类预测时 precision 为 null，不能写成 100%。已知 MP3/AAC/Vorbis/Opus 的当前编解码器证据与隐藏转码检测分开统计。

| 组别 | 原始 17 条 | 新参数 8 条 | 真实钢琴 16 条 |
| --- | --- | --- | --- |
| 生成 PCM / 浮点 PCM 检查 | 3/3 | 1/1 | 无此组 |
| 整数扩位检查 | 1/1 | 1/1 | 无此组 |
| 当前已知有损编解码器检查 | 4/4 | 无此组 | 6/6 |
| 隐藏转码、升频及转码后升频精确分类 | **1/6** | **2/5** | **0/10** |
| 静音/纯音/低通保守反例检查 | 3/3 | 1/1 | 无此组 |
| 非弃权的分类不符 / ≥80 分分类不符 | 1 / 0 | 0 / 0 | 0 / 0 |

合并检查通过数为12/17、5/8、6/16；反例是保守行为检查，不是来源真值分类，不能将该比率宣传为音乐真实性准确率。三个合成纯升频样本被检出；`09-mp3-to-96k.flac`仅判疑似升频66分，遗漏有损历史，按精确分类计错。真实钢琴的10个隐藏异常仍全部弃权，泛化和隐藏有损识别仍未达标。

频谱空洞测量使用原始16384点STFT帧的跨时间深凹陷占用P90，保留0.10判据。截止稳定度改为±500Hz密簇统计，增加频谱边缘频率、深度下十分位数及跨窗稳定性，解决单个精确bin抖动和强制要求残留镜像造成的升频漏判。算法/参数版本为`lossless-1.2 / lossless-params-4`，使旧缓存失效。独立时频空洞仍是有损判定的必要证据，陡峭截止单独仅作中性带宽证据；升频推断明确不能排除母带数字低通。

审查曾发现仅凭陡边缘判有损的过度推断，新增44.1/48k未经有损编码的陡峭FIR低通反例先失败再修复。该被撤销实验曾有10/11合成隐藏分类命中，不能引用为最终识别率。渐进低通与高质量windowed-sinc重采样也有独立数值测试。前置能量观测现已实现为中性证据，尚未校准为编码器前回声识别器；未找到合格瞬态时`transientPreEchoMeasured=false`，JSON分数为null、CSV为空，不把默认0冒充实测；置信度仍为未经统计校准的工程证据强度。

最新证据为`lossless-refinement-corpus-evaluation.json`、`lossless-refinement-heldout-evaluation.json`、`lossless-refinement-music-evaluation.json`及各自`lossless-refinement-*-scan.json`扫描，均位于`build/qa/`。早期0/11结果及已撤销实验保留作为历史诊断。

真实音乐语料由`scripts/qa-lossless-music.ps1`生成：取同一首钢琴作品不同时间段，产生10个隐藏变换和6个已知有损容器；另保存两份参考PCM的命令和哈希，不将参考文件当原生母带计分。它扩展了内容，但仍不是跨曲目/流派的大样本校准。

发布方明确说明[Decaud DSD样本](https://www.decaud.io/dsd.html)来自CC0 Open Goldberg Variations的24/96 PCM，经8阶delta-sigma转换为DSD64。66,628,959字节文件SHA256与发布方一致：`BD3FBB4DB97C7F8F7E9F4F780DA34421C5CCAAF5A6A49182C62F4371DB43234E`。旧规则误判`credible_native_dsd 62`，修复后为`inconclusive 35`；原始1-bit统计与噪声整形只能支持有效DSD表示，不能肯定原生录音来源。实际文件分析覆盖率约99.76%（FFmpeg估算时长），源哈希不变。证据：`build/qa/lossless-external/manifest.json`、`dsf-before.json`、`dsf-final.json`、最新Debug复测`dsf-debug-final.json`、本轮`dsf-refinement.json`，以及18个音乐/参考文件的`lossless-music-source-hashes.json`。

DST 外部夹具来源：[FFmpeg FATE DST 样本](https://fate-suite.ffmpeg.org/dst/dst-64fs44-2ch.dff)，SHA256 `29D305BD19731CE079324ED14AC17254F254115D7787B5646A943E367B238ED0`。合成 ISO 内封装该文件的原始 DSTF 压缩帧，不冒充实际 SACD 镜像。

SACD reader 为独立、范围受控的只读实现，使用既有 FFmpeg 解码。仅参考二进制格式字段事实，未复制或链接 sacd-ripper/sacd_extract 实现。未添加设备抓轨、认证、密钥或解密能力。

## 尚需完成的验收

- Debug 播放测试的既有时序不稳定需单独治理；本次一次通过不代表其已修复。
- 隐藏有损识别、低带宽内容的升频识别、前回声的来源鉴别能力与独立大规模概率校准；现有规则未达专业识别验收。
- 真实原生DSD、实际SACD ISO双区域/多声道长轨验收仍缺样本。真实PCM→DSD已取得并分析，但识别为弃权。原生DSD没有足够证据时不输出来源可信。
- 已核实[2L测试台暂停提供下载](https://www.2l.no/hires/index.html)，旧DSD/DXD链接404；NativeDSD公开Dropbox目录无法连接，OPPO官方DSD下载域名无法解析。已向用户询问可用本地样本目录。
- ISO 定位目前针对曲库中的容器文件；尚未实现播放器内的 ISO 区域/内部轨道逐项定位。

队列最多 1500 项、每次输入最多 10000 路径、并发 1–4（默认 2）、发现任务最多 4、同时导出报告最多 1。缓存预算 64 MiB，淘汰时计入模型保留结果估算；此预算不等于整个应用内存上限。保留 256 点频谱，时频图按需读取；取消/销毁、缓存身份失效、JSON/CSV 原子导出与防覆盖源文件已有测试。

音频分析与解码测试不等于实际听音或实体音频硬件验收；本功能不会更改源文件或主动开始播放。

## 包体积与本地预览

同一源码基线、依赖、Release 配置及 Inno Setup 压缩设置对比：

| 项目 | 字节 | MiB |
| --- | ---: | ---: |
| 功能前 | 35,535,894 | 33.889669 |
| 功能后 | 35,772,451 | 34.115268 |
| 增量 | **236,557** | **0.225598** |

低于 3.5 MiB 目标与 5 MiB 硬上限。最终包为本 worktree 的 `build/installer/AgPlayer-Setup-1.0.0-x64.exe`，SHA256 `B2783179037D202BEB69608E6D6C7AD7CA140C3B5495C4BBBE96D654296386DD`。准确数值保存在 `build/qa/lossless-package-comparison.json`。

对 `build/package/AgPlayer/AgPlayer.exe` 做了本机独立启动检查：PATH 仅保留 Windows/System32 与 Windows，清空开发期 Qt/QML 路径；实际读入 5 个文件完成分析、输出 `build/qa/lossless-ui/packaged.png`，退出码 0、0 QML 绑定错误。包内无测试音频、ISO 或模型文件。该检查不是新装 Windows/完整安装器验收；没有执行安装、发布或签名认证。专业准确率门槛尚未满足，产物仅作开发预览与体积对照。

## 执行入口与审查

所有命令均在功能 worktree 中执行。先加载 `BuildTools/Common7/Tools/Launch-VsDevShell.ps1 -Arch amd64 -HostArch amd64`，现有 CMakeCache 已固定 Qt 6.7、共享 vcpkg 安装目录与 `VCPKG_MANIFEST_INSTALL=OFF`。

```powershell
cmake --build build/release --target AgPlayer lossless_analyzer_test lossless_analysis_controller_test sacd_reader_test qml_audio_tools_test
# 同样构建 build/debug；完整最终目标/日志见 build/qa/lossless-post-review-*-build.log
$filter='^(sacd_reader_test|lossless_analyzer_test|lossless_analysis_controller_test|qml_lossless_identification_test|qml_audio_editor_native_input_test|qml_format_converter_test|qml_filename_process_test|qml_metadata_editor_test|qml_vocal_separation_test|audio_tools_layout_contract_test|ui_design_system_usage_contract_test|decoder_test|playback_session_test|audio_engine_test|c_api_lifecycle_test|dependency_smoke_test)$'
ctest --test-dir build/release --output-on-failure -R $filter
ctest --test-dir build/debug --output-on-failure -R $filter
./scripts/qa-lossless-ui.ps1 -Name completed -InputDirectory build/qa/lossless-ui-input
./scripts/qa-lossless-evaluate.ps1 -Manifest build/qa/lossless-corpus/manifest.json -Scan build/qa/lossless-scan-final-edge-evidence.json -Output build/qa/lossless-evaluation-final-edge-evidence.json
./scripts/package-windows.ps1 -BuildDirectory build/release -SkipBuild
git -c core.autocrlf=true -c core.safecrlf=false diff --check
```

独立审查覆盖主程序导入/定位/翻译/生命周期、SACD 边界及 CMake；发现的 DST 连续性阻断已用失败测试复现并修复。主代理复核模型/缓存/报告、Scene Graph 资源、QML 状态及最终差异。既有非本任务文件保持初始哈希，任务所需共享文件按基线逐文件比较，未提交或合并主目录；另一项 UI/性能任务仍在其独立工作区。

最终检查：`build/qa/lossless-final-review.json` 记录 tracked diff-check 为 0、既有无关文件变动为 0、38个新增相关文件无空白/冲突标记问题。本次未运行全仓 168 个测试，不宣称全量回归或生产发布就绪。
