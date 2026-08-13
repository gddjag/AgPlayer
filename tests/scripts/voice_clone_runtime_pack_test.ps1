param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'
$buildScript = Join-Path $SourceRoot 'scripts/voice-clone/build-runtime-pack.ps1'
if (-not (Test-Path -LiteralPath $buildScript -PathType Leaf)) {
    throw "Runtime Pack builder is missing: $buildScript"
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("agplayer-runtime-contract-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempRoot | Out-Null
try {
    foreach ($runtimeId in @('qwen', 'indextts25', 'cosyvoice3')) {
        $lockPath = Join-Path $SourceRoot "plugins/voice-clone/runtime/$runtimeId.lock.json"
        if (-not (Test-Path -LiteralPath $lockPath -PathType Leaf)) {
            throw "Runtime lock is missing: $lockPath"
        }
        $lock = Get-Content -Raw -LiteralPath $lockPath | ConvertFrom-Json
        if ($lock.ready -ne $false -or $lock.unresolved.Count -lt 1) {
            throw "$runtimeId lock must remain fail-closed until every artifact hash is resolved"
        }
        $cache = Join-Path $tempRoot "$runtimeId-cache"
        New-Item -ItemType Directory -Path $cache | Out-Null
        $output = Join-Path $tempRoot "$runtimeId-output"
        $previousPreference = $ErrorActionPreference
        $ErrorActionPreference = 'SilentlyContinue'
        & powershell -NoProfile -ExecutionPolicy Bypass -File $buildScript `
            -LockFile $lockPath -OfficialCacheRoot $cache -OutputRoot $output *> $null
        $buildExitCode = $LASTEXITCODE
        $ErrorActionPreference = $previousPreference
        if ($buildExitCode -eq 0) {
            throw "$runtimeId unresolved lock unexpectedly produced a Runtime Pack"
        }
    }

    $cacheRoot = Join-Path $tempRoot 'ready-cache'
    New-Item -ItemType Directory -Path (Join-Path $cacheRoot 'python') -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $cacheRoot 'source') -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $cacheRoot 'python/python.exe') -Value 'runtime' -NoNewline
    Set-Content -LiteralPath (Join-Path $cacheRoot 'source/LICENSE') -Value 'contract license' -NoNewline
    $runtimeHash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $cacheRoot 'python/python.exe')).Hash.ToLowerInvariant()
    $licenseHash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $cacheRoot 'source/LICENSE')).Hash.ToLowerInvariant()
    $readyLock = [ordered]@{
        schemaVersion = 1
        runtimeId = 'contract-runtime'
        ready = $true
        python = [ordered]@{ version = '3.12.9' }
        source = [ordered]@{
            repository = 'https://github.com/example/official-runtime'
            revision = '0123456789abcdef0123456789abcdef01234567'
        }
        artifacts = @(
            [ordered]@{ path = 'python/python.exe'; sha256 = $runtimeHash; kind = 'runtime' },
            [ordered]@{ path = 'source/LICENSE'; sha256 = $licenseHash; kind = 'license' }
        )
        notices = @(
            [ordered]@{
                name = 'Contract Runtime'
                license = 'MIT'
                sourceUrl = 'https://github.com/example/official-runtime'
                licenseFile = 'source/LICENSE'
            }
        )
        unresolved = @()
    }
    $readyLockPath = Join-Path $tempRoot 'ready.lock.json'
    $readyLock | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $readyLockPath -Encoding utf8
    Push-Location $tempRoot
    try {
        $previousPreference = $ErrorActionPreference
        $ErrorActionPreference = 'SilentlyContinue'
        & powershell -NoProfile -ExecutionPolicy Bypass -File $buildScript `
            -LockFile $readyLockPath -OfficialCacheRoot $cacheRoot -OutputRoot 'relative-output' *> $null
        $relativeExitCode = $LASTEXITCODE
        $ErrorActionPreference = $previousPreference
        if ($relativeExitCode -eq 0) {
            throw 'Runtime Pack builder accepted a relative OutputRoot'
        }
    }
    finally {
        Pop-Location
    }
    $outputRoot = Join-Path $tempRoot 'ready-output'
    & powershell -NoProfile -ExecutionPolicy Bypass -File $buildScript `
        -LockFile $readyLockPath -OfficialCacheRoot $cacheRoot -OutputRoot $outputRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Ready Runtime Pack build failed with exit code $LASTEXITCODE"
    }
    foreach ($required in @('python/python.exe', 'runtime-manifest.json', 'THIRD_PARTY_NOTICES.txt')) {
        if (-not (Test-Path -LiteralPath (Join-Path $outputRoot $required) -PathType Leaf)) {
            throw "Runtime Pack output is missing $required"
        }
    }
    $manifest = Get-Content -Raw -LiteralPath (Join-Path $outputRoot 'runtime-manifest.json') | ConvertFrom-Json
    if ($manifest.runtimeId -ne 'contract-runtime' -or $manifest.artifacts.Count -ne 2) {
        throw 'Runtime Pack manifest does not preserve the verified lock identity'
    }
    $notices = Get-Content -Raw -LiteralPath (Join-Path $outputRoot 'THIRD_PARTY_NOTICES.txt')
    if ($notices -notmatch 'Contract Runtime' -or $notices -notmatch 'contract license') {
        throw 'Runtime Pack notices are incomplete'
    }
}
finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}

Write-Output 'voice clone Runtime Pack contracts passed'
