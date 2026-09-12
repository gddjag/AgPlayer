param(
    [Parameter(Mandatory=$true)][string]$Ffmpeg,
    [Parameter(Mandatory=$true)][string]$ArchivePath,
    [string]$OutputDirectory='build/qa/round3-opus-heldout',
    [ValidateSet('opus-d01','hybrid-d02')][string]$Set='opus-d01'
)
$ErrorActionPreference='Stop'
$archive=(Resolve-Path -LiteralPath $ArchivePath).Path
$ffmpegPath=(Resolve-Path -LiteralPath $Ffmpeg).Path
$expected='7D6FCD0FC42354637291792534B61BF129612F221F8EFEF97B62E8942A8686AA'
if((Get-FileHash $archive).Hash -ne $expected){throw 'Unreviewed SQAM archive'}
# Locked before round-3 predictions. All variants of a track remain together.
$tracks=@('09','12','15','19','23','29','46','54')
$previous=@('08','10','11','13','16','18','20','21','22','25','27','28','31','32','35','36','39','40','41','43','44','45','47','48','49','50','51','55','60','65')
if($Set -eq 'hybrid-d02'){
    $previous+=$tracks
    $tracks=@('14','17','24','26','30','33','34','37','38','42','52','53','56','57','58','59')
}
$rate=if($Set -eq 'hybrid-d02'){44100}else{48000}
$durations=if($Set -eq 'hybrid-d02'){@('2.5')}else{@('2.5','5','10','20')}
if(@($tracks | Where-Object {$_ -in $previous}).Count){throw 'Source leakage'}
$out=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path $out){throw 'Do not overwrite a held-out corpus'}
$null=New-Item -ItemType Directory -Path $out
$null=New-Item -ItemType Directory -Path "$out/sources","$out/encoded"
$samples=[Collections.Generic.List[object]]::new()
function Encode([string[]]$Arguments){
    & $ffmpegPath -nostdin -hide_banner -loglevel error @Arguments
    if($LASTEXITCODE){throw 'Corpus generation failed'}
}
function Record([string]$Name,[string]$Truth,[string[]]$Arguments){
    $samples.Add([ordered]@{name=$Name;truth=$Truth;sourceGroup="EBU-SQAM-track-$track";
        evaluationSet='heldout';arguments=$Arguments;sha256=(Get-FileHash "$out/$Name").Hash})
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=[IO.Compression.ZipFile]::OpenRead($archive)
try {
    foreach($track in $tracks){
        $entry=$zip.GetEntry("$track.flac")
        if(!$entry){throw "Missing source $track"}
        $source="$out/sources/$track.flac"
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$source,$false)
        $pcm="round3-$track-pcm$rate.wav"
        $argsPcm=@('-ss','1','-i',$source,'-t','12','-map_metadata','-1','-ar',"$rate",'-c:a','pcm_s24le',"$out/$pcm")
        Encode $argsPcm
        Record $pcm 'inconclusive' $argsPcm
        $fir="round3-$track-fir15k.flac"
        $argsFir=@('-i',"$out/$pcm",'-f','lavfi','-i',"sinc=r=${rate}:lp=15000:lptaps=1023",
            '-filter_complex','[0:a][1:a]afir=gtype=none:irfmt=mono:precision=double',
            '-t','12','-map_metadata','-1','-c:a','flac','-sample_fmt','s32',"$out/$fir")
        Encode $argsFir
        Record $fir 'lowpass_not_lossy' $argsFir
        foreach($duration in $durations){
            $encoded="$out/encoded/$track-$duration.opus"
            $encodeArgs=@('-i',"$out/$pcm",'-map_metadata','-1','-c:a','libopus',
                '-b:a','128k','-vbr','off','-application','audio','-frame_duration',$duration,$encoded)
            Encode $encodeArgs
            $name="round3-$track-opus128-cbr-$duration.flac"
            $decodeArgs=@('-i',$encoded,'-map_metadata','-1','-c:a','flac','-sample_fmt','s32',"$out/$name")
            Encode $decodeArgs
            Record $name 'lossy_transcode' ($encodeArgs + @('THEN_DECODE') + $decodeArgs)
        }
        if($Set -eq 'hybrid-d02'){
            foreach($codec in @(@{name='320';args=@('-b:a','320k')},@{name='256';args=@('-b:a','256k')},
                               @{name='q3';args=@('-q:a','3')},@{name='q0';args=@('-q:a','0')})){
                $encoded="$out/encoded/$track-mp3-$($codec.name).mp3"
                $encodeArgs=@('-i',"$out/$pcm",'-map_metadata','-1','-c:a','libmp3lame')+$codec.args+@($encoded)
                Encode $encodeArgs
                $name="round3-$track-mp3-$($codec.name).flac"
                $decodeArgs=@('-i',$encoded,'-map_metadata','-1','-c:a','flac','-sample_fmt','s32',"$out/$name")
                Encode $decodeArgs
                Record $name 'lossy_transcode' ($encodeArgs+@('THEN_DECODE')+$decodeArgs)
            }
        }
    }
} finally {$zip.Dispose()}
if($samples.Count -ne $(if($Set -eq 'hybrid-d02'){112}else{48})){throw 'Incomplete corpus'}
[ordered]@{schemaVersion='1';description="Round3 locked $Set parameter challenge; not cross-corpus certification";
    sourceUrl='https://qc.ebu.io/testmaterials/523/';archiveSha256=$expected;
    provenance='SQAM pre-CD history unknown; PCM controls are unscored, FIR controls exclude codec processing only';
    license='Local R&D only; do not redistribute or package audio';
    sourceLock=$tracks;excludedSources=$previous;parameterLock="128k CBR Opus $($durations -join '/')ms; hybrid-d02 adds MP3 320k/256k/q3/q0; 1s start, 12s duration";
    ffmpeg=(& $ffmpegPath -version | Select-Object -First 1);
    sources=@(Get-FileHash "$out/sources/*.flac");samples=$samples.ToArray()} |
    ConvertTo-Json -Depth 15 | Set-Content "$out/manifest.json" -Encoding utf8
Write-Output "Prepared $($samples.Count) locked samples; no analyzer has been run."
