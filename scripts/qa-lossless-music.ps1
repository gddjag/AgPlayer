param(
    [Parameter(Mandatory=$true)][string]$Ffmpeg,
    [Parameter(Mandatory=$true)][string]$SourceDsf,
    [string]$OutputDirectory = 'build/qa/lossless-music'
)
$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath $SourceDsf).Path
$sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$destination = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($destination) | Out-Null
$records = [Collections.Generic.List[object]]::new()
function Encode-Sample([string]$Name, [string]$Truth, [string[]]$EncodeArguments) {
    $output = Join-Path $destination $Name
    if ([StringComparer]::OrdinalIgnoreCase.Equals($output, $source)) { throw 'Output would overwrite source' }
    & $Ffmpeg -nostdin -hide_banner -loglevel error -y @EncodeArguments $output
    if ($LASTEXITCODE -ne 0) { throw "Encoding failed: $Name" }
    $records.Add([ordered]@{name=$Name; truth=$Truth; arguments=$EncodeArguments;
        bytes=(Get-Item -LiteralPath $output).Length;
        sha256=(Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash})
}
# This is a controlled codec test of real piano content, not a native-master claim.
# Publisher: https://www.decaud.io/dsd.html ; CC0 Open Goldberg Variations.
# The source is explicitly 24/96 PCM -> eighth-order delta-sigma -> DSD64.
# The two low-rate references are conservative controls: conversion provenance
# is known, but the PCM/DSD history is not generally recoverable from samples.
Encode-Sample 'reference-44k16.wav' 'reference_only' @('-ss','20','-i',$source,'-t','20','-ar','44100','-c:a','pcm_s16le')
Encode-Sample 'reference-48k24.wav' 'reference_only' @('-ss','45','-i',$source,'-t','20','-ar','48000','-c:a','pcm_s24le')
$references = $records.ToArray()
$records.Clear()
$cd = Join-Path $destination 'reference-44k16.wav'
$pcm48 = Join-Path $destination 'reference-48k24.wav'
Encode-Sample 'music-44-to-48.flac' 'upsample' @('-i',$cd,'-ar','48000','-c:a','flac','-sample_fmt','s32')
Encode-Sample 'music-44-to-96.flac' 'upsample' @('-i',$cd,'-ar','96000','-c:a','flac','-sample_fmt','s32')
Encode-Sample 'music-48-to-192.flac' 'upsample' @('-i',$pcm48,'-ar','192000','-c:a','flac','-sample_fmt','s32')
foreach ($codec in @(
    @{name='mp3-vbr.mp3';encoder='libmp3lame';settings=@('-q:a','2')},
    @{name='mp3-320.mp3';encoder='libmp3lame';settings=@('-b:a','320k')},
    @{name='aac-128.m4a';encoder='aac';settings=@('-b:a','128k')},
    @{name='aac-256.m4a';encoder='aac';settings=@('-b:a','256k')},
    @{name='opus-64.opus';encoder='libopus';settings=@('-b:a','64k')},
    @{name='vorbis-160.ogg';encoder='libvorbis';settings=@('-b:a','160k')})) {
    Encode-Sample $codec.name 'known_lossy_container' (@('-i',$cd,'-c:a',$codec.encoder) + $codec.settings)
    Encode-Sample ($codec.name + '.flac') 'lossy_transcode' @('-i',(Join-Path $destination $codec.name),'-c:a','flac','-sample_fmt','s32')
}
Encode-Sample 'music-aac-to-96.flac' 'lossy_upsample' @('-i',(Join-Path $destination 'aac-128.m4a'),'-ar','96000','-c:a','flac','-sample_fmt','s32')
if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $sourceHash) { throw 'Source hash changed' }
[ordered]@{
    description='Real piano controlled transformations; a single musical work, not a genre-diverse or native-master accuracy benchmark';
    source=[ordered]@{path=$source;sha256=$sourceHash;publisher='https://www.decaud.io/dsd.html';provenance='24/96 PCM -> DSD64; CC0 Open Goldberg Variations'};
    ffmpeg=(& $Ffmpeg -version | Select-Object -First 1); references=$references; samples=$records.ToArray()
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $destination 'manifest.json') -Encoding utf8
Write-Output "Generated $($records.Count) real-music samples in $destination"
