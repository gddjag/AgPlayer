param(
    [Parameter(Mandatory=$true)][string]$Scanner,
    [Parameter(Mandatory=$true)][string[]]$GuardFiles,
    [Parameter(Mandatory=$true)][string]$Output
)
$ErrorActionPreference='Stop'
$paths=@($GuardFiles | ForEach-Object {(Resolve-Path -LiteralPath $_).Path})
$hashes=@($paths | ForEach-Object {(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash})
& $Scanner @paths > $Output
if($LASTEXITCODE){throw 'Guard scanner failed'}
$rows=@(Get-Content -LiteralPath $Output -Raw | ConvertFrom-Json)
if($rows.Count -ne $paths.Count){throw 'Missing guard result'}
foreach($row in $rows){
    if($row.error -or $row.verdict -ne 'inconclusive'){
        throw "Source-uncertain filtered PCM must abstain: $($row.path): $($row.verdict)"
    }
}
for($i=0;$i -lt $paths.Count;$i++){
    if((Get-FileHash -LiteralPath $paths[$i] -Algorithm SHA256).Hash -ne $hashes[$i]){
        throw 'Guard source was modified'
    }
}
'Filtered PCM provenance guards passed.'
