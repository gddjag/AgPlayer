param(
    [Parameter(Mandatory=$true)][string]$Scanner,
    [Parameter(Mandatory=$true)][string]$Manifest,
    [Parameter(Mandatory=$true)][string]$Output
)
$ErrorActionPreference = 'Stop'
# The manifest must describe the five native-rate AM controls, not recordings
# with unknown provenance. Hashes bind this regression to the reviewed fixtures.
$samples = @(Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json)
if ($samples.Count -ne 5) { throw 'Expected five fixed native AM controls' }
foreach ($sample in $samples) {
    if ((Get-FileHash -LiteralPath $sample.path -Algorithm SHA256).Hash -ne $sample.sha256) {
        throw "AM fixture hash mismatch: $($sample.path)"
    }
}
& $Scanner @($samples.path) | Set-Content -LiteralPath $Output -Encoding utf8
if ($LASTEXITCODE) { throw 'Native AM scanner failed' }
$rows = @(Get-Content -LiteralPath $Output -Raw | ConvertFrom-Json)
if ($rows.Count -ne $samples.Count) { throw 'Missing native AM results' }
foreach ($sample in $samples) {
    if ((Get-FileHash -LiteralPath $sample.path -Algorithm SHA256).Hash -ne $sample.sha256) {
        throw 'Native AM input changed during scanning'
    }
}
foreach ($row in $rows) {
    if ($row.error -or $row.verdict -ne 'inconclusive') {
        throw "Native AM must abstain from source history: $($row.path): $($row.verdict)"
    }
}
# Three low-amplitude controls must still expose the measured periodicity.
# Silencing the measurement instead of correcting its causal interpretation
# must not make this guard pass.
if (@($rows | Where-Object {
    $_.resamplingPhaseCoherence -gt 0.995 -and $_.resamplingPhaseStrength -gt 0.02
}).Count -ne 3) { throw 'Expected preserved periodic AM measurements' }
'Five fixed native AM guards passed (one synthetic source group).'
