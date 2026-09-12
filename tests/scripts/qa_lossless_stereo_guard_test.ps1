param(
    [Parameter(Mandatory=$true)][string]$Scanner,
    [Parameter(Mandatory=$true)][string]$Manifest,
    [Parameter(Mandatory=$true)][string]$Output
)
$ErrorActionPreference='Stop'
$cases=@((Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json).files)
if($cases.Count -ne 3 -or @($cases | Where-Object truth -eq 'suspected_lossy_transcode').Count -ne 2 -or
    @($cases | Where-Object truth -eq 'inconclusive').Count -ne 1){throw 'Expected two known compressed-history positives and one native control'}
foreach($case in $cases){
    if((Get-FileHash -LiteralPath $case.path).Hash -ne $case.sha256){throw 'Pre-scan input SHA mismatch'}
}
$paths=@($cases | ForEach-Object path)
& $Scanner @paths | Set-Content -LiteralPath $Output -Encoding utf8
if($LASTEXITCODE){throw 'Scanner failed'}
$rows=@(Get-Content -LiteralPath $Output -Raw | ConvertFrom-Json)
if($rows.Count -ne $cases.Count){throw 'Missing results'}
foreach($case in $cases){
    if((Get-FileHash -LiteralPath $case.path).Hash -ne $case.sha256){throw 'Input changed'}
    $matches=@($rows | Where-Object path -eq $case.path)
    if($matches.Count -ne 1 -or $matches[0].error -or $matches[0].verdict -ne $case.truth){
        throw "Stereo history regression: $($case.path); expected $($case.truth), got $($matches[0].verdict)"
    }
}
'Quiet compressed channel, original u04 excerpt and native control all passed; no whole-recording accuracy claim.'
