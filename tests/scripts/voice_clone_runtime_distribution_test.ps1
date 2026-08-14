param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'
$packageScript = Join-Path $SourceRoot 'scripts/voice-clone/package-runtime.ps1'
$verifyScript = Join-Path $SourceRoot 'scripts/voice-clone/verify-runtime-package.ps1'
$preflightScript = Join-Path $SourceRoot 'scripts/voice-clone/fetch-runtime-cache.ps1'
foreach ($script in @($packageScript,$verifyScript,$preflightScript)) {
    if (-not (Test-Path -LiteralPath $script -PathType Leaf)) { throw "Missing script: $script" }
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
    'agplayer-runtime-distribution-' + [guid]::NewGuid().ToString('N'))
$runtimeRoot = Join-Path $tempRoot 'runtime'
$outputRoot = Join-Path $tempRoot 'output'
$cacheRoot = Join-Path $tempRoot 'empty-cache'
New-Item -ItemType Directory -Path $runtimeRoot,$outputRoot,$cacheRoot -Force | Out-Null
try {
    $lock = Get-Content -Raw -LiteralPath (
        Join-Path $SourceRoot 'plugins/voice-clone/runtime/qwen.lock.json') | ConvertFrom-Json
    [System.IO.File]::WriteAllBytes((Join-Path $runtimeRoot 'python.exe'),
                                    [System.Text.Encoding]::UTF8.GetBytes('test-python'))
    [System.IO.File]::WriteAllText((Join-Path $runtimeRoot 'THIRD_PARTY_NOTICES.txt'),
                                   'test notices', [System.Text.UTF8Encoding]::new($false))
    $buildManifest = [ordered]@{
        schemaVersion = 1
        runtimeId = $lock.runtimeId
        source = [ordered]@{ repository = $lock.source.repository; revision = $lock.source.revision; tree = $lock.source.tree }
        requirements = [ordered]@{ path = $lock.requirements.path; sha256 = $lock.requirements.sha256 }
    }
    [System.IO.File]::WriteAllText((Join-Path $runtimeRoot 'runtime-manifest.json'),
        ($buildManifest | ConvertTo-Json -Depth 6), [System.Text.UTF8Encoding]::new($false))

    & powershell -NoProfile -ExecutionPolicy Bypass -File $packageScript `
        -Runtime qwen -RuntimeRoot $runtimeRoot -OutputDirectory $outputRoot -Version 1.0.0
    if ($LASTEXITCODE -ne 0) { throw "Runtime package script failed: $LASTEXITCODE" }
    $archive = Join-Path $outputRoot 'runtime-qwen-1.0.0-windows-x86_64.zip'
    $manifest = Join-Path $outputRoot 'runtime-qwen-1.0.0-windows-x86_64.manifest.json'
    $publishedManifest = Get-Content -Raw -LiteralPath $manifest | ConvertFrom-Json
    $declaredBytes = [int64](($publishedManifest.files | Measure-Object -Property bytes -Sum).Sum)
    if ($publishedManifest.installedBytes -ne $declaredBytes -or
        $publishedManifest.entryCount -ne $publishedManifest.files.Count) {
        throw 'Runtime package script omitted the exact installed size/entry count contract'
    }
    & powershell -NoProfile -ExecutionPolicy Bypass -File $verifyScript `
        -ManifestPath $manifest -ArchivePath $archive
    if ($LASTEXITCODE -ne 0) { throw "Runtime verify script failed: $LASTEXITCODE" }

    $preflightSource = Get-Content -Raw -LiteralPath $preflightScript
    if ($preflightSource -notmatch "'win_amd64'" -or
        $preflightSource -notmatch "'abi3'" -or
        $preflightSource -notmatch 'Test-CompatibleWheel') {
        throw 'Runtime cache preflight does not enforce Windows/Python wheel compatibility'
    }

    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = 'SilentlyContinue'
    & powershell -NoProfile -ExecutionPolicy Bypass -File $packageScript `
        -Runtime qwen -RuntimeRoot $runtimeRoot -OutputDirectory $outputRoot -Version 2.0.0 `
        -PublishBaseUrl 'https://downloads.agplayer.cn/runtime/' *> $null
    $releaseExit = $LASTEXITCODE
    $ErrorActionPreference = $oldPreference
    if ($releaseExit -eq 0) { throw 'Runtime package script fabricated a release without signing' }

    [System.IO.File]::AppendAllText($archive, 'tamper')
    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = 'SilentlyContinue'
    & powershell -NoProfile -ExecutionPolicy Bypass -File $verifyScript `
        -ManifestPath $manifest -ArchivePath $archive *> $null
    $tamperExit = $LASTEXITCODE
    $ErrorActionPreference = $oldPreference
    if ($tamperExit -eq 0) { throw 'Runtime verifier accepted a tampered ZIP' }

    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = 'SilentlyContinue'
    $preflight = & powershell -NoProfile -ExecutionPolicy Bypass -File $preflightScript `
        -Runtime qwen -OfficialCacheRoot $cacheRoot 2>&1
    $preflightExit = $LASTEXITCODE
    $ErrorActionPreference = $oldPreference
    $preflightText = ($preflight | Out-String)
    if ($preflightExit -ne 2 -or $preflightText -notmatch 'RUNTIME_CACHE_INCOMPLETE:qwen-shared' -or
        $preflightText -notmatch 'MISSING python-archive:' -or
        $preflightText -notmatch 'MISSING source:' -or
        $preflightText -notmatch 'MISSING wheel:') {
        throw "Runtime preflight did not report exact missing categories: $preflightText"
    }
}
finally {
    if (Test-Path -LiteralPath $tempRoot) {
        $item = Get-Item -LiteralPath $tempRoot -Force
        $tempPrefix = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd('\') + '\'
        if (-not $item.FullName.StartsWith($tempPrefix, [System.StringComparison]::OrdinalIgnoreCase) -or
            ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
            throw 'Runtime distribution test cleanup escaped the temporary root'
        }
        Remove-Item -LiteralPath $item.FullName -Recurse -Force
    }
}

Write-Output 'voice clone Runtime distribution contracts passed'
