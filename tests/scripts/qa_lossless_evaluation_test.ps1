$ErrorActionPreference = 'Stop'

$sourceRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$prepareScript = Join-Path $sourceRoot 'scripts\qa-lossless-prepare-blind.ps1'
$evaluateScript = Join-Path $sourceRoot 'scripts\qa-lossless-evaluate.ps1'
$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) (
    'agplayer-lossless-evaluation-' + [Guid]::NewGuid().ToString('N'))

function Assert-Equal($Actual, $Expected, [string]$Message) {
    if ($Actual -ne $Expected) {
        throw "$Message. Expected '$Expected', got '$Actual'."
    }
}

function Assert-ThrowsMatching([scriptblock]$Action, [string]$Pattern,
    [string]$Message) {
    try {
        & $Action
    } catch {
        if ($_.Exception.Message -like $Pattern) { return }
        throw "$Message. Unexpected error: $($_.Exception.Message)"
    }
    throw "$Message. Expected an error matching '$Pattern'."
}

try {
    $groupARoot = New-Item -ItemType Directory -Path (
        Join-Path $temporaryRoot 'truth-named-a') -Force
    $groupBRoot = New-Item -ItemType Directory -Path (
        Join-Path $temporaryRoot 'truth-named-b') -Force
    $blindRoot = Join-Path $temporaryRoot 'blind'

    $groupASamples = @(
        [ordered]@{name='native-generated.wav';truth='generated_pcm'},
        [ordered]@{name='lossy-upsample.flac';truth='lossy_upsample'},
        [ordered]@{name='lowpass-counterexample.wav';truth='lowpass_not_lossy'}
    )
    $groupBSamples = @(
        [ordered]@{name='upsample.flac';truth='upsample'},
        [ordered]@{name='hidden-lossy.flac';truth='lossy_transcode';
            transformation='lossy encode followed by gain and noise'},
        [ordered]@{name='obvious-lossy.mp3';truth='known_lossy_container'},
        [ordered]@{name='unknown-history-control.wav';truth='inconclusive'},
        [ordered]@{name='heldout-lowpass.wav';truth='lowpass_not_lossy'}
    )
    foreach ($sample in @($groupASamples + $groupBSamples)) {
        $root = if ($groupASamples.name -contains $sample.name) {
            $groupARoot.FullName
        } else {
            $groupBRoot.FullName
        }
        $bytes = [Text.Encoding]::UTF8.GetBytes('fixture:' + $sample.name)
        [IO.File]::WriteAllBytes((Join-Path $root $sample.name), $bytes)
        $sample.sha256 = (Get-FileHash -LiteralPath (
            Join-Path $root $sample.name) -Algorithm SHA256).Hash
    }
    $manifestA = Join-Path $temporaryRoot 'manifest-a.json'
    $manifestB = Join-Path $temporaryRoot 'manifest-b.json'
    [ordered]@{description='group a';samples=$groupASamples} |
        ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestA -Encoding utf8
    [ordered]@{description='group b';samples=$groupBSamples} |
        ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestB -Encoding utf8
    $protocolPath = Join-Path $temporaryRoot 'protocol.json'
    [ordered]@{
        schemaVersion='1'
        description='evaluation regression'
        seed='fixed-regression-seed'
        algorithmVersion='lossless-test-1'
        parameterVersion='lossless-test-params-1'
        sourceEqualWeightDiagnostics=[ordered]@{
            enabled=$true
            aggregation='mean_of_within_source_rates'
            evaluationSets=@('heldout','external_validation')
        }
        corpora=@(
            [ordered]@{manifest=$manifestA;root=$groupARoot.FullName;
                sourceGroup='synthetic-a';evaluationSet='development'},
            [ordered]@{manifest=$manifestB;root=$groupBRoot.FullName;
                sourceGroup='music-b';evaluationSet='heldout'}
        )
    } | ConvertTo-Json -Depth 6 |
        Set-Content -LiteralPath $protocolPath -Encoding utf8

    & $prepareScript -Protocol $protocolPath -OutputDirectory $blindRoot |
        Out-Null
    $blindManifestPath = Join-Path $blindRoot 'manifest.json'
    $blindManifest = Get-Content -LiteralPath $blindManifestPath -Raw |
        ConvertFrom-Json
    Assert-Equal $blindManifest.schemaVersion '2' 'Blind manifest schema'
    Assert-Equal $blindManifest.protocol.sourceEqualWeightDiagnostics.enabled `
        $true 'Source-equal diagnostic policy survives blinding'
    Assert-Equal @($blindManifest.samples).Count 8 'Blind sample count'
    Assert-Equal @($blindManifest.samples | Group-Object sourceGroup).Count 2 `
        'Source group count'
    foreach ($sample in $blindManifest.samples) {
        if ($sample.scanName -match '(lossy|upsample|native|lowpass|expanded)') {
            throw "Truth leaked through blinded scan name: $($sample.scanName)"
        }
        $blindPath = Join-Path $blindRoot $sample.scanName
        if (-not (Test-Path -LiteralPath $blindPath -PathType Leaf)) {
            throw "Blinded sample is missing: $blindPath"
        }
        Assert-Equal (Get-FileHash -LiteralPath $blindPath -Algorithm SHA256).Hash `
            $sample.sha256 'Blinded sample hash'
    }

    $predictions = @{
        'native-generated.wav' = @('credible_lossless', 90)
        'lossy-upsample.flac' = @('suspected_lossy_transcode', 85)
        'lowpass-counterexample.wav' = @('suspected_upsample', 95)
        'upsample.flac' = @('inconclusive', 30)
        'hidden-lossy.flac' = @('analysis_failed', 0)
        'obvious-lossy.mp3' = @('suspected_lossy_transcode', 90)
        'unknown-history-control.wav' = @('suspected_lossy_transcode', 99)
        'heldout-lowpass.wav' = @('inconclusive', 10)
    }
    $scan = foreach ($sample in $blindManifest.samples) {
        $prediction = $predictions[$sample.originalName]
        [ordered]@{
            path=(Join-Path $blindRoot $sample.scanName)
            verdict=$prediction[0]
            confidence=$prediction[1]
            algorithmVersion='lossless-test-1'
            parameterVersion='lossless-test-params-1'
            error=$(if ($prediction[0] -eq 'analysis_failed') {
                'decoder unavailable'
            } else { '' })
        }
    }
    $scanPath = Join-Path $temporaryRoot 'scan.json'
    @($scan) | ConvertTo-Json -Depth 6 |
        Set-Content -LiteralPath $scanPath -Encoding utf8
    $reportPath = Join-Path $temporaryRoot 'evaluation.json'
    & $evaluateScript -Manifest $blindManifestPath -Scan $scanPath `
        -Output $reportPath | Out-Null
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json

    Assert-Equal $report.schemaVersion '2' 'Evaluation schema'
    Assert-Equal $report.protocolCompliant $true 'Protocol compliance'
    Assert-Equal $report.scanVersion.status 'verified' 'Scan version status'
    Assert-Equal $report.samples 8 'Evaluated samples'
    Assert-Equal $report.independentSources 2 'Independent source count'
    Assert-Equal $report.metrics.exact.numerator 2 'Exact correct count'
    Assert-Equal $report.metrics.exact.denominator 5 'Exact eligible count'
    Assert-Equal $report.metrics.exact.estimate 0.4 'Exact accuracy'
    Assert-Equal $report.metrics.exact.lower 0.117621 'Exact Wilson lower'
    Assert-Equal $report.metrics.exact.upper 0.769276 'Exact Wilson upper'
    Assert-Equal $report.metrics.partialCorrect 1 'Partial family match count'
    Assert-Equal $report.metrics.abstention.numerator 2 'Abstention count'
    Assert-Equal $report.metrics.abstention.denominator 7 `
        'Abstention non-operational denominator'
    Assert-Equal $report.metrics.operationalFailures 1 'Operational failures'
    Assert-Equal $report.metrics.falsePositive.numerator 1 `
        'Guard false positive count'
    Assert-Equal $report.metrics.falsePositive.denominator 2 `
        'Guard denominator'
    Assert-Equal $report.metrics.highConfidenceWrong.numerator 1 `
        'High confidence wrong count'
    Assert-Equal @($report.sourceGroups).Count 2 'Reported source groups'
    Assert-Equal @($report.evaluationSets).Count 2 'Reported evaluation sets'
    Assert-Equal $report.candidateGate.pcmHistory.status 'ineligible' `
        'Undeclared source coverage cannot pass candidate gate'
    Assert-Equal $report.professionalAcceptance.status 'ineligible' `
        'Engineering candidate metrics never imply professional acceptance'
    Assert-Equal $report.professionalAcceptance.projectPolicy.priority 'low_false_positive' `
        'User delegated a low-error project policy'
    Assert-Equal $report.professionalAcceptance.projectPolicy.falsePositiveRateTarget 0.01 `
        'Project false-positive target remains explicit'
    Assert-Equal $report.professionalAcceptance.projectPolicy.highConfidenceWrongMaximum 0 `
        'High-confidence errors are not permitted'
    if (@($report.professionalAcceptance.reasons) -contains 'no_approved_professional_numeric_standard') {
        throw 'Do not ask the user again for a delegated project standard'
    }
    Assert-Equal $report.sourceEqualWeightDiagnostics.status `
        'reported_descriptive_only' `
        'Predeclared source-equal diagnostics are reported without a gate'
    Assert-Equal $report.sourceEqualWeightDiagnostics.sources 1 `
        'Heldout source-equal source count'
    Assert-Equal $report.sourceEqualWeightDiagnostics.mean.lossyRecall 0 `
        'Failed lossy sample has zero within-source recall'
    Assert-Equal `
        $report.sourceEqualWeightDiagnostics.mean.explicitFalsePositive 0 `
        'Heldout guard has zero within-source false-positive rate'
    Assert-Equal $report.sourceEqualWeightDiagnostics.mean.operationalFailure `
        0.333333 'Each source receives one equal-weight failure rate'

    $chainManifest = $blindManifest | ConvertTo-Json -Depth 8 |
        ConvertFrom-Json
    $chainSample = @($chainManifest.samples |
        Where-Object originalName -eq 'upsample.flac')[0]
    $chainSample.truth = 'lossy_upsample'
    $chainScan = @($scan | ConvertTo-Json -Depth 8 | ConvertFrom-Json)
    $chainPrediction = @($chainScan | Where-Object {
        [IO.Path]::GetFileName($_.path) -eq $chainSample.scanName
    })[0]
    $chainPrediction.verdict = 'suspected_lossy_transcode'
    $chainPrediction.confidence = 88
    $chainManifestPath = Join-Path $temporaryRoot 'manifest-chain.json'
    $chainScanPath = Join-Path $temporaryRoot 'scan-chain.json'
    $chainReportPath = Join-Path $temporaryRoot 'evaluation-chain.json'
    $chainManifest | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath $chainManifestPath -Encoding utf8
    @($chainScan) | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath $chainScanPath -Encoding utf8
    & $evaluateScript -Manifest $chainManifestPath -Scan $chainScanPath `
        -Output $chainReportPath | Out-Null
    $chainReport = Get-Content -LiteralPath $chainReportPath -Raw |
        ConvertFrom-Json
    Assert-Equal `
        $chainReport.sourceEqualWeightDiagnostics.mean.lossyUpsampleExact 0 `
        'A lossy-only verdict is not an exact composite-chain match'
    Assert-Equal `
        $chainReport.sourceEqualWeightDiagnostics.mean.lossyUpsamplePartial 1 `
        'A lossy-only verdict remains a partial composite-chain match'
    $chainSource = @($chainReport.sourceEqualWeightDiagnostics.perSource |
        Where-Object sourceGroup -eq 'music-b')[0]
    Assert-Equal $chainSource.lossyUpsampleExact.denominator 1 `
        'Composite-chain exact rate uses only lossy-upsample samples'
    Assert-Equal $chainSource.lossyUpsamplePartial.numerator 1 `
        'Composite-chain partial rate records family-only detection'

    $failedPositiveName = @($blindManifest.samples |
        Where-Object originalName -eq 'hidden-lossy.flac')[0].scanName
    foreach ($row in $scan) {
        if ([IO.Path]::GetFileName($row.path) -eq $failedPositiveName) {
            $row.verdict = 'suspected_lossy_transcode'
            $row.confidence = 90
        }
    }
    @($scan) | ConvertTo-Json -Depth 6 |
        Set-Content -LiteralPath $scanPath -Encoding utf8
    $errorVerdictReportPath = Join-Path $temporaryRoot `
        'evaluation-error-with-verdict.json'
    & $evaluateScript -Manifest $blindManifestPath -Scan $scanPath `
        -Output $errorVerdictReportPath | Out-Null
    $errorVerdictReport = Get-Content -LiteralPath $errorVerdictReportPath -Raw |
        ConvertFrom-Json
    $failedPositive = @($errorVerdictReport.rows |
        Where-Object originalName -eq 'hidden-lossy.flac')[0]
    Assert-Equal $failedPositive.operationalFailure $true `
        'A non-empty error remains an operational failure despite its verdict'
    $errorSourceLossy = @($errorVerdictReport.familyMetricsByTask.sourceInference |
        Where-Object family -eq 'lossy')[0]
    Assert-Equal $errorSourceLossy.truePositive 1 `
        'An operational failure cannot count as a source-inference true positive'
    Assert-Equal $errorVerdictReport.candidateGate.pcmHistory.observed.lossyRecall.numerator 0 `
        'An operational failure cannot improve candidate-gate recall'
    foreach ($row in $scan) {
        if ([IO.Path]::GetFileName($row.path) -eq $failedPositiveName) {
            $row.verdict = 'analysis_failed'
            $row.confidence = 0
        }
    }
    @($scan) | ConvertTo-Json -Depth 6 |
        Set-Content -LiteralPath $scanPath -Encoding utf8

    $missingScanPath = Join-Path $temporaryRoot 'scan-missing.json'
    @($scan | Select-Object -Skip 1) | ConvertTo-Json -Depth 6 |
        Set-Content -LiteralPath $missingScanPath -Encoding utf8
    Assert-ThrowsMatching {
        & $evaluateScript -Manifest $blindManifestPath -Scan $missingScanPath `
            -Output (Join-Path $temporaryRoot 'missing-report.json') | Out-Null
    } 'Expected exactly one scan result for *; found 0.' `
        'A partial scan cannot be evaluated as a complete manifest'

    $duplicateManifest = Get-Content -LiteralPath $blindManifestPath -Raw |
        ConvertFrom-Json
    $duplicateManifest.samples[1].scanName =
        $duplicateManifest.samples[0].scanName.ToUpperInvariant()
    $duplicateManifestPath = Join-Path $temporaryRoot 'manifest-duplicate.json'
    $duplicateManifest | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath $duplicateManifestPath -Encoding utf8
    Assert-ThrowsMatching {
        & $evaluateScript -Manifest $duplicateManifestPath -Scan $scanPath `
            -Output (Join-Path $temporaryRoot 'duplicate-report.json') | Out-Null
    } 'Duplicate scanName in manifest:*' `
        'Manifest scan names must be unique on a case-insensitive filesystem'

    $nonNeutralManifest = Get-Content -LiteralPath $blindManifestPath -Raw |
        ConvertFrom-Json
    $nonNeutralManifest.samples[0].blindId = '0123456789abcdef0123'
    $nonNeutralManifestPath = Join-Path $temporaryRoot 'manifest-nonneutral.json'
    $nonNeutralManifest | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath $nonNeutralManifestPath -Encoding utf8
    $nonNeutralReportPath = Join-Path $temporaryRoot 'nonneutral-report.json'
    & $evaluateScript -Manifest $nonNeutralManifestPath -Scan $scanPath `
        -Output $nonNeutralReportPath | Out-Null
    $nonNeutral = Get-Content -LiteralPath $nonNeutralReportPath -Raw |
        ConvertFrom-Json
    Assert-Equal $nonNeutral.protocolCompliant $false `
        'A blindId/name mismatch invalidates protocol compliance'

    $lossy = @($report.familyMetrics | Where-Object family -eq 'lossy')[0]
    Assert-Equal $lossy.truePositive 2 'Lossy family true positives'
    Assert-Equal $lossy.falseNegative 1 'Lossy family false negatives'
    Assert-Equal $lossy.precision.estimate 1 'Lossy family precision'
    Assert-Equal $lossy.recall.estimate 0.666667 'Lossy family recall'
    $sourceLossy = @($report.familyMetricsByTask.sourceInference |
        Where-Object family -eq 'lossy')[0]
    Assert-Equal $sourceLossy.actual 2 'Source-inference lossy positives'
    Assert-Equal $sourceLossy.truePositive 1 `
        'Source-inference lossy true positives'
    $formatLossy = @($report.familyMetricsByTask.formatRecognition |
        Where-Object family -eq 'lossy')[0]
    Assert-Equal $formatLossy.actual 1 'Format-recognition lossy positives'
    Assert-Equal $formatLossy.truePositive 1 `
        'Format-recognition lossy true positives'
    $upsample = @($report.familyMetrics |
        Where-Object family -eq 'upsample')[0]
    Assert-Equal $upsample.truePositive 0 'Upsample family true positives'
    Assert-Equal $upsample.falsePositive 1 'Upsample family false positives'
    Assert-Equal $upsample.falseNegative 2 'Upsample family false negatives'

    $partial = @($report.rows | Where-Object originalName -eq 'lossy-upsample.flac')[0]
    Assert-Equal $partial.partialCorrect $true 'Partial classification marker'
    Assert-Equal $partial.highConfidenceWrong $false `
        'Partial match is not high-confidence wholly wrong'
    $guard = @($report.rows |
        Where-Object originalName -eq 'lowpass-counterexample.wav')[0]
    Assert-Equal $guard.falsePositive $true 'Guard false positive marker'
    Assert-Equal $guard.highConfidenceWrong $true `
        'High-confidence guard violation marker'
    $failure = @($report.rows |
        Where-Object originalName -eq 'hidden-lossy.flac')[0]
    Assert-Equal $failure.operationalFailure $true 'Operational failure marker'
    Assert-Equal $failure.abstained $false 'Failure is not abstention'
    Assert-Equal (@($failure.requiredFamilies) -contains 'lossy') $true `
        'Post-lossy enhancement remains a positive lossy-history sample'
    Assert-Equal $failure.falsePositive $false `
        'Post-lossy enhancement is never treated as a negative control'
    $unknown = @($report.rows |
        Where-Object originalName -eq 'unknown-history-control.wav')[0]
    Assert-Equal $unknown.evaluationKind 'unscored_control' `
        'Unknown provenance control is unscored'
    Assert-Equal $unknown.falsePositive $false `
        'Unknown history is not an automatic false positive'
    Assert-Equal $unknown.highConfidenceWrong $false `
        'Unknown history is not proven high-confidence wrong'

    $blindManifest.protocol.coverageMinimums = [pscustomobject][ordered]@{
        lossyPositive=1;explicitNegative=1;nativeDsd=1;pcmToDsd=1
    }
    $gatedManifestPath = Join-Path $blindRoot 'manifest-gated.json'
    $blindManifest | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath $gatedManifestPath -Encoding utf8
    $gatedReportPath = Join-Path $temporaryRoot 'evaluation-gated.json'
    & $evaluateScript -Manifest $gatedManifestPath -Scan $scanPath `
        -Output $gatedReportPath | Out-Null
    $gated = Get-Content -LiteralPath $gatedReportPath -Raw | ConvertFrom-Json
    Assert-Equal $gated.candidateGate.pcmHistory.status 'ineligible' `
        'Repeated variants from one source cannot pass without cluster validation'
    if (@($gated.candidateGate.pcmHistory.reasons) -notcontains
        'correlated_source_variants_without_cluster_validation') {
        throw 'Repeated acceptance-source variants must produce an explicit gate reason.'
    }
    Assert-Equal $gated.candidateGate.dsd.status 'ineligible' `
        'PCM metrics cannot satisfy independent DSD coverage'
    Assert-Equal $gated.professionalAcceptance.status 'ineligible' `
        'Engineering candidate failure does not alter professional status'

    foreach ($sample in @($blindManifest.samples | Where-Object {
        $_.evaluationSet -eq 'heldout'
    })) {
        $sample.sourceGroup = 'heldout-' + $sample.blindId
    }
    $blindManifest | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath $gatedManifestPath -Encoding utf8
    & $evaluateScript -Manifest $gatedManifestPath -Scan $scanPath `
        -Output $gatedReportPath | Out-Null
    $independentGate = Get-Content -LiteralPath $gatedReportPath -Raw |
        ConvertFrom-Json
    Assert-Equal $independentGate.candidateGate.pcmHistory.status 'fail' `
        'Independent small fixture must fail conservative Wilson thresholds'

    $guardName = @($blindManifest.samples |
        Where-Object originalName -eq 'heldout-lowpass.wav')[0].scanName
    foreach ($row in $scan) {
        if ([IO.Path]::GetFileName($row.path) -eq $guardName) {
            $row.verdict = 'analysis_failed'
            $row.confidence = 0
            $row.error = 'decoder unavailable'
        }
    }
    @($scan) | ConvertTo-Json -Depth 6 |
        Set-Content -LiteralPath $scanPath -Encoding utf8
    & $evaluateScript -Manifest $gatedManifestPath -Scan $scanPath `
        -Output $gatedReportPath | Out-Null
    $failedGuard = Get-Content -LiteralPath $gatedReportPath -Raw |
        ConvertFrom-Json
    Assert-Equal $failedGuard.metrics.falsePositive.denominator 1 `
        'Failed guards are excluded from the false-positive denominator'
    Assert-Equal $failedGuard.candidateGate.pcmHistory.observedIndependentSources.explicitNegative 0 `
        'Failed guards do not satisfy independent negative-source coverage'
    Assert-Equal $failedGuard.candidateGate.pcmHistory.status 'ineligible' `
        'A failed acceptance guard cannot pass the candidate gate'
    if (@($failedGuard.candidateGate.pcmHistory.reasons) -notcontains
        'explicit_negative_operational_failures') {
        throw 'Failed acceptance guards must produce an explicit gate reason.'
    }
    Assert-Equal $failedGuard.candidateGate.pcmHistory.observed.explicitFalsePositive.denominator 0 `
        'An unevaluated guard is not evidence of a correct negative'
    foreach ($row in $scan) {
        if ([IO.Path]::GetFileName($row.path) -eq $guardName) {
            $row.verdict = 'inconclusive'
            $row.confidence = 10
            $row.error = ''
        }
    }

    foreach ($row in $scan) {
        if ([IO.Path]::GetFileName($row.path) -eq $guardName) {
            $row.verdict = 'suspected_lossy_transcode'; $row.confidence = 90
        }
    }
    @($scan) | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $scanPath -Encoding utf8
    & $evaluateScript -Manifest $gatedManifestPath -Scan $scanPath -Output $gatedReportPath | Out-Null
    $guardReport = Get-Content -LiteralPath $gatedReportPath -Raw | ConvertFrom-Json
    Assert-Equal $guardReport.candidateGate.pcmHistory.observed.lossyPrecision.denominator 1 `
        'Explicit negative false positives belong in source precision denominator'
    Assert-Equal $guardReport.candidateGate.pcmHistory.observed.lossyPrecision.numerator 0 `
        'A false-positive guard is not a source true positive'
    $heldoutSourceGroup = @($blindManifest.samples | Where-Object {
        $_.evaluationSet -eq 'heldout'
    })[0].sourceGroup
    @($blindManifest.samples |
        Where-Object originalName -eq 'native-generated.wav')[0].sourceGroup =
        $heldoutSourceGroup
    $blindManifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $gatedManifestPath -Encoding utf8
    & $evaluateScript -Manifest $gatedManifestPath -Scan $scanPath -Output $gatedReportPath | Out-Null
    $leaked = Get-Content -LiteralPath $gatedReportPath -Raw | ConvertFrom-Json
    Assert-Equal $leaked.protocolCompliant $false 'Source leakage between splits invalidates acceptance'
    Write-Host 'Lossless evaluation protocol regression passed.'
} finally {
    $resolvedTemporary = [IO.Path]::GetFullPath($temporaryRoot)
    $expectedParent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\','/')
    if ((Split-Path -Parent $resolvedTemporary) -ne $expectedParent -or
        (Split-Path -Leaf $resolvedTemporary) -notmatch '^agplayer-lossless-evaluation-[0-9a-f]{32}$') {
        throw 'Refusing cleanup outside the exact generated test directory'
    }
    if (Test-Path -LiteralPath $resolvedTemporary) {
        if ((Get-Item -LiteralPath $resolvedTemporary).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw 'Refusing cleanup of a reparse point'
        }
        Remove-Item -LiteralPath $resolvedTemporary -Recurse -Force
    }
}
