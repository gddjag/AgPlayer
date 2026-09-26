param(
    [Parameter(Mandatory=$true)][string]$Protocol,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'

function Resolve-ProtocolPath([string]$Value, [string]$BaseDirectory) {
    if ([IO.Path]::IsPathRooted($Value)) {
        return [IO.Path]::GetFullPath($Value)
    }
    return [IO.Path]::GetFullPath((Join-Path $BaseDirectory $Value))
}

function Get-TextSha256([string]$Value) {
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes($Value)
        return ([BitConverter]::ToString(
            $algorithm.ComputeHash($bytes))).Replace('-', '')
    } finally {
        $algorithm.Dispose()
    }
}

$protocolPath = (Resolve-Path -LiteralPath $Protocol).Path
$protocolDirectory = Split-Path -Parent $protocolPath
$definition = Get-Content -LiteralPath $protocolPath -Raw | ConvertFrom-Json
if ([string]$definition.schemaVersion -ne '1') {
    throw "Unsupported protocol schemaVersion: $($definition.schemaVersion)"
}
$corpora = @($definition.corpora)
if ($corpora.Count -eq 0) { throw 'The protocol contains no corpora.' }
$seed = [string]$definition.seed
if ([string]::IsNullOrWhiteSpace($seed)) {
    throw 'The protocol seed must be non-empty for reproducible blinding.'
}

$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $outputPath) {
    if (@(Get-ChildItem -LiteralPath $outputPath -Force).Count -ne 0) {
        throw "Blind output directory must be empty: $outputPath"
    }
} else {
    $null = New-Item -ItemType Directory -Path $outputPath
}

$records = New-Object System.Collections.Generic.List[object]
$blindIds = @{}
for ($corpusIndex = 0; $corpusIndex -lt $corpora.Count; ++$corpusIndex) {
    $corpus = $corpora[$corpusIndex]
    $corpusSourceGroup = [string]$corpus.sourceGroup
    $corpusEvaluationSet = [string]$corpus.evaluationSet
    $manifestPath = Resolve-ProtocolPath ([string]$corpus.manifest) $protocolDirectory
    $sourceRoot = Resolve-ProtocolPath ([string]$corpus.root) $protocolDirectory
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $samples = @($manifest.samples)
    for ($sampleIndex = 0; $sampleIndex -lt $samples.Count; ++$sampleIndex) {
        $sample = $samples[$sampleIndex]
        $sourceGroup = [string]$sample.sourceGroup
        if ([string]::IsNullOrWhiteSpace($sourceGroup)) {
            $sourceGroup = $corpusSourceGroup
        }
        $evaluationSet = [string]$sample.evaluationSet
        if ([string]::IsNullOrWhiteSpace($evaluationSet)) {
            $evaluationSet = [string]$sample.split
        }
        if ([string]::IsNullOrWhiteSpace($evaluationSet)) {
            $evaluationSet = $corpusEvaluationSet
        }
        if ([string]::IsNullOrWhiteSpace($sourceGroup)) {
            throw "Corpus $corpusIndex sample $sampleIndex has no sourceGroup."
        }
        if ([string]::IsNullOrWhiteSpace($evaluationSet)) {
            throw "Corpus $corpusIndex sample $sampleIndex has no evaluationSet or split."
        }
        $originalName = [string]$sample.name
        if ([string]::IsNullOrWhiteSpace($originalName)) {
            throw "Corpus $corpusIndex sample $sampleIndex has no name."
        }
        $sourcePath = [IO.Path]::GetFullPath((Join-Path $sourceRoot $originalName))
        $rootPrefix = $sourceRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
        if (-not $sourcePath.StartsWith(
                $rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Sample escapes its declared root: $originalName"
        }
        if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
            throw "Sample file is missing: $sourcePath"
        }
        $actualHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
        $declaredHash = [string]$sample.sha256
        if (-not [string]::IsNullOrWhiteSpace($declaredHash) -and
            $actualHash -cne $declaredHash.ToUpperInvariant()) {
            throw "Sample hash mismatch: $originalName"
        }
        $opaque = (Get-TextSha256 (
            "$seed|$actualHash|$corpusIndex|$sampleIndex")).Substring(0, 20)
        if ($blindIds.ContainsKey($opaque)) {
            throw "Blind identifier collision: $opaque"
        }
        $blindIds[$opaque] = $true
        $extension = [IO.Path]::GetExtension($originalName).ToLowerInvariant()
        $scanName = "sample-$opaque$extension"
        $destination = Join-Path $outputPath $scanName
        Copy-Item -LiteralPath $sourcePath -Destination $destination
        $copiedHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
        if ($copiedHash -cne $actualHash) {
            throw "Blinded copy hash mismatch: $scanName"
        }
        $records.Add([pscustomobject][ordered]@{
            blindId=$opaque
            scanName=$scanName
            originalName=$originalName
            truth=[string]$sample.truth
            sourceGroup=$sourceGroup
            evaluationSet=$evaluationSet
            sha256=$actualHash
            sourceManifest=$manifestPath
            transformation=[string]$sample.transformation
            encoderArguments=@($sample.encoderArguments)
            processingArguments=@($sample.arguments)
            blinded=$true
        })
    }
}

$orderedRecords = @($records | Sort-Object blindId)
$outputManifest = [ordered]@{
    schemaVersion='2'
    description=[string]$definition.description
    protocol=[ordered]@{
        blinded=$true
        blindingMethod='sha256(seed|contentHash|corpusIndex|sampleIndex), first 20 hex'
        seedSha256=(Get-TextSha256 $seed)
        algorithmVersion=[string]$definition.algorithmVersion
        parameterVersion=[string]$definition.parameterVersion
        coverageMinimums=$definition.coverageMinimums
        sourceEqualWeightDiagnostics=$definition.sourceEqualWeightDiagnostics
    }
    samples=$orderedRecords
}
$manifestDestination = Join-Path $outputPath 'manifest.json'
$outputManifest | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath $manifestDestination -Encoding utf8
[pscustomobject][ordered]@{
    output=$manifestDestination
    samples=$orderedRecords.Count
    sourceGroups=@($orderedRecords | Select-Object -ExpandProperty sourceGroup |
        Sort-Object -Unique).Count
}
