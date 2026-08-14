param(
    [Parameter(Mandatory = $true)]
    [string]$ManifestPath,
    [Parameter(Mandatory = $true)]
    [string]$ArchivePath
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression

function Resolve-SafeFile([string]$Path, [string]$Label) {
    if (-not [System.IO.Path]::IsPathRooted($Path)) { throw "$Label must be absolute" }
    $item = Get-Item -LiteralPath ([System.IO.Path]::GetFullPath($Path)) -Force -ErrorAction Stop
    if ($item.PSIsContainer -or
        ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
        throw "$Label must be a regular file"
    }
    $current = $item.Directory
    while ($null -ne $current) {
        if ($current.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
            throw "$Label has a reparse-point ancestor"
        }
        $current = $current.Parent
    }
    return $item.FullName
}

function Assert-SafeRelativePath([string]$Path) {
    if ($Path -notmatch '^[A-Za-z0-9+_.-]+(?:/[A-Za-z0-9+_.-]+)*$' -or
        $Path.Contains(':') -or $Path.Contains('..')) { throw "Unsafe Runtime path: $Path" }
    foreach ($component in $Path.Split('/')) {
        $base = $component.Split('.')[0].ToUpperInvariant()
        if ($component.EndsWith('.') -or $component.EndsWith(' ') -or
            $base -in @('CON','PRN','AUX','NUL') -or $base -match '^(COM|LPT)[1-9]$') {
            throw "Unsafe Runtime path: $Path"
        }
    }
}

function Get-StreamSha256([System.IO.Stream]$Stream) {
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try {
        return ([System.BitConverter]::ToString($algorithm.ComputeHash($Stream)) -replace '-', '').ToLowerInvariant()
    } finally { $algorithm.Dispose() }
}

$manifestFull = Resolve-SafeFile $ManifestPath 'ManifestPath'
$archiveFull = Resolve-SafeFile $ArchivePath 'ArchivePath'
$manifest = Get-Content -Raw -LiteralPath $manifestFull | ConvertFrom-Json
$approved = @{
    'runtime-qwen' = @{ runtimeId = 'qwen-shared'; adapterId = 'qwen' }
    'runtime-indextts25' = @{ runtimeId = 'indextts25-isolated'; adapterId = 'indextts25' }
    'runtime-cosyvoice3' = @{ runtimeId = 'cosyvoice3-isolated'; adapterId = 'cosyvoice3' }
}
$identity = $approved[$manifest.packageId]
if ($null -eq $identity -or $manifest.schemaVersion -ne 1 -or
    $manifest.runtimeId -ne $identity.runtimeId -or
    $manifest.version -notmatch '^\d+\.\d+\.\d+$' -or
    $manifest.compatibleAdapters.Count -ne 1 -or
    $manifest.compatibleAdapters[0].adapterId -ne $identity.adapterId -or
    $manifest.compatibleAdapters[0].adapterVersion -ne '1.0.0' -or
    $manifest.protocolVersion -ne 1 -or $manifest.platform -ne 'windows' -or
    $manifest.architecture -ne 'x86_64' -or $manifest.archiveFormat -ne 'zip' -or
    $manifest.minimumPlayerVersion -ne '1.0.0' -or
    $manifest.installRoot -ne "runtime/$($identity.runtimeId)/$($manifest.version)" -or
    $manifest.publisher.name -ne 'AG Player' -or
    $manifest.publisher.url -ne 'https://agplayer.cn' -or
    $manifest.licenseNotices.Count -lt 1 -or
    $manifest.signature.status -ne 'unsigned-test' -or
    $manifest.packageUrl -notmatch '^http://(127\.0\.0\.1|localhost)/unsigned-test/' -or
    $manifest.packageBytes -le 0 -or $manifest.packageSha256 -notmatch '^[0-9a-f]{64}$' -or
    $manifest.files.Count -lt 3) {
    throw 'Runtime package manifest identity/schema is invalid'
}
foreach ($notice in $manifest.licenseNotices) {
    if ([string]::IsNullOrWhiteSpace($notice.name) -or
        [string]::IsNullOrWhiteSpace($notice.spdx) -or
        $notice.url -notmatch '^https://') {
        throw 'Runtime package license notice is invalid'
    }
}
$archiveItem = Get-Item -LiteralPath $archiveFull
if ($archiveItem.Length -ne [int64]$manifest.packageBytes -or
    (Get-FileHash -LiteralPath $archiveFull -Algorithm SHA256).Hash.ToLowerInvariant() -ne
        $manifest.packageSha256) {
    throw 'Runtime ZIP size/SHA-256 mismatch'
}

$expected = @{}
foreach ($file in $manifest.files) {
    Assert-SafeRelativePath $file.path
    $key = $file.path.ToLowerInvariant()
    if ($expected.ContainsKey($key) -or $file.bytes -lt 0 -or
        $file.sha256 -notmatch '^[0-9a-f]{64}$') {
        throw 'Runtime manifest contains duplicate or invalid payload metadata'
    }
    $expected[$key] = $file
}
foreach ($required in @('python.exe','runtime-manifest.json','third_party_notices.txt')) {
    if (-not $expected.ContainsKey($required)) { throw "Runtime payload is missing $required" }
}

$stream = [System.IO.File]::OpenRead($archiveFull)
try {
    $zip = [System.IO.Compression.ZipArchive]::new(
        $stream, [System.IO.Compression.ZipArchiveMode]::Read, $true)
    try {
        $seen = @{}
        foreach ($entry in $zip.Entries) {
            $path = $entry.FullName.Replace('\','/')
            Assert-SafeRelativePath $path
            $key = $path.ToLowerInvariant()
            if ($seen.ContainsKey($key) -or -not $expected.ContainsKey($key)) {
                throw "Runtime ZIP contains an unlisted or duplicate path: $path"
            }
            $unixType = (($entry.ExternalAttributes -shr 16) -band 0xF000)
            if (($unixType -ne 0 -and $unixType -ne 0x8000) -or
                (($entry.ExternalAttributes -band 0x400) -ne 0)) {
                throw "Runtime ZIP contains a link/reparse entry: $path"
            }
            $metadata = $expected[$key]
            if ($entry.Length -ne [int64]$metadata.bytes) {
                throw "Runtime ZIP entry size mismatch: $path"
            }
            $entryStream = $entry.Open()
            try { $actualHash = Get-StreamSha256 $entryStream } finally { $entryStream.Dispose() }
            if ($actualHash -ne $metadata.sha256) {
                throw "Runtime ZIP entry SHA-256 mismatch: $path"
            }
            $seen[$key] = $true
        }
        if ($seen.Count -ne $expected.Count) { throw 'Runtime ZIP payload list is incomplete' }
    } finally { $zip.Dispose() }
} finally { $stream.Dispose() }

Write-Output "Runtime Pack verified: $($manifest.packageId) $($manifest.version)"
