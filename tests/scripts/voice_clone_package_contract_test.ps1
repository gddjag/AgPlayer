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

$source = [IO.Path]::GetFullPath($SourceRoot)
$build = [IO.Path]::GetFullPath($BuildRoot)
$probe = [IO.Path]::GetFullPath($HostProbePath)
$packageScript = Join-Path $source 'scripts/voice-clone/package-plugin.ps1'
$verifyScript = Join-Path $source 'scripts/voice-clone/verify-plugin-package.ps1'
$scratch = Join-Path ([IO.Path]::GetTempPath()) ("agplayer-vc-package-contract-" + [guid]::NewGuid().ToString('N'))
$output = Join-Path $scratch 'artifacts'
$expanded = Join-Path $scratch 'expanded'

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
    $resolvedScratch = [IO.Path]::GetFullPath($scratch)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if ($resolvedScratch.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $resolvedScratch)) {
        Remove-Item -LiteralPath $resolvedScratch -Recurse -Force
    }
}

Write-Host 'voice clone package contract passed'
