param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('qwen', 'indextts25', 'cosyvoice3')]
    [string]$Runtime,
    [Parameter(Mandatory = $true)]
    [string]$RuntimeRoot,
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [string]$PublishBaseUrl,
    [string]$SigningKeyPath
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression

if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Version must be semantic x.y.z' }
if (-not [string]::IsNullOrWhiteSpace($PublishBaseUrl) -or
    -not [string]::IsNullOrWhiteSpace($SigningKeyPath)) {
    throw 'RELEASE_FEED_REFUSED: no AG Player Runtime Pack signing implementation/key is configured'
}

$sourceRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$runtimeMap = @{
    qwen = @{ packageId = 'runtime-qwen'; runtimeId = 'qwen-shared'; adapterId = 'qwen' }
    indextts25 = @{ packageId = 'runtime-indextts25'; runtimeId = 'indextts25-isolated'; adapterId = 'indextts25' }
    cosyvoice3 = @{ packageId = 'runtime-cosyvoice3'; runtimeId = 'cosyvoice3-isolated'; adapterId = 'cosyvoice3' }
}
$identity = $runtimeMap[$Runtime]

function Resolve-SafeDirectory([string]$Path, [string]$Label) {
    if (-not [System.IO.Path]::IsPathRooted($Path)) { throw "$Label must be absolute" }
    $item = Get-Item -LiteralPath ([System.IO.Path]::GetFullPath($Path)) -Force -ErrorAction Stop
    if (-not $item.PSIsContainer -or
        ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
        throw "$Label must be a real directory"
    }
    $current = $item
    while ($null -ne $current) {
        if ($current.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
            throw "$Label has a reparse-point ancestor"
        }
        $current = $current.Parent
    }
    return $item.FullName
}

function Assert-SafeRelativePath([string]$Path) {
    if ([string]::IsNullOrEmpty($Path) -or $Path.StartsWith('/') -or $Path.Contains('\') -or
        $Path -match '[<>:"|?*\x00-\x1F]') { throw "Unsafe Runtime path: $Path" }
    foreach ($component in $Path.Split('/')) {
        $base = $component.Split('.')[0].ToUpperInvariant()
        if ([string]::IsNullOrEmpty($component) -or $component -in @('.','..') -or
            $component.StartsWith(' ') -or $component.EndsWith('.') -or $component.EndsWith(' ') -or
            $base -in @('CON','PRN','AUX','NUL') -or $base -match '^(COM|LPT)[1-9]$') {
            throw "Unsafe Runtime path: $Path"
        }
    }
}

function Get-RelativePath([string]$Root, [string]$Path) {
    $rootUri = [System.Uri]::new($Root.TrimEnd('\') + '\')
    $pathUri = [System.Uri]::new($Path)
    return [System.Uri]::UnescapeDataString($rootUri.MakeRelativeUri($pathUri).ToString())
}

function Get-SafeRuntimeFiles([string]$Root) {
    $files = [System.Collections.Generic.List[object]]::new()
    $pending = [System.Collections.Generic.Stack[string]]::new()
    $pending.Push($Root)
    while ($pending.Count -gt 0) {
        $directory = $pending.Pop()
        foreach ($item in @(Get-ChildItem -LiteralPath $directory -Force)) {
            if ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
                throw "Runtime Pack contains a link or reparse point: $($item.FullName)"
            }
            if ($item.PSIsContainer) { $pending.Push($item.FullName); continue }
            $relative = (Get-RelativePath $Root $item.FullName).Replace('\','/')
            Assert-SafeRelativePath $relative
            $files.Add([pscustomobject]@{ item = $item; relative = $relative })
        }
    }
    return @($files | Sort-Object relative)
}

$runtimeRootFull = Resolve-SafeDirectory $RuntimeRoot 'RuntimeRoot'
$outputFull = Resolve-SafeDirectory $OutputDirectory 'OutputDirectory'
$lockPath = Join-Path $sourceRoot "plugins/voice-clone/runtime/$Runtime.lock.json"
$lock = Get-Content -Raw -LiteralPath $lockPath | ConvertFrom-Json
if ($lock.schemaVersion -ne 1 -or $lock.ready -ne $true -or
    $lock.runtimeId -ne $identity.runtimeId -or $lock.unresolved.Count -ne 0) {
    throw 'Runtime lock is not an approved ready recipe'
}
$buildManifestPath = Join-Path $runtimeRootFull 'runtime-manifest.json'
$noticesPath = Join-Path $runtimeRootFull 'THIRD_PARTY_NOTICES.txt'
$pythonPath = Join-Path $runtimeRootFull 'python.exe'
foreach ($required in @($buildManifestPath, $noticesPath, $pythonPath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Runtime build output is incomplete: $required"
    }
}
$buildManifest = Get-Content -Raw -LiteralPath $buildManifestPath | ConvertFrom-Json
if ($buildManifest.schemaVersion -ne 1 -or $buildManifest.runtimeId -ne $lock.runtimeId -or
    $buildManifest.source.revision -ne $lock.source.revision -or
    $buildManifest.source.tree -ne $lock.source.tree -or
    $buildManifest.requirements.sha256 -ne $lock.requirements.sha256) {
    throw 'Runtime build manifest does not match the pinned official lock'
}

$files = Get-SafeRuntimeFiles $runtimeRootFull
$archiveName = "$($identity.packageId)-$Version-windows-x86_64.zip"
$archivePath = Join-Path $outputFull $archiveName
$manifestPath = Join-Path $outputFull "$($identity.packageId)-$Version-windows-x86_64.manifest.json"
if ((Test-Path -LiteralPath $archivePath) -or (Test-Path -LiteralPath $manifestPath)) {
    throw 'Runtime package output already exists'
}

$archiveStream = [System.IO.File]::Open($archivePath, [System.IO.FileMode]::CreateNew,
                                       [System.IO.FileAccess]::ReadWrite,
                                       [System.IO.FileShare]::None)
try {
    $zip = [System.IO.Compression.ZipArchive]::new(
        $archiveStream, [System.IO.Compression.ZipArchiveMode]::Create, $true)
    try {
        foreach ($file in $files) {
            $entry = $zip.CreateEntry($file.relative, [System.IO.Compression.CompressionLevel]::Optimal)
            $entry.LastWriteTime = [System.DateTimeOffset]::new(1980,1,1,0,0,0,[System.TimeSpan]::Zero)
            $source = [System.IO.File]::OpenRead($file.item.FullName)
            $target = $entry.Open()
            try { $source.CopyTo($target) } finally { $target.Dispose(); $source.Dispose() }
        }
    } finally { $zip.Dispose() }
} finally { $archiveStream.Dispose() }

$payload = @()
$installedBytes = [int64]0
foreach ($file in $files) {
    $installedBytes += [int64]$file.item.Length
    $payload += [ordered]@{
        path = $file.relative
        bytes = $file.item.Length
        sha256 = (Get-FileHash -LiteralPath $file.item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}
$licenseNotices = @([ordered]@{
    name = 'CPython'; spdx = 'PSF-2.0'; url = 'https://docs.python.org/3/license.html'
})
foreach ($notice in $lock.notices) {
    $licenseNotices += [ordered]@{
        name = $notice.name; spdx = $notice.license; url = $notice.sourceUrl
    }
}
$manifest = [ordered]@{
    schemaVersion = 1
    packageId = $identity.packageId
    runtimeId = $identity.runtimeId
    version = $Version
    compatibleAdapters = @([ordered]@{ adapterId = $identity.adapterId; adapterVersion = '1.0.0' })
    protocolVersion = 1
    platform = 'windows'
    architecture = 'x86_64'
    minimumPlayerVersion = '1.0.0'
    archiveFormat = 'zip'
    installRoot = "runtime/$($identity.runtimeId)/$Version"
    publisher = [ordered]@{ name = 'AG Player'; url = 'https://agplayer.cn' }
    licenseNotices = $licenseNotices
    signature = [ordered]@{ status = 'unsigned-test'; algorithm = $null; keyId = $null }
    packageUrl = "http://127.0.0.1/unsigned-test/$archiveName"
    packageBytes = (Get-Item -LiteralPath $archivePath).Length
    packageSha256 = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    installedBytes = $installedBytes
    entryCount = $payload.Count
    files = $payload
}
[System.IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 10),
                               [System.Text.UTF8Encoding]::new($false))
Write-Output "Unsigned-test Runtime Pack: $archivePath"
Write-Output "Manifest: $manifestPath"
