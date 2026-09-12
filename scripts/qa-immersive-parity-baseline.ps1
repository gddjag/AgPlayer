[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build/release',
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$capturedAt = [DateTime]::UtcNow.ToString('o')

function Get-RelativePath([string]$Base, [string]$Path) {
    $baseUri = [Uri]($Base.TrimEnd('\') + '\')
    $pathUri = [Uri]$Path
    return [Uri]::UnescapeDataString($baseUri.MakeRelativeUri($pathUri).ToString()).Replace('/', '\')
}

function Get-FileRecord([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    $item = Get-Item -LiteralPath $Path
    return [ordered]@{
        path = [IO.Path]::GetFullPath($Path)
        sizeBytes = [int64]$item.Length
        lastWriteUtc = $item.LastWriteTimeUtc.ToString('o')
        sha256 = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash
    }
}

function Copy-WithVerification([string]$Source, [string]$Destination, [string]$Category) {
    $directory = Split-Path -Parent $Destination
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
    $sourceHash = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
    $destinationHash = (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash
    return [ordered]@{
        category = $Category
        source = [IO.Path]::GetFullPath($Source)
        backup = [IO.Path]::GetFullPath($Destination)
        sourceSha256 = $sourceHash
        backupSha256 = $destinationHash
        bytesMatch = ($sourceHash -eq $destinationHash)
    }
}

function Get-QtDllRecord([string]$Path) {
    $record = Get-FileRecord $Path
    if ($null -eq $record) { return $null }
    $record.fileVersion = (Get-Item -LiteralPath $Path).VersionInfo.FileVersion
    $record.productVersion = (Get-Item -LiteralPath $Path).VersionInfo.ProductVersion
    return $record
}

function Export-GitBytes([string]$Repository, [string[]]$Arguments,
                         [string]$Destination) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $Destination) -Force | Out-Null
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = 'git'
    $startInfo.WorkingDirectory = $Repository
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($argument in $Arguments) { [void]$startInfo.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (!$process.Start()) { throw "Could not start git $($Arguments -join ' ')" }
    try {
        $output = [IO.File]::Open($Destination, [IO.FileMode]::Create,
                                  [IO.FileAccess]::Write, [IO.FileShare]::None)
        $digest = [Security.Cryptography.SHA256]::Create()
        try {
            $buffer = New-Object byte[] 65536
            while (($count = $process.StandardOutput.BaseStream.Read($buffer, 0, $buffer.Length)) -gt 0) {
                [void]$digest.TransformBlock($buffer, 0, $count, $buffer, 0)
                $output.Write($buffer, 0, $count)
            }
            [void]$digest.TransformFinalBlock([byte[]]::new(0), 0, 0)
            $sourceSha256 = [BitConverter]::ToString($digest.Hash).Replace('-', '')
        }
        finally { $output.Dispose() }
        $errorText = $process.StandardError.ReadToEnd()
        $process.WaitForExit()
        if ($process.ExitCode -ne 0) {
            throw "git $($Arguments -join ' ') failed: $errorText"
        }
    }
    finally {
        if ($null -ne $digest) { $digest.Dispose() }
        $process.Dispose()
    }
    return $sourceSha256
}

function Get-IndexBlobInfo([string]$Repository, [string]$RelativePath) {
    $line = @(& git -C $Repository ls-files --stage -- $RelativePath | Select-Object -First 1)
    if ($line.Count -eq 0 -or [string]::IsNullOrWhiteSpace($line[0])) { return $null }
    if ($line[0] -notmatch '^\d+\s+([0-9a-fA-F]{40,64})\s+\d+\t') {
        throw "Could not parse index entry for ${RelativePath}: $($line[0])"
    }
    return [pscustomobject]@{ objectId = $Matches[1].ToLowerInvariant() }
}

function Backup-IndexBlob([string]$Repository, [string]$RelativePath,
                          [string]$Destination, $IndexInfo) {
    if ($null -eq $IndexInfo) {
        return [ordered]@{
            category = 'staged-deletion'
            source = "git-index-deletion:$RelativePath"
            backup = $null
            sourceSha256 = $null
            backupSha256 = $null
            bytesMatch = $null
            preservedBy = 'cached-from-HEAD.patch'
        }
    }
    $sourceSha256 = Export-GitBytes $Repository @('-C', $Repository, 'cat-file', 'blob', ":$RelativePath") $Destination
    $backupSha256 = (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash
    $backupObjectId = (& git -C $Repository hash-object -- $Destination).Trim().ToLowerInvariant()
    return [ordered]@{
        category = 'staged-index-blob'
        source = "git-index:$RelativePath"
        backup = [IO.Path]::GetFullPath($Destination)
        sourceSha256 = $sourceSha256
        backupSha256 = $backupSha256
        bytesMatch = ($sourceSha256 -eq $backupSha256)
        sourceGitObjectId = $IndexInfo.objectId
        backupGitObjectId = $backupObjectId
        gitObjectMatch = ($IndexInfo.objectId -eq $backupObjectId)
    }
}

if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $repoRoot ('build/qa/immersive-parity/local-baseline/' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
}
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $outputRoot) {
    throw "Refusing to overwrite an earlier baseline: $outputRoot"
}
New-Item -ItemType Directory -Path $outputRoot | Out-Null

$buildRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDirectory))
$exePath = Join-Path $buildRoot 'app/AgPlayer.exe'
$qtQuickPath = Join-Path $buildRoot 'app/Qt6Quick.dll'
$backupRoot = Join-Path $outputRoot 'backups'
$verification = @()

$gitHead = (& git -C $repoRoot rev-parse HEAD).Trim()
$gitBranch = (& git -C $repoRoot branch --show-current).Trim()
$gitStatusPorcelain = @(& git -C $repoRoot status --porcelain=v1)
$cachedPatchPath = Join-Path $backupRoot 'git/cached-from-HEAD.patch'
$unstagedPatchPath = Join-Path $backupRoot 'git/unstaged-from-index.patch'
Export-GitBytes $repoRoot @('-C', $repoRoot, 'diff', '--cached', '--binary', '--') $cachedPatchPath
Export-GitBytes $repoRoot @('-C', $repoRoot, 'diff', '--binary', '--') $unstagedPatchPath
$cachedPaths = @(& git -C $repoRoot diff --cached --name-only --)
$unstagedPaths = @(& git -C $repoRoot diff --name-only --)
$dirtyTracked = @($cachedPaths + $unstagedPaths | Sort-Object -Unique)
$untracked = @(& git -C $repoRoot ls-files --others --exclude-standard)
foreach ($relative in $cachedPaths) {
    if ([string]::IsNullOrWhiteSpace($relative)) { continue }
    $indexInfo = Get-IndexBlobInfo $repoRoot $relative
    $verification += Backup-IndexBlob $repoRoot $relative (Join-Path $backupRoot ('index/' + $relative)) $indexInfo
}
foreach ($relative in @($dirtyTracked + $untracked | Sort-Object -Unique)) {
    if ([string]::IsNullOrWhiteSpace($relative)) { continue }
    $source = Join-Path $repoRoot $relative
    if (Test-Path -LiteralPath $source -PathType Leaf) {
        $kind = if ($untracked -contains $relative) { 'untracked-current-bytes' } else { 'tracked-current-bytes' }
        $verification += Copy-WithVerification $source (Join-Path $backupRoot ('files/' + $relative)) $kind
    }
    else {
        $verification += [ordered]@{ category = 'deleted-or-unavailable'; source = $source; backup = $null; sourceSha256 = $null; backupSha256 = $null; bytesMatch = $false }
    }
}

$qmlFiles = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'app/qml/AgPlayer') -Recurse -File -Filter '*.qml' |
    ForEach-Object { Get-FileRecord $_.FullName })
$shaderFiles = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'qt/shaders') -File -Include '*.vert','*.frag','*.comp' |
    ForEach-Object { Get-FileRecord $_.FullName })
$qsbFiles = if (Test-Path -LiteralPath $buildRoot) {
    @(Get-ChildItem -LiteralPath $buildRoot -Recurse -File -Filter '*.qsb' |
        ForEach-Object { Get-FileRecord $_.FullName })
} else { @() }

# The application names establish the Qt NativeFormat location.  Export only
# the proven immersiveVisual subkey; never copy the parent settings hive.
$immersiveKey = 'HKCU:\Software\AgPlayer\AgPlayer\immersiveVisual'
$config = [ordered]@{
    source = 'Qt QSettings NativeFormat; app/main.cpp sets organization=AgPlayer and application=AgPlayer; player_experience_controller.cpp uses immersiveVisual'
    registryPath = $immersiveKey
    status = 'unavailable'
    backup = $null
}
if (Test-Path -LiteralPath $immersiveKey) {
    $configBackup = Join-Path $backupRoot 'config/immersiveVisual.reg'
    New-Item -ItemType Directory -Path (Split-Path -Parent $configBackup) -Force | Out-Null
    & reg.exe export 'HKCU\Software\AgPlayer\AgPlayer\immersiveVisual' $configBackup /y | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'reg.exe could not export the proven immersiveVisual settings key.' }
    $verification += Copy-WithVerification $configBackup (Join-Path $backupRoot 'config/immersiveVisual.verified.reg') 'immersive-settings-export'
    $config.status = 'confirmed-present'
    $config.backup = Get-FileRecord $configBackup
}

$processes = @(Get-Process -Name AgPlayer -ErrorAction SilentlyContinue | ForEach-Object {
    [ordered]@{ id = $_.Id; path = $_.Path; startTime = $(if ($_.StartTime) { $_.StartTime.ToUniversalTime().ToString('o') } else { $null }) }
})
$gpu = @(Get-CimInstance Win32_VideoController | ForEach-Object {
    [ordered]@{ name = $_.Name; driverVersion = $_.DriverVersion; videoProcessor = $_.VideoProcessor; pnpDeviceId = $_.PNPDeviceID }
})
$historicLogs = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'build/qa') -Recurse -File -Include '*immersive*.log','*immersive*.txt' -ErrorAction SilentlyContinue |
    Select-Object -First 30 | ForEach-Object {
        [ordered]@{ path = $_.FullName; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash; evidenceKind = 'historic-only; not a current backend observation' }
    })

$manifest = [ordered]@{
    schemaVersion = 1
    capturedAtUtc = $capturedAt
    repository = [ordered]@{
        root = $repoRoot; head = $gitHead; branch = $gitBranch; statusPorcelain = $gitStatusPorcelain
        cachedPatchFromHead = Get-FileRecord $cachedPatchPath
        unstagedPatchFromIndex = Get-FileRecord $unstagedPatchPath
        cachedPaths = $cachedPaths; unstagedPaths = $unstagedPaths
        dirtyTrackedPaths = $dirtyTracked; untrackedPaths = $untracked
    }
    developmentExecutable = [ordered]@{ status = $(if (Test-Path -LiteralPath $exePath) { 'confirmed' } else { 'unavailable' }); record = Get-FileRecord $exePath }
    qtQuickDll = [ordered]@{ status = $(if (Test-Path -LiteralPath $qtQuickPath) { 'confirmed' } else { 'unavailable' }); record = Get-QtDllRecord $qtQuickPath }
    resources = [ordered]@{ qml = $qmlFiles; shaderSources = $shaderFiles; compiledQsb = $qsbFiles; runtimeLoadedResourceSourceType = 'unknown; source hashes alone do not prove a live or synthetic runtime resource identity' }
    immersiveConfiguration = $config
    currentProcesses = $processes
    gpuAndDriver = $gpu
    historicBackendEvidence = $historicLogs
    backupVerification = $verification
}
$manifestPath = Join-Path $outputRoot 'manifest.json'
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding utf8NoBOM
$verification | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputRoot 'backup-verification.json') -Encoding utf8NoBOM

[pscustomobject]@{
    OutputDirectory = $outputRoot
    Manifest = $manifestPath
    BackupEntries = $verification.Count
    BackupHashesVerified = @($verification | Where-Object { $_.bytesMatch }).Count
    RuntimeLoadedResourceSourceType = 'unknown'
}
