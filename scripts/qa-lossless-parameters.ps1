param(
    [Parameter(Mandatory=$true)][string]$Ffmpeg,
    [string]$InputManifest='build/qa/lossless-sqam/manifest-heldout.json',
    [string]$OutputDirectory='build/qa/lossless-parameters'
)
$ErrorActionPreference='Stop'
$manifestPath=(Resolve-Path -LiteralPath $InputManifest).Path
$sourceRoot=Split-Path $manifestPath
$inputData=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$destination=[IO.Path]::GetFullPath($OutputDirectory)
if([StringComparer]::OrdinalIgnoreCase.Equals($destination,$sourceRoot)){throw 'Use a separate output directory'}
[IO.Directory]::CreateDirectory($destination) | Out-Null
$records=[Collections.Generic.List[object]]::new()
# Fixed before reading any holdout prediction: higher rates and an unseen
# short Opus frame challenge parameter dependence, not just source dependence.
foreach($source in $inputData.samples | Where-Object {$_.name -like '*control-pcm.wav'}) {
    $sourcePath=Join-Path $sourceRoot $source.name
    if((Get-FileHash -LiteralPath $sourcePath).Hash -ne $source.sha256){throw 'Source hash mismatch'}
    foreach($codec in @(
        @{id='mp3-q3';encoder='libmp3lame';extension='mp3';settings=@('-q:a','3')},
        @{id='aac-256';encoder='aac';extension='m4a';settings=@('-b:a','256k')},
        @{id='vorbis-q7';encoder='libvorbis';extension='ogg';settings=@('-q:a','7')},
        @{id='opus-128-5ms';encoder='libopus';extension='opus';settings=@('-b:a','128k','-frame_duration','5')}
    )) {
        $stem=$source.sourceGroup+'-'+$codec.id
        $encoded=Join-Path $destination ($stem+'.'+$codec.extension)
        $name=$stem+'.flac';$output=Join-Path $destination $name
        $arguments=@('-i',$sourcePath,'-map_metadata','-1','-c:a',$codec.encoder)+$codec.settings
        & $Ffmpeg -nostdin -hide_banner -loglevel error -y @arguments $encoded
        if($LASTEXITCODE -ne 0){throw "Encoding failed: $stem"}
        & $Ffmpeg -nostdin -hide_banner -loglevel error -y -i $encoded -map_metadata -1 -c:a flac -sample_fmt s32 $output
        if($LASTEXITCODE -ne 0){throw "Decoding failed: $stem"}
        $records.Add([ordered]@{name=$name;truth='lossy_transcode';sourceGroup=$source.sourceGroup;split='heldout';
            transformation=$codec.id;encoderArguments=$arguments;sourceSha256=$source.sha256;
            sha256=(Get-FileHash -LiteralPath $output).Hash;bytes=(Get-Item -LiteralPath $output).Length})
    }
    if((Get-FileHash -LiteralPath $sourcePath).Hash -ne $source.sha256){throw 'Source changed'}
}
[ordered]@{description='Source-heldout codec parameter challenge; fixed before acceptance scan, no parameter tuning on these results';
    ffmpeg=(& $Ffmpeg -version | Select-Object -First 1);samples=$records.ToArray()} |
    ConvertTo-Json -Depth 8 | Set-Content (Join-Path $destination 'manifest.json') -Encoding utf8
Write-Output "Generated $($records.Count) parameter challenges"
