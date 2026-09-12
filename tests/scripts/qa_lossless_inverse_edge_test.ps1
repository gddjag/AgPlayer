param(
 [Parameter(Mandatory=$true)][string]$Scanner,
 [Parameter(Mandatory=$true)][string]$CorpusRoot,
 [Parameter(Mandatory=$true)][string]$Output
)
$ErrorActionPreference='Stop'
$files=@('freepats-a5-aac256-to96.flac','freepats-a5-44-to-96.flac')|ForEach-Object {(Resolve-Path (Join-Path $CorpusRoot $_)).Path}
$hashes=@(Get-FileHash -LiteralPath $files)
& $Scanner @files | Set-Content -LiteralPath $Output -Encoding utf8
if($LASTEXITCODE){throw 'Actual inverse audio scan failed'}
$rows=@(Get-Content -LiteralPath $Output -Raw|ConvertFrom-Json)
foreach($h in $hashes){if((Get-FileHash -LiteralPath $h.Path).Hash -ne $h.Hash){throw 'Source changed'}}
if($rows.Count -ne 2 -or $rows[0].verdict -ne 'suspected_lossy_transcode' -or $rows[1].verdict -ne 'inconclusive'){
 throw "Inverse-edge regression: $($rows.verdict -join ', ')"
}
Write-Output 'Inverse lossy evidence retained; PCM grid abstains under the low-error policy. Known transformation truth remains unchanged.'
