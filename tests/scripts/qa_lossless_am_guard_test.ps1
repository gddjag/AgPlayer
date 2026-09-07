param(
    [Parameter(Mandatory=$true)][string]$Scanner,
    [Parameter(Mandatory=$true)][string]$Manifest,
    [Parameter(Mandatory=$true)][string]$Output
)
$ErrorActionPreference='Stop'
$samples=@(Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json)
if($samples.Count -ne 5){throw 'Expected the five fixed native AM controls'}
foreach($s in $samples){if((Get-FileHash -LiteralPath $s.path).Hash -ne $s.sha256){throw 'Input hash mismatch'}}
& $Scanner @($samples.path) | Set-Content -LiteralPath $Output -Encoding utf8
if($LASTEXITCODE){throw 'Native AM scan failed'}
$rows=@(Get-Content -LiteralPath $Output -Raw | ConvertFrom-Json)
if($rows.Count -ne $samples.Count){throw 'Missing native AM results'}
foreach($s in $samples){if((Get-FileHash -LiteralPath $s.path).Hash -ne $s.sha256){throw 'Source changed'}}
foreach($row in $rows){
    if($row.error -or $row.verdict -in @('suspected_upsample','suspected_lossy_upsample','suspected_lossy_transcode')){
        throw "Native AM manufactured a source-history verdict: $($row.path): $($row.verdict)"
    }
}
Write-Output 'Five fixed native AM source-history guards passed; one synthetic source group.'
