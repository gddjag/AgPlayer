param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'

$provider = Join-Path $SourceRoot 'qt/src/track_waveform_thumbnail_provider.cpp'
$header = Join-Path $SourceRoot 'qt/src/track_waveform_thumbnail_provider.hpp'
$cache = Join-Path $SourceRoot 'core/src/waveform_cache.cpp'
$cacheHeader = Join-Path $SourceRoot 'core/src/waveform_cache.hpp'
if (-not (Test-Path -LiteralPath $provider -PathType Leaf)) {
    throw "Missing thumbnail provider: $provider"
}
if (-not (Test-Path -LiteralPath $header -PathType Leaf)) {
    throw "Missing thumbnail provider header: $header"
}
if (-not (Test-Path -LiteralPath $cache -PathType Leaf)) {
    throw "Missing waveform cache source: $cache"
}
if (-not (Test-Path -LiteralPath $cacheHeader -PathType Leaf)) {
    throw "Missing waveform cache header: $cacheHeader"
}

$providerText = Get-Content -Raw -LiteralPath $provider
$headerText = Get-Content -Raw -LiteralPath $header
$allText = $providerText + "`n" + $headerText
$cacheText = (Get-Content -Raw -LiteralPath $cache) + "`n" +
    (Get-Content -Raw -LiteralPath $cacheHeader)
$cacheOnlyBoundaryText = $allText + "`n" + $cacheText

$required = @(
    'WaveformCache::key_for\s*\(',
    'WaveformCache::load_v4\s*\(',
    'quantizeBandEnergy\s*\(data\.bass,\s*data\.mix\)',
    'quantizeBandEnergy\s*\(data\.mid,\s*data\.mix\)',
    'quantizeBandEnergy\s*\(data\.high,\s*data\.mix\)'
)
foreach ($pattern in $required) {
    if ($providerText -notmatch $pattern) {
        throw "Missing required cache-only call: $pattern"
    }
}

# This source boundary is the authoritative proof that list thumbnails cannot
# invoke analysis/decoding or disguise that behavior behind a diagnostic value.
$analysisOrDecodeForbidden = @(
    'analysisCalls',
    'ag_track_analysis(?:_with_aggregation)?\s*\(',
    'WaveformProvider',
    'WaveformAnalyzer',
    '\bDecoder\b',
    '\bQAudioDecoder\b',
    '\bQMediaPlayer\b',
    'avformat_',
    'avcodec_',
    'swr_'
)
foreach ($pattern in $analysisOrDecodeForbidden) {
    if ($cacheOnlyBoundaryText -match $pattern) {
        throw "Forbidden thumbnail analysis/decode dependency: $pattern"
    }
}

$forbidden = @(
    'prefetch',
    'WaveformCache::load\s*\(',
    'WaveformCache::save(?:_v2)?\s*\(',
    'QSaveFile',
    '\bQFile\b',
    'std::(?:o|f)stream',
    'mkpath\s*\(',
    '\bQImage\b',
    '\bQPixmap\b',
    '\bQSGTexture\b',
    'new\s+QThread',
    'QThread::create\s*\(',
    'std::thread',
    '\bqHash\s*\(',
    'QRandomGenerator',
    '\brand\s*\('
)
foreach ($pattern in $forbidden) {
    if ($allText -match $pattern) {
        throw "Forbidden thumbnail dependency or side effect: $pattern"
    }
}

Write-Host 'Track waveform thumbnail cache-only contract passed.'
