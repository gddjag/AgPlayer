param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

$downloadRoot = Join-Path $SourceRoot 'plugins/voice-clone/registry/downloads'
$modelRegistryPath = Join-Path $SourceRoot 'plugins/voice-clone/registry/models.json'
$readmePath = Join-Path $SourceRoot 'docs/voice-clone/README.zh-CN.md'
$addingModelsPath = Join-Path $SourceRoot 'docs/voice-clone/ADDING_MODELS.zh-CN.md'

$expected = [ordered]@{
    'qwen3-tts-0.6b.json' = @{
        ModelId = 'Qwen/Qwen3-TTS-12Hz-0.6B-Base'; AdapterId = 'qwen'; Repository = 'Qwen/Qwen3-TTS-12Hz-0.6B-Base'; FileCount = 13
    }
    'qwen3-tts-1.7b.json' = @{
        ModelId = 'Qwen/Qwen3-TTS-12Hz-1.7B-Base'; AdapterId = 'qwen'; Repository = 'Qwen/Qwen3-TTS-12Hz-1.7B-Base'; FileCount = 13
    }
    'indextts-2.5.json' = @{
        ModelId = 'IndexTeam/IndexTTS-2.5'; AdapterId = 'indextts25'; Repository = 'IndexTeam/IndexTTS-2.5'; FileCount = 32
    }
    'fun-cosyvoice3.json' = @{
        ModelId = 'FunAudioLLM/Fun-CosyVoice3-0.5B-2512'; AdapterId = 'cosyvoice3'; Repository = 'FunAudioLLM/Fun-CosyVoice3-0.5B-2512'; FileCount = 20
    }
}

Assert-True (Test-Path -LiteralPath $downloadRoot -PathType Container) 'Missing download manifest directory'
$actualNames = @(Get-ChildItem -LiteralPath $downloadRoot -File -Filter '*.json' | Select-Object -ExpandProperty Name | Sort-Object)
$expectedNames = @($expected.Keys | Sort-Object)
Assert-True (($actualNames -join '|') -eq ($expectedNames -join '|')) 'Download Registry must contain exactly the four approved manifests'

$hashPattern = '^[0-9a-f]{64}$'
$revisionPattern = '^[0-9a-f]{40}$'
$executablePattern = '\.(exe|dll|com|bat|cmd|ps1|py|pyw|pyd|sh)$'
$allModelIds = @()

foreach ($fileName in $expected.Keys) {
    $spec = $expected[$fileName]
    $manifestPath = Join-Path $downloadRoot $fileName
    $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding utf8 | ConvertFrom-Json

    Assert-True ($manifest.schemaVersion -eq 1) "$fileName has unsupported schemaVersion"
    Assert-True ($manifest.modelId -ceq $spec.ModelId) "$fileName has the wrong modelId"
    Assert-True ($manifest.adapterId -ceq $spec.AdapterId) "$fileName has the wrong adapterId"
    Assert-True ($manifest.revision -match $revisionPattern) "$fileName revision must be an immutable 40-hex commit"
    Assert-True ($manifest.revision -cne 'main') "$fileName must not use main"
    Assert-True ($manifest.source.provider -ceq 'hugging-face') "$fileName must use the official Hugging Face source"
    Assert-True ($manifest.source.repository -ceq $spec.Repository) "$fileName has the wrong official repository"
    Assert-True ($manifest.source.url -ceq "https://huggingface.co/$($spec.Repository)") "$fileName has the wrong official source URL"
    Assert-True (@($manifest.files).Count -eq $spec.FileCount) "$fileName does not contain the complete pinned file graph"
    Assert-True (@($manifest.licenses).Count -gt 0) "$fileName must declare its license set"

    $seenLicenseIds = @{}
    foreach ($license in @($manifest.licenses)) {
        Assert-True ($license.id -cmatch '^[a-z0-9][a-z0-9._-]*$') "$fileName contains an invalid license id"
        Assert-True (-not $seenLicenseIds.ContainsKey($license.id)) "$fileName contains a duplicate license id"
        $seenLicenseIds[$license.id] = $true
        Assert-True (-not [string]::IsNullOrWhiteSpace($license.name)) "$fileName contains an empty license name"
        Assert-True ($license.url -match '^https://(github\.com|huggingface\.co)/') "$fileName contains a non-official license URL"
        Assert-True ($license.revision -cmatch $revisionPattern) "$fileName contains a mutable license revision"
        Assert-True (-not [string]::IsNullOrWhiteSpace($license.spdx)) "$fileName contains an empty SPDX identity"
        Assert-True ($license.requiredAcceptance -is [bool]) "$fileName has an invalid requiredAcceptance type"
        Assert-True ($license.useRestriction -is [string]) "$fileName has an invalid useRestriction type"
    }
    if ($manifest.modelId -cne 'IndexTeam/IndexTTS-2.5') {
        Assert-True (@($manifest.licenses | Where-Object { $_.requiredAcceptance }).Count -eq 0) "$fileName must not enable a license gate"
    }

    $seenPaths = @{}
    [long]$sum = 0
    foreach ($entry in @($manifest.files)) {
        Assert-True (-not [string]::IsNullOrWhiteSpace($entry.path)) "$fileName contains an empty path"
        Assert-True (-not [IO.Path]::IsPathRooted($entry.path)) "$fileName contains an absolute path"
        Assert-True ($entry.path -notmatch '(^|[\\/])\.\.([\\/]|$)') "$fileName contains path traversal"
        Assert-True ($entry.path -notmatch $executablePattern) "$fileName contains executable content: $($entry.path)"
        $key = $entry.path.ToLowerInvariant()
        Assert-True (-not $seenPaths.ContainsKey($key)) "$fileName contains duplicate path: $($entry.path)"
        $seenPaths[$key] = $true
        Assert-True ($entry.url -match '^https://huggingface\.co/[^/]+/[^/]+/resolve/[0-9a-f]{40}/') "$fileName contains an unpinned or non-official file URL"
        Assert-True ($entry.sha256 -cmatch $hashPattern) "$fileName contains an invalid SHA-256"
        Assert-True ($entry.sizeBytes -is [long] -or $entry.sizeBytes -is [int]) "$fileName contains a non-integer size"
        Assert-True ([long]$entry.sizeBytes -ge 0) "$fileName contains a negative size"
        $sum += [long]$entry.sizeBytes
    }
    Assert-True ([long]$manifest.totalBytes -eq $sum) "$fileName totalBytes does not equal its complete file graph"
    $allModelIds += $manifest.modelId
}

$index = Get-Content -LiteralPath (Join-Path $downloadRoot 'indextts-2.5.json') -Raw -Encoding utf8 | ConvertFrom-Json
$indexLicenses = @($index.licenses)
$bilibili = @($indexLicenses | Where-Object { $_.id -ceq 'bilibili-model-use-license' })
$maskGct = @($indexLicenses | Where-Object { $_.id -ceq 'maskgct-cc-by-nc-4.0' })
Assert-True ($bilibili.Count -eq 1 -and $bilibili[0].requiredAcceptance -eq $true) 'IndexTTS must gate the exact bilibili license'
Assert-True ($bilibili[0].url -ceq 'https://huggingface.co/IndexTeam/IndexTTS-2.5/blob/c39ce5ba981572cb187443877ff559dfb246ce63/LICENSE') 'IndexTTS must pin the official bilibili license URL'
Assert-True ($bilibili[0].revision -ceq 'c39ce5ba981572cb187443877ff559dfb246ce63') 'IndexTTS must pin the bilibili license revision'
Assert-True ($maskGct.Count -eq 1 -and $maskGct[0].requiredAcceptance -eq $true) 'IndexTTS must gate the MaskGCT CC-BY-NC-4.0 dependency'
Assert-True ($maskGct[0].spdx -ceq 'CC-BY-NC-4.0') 'MaskGCT must retain its non-commercial SPDX identity'
Assert-True ($maskGct[0].useRestriction -match 'non-commercial') 'MaskGCT must state its non-commercial restriction'
Assert-True ($maskGct[0].url -ceq 'https://huggingface.co/amphion/MaskGCT/blob/265c6cef07625665d0c28d2faafb1415562379dc/README.md') 'MaskGCT must pin its official license declaration'

$auxiliaryRepos = @(
    'facebook/w2v-bert-2.0',
    'amphion/MaskGCT',
    'funasr/campplus',
    'nvidia/bigvgan_v2_22khz_80band_256x'
)
foreach ($repo in $auxiliaryRepos) {
    Assert-True (@($index.files | Where-Object { $_.url -like "https://huggingface.co/$repo/resolve/*" }).Count -gt 0) "IndexTTS is missing official auxiliary asset $repo"
}

$registry = Get-Content -LiteralPath $modelRegistryPath -Raw -Encoding utf8 | ConvertFrom-Json
Assert-True (@($registry.models).Count -eq 4) 'Product Registry must contain exactly four approved models'
foreach ($model in @($registry.models)) {
    Assert-True ($model.revision -match $revisionPattern) "Product Registry model $($model.stableId) is not pinned"
    $download = Get-Content -LiteralPath (Join-Path $downloadRoot (($expected.GetEnumerator() | Where-Object { $_.Value.ModelId -ceq $model.stableId }).Key)) -Raw -Encoding utf8 | ConvertFrom-Json
    Assert-True ($model.revision -ceq $download.revision) "Product Registry and download manifest revision differ for $($model.stableId)"
    Assert-True ($model.license.url -ceq $download.licenses[0].url) "Product Registry and download manifest license URL differ for $($model.stableId)"
    Assert-True ($model.license.revision -ceq $download.licenses[0].revision) "Product Registry and download manifest license revision differ for $($model.stableId)"
}

Assert-True (Test-Path -LiteralPath $readmePath -PathType Leaf) 'Missing Chinese voice-clone user guide'
Assert-True (Test-Path -LiteralPath $addingModelsPath -PathType Leaf) 'Missing no-UI model extension guide'
$readme = Get-Content -LiteralPath $readmePath -Raw -Encoding utf8
$addingModels = Get-Content -LiteralPath $addingModelsPath -Raw -Encoding utf8
$nonCommercialChinese = -join @([char]0x4EC5, [char]0x975E, [char]0x5546, [char]0x4E1A)
$officialProjectChinese = -join @([char]0x5B98, [char]0x65B9, [char]0x9879, [char]0x76EE)
$refreshModelsChinese = -join @([char]0x5237, [char]0x65B0, [char]0x6A21, [char]0x578B)
foreach ($needle in @('Qwen3-TTS 0.6B', 'Qwen3-TTS 1.7B', 'IndexTTS-2.5', 'Fun-CosyVoice3', 'CC-BY-NC-4.0', $nonCommercialChinese, $officialProjectChinese, 'Hugging Face', 'ModelScope')) {
    Assert-True ($readme.Contains($needle)) "User guide is missing: $needle"
}
$commercialRestrictionChinese = -join @([char]0x5546, [char]0x4E1A, [char]0x7528, [char]0x9014, [char]0x7981, [char]0x7528)
Assert-True ($readme.Contains($commercialRestrictionChinese)) 'User guide must explicitly prohibit commercial use without separate authorization'
foreach ($needle in @('agplayer-model.json', 'local-unverified', 'qwen', 'indextts25', 'cosyvoice3', 'Adapter Pack', $refreshModelsChinese)) {
    Assert-True ($addingModels.Contains($needle)) "Model extension guide is missing: $needle"
}

$productText = ((Get-Content -LiteralPath $modelRegistryPath -Raw -Encoding utf8) + "`n" +
                ((Get-ChildItem -LiteralPath $downloadRoot -File | ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw -Encoding utf8 }) -join "`n") + "`n" +
                $readme + "`n" + $addingModels)
$forbiddenName = 'V' + 'ibe' + 'Voice'
Assert-True (-not $productText.Contains($forbiddenName)) 'Canceled model name leaked into product Registry or documentation'

Write-Host 'Voice clone Registry contract passed: 4 pinned official manifests, exact hashes/sizes/licenses, extensibility docs.'
