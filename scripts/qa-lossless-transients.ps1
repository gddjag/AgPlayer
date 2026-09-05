param(
    [Parameter(Mandatory=$true)][string]$Ffmpeg,
    [string]$OutputDirectory='build/qa/lossless-transients'
)
$ErrorActionPreference='Stop'
$destination=[IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($destination) | Out-Null
$records=[Collections.Generic.List[object]]::new()
function Write-Sample([string]$Name,[string]$Purpose,[string[]]$Arguments) {
    $path=Join-Path $destination $Name
    & $Ffmpeg -nostdin -hide_banner -loglevel error -y @Arguments $path
    if($LASTEXITCODE -ne 0){throw "Failed to generate $Name"}
    $records.Add([ordered]@{name=$Name;purpose=$Purpose;arguments=$Arguments;
        bytes=(Get-Item -LiteralPath $path).Length;
        sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash})
}
# These controls test measured temporal structure and channel handling, not
# recoverability of codec history. Synthetic pre-onset energy is not a lossy source.
$tone='0.4*sin(2*PI*997*t)+0.15*sin(2*PI*7001*t)'
Write-Sample 'in-phase.wav' 'Same active signal in both channels' @('-f','lavfi','-i',"aevalsrc=$tone|$tone`:s=48000:d=3",'-c:a','pcm_s24le')
Write-Sample 'opposite-phase.wav' 'Same spectrum and activity as in-phase despite opposite polarity' @('-f','lavfi','-i',"aevalsrc=$tone|-($tone):s=48000:d=3",'-c:a','pcm_s24le')
Write-Sample 'right-only.wav' 'Silent left channel must not hide active right channel' @('-f','lavfi','-i',"aevalsrc=0|$tone`:s=48000:d=3",'-c:a','pcm_s24le')
$pulse='if(between(mod(t\,0.5)\,0.1\,0.102)\,0.7*sin(2*PI*8000*t)\,0)'
$before='if(between(mod(t\,0.5)\,0.080\,0.097)\,0.05*sin(2*PI*8000*t)\,0)'
Write-Sample 'clean-onsets.wav' 'Abrupt bursts without intentional precursor' @('-f','lavfi','-i',"aevalsrc=$pulse`:s=48000:d=3",'-c:a','pcm_s24le')
Write-Sample 'precursor-onsets.wav' 'Known pre-onset energy; must not alone prove lossy coding' @('-f','lavfi','-i',"aevalsrc=$pulse+$before`:s=48000:d=3",'-c:a','pcm_s24le')
Write-Sample 'steady.wav' 'Continuous stationary tone, no qualified isolated transients' @('-f','lavfi','-i',"aevalsrc=$tone`:s=48000:d=3",'-c:a','pcm_s24le')
Write-Sample 'silence.wav' 'No measurable transients' @('-f','lavfi','-i','anullsrc=r=48000:cl=stereo','-t','3','-c:a','pcm_s24le')
Write-Sample 'onsets.mp3' 'Known MP3 encoding of clean bursts, distinct from hidden-source classification' @('-i',(Join-Path $destination 'clean-onsets.wav'),'-c:a','libmp3lame','-b:a','128k')
Write-Sample 'onsets-mp3.flac' 'MP3 decoded into FLAC; diagnostic, no required verdict shortcut' @('-i',(Join-Path $destination 'onsets.mp3'),'-c:a','flac','-sample_fmt','s32')
[ordered]@{description='Deterministic temporal and channel controls; not an accuracy benchmark';
    ffmpeg=(& $Ffmpeg -version | Select-Object -First 1);samples=$records.ToArray()} |
    ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $destination 'manifest.json') -Encoding utf8
Write-Output "Generated $($records.Count) controls in $destination"
