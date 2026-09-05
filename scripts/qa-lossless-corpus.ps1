param(
    [Parameter(Mandatory=$true)][string]$Ffmpeg,
    [string]$OutputDirectory = "build/qa/lossless-corpus"
)
$ErrorActionPreference = 'Stop'
$destination = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($destination) | Out-Null
$records = [Collections.Generic.List[object]]::new()
function Encode-Sample([string]$Name, [string]$Truth, [string[]]$EncodeArguments) {
    $output = Join-Path $destination $Name
    & $Ffmpeg -nostdin -hide_banner -loglevel error -y @EncodeArguments $output
    if ($LASTEXITCODE -ne 0) { throw "Encoding failed: $Name" }
    $records.Add([ordered]@{name=$Name; truth=$Truth; arguments=$EncodeArguments;
        bytes=(Get-Item -LiteralPath $output).Length;
        sha256=(Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash})
}
# Deterministic test signals, not claims about a commercial recording's master.
# The broadband mixture gives codec encoders simultaneous tonal/noisy/transient content.
$signal = 'anoisesrc=color=white:amplitude=0.1:sample_rate=96000:duration=12:seed=1729'
$filter = 'aeval=val(0)*(0.4+0.6*lt(mod(t\,0.7)\,0.12))+0.09*sin(2*PI*440*t)+0.03*sin(2*PI*3127*t)'
Encode-Sample '01-broadband-96k24.wav' 'generated_pcm' @('-f','lavfi','-i',$signal,'-af',$filter,'-ac','2','-c:a','pcm_s24le')
$master = Join-Path $destination '01-broadband-96k24.wav'
Encode-Sample '02-source-44k16.wav' 'generated_pcm' @('-i',$master,'-ar','44100','-c:a','pcm_s16le')
$cd = Join-Path $destination '02-source-44k16.wav'
Encode-Sample '03-expanded-44k24.flac' 'bit_depth_expansion' @('-i',$cd,'-c:a','flac','-sample_fmt','s32')
Encode-Sample '04-upsampled-96k24.flac' 'upsample' @('-i',$cd,'-ar','96000','-c:a','flac','-sample_fmt','s32')
Encode-Sample '05-lowpass-counterexample.wav' 'lowpass_not_lossy' @('-i',$master,'-af','lowpass=f=15000:p=2,lowpass=f=15000:p=2','-c:a','pcm_s24le')
Encode-Sample '06-silence.wav' 'inconclusive' @('-f','lavfi','-i','anullsrc=r=48000:cl=stereo','-t','2','-c:a','pcm_s24le')
Encode-Sample '07-tone.wav' 'inconclusive' @('-f','lavfi','-i','sine=frequency=1000:sample_rate=96000:duration=3','-c:a','pcm_s24le')
foreach ($codec in @(
    @{name='mp3';encoder='libmp3lame';rate='128k'},
    @{name='aac';encoder='aac';rate='128k'},
    @{name='ogg';encoder='libvorbis';rate='96k'},
    @{name='opus';encoder='libopus';rate='96k'})) {
    $encoded = "encoded.$($codec.name)"
    Encode-Sample $encoded 'known_lossy_container' @('-i',$cd,'-c:a',$codec.encoder,'-b:a',$codec.rate)
    Encode-Sample "08-$($codec.name)-to-flac.flac" 'lossy_transcode' @('-i',(Join-Path $destination $encoded),'-c:a','flac','-sample_fmt','s32')
}
Encode-Sample '09-mp3-to-96k.flac' 'lossy_upsample' @('-i',(Join-Path $destination 'encoded.mp3'),'-ar','96000','-c:a','flac','-sample_fmt','s32')
Encode-Sample '10-float32.wav' 'generated_float_pcm' @('-i',$master,'-c:a','pcm_f32le')
$manifest = [ordered]@{description='Deterministic synthetic regression corpus; not a mastering authenticity accuracy benchmark';
    ffmpeg=(& $Ffmpeg -version | Select-Object -First 1); samples=$records.ToArray()}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $destination 'manifest.json') -Encoding utf8
Write-Output "Generated $($records.Count) samples in $destination"
