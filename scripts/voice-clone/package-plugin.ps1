[CmdletBinding()]
param(
    [string]$SourceRoot,
    [Parameter(Mandatory = $true)][string]$BuildRoot,
    [Parameter(Mandatory = $true)][string]$OutputRoot,
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$PublishBaseUrl
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Write-Utf8NoBom([string]$Path, [string]$Content) {
    [IO.File]::WriteAllText($Path, $Content, [Text.UTF8Encoding]::new($false))
}

function Assert-NoReparseAncestor([string]$Path) {
    $cursor = [IO.Path]::GetFullPath($Path)
    while (-not [string]::IsNullOrWhiteSpace($cursor)) {
        if (Test-Path -LiteralPath $cursor) {
            $item = Get-Item -LiteralPath $cursor -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Path must not traverse a link or reparse point: $Path"
            }
        }
        $parent = Split-Path -Parent $cursor
        if ($parent -eq $cursor) { break }
        $cursor = $parent
    }
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

function Copy-PackageFile([string]$From, [string]$RelativeTo) {
    Assert-SafeRelativePath $RelativeTo
    if (-not (Test-Path -LiteralPath $From -PathType Leaf)) {
        throw "Required package input is missing: $From"
    }
    Assert-NoReparseAncestor $From
    $destination = Join-Path $script:packageRoot $RelativeTo
    Assert-ContainedPath $script:packageRoot $destination
    $destinationDirectory = Split-Path -Parent $destination
    New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
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
Assert-NoReparseAncestor $source
Assert-NoReparseAncestor $build

$output = [IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Path $output -Force | Out-Null
Assert-NoReparseAncestor $output

$publishRequested = $PSBoundParameters.ContainsKey('PublishBaseUrl')
$publishUri = $null
if ($publishRequested) {
    if ([string]::IsNullOrWhiteSpace($PublishBaseUrl)) { throw 'PublishBaseUrl must be a real HTTPS base URL' }
    if (-not [Uri]::TryCreate($PublishBaseUrl, [UriKind]::Absolute, [ref]$publishUri) -or
        $publishUri.Scheme -ne 'https' -or [string]::IsNullOrWhiteSpace($publishUri.Host) -or
        $publishUri.IsLoopback -or $publishUri.Host -match '(?i)(^|\.)example\.(com|net|org)$|\.(invalid|example|test)$') {
        throw 'PublishBaseUrl must be a real HTTPS base URL'
    }
}

$dllMatches = @(Get-ChildItem -LiteralPath $build -Recurse -Force -File -Filter 'agplayer_voice_clone.dll' |
    Where-Object { ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0 })
if ($dllMatches.Count -ne 1) {
    throw "BuildRoot must contain exactly one agplayer_voice_clone.dll; found $($dllMatches.Count)"
}

$packageName = "AGPlayer-VoiceClonePlugin-$Version-windows-x64"
$zipName = "$packageName.zip"
$externalManifestName = "$packageName.manifest.json"
$stagingContainer = Join-Path $output ('.stage-' + [guid]::NewGuid().ToString('N'))
$script:packageRoot = Join-Path $stagingContainer $packageName
$temporaryZip = Join-Path $output ('.package-' + [guid]::NewGuid().ToString('N') + '.zip')
$finalZip = Join-Path $output $zipName
$externalManifest = Join-Path $output $externalManifestName

New-Item -ItemType Directory -Path $script:packageRoot -Force | Out-Null
Assert-ContainedPath $output $script:packageRoot
Assert-ContainedPath $output $temporaryZip
Assert-ContainedPath $output $finalZip

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
    Write-Utf8NoBom (Join-Path $script:packageRoot 'agplayer-voice-clone.json') ($hostManifest | ConvertTo-Json -Depth 8)

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
    $packageManifestJson = $packageManifestObject | ConvertTo-Json -Depth 12
    Write-Utf8NoBom $packageManifestPath $packageManifestJson

    $hashFiles = @(Get-ChildItem -LiteralPath $script:packageRoot -Recurse -Force -File |
        Where-Object { $_.Name -ne 'SHA256SUMS' } |
        Sort-Object { Get-RelativePackagePath $_.FullName })
    $hashLines = @($hashFiles | ForEach-Object {
        $entry = New-HashEntry $_
        "$($entry.sha256)  $($entry.path)"
    })
    Write-Utf8NoBom (Join-Path $script:packageRoot 'SHA256SUMS') (($hashLines -join "`n") + "`n")

    Compress-Archive -LiteralPath $script:packageRoot -DestinationPath $temporaryZip -CompressionLevel Optimal
    & (Join-Path $source 'scripts/voice-clone/verify-plugin-package.ps1') -PackagePath $temporaryZip

    if (Test-Path -LiteralPath $finalZip) { Remove-Item -LiteralPath $finalZip -Force }
    Move-Item -LiteralPath $temporaryZip -Destination $finalZip
    Write-Utf8NoBom $externalManifest $packageManifestJson

    if ($publishRequested) {
        $base = $PublishBaseUrl.TrimEnd('/')
        $feed = [ordered]@{
            schemaVersion = 1
            pluginId = 'agplayer.voice-clone'
            version = $Version
            platform = 'windows'
            architecture = 'x86_64'
            minimumPlayerVersion = '1.0.0'
            packageUrl = "$base/$zipName"
            manifestUrl = "$base/$externalManifestName"
            signature = [ordered]@{ status = 'unsigned-test'; algorithm = $null; keyId = $null }
        }
        Write-Utf8NoBom (Join-Path $output 'plugin-feed.json') ($feed | ConvertTo-Json -Depth 8)
    }
}
finally {
    if (Test-Path -LiteralPath $temporaryZip) { Remove-Item -LiteralPath $temporaryZip -Force }
    if (Test-Path -LiteralPath $stagingContainer) {
        Assert-ContainedPath $output $stagingContainer
        Remove-Item -LiteralPath $stagingContainer -Recurse -Force
    }
}

Write-Host $finalZip
