param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'
$buildScript = Join-Path $SourceRoot 'scripts/voice-clone/build-runtime-pack.ps1'
if (-not (Test-Path -LiteralPath $buildScript -PathType Leaf)) {
    throw "Runtime Pack builder is missing: $buildScript"
}

function Assert-FullHash([string]$Value, [string]$Label) {
    if ($Value -notmatch '^[0-9a-f]{64}$') {
        throw "$Label must contain a real SHA-256"
    }
}

$runtimeDirectory = Join-Path $SourceRoot 'plugins/voice-clone/runtime'
foreach ($runtimeId in @('qwen', 'indextts25', 'cosyvoice3')) {
    $lockPath = Join-Path $runtimeDirectory "$runtimeId.lock.json"
    if (-not (Test-Path -LiteralPath $lockPath -PathType Leaf)) {
        throw "Runtime lock is missing: $lockPath"
    }
    $lock = Get-Content -Raw -LiteralPath $lockPath | ConvertFrom-Json
    if ($lock.ready -ne $true -or $lock.unresolved.Count -ne 0) {
        throw "$runtimeId lock is not a resolved, reproducible recipe"
    }
    if ($lock.artifacts.Count -lt 4) {
        throw "$runtimeId lock does not enumerate its build inputs"
    }
    if ($lock.source.repository -notmatch '^https://github\.com/' -or
        $lock.source.revision -notmatch '^[0-9a-f]{40}$' -or
        $lock.source.tree -notmatch '^[0-9a-f]{40}$') {
        throw "$runtimeId source is not pinned to an official Git tree"
    }
    Assert-FullHash $lock.python.archive.sha256 "$runtimeId Python archive"
    Assert-FullHash $lock.resolver.archive.sha256 "$runtimeId resolver archive"
    Assert-FullHash $lock.requirements.sha256 "$runtimeId requirements lock"
    if ($lock.python.archive.url -notmatch '^https://www\.python\.org/' -or
        $lock.resolver.archive.url -notmatch '^https://github\.com/astral-sh/uv/') {
        throw "$runtimeId runtime artifacts do not use official download URLs"
    }
    $requirementsPath = Join-Path $runtimeDirectory $lock.requirements.path
    if (-not (Test-Path -LiteralPath $requirementsPath -PathType Leaf)) {
        throw "$runtimeId requirements lock is missing"
    }
    $requirementsHash = (Get-FileHash -LiteralPath $requirementsPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($requirementsHash -ne $lock.requirements.sha256) {
        throw "$runtimeId requirements lock hash is stale"
    }

    $emptyCache = Join-Path ([System.IO.Path]::GetTempPath()) ("agplayer-empty-cache-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $emptyCache | Out-Null
    $failedOutput = Join-Path ([System.IO.Path]::GetTempPath()) ("agplayer-unexpected-runtime-" + [guid]::NewGuid().ToString('N'))
    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = 'SilentlyContinue'
    & powershell -NoProfile -ExecutionPolicy Bypass -File $buildScript `
        -LockFile $lockPath -OfficialCacheRoot $emptyCache -OutputRoot $failedOutput *> $null
    $missingCacheExitCode = $LASTEXITCODE
    $ErrorActionPreference = $oldPreference
    if ($missingCacheExitCode -eq 0 -or (Test-Path -LiteralPath $failedOutput)) {
        throw "$runtimeId recipe did not fail closed when the official cache was incomplete"
    }
    Remove-Item -LiteralPath $emptyCache -Recurse -Force
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("agplayer-runtime-contract-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempRoot | Out-Null
try {
    $cacheRoot = Join-Path $tempRoot 'official-cache'
    $downloads = Join-Path $cacheRoot 'downloads'
    $wheelhouse = Join-Path $cacheRoot 'wheelhouse'
    $sourceCache = Join-Path $cacheRoot 'sources/contract-runtime'
    New-Item -ItemType Directory -Path $downloads,$wheelhouse,$sourceCache -Force | Out-Null

    $managedPython = (& uv python find 3.12).Trim()
    if (-not (Test-Path -LiteralPath $managedPython -PathType Leaf) -or
        $managedPython -like '*WindowsApps*') {
        throw 'A real bundled or uv-managed Python 3.12 is required for the package-layout contract'
    }
    $pythonSeed = Join-Path $tempRoot 'python-seed'
    New-Item -ItemType Directory -Path $pythonSeed | Out-Null
    Get-ChildItem -LiteralPath (Split-Path -Parent $managedPython) -File | Copy-Item -Destination $pythonSeed
    $pythonArchive = Join-Path $downloads 'python-contract.zip'
    Compress-Archive -Path (Join-Path $pythonSeed '*') -DestinationPath $pythonArchive

    $uvExecutable = (Get-Command uv -ErrorAction Stop).Source
    $uvSeed = Join-Path $tempRoot 'uv-seed'
    New-Item -ItemType Directory -Path $uvSeed | Out-Null
    Copy-Item -LiteralPath $uvExecutable -Destination (Join-Path $uvSeed 'uv.exe')
    $uvArchive = Join-Path $downloads 'uv-contract.zip'
    Compress-Archive -Path (Join-Path $uvSeed '*') -DestinationPath $uvArchive

    Set-Content -LiteralPath (Join-Path $sourceCache 'LICENSE') -Value 'contract license' -NoNewline
    Set-Content -LiteralPath (Join-Path $sourceCache 'contract.py') -Value 'VALUE = 1' -NoNewline
    git -C $sourceCache init --quiet
    git -C $sourceCache config user.email 'contract@agplayer.invalid'
    git -C $sourceCache config user.name 'AG Player Contract'
    git -C $sourceCache remote add origin 'https://example.invalid/contract-runtime.git'
    git -C $sourceCache add LICENSE contract.py
    git -C $sourceCache commit --quiet -m 'contract source'
    $sourceRevision = (git -C $sourceCache rev-parse HEAD).Trim()
    $sourceTree = (git -C $sourceCache rev-parse 'HEAD^{tree}').Trim()

    $requirementsPath = Join-Path $tempRoot 'contract.requirements.txt'
    [System.IO.File]::WriteAllText($requirementsPath, '', [System.Text.UTF8Encoding]::new($false))
    $readyLock = [ordered]@{
        schemaVersion = 1
        runtimeId = 'contract-runtime'
        ready = $true
        python = [ordered]@{
            version = '3.12.9'
            archive = [ordered]@{
                cachePath = 'downloads/python-contract.zip'
                url = 'https://www.python.org/ftp/python/3.12.9/python-3.12.9-embed-amd64.zip'
                sha256 = (Get-FileHash -LiteralPath $pythonArchive -Algorithm SHA256).Hash.ToLowerInvariant()
            }
        }
        resolver = [ordered]@{
            version = 'contract'
            archive = [ordered]@{
                cachePath = 'downloads/uv-contract.zip'
                url = 'https://github.com/astral-sh/uv/releases/download/contract/uv.zip'
                sha256 = (Get-FileHash -LiteralPath $uvArchive -Algorithm SHA256).Hash.ToLowerInvariant()
            }
        }
        source = [ordered]@{
            repository = 'https://example.invalid/contract-runtime.git'
            revision = $sourceRevision
            tree = $sourceTree
            cachePath = 'sources/contract-runtime'
            installPath = 'source/contract-runtime'
            pythonPath = 'source/contract-runtime'
        }
        requirements = [ordered]@{
            path = 'contract.requirements.txt'
            sha256 = (Get-FileHash -LiteralPath $requirementsPath -Algorithm SHA256).Hash.ToLowerInvariant()
        }
        artifacts = @(
            [ordered]@{ kind = 'python-archive'; cachePath = 'downloads/python-contract.zip'; sha256 = (Get-FileHash $pythonArchive -Algorithm SHA256).Hash.ToLowerInvariant() },
            [ordered]@{ kind = 'resolver-archive'; cachePath = 'downloads/uv-contract.zip'; sha256 = (Get-FileHash $uvArchive -Algorithm SHA256).Hash.ToLowerInvariant() },
            [ordered]@{ kind = 'dependency-lock'; path = 'contract.requirements.txt'; sha256 = (Get-FileHash $requirementsPath -Algorithm SHA256).Hash.ToLowerInvariant() },
            [ordered]@{ kind = 'source-tree'; cachePath = 'sources/contract-runtime'; revision = $sourceRevision; tree = $sourceTree }
        )
        models = @()
        notices = @(
            [ordered]@{
                name = 'Contract Runtime'
                license = 'MIT'
                sourceUrl = 'https://example.invalid/contract-runtime.git'
                sourceLicensePath = 'LICENSE'
            }
        )
        unresolved = @()
    }
    $readyLockPath = Join-Path $tempRoot 'contract.lock.json'
    $readyLock | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $readyLockPath -Encoding utf8

    Push-Location $tempRoot
    try {
        $oldPreference = $ErrorActionPreference
        $ErrorActionPreference = 'SilentlyContinue'
        & powershell -NoProfile -ExecutionPolicy Bypass -File $buildScript `
            -LockFile $readyLockPath -OfficialCacheRoot $cacheRoot -OutputRoot 'relative-output' *> $null
        $relativeExitCode = $LASTEXITCODE
        $ErrorActionPreference = $oldPreference
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
    foreach ($required in @('python.exe', 'runtime-manifest.json', 'THIRD_PARTY_NOTICES.txt')) {
        if (-not (Test-Path -LiteralPath (Join-Path $outputRoot $required) -PathType Leaf)) {
            throw "Runtime Pack output is missing $required"
        }
    }
    $pythonVersion = & (Join-Path $outputRoot 'python.exe') --version 2>&1
    if ($LASTEXITCODE -ne 0 -or $pythonVersion -notmatch '^Python 3\.12\.') {
        throw "Packaged root python.exe is not executable: $pythonVersion"
    }
    $manifest = Get-Content -Raw -LiteralPath (Join-Path $outputRoot 'runtime-manifest.json') | ConvertFrom-Json
    if ($manifest.runtimeId -ne 'contract-runtime' -or $manifest.artifacts.Count -ne 4) {
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
