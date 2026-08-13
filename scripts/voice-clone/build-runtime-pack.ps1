param(
    [Parameter(Mandatory = $true)]
    [string]$LockFile,
    [Parameter(Mandatory = $true)]
    [string]$OfficialCacheRoot,
    [Parameter(Mandatory = $true)]
    [string]$OutputRoot
)

$ErrorActionPreference = 'Stop'

function Resolve-ExistingDirectory([string]$Path, [string]$Label) {
    if ([string]::IsNullOrWhiteSpace($Path) -or -not [System.IO.Path]::IsPathRooted($Path)) {
        throw "$Label must be an absolute path"
    }
    $item = Get-Item -LiteralPath $Path -ErrorAction Stop
    if (-not $item.PSIsContainer -or ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
        throw "$Label must be a real directory, not a link or reparse point"
    }
    return $item.FullName
}

function Resolve-RelativeFile([string]$Root, [string]$RelativePath, [string]$Label) {
    if ([string]::IsNullOrWhiteSpace($RelativePath) -or [System.IO.Path]::IsPathRooted($RelativePath)) {
        throw "$Label must be a relative path"
    }
    $normalized = $RelativePath.Replace('/', [System.IO.Path]::DirectorySeparatorChar)
    $parts = $normalized.Split([System.IO.Path]::DirectorySeparatorChar, [System.StringSplitOptions]::RemoveEmptyEntries)
    if ($parts -contains '..' -or $normalized.Contains(':')) {
        throw "$Label escapes its root"
    }
    $candidate = [System.IO.Path]::GetFullPath((Join-Path $Root $normalized))
    $rootPrefix = $Root.TrimEnd('\') + '\'
    if (-not $candidate.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label escapes its root"
    }
    return $candidate
}

$lockPath = (Get-Item -LiteralPath $LockFile -ErrorAction Stop).FullName
$cacheRoot = Resolve-ExistingDirectory $OfficialCacheRoot 'OfficialCacheRoot'
$lock = Get-Content -Raw -LiteralPath $lockPath | ConvertFrom-Json
if ($lock.schemaVersion -ne 1 -or [string]::IsNullOrWhiteSpace($lock.runtimeId)) {
    throw 'Runtime lock schema or identity is invalid'
}
if ($lock.ready -ne $true -or $lock.unresolved.Count -ne 0) {
    throw "RUNTIME_LOCK_NOT_READY: $($lock.runtimeId) has unresolved dependency or license artifacts"
}
if ($lock.source.revision -notmatch '^[0-9a-f]{40}$' -or $lock.source.repository -notmatch '^https://') {
    throw 'Runtime source must use an HTTPS repository and a full commit revision'
}
if ($lock.artifacts.Count -lt 1 -or $lock.notices.Count -lt 1) {
    throw 'Runtime lock requires verified artifacts and notices'
}

$verified = @()
foreach ($artifact in $lock.artifacts) {
    if ($artifact.sha256 -notmatch '^[0-9a-f]{64}$') {
        throw "Artifact has no verified SHA-256: $($artifact.path)"
    }
    $sourcePath = Resolve-RelativeFile $cacheRoot $artifact.path 'Artifact path'
    $item = Get-Item -LiteralPath $sourcePath -ErrorAction Stop
    if ($item.PSIsContainer -or ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
        throw "Artifact must be a regular file: $($artifact.path)"
    }
    $actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $sourcePath).Hash.ToLowerInvariant()
    if ($actualHash -ne $artifact.sha256) {
        throw "Artifact SHA-256 mismatch: $($artifact.path)"
    }
    $verified += [pscustomobject]@{
        path = $artifact.path
        sha256 = $actualHash
        kind = $artifact.kind
        sourcePath = $sourcePath
    }
}

$outputIsAbsolute = -not [string]::IsNullOrWhiteSpace($OutputRoot) -and
    [System.IO.Path]::IsPathRooted($OutputRoot)
$outputFull = if ($outputIsAbsolute) { [System.IO.Path]::GetFullPath($OutputRoot) } else { '' }
if (-not $outputIsAbsolute -or
    $outputFull -eq [System.IO.Path]::GetPathRoot($outputFull) -or
    (Test-Path -LiteralPath $outputFull)) {
    throw 'OutputRoot must be an absolute, non-root path that does not already exist'
}
$outputParent = Split-Path -Parent $outputFull
if (-not (Test-Path -LiteralPath $outputParent -PathType Container)) {
    New-Item -ItemType Directory -Path $outputParent -Force | Out-Null
}
$outputParent = Resolve-ExistingDirectory $outputParent 'Output parent'
$staging = Join-Path $outputParent ('.agplayer-runtime-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $staging | Out-Null

try {
    foreach ($artifact in $verified) {
        $destination = Resolve-RelativeFile $staging $artifact.path 'Artifact destination'
        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
        Copy-Item -LiteralPath $artifact.sourcePath -Destination $destination
    }

    $noticeSections = @()
    foreach ($notice in $lock.notices) {
        if ([string]::IsNullOrWhiteSpace($notice.name) -or
            [string]::IsNullOrWhiteSpace($notice.license) -or
            $notice.sourceUrl -notmatch '^https://') {
            throw 'Runtime notice metadata is incomplete'
        }
        $licensePath = Resolve-RelativeFile $cacheRoot $notice.licenseFile 'Notice licenseFile'
        if (-not (Test-Path -LiteralPath $licensePath -PathType Leaf)) {
            throw "Notice license file is missing: $($notice.licenseFile)"
        }
        $verifiedLicense = $verified | Where-Object { $_.sourcePath -eq $licensePath }
        if (-not $verifiedLicense) {
            throw "Notice license file is not a verified artifact: $($notice.licenseFile)"
        }
        $licenseText = Get-Content -Raw -LiteralPath $licensePath
        $noticeSections += "$($notice.name)`r`nLicense: $($notice.license)`r`nSource: $($notice.sourceUrl)`r`n`r`n$licenseText"
    }
    [System.IO.File]::WriteAllText((Join-Path $staging 'THIRD_PARTY_NOTICES.txt'),
        ($noticeSections -join "`r`n`r`n---`r`n`r`n"), [System.Text.UTF8Encoding]::new($false))

    $manifest = [ordered]@{
        schemaVersion = 1
        runtimeId = $lock.runtimeId
        python = $lock.python
        source = $lock.source
        artifacts = @($verified | ForEach-Object {
            [ordered]@{ path = $_.path; sha256 = $_.sha256; kind = $_.kind }
        })
    }
    [System.IO.File]::WriteAllText((Join-Path $staging 'runtime-manifest.json'),
        ($manifest | ConvertTo-Json -Depth 8), [System.Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $staging -Destination $outputFull
    Write-Output "Runtime Pack created: $outputFull"
}
catch {
    $stagingItem = Get-Item -LiteralPath $staging -ErrorAction SilentlyContinue
    if ($stagingItem -and $stagingItem.FullName.StartsWith($outputParent.TrimEnd('\') + '\', [System.StringComparison]::OrdinalIgnoreCase) -and
        -not ($stagingItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
        Remove-Item -LiteralPath $stagingItem.FullName -Recurse -Force
    }
    throw
}
