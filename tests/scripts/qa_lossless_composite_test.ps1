param(
    [Parameter(Mandatory=$true)][string]$Scanner,
    [Parameter(Mandatory=$true)][string]$CorpusRoot,
    [Parameter(Mandatory=$true)][string]$Output
)
$ErrorActionPreference='Stop'
$cases=@(
    @{name='round2-08-aac-256-to-96.flac';expected='suspected_lossy_transcode'},
    @{name='round2-08-44-to-96.flac';expected='inconclusive'},
    @{name='round2-18-control-fir15k.flac';expected='inconclusive'},
    @{name='round2-43-control-fir15k.flac';expected='inconclusive'}
)
$files=@($cases|ForEach-Object {(Resolve-Path -LiteralPath (Join-Path $CorpusRoot $_.name)).Path})
$hashes=@(Get-FileHash -LiteralPath $files)
& $Scanner @files | Set-Content -LiteralPath $Output -Encoding utf8
if($LASTEXITCODE){throw 'Actual audio scan failed'}
$rows=@(Get-Content -LiteralPath $Output -Raw|ConvertFrom-Json)
if($rows.Count -ne $cases.Count){throw 'Missing actual audio results'}
foreach($hash in $hashes){if((Get-FileHash -LiteralPath $hash.Path).Hash -ne $hash.Hash){throw 'Source changed'}}
foreach($case in $cases){
    $row=@($rows|Where-Object {[IO.Path]::GetFileName($_.path) -eq $case.name})
    if($row.Count -ne 1 -or $row[0].error -or $row[0].verdict -ne $case.expected){
        throw "Composite regression: $($case.name), expected $($case.expected), got $($row[0].verdict)"
    }
}
Write-Output 'Conservative chain behavior passed: lossy evidence retained, ambiguous upsampling abstains; corpus truth is unchanged.'
