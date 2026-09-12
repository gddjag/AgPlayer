param(
    [Parameter(Mandatory=$true)][string]$Manifest,
    [Parameter(Mandatory=$true)][string]$Scan,
    [Parameter(Mandatory=$true)][string]$Output,
    [ValidateRange(0, 100)][int]$HighConfidenceThreshold = 80
)

$ErrorActionPreference = 'Stop'
$wilsonZ = 1.959963984540054

function Round-Rate([double]$Value) {
    return [Math]::Round($Value, 6, [MidpointRounding]::AwayFromZero)
}

function New-WilsonRate([int]$Numerator, [int]$Denominator) {
    if ($Denominator -eq 0) {
        return [ordered]@{
            numerator=$Numerator;denominator=0;estimate=$null;lower=$null
            upper=$null;confidenceLevel=0.95;method='wilson_score'
        }
    }
    $n = [double]$Denominator
    $p = $Numerator / $n
    $z2 = $wilsonZ * $wilsonZ
    $denominatorTerm = 1.0 + $z2 / $n
    $center = ($p + $z2 / (2.0 * $n)) / $denominatorTerm
    $margin = $wilsonZ * [Math]::Sqrt(
        ($p * (1.0 - $p) / $n) + ($z2 / (4.0 * $n * $n))) /
        $denominatorTerm
    return [ordered]@{
        numerator=$Numerator;denominator=$Denominator;estimate=(Round-Rate $p)
        lower=(Round-Rate ([Math]::Max(0.0, $center - $margin)))
        upper=(Round-Rate ([Math]::Min(1.0, $center + $margin)))
        confidenceLevel=0.95;method='wilson_score'
    }
}

function New-ObservedRate([int]$Numerator, [int]$Denominator) {
    return [ordered]@{
        numerator=$Numerator
        denominator=$Denominator
        value=$(if ($Denominator -eq 0) { $null } else {
            Round-Rate ($Numerator / [double]$Denominator)
        })
    }
}

function Get-EqualWeightMean([object[]]$PerSource, [string]$RateName) {
    $values = New-Object System.Collections.Generic.List[double]
    foreach ($source in $PerSource) {
        $rate = $source.PSObject.Properties[$RateName].Value
        if ($null -ne $rate.value) { $values.Add([double]$rate.value) }
    }
    if ($values.Count -eq 0) { return $null }
    return Round-Rate (($values | Measure-Object -Average).Average)
}

$truthDefinitions = @{
    generated_pcm = [ordered]@{kind='format_recognition';expected='credible_lossless';required=@('credible_pcm');forbidden=@();hidden=$false}
    generated_float_pcm = [ordered]@{kind='format_recognition';expected='credible_lossless';required=@('credible_pcm');forbidden=@();hidden=$false}
    bit_depth_expansion = [ordered]@{kind='source_inference';expected='suspected_bit_depth_expansion';required=@('bit_depth');forbidden=@();hidden=$false}
    upsample = [ordered]@{kind='source_inference';expected='suspected_upsample';required=@('upsample');forbidden=@();hidden=$true}
    lossy_transcode = [ordered]@{kind='source_inference';expected='suspected_lossy_transcode';required=@('lossy');forbidden=@();hidden=$true}
    lossy_upsample = [ordered]@{kind='source_inference';expected='suspected_lossy_upsample';required=@('lossy','upsample');forbidden=@();hidden=$true}
    known_lossy_container = [ordered]@{kind='format_recognition';expected='suspected_lossy_transcode';required=@('lossy');forbidden=@();hidden=$false}
    lowpass_not_lossy = [ordered]@{kind='guard_control';expected=$null;required=@();forbidden=@('lossy','upsample','bit_depth','pcm_to_dsd');hidden=$false}
    inconclusive = [ordered]@{kind='unscored_control';expected=$null;required=@();forbidden=@();hidden=$false}
    native_dsd = [ordered]@{kind='dsd_source_inference';expected='credible_native_dsd';required=@('native_dsd');forbidden=@();hidden=$true}
    pcm_to_dsd = [ordered]@{kind='dsd_source_inference';expected='suspected_pcm_to_dsd';required=@('pcm_to_dsd');forbidden=@();hidden=$true}
}
$verdictFamilies = @{
    credible_lossless=@('credible_pcm');credible_native_dsd=@('native_dsd')
    suspected_lossy_transcode=@('lossy');suspected_upsample=@('upsample')
    suspected_bit_depth_expansion=@('bit_depth')
    suspected_lossy_upsample=@('lossy','upsample')
    suspected_pcm_to_dsd=@('pcm_to_dsd');inconclusive=@()
    analysis_failed=@();cancelled=@()
}

$truth = Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json
$results = @(Get-Content -LiteralPath $Scan -Raw | ConvertFrom-Json)
$protocolWarnings = New-Object System.Collections.Generic.List[string]
$protocolCompliant = [string]$truth.schemaVersion -eq '2' -and
    $truth.protocol.blinded -eq $true
if (-not $protocolCompliant) {
    $protocolWarnings.Add(
        'Legacy/unblinded manifest: metrics are diagnostic; filename-based source-label leakage cannot be ruled out.')
}
$expectedAlgorithmVersion = [string]$truth.protocol.algorithmVersion
$expectedParameterVersion = [string]$truth.protocol.parameterVersion
$versionUnknown = [string]::IsNullOrWhiteSpace($expectedAlgorithmVersion) -or
    [string]::IsNullOrWhiteSpace($expectedParameterVersion)
$versionMismatch = $false
if ($versionUnknown) {
    $protocolCompliant = $false
    $protocolWarnings.Add(
        'Algorithm/parameter version is not locked; historical scan provenance is unknown.')
}

$seenScanNames = @{}
$seenBlindIds = @{}
$rows = New-Object System.Collections.Generic.List[object]
foreach ($sample in @($truth.samples)) {
    $truthLabel = [string]$sample.truth
    if (-not $truthDefinitions.ContainsKey($truthLabel)) {
        throw "Unknown truth label: $truthLabel"
    }
    $definition = $truthDefinitions[$truthLabel]
    $scanName = [string]$sample.scanName
    if ([string]::IsNullOrWhiteSpace($scanName)) {
        $scanName = [string]$sample.name
    }
    if ([string]::IsNullOrWhiteSpace($scanName)) {
        throw 'Every sample must provide scanName or name.'
    }
    $scanNameKey = $scanName.ToLowerInvariant()
    if ($seenScanNames.ContainsKey($scanNameKey)) {
        throw "Duplicate scanName in manifest: $scanName"
    }
    $seenScanNames[$scanNameKey] = $true
    $sourceGroup = [string]$sample.sourceGroup
    if ([string]::IsNullOrWhiteSpace($sourceGroup)) {
        $sourceGroup = 'legacy-unassigned'
        $protocolCompliant = $false
        $protocolWarnings.Add("Sample '$scanName' has no sourceGroup.")
    }
    $evaluationSet = [string]$sample.evaluationSet
    if ([string]::IsNullOrWhiteSpace($evaluationSet)) { $evaluationSet = [string]$sample.split }
    if ([string]::IsNullOrWhiteSpace($evaluationSet)) {
        $evaluationSet = 'legacy-unassigned'
        $protocolCompliant = $false
        $protocolWarnings.Add("Sample '$scanName' has no evaluationSet.")
    }
    if ([string]$truth.schemaVersion -eq '2') {
        $blindId = [string]$sample.blindId
        if ($sample.blinded -ne $true -or
            [string]::IsNullOrWhiteSpace($blindId)) {
            $protocolCompliant = $false
            $protocolWarnings.Add("Sample '$scanName' is not marked as blinded.")
        } else {
            $blindIdKey = $blindId.ToLowerInvariant()
            if ($seenBlindIds.ContainsKey($blindIdKey)) {
                throw "Duplicate blindId in manifest: $blindId"
            }
            $seenBlindIds[$blindIdKey] = $true
            $expectedScanName = "sample-$blindId$([IO.Path]::GetExtension($scanName).ToLowerInvariant())"
            if ($blindId -cnotmatch '^[0-9A-Fa-f]{20}$' -or
                $scanName -cne $expectedScanName) {
                $protocolCompliant = $false
                $protocolWarnings.Add(
                    "Sample '$scanName' does not use the protocol's neutral blind name.")
            }
        }
    }
    $matches = @($results | Where-Object {
        [IO.Path]::GetFileName([string]$_.path) -ceq $scanName
    })
    if ($matches.Count -ne 1) {
        throw "Expected exactly one scan result for $scanName; found $($matches.Count)."
    }
    $result = $matches[0]
    $rowAlgorithmVersion = [string]$result.algorithmVersion
    $rowParameterVersion = [string]$result.parameterVersion
    if ([string]::IsNullOrWhiteSpace($rowAlgorithmVersion) -or
        [string]::IsNullOrWhiteSpace($rowParameterVersion)) {
        $versionUnknown = $true
        $protocolCompliant = $false
        $protocolWarnings.Add(
            "Scan result '$scanName' has unknown historical algorithm/parameter version.")
    } elseif ((-not [string]::IsNullOrWhiteSpace($expectedAlgorithmVersion) -and
               $rowAlgorithmVersion -cne $expectedAlgorithmVersion) -or
              (-not [string]::IsNullOrWhiteSpace($expectedParameterVersion) -and
               $rowParameterVersion -cne $expectedParameterVersion)) {
        $versionMismatch = $true
        $protocolCompliant = $false
        $protocolWarnings.Add("Scan result '$scanName' does not match the locked version.")
    }
    $predicted = [string]$result.verdict
    if ([string]::IsNullOrWhiteSpace($predicted) -and $result.error) {
        $predicted = 'analysis_failed'
    }
    if (-not $verdictFamilies.ContainsKey($predicted)) {
        throw "Unknown predicted verdict for ${scanName}: $predicted"
    }
    $declaredHash = [string]$sample.sha256
    $scanPath = [string]$result.path
    if (-not [string]::IsNullOrWhiteSpace($declaredHash) -and
        (Test-Path -LiteralPath $scanPath -PathType Leaf)) {
        $actualHash = (Get-FileHash -LiteralPath $scanPath -Algorithm SHA256).Hash
        if ($actualHash -cne $declaredHash.ToUpperInvariant()) {
            throw "Scanned file hash does not match manifest: $scanName"
        }
    } elseif ([string]$truth.schemaVersion -eq '2') {
        throw "Protocol sample cannot be hash-verified: $scanName"
    }
    $operationalFailure = $predicted -in @('analysis_failed','cancelled') -or
        -not [string]::IsNullOrWhiteSpace([string]$result.error)
    $abstained = -not $operationalFailure -and $predicted -eq 'inconclusive'
    $predictedFamilies = @($verdictFamilies[$predicted])
    $requiredFamilies = @($definition.required)
    $forbiddenFamilies = @($definition.forbidden)
    $matchedFamilies = @($requiredFamilies | Where-Object {
        $predictedFamilies -contains $_
    })
    $violatedFamilies = @($forbiddenFamilies | Where-Object {
        $predictedFamilies -contains $_
    })
    $exactEligible = $null -ne $definition.expected
    $exactCorrect = $exactEligible -and -not $operationalFailure -and
        $predicted -ceq [string]$definition.expected
    $partialCorrect = $exactEligible -and -not $exactCorrect -and
        $matchedFamilies.Count -gt 0
    $falsePositive = $definition.kind -eq 'guard_control' -and
        $violatedFamilies.Count -gt 0
    $whollyWrong = $falsePositive -or ($exactEligible -and
        -not $operationalFailure -and -not $abstained -and
        $matchedFamilies.Count -eq 0)
    $confidence = [int]$result.confidence
    $highConfidenceWrong = $whollyWrong -and
        $confidence -ge $HighConfidenceThreshold
    $acceptedCheck = if ($exactEligible) {
        $exactCorrect
    } elseif ($definition.kind -eq 'guard_control') {
        -not $operationalFailure -and -not $falsePositive
    } else {
        $false
    }
    $rows.Add([pscustomobject][ordered]@{
        blindId=[string]$sample.blindId;scanName=$scanName
        originalName=$(if ([string]::IsNullOrWhiteSpace(
            [string]$sample.originalName)) { $scanName } else {
            [string]$sample.originalName })
        sourceGroup=$sourceGroup;evaluationSet=$evaluationSet;truth=$truthLabel
        transformation=[string]$sample.transformation
        encoderArguments=@($sample.encoderArguments)
        algorithmVersion=$rowAlgorithmVersion
        parameterVersion=$rowParameterVersion
        evaluationKind=[string]$definition.kind;expectedExact=$definition.expected
        predicted=$predicted;confidence=$confidence
        requiredFamilies=$requiredFamilies;predictedFamilies=$predictedFamilies
        matchedFamilies=$matchedFamilies;exactEligible=$exactEligible
        exactCorrect=$exactCorrect;partialCorrect=$partialCorrect
        abstained=$abstained;operationalFailure=$operationalFailure
        falsePositive=$falsePositive;highConfidenceWrong=$highConfidenceWrong
        highConfidenceExactMismatch=($confidence -ge $HighConfidenceThreshold -and
            $exactEligible -and -not $exactCorrect -and -not $operationalFailure -and -not $abstained)
        acceptedCheck=$acceptedCheck;hidden=[bool]$definition.hidden
        error=[string]$result.error
    })
}

function New-GroupMetrics([object[]]$GroupRows, [string]$Name) {
    $exactRows = @($GroupRows | Where-Object exactEligible)
    $nonOperational = @($GroupRows | Where-Object { -not $_.operationalFailure })
    $guards = @($GroupRows | Where-Object evaluationKind -eq 'guard_control')
    $evaluatedGuards = @($guards | Where-Object { -not $_.operationalFailure })
    $requiredCount = 0
    $matchedCount = 0
    foreach ($row in $GroupRows) {
        $requiredCount += @($row.requiredFamilies).Count
        $matchedCount += @($row.matchedFamilies).Count
    }
    $exactCorrect = @($exactRows | Where-Object exactCorrect).Count
    $abstained = @($nonOperational | Where-Object abstained).Count
    $falsePositive = @($evaluatedGuards | Where-Object falsePositive).Count
    $highWrong = @($nonOperational | Where-Object highConfidenceWrong).Count
    $failures = @($GroupRows | Where-Object operationalFailure).Count
    return [ordered]@{
        name=$Name;samples=$GroupRows.Count
        independentSources=@($GroupRows | Where-Object {
            $_.sourceGroup -ne 'legacy-unassigned'
        } | Select-Object -ExpandProperty sourceGroup | Sort-Object -Unique).Count
        exact=(New-WilsonRate $exactCorrect $exactRows.Count)
        familyRecall=(New-WilsonRate $matchedCount $requiredCount)
        abstention=(New-WilsonRate $abstained $nonOperational.Count)
        falsePositive=(New-WilsonRate $falsePositive $evaluatedGuards.Count)
        highConfidenceWrong=(New-WilsonRate $highWrong $nonOperational.Count)
        operationalFailure=(New-WilsonRate $failures $GroupRows.Count)
    }
}

$rowArray = @($rows.ToArray())
$exactRows = @($rowArray | Where-Object exactEligible)
$nonOperational = @($rowArray | Where-Object { -not $_.operationalFailure })
$guards = @($rowArray | Where-Object evaluationKind -eq 'guard_control')
$evaluatedGuards = @($guards | Where-Object { -not $_.operationalFailure })
$failures = @($rowArray | Where-Object operationalFailure).Count
$metrics = [ordered]@{
    exact=(New-WilsonRate @($exactRows | Where-Object exactCorrect).Count $exactRows.Count)
    partialCorrect=@($rowArray | Where-Object partialCorrect).Count
    highConfidenceExactMismatches=@($rowArray | Where-Object highConfidenceExactMismatch).Count
    abstention=(New-WilsonRate @($nonOperational | Where-Object abstained).Count $nonOperational.Count)
    operationalFailures=$failures
    operationalFailureRate=(New-WilsonRate $failures $rowArray.Count)
    falsePositive=(New-WilsonRate `
        @($evaluatedGuards | Where-Object falsePositive).Count `
        $evaluatedGuards.Count)
    highConfidenceWrong=(New-WilsonRate @($nonOperational | Where-Object highConfidenceWrong).Count $nonOperational.Count)
}

$allFamilies = @('credible_pcm','lossy','upsample','bit_depth',
                 'native_dsd','pcm_to_dsd')
function New-FamilyMetrics([object[]]$InputRows) {
    return @(
        foreach ($family in $allFamilies) {
            $actual = @($InputRows | Where-Object {
            @($_.requiredFamilies) -contains $family
            })
            $predictedEligible = @($InputRows | Where-Object {
                -not $_.operationalFailure -and
                (@($_.predictedFamilies) -contains $family) -and
                ($_.exactEligible -or
                 (@($truthDefinitions[$_.truth].forbidden) -contains $family))
            })
            $truePositive = @($predictedEligible | Where-Object {
                @($_.requiredFamilies) -contains $family
            }).Count
            $falsePositive = $predictedEligible.Count - $truePositive
            $falseNegative = $actual.Count - $truePositive
            [ordered]@{
                family=$family;actual=$actual.Count
                predicted=$predictedEligible.Count;truePositive=$truePositive
                falsePositive=$falsePositive;falseNegative=$falseNegative
                precision=(New-WilsonRate $truePositive $predictedEligible.Count)
                recall=(New-WilsonRate $truePositive $actual.Count)
            }
        }
    )
}
$familyMetrics = New-FamilyMetrics $rowArray
$familyMetricsByTask = [ordered]@{
    sourceInference=(New-FamilyMetrics @($rowArray | Where-Object {
        $_.evaluationKind -in @('source_inference','guard_control')
    }))
    formatRecognition=(New-FamilyMetrics @($rowArray | Where-Object {
        $_.evaluationKind -eq 'format_recognition'
    }))
    dsdSourceInference=(New-FamilyMetrics @($rowArray | Where-Object {
        $_.evaluationKind -eq 'dsd_source_inference'
    }))
}
$independentSources = @($rowArray | Where-Object {
    $_.sourceGroup -ne 'legacy-unassigned'
} | Select-Object -ExpandProperty sourceGroup | Sort-Object -Unique).Count
$gateRows = @($rowArray | Where-Object {
    $_.evaluationSet -in @('heldout','external_validation')
})
$gateCandidateRows = @($gateRows | Where-Object {
    $_.evaluationKind -in @('source_inference','guard_control') -and
    $_.sourceGroup -ne 'legacy-unassigned'
})
$gateCorrelatedSourceGroups = @($gateCandidateRows |
    Group-Object sourceGroup | Where-Object Count -gt 1)
$gateGuards = @($gateRows | Where-Object evaluationKind -eq 'guard_control')
$gateEvaluatedGuards = @($gateGuards | Where-Object {
    -not $_.operationalFailure
})
$gateGuardFailures = @($gateGuards | Where-Object operationalFailure)
foreach ($group in $rowArray | Group-Object sourceGroup) {
    $sets = @($group.Group.evaluationSet | Sort-Object -Unique)
    if ($sets -contains 'development' -and
        ($sets -contains 'heldout' -or $sets -contains 'external_validation')) {
        $protocolCompliant = $false
        $protocolWarnings.Add("Source group '$($group.Name)' crosses development and acceptance sets.")
    }
}
$gateInferenceRows = @($gateRows | Where-Object {
    $_.evaluationKind -eq 'source_inference'
})
$gateLossyPositiveSources = @($gateRows | Where-Object {
    $_.sourceGroup -ne 'legacy-unassigned' -and
    $_.evaluationKind -eq 'source_inference' -and
    @($_.requiredFamilies) -contains 'lossy'
} | Select-Object -ExpandProperty sourceGroup | Sort-Object -Unique).Count
$gateExplicitNegativeSources = @($gateEvaluatedGuards | Where-Object {
    $_.sourceGroup -ne 'legacy-unassigned' -and
    -not [string]::IsNullOrWhiteSpace($_.sourceGroup)
} | Select-Object -ExpandProperty sourceGroup | Sort-Object -Unique).Count
$gateNativeDsdSources = @($gateRows | Where-Object {
    $_.sourceGroup -ne 'legacy-unassigned' -and
    @($_.requiredFamilies) -contains 'native_dsd'
} | Select-Object -ExpandProperty sourceGroup | Sort-Object -Unique).Count
$gatePcmToDsdSources = @($gateRows | Where-Object {
    $_.sourceGroup -ne 'legacy-unassigned' -and
    @($_.requiredFamilies) -contains 'pcm_to_dsd'
} | Select-Object -ExpandProperty sourceGroup | Sort-Object -Unique).Count
$gateLossyActual = @($gateInferenceRows | Where-Object {
    @($_.requiredFamilies) -contains 'lossy'
})
$gateLossyPredicted = @($gateInferenceRows | Where-Object {
    -not $_.operationalFailure -and
    @($_.predictedFamilies) -contains 'lossy'
})
$gateLossyPredicted += @($gateEvaluatedGuards | Where-Object {
    @($_.predictedFamilies) -contains 'lossy' -and
    @($truthDefinitions[$_.truth].forbidden) -contains 'lossy'
})
$gateLossyTruePositive = @($gateLossyPredicted | Where-Object {
    @($_.requiredFamilies) -contains 'lossy'
}).Count
$gateLossyPrecision = New-WilsonRate $gateLossyTruePositive `
    $gateLossyPredicted.Count
$gateLossyRecall = New-WilsonRate $gateLossyTruePositive `
    $gateLossyActual.Count
$gateFalsePositive = New-WilsonRate `
    @($gateEvaluatedGuards | Where-Object falsePositive).Count `
    $gateEvaluatedGuards.Count
$gateNonOperational = @($gateRows | Where-Object {
    $_.evaluationKind -in @('source_inference','guard_control') -and
    -not $_.operationalFailure
})
$gateHighWrong = New-WilsonRate `
    @($gateNonOperational | Where-Object highConfidenceWrong).Count `
    $gateNonOperational.Count
$coverageMinimums = $truth.protocol.coverageMinimums
$coverageProperties = @()
if ($null -ne $coverageMinimums) {
    $coverageProperties = @($coverageMinimums.PSObject.Properties.Name)
}
$pcmGateReasons = New-Object System.Collections.Generic.List[string]
$coverageDeclared = $coverageProperties -contains 'lossyPositive' -and
    $coverageProperties -contains 'explicitNegative' -and
    [int]$coverageMinimums.lossyPositive -gt 0 -and
    [int]$coverageMinimums.explicitNegative -gt 0
if (-not $protocolCompliant) {
    $pcmGateReasons.Add('manifest_not_protocol_compliant')
}
if ($gateRows.Count -eq 0) {
    $pcmGateReasons.Add('no_locked_acceptance_rows')
}
if ($gateCorrelatedSourceGroups.Count -gt 0) {
    $pcmGateReasons.Add(
        'correlated_source_variants_without_cluster_validation')
}
if ($gateGuardFailures.Count -gt 0) {
    $pcmGateReasons.Add('explicit_negative_operational_failures')
}
if (-not $coverageDeclared) {
    $pcmGateReasons.Add('independent_source_coverage_minima_not_declared')
} else {
    if ($gateLossyPositiveSources -lt [int]$coverageMinimums.lossyPositive) {
        $pcmGateReasons.Add('insufficient_lossy_positive_sources')
    }
    if ($gateExplicitNegativeSources -lt [int]$coverageMinimums.explicitNegative) {
        $pcmGateReasons.Add('insufficient_explicit_negative_sources')
    }
}
$pcmGateEligible = $pcmGateReasons.Count -eq 0
$pcmThresholdsPass = $gateLossyPrecision.lower -ne $null -and
    $gateLossyPrecision.lower -ge 0.95 -and
    $gateLossyRecall.lower -ne $null -and
    $gateLossyRecall.lower -ge 0.80 -and
    $gateFalsePositive.upper -ne $null -and
    $gateFalsePositive.upper -le 0.01 -and
    $gateHighWrong.numerator -eq 0
$pcmGateStatus = if (-not $pcmGateEligible) {
    'ineligible'
} elseif ($pcmThresholdsPass) {
    'pass'
} else {
    'fail'
}
$dsdCoverageDeclared = $coverageProperties -contains 'nativeDsd' -and
    $coverageProperties -contains 'pcmToDsd' -and
    [int]$coverageMinimums.nativeDsd -gt 0 -and
    [int]$coverageMinimums.pcmToDsd -gt 0
$dsdStatus = if (-not $dsdCoverageDeclared -or
    $gateNativeDsdSources -lt [int]$coverageMinimums.nativeDsd -or
    $gatePcmToDsdSources -lt [int]$coverageMinimums.pcmToDsd) {
    'ineligible'
} else {
    'reported_separately_no_approved_threshold'
}
$candidateGate = [ordered]@{
    authority='unapproved_engineering_candidate; not an industry standard or release requirement'
    intervalDecisionRule='precision/recall use Wilson lower bounds; false-positive uses Wilson upper bound'
    pcmHistory=[ordered]@{
        status=$pcmGateStatus
        evaluationSets=@('heldout','external_validation')
        thresholds=[ordered]@{
            lossyPrecisionLower=0.95;lossyRecallLower=0.80
            explicitFalsePositiveUpper=0.01;highConfidenceWrongMaximum=0
        }
        minimumIndependentSources=$coverageMinimums
        observedIndependentSources=[ordered]@{
            lossyPositive=$gateLossyPositiveSources
            explicitNegative=$gateExplicitNegativeSources
        }
        observed=[ordered]@{
            lossyPrecision=$gateLossyPrecision
            lossyRecall=$gateLossyRecall
            explicitFalsePositive=$gateFalsePositive
            highConfidenceWrong=$gateHighWrong
            explicitNegativeOperationalFailures=$gateGuardFailures.Count
        }
        reasons=@($pcmGateReasons)
    }
    dsd=[ordered]@{
        status=$dsdStatus
        observedIndependentSources=[ordered]@{
            nativeDsd=$gateNativeDsdSources;pcmToDsd=$gatePcmToDsdSources
        }
        note='DSD is reported independently; no DSD candidate performance threshold is approved.'
    }
}
$scanVersionStatus = if ($versionMismatch) {
    'mismatch'
} elseif ($versionUnknown) {
    'historical_or_unknown'
} else {
    'verified'
}
$scanVersion = [ordered]@{
    status=$scanVersionStatus
    expectedAlgorithmVersion=$expectedAlgorithmVersion
    expectedParameterVersion=$expectedParameterVersion
    observedAlgorithmVersions=@($rowArray | Select-Object -ExpandProperty algorithmVersion |
        Sort-Object -Unique)
    observedParameterVersions=@($rowArray | Select-Object -ExpandProperty parameterVersion |
        Sort-Object -Unique)
}
$professionalAcceptance = [ordered]@{
    status='ineligible'
    projectPolicy=[ordered]@{
        version='low-error-2026-09-06'
        authority='User delegated project criteria with low error as priority; not an industry standard'
        priority='low_false_positive'
        falsePositiveRateTarget=0.01
        highConfidenceWrongMaximum=0
        confidenceIsCalibratedProbability=$false
        abstention='Allowed when evidence is insufficient; report recall and abstention separately'
        populationClaim='Requires representative independent validation and an uncertainty interval; synthetic variants do not establish a real-recording population rate'
    }
    reasons=@(
        'large_scale_independent_source_validation_missing',
        'native_dsd_and_pcm_to_dsd_validation_missing',
        'confidence_calibration_missing'
    )
    note='Project criteria are delegated and defined; remaining validation is owned by development, not a request for user samples. Candidate metrics alone cannot establish professional accuracy.'
}
$sourceEqualPolicy = $truth.protocol.sourceEqualWeightDiagnostics
$sourceEqualStatus = 'not_predeclared'
$sourceEqualRows = @()
$sourceEqualSets = @()
if ($null -ne $sourceEqualPolicy -and $sourceEqualPolicy.enabled -eq $true) {
    $sourceEqualSets = @($sourceEqualPolicy.evaluationSets)
    if ([string]$sourceEqualPolicy.aggregation -cne
            'mean_of_within_source_rates' -or $sourceEqualSets.Count -eq 0) {
        $sourceEqualStatus = 'invalid_predeclared_policy'
        $protocolCompliant = $false
        $protocolWarnings.Add(
            'Source-equal diagnostics policy is incomplete or unsupported.')
    } else {
        $diagnosticRows = @($rowArray | Where-Object {
            $_.evaluationSet -in $sourceEqualSets -and
            $_.evaluationKind -in @('source_inference','guard_control') -and
            $_.sourceGroup -ne 'legacy-unassigned'
        })
        $sourceEqualRows = @(
            foreach ($group in $diagnosticRows | Group-Object sourceGroup |
                     Sort-Object Name) {
                $groupRows = @($group.Group)
                $lossyActual = @($groupRows | Where-Object {
                    $_.evaluationKind -eq 'source_inference' -and
                    @($_.requiredFamilies) -contains 'lossy'
                })
                $lossyTruePositive = @($lossyActual | Where-Object {
                    -not $_.operationalFailure -and
                    @($_.predictedFamilies) -contains 'lossy'
                }).Count
                $lossyUpsampleActual = @($groupRows | Where-Object {
                    $_.truth -eq 'lossy_upsample'
                })
                $evaluatedSourceGuards = @($groupRows | Where-Object {
                    $_.evaluationKind -eq 'guard_control' -and
                    -not $_.operationalFailure
                })
                $nonOperationalGroupRows = @($groupRows | Where-Object {
                    -not $_.operationalFailure
                })
                [pscustomobject][ordered]@{
                    sourceGroup=$group.Name
                    samples=$groupRows.Count
                    lossyRecall=(New-ObservedRate $lossyTruePositive `
                        $lossyActual.Count)
                    lossyUpsampleExact=(New-ObservedRate `
                        @($lossyUpsampleActual |
                            Where-Object exactCorrect).Count `
                        $lossyUpsampleActual.Count)
                    lossyUpsamplePartial=(New-ObservedRate `
                        @($lossyUpsampleActual |
                            Where-Object partialCorrect).Count `
                        $lossyUpsampleActual.Count)
                    explicitFalsePositive=(New-ObservedRate `
                        @($evaluatedSourceGuards | Where-Object falsePositive).Count `
                        $evaluatedSourceGuards.Count)
                    operationalFailure=(New-ObservedRate `
                        @($groupRows | Where-Object operationalFailure).Count `
                        $groupRows.Count)
                    highConfidenceWrong=(New-ObservedRate `
                        @($nonOperationalGroupRows |
                            Where-Object highConfidenceWrong).Count `
                        $nonOperationalGroupRows.Count)
                }
            }
        )
        $sourceEqualStatus = if ($sourceEqualRows.Count -eq 0) {
            'no_rows'
        } else {
            'reported_descriptive_only'
        }
    }
}
$sourceEqualDiagnostics = [ordered]@{
    status=$sourceEqualStatus
    aggregation='equal_weight_mean_of_within_source_rates'
    evaluationSets=$sourceEqualSets
    sources=$sourceEqualRows.Count
    mean=[ordered]@{
        lossyRecall=(Get-EqualWeightMean `
            -PerSource $sourceEqualRows -RateName 'lossyRecall')
        lossyUpsampleExact=(Get-EqualWeightMean `
            -PerSource $sourceEqualRows -RateName 'lossyUpsampleExact')
        lossyUpsamplePartial=(Get-EqualWeightMean `
            -PerSource $sourceEqualRows -RateName 'lossyUpsamplePartial')
        explicitFalsePositive=(Get-EqualWeightMean `
            -PerSource $sourceEqualRows -RateName 'explicitFalsePositive')
        operationalFailure=(Get-EqualWeightMean `
            -PerSource $sourceEqualRows -RateName 'operationalFailure')
        highConfidenceWrong=(Get-EqualWeightMean `
            -PerSource $sourceEqualRows -RateName 'highConfidenceWrong')
    }
    perSource=$sourceEqualRows
    note='Descriptive only: each source has equal weight after computing within-source rates; no independence assumption, confidence interval, candidate gate, or professional claim.'
}
$sourceGroups = @(
    foreach ($group in $rowArray | Group-Object sourceGroup | Sort-Object Name) {
        New-GroupMetrics @($group.Group) $group.Name
    }
)
$evaluationKinds = @(
    foreach ($group in $rowArray | Group-Object evaluationKind | Sort-Object Name) {
        New-GroupMetrics @($group.Group) $group.Name
    }
)
$evaluationSets = @(
    foreach ($group in $rowArray | Group-Object evaluationSet | Sort-Object Name) {
        New-GroupMetrics @($group.Group) $group.Name
    }
)
$confusion = @(
    foreach ($group in $rowArray | Group-Object truth,predicted) {
        [ordered]@{truth=$group.Group[0].truth
            predicted=$group.Group[0].predicted;count=$group.Count}
    }
)
$hidden = @($rowArray | Where-Object hidden)
$report = [ordered]@{
    schemaVersion='2';scope=[string]$truth.description
    limitations='Evaluation reports observed classification behavior only. Confidence is uncalibrated evidence strength. The low-error project target is not an industry standard or a measured population guarantee.'
    protocolCompliant=$protocolCompliant
    protocolWarnings=@($protocolWarnings | Sort-Object -Unique)
    scanVersion=$scanVersion
    manifest=[IO.Path]::GetFullPath($Manifest);scan=[IO.Path]::GetFullPath($Scan)
    highConfidenceThreshold=$HighConfidenceThreshold;samples=$rowArray.Count
    independentSources=$independentSources
    dependenceNote='Wilson intervals use files as observations and do not make same-source derivatives independent or prove population performance; source counts are reported separately.'
    acceptedChecks=@($rowArray | Where-Object acceptedCheck).Count
    controls=$guards.Count;hiddenSamples=$hidden.Count
    hiddenCorrect=@($hidden | Where-Object exactCorrect).Count
    metrics=$metrics;candidateGate=$candidateGate
    sourceEqualWeightDiagnostics=$sourceEqualDiagnostics
    professionalAcceptance=$professionalAcceptance
    sourceGroups=$sourceGroups;evaluationKinds=$evaluationKinds
    evaluationSets=$evaluationSets
    familyMetrics=$familyMetrics;familyMetricsByTask=$familyMetricsByTask
    confusion=$confusion;rows=$rowArray
}
$report | ConvertTo-Json -Depth 10 |
    Set-Content -LiteralPath $Output -Encoding utf8
[pscustomobject][ordered]@{
    samples=$rowArray.Count;exactCorrect=$metrics.exact.numerator
    exactEligible=$metrics.exact.denominator
    abstained=$metrics.abstention.numerator;operationalFailures=$failures
    falsePositive=$metrics.falsePositive.numerator
    protocolCompliant=$protocolCompliant
}
