param([Parameter(Mandatory=$true)][string]$Ffmpeg,
      [string]$CorpusDirectory='build/qa/lossless-corpus',
      [string]$OutputDirectory='build/qa/lossless-heldout')
$ErrorActionPreference='Stop'
$destination=[IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($destination) | Out-Null
$master=[IO.Path]::GetFullPath((Join-Path $CorpusDirectory '01-broadband-96k24.wav'))
$cd=[IO.Path]::GetFullPath((Join-Path $CorpusDirectory '02-source-44k16.wav'))
$records=[Collections.Generic.List[object]]::new()
function Encode-Heldout([string]$Name,[string]$Truth,[string[]]$Arguments){
    $path=Join-Path $destination $Name
    & $Ffmpeg -nostdin -v error -y @Arguments $path
    if($LASTEXITCODE -ne 0){throw "Failed encoding $Name"}
    $records.Add([ordered]@{name=$Name;truth=$Truth;arguments=$Arguments;sha256=(Get-FileHash -LiteralPath $path).Hash})
}
Encode-Heldout 'h-native48k24.wav' 'generated_pcm' @('-i',$master,'-ar','48000','-c:a','pcm_s24le')
Encode-Heldout 'h-expanded32.wav' 'bit_depth_expansion' @('-i',$cd,'-c:a','pcm_s32le')
Encode-Heldout 'h-upsample88k.flac' 'upsample' @('-i',$cd,'-ar','88200','-c:a','flac','-sample_fmt','s32')
Encode-Heldout 'h-upsample192k.flac' 'upsample' @('-i',$cd,'-ar','192000','-c:a','flac','-sample_fmt','s32')
Encode-Heldout 'h-lowpass16k.wav' 'lowpass_not_lossy' @('-i',$master,'-af','lowpass=f=16000:p=2,lowpass=f=16000:p=2','-c:a','pcm_s24le')
$encoders=@(
    @{name='vbr.mp3';arguments=@('-c:a','libmp3lame','-q:a','2')},
    @{name='192k.aac';arguments=@('-c:a','aac','-b:a','192k')},
    @{name='48k.opus';arguments=@('-c:a','libopus','-b:a','48k')})
foreach($encoder in $encoders){
    $encoded=Join-Path $destination $encoder.name
    $arguments=@('-i',$cd)+$encoder.arguments
    & $Ffmpeg -nostdin -v error -y @arguments $encoded
    if($LASTEXITCODE -ne 0){throw "Failed encoding $encoded"}
    Encode-Heldout "h-$($encoder.name)-to-flac.flac" 'lossy_transcode' @('-i',$encoded,'-c:a','flac','-sample_fmt','s32')
    $records[$records.Count-1].encoderArguments=$arguments
    $records[$records.Count-1].encodedSha256=(Get-FileHash -LiteralPath $encoded).Hash
}
[ordered]@{description='Previously unused codec/rate parameters; same synthetic source family, not unseen musical content';
    ffmpeg=(& $Ffmpeg -version | Select-Object -First 1);sourceSha256=(Get-FileHash -LiteralPath $master).Hash;samples=$records.ToArray()} |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $destination 'manifest.json') -Encoding utf8
Write-Output "Generated $($records.Count) held-out parameter samples"
