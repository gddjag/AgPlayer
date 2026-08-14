[CmdletBinding()]
param(
    [string]$SourceRoot,
    [Parameter(Mandatory = $true)][string]$BuildRoot,
    [Parameter(Mandatory = $true)][string]$OutputRoot,
    [Parameter(Mandatory = $true)][string]$Version,
    [ValidateSet('Release')][string]$Configuration,
    [string]$PublishBaseUrl
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Write-Utf8NoBom([string]$Path, [string]$Content) {
    [IO.File]::WriteAllText($Path, $Content, [Text.UTF8Encoding]::new($false))
}

function Assert-NoReparseAncestor([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    $cursor = $full
    while (-not (Test-Path -LiteralPath $cursor)) {
        $parent = Split-Path -Parent $cursor
        if ([string]::IsNullOrWhiteSpace($parent) -or $parent -eq $cursor) {
            throw "Path has no existing ancestor: $Path"
        }
        $cursor = $parent
    }
    $existingAncestor = $cursor
    while (-not [string]::IsNullOrWhiteSpace($cursor)) {
        $item = Get-Item -LiteralPath $cursor -Force -ErrorAction Stop
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Path must not traverse a link or reparse point: $Path"
        }
        $parent = Split-Path -Parent $cursor
        if ($parent -eq $cursor) { break }
        $cursor = $parent
    }
    $canonicalAncestor = [IO.Path]::GetFullPath((Resolve-Path -LiteralPath $existingAncestor -ErrorAction Stop).ProviderPath)
    if (-not $existingAncestor.Equals($canonicalAncestor, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Existing path ancestor is not canonical: $Path"
    }
    return $full
}

function Assert-SafeRelativePath([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path) -or [IO.Path]::IsPathRooted($Path) -or
        $Path.Contains('..') -or $Path.Contains(':') -or $Path.Contains('\')) {
        throw "Unsafe package path: $Path"
    }
}

function Assert-ContainedPath([string]$Root, [string]$Path) {
    $rootPrefix = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    $candidate = [IO.Path]::GetFullPath($Path)
    if (-not $candidate.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Package staging path escaped its root: $Path"
    }
}

function New-SafeDirectory([string]$Path, [string]$AllowedRoot = '') {
    $full = Assert-NoReparseAncestor $Path
    if (-not [string]::IsNullOrWhiteSpace($AllowedRoot)) {
        [void](Assert-NoReparseAncestor $AllowedRoot)
        $allowedFull = [IO.Path]::GetFullPath($AllowedRoot)
        if (-not $full.Equals($allowedFull, [StringComparison]::OrdinalIgnoreCase)) {
            Assert-ContainedPath $allowedFull $full
        }
    }
    $missing = @()
    $cursor = $full
    while (-not (Test-Path -LiteralPath $cursor)) {
        $missing = @((Split-Path -Leaf $cursor)) + $missing
        $cursor = Split-Path -Parent $cursor
    }
    foreach ($segment in $missing) {
        $cursor = Join-Path $cursor $segment
        if (-not [string]::IsNullOrWhiteSpace($AllowedRoot)) { Assert-ContainedPath $AllowedRoot $cursor }
        New-Item -ItemType Directory -Path $cursor -ErrorAction Stop | Out-Null
        $created = Get-Item -LiteralPath $cursor -Force -ErrorAction Stop
        if (-not $created.PSIsContainer -or
            ($created.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Directory creation produced an unsafe path: $cursor"
        }
    }
    $directory = Get-Item -LiteralPath $full -Force -ErrorAction Stop
    if (-not $directory.PSIsContainer) { throw "Expected a directory: $full" }
    [void](Assert-NoReparseAncestor $full)
    return $full
}

function Assert-SafeFileTarget([string]$Path, [string]$AllowedRoot) {
    $full = [IO.Path]::GetFullPath($Path)
    Assert-ContainedPath $AllowedRoot $full
    [void](Assert-NoReparseAncestor $full)
    $parent = Split-Path -Parent $full
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
        throw "File target parent is missing: $full"
    }
    if (Test-Path -LiteralPath $full) {
        $item = Get-Item -LiteralPath $full -Force -ErrorAction Stop
        if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "File target is unsafe: $full"
        }
    }
    return $full
}

function Remove-SafeFile([string]$Path, [string]$AllowedRoot) {
    $full = Assert-SafeFileTarget $Path $AllowedRoot
    if (Test-Path -LiteralPath $full) { Remove-Item -LiteralPath $full -Force -ErrorAction Stop }
}

function Remove-SafeTree([string]$Path, [string]$AllowedRoot) {
    $full = [IO.Path]::GetFullPath($Path)
    Assert-ContainedPath $AllowedRoot $full
    [void](Assert-NoReparseAncestor $full)
    if (-not (Test-Path -LiteralPath $full)) { return }
    $rootItem = Get-Item -LiteralPath $full -Force -ErrorAction Stop
    if (-not $rootItem.PSIsContainer) { throw "Cleanup target is not a directory: $full" }
    $directories = [Collections.Generic.List[string]]::new()
    $files = [Collections.Generic.List[string]]::new()
    $stack = [Collections.Generic.Stack[string]]::new()
    $stack.Push($full)
    while ($stack.Count -gt 0) {
        $directory = $stack.Pop()
        $directories.Add($directory)
        foreach ($child in @(Get-ChildItem -LiteralPath $directory -Force -ErrorAction Stop)) {
            if (($child.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Cleanup target contains a link or reparse point: $($child.FullName)"
            }
            if ($child.PSIsContainer) { $stack.Push($child.FullName) }
            else { $files.Add($child.FullName) }
        }
    }
    foreach ($file in $files) { Remove-Item -LiteralPath $file -Force -ErrorAction Stop }
    for ($index = $directories.Count - 1; $index -ge 0; --$index) {
        Remove-Item -LiteralPath $directories[$index] -Force -ErrorAction Stop
    }
}

function Copy-PackageFile([string]$From, [string]$RelativeTo) {
    Assert-SafeRelativePath $RelativeTo
    if (-not (Test-Path -LiteralPath $From -PathType Leaf)) {
        throw "Required package input is missing: $From"
    }
    [void](Assert-NoReparseAncestor $From)
    $destination = Join-Path $script:packageRoot $RelativeTo
    Assert-ContainedPath $script:packageRoot $destination
    $destinationDirectory = Split-Path -Parent $destination
    [void](New-SafeDirectory $destinationDirectory $script:packageRoot)
    [void](Assert-SafeFileTarget $destination $script:packageRoot)
    Copy-Item -LiteralPath $From -Destination $destination
}

function Get-RelativePackagePath([string]$Path) {
    return $Path.Substring($script:packageRoot.Length + 1).Replace('\', '/')
}

function New-HashEntry([IO.FileInfo]$File) {
    $relative = Get-RelativePackagePath $File.FullName
    return [ordered]@{
        path = $relative
        bytes = $File.Length
        sha256 = (Get-FileHash -LiteralPath $File.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

if ([string]::IsNullOrWhiteSpace($SourceRoot)) {
    $SourceRoot = Join-Path $PSScriptRoot '../..'
}

if ($Version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$') {
    throw 'Version must use numeric major.minor.patch form'
}
if ($Version -ne '1.0.0') {
    throw 'Version must match the compiled Voice Clone plugin metadata (1.0.0)'
}

$source = [IO.Path]::GetFullPath($SourceRoot)
$build = [IO.Path]::GetFullPath($BuildRoot)
if (-not (Test-Path -LiteralPath $source -PathType Container)) { throw "SourceRoot is missing: $source" }
if (-not (Test-Path -LiteralPath $build -PathType Container)) { throw "BuildRoot is missing: $build" }
[void](Assert-NoReparseAncestor $source)
[void](Assert-NoReparseAncestor $build)

$cmakeCachePath = Join-Path $build 'CMakeCache.txt'
if (-not (Test-Path -LiteralPath $cmakeCachePath -PathType Leaf)) {
    throw 'BuildRoot must contain CMakeCache.txt'
}
$cmakeCache = Get-Content -Raw -LiteralPath $cmakeCachePath
$multiConfiguration = $cmakeCache -match '(?m)^CMAKE_CONFIGURATION_TYPES:[^=]*=.+$'
if ($multiConfiguration) {
    if ($Configuration -ne 'Release') {
        throw 'Multi-configuration builds require -Configuration Release'
    }
}
elseif ($cmakeCache -notmatch '(?m)^CMAKE_BUILD_TYPE:STRING=Release\s*$') {
    throw 'BuildRoot must be a CMake Release build'
}

$output = [IO.Path]::GetFullPath($OutputRoot)
$output = New-SafeDirectory $output

$publishRequested = $PSBoundParameters.ContainsKey('PublishBaseUrl')
$publishUri = $null
if ($publishRequested) {
    if ([string]::IsNullOrWhiteSpace($PublishBaseUrl)) { throw 'PublishBaseUrl must be a real HTTPS base URL' }
    if (-not [Uri]::TryCreate($PublishBaseUrl, [UriKind]::Absolute, [ref]$publishUri)) {
        throw 'PublishBaseUrl must be a real HTTPS base URL'
    }
    $publishHost = $publishUri.Host.TrimEnd('.')
    $publishHostType = [Uri]::CheckHostName($publishHost)
    $publishHostIsIpAddress = $publishHostType -in @([UriHostNameType]::IPv4, [UriHostNameType]::IPv6)
    if (
        $publishUri.Scheme -ne 'https' -or [string]::IsNullOrWhiteSpace($publishUri.Host) -or
        $publishUri.IsLoopback -or -not [string]::IsNullOrEmpty($publishUri.UserInfo) -or
        -not [string]::IsNullOrEmpty($publishUri.Query) -or
        -not [string]::IsNullOrEmpty($publishUri.Fragment) -or
        (-not $publishHostIsIpAddress -and $publishHost.IndexOf('.') -lt 0) -or
        $publishHost.EndsWith('.localhost', [StringComparison]::OrdinalIgnoreCase) -or
        $publishHost -match '(?i)(^|\.)example\.(com|net|org)$|\.(invalid|example|test)$' -or
        $PublishBaseUrl -match '(?i)(^|/)(\.\.|%2e%2e)(/|$)|%2f|%5c' -or
        [Uri]::UnescapeDataString($publishUri.AbsolutePath).Contains('\')) {
        throw 'PublishBaseUrl must be a real HTTPS base URL'
    }
}

$dllMatches = @(Get-ChildItem -LiteralPath $build -Recurse -Force -File -Filter 'agplayer_voice_clone.dll' |
    Where-Object { ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0 })
if ($multiConfiguration) {
    $dllMatches = @($dllMatches | Where-Object {
        $relative = $_.FullName.Substring($build.Length + 1).Replace('\', '/')
        @($relative.Split('/')) -contains 'Release'
    })
}
if ($dllMatches.Count -ne 1) {
    throw "BuildRoot must contain exactly one agplayer_voice_clone.dll; found $($dllMatches.Count)"
}
$dllImports = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($dllMatches[0].FullName))
if ($dllImports -match '(?i)(Qt6[A-Za-z0-9_]*d\.dll|MSVCP[0-9]*D\.dll|VCRUNTIME[0-9_]*D\.dll|ucrtbased\.dll)') {
    throw 'Voice Clone plugin imports Debug runtime libraries and cannot be packaged'
}

$packageName = "AGPlayer-VoiceClonePlugin-$Version-windows-x64"
$zipName = "$packageName.zip"
$externalManifestName = "$packageName.manifest.json"
$stagingContainer = Join-Path $output ('.stage-' + [guid]::NewGuid().ToString('N'))
$script:packageRoot = Join-Path $stagingContainer $packageName
$temporaryZip = Join-Path $output ('.package-' + [guid]::NewGuid().ToString('N') + '.zip')
$finalZip = Join-Path $output $zipName
$externalManifest = Join-Path $output $externalManifestName

Assert-ContainedPath $output $script:packageRoot
Assert-ContainedPath $output $temporaryZip
Assert-ContainedPath $output $finalZip
[void](New-SafeDirectory $stagingContainer $output)
$script:packageRoot = New-SafeDirectory $script:packageRoot $stagingContainer
[void](Assert-SafeFileTarget $temporaryZip $output)
[void](Assert-SafeFileTarget $finalZip $output)
[void](Assert-SafeFileTarget $externalManifest $output)

try {
    Copy-PackageFile $dllMatches[0].FullName 'agplayer_voice_clone.dll'

    $qmlFiles = @(
        'VoiceCloneWorkspace.qml', 'VoiceCloneModelBar.qml', 'VoiceCloneReferencePanel.qml',
        'VoiceCloneTextPanel.qml', 'VoiceCloneOutputPanel.qml', 'VoiceCloneParameterPanel.qml',
        'VoiceCloneResultPanel.qml'
    )
    foreach ($name in $qmlFiles) {
        Copy-PackageFile (Join-Path $source "plugins/voice-clone/qml/$name") "qml/$name"
    }

    Copy-PackageFile (Join-Path $source 'plugins/voice-clone/registry/models.json') 'registry/models.json'
    foreach ($name in @('qwen3-tts-0.6b.json', 'qwen3-tts-1.7b.json', 'indextts-2.5.json', 'fun-cosyvoice3.json')) {
        Copy-PackageFile (Join-Path $source "plugins/voice-clone/registry/downloads/$name") "registry/downloads/$name"
    }
    Copy-PackageFile (Join-Path $source 'plugins/voice-clone/config/agplayer-model.example.json') 'config/agplayer-model.example.json'
    Copy-PackageFile (Join-Path $source 'plugins/voice-clone/config/plugin-feed.example.json') 'config/plugin-feed.example.json'
    Copy-PackageFile (Join-Path $source 'plugins/voice-clone/config/runtime-feed.example.json') 'config/runtime-feed.example.json'

    $adapters = @(
        @{ id = 'qwen'; worker = 'qwen' },
        @{ id = 'indextts25'; worker = 'indextts25' },
        @{ id = 'cosyvoice3'; worker = 'cosyvoice3' }
    )
    foreach ($adapter in $adapters) {
        $prefix = "adapters/$($adapter.id)/1.0.0"
        Copy-PackageFile (Join-Path $source "plugins/voice-clone/adapters/$($adapter.id)/1.0.0/adapter.json") "$prefix/adapter.json"
        Copy-PackageFile (Join-Path $source "plugins/voice-clone/worker/$($adapter.worker)/worker.py") "$prefix/workers/$($adapter.worker)/worker.py"
        Copy-PackageFile (Join-Path $source 'plugins/voice-clone/worker/common/agvoice_protocol.py') "$prefix/workers/common/agvoice_protocol.py"
    }

    foreach ($name in @('qwen.lock.json', 'indextts25.lock.json', 'cosyvoice3.lock.json')) {
        Copy-PackageFile (Join-Path $source "plugins/voice-clone/runtime/$name") "runtime/$name"
    }
    foreach ($name in @('qwen-win-x64-py312.requirements.txt', 'indextts25-win-x64-py311.requirements.txt', 'cosyvoice3-win-x64-py310.requirements.txt')) {
        Copy-PackageFile (Join-Path $source "plugins/voice-clone/runtime/locks/$name") "runtime/locks/$name"
    }
    Copy-PackageFile (Join-Path $source 'scripts/voice-clone/build-runtime-pack.ps1') 'scripts/build-runtime-pack.ps1'
    Copy-PackageFile (Join-Path $source 'docs/voice-clone/README.zh-CN.md') 'docs/README.zh-CN.md'
    Copy-PackageFile (Join-Path $source 'docs/voice-clone/ADDING_MODELS.zh-CN.md') 'docs/ADDING_MODELS.zh-CN.md'
    Copy-PackageFile (Join-Path $source 'plugins/voice-clone/licenses/THIRD_PARTY_NOTICES.json') 'licenses/THIRD_PARTY_NOTICES.json'

    $hostManifest = [ordered]@{
        schemaVersion = 1
        pluginId = 'agplayer.voice-clone'
        version = $Version
        availableVersion = $Version
        platform = 'windows'
        architecture = 'x86_64'
        minimumPlayerVersion = '1.0.0'
        protocolVersion = 1
        library = 'agplayer_voice_clone.dll'
    }
    $hostManifestPath = Assert-SafeFileTarget (Join-Path $script:packageRoot 'agplayer-voice-clone.json') $script:packageRoot
    Write-Utf8NoBom $hostManifestPath ($hostManifest | ConvertTo-Json -Depth 8)

    $payloadFiles = @(Get-ChildItem -LiteralPath $script:packageRoot -Recurse -Force -File |
        Sort-Object { Get-RelativePackagePath $_.FullName })
    $fileEntries = @($payloadFiles | ForEach-Object { New-HashEntry $_ })
    $packageManifestObject = [ordered]@{
        schemaVersion = 1
        packageId = 'agplayer.voice-clone'
        version = $Version
        platform = 'windows'
        architecture = 'x86_64'
        minimumPlayerVersion = '1.0.0'
        protocolVersion = 1
        installRoot = 'plugins/voice-clone'
        signatureStatus = 'unsigned-test'
        signature = $null
        containsModelWeights = $false
        containsRuntimePayloads = $false
        files = $fileEntries
    }
    $packageManifestPath = Join-Path $script:packageRoot 'package-manifest.json'
    [void](Assert-SafeFileTarget $packageManifestPath $script:packageRoot)
    $packageManifestJson = $packageManifestObject | ConvertTo-Json -Depth 12
    Write-Utf8NoBom $packageManifestPath $packageManifestJson

    $hashFiles = @(Get-ChildItem -LiteralPath $script:packageRoot -Recurse -Force -File |
        Where-Object { $_.Name -ne 'SHA256SUMS' } |
        Sort-Object { Get-RelativePackagePath $_.FullName })
    $hashLines = @($hashFiles | ForEach-Object {
        $entry = New-HashEntry $_
        "$($entry.sha256)  $($entry.path)"
    })
    $hashListPath = Assert-SafeFileTarget (Join-Path $script:packageRoot 'SHA256SUMS') $script:packageRoot
    Write-Utf8NoBom $hashListPath (($hashLines -join "`n") + "`n")

    Compress-Archive -LiteralPath $script:packageRoot -DestinationPath $temporaryZip -CompressionLevel Optimal
    & (Join-Path $source 'scripts/voice-clone/verify-plugin-package.ps1') -PackagePath $temporaryZip

    if (Test-Path -LiteralPath $finalZip) { Remove-SafeFile $finalZip $output }
    Move-Item -LiteralPath $temporaryZip -Destination $finalZip
    Write-Utf8NoBom $externalManifest $packageManifestJson

    if ($publishRequested) {
        $baseBuilder = [UriBuilder]::new($publishUri)
        $baseBuilder.UserName = ''
        $baseBuilder.Password = ''
        $baseBuilder.Query = ''
        $baseBuilder.Fragment = ''
        $baseBuilder.Path = $publishUri.AbsolutePath.TrimEnd('/') + '/'
        $normalizedBase = $baseBuilder.Uri
        $packageUri = [Uri]::new($normalizedBase, [Uri]::EscapeDataString($zipName))
        $manifestUri = [Uri]::new($normalizedBase, [Uri]::EscapeDataString($externalManifestName))
        $feed = [ordered]@{
            schemaVersion = 1
            pluginId = 'agplayer.voice-clone'
            version = $Version
            platform = 'windows'
            architecture = 'x86_64'
            minimumPlayerVersion = '1.0.0'
            packageUrl = $packageUri.AbsoluteUri
            manifestUrl = $manifestUri.AbsoluteUri
            signature = [ordered]@{ status = 'unsigned-test'; algorithm = $null; keyId = $null }
        }
        $feedPath = Assert-SafeFileTarget (Join-Path $output 'plugin-feed.json') $output
        Write-Utf8NoBom $feedPath ($feed | ConvertTo-Json -Depth 8)
    }
}
finally {
    if (Test-Path -LiteralPath $temporaryZip) { Remove-SafeFile $temporaryZip $output }
    if (Test-Path -LiteralPath $stagingContainer) {
        Remove-SafeTree $stagingContainer $output
    }
}

Write-Host $finalZip
