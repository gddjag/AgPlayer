# 无损鉴别来源准确率评估协议

状态：工程候选协议。它不是行业标准，也未获得产品发布门槛批准。现有语料数量不足时必须报告 `ineligible`，不能把局部回归通过写成专业准确率达标。

## 评估对象

评估分为三种任务，结果不得合并成一个“准确率”：

1. `format_recognition`：文件当前容器或明确编码格式可直接确定的检查，例如已知 MP3 容器。
2. `source_inference`：对隐藏有损、升频、扩位等历史处理链的推断。
3. `guard_control`：有明确加工真值的反例，用于统计错误来源断言。原始制作历史未知的 PCM 仅为 `unscored_control`，不能自动充当“从未有损”的负样本。

DSD 使用独立任务和来源组报告。PCM 指标不能替代原生 DSD、PCM→DSD 或 SACD 来源推断指标。

后续增益、裁剪、加噪或再次封装不会消除已经发生的有损编码历史。这类样本仍是 lossy positive，不能放入负样本或假阳性分母。

## 独立来源与数据划分

- `sourceGroup` 表示一份独立源录音。同一源生成的不同编码器、码率、增益、裁剪、噪声和封装衍生文件必须使用相同 `sourceGroup`。
- 80 个同源衍生文件仍只算一个独立来源。报告同时输出文件数和 `independentSources`；逐文件 Wilson 区间不把衍生文件变成独立抽样，也不证明总体人群性能。
- `development` 可用于规则调试。`heldout` 在规则和阈值冻结前按 `sourceGroup` 划分，不能把同源衍生跨到两个集合。查看 heldout 结果后再调参，必须另建新的未见来源验收集。
- 不同曲目、说话者、乐器、年代、母带链和编码器需分源报告。单曲多片段不等于跨内容泛化。

## 盲化和身份校验

扫描前使用 `scripts/qa-lossless-prepare-blind.ps1`。输入协议示例：

```json
{
  "schemaVersion": "1",
  "description": "locked evaluation",
  "seed": "recorded-fixed-seed",
  "algorithmVersion": "lossless-1.2",
  "parameterVersion": "lossless-params-4",
  "coverageMinimums": {
    "lossyPositive": 20,
    "explicitNegative": 20,
    "nativeDsd": 10,
    "pcmToDsd": 10
  },
  "corpora": [
    {
      "manifest": "manifest-heldout.json",
      "root": "corpus-heldout",
      "evaluationSet": "heldout"
    }
  ]
}
```

语料 manifest 的每个样本必须提供 `name`、`truth`、`sha256`、`sourceGroup`，并提供 `evaluationSet` 或 `split`。协议中的 corpus 字段可以为旧语料提供后备 `sourceGroup`/`evaluationSet`。

脚本把文件复制为由固定 seed、内容 SHA256、corpus/sample 索引生成的中性文件名，写出 schema 2 manifest，并逐文件复核 SHA256。原始文件名只留在评估 manifest，扫描器只接收盲目录中的路径。输出目录本身也应使用中性名称。

该步骤降低文件名泄露标签的风险，但不是“已证明不存在泄露”。扩展名、容器结构和音频数据本来就是被测输入；目录、旁车元数据、调用参数和人工操作仍需独立审查。

协议必须锁定 `algorithmVersion` 与 `parameterVersion`，扫描每一行也必须携带相同版本。旧扫描缺少版本时标为 `historical_or_unknown`，版本不符标为 `mismatch`；两者都使工程门槛不可评估，不能从文件时间或当前源码猜测旧结果的版本。

## 计分层级

`scripts/qa-lossless-evaluate.ps1` 输出以下互不替代的结果：

- `exact`：稳定 verdict 完全一致。运行失败和弃权在有真值样本上均计为未命中。
- `familyMetrics`：全部任务的诊断汇总；`familyMetricsByTask` 分别给出 `sourceInference`、`formatRecognition` 和 `dsdSourceInference`，正式结论必须使用分任务结果，不能用显式 MP3 容器识别抬高隐藏来源 precision/recall。来源族包括 `credible_pcm`、`lossy`、`upsample`、`bit_depth`、`native_dsd`、`pcm_to_dsd`；`suspected_lossy_upsample` 同时覆盖 lossy 与 upsample。
- `partialCorrect`：exact 错误但至少命中一个必需来源族。例如 lossy+upsample 被判为 lossy，只算部分命中，不能提升 exact。
- `abstention`：只包含 `inconclusive`。`analysis_failed`、`cancelled` 或非空 error 属于 `operationalFailure`，不能伪装成保守弃权。
- `falsePositive`：仅对具有明确禁止来源族的 `guard_control` 统计。未知原始制作历史的 control 不进入该分母。
- `highConfidenceWrong`：置信度达到参数阈值，且预测来源族与必需族完全不相交，或违反明确 guard 的错误断言。部分命中不归入这一项。
- `highConfidenceExactMismatch`：另外保留所有高置信度非弃权的 exact 错误（包括部分命中），避免漏报额外推断或不完整转换链。`sourceInference` 的 precision 分母包含明确反例上的假阳性；同一来源跨开发与验收集合会使协议失效。
- `sourceGroups`、`evaluationKinds`、`evaluationSets`：分别按独立来源、任务类型和数据划分输出相同摘要，防止大来源组或开发集掩盖小来源组和留出集。

precision、recall、弃权率、运行失败率、假阳性率和高置信错误率均输出 95% Wilson score 区间。计算使用 `z=1.959963984540054`，结果按 midpoint-away-from-zero 保留六位；分母为零时 estimate/lower/upper 均为 null。混淆矩阵和逐文件行用于复核，不能用 null precision 表述为 100%。

## 工程候选门槛

脚本提供一个未批准的工程候选门槛，便于持续集成暴露退化：

- lossy precision 的 Wilson 下界 ≥ 0.95；
- lossy recall 的 Wilson 下界 ≥ 0.80；
- 明确反例假阳性率的 Wilson 上界 ≤ 0.01；
- 置信度 ≥ 80 的完全错误断言为 0。

这些值不是行业标准或已批准的发布要求。`coverageMinimums.lossyPositive` 与 `explicitNegative` 必须由验收负责人在锁定协议中明确给出，且实际独立来源数达到要求；未声明、语料不足或 manifest 未盲化时状态必须为 `ineligible`，不能返回 pass。达到覆盖后不满足统计门槛返回 `fail`。

候选验收集合中的 `source_inference` 与 `guard_control` 必须每个 `sourceGroup` 仅有一行。若同一来源包含多个编码、增益、裁剪或其他相关变体，当前脚本仍保留逐文件诊断，但因没有预先锁定的聚类统计方法，候选 gate 返回 `ineligible`，reason 为 `correlated_source_variants_without_cluster_validation`。不得靠重复同源变体收窄逐文件 Wilson 区间。

协议可在扫描前声明 `sourceEqualWeightDiagnostics.enabled=true`、`aggregation=mean_of_within_source_rates` 和允许的验收集合。脚本先在每个 `sourceGroup` 内计算 lossy recall、`lossy_upsample` 复合链 exact/partial、明确反例假阳性率、运行失败率和高置信完全错误率，再对有定义的来源率做等权算术平均。复合链只判为 lossy 或只判为 upsample 时计 partial，不能计 exact。这一结果标记为 `reported_descriptive_only`，不计算置信区间、不改变候选 gate，也不能替代独立来源统计或专业认证。

候选门槛只读取 `heldout` 与 `external_validation` 行；其中 lossy precision/recall 只来自 `source_inference`，显式 MP3 等 `format_recognition` 命中不能帮助隐藏来源 gate 通过。`development` 和 robustness 调参集仍出现在诊断指标中，但永远不能帮助 gate 通过。输出中的 `professionalAcceptance` 固定保持 `ineligible`，直到另有获批的专业数值标准、大规模独立来源、DSD 专项和置信度校准证据。

DSD 独立报告 `nativeDsd` 与 `pcmToDsd` 来源数。当前没有获批的 DSD 性能门槛；即使覆盖数量达到协议要求，也只能标记为独立报告，不能借用 PCM gate 宣布通过。

## c01 留出集预声明

`scripts/qa-lossless-round2-corpus.ps1` 在任何 c01 预测前锁定 EBU SQAM track `08,11,13,16,18,20,22,25,28,32,36,39,41,43,45,48,51,55,60,65`，避开此前使用的 `10,21,27,31,35,40,44,47,49,50`。每个来源固定截取从第 1 秒开始的 12 秒，并生成 11 条：PCM、FIR15k 明确反例、MP3 q3、MP3 320k、AAC 256k、AAC 256k 后升频至 96 kHz、Vorbis q7、Opus 96k 的 5/10/20 ms 帧，以及 PCM 44.1→96 kHz。复合链真值固定为 `lossy_upsample`。

c01 锁定 `lossless-1.4` / `lossless-params-6`，共 20 个来源、220 个文件。同源 11 个衍生文件不满足逐行独立假设，因此候选 gate 必须保持 `ineligible`；来源等权结果只用于观察内容间稳定性。

## 可复现执行

```powershell
& scripts/qa-lossless-prepare-blind.ps1 `
  -Protocol build/qa/evaluation-protocol.json `
  -OutputDirectory build/qa/evaluation-blind

# 使用 lossless_scan_cli 扫描 evaluation-blind 中除 manifest.json 外的音频。

& scripts/qa-lossless-evaluate.ps1 `
  -Manifest build/qa/evaluation-blind/manifest.json `
  -Scan build/qa/evaluation-scan.json `
  -Output build/qa/evaluation-report.json
```

每轮记录协议、盲 manifest、扫描 JSON、评估 JSON、算法/参数版本和全部文件 SHA256。算法、阈值或语料定义变化后重新生成报告，不覆盖旧证据。
