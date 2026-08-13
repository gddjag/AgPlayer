param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
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
    if ([string]::IsNullOrWhiteSpace($lock.source.importProbe.module) -or
        [string]::IsNullOrWhiteSpace($lock.source.importProbe.expectedRoot)) {
        throw "$runtimeId source import probe is missing"
    }
    if ($runtimeId -eq 'qwen') {
        if ($lock.source.pythonPath -ne 'source/Qwen3-TTS' -or
            $lock.source.importProbe.module -ne 'qwen_tts') {
            throw 'Qwen runtime does not import the locked official Git tree'
        }
        if (Select-String -LiteralPath $requirementsPath -Pattern '^qwen-tts==' -Quiet) {
            throw 'Qwen runtime must not shadow its locked Git source with a PyPI wheel'
        }
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
    Get-ChildItem -LiteralPath (Split-Path -Parent $managedPython) | Copy-Item -Destination $pythonSeed -Recurse
    $externallyManaged = Join-Path $pythonSeed 'Lib/EXTERNALLY-MANAGED'
    if (Test-Path -LiteralPath $externallyManaged -PathType Leaf) {
        Remove-Item -LiteralPath $externallyManaged -Force
    }
    $pythonArchive = Join-Path $downloads 'python-contract.zip'
    [System.IO.Compression.ZipFile]::CreateFromDirectory(
        $pythonSeed, $pythonArchive, [System.IO.Compression.CompressionLevel]::NoCompression, $false)

    $uvExecutable = (Get-Command uv -ErrorAction Stop).Source
    $uvSeed = Join-Path $tempRoot 'uv-seed'
    New-Item -ItemType Directory -Path $uvSeed | Out-Null
    Copy-Item -LiteralPath $uvExecutable -Destination (Join-Path $uvSeed 'uv.exe')
    $uvArchive = Join-Path $downloads 'uv-contract.zip'
    Compress-Archive -Path (Join-Path $uvSeed '*') -DestinationPath $uvArchive

    Set-Content -LiteralPath (Join-Path $sourceCache 'LICENSE') -Value 'contract license' -NoNewline
    New-Item -ItemType Directory -Path (Join-Path $sourceCache 'contract_runtime') | Out-Null
    Set-Content -LiteralPath (Join-Path $sourceCache 'contract_runtime/__init__.py') -Value 'VALUE = 1' -NoNewline
    git -C $sourceCache init --quiet
    git -C $sourceCache config user.email 'contract@agplayer.invalid'
    git -C $sourceCache config user.name 'AG Player Contract'
    git -C $sourceCache remote add origin 'https://example.invalid/contract-runtime.git'
    git -C $sourceCache add LICENSE contract_runtime/__init__.py
    git -C $sourceCache commit --quiet -m 'contract source'
    $sourceRevision = (git -C $sourceCache rev-parse HEAD).Trim()
    $sourceTree = (git -C $sourceCache rev-parse 'HEAD^{tree}').Trim()

    $wheelSeed = Join-Path $tempRoot 'wheel-seed'
    New-Item -ItemType Directory -Path (Join-Path $wheelSeed 'contractdep'),(Join-Path $wheelSeed 'contractdep-1.0.0.dist-info/licenses') -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $wheelSeed 'contractdep/__init__.py') -Value '__version__ = "1.0.0"' -NoNewline
    Set-Content -LiteralPath (Join-Path $wheelSeed 'contractdep-1.0.0.dist-info/METADATA') -Value "Metadata-Version: 2.1`nName: contractdep`nVersion: 1.0.0`nLicense: MIT`n"
    Set-Content -LiteralPath (Join-Path $wheelSeed 'contractdep-1.0.0.dist-info/WHEEL') -Value "Wheel-Version: 1.0`nGenerator: AG Player contract`nRoot-Is-Purelib: true`nTag: py3-none-any`n"
    Set-Content -LiteralPath (Join-Path $wheelSeed 'contractdep-1.0.0.dist-info/licenses/LICENSE') -Value 'contract dependency license' -NoNewline
    Set-Content -LiteralPath (Join-Path $wheelSeed 'contractdep-1.0.0.dist-info/RECORD') -Value '' -NoNewline
    $wheelZip = Join-Path $tempRoot 'contractdep.zip'
    Compress-Archive -Path (Join-Path $wheelSeed '*') -DestinationPath $wheelZip
    $wheelPath = Join-Path $wheelhouse 'contractdep-1.0.0-py3-none-any.whl'
    Move-Item -LiteralPath $wheelZip -Destination $wheelPath
    $wheelHash = (Get-FileHash -LiteralPath $wheelPath -Algorithm SHA256).Hash.ToLowerInvariant()

    $requirementsPath = Join-Path $tempRoot 'contract.requirements.txt'
    [System.IO.File]::WriteAllText($requirementsPath, "contractdep==1.0.0 --hash=sha256:$wheelHash`n", [System.Text.UTF8Encoding]::new($false))
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
            importProbe = [ordered]@{
                module = 'contract_runtime'
                expectedRoot = 'source/contract-runtime'
            }
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
    if ($manifest.runtimeId -ne 'contract-runtime' -or $manifest.artifacts.Count -ne 4 -or
        $manifest.wheels.Count -ne 1 -or $manifest.wheels[0].file -ne 'contractdep-1.0.0-py3-none-any.whl' -or
        $manifest.wheels[0].sha256 -ne $wheelHash) {
        throw "Runtime Pack manifest mismatch: id=$($manifest.runtimeId) artifacts=$($manifest.artifacts.Count) wheels=$($manifest.wheels.Count) file=$($manifest.wheels[0].file) hash=$($manifest.wheels[0].sha256) expected=$wheelHash"
    }
    $sourceOrigin = & (Join-Path $outputRoot 'python.exe') -I -s -c `
        'import contract_runtime; print(contract_runtime.__file__)' 2>&1
    if ($LASTEXITCODE -ne 0 -or $sourceOrigin -notmatch 'source[\\/]contract-runtime[\\/]contract_runtime') {
        throw "Packaged source module did not resolve from the locked Git tree: $sourceOrigin"
    }
    $notices = Get-Content -Raw -LiteralPath (Join-Path $outputRoot 'THIRD_PARTY_NOTICES.txt')
    if ($notices -notmatch 'Contract Runtime' -or $notices -notmatch 'contract license') {
        throw 'Runtime Pack notices are incomplete'
    }

    Copy-Item -LiteralPath $wheelPath -Destination (Join-Path $wheelhouse 'contractdep-1.0.0-1-py3-none-any.whl')
    $duplicateOutput = Join-Path $tempRoot 'duplicate-output'
    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = 'SilentlyContinue'
    & powershell -NoProfile -ExecutionPolicy Bypass -File $buildScript `
        -LockFile $readyLockPath -OfficialCacheRoot $cacheRoot -OutputRoot $duplicateOutput *> $null
    $duplicateExit = $LASTEXITCODE
    $ErrorActionPreference = $oldPreference
    if ($duplicateExit -eq 0 -or (Test-Path -LiteralPath $duplicateOutput)) {
        throw 'Runtime Pack builder accepted multiple wheel candidates for one distribution/version/platform'
    }
    Remove-Item -LiteralPath (Join-Path $wheelhouse 'contractdep-1.0.0-1-py3-none-any.whl') -Force

    Set-Content -LiteralPath (Join-Path $wheelhouse 'contractdep-1.0.0.tar.gz') -Value 'sdist' -NoNewline
    $sdistOutput = Join-Path $tempRoot 'sdist-output'
    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = 'SilentlyContinue'
    & powershell -NoProfile -ExecutionPolicy Bypass -File $buildScript `
        -LockFile $readyLockPath -OfficialCacheRoot $cacheRoot -OutputRoot $sdistOutput *> $null
    $sdistExit = $LASTEXITCODE
    $ErrorActionPreference = $oldPreference
    if ($sdistExit -eq 0 -or (Test-Path -LiteralPath $sdistOutput)) {
        throw 'Runtime Pack builder accepted an sdist in the offline cache'
    }
    Remove-Item -LiteralPath (Join-Path $wheelhouse 'contractdep-1.0.0.tar.gz') -Force

    $junctionTarget = Join-Path $tempRoot 'junction-target'
    $junctionPath = Join-Path $tempRoot 'junction-parent'
    New-Item -ItemType Directory -Path (Join-Path $junctionTarget 'output-parent') | Out-Null
    New-Item -ItemType Junction -Path $junctionPath -Target $tempRoot | Out-Null
    try {
        $junctionCacheOutput = Join-Path $tempRoot 'junction-cache-output'
        $oldPreference = $ErrorActionPreference
        $ErrorActionPreference = 'SilentlyContinue'
        & powershell -NoProfile -ExecutionPolicy Bypass -File $buildScript `
            -LockFile $readyLockPath -OfficialCacheRoot (Join-Path $junctionPath 'official-cache') `
            -OutputRoot $junctionCacheOutput *> $null
        $junctionCacheExit = $LASTEXITCODE
        $ErrorActionPreference = $oldPreference
        if ($junctionCacheExit -eq 0 -or (Test-Path -LiteralPath $junctionCacheOutput)) {
            throw 'Runtime Pack builder accepted a cache below a junction ancestor'
        }

        $junctionOutput = Join-Path $junctionPath 'junction-target/output-parent/runtime'
        $oldPreference = $ErrorActionPreference
        $ErrorActionPreference = 'SilentlyContinue'
        & powershell -NoProfile -ExecutionPolicy Bypass -File $buildScript `
            -LockFile $readyLockPath -OfficialCacheRoot $cacheRoot -OutputRoot $junctionOutput *> $null
        $junctionOutputExit = $LASTEXITCODE
        $ErrorActionPreference = $oldPreference
        if ($junctionOutputExit -eq 0 -or (Test-Path -LiteralPath $junctionOutput)) {
            throw 'Runtime Pack builder accepted an output below a junction ancestor'
        }
    }
    finally {
        [System.IO.Directory]::Delete($junctionPath)
    }
}
finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}

Write-Output 'voice clone Runtime Pack contracts passed'
