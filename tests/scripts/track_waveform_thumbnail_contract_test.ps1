param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'

$provider = Join-Path $SourceRoot 'qt/src/track_waveform_thumbnail_provider.cpp'
$header = Join-Path $SourceRoot 'qt/src/track_waveform_thumbnail_provider.hpp'
if (-not (Test-Path -LiteralPath $provider -PathType Leaf)) {
    throw "Missing thumbnail provider: $provider"
}
if (-not (Test-Path -LiteralPath $header -PathType Leaf)) {
    throw "Missing thumbnail provider header: $header"
}

$providerText = Get-Content -Raw -LiteralPath $provider
$headerText = Get-Content -Raw -LiteralPath $header
$allText = $providerText + "`n" + $headerText

$required = @(
    'WaveformCache::key_for\s*\(',
    'WaveformCache::load_v2\s*\('
)
foreach ($pattern in $required) {
    if ($providerText -notmatch $pattern) {
        throw "Missing required cache-only call: $pattern"
    }
}

$forbidden = @(
    'ag_track_analysis\s*\(',
    'ag_track_analysis_with_aggregation\s*\(',
    'WaveformProvider',
    'prefetch',
    'avformat_open_input\s*\(',
    'avcodec_',
    'swr_',
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
