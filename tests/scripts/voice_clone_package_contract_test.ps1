param(
    [Parameter(Mandatory = $true)][string]$SourceRoot,
    [Parameter(Mandatory = $true)][string]$BuildRoot,
    [Parameter(Mandatory = $true)][string]$HostProbePath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Invoke-ExpectFailure([scriptblock]$Action, [string]$Message) {
    $failed = $false
    try { & $Action } catch { $failed = $true }
    if (-not $failed) { throw $Message }
}

function Invoke-ExpectNativeFailure([scriptblock]$Action, [string]$Message) {
    & $Action
    if ($LASTEXITCODE -eq 0) { throw $Message }
}

function Write-Utf8NoBom([string]$Path, [string]$Content) {
    [IO.File]::WriteAllText($Path, $Content, [Text.UTF8Encoding]::new($false))
}

function New-SelfConsistentExtraFilePackage(
    [string]$OriginalRoot,
    [string]$WorkRoot,
    [string]$RelativePath,
    [string]$Content
) {
    New-Item -ItemType Directory -Path $WorkRoot -Force | Out-Null
    $container = Join-Path $WorkRoot ([guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $container | Out-Null
    Copy-Item -LiteralPath $OriginalRoot -Destination $container -Recurse
    $mutatedRoot = Join-Path $container (Split-Path -Leaf $OriginalRoot)
    $extraPath = Join-Path $mutatedRoot $RelativePath
    New-Item -ItemType Directory -Path (Split-Path -Parent $extraPath) -Force | Out-Null
    Write-Utf8NoBom $extraPath $Content

    $manifestPath = Join-Path $mutatedRoot 'package-manifest.json'
    $manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
    $manifest.files = @(Get-ChildItem -LiteralPath $mutatedRoot -Recurse -Force -File |
        Where-Object { $_.Name -notin @('package-manifest.json', 'SHA256SUMS') } |
        Sort-Object FullName | ForEach-Object {
            [ordered]@{
                path = $_.FullName.Substring($mutatedRoot.Length + 1).Replace('\', '/')
                bytes = $_.Length
                sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            }
        })
    Write-Utf8NoBom $manifestPath ($manifest | ConvertTo-Json -Depth 20)

    $hashLines = @(Get-ChildItem -LiteralPath $mutatedRoot -Recurse -Force -File |
        Where-Object { $_.Name -ne 'SHA256SUMS' } |
        Sort-Object FullName | ForEach-Object {
            $relative = $_.FullName.Substring($mutatedRoot.Length + 1).Replace('\', '/')
            $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            "$hash  $relative"
        })
    Write-Utf8NoBom (Join-Path $mutatedRoot 'SHA256SUMS') (($hashLines -join "`n") + "`n")
    $zipPath = Join-Path $WorkRoot (([guid]::NewGuid().ToString('N')) + '.zip')
    Compress-Archive -LiteralPath $mutatedRoot -DestinationPath $zipPath
    return $zipPath
}

function New-ZipWithEntry([string]$SourceZip, [string]$DestinationZip, [string]$EntryName) {
    Copy-Item -LiteralPath $SourceZip -Destination $DestinationZip
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::Open($DestinationZip, [IO.Compression.ZipArchiveMode]::Update)
    try {
        $entry = $archive.CreateEntry($EntryName)
        $stream = $entry.Open()
        try {
            $bytes = [Text.Encoding]::UTF8.GetBytes('unsafe')
            $stream.Write($bytes, 0, $bytes.Length)
        }
        finally { $stream.Dispose() }
    }
    finally { $archive.Dispose() }
}

$source = [IO.Path]::GetFullPath($SourceRoot)
$build = [IO.Path]::GetFullPath($BuildRoot)
$probe = [IO.Path]::GetFullPath($HostProbePath)
$packageScript = Join-Path $source 'scripts/voice-clone/package-plugin.ps1'
$verifyScript = Join-Path $source 'scripts/voice-clone/verify-plugin-package.ps1'
$cachePath = Join-Path $build 'CMakeCache.txt'
Assert-True (Test-Path -LiteralPath $cachePath -PathType Leaf) 'BuildRoot CMakeCache.txt is missing'
$cacheText = Get-Content -Raw -LiteralPath $cachePath
$isReleaseBuild = $cacheText -match '(?m)^CMAKE_BUILD_TYPE:STRING=Release\s*$'
if (-not $isReleaseBuild) {
    $debugOutput = Join-Path ([IO.Path]::GetTempPath()) ("agplayer-vc-debug-rejection-" + [guid]::NewGuid().ToString('N'))
    try {
        Invoke-ExpectNativeFailure {
            & powershell -NoProfile -ExecutionPolicy Bypass -File $packageScript -BuildRoot $build -OutputRoot $debugOutput -Version '1.0.0'
        } 'Debug plugin build must be rejected by the package script'
    }
    finally {
        if (Test-Path -LiteralPath $debugOutput) { Remove-Item -LiteralPath $debugOutput -Recurse -Force }
    }
    Write-Host 'voice clone Debug package rejection contract passed'
    return
}
$scratch = Join-Path ([IO.Path]::GetTempPath()) ("agplayer-vc-package-contract-" + [guid]::NewGuid().ToString('N'))
$output = Join-Path $scratch 'artifacts'
$expanded = Join-Path $scratch 'expanded'
$junctionPath = $null

New-Item -ItemType Directory -Path $scratch | Out-Null
try {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $packageScript -BuildRoot $build -OutputRoot $output -Version '1.0.0'
    Assert-True ($LASTEXITCODE -eq 0) 'documented standalone package command failed'
    $zip = Join-Path $output 'AGPlayer-VoiceClonePlugin-1.0.0-windows-x64.zip'
    Assert-True (Test-Path -LiteralPath $zip -PathType Leaf) 'standalone plugin ZIP was not created'

    & $verifyScript -PackagePath $zip -HostProbePath $probe
    Expand-Archive -LiteralPath $zip -DestinationPath $expanded
    $rootEntries = @(Get-ChildItem -LiteralPath $expanded -Force)
    Assert-True ($rootEntries.Count -eq 1 -and $rootEntries[0].PSIsContainer) 'ZIP must contain exactly one root directory'
    $root = $rootEntries[0].FullName
    Assert-True ($rootEntries[0].Name -eq 'AGPlayer-VoiceClonePlugin-1.0.0-windows-x64') 'ZIP root is not stable'

    $required = @(
        'agplayer-voice-clone.json', 'agplayer_voice_clone.dll', 'package-manifest.json', 'SHA256SUMS',
        'qml/VoiceCloneWorkspace.qml', 'registry/models.json',
        'registry/downloads/qwen3-tts-0.6b.json', 'registry/downloads/qwen3-tts-1.7b.json',
        'registry/downloads/indextts-2.5.json', 'registry/downloads/fun-cosyvoice3.json',
        'config/agplayer-model.example.json', 'config/plugin-feed.example.json',
        'adapters/qwen/1.0.0/adapter.json', 'adapters/qwen/1.0.0/workers/qwen/worker.py',
        'adapters/qwen/1.0.0/workers/common/agvoice_protocol.py',
        'adapters/indextts25/1.0.0/adapter.json', 'adapters/indextts25/1.0.0/workers/indextts25/worker.py',
        'adapters/indextts25/1.0.0/workers/common/agvoice_protocol.py',
        'adapters/cosyvoice3/1.0.0/adapter.json', 'adapters/cosyvoice3/1.0.0/workers/cosyvoice3/worker.py',
        'adapters/cosyvoice3/1.0.0/workers/common/agvoice_protocol.py',
        'runtime/qwen.lock.json', 'runtime/indextts25.lock.json', 'runtime/cosyvoice3.lock.json',
        'runtime/locks/qwen-win-x64-py312.requirements.txt',
        'scripts/build-runtime-pack.ps1', 'docs/README.zh-CN.md', 'docs/ADDING_MODELS.zh-CN.md',
        'licenses/THIRD_PARTY_NOTICES.json'
    )
    foreach ($relative in $required) {
        Assert-True (Test-Path -LiteralPath (Join-Path $root $relative) -PathType Leaf) "package is missing $relative"
    }

    $manifest = Get-Content -Raw -LiteralPath (Join-Path $root 'package-manifest.json') | ConvertFrom-Json
    Assert-True ($manifest.packageId -eq 'agplayer.voice-clone') 'package identity is wrong'
    Assert-True ($manifest.version -eq '1.0.0' -and $manifest.platform -eq 'windows' -and $manifest.architecture -eq 'x86_64') 'package platform metadata is wrong'
    Assert-True ($manifest.signatureStatus -eq 'unsigned-test') 'test artifact must not claim a signature'
    Assert-True (@($manifest.files).Count -gt 20) 'package hash manifest is incomplete'
    $hashLines = Get-Content -LiteralPath (Join-Path $root 'SHA256SUMS')
    Assert-True ([bool]($hashLines | Where-Object { $_ -match '^[a-f0-9]{64}  package-manifest\.json$' })) 'package Manifest itself must be covered by SHA256SUMS'

    $notices = Get-Content -Raw -LiteralPath (Join-Path $root 'licenses/THIRD_PARTY_NOTICES.json') | ConvertFrom-Json
    $expectedLicenseIds = @('qwen-license', 'cosyvoice-license', 'index-model-license', 'maskgct-readme', 'w2v-bert-readme', 'campplus-readme', 'bigvgan-license', 'index-source-license', 'matcha-license')
    Assert-True (@($notices.references).Count -eq $expectedLicenseIds.Count) 'license reference set is incomplete'
    foreach ($licenseId in $expectedLicenseIds) {
        $reference = @($notices.references | Where-Object { $_.id -eq $licenseId })
        Assert-True ($reference.Count -eq 1) "license reference is missing or duplicated: $licenseId"
        Assert-True ([string]$reference[0].url -match '^https://.+/[0-9a-f]{40}/') "license URL is not immutable: $licenseId"
        Assert-True ([string]$reference[0].sha256 -match '^[0-9a-f]{64}$' -and [long]$reference[0].bytes -gt 0) "license content hash is missing: $licenseId"
    }

    $entryNames = @(Get-ChildItem -LiteralPath $root -Recurse -Force -File | ForEach-Object {
        $_.FullName.Substring($root.Length + 1).Replace('\', '/')
    })
    Assert-True (-not ($entryNames | Where-Object { $_ -match '(?i)(^|/)(\.git|__pycache__|cache)(/|$)|\.(pyc|pyo|safetensors|pt|pth|onnx|ckpt|gguf)$' })) 'package contains cache or model/runtime payloads'
    $allText = ($entryNames | Where-Object { $_ -match '\.(json|qml|py|ps1|md|txt)$' } | ForEach-Object {
        Get-Content -Raw -LiteralPath (Join-Path $root $_)
    }) -join "`n"
    Assert-True ($allText -notmatch '(?i)VibeVoice') 'cancelled VibeVoice leaked into the package'
    Assert-True ($allText -notmatch '(?i)([A-Z]:[\\/](Users|ai|Administrator|Documents|Desktop|source|build)[\\/]|/home/|/Users/)') 'absolute development path leaked into package text'
    Assert-True ($allText -notmatch '(?i)(-----BEGIN [A-Z ]*PRIVATE KEY-----|ghp_[A-Za-z0-9]{20,}|AKIA[0-9A-Z]{16})') 'secret material leaked into package text'

    Invoke-ExpectFailure {
        & $packageScript -SourceRoot $source -BuildRoot $build -OutputRoot (Join-Path $scratch 'empty-feed') -Version '1.0.0' -PublishBaseUrl ''
    } 'empty publish URL must be rejected'
    Invoke-ExpectFailure {
        & $packageScript -SourceRoot $source -BuildRoot $build -OutputRoot (Join-Path $scratch 'http-feed') -Version '1.0.0' -PublishBaseUrl 'http://downloads.agplayer.cn/voice-clone'
    } 'HTTP publish URL must be rejected'

    $publishOutput = Join-Path $scratch 'https-feed'
    & $packageScript -SourceRoot $source -BuildRoot $build -OutputRoot $publishOutput -Version '1.0.0' -PublishBaseUrl 'https://downloads.agplayer.cn/voice-clone'
    $feed = Get-Content -Raw -LiteralPath (Join-Path $publishOutput 'plugin-feed.json') | ConvertFrom-Json
    Assert-True ($feed.packageUrl -eq 'https://downloads.agplayer.cn/voice-clone/AGPlayer-VoiceClonePlugin-1.0.0-windows-x64.zip') 'publish feed package URL is wrong'
    Assert-True ($feed.manifestUrl -eq 'https://downloads.agplayer.cn/voice-clone/AGPlayer-VoiceClonePlugin-1.0.0-windows-x64.manifest.json') 'publish feed manifest URL is wrong'
    Assert-True ($feed.signature.status -eq 'unsigned-test') 'feed must not claim a signature'

    $normalizedFeedOutput = Join-Path $scratch 'normalized-feed'
    & $packageScript -SourceRoot $source -BuildRoot $build -OutputRoot $normalizedFeedOutput -Version '1.0.0' -PublishBaseUrl 'HTTPS://DOWNLOADS.AGPLAYER.CN/voice-clone/'
    $normalizedFeed = Get-Content -Raw -LiteralPath (Join-Path $normalizedFeedOutput 'plugin-feed.json') | ConvertFrom-Json
    Assert-True ($normalizedFeed.packageUrl -eq 'https://downloads.agplayer.cn/voice-clone/AGPlayer-VoiceClonePlugin-1.0.0-windows-x64.zip') 'publish URL was not normalized safely'

    foreach ($badBaseUrl in @(
        'https://user:password@downloads.agplayer.cn/voice-clone',
        'https://downloads.agplayer.cn/voice-clone?channel=test',
        'https://downloads.agplayer.cn/voice-clone#latest',
        'https://127.0.0.1/voice-clone',
        'https://example.com/voice-clone',
        'https://example.com./voice-clone',
        'https://downloads/voice-clone',
        'https://test/voice-clone',
        'https://invalid/voice-clone',
        'https://example/voice-clone',
        'https://-/voice-clone',
        'https://localhost/voice-clone',
        'https://localhost./voice-clone',
        'https://voice.localhost/voice-clone',
        'https://nested.voice.localhost/voice-clone',
        'https://voice.localhost./voice-clone',
        'https://downloads.agplayer.cn/voice-clone/../private'
    )) {
        Invoke-ExpectFailure {
            & $packageScript -SourceRoot $source -BuildRoot $build -OutputRoot (Join-Path $scratch ([guid]::NewGuid().ToString('N'))) -Version '1.0.0' -PublishBaseUrl $badBaseUrl
        } "unsafe publish URL must be rejected: $badBaseUrl"
    }

    $fakeDebugBuild = Join-Path $scratch 'fake-debug-import-build'
    New-Item -ItemType Directory -Path (Join-Path $fakeDebugBuild 'plugins/voice-clone') -Force | Out-Null
    Copy-Item -LiteralPath $cachePath -Destination $fakeDebugBuild
    [IO.File]::WriteAllBytes(
        (Join-Path $fakeDebugBuild 'plugins/voice-clone/agplayer_voice_clone.dll'),
        [Text.Encoding]::ASCII.GetBytes("MZ`0Qt6Cored.dll`0VCRUNTIME140D.dll`0"))
    Invoke-ExpectFailure {
        & $packageScript -SourceRoot $source -BuildRoot $fakeDebugBuild -OutputRoot (Join-Path $scratch 'fake-debug-output') -Version '1.0.0'
    } 'DLL with Debug Qt/runtime imports must be rejected'

    $multiCache = "CMAKE_CONFIGURATION_TYPES:STRING=Debug;Release;RelWithDebInfo`n"
    $fakeMultiBuild = Join-Path $scratch 'fake-multi-build'
    New-Item -ItemType Directory -Path (Join-Path $fakeMultiBuild 'plugins/voice-clone/Release') -Force | Out-Null
    Write-Utf8NoBom (Join-Path $fakeMultiBuild 'CMakeCache.txt') $multiCache
    Copy-Item -LiteralPath (Join-Path $build 'plugins/voice-clone/agplayer_voice_clone.dll') -Destination (Join-Path $fakeMultiBuild 'plugins/voice-clone/Release')
    Invoke-ExpectFailure {
        & $packageScript -SourceRoot $source -BuildRoot $fakeMultiBuild -OutputRoot (Join-Path $scratch 'multi-missing-config') -Version '1.0.0'
    } 'multi-configuration build without explicit Release selection must be rejected'
    & $packageScript -SourceRoot $source -BuildRoot $fakeMultiBuild -OutputRoot (Join-Path $scratch 'multi-release') -Version '1.0.0' -Configuration Release

    $fakeMultiWrongPath = Join-Path $scratch 'fake-multi-wrong-path'
    New-Item -ItemType Directory -Path (Join-Path $fakeMultiWrongPath 'plugins/voice-clone/Debug') -Force | Out-Null
    Write-Utf8NoBom (Join-Path $fakeMultiWrongPath 'CMakeCache.txt') $multiCache
    Copy-Item -LiteralPath (Join-Path $build 'plugins/voice-clone/agplayer_voice_clone.dll') -Destination (Join-Path $fakeMultiWrongPath 'plugins/voice-clone/Debug')
    Invoke-ExpectFailure {
        & $packageScript -SourceRoot $source -BuildRoot $fakeMultiWrongPath -OutputRoot (Join-Path $scratch 'multi-wrong-path-output') -Version '1.0.0' -Configuration Release
    } 'multi-configuration Release packaging must reject a DLL outside the Release target path'

    $packageJunctionTarget = Join-Path $scratch 'package-junction-target'
    New-Item -ItemType Directory -Path $packageJunctionTarget | Out-Null
    $junctionPath = Join-Path $scratch 'package-junction-alias'
    New-Item -ItemType Junction -Path $junctionPath -Target $packageJunctionTarget | Out-Null
    Invoke-ExpectFailure {
        & $packageScript -SourceRoot $source -BuildRoot $build -OutputRoot (Join-Path $junctionPath 'artifacts') -Version '1.0.0'
    } 'package script wrote through an output junction'
    [IO.Directory]::Delete($junctionPath, $false)
    $junctionPath = $null

    foreach ($extraFile in @('notes.txt', 'helper.dll', 'tool.exe')) {
        $selfConsistentZip = New-SelfConsistentExtraFilePackage $root (Join-Path $scratch 'self-consistent') $extraFile 'not allowed'
        Invoke-ExpectFailure {
            & $verifyScript -PackagePath $selfConsistentZip
        } "verifier trusted a self-declared extra payload: $extraFile"
    }

    $dangerousEntries = @(
        "$($rootEntries[0].Name)/CON.txt",
        "$($rootEntries[0].Name)/../escape.txt",
        "$($rootEntries[0].Name)/trailing. ",
        "$($rootEntries[0].Name)/file.txt`:secret"
    )
    foreach ($entryName in $dangerousEntries) {
        $dangerousZip = Join-Path $scratch (([guid]::NewGuid().ToString('N')) + '.zip')
        New-ZipWithEntry $zip $dangerousZip $entryName
        Invoke-ExpectFailure {
            & $verifyScript -PackagePath $dangerousZip
        } "verifier accepted dangerous ZIP entry: $entryName"
    }

    $junctionTarget = Join-Path $scratch 'junction-target'
    New-Item -ItemType Directory -Path $junctionTarget | Out-Null
    Copy-Item -LiteralPath $zip -Destination $junctionTarget
    $junctionPath = Join-Path $scratch 'junction-alias'
    New-Item -ItemType Junction -Path $junctionPath -Target $junctionTarget | Out-Null
    Invoke-ExpectFailure {
        & $verifyScript -PackagePath (Join-Path $junctionPath (Split-Path -Leaf $zip))
    } 'verifier accepted a package path through a junction'
    [IO.Directory]::Delete($junctionPath, $false)
    $junctionPath = $null

    $tamperedRoot = Join-Path $scratch 'tampered-root'
    Copy-Item -LiteralPath $root -Destination $tamperedRoot -Recurse
    New-Item -ItemType File -Path (Join-Path $tamperedRoot 'model.safetensors') | Out-Null
    $tamperedZip = Join-Path $scratch 'tampered.zip'
    Compress-Archive -LiteralPath $tamperedRoot -DestinationPath $tamperedZip
    Invoke-ExpectFailure {
        & $verifyScript -PackagePath $tamperedZip
    } 'verifier must reject model weights and unexpected files'
}
finally {
    if ($null -ne $junctionPath -and (Test-Path -LiteralPath $junctionPath)) {
        [IO.Directory]::Delete($junctionPath, $false)
    }
    $resolvedScratch = [IO.Path]::GetFullPath($scratch)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if ($resolvedScratch.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $resolvedScratch)) {
        Remove-Item -LiteralPath $resolvedScratch -Recurse -Force
    }
}

Write-Host 'voice clone package contract passed'
