[CmdletBinding()]
param(
    [string]$BuildDirectory = "build/frequency-clean-release",
    [string]$Fixture,
    [string]$OutputDirectory = "artifacts/frequency-color-waveform",
    [string]$RepositoryRoot,
    [string]$BaselineRepositoryRoot,
    [string]$BaselineBuildDirectory,
    [string]$BaselineMetrics,
    [int]$ProgressDurationSeconds = 600,
    [int]$ProbeIterations = 100,
    [int]$PairCount = 10,
    [int]$PairSeed = 20260831,
    [switch]$BaselineOnly
)

$ErrorActionPreference = "Stop"

function Assert-UnderPath {
    param([string]$Path, [string]$Root, [string]$Label)
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $fullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $prefix = $fullRoot + [System.IO.Path]::DirectorySeparatorChar
    if ($fullPath -ne $fullRoot -and -not $fullPath.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label must stay under ${fullRoot}: $fullPath"
    }
    return $fullPath
}

function Get-Median {
    param([double[]]$Values)
    if ($Values.Count -eq 0) { throw "Cannot calculate a median from no values" }
    $ordered = @($Values | Sort-Object)
    $middle = [int][math]::Floor($ordered.Count / 2)
    if (($ordered.Count % 2) -eq 1) { return [double]$ordered[$middle] }
    return ([double]$ordered[$middle - 1] + [double]$ordered[$middle]) / 2.0
}

function Get-AppTreeBytes {
    param([string]$BuildRoot)
    $appRoot = Join-Path $BuildRoot "app"
    if (-not (Test-Path -LiteralPath $appRoot -PathType Container)) { throw "Application output directory is missing: $appRoot" }
    return [int64](Get-ChildItem -LiteralPath $appRoot -Recurse -File | Measure-Object -Property Length -Sum).Sum
}

function Get-CacheValue {
    param([string]$CachePath, [string]$Name)
    $line = Select-String -LiteralPath $CachePath -Pattern ("^{0}:[^=]*=(.+)$" -f [regex]::Escape($Name)) | Select-Object -First 1
    if ($null -eq $line) { throw "$Name is missing from $CachePath" }
    return $line.Matches[0].Groups[1].Value
}

function Get-Sha256String {
    param([string]$Text)
    $bytes = [Text.Encoding]::UTF8.GetBytes($Text)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace("-", "") }
    finally { $sha.Dispose() }
}

function Get-ComparableBuildConfig {
    param([string]$CachePath)
    $keys = @("CMAKE_BUILD_TYPE", "CMAKE_CXX_COMPILER", "CMAKE_CXX_FLAGS_RELEASE", "CMAKE_GENERATOR", "CMAKE_PREFIX_PATH", "VCPKG_MANIFEST_MODE", "VCPKG_INSTALLED_DIR")
    $config = [ordered]@{}
    foreach ($key in $keys) { $config[$key] = (Get-CacheValue -CachePath $CachePath -Name $key).Replace('/', '\').Trim() }
    return [pscustomobject]@{ values = $config; fingerprint = Get-Sha256String -Text ($config | ConvertTo-Json -Compress) }
}

function Get-Percentile {
    param([double[]]$Values, [double]$Quantile)
    if ($Values.Count -eq 0 -or $Quantile -lt 0.0 -or $Quantile -gt 1.0) { throw "Invalid percentile input" }
    $ordered = @($Values | Sort-Object)
    $position = ($ordered.Count - 1) * $Quantile
    $lower = [int][math]::Floor($position)
    $upper = [int][math]::Ceiling($position)
    if ($lower -eq $upper) { return [double]$ordered[$lower] }
    return [double]$ordered[$lower] + ($position - $lower) * ([double]$ordered[$upper] - [double]$ordered[$lower])
}

function Get-BootstrapMedianConfidenceInterval {
    param([double[]]$Values, [int]$Seed)
    if ($Values.Count -eq 0) { throw "Cannot bootstrap empty values" }
    $random = [System.Random]::new($Seed)
    $resamples = New-Object 'System.Collections.Generic.List[double]'
    for ($round = 0; $round -lt 5000; ++$round) {
        $sample = New-Object 'System.Collections.Generic.List[double]'
        for ($index = 0; $index -lt $Values.Count; ++$index) {
            $sample.Add([double]$Values[$random.Next($Values.Count)])
        }
        $resamples.Add((Get-Median -Values $sample.ToArray()))
    }
    return [ordered]@{ method = "deterministic-bootstrap-median"; seed = $Seed; resamples = 5000; lower95 = Get-Percentile -Values $resamples.ToArray() -Quantile 0.025; upper95 = Get-Percentile -Values $resamples.ToArray() -Quantile 0.975 }
}

function Get-MeasurementAffinityMask {
    $available = [UInt64][Diagnostics.Process]::GetCurrentProcess().ProcessorAffinity.ToInt64()
    if ($available -eq 0U) { throw "Cannot determine current process affinity" }
    for ($bit = 0; $bit -lt 63; ++$bit) {
        $candidate = ([UInt64]1 -shl $bit)
        if (($available -band $candidate) -ne 0U) { return [UInt64]$candidate }
    }
    throw "No usable single-CPU affinity bit is available"
}

function Get-DependencyEvidence {
    param([string]$CachePath)
    $installed = Get-CacheValue -CachePath $CachePath -Name "VCPKG_INSTALLED_DIR"
    $root = Join-Path $installed "x64-windows"
    $relativePaths = @(
        "include/miniaudio.h",
        "lib/avformat.lib",
        "lib/avcodec.lib",
        "lib/swresample.lib",
        "lib/avutil.lib",
        "lib/SoundTouch.lib"
    )
    $files = foreach ($relativePath in $relativePaths) {
        $path = Join-Path $root $relativePath
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Required comparison dependency is missing: $path"
        }
        [ordered]@{ path = $path; sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
    }
    return [ordered]@{ installedRoot = $installed; files = $files }
}

function Get-CpuSnapshot {
    $values = @(Get-CimInstance Win32_Processor | ForEach-Object { [int]$_.LoadPercentage })
    if ($values.Count -eq 0) { return $null }
    return [int][math]::Round(($values | Measure-Object -Average).Average)
}

function Get-RepositoryIdentity {
    param([string]$Root, [string]$Label, [switch]$RequireClean)
    $head = (& git -C $Root rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($head)) {
        throw "$Label QA cannot resolve the repository HEAD: $Root"
    }
    $status = @(& git -C $Root status --porcelain=v1 --untracked-files=all)
    if ($LASTEXITCODE -ne 0) {
        throw "$Label QA cannot inspect repository cleanliness: $Root"
    }
    $dirtyEntries = @($status | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    $clean = $dirtyEntries.Count -eq 0
    if ($RequireClean -and -not $clean) {
        throw "$Label QA requires a clean candidate repository; refusing to measure an uncommitted overlay in $Root"
    }
    return [pscustomobject]@{ head = $head; clean = $clean; status = $dirtyEntries }
}

function New-TargetContext {
    param([string]$Label, [string]$Root, [string]$BuildDirectoryArgument, [string]$FixtureArgument, [string]$ProbeSource, [switch]$RequireCleanRepository)
    $resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
    $resolvedBuild = if ([IO.Path]::IsPathRooted($BuildDirectoryArgument)) { (Resolve-Path -LiteralPath $BuildDirectoryArgument).Path } else { (Resolve-Path -LiteralPath (Join-Path $resolvedRoot $BuildDirectoryArgument)).Path }
    $resolvedBuild = Assert-UnderPath -Path $resolvedBuild -Root $resolvedRoot -Label "$Label BuildDirectory"
    $cachePath = Join-Path $resolvedBuild "CMakeCache.txt"
    $appPath = Join-Path $resolvedBuild "app/AgPlayer.exe"
    foreach ($required in @($cachePath, $appPath)) { if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "$Label QA input is missing: $required" } }
    $fixtureCandidate = if ([string]::IsNullOrWhiteSpace($FixtureArgument)) { Join-Path $resolvedBuild "tests/fixtures/sine-440hz.wav" } elseif ([IO.Path]::IsPathRooted($FixtureArgument)) { $FixtureArgument } else { Join-Path $resolvedRoot $FixtureArgument }
    $resolvedFixture = Assert-UnderPath -Path (Resolve-Path -LiteralPath $fixtureCandidate).Path -Root $resolvedBuild -Label "$Label Fixture"
    $qtDir = Get-CacheValue -CachePath $cachePath -Name "Qt6_DIR"
    $qtBin = Join-Path ([IO.Path]::GetFullPath((Join-Path $qtDir "..\..\.."))) "bin"
    $vcpkgBin = Join-Path (Get-CacheValue -CachePath $cachePath -Name "VCPKG_INSTALLED_DIR") "x64-windows/bin"
    foreach ($runtimePath in @($qtBin, $vcpkgBin)) { if (-not (Test-Path -LiteralPath $runtimePath -PathType Container)) { throw "$Label configured runtime path is unavailable: $runtimePath" } }
    $repository = Get-RepositoryIdentity -Root $resolvedRoot -Label $Label -RequireClean:$RequireCleanRepository
    return [pscustomobject]@{
        label = $Label; root = $resolvedRoot; buildRoot = $resolvedBuild; cachePath = $cachePath; appPath = $appPath; fixture = $resolvedFixture
        fixtureSha256 = (Get-FileHash -LiteralPath $resolvedFixture -Algorithm SHA256).Hash; probeSource = $ProbeSource
        probeSourceSha256 = (Get-FileHash -LiteralPath $ProbeSource -Algorithm SHA256).Hash; config = Get-ComparableBuildConfig -CachePath $cachePath; dependencies = Get-DependencyEvidence -CachePath $cachePath
        runtimePaths = @($qtBin, $vcpkgBin); repository = $repository; commit = $repository.head; appExecutableSha256 = (Get-FileHash -LiteralPath $appPath -Algorithm SHA256).Hash; appTreeBytes = Get-AppTreeBytes -BuildRoot $resolvedBuild
    }
}

function Ensure-NormalProbe {
    param([pscustomobject]$Context, [string]$InjectPath, [switch]$Inject)
    $probe = Join-Path $Context.buildRoot "tests/normal_waveform_performance_probe.exe"
    if ($Inject) {
        $sourceForCmake = $Context.probeSource.Replace('\', '/')
        @(
            'if(NOT DEFINED TASK8_NORMAL_WAVEFORM_PROBE_SOURCE)', '  message(FATAL_ERROR "TASK8_NORMAL_WAVEFORM_PROBE_SOURCE is required")', 'endif()',
            "function(task8_add_normal_waveform_performance_probe)", "  if(TARGET normal_waveform_performance_probe)", "    return()", "  endif()",
            '  add_executable(normal_waveform_performance_probe "${TASK8_NORMAL_WAVEFORM_PROBE_SOURCE}")', "  add_dependencies(normal_waveform_performance_probe decoder_fixture)",
            "  target_link_libraries(normal_waveform_performance_probe PRIVATE agplayer_core)", "  if(WIN32)", "    target_link_libraries(normal_waveform_performance_probe PRIVATE Psapi)", "  endif()",
            "  agplayer_enable_warnings(normal_waveform_performance_probe)", '  set_target_properties(normal_waveform_performance_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")',
            "endfunction()", 'cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL task8_add_normal_waveform_performance_probe)'
        ) | Set-Content -LiteralPath $InjectPath -Encoding UTF8
        & cmake -S $Context.root -B $Context.buildRoot "-DCMAKE_PROJECT_INCLUDE=$InjectPath" "-DTASK8_NORMAL_WAVEFORM_PROBE_SOURCE=$sourceForCmake"
        if ($LASTEXITCODE -ne 0) { throw "$($Context.label) probe CMake configuration failed" }
    }
    & cmake --build $Context.buildRoot --target normal_waveform_performance_probe --parallel 2
    if ($LASTEXITCODE -ne 0) { throw "$($Context.label) probe build failed" }
    if (-not (Test-Path -LiteralPath $probe -PathType Leaf)) { throw "$($Context.label) normal waveform probe is missing: $probe" }
    $Context | Add-Member -NotePropertyName probeExecutable -NotePropertyValue $probe -Force
    $Context | Add-Member -NotePropertyName probeExecutableSha256 -NotePropertyValue ((Get-FileHash -LiteralPath $probe -Algorithm SHA256).Hash) -Force
}

function Invoke-NormalProbe {
    param([pscustomobject]$Context, [ValidateSet("cold", "hot")][string]$Mode, [int]$PairIndex, [int]$OrderInPair, [int]$Iterations, [UInt64]$AffinityMask, [string]$WorkRoot)
    $prefix = Join-Path $WorkRoot ("normal-{0}-{1}-pair{2}-{3}" -f $Context.label, $Mode, $PairIndex, $OrderInPair)
    $stdout = "$prefix.stdout.log"; $stderr = "$prefix.stderr.log"; $watch = [Diagnostics.Stopwatch]::StartNew()
    $cpuBefore = Get-CpuSnapshot
    $process = Start-Process -FilePath $Context.probeExecutable -ArgumentList @($Context.fixture, "--$Mode", "--iterations", $Iterations, "--affinity-mask", $AffinityMask) -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    try {
        $process.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::High
        $priorityClass = $process.PriorityClass.ToString()
    }
    catch {
        throw "$($Context.label) $Mode probe could not acquire High priority: $($_.Exception.Message)"
    }
    if ($priorityClass -ne "High") { throw "$($Context.label) $Mode probe priority is $priorityClass, not High" }
    $process.WaitForExit()
    $cpuAfter = Get-CpuSnapshot
    $watch.Stop(); $raw = Get-Content -Raw -LiteralPath $stdout -ErrorAction SilentlyContinue
    if ($process.ExitCode -ne 0) { throw "$($Context.label) $Mode probe exited with $($process.ExitCode): $raw" }
    try { $probe = $raw.Trim() | ConvertFrom-Json } catch { throw "$($Context.label) $Mode probe did not emit one JSON object: $raw" }
    if ($probe.mode -ne $Mode -or [int]$probe.analysisIterations -ne $Iterations -or $probe.priorityClass -ne "High" -or [UInt64]$probe.affinityMask -ne $AffinityMask) { throw "$($Context.label) probe output mismatch, priority, or affinity contract failure" }
    $rawSamples = @($probe.analysisSamplesMilliseconds | ForEach-Object { [double]$_ })
    if ($rawSamples.Count -ne $Iterations -or [double]$probe.analysisElapsedMilliseconds -lt 1000.0) { throw "ENVIRONMENT_UNSUITABLE: $($Context.label) $Mode probe did not collect >=1 second of $Iterations internal samples" }
    return [pscustomobject]@{ target = $Context.label; mode = $Mode; pairIndex = $PairIndex; orderInPair = $OrderInPair; exitCode = [int]$process.ExitCode; priorityClass = $priorityClass; probePriorityClass = $probe.priorityClass; affinityMask = [UInt64]$probe.affinityMask; wallMilliseconds = [math]::Round($watch.Elapsed.TotalMilliseconds, 3); analysisIterations = [int]$probe.analysisIterations; warmupIterations = [int]$probe.warmupIterations; analysisMilliseconds = [double]$probe.analysisMilliseconds; analysisElapsedMilliseconds = [double]$probe.analysisElapsedMilliseconds; analysisSamplesMilliseconds = $rawSamples; residentBytes = [int64]$probe.residentBytes; peakResidentBytes = [int64]$probe.peakResidentBytes; cpuBeforePercent = $cpuBefore; cpuAfterPercent = $cpuAfter; stdoutLog = $stdout; stderrLog = $stderr }
}

function Get-PairedRegression {
    param([object[]]$Samples, [string]$Mode, [int]$PairCount, [int]$BootstrapSeed)
    $pairs = @()
    foreach ($pairIndex in 1..$PairCount) {
        $baseline = @($Samples | Where-Object { $_.mode -eq $Mode -and $_.pairIndex -eq $pairIndex -and $_.target -eq "baseline" })
        $candidate = @($Samples | Where-Object { $_.mode -eq $Mode -and $_.pairIndex -eq $pairIndex -and $_.target -eq "candidate" })
        if ($baseline.Count -ne 1 -or $candidate.Count -ne 1) { throw "$Mode pair $pairIndex is incomplete" }
        if ([double]$baseline[0].analysisMilliseconds -le 0.0) { throw "$Mode pair $pairIndex has an invalid baseline duration" }
        $ratio = [double]$candidate[0].analysisMilliseconds / [double]$baseline[0].analysisMilliseconds
        $pairs += [pscustomobject]@{ pairIndex = $pairIndex; baselineFirst = ($baseline[0].orderInPair -lt $candidate[0].orderInPair); ratio = $ratio; regressionPercent = ($ratio - 1.0) * 100.0 }
    }
    $medianRatio = Get-Median -Values @($pairs | ForEach-Object { [double]$_.ratio })
    $mad = Get-Median -Values @($pairs | ForEach-Object { [math]::Abs([double]$_.ratio - $medianRatio) })
    $confidenceInterval = Get-BootstrapMedianConfidenceInterval -Values @($pairs | ForEach-Object { [double]$_.ratio }) -Seed $BootstrapSeed
    return [ordered]@{ pairCount = $PairCount; pairs = $pairs; medianRatio = $medianRatio; medianRegressionPercent = ($medianRatio - 1.0) * 100.0; ratioMad = $mad; relativeMadPercent = ($mad / [math]::Abs($medianRatio)) * 100.0; medianRatioConfidenceInterval95 = $confidenceInterval }
}

function Get-NormalSummary {
    param([object[]]$Samples, [string]$Label, [int]$PairCount)
    $cold = @($Samples | Where-Object { $_.target -eq $Label -and $_.mode -eq "cold" }); $hot = @($Samples | Where-Object { $_.target -eq $Label -and $_.mode -eq "hot" })
    if ($cold.Count -ne $PairCount -or $hot.Count -ne $PairCount) { throw "$Label must have exactly $PairCount cold and hot samples" }
    return [ordered]@{
        cold = $cold; hot = $hot
        coldMedianAnalysisMilliseconds = Get-Median -Values @($cold | ForEach-Object { [double]$_.analysisMilliseconds })
        hotMedianAnalysisMilliseconds = Get-Median -Values @($hot | ForEach-Object { [double]$_.analysisMilliseconds })
        coldMedianWallMilliseconds = Get-Median -Values @($cold | ForEach-Object { [double]$_.wallMilliseconds })
        hotMedianWallMilliseconds = Get-Median -Values @($hot | ForEach-Object { [double]$_.wallMilliseconds })
    }
}

if ([string]::IsNullOrWhiteSpace($RepositoryRoot)) { $RepositoryRoot = Join-Path $PSScriptRoot ".." }
if ($ProgressDurationSeconds -le 0) { throw "ProgressDurationSeconds must be positive; the default is a real 600-second measurement." }
if ($ProbeIterations -lt 30) { throw "ProbeIterations must be >= 30" }
if ($PairCount -lt 10) { throw "PairCount must be >= 10 for the robust paired protocol" }
if (([string]::IsNullOrWhiteSpace($BaselineRepositoryRoot) -or [string]::IsNullOrWhiteSpace($BaselineBuildDirectory)) -and -not $BaselineOnly) { throw "BaselineRepositoryRoot and BaselineBuildDirectory are required for comparable candidate QA." }

$repoRoot = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$probeSource = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "../tests/stress/normal_waveform_performance_probe.cpp")).Path
$candidate = New-TargetContext -Label "candidate" -Root $repoRoot -BuildDirectoryArgument $BuildDirectory -FixtureArgument $Fixture -ProbeSource $probeSource -RequireCleanRepository
$artifactRoot = Join-Path $repoRoot "artifacts"
$outputCandidate = if ([IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory } else { Join-Path $repoRoot $OutputDirectory }
$outputRoot = Assert-UnderPath -Path $outputCandidate -Root $artifactRoot -Label "OutputDirectory"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
$metricsPath = Join-Path $outputRoot "frequency-color-metrics.json"; $summaryPath = Join-Path $outputRoot "frequency-color-summary.md"
$baselineEvidencePath = Join-Path $outputRoot "normal-waveform-baseline.json"; $baselineSummaryPath = Join-Path $outputRoot "normal-waveform-baseline.md"; $environmentEvidencePath = Join-Path $outputRoot "environment-preflight.json"
foreach ($evidence in @($metricsPath, $summaryPath, $baselineEvidencePath, $baselineSummaryPath, $environmentEvidencePath)) { if (Test-Path -LiteralPath $evidence) { throw "Refusing to overwrite existing QA evidence: $evidence" } }
$workRoot = Join-Path $outputRoot ("run-" + [Guid]::NewGuid().ToString("N")); New-Item -ItemType Directory -Force -Path $workRoot | Out-Null
$originalPath = $env:Path; $originalPlatform = $env:QT_QPA_PLATFORM; $originalScale = $env:QT_SCALE_FACTOR
try {
    if ($BaselineOnly) {
        $measurementAffinityMask = Get-MeasurementAffinityMask
        $env:Path = ($candidate.runtimePaths -join ";") + ";" + $originalPath
        $existingProbe = Join-Path $candidate.buildRoot "tests/normal_waveform_performance_probe.exe"
        if (Test-Path -LiteralPath $existingProbe -PathType Leaf) {
            Ensure-NormalProbe -Context $candidate
        } else {
            Ensure-NormalProbe -Context $candidate -InjectPath (Join-Path $workRoot "baseline-normal-waveform-probe-inject.cmake") -Inject
        }
        $singleSamples = New-Object 'System.Collections.Generic.List[object]'; foreach ($mode in @("cold", "hot")) { for ($ordinal = 1; $ordinal -le $PairCount; ++$ordinal) { $singleSamples.Add((Invoke-NormalProbe -Context $candidate -Mode $mode -PairIndex $ordinal -OrderInPair 1 -Iterations $ProbeIterations -AffinityMask $measurementAffinityMask -WorkRoot $workRoot)) } }
        $summary = Get-NormalSummary -Samples $singleSamples.ToArray() -Label "candidate" -PairCount $PairCount
        $document = [ordered]@{ schema = "agplayer.frequency-color-waveform.qa.v4"; kind = "baseline"; commit = $candidate.commit; repository = $candidate.repository; command = $MyInvocation.Line; build = $candidate.config; fixture = $candidate.fixture; fixtureSha256 = $candidate.fixtureSha256; probeSource = $candidate.probeSource; probeSourceSha256 = $candidate.probeSourceSha256; probeExecutable = $candidate.probeExecutable; probeExecutableSha256 = $candidate.probeExecutableSha256; appExecutable = $candidate.appPath; appExecutableSha256 = $candidate.appExecutableSha256; appTreeBytes = $candidate.appTreeBytes; probeIterations = $ProbeIterations; pairCount = $PairCount; measurementAffinityMask = $measurementAffinityMask; normalWaveform = $summary; result = "PASS" }
        $document | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $metricsPath -Encoding UTF8
        @("# Normal waveform baseline", "", "- Commit: $($candidate.commit)", "- Cold median: $($summary.coldMedianAnalysisMilliseconds) ms", "- Hot median: $($summary.hotMedianAnalysisMilliseconds) ms", "- Result: PASS") | Set-Content -LiteralPath $summaryPath -Encoding UTF8
        exit 0
    }
    $baseline = New-TargetContext -Label "baseline" -Root $BaselineRepositoryRoot -BuildDirectoryArgument $BaselineBuildDirectory -FixtureArgument $null -ProbeSource $probeSource
    if ($candidate.fixtureSha256 -ne $baseline.fixtureSha256) { throw "Fixture SHA-256 mismatch between baseline and candidate" }
    if ($candidate.probeSourceSha256 -ne $baseline.probeSourceSha256) { throw "Probe source SHA-256 mismatch" }
    if ($candidate.config.fingerprint -ne $baseline.config.fingerprint) { throw "Comparable CMake configuration mismatch" }
    if (($candidate.dependencies | ConvertTo-Json -Depth 6 -Compress) -ne ($baseline.dependencies | ConvertTo-Json -Depth 6 -Compress)) { throw "Comparison dependency hash mismatch" }
    if (-not [string]::IsNullOrWhiteSpace($BaselineMetrics)) { $prior = Get-Content -Raw -LiteralPath (Resolve-Path -LiteralPath $BaselineMetrics).Path | ConvertFrom-Json; if ($prior.fixtureSha256 -and $prior.fixtureSha256 -ne $baseline.fixtureSha256) { throw "Provided baseline JSON fixture hash mismatch" }; if ($prior.probeSourceSha256 -and $prior.probeSourceSha256 -ne $baseline.probeSourceSha256) { throw "Provided baseline JSON probe hash mismatch" } }
    $env:Path = (($candidate.runtimePaths + $baseline.runtimePaths | Select-Object -Unique) -join ";") + ";" + $originalPath; $env:QT_QPA_PLATFORM = "offscreen"; $env:QT_SCALE_FACTOR = "2"
    $measurementAffinityMask = Get-MeasurementAffinityMask
    Ensure-NormalProbe -Context $candidate
    Ensure-NormalProbe -Context $baseline -InjectPath (Join-Path $workRoot "f0344c9-normal-waveform-probe-inject.cmake") -Inject
    $candidateFinalIdentity = Get-RepositoryIdentity -Root $candidate.root -Label "candidate" -RequireClean
    if ($candidateFinalIdentity.head -ne $candidate.commit) { throw "Candidate HEAD changed during QA; refusing to mix identities" }
    $candidate.repository = $candidateFinalIdentity
    $samples = New-Object 'System.Collections.Generic.List[object]'
    $firstTargets = New-Object 'System.Collections.Generic.List[string]'
    for ($index = 0; $index -lt $PairCount; ++$index) { $firstTargets.Add($(if (($index % 2) -eq 0) { "baseline" } else { "candidate" })) }
    $scheduleRandom = [System.Random]::new($PairSeed)
    for ($index = $firstTargets.Count - 1; $index -gt 0; --$index) { $swap = $scheduleRandom.Next($index + 1); $value = $firstTargets[$index]; $firstTargets[$index] = $firstTargets[$swap]; $firstTargets[$swap] = $value }
    $pairOrders = New-Object 'System.Collections.Generic.List[object]'
    foreach ($firstTarget in $firstTargets) {
        if ($firstTarget -eq "baseline") { $pairOrders.Add(@("baseline", "candidate")) }
        else { $pairOrders.Add(@("candidate", "baseline")) }
    }
    foreach ($mode in @("cold", "hot")) {
        $pairIndex = 0
        foreach ($pairOrder in $pairOrders) {
            ++$pairIndex
            for ($orderInPair = 0; $orderInPair -lt 2; ++$orderInPair) {
                $label = $pairOrder[$orderInPair]
                $context = if ($label -eq "baseline") { $baseline } else { $candidate }
                $samples.Add((Invoke-NormalProbe -Context $context -Mode $mode -PairIndex $pairIndex -OrderInPair ($orderInPair + 1) -Iterations $ProbeIterations -AffinityMask $measurementAffinityMask -WorkRoot $workRoot))
            }
        }
    }
    $sampleArray = $samples.ToArray()
    $baselineNormal = Get-NormalSummary -Samples $sampleArray -Label "baseline" -PairCount $PairCount; $candidateNormal = Get-NormalSummary -Samples $sampleArray -Label "candidate" -PairCount $PairCount
    $coldPaired = Get-PairedRegression -Samples $sampleArray -Mode "cold" -PairCount $PairCount -BootstrapSeed $PairSeed; $hotPaired = Get-PairedRegression -Samples $sampleArray -Mode "hot" -PairCount $PairCount -BootstrapSeed ($PairSeed + 1)
    $coldRegression = $coldPaired.medianRegressionPercent; $hotRegression = $hotPaired.medianRegressionPercent
    [ordered]@{ schema = "agplayer.frequency-color-waveform.environment.v3"; timestamp = (Get-Date).ToString("o"); mode = "same-machine balanced randomized ABBA"; probeIterations = $ProbeIterations; pairCount = $PairCount; pairSeed = $PairSeed; measurementPriorityClass = "High"; measurementAffinityMask = $measurementAffinityMask; warmupContract = [ordered]@{ cold = 0; hot = 1 }; pairOrders = $pairOrders; samples = $sampleArray | Select-Object target, mode, pairIndex, orderInPair, priorityClass, probePriorityClass, affinityMask, warmupIterations, analysisIterations, analysisElapsedMilliseconds, analysisMilliseconds, analysisSamplesMilliseconds, cpuBeforePercent, cpuAfterPercent, stdoutLog, stderrLog; result = "RECORDED" } | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $environmentEvidencePath -Encoding UTF8
    $baselineDocument = [ordered]@{ schema = "agplayer.frequency-color-waveform.qa.v4"; kind = "baseline"; commit = $baseline.commit; repository = $baseline.repository; command = $MyInvocation.Line; measurementPriorityClass = "High"; measurementAffinityMask = $measurementAffinityMask; pairCount = $PairCount; pairSeed = $PairSeed; build = $baseline.config; dependencies = $baseline.dependencies; fixture = $baseline.fixture; fixtureSha256 = $baseline.fixtureSha256; probeSource = $baseline.probeSource; probeSourceSha256 = $baseline.probeSourceSha256; probeExecutable = $baseline.probeExecutable; probeExecutableSha256 = $baseline.probeExecutableSha256; appExecutable = $baseline.appPath; appExecutableSha256 = $baseline.appExecutableSha256; appTreeBytes = $baseline.appTreeBytes; probeIterations = $ProbeIterations; normalWaveform = $baselineNormal; paired = [ordered]@{ cold = $coldPaired; hot = $hotPaired }; result = "RECORDED" }
    $baselineDocument | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $baselineEvidencePath -Encoding UTF8
    @("# Comparable normal waveform baseline", "", "- Commit: $($baseline.commit)", "- Cold median: $($baselineNormal.coldMedianAnalysisMilliseconds) ms", "- Hot median: $($baselineNormal.hotMedianAnalysisMilliseconds) ms", "- Source SHA-256: $($baseline.probeSourceSha256)", "- Result: PASS") | Set-Content -LiteralPath $baselineSummaryPath -Encoding UTF8
    $stressTest = Join-Path $candidate.buildRoot "tests/frequency_color_waveform_stress_test.exe"; if (-not (Test-Path -LiteralPath $stressTest -PathType Leaf)) { throw "Frequency stress executable is missing: $stressTest" }; $stressTestSha256 = (Get-FileHash -LiteralPath $stressTest -Algorithm SHA256).Hash
    $driverJson = Join-Path $workRoot "driver-metrics.json"; $driverOut = Join-Path $workRoot "driver.stdout.log"; $driverErr = Join-Path $workRoot "driver.stderr.log"
    if ($ProgressDurationSeconds -gt ([int]::MaxValue / 1000)) { throw "ProgressDurationSeconds is too large" }
    $started = Get-Date; & $stressTest --measure $candidate.fixture $driverJson ($ProgressDurationSeconds * 1000) 1> $driverOut 2> $driverErr; $driverExitCode = $LASTEXITCODE; $finished = Get-Date
    if ($driverExitCode -ne 0 -or -not (Test-Path -LiteralPath $driverJson -PathType Leaf)) { $detail = @((Get-Content -Raw -LiteralPath $driverOut -ErrorAction SilentlyContinue), (Get-Content -Raw -LiteralPath $driverErr -ErrorAction SilentlyContinue)) -join "`n"; throw "Frequency measurement driver failed with ${driverExitCode}:`n$detail" }
    $driver = Get-Content -Raw -LiteralPath $driverJson | ConvertFrom-Json; if ($driver.measurementPriorityClass -ne "High") { throw "Frequency measurement driver did not record High priority" }; $elapsedSeconds = [math]::Max(0.001, [double]$driver.progressDurationMilliseconds / 1000.0); $driverWallElapsedSeconds = [math]::Max(0.001, ($finished - $started).TotalSeconds); $processCpuSeconds = [double]$driver.progressProcessCpuMilliseconds / 1000.0; $cpuPercent = ($processCpuSeconds / $elapsedSeconds / [math]::Max(1, [Environment]::ProcessorCount)) * 100.0
    $ctestLog = Join-Path $workRoot "focused-ctest.log"; & ctest --test-dir $candidate.buildRoot -R '^(decoder_timeline_contract_test|frequency_color_waveform_stress_test|normal_waveform_performance_probe|waveform_item_test|waveform_provider_test)$' --output-on-failure *>&1 | Tee-Object -FilePath $ctestLog; if ($LASTEXITCODE -ne 0) { throw "Focused CTest gate failed; see $ctestLog" }
    $sizeDelta = [int64]$candidate.appTreeBytes - [int64]$baseline.appTreeBytes; $cacheSizes = @($driver.cacheBytes | ForEach-Object { [int64]$_ }); $openCounts = @($driver.decoderOpenCounts | ForEach-Object { [int]$_ }); $memoryGrowth = [int64]$driver.residentAfterProgressBytes - [int64]$driver.residentAfterWarmupBytes
    $failures = @(); if ($coldRegression -gt 3.0) { $failures += "normal waveform cold paired median regressed by more than 3%" }; if ($hotRegression -gt 3.0) { $failures += "normal waveform hot paired median regressed by more than 3%" }; if ($coldPaired.relativeMadPercent -gt 3.0) { $failures += "normal waveform cold paired ratio MAD/median exceeds 3%" }; if ($hotPaired.relativeMadPercent -gt 3.0) { $failures += "normal waveform hot paired ratio MAD/median exceeds 3%" }; if (($cacheSizes | Where-Object { $_ -ge 8192 }).Count -ne 0) { $failures += "FCW cache >= 8192 bytes" }; if ($sizeDelta -gt 1MB) { $failures += "app tree grew by more than 1 MiB" }; if (($openCounts | Where-Object { $_ -ne 1 }).Count -ne 0) { $failures += "analyzer decoder opens != 1" }; if ($memoryGrowth -gt 8MB) { $failures += "10-minute progress resident growth > 8 MiB" }
    $environmentUnsuitable = $coldPaired.relativeMadPercent -gt 3.0 -or $hotPaired.relativeMadPercent -gt 3.0
    $result = if ($environmentUnsuitable) { "ENVIRONMENT_UNSUITABLE" } elseif ($failures.Count -eq 0) { "PASS" } else { "FAIL" }
    $metrics = [ordered]@{ schema = "agplayer.frequency-color-waveform.qa.v4"; kind = "candidate"; commit = $candidate.commit; repository = $candidate.repository; command = $MyInvocation.Line; measurementPriorityClass = "High"; measurementAffinityMask = $measurementAffinityMask; pairCount = $PairCount; pairSeed = $PairSeed; warmupContract = [ordered]@{ cold = 0; hot = 1 }; candidate = [ordered]@{ build = $candidate.config; dependencies = $candidate.dependencies; fixture = $candidate.fixture; fixtureSha256 = $candidate.fixtureSha256; probeSource = $candidate.probeSource; probeSourceSha256 = $candidate.probeSourceSha256; probeExecutable = $candidate.probeExecutable; probeExecutableSha256 = $candidate.probeExecutableSha256; appExecutable = $candidate.appPath; appExecutableSha256 = $candidate.appExecutableSha256; frequencyStressExecutable = $stressTest; frequencyStressExecutableSha256 = $stressTestSha256; appTreeBytes = $candidate.appTreeBytes; normalWaveform = $candidateNormal }; baseline = [ordered]@{ path = $baselineEvidencePath; commit = $baseline.commit; repository = $baseline.repository; build = $baseline.config; dependencies = $baseline.dependencies; fixtureSha256 = $baseline.fixtureSha256; probeSourceSha256 = $baseline.probeSourceSha256; appExecutableSha256 = $baseline.appExecutableSha256; appTreeBytes = $baseline.appTreeBytes; normalWaveform = $baselineNormal }; probeIterations = $ProbeIterations; cpuEvidencePath = $environmentEvidencePath; normalWaveformColdPaired = $coldPaired; normalWaveformHotPaired = $hotPaired; normalWaveformColdMedianRegressionPercent = $coldRegression; normalWaveformHotMedianRegressionPercent = $hotRegression; appTreeDeltaBytes = $sizeDelta; measurement = [ordered]@{ priorityClass = $driver.measurementPriorityClass; progressRequestedSeconds = $ProgressDurationSeconds; progressElapsedSeconds = $elapsedSeconds; measurementDriverWallElapsedSeconds = $driverWallElapsedSeconds; progressCadenceHz = [int]$driver.progressCadenceHz; progressProcessCpuSeconds = $processCpuSeconds; progressCpuPercent = $cpuPercent; coldAnalysisMilliseconds = @($driver.coldAnalysisMilliseconds); cacheHitMilliseconds = @($driver.cacheHitMilliseconds); cacheBytes = $cacheSizes; decoderOpenCounts = $openCounts; progressUpdates = [int64]$driver.progressUpdates; progressRequestedUpdates = [int64]$driver.progressRequestedUpdates; progressGeometryRevision = [int64]$driver.progressGeometryRevision; progressMaterialRevision = [int64]$driver.progressMaterialRevision; residentAfterWarmupBytes = [int64]$driver.residentAfterWarmupBytes; residentPeakBytes = [int64]$driver.residentPeakBytes; residentAfterProgressBytes = [int64]$driver.residentAfterProgressBytes; residentGrowthBytes = $memoryGrowth; focusedCTestLog = $ctestLog }; result = $result; failures = $failures }
    $metrics | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath $metricsPath -Encoding UTF8
    @("# Frequency Color Waveform QA", "", "- Commit: $($candidate.commit)", "- Baseline commit: $($baseline.commit)", "- QA process priority: High; affinity mask: $measurementAffinityMask", "- Balanced randomized ABBA pairs: $PairCount (seed $PairSeed)", "- Normal cold paired median regression: $([math]::Round($coldRegression, 3))%; ratio relative MAD: $([math]::Round($coldPaired.relativeMadPercent, 3))%; bootstrap 95% CI: $([math]::Round((($coldPaired.medianRatioConfidenceInterval95.lower95 - 1.0) * 100.0), 3))%..$([math]::Round((($coldPaired.medianRatioConfidenceInterval95.upper95 - 1.0) * 100.0), 3))%", "- Normal hot paired median regression: $([math]::Round($hotRegression, 3))%; ratio relative MAD: $([math]::Round($hotPaired.relativeMadPercent, 3))%; bootstrap 95% CI: $([math]::Round((($hotPaired.medianRatioConfidenceInterval95.lower95 - 1.0) * 100.0), 3))%..$([math]::Round((($hotPaired.medianRatioConfidenceInterval95.upper95 - 1.0) * 100.0), 3))%", "- Probe iterations per process: $ProbeIterations (internal elapsed >= 1 s required)", "- FCW bytes: $($cacheSizes -join ', ')", "- Progress: $([math]::Round($elapsedSeconds, 3)) s at $($driver.progressCadenceHz) Hz, $($driver.progressUpdates) updates", "- Resident growth: $memoryGrowth bytes", "- App-tree delta: $sizeDelta bytes", "- Focused CTest: $ctestLog", "- Result: $($metrics.result)") | Set-Content -LiteralPath $summaryPath -Encoding UTF8
    if ($failures.Count -ne 0) { exit 1 }
}
finally { $env:Path = $originalPath; $env:QT_QPA_PLATFORM = $originalPlatform; $env:QT_SCALE_FACTOR = $originalScale }
