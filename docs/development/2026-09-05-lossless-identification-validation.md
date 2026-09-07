# 无损鉴别验证记录

状态：开发预览，专业识别准确率验收未通过。以下为实测证据；不能据此宣布整个需求完成。

当前整合候选：1.13 / params16；下列主线 AM 误报修复与分支各轮验证均保留为历史证据。集成版本继续使用主线更严格的 AM 中性证据与空处理链约束。
## 2026-09-06 原生 AM 误报边界修复（1.11 / params14）

本次修复只将 `resampling_polyphase_grid` 降为中性证据，不再单凭周期残差设置升采样结论或推测处理链；保留测量、阈值、MDCT/CELT 判据以及其他判定分支。原生96 kHz合成噪声经同采样率FIR及幅度调制、不经过重采样或有损编码，也能跨过原有多相条件，因此不能把该测量当作来源因果证明。版本避开现有worktree及冻结试验使用的1.4–1.10 / params7–13，以免读取旧缓存。

- RED：`tests/scripts/qa_lossless_am_guard_test.ps1` 对旧params7 CLI在 `am-0.001.wav` 报 `suspected_upsample` 而失败；0.001、0.003、0.01三种调制幅度共3/5误报。
- GREEN：同一脚本对1.11 / params14的5个固定负例全部通过；三条周期强度/相干性数值与旧版完全相同。所有输入扫描前后SHA256不变。这是一个合成来源组，不是五个独立录音。
- 明确退化：冻结c01中20个真实44.1→96 kHz样本全部由疑似升采样转为弃权，周期测量20/20未变；**升采样召回由20/20变为0/20，不将弃权算正确**。
- 主代理统一构建后，完整 `lossless_analyzer_test` 通过（79.91秒）。既有真实升采样测试继续保护源率、相干性，并断言中性证据与空处理链。

证据目录：`build/qa/params7-c01-regression/`；`am-guard-red.json`、`am-guard-green.json`、`upsampling-params14.json`；GREEN CLI SHA256为 `D3ACDB44629AAAF32B4F0FF852FDC049919C82AC48096F6E9A48E0DB91735D10`。该二进制包含同期共享工作树改动；不将其他模块变化归功于本补丁。

此前params7完整冻结220回归耗时417.5秒，0运行错误，有损99/160（61.875%），原生PCM/FIR40对照误报0；所有样本扫描前后hash一致。原始params6 manifest未改，评估明确为既见样本回归且协议版本不匹配，不是新盲测。

### params14 完整冻结220补充回归

同一CLI（上列SHA256）完整扫描220/220，耗时492.4秒、运行错误0，扫描前后全部输入与manifest哈希一致，各批CLI版本/哈希一致。结果：有损99/160（61.875%），总弃权121/220（55%），原生PCM/FIR来源误报0/40；精确类别79/180，AAC→96的20项只算有损历史部分识别。相比params7只有20个直接升采样样本转为弃权，其余200个结论不变。直接升采样召回0/20；计入AAC→96后升采样阶段召回0/40，完整AAC→96链0/20。弃权不计为正确。

| 编码族（各20项） | 有损检出 / 召回 | 弃权 |
| --- | ---: | ---: |
| AAC 256 | 20 / 100% | 0 |
| AAC 256→96 kHz | 20 / 100%（仅有损阶段） | 0 |
| MP3 320 | 0 / 0% | 20 |
| MP3 q3 | 6 / 30% | 14 |
| Opus 96 / 10 ms | 11 / 55% | 9 |
| Opus 96 / 20 ms | 15 / 75% | 5 |
| Opus 96 / 5 ms | 10 / 50% | 10 |
| Vorbis q7 | 17 / 85% | 3 |

原生PCM、原生FIR、直接升采样三组各20项全部弃权。标准评估器false-positive分母为20项显式FIR负标签；另20项原生PCM保持原始inconclusive标签，0/40只是两组来源误报的联合观察，不是总体误报率估计。证据：`build/qa/params14-c01-regression/README.md`、`encoding-family-summary.json`、`verdict-changes-vs-params7.json`、`run.json`、`scan.json`；旧params7工件保留。**这仍是20个关联来源组的既见集回归，非新盲测，专业准确率仍未通过**。需来源可核实、事前冻结、独立未见语料；来源不明的本机音乐目录不能提供真无损标签。

## 第八轮：保持判定的性能优化（2026-09-07，最终候选1.13 / params16）

- 最终保留逐实例Hann与幅值系数预计算、hybrid log4；power/FFT input/output/prefix恢复旧版的局部生命周期。Hann数组共229376B（224KiB，另3个scale共24B及对象元数据），相比1.12试验缓存数组减少约83%；相对旧版最大窗口同时点理论增加96KiB。7轮按生产/MD、/O2、/Ob2编译的交错局部对照，2声道CPU中位218.750→171.875ms（约21.4%），Hann-only与全scratch的CPU中位相同；64声道波动明显，未证明稳定收益或缓存竞争根因。最终真实生产函数对旧函数144个连续窗口逐位相等，包含64声道，释放检查通过。见round8-performance/diagnosis；未采用全scratch缓存。
- 最终1.13 Release核心121.10秒、hybrid3.32秒、完整控制器22.21秒全部通过；Debug完整控制器76.49秒通过。Release应用及Release/Debug扫描器构建通过；重新执行52次/51唯一文件的完整JSON对照，判定/结构/整数差异0、运行错误0，最大浮点差仍7.9847e-13，51个输入、36个原始用户源及冻结EXE/源码SHA均不变。新中文真实应用图final-1.13/ui-zh.png已查看，日志无QML绑定错误。记录在round8-performance/final-1.13；不是应用全量或完整像素级验收。
- 最终四输入各一次fresh-process资源/语义核对：MP3片段 13.211→13.395MiB，原生对照 15.402→15.398MiB，整曲 23.195→23.270MiB，64声道 97.598→97.762MiB；均为整进程私有提交峰，最大增加约0.184MiB，不等于DSP预算或整个应用占用。四对完整结果一致（仅版本/耗时排除、浮点最大差7.9847e-13），输入SHA不变。测量仍有持续索引争用，单次资源核对不用于宣称稳定整曲提速；之前相反方向结果全部保留。功能和生命周期审查未发现阻断项，专业识别准确率与稳定整曲速度验收仍未完成。未提交、合并或打包。

- 冻结1.11/params14作为基线。1.12/params15试验候选预计算三种STFT窗的Hann并复用scratch；MP3 hybrid对4个逐项取绝对值及原1e-10下限后的系数合并标准log10，非有限输入仍拒绝，乘积溢出回退原标量公式。阈值、锚点、观测帧数、采样、备用声道条件和取消检查均未改，不启用fast-math、不截短输入、不新增依赖。MDCT窗与预因子合并原型无稳定额外收益，未采用。
- 1.12真实生产STFT与旧函数的144个连续窗口逐元素完全相同，覆盖4096/8192/16384及1/2/6/32声道、强音/静音/弱音/声道切换/NaN/Inf。hybrid独立80位Decimal覆盖12组数值；普通576系数组与旧标量总和最大差2.73e-12，极大乘积回退与旧公式完全一致，最宽动态范围两者对高精度参考误差均约1.35e-10。完整hybrid数学/流式测试及实际生产函数对照通过。局部log4内核8次中位CPU减少25.9%～27.9%，不代表整曲提速。证据round8-hybrid-performance、round8-pipeline-performance。
- 1.12 Release应用及Release/Debug扫描器构建通过。Release核心124.19秒、hybrid2.95秒、完整控制器16.00秒通过；Debug hybrid32.87秒、完整控制器81.05秒通过。新增真实PCM强→静音→弱及6声道切换回归，Release的assert由测试源显式取消NDEBUG而保持有效。没有更改测试等待预算；上述是相关模块检查，不是应用全量回归。
- 1.12固定52次分析/51个唯一输入与1.11比较：声道guard3条、编码反例/匹配正例12条、旧12MP3片段+整曲、新24已见MP3均保持判定，运行错误0；整数、布尔、离散与结构差异0，158个浮点差异全部保留，最大绝对差7.9847e-13（整曲MDCT Z）。排除仅版本和耗时，浮点容差abs/rel均1e-8；没有把整数纳入近似比较。历史24条报告的id/set包装已按同一manifest在单独文件重现，原始CLI输出及首次结构差异均保留，不改truth或扩大排除项。51个输入和36个原始用户源文件SHA前后不变。证据round8-performance/semantic-regression。
- 1.12真实中文应用截图round8-performance/ui-zh.png（1672×941）已查看，真实较弱MP3声道输入显示疑似有损、完整度100%、源格式正确，日志无QML绑定错误；本轮未改视觉布局，未重做完整像素级验收。
- 整文件测量固定12秒MP3、12秒原生备用声道对照、235.729秒48k双声道整曲、3秒48k/64声道；相同DLL环境及完整输入，进程CPU采用[GetProcessTimes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes)的kernel+user，内存是fresh-process私有提交峰而非DSP分配器或整个应用。依次保留未绑CPU3轮、同逻辑CPU2三轮、输出置于仓库外的同CPU五轮交错数据，不剔除慢回合。44对完整输出均保持判定，浮点最大差7.9847e-13，输入SHA不变。
- **这些整文件耗时不通过稳定提速验收。** 各批存在相反方向与很大波动，包括整曲和64声道的中位退化。现场观察codebase-memory-mcp worker占603% CPU；后续原生只读检查确认多份长期MCP服务不断启动扫描根仓库D:/ai/AgPlayer的新worker。不能确认其任务归属、触发条件，或将所有退化都归因于索引；单批worker退出也不代表恢复安静。未改全局配置、其他进程或生产调度。所有原始结果、方法与环境复核保留在round8-performance/{comparison.json,controlled,external-control,environment-review.json}，不选择最有利一批宣传。
- 1.12完整scratch三套载荷约1.313MiB，分析结束释放；相对旧最大窗口同时点理论增加约576KiB。末组整进程峰值中位：13.148→13.746MiB、15.336→15.934MiB、23.203→23.926MiB、97.629→98.539MiB。进程峰值不能当作96MiB DSP门槛；此试验方案已由上方最终1.13的Hann-only替代，原始结果保留。

## 第七轮：补齐较弱立体声声道的MP3证据（1.11 / params14）

- 根因：MP3探针每锚仅保留较高能量原始声道，较弱声道的独立编码结构被丢弃。真实u04片段原路径1.990062dB/Z9.423812未过2dB；另一原始声道路径2.137897dB/Z10.519、4锚同相，独立通过原门槛。整曲两条路径都未过2dB，仍弃权。增益0.01/10、简单剔除高频带、M/S及固定同锚8→32帧对照不能解决整曲，不进入生产。联合立体声背景参考[LAME官方说明](https://lame.sourceforge.io/ms_stereo.php)，不作为检测准确性证据。
- 独立红灯：较强原生噪声左侧RMS0.15、已知MP3较弱右侧RMS0.015，旧1.10与纯原生双声道对照测量完全相同，均inconclusive；固定三文件回归脚本先失败，1.11已知双声道历史和u04片段均判有损，原生对照仍弃权，源/输入SHA不变。没有降低阈值、修改truth或提高置信分数。
- 新实现仅双声道捕获另一原始声道，同一锚点和8帧深度；先检查原MDCT/CELT完整门槛，不合格才计算备用路径。两套Measurement独立，select_mdct选择完整候选，禁止拼合峰/Z/相位。新增四锚mono保留上限199680B（195KiB）加少量状态；非双声道不分配该PCM缓存，共享顺序计算工作区。缓存按1.11/params14失效。
- 用户MP3此前12片段从11/12→12/12，新增1、退化0；整曲u04仍无法确定。另24个未测文件先按固定seed、采样率/码率分层排除旧12，预测前分12开发/12留出；开发旧版12/12，冻结候选后首次留出12/12，旧版留出也12/12，不能将持平写成新增。均为相关DJ音乐已知MP3历史正例，不能外推总体准确率或无损误判率。证据round7-local-validation。
- 预声明固定10个无编码PCM反例（滤波、量化、周期增益/门控/脉冲/重复、采样保持、STFT门限）在1.10和1.11均inconclusive；2个对应MP3128k正例均检出，错误0，输入SHA不变。重复块可产生相位4/4，FIR峰可超过2dB，不能单独使用这些条件。一个合成源组不代表总体误判率。证据round7-codec-controls。
- API红灯和真实文件红灯保留；Release hybrid数值/流式/交换声道/同相反相/静音/单声道/取消幂等测试通过。Release核心98.81秒、完整控制器20.87秒通过；Debug hybrid17.28秒、完整控制器44.92秒通过。Release应用及Release/Debug扫描器构建通过。以上为相关模块测试，不是应用全量回归。
- 实际中文应用截图round7-local-validation/ui-zh.png（1672×941）已查看，quiet-coded-right显示疑似有损及MP3结构证据、源格式正确、无升频推断，日志无QML绑定错误。首个截图命令路径写成build/release/AgPlayer.exe未启动，随后使用实际app/AgPlayer.exe成功；没有把失败命令计为通过。本轮不修改视觉布局，不能宣布完整像素级验收通过。
- 独立源码及固定反例审查无阻断项。新增声道检验也增加检验机会，结果仅支持当前修复和局部回归；总体误报统计、置信度校准、真实原生DSD及升频可靠性仍未完成。1.10冻结补丁不变，本轮新候选未提交、合并或打包。
- 最终候选开发集12/12、留出集12/12均通过，24个原件/分析输入及冻结源码/EXE的SHA全部复核不变。三个交错回合的有限耗时测量：需要备用检测的原生对照中位1.351→1.587秒（+0.236秒，约17.4%）；已检出对照1.560→1.375秒，路径未增加计算，不能据此宣称提速。包含进程/解码及环境波动，仅这两个开发负载，非全局性能保证。见benchmark.json。最终Diff Review及diff --check通过。

## 用户本地音乐首次测试（2026-09-06，1.10 / params13）

新增授权目录F:/AI音乐：42个WAV扩展名中40个可读PCM 16bit/48kHz双声道，1个零字节、1个实际JPEG无音频流；异常两项均返回analysis_failed，无虚假成功。固定选取12个正常WAV整曲扫描，5项疑似有损转码、7项无法确定，运行错误0、12个原件SHA前后不变。来源历史未知，全部不评分，不能据此宣称5项真实有损或计算准确率。另4个MP3仅元数据核实、MP4未纳入本次音频扫描。证据build/qa/user-ai-music-20260906，含逐文件中文检查报告；1.10算法及冻结补丁不变。

授权目录F:/U子1.0/日常更新递归核实198个音频：197个实际MP3（191个320kbps、4个192kbps、2个128kbps），1个16bit/44.1kHz PCM WAV。音频元数据读取失败0，没有原生HiRes/DSD文件。原文件只读，副本位于build/qa/user-library-20260906，未上传音频。

先按实际MP3路径排序，固定均匀取12条；每条从min(30秒,全长/4)开始取12秒，保留采样率/声道，解码为FLAC24并去除原始metadata，无新增有损编码。锁定计划和1.10扫描器SHA后读取预测。首次结果11/12检测出有损来源（91.67%），1条无法确定，运行错误0；所有抽样原件和FLAC输入SHA前后不变。该数值是相关DJ音乐片段的已知MP3历史检出率，不是总体准确率，不能提供真无损误判率。见locked-plan.json、prepared-manifest.json、scan.json、summary.json。

唯一WAV完整分析返回inconclusive32，SHA不变；因前史未知不计分。漏检u04的12秒片段coverage1、mp3_hybrid_36相干峰1.9900617dB、z9.4238116、4/4段相位对齐，未达到既有2dB门槛；保持门槛不变，后续整曲诊断另存，不覆盖首次11/12结果。本轮无生产代码修改。

u04整曲解码FLAC诊断同样inconclusive32、coverage1，峰1.475451dB、z7.465309；来源SHA不变、运行错误0。不能通过扩大到整曲消除此例漏检，也未依据这一条调低门槛。见u04-full-scan.json。

## 第六轮：用户委托标准，低误判优先（1.10 / params13）

用户明确没有样本，委托采用合理工程标准并优先低错误率。开发自行选定项目目标：明确制作链反例误判率不高于1%、>=80分的误判0；允许证据不足弃权，同时报告检出率/弃权率。该数值不是行业统一标准，也不是已经测出的总体错误率；不再要求用户提供数值或样本才继续。历史“不具备数值标准”状态保留为历史，当前已被此授权覆盖。

- 评估器新增版本化projectPolicy并移除该用户输入阻碍；报告回归先红（缺policy），后全脚本绿。旧candidateGate的更严格区间检查保留，不借本次授权放松统计证明要求。
- 复现了确定误报：直接96k噪声→20k FIR→44.1k AM，无编码或重采样；预先固定幅度0/.0001/.001/.003/.01。1.9后三条误判suspected_upsample66，不能用“非高置信度”掩盖类型误判。AM统计与真实WTC/FreePats/c01重叠，无已验证低成本反证，见round6-am-diagnosis/findings.md。
- 1.10周期/镜像只保留中性测量及“重采样或周期调制”候选，不再单独推出升频；MDCT/CELT仍判断有损，复合链不自动追加升频因果。新规则目前不产生SuspectedUpsample/SuspectedLossyUpsample主判，枚举和历史结果兼容保留。这是明确降低升频覆盖的取舍，不宣称恢复了升频真实性。
- 固定5个AM回归先3条红，后5/5通过；另先前30秒弱AM也未误判。8个直接1bit/PDM合成DSD在1.9和1.10均返回无法确定，未复现PCM历史误判，DSD规则未改。当前13个新定向反例0误判，源SHA不变；它们是少数相关合成组，不是13个独立真实录音，不证明总体<=1%。
- WTC原标签和文件完全保留，1.10作为development复跑：exact从4/5降到2/5，AAC→96仅lossy阶段为partial，两条已知升频均弃权；有损family3/3保持，运行错误0。两个前史未知控制仍不评分。真实升频的弃权计漏检，未改truth来让测试变绿。证据round6-low-error/wtc-evaluation.json。
- 核心行为测试明确保留正例的44.1k栅格测量及Neutral证据断言；应用的升频来源推断契约相应更新，旧1.9红绿报告不覆盖。Release核心通过141.80秒；完整控制器Release23.82秒、Debug57.93秒通过，5条AM实际文件回归在Release/Debug均通过，FreePats逆采样两条通过。上述是相关模块验证，不是整个应用全量回归。
- Release应用及Debug扫描器/控制器构建通过。实际中英文界面截图round6-low-error/ui-{zh,en}.png（1672×941）均已查看：AM显示无法可靠判定、周期调制候选，无推测转换链、重采样表格为空；源96k信息保持。日志未见QML绑定错误。英文部分长标签省略仍为既有视觉限制，未宣布像素级全部交互验收通过。
- 两项独立源码审查未发现判定旁路或阻断问题；镜像测量旧参考文字仍仅作为中性测量，不能用于来源归因。冻结1.9补丁不变，1.10为新的候选版本，未提交/打包。总体误判率、跨来源代表性和置信度校准尚未得到统计证明，不能称专业准确率验收完成。
- 最终四文件转换链/历史FIR反例回归通过，源SHA不变；保留有损阶段、纯升频弃权，两条历史FIR反例仍弃权。最终Diff Review及git diff --check通过，真实暂存区保持为空。

## 第四轮：继续优化中，禁止将阶段证据作为专业完成结论

用户在第三轮交接后明确要求未达标继续执行。第三轮冻结补丁保留，工作树后续已继续修改；当前1.9/params12仍在跨来源验证，未宣布专业完成。

- Opus已见d02曲线定位：37有单段低频竞争峰，17低频带显著性不足；统一4×192恢复17/37/26，4×768再恢复30。16个开发例由9→12→13，32同源控制及8周期反例未见误报；8×96没有额外收益，不采用。保留原z/相干性/峰宽门槛。QA证据在`round4-opus-diagnosis`。
- 1.7新增120帧192/768按需深检，保持原12/48；已知时长不足3片段则不分配，未知时长仍按固定时点捕获。1.8进一步将192限制为<=32声道、768<=8声道，避免叠加捕获超过内存目标；原12/48仍适用于原通道范围。原静态审查64ch+192可达约93.4MiB且未计解码临时，不能写内存目标已证实。
- 双频带共用单个系数对数，保持原归一化下限；固定两文件3轮交错测量，wall中位29.129→25.261s、CPU23.531→20.984s，判定不变，z差约1.5e-13。只说明该开发负载，非全局性能保证。证据`round4-performance/comparison.json`。
- 新192测试先失败（不支持扩展深度），后192/768/运行中取消Release3项9.69秒、Debug3项41.29秒通过；1.7 Release Core133.20秒、Qt20.31秒通过。1.8后续修改不能沿用这些数字宣称当前全量通过。
- 第一个非SQAM FreePats外部pilot仅一个相关录音组：完整4.558秒单音，不循环/拼接。MP3320、Opus10ms96k、单独升频3条exact，AAC256→96只识别升频（partial），原PCM未计分且弃权。首次1.7冻结在`round4-evaluation`；两处truth enum拼写在读取预测前修正并另存原件/哈希，语义未改变。不可用1组宣称跨来源专业gate通过。
- 复合链代码缺陷：MDCT合格后提前返回；c01的20条AAC→96其实同时满足全部polyphase门槛且MDCT分析率与候选源率一致。1.8将这两组独立测量合并为疑似有损后升频，要求候选率低于当前率且一致；不使用截止+空洞。真实文件红灯后，复合链、纯升频、18/43两个历史FIR反例4条绿灯，见`composite-{red,green}.json`和`tests/scripts/qa_lossless_composite_test.ps1`。
- FreePats复合漏检另有逆采样低通损伤：原AAC解码帧峰8.44dB，默认逆采样5.84dB，固定128tap/cutoff1.0 QA候选6.64dB；同源PCM未误报。经51个固定候选组件对照（20 AAC、20纯升频、3 FIR升频、8原生96k合成）后，1.9接入固定128tap/cutoff1.0并检查设置失败；31个对照无新增有损帧误报。真实FreePats AAC→96及纯升频先红后绿。近目标Nyquist衰减弱约1.8～2.9dB，不能认为理想逆变换，门槛未降低。证据`round4-composite-diagnosis`。
- 真实Decaud 24/96 PCM→DSD64原件（发布者SHA吻合）在冻结1.6中无法确定35。高频整形噪声遮蔽48k边缘；float64频谱及原始1bit栅格两种诊断均未找到可靠独立PCM证据，不把人为滤波后的低带宽改成来源结论。实际DSF时长94.2秒、已完整解码；FFmpeg估计94.429秒导致coverage .997575。真实原生DSD对照仍缺，已询问用户本地来源，其他实施继续。证据`round4-dsd`。

- 1.7冻结完整142输入回归：d02 Opus13/16，d01有损23/32（旧21/32），c01 Opus42/60保持；相对1.6新增6条、退化0、错误0。18个48k控制及d01的8个FIR均未判有损；未知前史PCM不计真实负例。142输入SHA不变；d02通过冻结manifest originalName/scanName映射对齐，见`round4-evaluation/development-comparison.json`。
- 1.9 Release受影响核心/逆采样/CELT/Qt 11项全部通过245.31秒（完整核心145.48秒、Qt28.00秒），不代表整个应用全部测试。Debug逆采样、192/768/运行中取消与完整Qt共5项通过123.19秒（Qt67.77秒）。
- 新Qt合格polyphase栅格展示测试先红（qualified-grid空字符串），修复后初次选择和语言刷新复用格式化，只有完整证据code/family/direction匹配才显示源率→目标率；DSD/DST与无效/候选数据仍隐藏。真实1.9中文截图`round4-ui/final-composite-zh.png`1672×941已检查，中英文两张均已检查，复合链、重采样表格一致，日志无QML ReferenceError/TypeError/Unable to assign；英文长标题自动换行，工具导航部分省略仍是既有视觉限制。像素级全部交互验收不在两张图结论内。
- 64声道48k/3秒实测：原整进程私有提交峰严格96MiB断言失败保留。显式fresh-process QA测得baseline10,465,280B、峰102,531,072B、增量92,065,792B（87.80MiB）；正常与QA分析结果除耗时一致、源SHA不变。该增量包含解码/运行时，不能等同DSP精确分配或所有工况保证；`round4-resources/memory-method.md`保留原值与限制。扩展矩阵增量：2ch48k/8s 12.699MiB、32ch48k/3s 52.645MiB、64ch96k/3s 57.227MiB、64ch192k/3s 67.555MiB；均exit0、coverage1、SHA不变。见matrix-report.md，仍不等同全量或分配器精确验收。
- 独立源码审查未见本轮阻断问题。记录保守漏报边界：两个inverse率均合格时先保留最高峰候选，再与polyphase源率比对；可能丢弃较低峰但率一致的复合候选。尚无真实触发样本，不因代码可构造猜测修改冻结候选。
## 第五轮外部首次预测：冻结1.9 / params12

新Open WTC录音项目BWV848公开原始FLAC（20,517,856B、75.44秒、24bit/96k），Commons SHA1核验一致，来源与CC0/CC BY许可差异保留在`round5-external/source-provenance.md`。与旧Goldberg同演奏家/录音室有相关性，不宣称完全独立制作人。所有派生使用整曲，无循环/拼接；原件/纯FIR因完整制作史未知不计无损真值或误报分母。

- 先固定编码/FIR参数与源manifest，再冻结1.9程序（SHA256 998D598296A70F96028458D6257704E6751A267CB03EB38C9ABD7001789D76AF），中性文件名扫描7项。
- 首次exact4/5：MP3320、Opus96/10ms、AAC256→96完整复合链、44.1→96正确；FIR15k→96返回无法确定。原件和FIR-only均无法确定，2条不评分。
- 有损family3/3是单组小样本诊断，精确率/召回Wilson下界仅.438503；严格负例分母0，不能写总体误报率0。运行失败0，7输入与扫描器hash不变。
- 首次调用脚本错误假设prepare会生成inputs.json，实际未提供任何文件，空扫描/evaluator缺行失败已保存在empty-invocation-*；随后在读取预测前从冻结manifest构造并验证7条文件列表后执行。无算法/标签改动，没有将空运行计为成功。
- 证据`round5-evaluation/evaluation.json`、`scan-provenance.json`、`named-diagnostics.json`。此后WTC已是已见开发来源，进一步诊断不能重复称首次外部验收。FIR升频漏检已定位：149段全部低于内部强度.005（最大.003873），残差熵.559和能量均合格。输出float64与内部double两对照仍0/149；不支持简单精度修复。全曲PSD含startup不能直接归因高频噪声；当前方法未提取到证据不等于信息绝对不存在。诊断在round5-diagnosis/findings.md，未改首次标签、预测、置信度或门槛。
继续执行的固定局部循环谱诊断：WTC FIR升频在局部配对带可见约.798相关、跨59个稳态窗相位一致，但强周期AM/周期方差噪声也有高相关。唯一固定弱AM反例（直接96k噪声→20k FIR→乘1+1e-4*cos44100t，从未重采样）同时给出局部相关.705～1、相位.99968～1、残差熵.7120、带外抑制83.010dB。现有熵/抑制防护仍不能排除此反例，因此拒绝把局部相关作为新判定分支，1.9生产保持不变。固定方案、有限30秒输入、原始分带结果与weak-am-results.json在`round5-diagnosis/cyclic`；这是同一周期证据家族，不声称新增独立正证据。
原生DSD新增调查Blue Coast、NativeDSD、Sound Liaison：前两者需账户/结算且前史须逐曲核验；后一制作说明较明确，但官方WeTransfer页面在HTML与实际浏览器两路径均未取得可见下载状态/文件。音频下载0，不把网页大小算成样本；见round5-native-dsd/findings.md。仍缺已确认数值验收线、来源可核验的真实负例/原生DSD、独立大样本及置信度校准，professionalAcceptance保持ineligible。当前没有新增获验证的规则修复，后续研究必须使用新证据/反例，不能反复调已见单曲凑结果。

## 第三轮后续：冻结1.6 / params9，专业验收仍未通过

新增按需MP3长块混合滤波器探针和CELT 120采样（2.5ms）普通/深检。MP3在32–48kHz原生PCM视图捕获四个互不重叠的有界片段，普通MDCT/CELT不合格后才计算；沿用2dB、z>=5、至少3段相位一致门槛。CELT保持原z、相干性、峰宽门槛。四种深检延迟单声道缓存上限2,880,000B，未引入模型或新依赖。版本与程序在读取新预测前冻结，翻译修复未改变冻结核心。

- d02首次留出集锁定16个新SQAM来源（14/17/24/26/30/33/34/37/38/42/52/53/56/57/58/59），112文件。MP3 256k、320k、VBR q0、q3分别16/16检出，共64/64；Opus128k CBR 2.5ms为9/16。合计有损73/80（91.25%），明确FIR反例0/16有损误报，运行失败0。证据：`build/qa/round3-hybrid-evaluation/evaluation.json`、`manifest.json`、`frozen-artifacts.json`。
- 16个制作前史未知PCM控制不计真实无损真值：14个被判疑似位深扩展，2个无法确定，均未判有损；不得写成16个PCM全部弃权或总体无误报。首次258输入（112新+146开发回归）SHA全部不变。
- 旧20来源MP3 320k为20/20、q3为19/20；离线首3秒原型40/40不能替代全12秒集成39/40。旧20PCM与20FIR均无有损误报。首批258及剩余140回归中均未发现非MP3样本被新增hybrid分支判有损；d01的Opus2.5ms由0/8提升到5/8，其他三种时长维持原结果，全部仅作为开发回归。完整旧220回归有损从params7的99/160提高至138/160（86.25%），新增39、退化0；Opus由36/60提高至42/60。AAC/Vorbis/升频既有结论未退化，复合AAC升频仍只识别有损阶段。20明确FIR反例无有损误报，运行失败0；剩余140输入SHA不变。`c01-development-evaluation.json`明确为未盲开发回归，protocolCompliant=false，不能用作首次盲测。
- 7个2.5ms漏检均成功解码、覆盖率100%、120/48探针已运行。37跨段一致性不足；17/26显著性不足；24/34/38两者不足；30另有峰宽不足。汇总测量不支持降低门槛，下一轮应先检查逐片段曲线与竞争峰，并使用新的独立验收集。
- 该批仍来自同一SQAM集合，16来源多变体不能看作80独立来源；明确负例仅16个。置信度仍为未校准工程分数。跨语料库、真实原生HiRes/DSD、复合链完整恢复和用户批准的数值标准未具备，candidateGate与professionalAcceptance仍ineligible。91.25%是该挑战集有损检出率，不是总体正确率或认证。
- MP3数学检查覆盖32个子带中心音、独立直接cosine MDCT/IMDCT、alias正逆、分块/反相/非法输入/取消；独立源码审查未发现索引、生命周期或取消集成阻断。未知总帧数时此模块不可用；短块、混合块、完整编码历史尚未验证。滤波窗来源与LGPL许可证在`LICENSES/`，尚未进行分发合规验收，本任务没有打包。
- Release新增DSP六项全部通过（96.44s）；Debug MP3/deep120/运行中取消三项通过（61.93s）；Release完整核心通过147.87s。深检入口已修复重复运行旧套件的问题。取消测试使用外部握手后触发，不能据此保证取消发生在特定FFT内部或声称硬实时。
- Qt全套在并发负载下仍出现超时：Release17.40s失败，Debug CTest30.02s超时；直接Debug运行定位真实WAV等待30秒断言、约30.4秒才完成（整套24通过1失败）。保留失败日志。释放批量负载后Release仍约11.8秒，确认新增深检已超过旧10秒功能等待。此用例顺序执行频谱与时频两次实际分析，现将每次功能等待设为Release20秒/Debug60秒、整套180秒；全部结果、缓存和矩阵断言保持，取消500ms目标保持。此调整不证明性能优化或更严格的时延验收通过。最终同一核心代码下Qt完整回归Release26.12秒、Debug57.93秒通过，见`round3-16-{release,debug}-controller-final.log`；不能把分批检查写成整个项目全量通过。
- 中英文真实MP3应用截图已运行。英文首次发现新增证据误放翻译context，已移至LosslessEvidence并重新截图验证：`build/qa/round3-ui/round3-mp3-en-fixed.png`，中文为`round3-mp3-zh.png`。未重做整套像素级UI验收。截图用于显示与翻译检查，不能代替听音或端到端全部验收。

## 第三轮：冻结1.5 / params8，专业验收仍未完成

在主线792b732的隔离工作树上新增有界CELT深检：12帧默认探针保持不变，48帧探针在同次解码只保存四段归一化单声道；普通CELT和MDCT均不合格后才执行深检，任何profile合格后停止。z、相干性、峰宽及跨频带相位门槛未降低。取消在每个相位扫描检查，缓存有固定上限；没有引入模型或额外运行时。新增`celtFramesPerAnchor`导出，未测为null，防止把深度48与帧长240等不同概念混淆。

- 补齐旧params7的220文件回归：有损99/160，明确FIR误报0/20，分析失败0，输入SHA不变。全部标为development，原params6首次盲测保持不变。证据`build/qa/round3-baseline`。
- 真实集成回归：旧60个Opus由params7的36命中增至42，6新增、0退化；18个48k音乐控制全弃权。旧params6的38/60不能误作params7基线。证据`round3-evaluation/comparison.json`及`all-scan.json`。
- 首次新来源参数挑战：SQAM 09/12/15/19/23/29/46/54，固定1秒起12秒，128k CBR的2.5/5/10/20ms；在预测前锁定来源、参数、版本、输入SHA和程序SHA。48文件中32隐藏有损命中16：2.5ms0/8、5ms5/8、10ms5/8、20ms6/8。8FIR误报0；8制作前史未知PCM仅为未评分控制。48个盲输入SHA不变，分析失败0。证据`round3-evaluation/evaluation.json`。该批已被查看，后续调试不能再称未见来源。
- 同一SQAM集合、8来源多变体和未经校准的分数不能代替跨语料库、DSD、精确率置信下界或专业认证。候选gate仍ineligible，不能把小分母零误报解释为总体误报率为零。
- 12帧扩为48帧有实际成本。三profile延迟mono缓存上限2,688,000B；960profile临时交错缓冲为50*960*channels*8字节，完成片段后释放。这个静态上限不等于整个应用峰值；还需计入原有解码/STFT/任务缓存。
- Release原CELT28.01s、deep240 38.89s、deep960 127.89s分别通过；原合并测试90.61s超时已保留，拆分后原90秒预算不变，新深检各180秒功能预算。Release核心266.78s通过。Qt新增字段红灯缺字段后，Release/Debug导出各5项通过。Debug deep240在并发负载下181.08s超时，批量扫描结束后按原180秒预算复跑73.81s通过（`round3-debug-deep-retry.log`），不改变预算且保留首次失败。后续审查发现深检入口完成后还串行执行原12帧套件，覆盖未缺失但时间包含重复工作；将在下一候选修正入口退出。
- 真实应用`build/qa/round3-ui/round3-deep.png`已检查：旧39/5ms漏检显示有损转码及CELT证据；日志无ReferenceError/TypeError/Unable to assign。未修改视觉布局，也未声称原始像素级验收全部完成。未提交、合并或打包。

MP3离线原型另保留在`build/qa/round3-mp3`。周期能量启发式失败；最初hybrid实现的子带映射oracle失败，不能用其输出评价正确算法。修正64点折叠调制后32个子带中心频率、MDCT重建及alias正逆检查通过，首对已知PCM/MP3出现区分，正在扩大验证；尚未进入上述冻结1.5程序。任何后续候选必须使用新版本与独立记录。
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


## 2026-09-07：校准输入审查与性能负载门控

本轮不是专业验收通过。算法保持冻结的 lossless-1.13 / lossless-params-16，不改变检测阈值或历史真值。

- 已修复将工程分数展示为概率的问题：任务列表、结论环和候选来源显示“分”，说明明确尚未校准。JSON/CSV 保留 legacy confidence 字段，同时标记 ordinal_evidence_score、uncalibrated；新结果 calibratedProbability 为 null，CSV 为空。旧缓存通过报告顶层语义说明覆盖。候选来源分数也不是概率。
- 新离线工具 `scripts/qa-lossless-calibrate.py` 按声明的来源组隔离拟合和验证，拒绝重复 SHA、来源交叉、未知接受真值、版本混用、不支持的验证分箱和单一正确性标签。输出离散分箱、Wilson 描述区间、Brier、可靠性分箱、来源等权诊断及来源 bootstrap；不生成可部署概率配置。来源和历史处理声明仍须人工/数据审计，工具不会认证录音前史。
- 当前 1.13 证据为51个去重输入，39个接受的有损判定全部64分，剩余12个拒判。旧验证集已暴露于开发；没有能支持这39个接受结果概率拟合的独立错误反例。不能把39/39当作真实音乐正确率，也不能将已知MP3正例与未知WAV拼成真假平衡集。详细审计：`build/qa/round9-calibration-data/report.md`。
- 固定 1.11 与 1.13 EXE、原四个完整输入，以ABBA顺序、CPU/墙钟/私有提交峰分别记录；预先门控负载、保留反向结果、污染整块作废。30秒 preflight-only 实测 invalid，0个音频扫描。没有连续5个安静样本；未修改其他进程优先级、亲和性或服务。证据：`build/qa/round9-performance/preflight-v1/preflight.json`。
- Release 应用和CLI构建通过；报告/控制器、音频工具布局检查通过。QML原测试期待87%，调整为本次产品语义87分后复测通过，未删除断言。1672×941中文真实UI截图已查看，评分和说明无裁切：`build/qa/round9-confidence-ui/ui-zh.png`。日志在同目录。本轮未运行全量或Debug回归。

剩余：冻结后、来源可信且包含错误接受机会的独立校准/验证数据，以及满足门控的整曲重复性能结果。完成工具和诚实显示，不等于已经完成概率校准或稳定性能验收。本轮未提交、合并、打包或发布。
