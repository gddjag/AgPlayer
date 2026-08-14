[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$PackagePath,
    [string]$HostProbePath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
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

function Assert-ContainedPath([string]$Root, [string]$Path) {
    $rootPrefix = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    $candidate = [IO.Path]::GetFullPath($Path)
    if (-not $candidate.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path escaped its root: $Path"
    }
}

function New-SafeDirectory([string]$Path, [string]$AllowedRoot) {
    $full = Assert-NoReparseAncestor $Path
    [void](Assert-NoReparseAncestor $AllowedRoot)
    Assert-ContainedPath $AllowedRoot $full
    $missing = @()
    $cursor = $full
    while (-not (Test-Path -LiteralPath $cursor)) {
        $missing = @((Split-Path -Leaf $cursor)) + $missing
        $cursor = Split-Path -Parent $cursor
    }
    foreach ($segment in $missing) {
        $cursor = Join-Path $cursor $segment
        Assert-ContainedPath $AllowedRoot $cursor
        New-Item -ItemType Directory -Path $cursor -ErrorAction Stop | Out-Null
        $created = Get-Item -LiteralPath $cursor -Force -ErrorAction Stop
        if (-not $created.PSIsContainer -or
            ($created.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Directory creation produced an unsafe path: $cursor"
        }
    }
    [void](Assert-NoReparseAncestor $full)
    return $full
}

function Remove-SafeTree([string]$Path, [string]$AllowedRoot) {
    $full = [IO.Path]::GetFullPath($Path)
    Assert-ContainedPath $AllowedRoot $full
    [void](Assert-NoReparseAncestor $full)
    if (-not (Test-Path -LiteralPath $full)) { return }
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

function Assert-SafeRelativePath([string]$Path) {
    Assert-True (-not [string]::IsNullOrWhiteSpace($Path)) 'empty package path'
    Assert-True (-not [IO.Path]::IsPathRooted($Path) -and -not $Path.StartsWith('/') -and
        -not $Path.Contains('\') -and -not $Path.Contains(':') -and
        $Path -notmatch '[\x00-\x1f]') "unsafe package path: $Path"
    $segments = $Path.Split('/')
    Assert-True (-not ($segments | Where-Object { $_ -eq '..' -or $_ -eq '.' -or [string]::IsNullOrWhiteSpace($_) })) "unsafe package path: $Path"
    foreach ($segment in $segments) {
        Assert-True ($segment -notmatch '[\. ]$') "Windows-trimmed package path is forbidden: $Path"
        $deviceName = $segment.Split('.')[0]
        Assert-True ($deviceName -notmatch '(?i)^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$') "Windows reserved package path is forbidden: $Path"
    }
}

function Get-RelativePath([string]$Root, [string]$Path) {
    return $Path.Substring($Root.Length + 1).Replace('\', '/')
}

$expectedPayloadPaths = @(
    'agplayer-voice-clone.json', 'agplayer_voice_clone.dll',
    'qml/VoiceCloneWorkspace.qml', 'qml/VoiceCloneModelBar.qml',
    'qml/VoiceCloneReferencePanel.qml', 'qml/VoiceCloneTextPanel.qml',
    'qml/VoiceCloneOutputPanel.qml', 'qml/VoiceCloneParameterPanel.qml',
    'qml/VoiceCloneResultPanel.qml',
    'registry/models.json', 'registry/downloads/qwen3-tts-0.6b.json',
    'registry/downloads/qwen3-tts-1.7b.json', 'registry/downloads/indextts-2.5.json',
    'registry/downloads/fun-cosyvoice3.json',
    'config/agplayer-model.example.json', 'config/plugin-feed.example.json',
    'adapters/qwen/1.0.0/adapter.json',
    'adapters/qwen/1.0.0/workers/qwen/worker.py',
    'adapters/qwen/1.0.0/workers/common/agvoice_protocol.py',
    'adapters/indextts25/1.0.0/adapter.json',
    'adapters/indextts25/1.0.0/workers/indextts25/worker.py',
    'adapters/indextts25/1.0.0/workers/common/agvoice_protocol.py',
    'adapters/cosyvoice3/1.0.0/adapter.json',
    'adapters/cosyvoice3/1.0.0/workers/cosyvoice3/worker.py',
    'adapters/cosyvoice3/1.0.0/workers/common/agvoice_protocol.py',
    'runtime/qwen.lock.json', 'runtime/indextts25.lock.json', 'runtime/cosyvoice3.lock.json',
    'runtime/locks/qwen-win-x64-py312.requirements.txt',
    'runtime/locks/indextts25-win-x64-py311.requirements.txt',
    'runtime/locks/cosyvoice3-win-x64-py310.requirements.txt',
    'scripts/build-runtime-pack.ps1', 'docs/README.zh-CN.md',
    'docs/ADDING_MODELS.zh-CN.md', 'licenses/THIRD_PARTY_NOTICES.json'
)
$expectedPayloadSet = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
foreach ($path in $expectedPayloadPaths) { [void]$expectedPayloadSet.Add($path) }
$expectedArchiveSet = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
foreach ($path in @($expectedPayloadPaths + @('package-manifest.json', 'SHA256SUMS'))) {
    [void]$expectedArchiveSet.Add($path)
}
$expectedDirectorySet = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
foreach ($path in $expectedArchiveSet) {
    $parent = Split-Path -Parent $path
    while (-not [string]::IsNullOrWhiteSpace($parent)) {
        $normalizedParent = $parent.Replace('\', '/')
        [void]$expectedDirectorySet.Add($normalizedParent)
        $parent = Split-Path -Parent $parent
    }
}

$zipPath = [IO.Path]::GetFullPath($PackagePath)
Assert-True (Test-Path -LiteralPath $zipPath -PathType Leaf) "package ZIP is missing: $zipPath"
Assert-True ([IO.Path]::GetExtension($zipPath) -eq '.zip') 'package must be a ZIP file'
[void](Assert-NoReparseAncestor $zipPath)

$archive = [IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    $names = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    $archiveFiles = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
    $rootName = $null
    [long]$totalBytes = 0
    foreach ($entry in $archive.Entries) {
        $directoryEntry = $entry.FullName.EndsWith('/') -or $entry.FullName.EndsWith('\')
        $name = $entry.FullName.Replace('\', '/').TrimEnd('/')
        if ([string]::IsNullOrWhiteSpace($name)) { continue }
        Assert-SafeRelativePath $name
        Assert-True ($names.Add($name)) "duplicate ZIP path: $name"
        $candidateRoot = $name.Split('/')[0]
        if ($null -eq $rootName) { $rootName = $candidateRoot }
        Assert-True ($candidateRoot -eq $rootName) 'ZIP contains more than one root'
        $relative = if ($name.Length -eq $rootName.Length) { '' } else { $name.Substring($rootName.Length + 1) }
        if ($directoryEntry) {
            Assert-True ([string]::IsNullOrEmpty($relative) -or $expectedDirectorySet.Contains($relative)) "unexpected package directory: $relative"
        }
        else {
            Assert-True ($expectedArchiveSet.Contains($relative)) "unexpected package payload: $relative"
            Assert-True ($archiveFiles.Add($relative)) "duplicate package payload: $relative"
            Assert-True ($relative -notmatch '(?i)\.exe$') "executable payload is forbidden: $relative"
            Assert-True ($relative -notmatch '(?i)\.dll$' -or $relative -eq 'agplayer_voice_clone.dll') "extra DLL payload is forbidden: $relative"
        }
        $totalBytes += $entry.Length
        Assert-True ($entry.Length -le 134217728 -and $totalBytes -le 268435456) 'ZIP exceeds the standalone plugin size limit'
        Assert-True ($name -notmatch '(?i)(^|/)(\.git|__pycache__|cache)(/|$)|\.(pyc|pyo|safetensors|pt|pth|onnx|ckpt|gguf)$') "forbidden cache, weight, or runtime payload: $name"
    }
    Assert-True ($rootName -match '^AGPlayer-VoiceClonePlugin-([0-9]+\.[0-9]+\.[0-9]+)-windows-x64$') 'ZIP root name is invalid'
    Assert-True ($archiveFiles.Count -eq $expectedArchiveSet.Count) 'ZIP payload contract is incomplete'
    foreach ($path in $expectedArchiveSet) {
        Assert-True ($archiveFiles.Contains($path)) "required ZIP payload is missing: $path"
    }
    $version = $Matches[1]
}
finally {
    $archive.Dispose()
}

$scratch = Join-Path ([IO.Path]::GetTempPath()) ("agplayer-vc-package-verify-" + [guid]::NewGuid().ToString('N'))
$tempRootPath = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
[void](Assert-NoReparseAncestor $tempRootPath)
$scratch = New-SafeDirectory $scratch $tempRootPath
try {
    Expand-Archive -LiteralPath $zipPath -DestinationPath $scratch
    $root = Join-Path $scratch $rootName
    Assert-True (Test-Path -LiteralPath $root -PathType Container) 'ZIP root did not extract'
    $reparse = @(Get-ChildItem -LiteralPath $root -Recurse -Force | Where-Object {
        ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
    })
    Assert-True ($reparse.Count -eq 0) 'extracted package contains a link or reparse point'

    $required = @(
        'agplayer-voice-clone.json', 'agplayer_voice_clone.dll', 'package-manifest.json', 'SHA256SUMS',
        'qml/VoiceCloneWorkspace.qml', 'registry/models.json', 'config/agplayer-model.example.json',
        'config/plugin-feed.example.json', 'runtime/qwen.lock.json', 'runtime/indextts25.lock.json',
        'runtime/cosyvoice3.lock.json', 'scripts/build-runtime-pack.ps1', 'docs/README.zh-CN.md',
        'docs/ADDING_MODELS.zh-CN.md', 'licenses/THIRD_PARTY_NOTICES.json'
    )
    foreach ($relative in $required) {
        Assert-True (Test-Path -LiteralPath (Join-Path $root $relative) -PathType Leaf) "required package file is missing: $relative"
    }

    $hostManifest = Get-Content -Raw -LiteralPath (Join-Path $root 'agplayer-voice-clone.json') | ConvertFrom-Json
    Assert-True ($hostManifest.schemaVersion -eq 1 -and $hostManifest.pluginId -eq 'agplayer.voice-clone') 'host manifest identity is invalid'
    Assert-True ($hostManifest.version -eq $version -and $hostManifest.availableVersion -eq $version) 'host manifest version is invalid'
    Assert-True ($hostManifest.platform -eq 'windows' -and $hostManifest.architecture -eq 'x86_64') 'host manifest platform is invalid'
    Assert-True ($hostManifest.minimumPlayerVersion -eq '1.0.0' -and $hostManifest.protocolVersion -eq 1) 'host compatibility metadata is invalid'
    Assert-True ($hostManifest.library -eq 'agplayer_voice_clone.dll') 'host plugin library metadata is invalid'

    $allFiles = @(Get-ChildItem -LiteralPath $root -Recurse -Force -File)
    $actualPaths = @($allFiles | ForEach-Object { Get-RelativePath $root $_.FullName })
    $actualSet = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
    foreach ($relative in $actualPaths) { Assert-SafeRelativePath $relative; [void]$actualSet.Add($relative) }
    Assert-True ($actualSet.Count -eq $expectedArchiveSet.Count) 'extracted payload contract is incomplete'
    foreach ($relative in $expectedArchiveSet) {
        Assert-True ($actualSet.Contains($relative)) "required extracted payload is missing: $relative"
    }

    $hashLines = @(Get-Content -LiteralPath (Join-Path $root 'SHA256SUMS') | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    $hashSet = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
    foreach ($line in $hashLines) {
        Assert-True ($line -match '^([a-f0-9]{64})  (.+)$') "invalid SHA256SUMS line: $line"
        $expectedHash = $Matches[1]
        $relative = $Matches[2]
        Assert-SafeRelativePath $relative
        Assert-True ($relative -ne 'SHA256SUMS' -and $hashSet.Add($relative)) "duplicate or recursive SHA256SUMS path: $relative"
        $file = Join-Path $root $relative
        Assert-True (Test-Path -LiteralPath $file -PathType Leaf) "hashed file is missing: $relative"
        $actualHash = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
        Assert-True ($actualHash -eq $expectedHash) "SHA-256 mismatch: $relative"
    }
    Assert-True ($hashSet.Count -eq ($actualSet.Count - 1)) 'SHA256SUMS file count is incomplete'
    foreach ($relative in $actualPaths) {
        if ($relative -ne 'SHA256SUMS') { Assert-True ($hashSet.Contains($relative)) "file is not covered by SHA256SUMS: $relative" }
    }

    $packageManifest = Get-Content -Raw -LiteralPath (Join-Path $root 'package-manifest.json') | ConvertFrom-Json
    Assert-True ($packageManifest.schemaVersion -eq 1 -and $packageManifest.packageId -eq 'agplayer.voice-clone') 'package manifest identity is invalid'
    Assert-True ($packageManifest.version -eq $version -and $packageManifest.platform -eq 'windows' -and $packageManifest.architecture -eq 'x86_64') 'package manifest platform metadata is invalid'
    Assert-True ($packageManifest.signatureStatus -eq 'unsigned-test' -and $null -eq $packageManifest.signature) 'package falsely claims a signature'
    Assert-True (-not $packageManifest.containsModelWeights -and -not $packageManifest.containsRuntimePayloads) 'standalone plugin package declares forbidden payloads'
    $payloadSet = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
    foreach ($entry in @($packageManifest.files)) {
        $relative = [string]$entry.path
        Assert-SafeRelativePath $relative
        Assert-True ($relative -notin @('package-manifest.json', 'SHA256SUMS') -and $payloadSet.Add($relative)) "invalid package manifest path: $relative"
        $file = Join-Path $root $relative
        Assert-True (Test-Path -LiteralPath $file -PathType Leaf) "package manifest file is missing: $relative"
        $info = Get-Item -LiteralPath $file
        Assert-True ([long]$entry.bytes -eq $info.Length) "package manifest byte count mismatch: $relative"
        $actualHash = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
        Assert-True ([string]$entry.sha256 -eq $actualHash) "package manifest SHA-256 mismatch: $relative"
    }
    Assert-True ($payloadSet.Count -eq ($actualSet.Count - 2)) 'package manifest file count is incomplete'
    Assert-True ($payloadSet.Count -eq $expectedPayloadSet.Count) 'package manifest does not match the built-in payload contract'
    foreach ($relative in $expectedPayloadSet) {
        Assert-True ($payloadSet.Contains($relative)) "package manifest omitted required payload: $relative"
    }
    foreach ($relative in $actualPaths) {
        if ($relative -notin @('package-manifest.json', 'SHA256SUMS')) {
            Assert-True ($payloadSet.Contains($relative)) "unexpected package file: $relative"
        }
    }

    $expectedAdapters = @('qwen', 'indextts25', 'cosyvoice3')
    foreach ($adapterId in $expectedAdapters) {
        $adapterRoot = Join-Path $root "adapters/$adapterId/1.0.0"
        $adapter = Get-Content -Raw -LiteralPath (Join-Path $adapterRoot 'adapter.json') | ConvertFrom-Json
        Assert-True ($adapter.adapterId -eq $adapterId -and $adapter.adapterVersion -eq '1.0.0' -and $adapter.protocolVersion -eq 1) "invalid Adapter identity: $adapterId"
        foreach ($launcher in @($adapter.launchers)) {
            $launcherPath = [string]$launcher.path
            Assert-SafeRelativePath $launcherPath
            Assert-True ($launcherPath.StartsWith('workers/', [StringComparison]::Ordinal)) "Adapter launcher is outside workers/: $adapterId"
            Assert-True (Test-Path -LiteralPath (Join-Path $adapterRoot $launcherPath) -PathType Leaf) "Adapter launcher is missing: $adapterId/$launcherPath"
        }
    }

    $expectedLicenseHashes = @{
        'qwen-license' = 'a44a6081c73ad75f0255bb2bb5cab74ef1829565a895a24e53a4f11290ab7655'
        'cosyvoice-license' = 'c71d239df91726fc519c6eb72d318ec65820627232b2f796219e87dcf35d0ab4'
        'index-model-license' = 'cc7da9ea0f8a97ef15ab3bf0389e636ce79ffca1aef5489520796ac87d87a87b'
        'maskgct-readme' = '8dfdbd32898b8b260f6cf63647ac1ae98aa6779f3b49cc4ff32059920e712cdf'
        'w2v-bert-readme' = '478afeacb3b4ce0d86b361f099416d28a7e7673ade31775c0f85f396979b7fc2'
        'campplus-readme' = 'c6025ccc42e290640f44e06f0742c584b99afe738bb819d74889330c692446f9'
        'bigvgan-license' = '90459cd52fc41bd723df7c0c76fac1e4dd60e6bfd644a7e2a93f325bed4f6d95'
        'index-source-license' = '53875532237e0a97b17a41721556ff57e93193fc566e48c3a0febe6e35bb5343'
        'matcha-license' = '874d84104bdc7b301f369b2f2e66b31f07826a67495af909655f3699c857620d'
    }
    $notices = Get-Content -Raw -LiteralPath (Join-Path $root 'licenses/THIRD_PARTY_NOTICES.json') | ConvertFrom-Json
    Assert-True ($notices.schemaVersion -eq 1 -and $notices.packageId -eq 'agplayer.voice-clone') 'license notice identity is invalid'
    Assert-True (-not $notices.packageContainsModelWeights -and -not $notices.packageContainsRuntimePayloads) 'license notice declares forbidden payloads'
    Assert-True (@($notices.references).Count -eq $expectedLicenseHashes.Count) 'license reference set is incomplete'
    $seenLicenseIds = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
    foreach ($reference in @($notices.references)) {
        $licenseId = [string]$reference.id
        Assert-True ($expectedLicenseHashes.ContainsKey($licenseId) -and $seenLicenseIds.Add($licenseId)) "unknown or duplicate license reference: $licenseId"
        Assert-True ([string]$reference.url -match '^https://.+/[0-9a-f]{40}/') "license URL is not immutable: $licenseId"
        Assert-True ([long]$reference.bytes -gt 0) "license byte count is missing: $licenseId"
        Assert-True ([string]$reference.sha256 -eq $expectedLicenseHashes[$licenseId]) "license content hash is wrong: $licenseId"
    }

    $textFiles = @($allFiles | Where-Object { $_.Extension -match '^\.(json|qml|py|ps1|md|txt)$' })
    $allText = ($textFiles | ForEach-Object { Get-Content -Raw -LiteralPath $_.FullName }) -join "`n"
    Assert-True ($allText -notmatch '(?i)VibeVoice') 'cancelled VibeVoice appears in package text'
    Assert-True ($allText -notmatch '(?i)([A-Z]:[\\/](Users|ai|Administrator|Documents|Desktop|source|build)[\\/]|/home/|/Users/)') 'absolute development path appears in package text'
    Assert-True ($allText -notmatch '(?i)(-----BEGIN [A-Z ]*PRIVATE KEY-----|ghp_[A-Za-z0-9]{20,}|AKIA[0-9A-Z]{16}|password\s*[:=])') 'secret material appears in package text'

    if ($PSBoundParameters.ContainsKey('HostProbePath')) {
        Assert-True (-not [string]::IsNullOrWhiteSpace($HostProbePath)) 'HostProbePath is empty'
        $probe = [IO.Path]::GetFullPath($HostProbePath)
        Assert-True (Test-Path -LiteralPath $probe -PathType Leaf) "Host probe is missing: $probe"
        & $probe $root
        Assert-True ($LASTEXITCODE -eq 0) 'existing Host/QPluginLoader rejected the clean extraction'
    }
}
finally {
    if (Test-Path -LiteralPath $scratch) { Remove-SafeTree $scratch $tempRootPath }
}

Write-Host "verified $zipPath"
