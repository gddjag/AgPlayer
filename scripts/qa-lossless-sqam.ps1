param(
    [Parameter(Mandatory=$true)][string]$Ffmpeg,
    [string]$OutputDirectory = 'build/qa/lossless-sqam',
    [string]$ArchivePath = '',
    [ValidateRange(8,20)][int]$Seconds = 12
)
$ErrorActionPreference = 'Stop'
$destination = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($destination) | Out-Null
$sourceDirectory = Join-Path $destination 'sources'
$encodedDirectory = Join-Path $destination 'encoded'
[IO.Directory]::CreateDirectory($sourceDirectory) | Out-Null
[IO.Directory]::CreateDirectory($encodedDirectory) | Out-Null
$downloadUrl = 'https://qc.ebu.io/testmaterials/523/1/download/'
$expectedArchiveHash = '7D6FCD0FC42354637291792534B61BF129612F221F8EFEF97B62E8942A8686AA'
if (!$ArchivePath) { $ArchivePath = Join-Path $destination 'ebu-sqam.zip' }
if (!(Test-Path -LiteralPath $ArchivePath)) {
    & curl.exe -fL --silent --show-error --max-time 180 $downloadUrl -o $ArchivePath
    if ($LASTEXITCODE -ne 0) { throw 'Official EBU SQAM download failed' }
}
$archiveFile = (Resolve-Path -LiteralPath $ArchivePath).Path
if ((Get-FileHash -LiteralPath $archiveFile).Hash -ne $expectedArchiveHash) {
    throw 'EBU archive differs from the reviewed version; inspect provenance before updating its hash'
}
# Track-level split is fixed before any analyzer results are observed. Every
# derived encoding of one track stays in the same split. These are CD test
# recordings, not independently proven native capture masters.
$tracks = @(
    @{id='10';label='Violoncello';split='heldout'},
    @{id='21';label='Trumpet';split='development'},
    @{id='27';label='Castanets';split='development'},
    @{id='31';label='Cymbal';split='heldout'},
    @{id='35';label='Glockenspiel';split='development'},
    @{id='40';label='Harpsichord';split='development'},
    @{id='44';label='Soprano';split='development'},
    @{id='47';label='Bass voice';split='heldout'},
    @{id='49';label='Female English speech';split='development'},
    @{id='50';label='Male English speech';split='heldout'}
)
$sources = [Collections.Generic.List[object]]::new()
$samples = [Collections.Generic.List[object]]::new()
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($archiveFile)
try {
    foreach ($track in $tracks) {
        $entry = $archive.GetEntry($track.id + '.flac')
        if (!$entry) { throw "Missing reviewed SQAM track $($track.id)" }
        $path = Join-Path $sourceDirectory ($track.id + '.flac')
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $path, $true)
        $sources.Add([ordered]@{ sourceGroup=('EBU-SQAM-track-' + $track.id);
            track=$track.id; description=$track.label; split=$track.split;
            path=$path; archiveEntry=$entry.FullName; url=$downloadUrl;
            sha256=(Get-FileHash -LiteralPath $path).Hash; bytes=(Get-Item -LiteralPath $path).Length;
            excerptStartSeconds=1; excerptDurationSeconds=$Seconds;
            provenance='EBU SQAM CD lossless FLAC; original pre-CD production history is not established' })
    }
} finally { $archive.Dispose() }
function Invoke-Encode([string]$Path, [string[]]$Arguments) {
    & $Ffmpeg -nostdin -hide_banner -loglevel error -y @Arguments $Path
    if ($LASTEXITCODE -ne 0) { throw "Encoding failed: $Path" }
}
function Add-Sample($Source, [string]$Suffix, [string]$Truth, [string]$Transformation, [string[]]$Arguments) {
    $name = 'sqam-' + $Source.track + '-' + $Suffix
    $path = Join-Path $destination $name
    Invoke-Encode $path $Arguments
    $samples.Add([ordered]@{ name=$name; truth=$Truth; sourceGroup=$Source.sourceGroup;
        split=$Source.split; transformation=$Transformation; arguments=$Arguments;
        sha256=(Get-FileHash -LiteralPath $path).Hash; bytes=(Get-Item -LiteralPath $path).Length })
    return $path
}
foreach ($source in $sources) {
    $pcm = Add-Sample $source 'control-pcm.wav' 'inconclusive' 'No newly introduced lossy codec; original capture provenance unknown' @(
        '-ss','1','-i',$source.path,'-t',"$Seconds",'-map_metadata','-1','-c:a','pcm_s16le')
    foreach ($codec in @(
        @{name='mp3';extension='mp3';encoder='libmp3lame';settings=@('-b:a','128k')},
        @{name='aac';extension='m4a';encoder='aac';settings=@('-b:a','128k')},
        @{name='vorbis';extension='ogg';encoder='libvorbis';settings=@('-q:a','4')},
        @{name='opus';extension='opus';encoder='libopus';settings=@('-b:a','96k')})) {
        $encoded = Join-Path $encodedDirectory ($source.track + '.' + $codec.extension)
        $encodeArguments = @('-i',$pcm,'-map_metadata','-1','-c:a',$codec.encoder) + $codec.settings
        Invoke-Encode $encoded $encodeArguments
        $null = Add-Sample $source ($codec.name + '-hidden.flac') 'lossy_transcode' (
            $codec.encoder + ' ' + ($codec.settings -join ' ') + ' -> FLAC; Opus internally uses 48 kHz') @(
            '-i',$encoded,'-map_metadata','-1','-c:a','flac','-sample_fmt','s32')
        $samples[$samples.Count-1].encoderArguments = $encodeArguments
        $samples[$samples.Count-1].intermediateSha256 = (Get-FileHash -LiteralPath $encoded).Hash
    }
    $null = Add-Sample $source '44-to-96.flac' 'upsample' 'Known 44.1 kHz CD PCM -> 96 kHz; no lossy codec introduced' @(
        '-i',$pcm,'-ar','96000','-c:a','flac','-sample_fmt','s32')
    $null = Add-Sample $source 'control-fir15k.flac' 'lowpass_not_lossy' '1023-tap linear phase 15 kHz FIR; mastering control, no lossy codec introduced' @(
        '-i',$pcm,'-f','lavfi','-i','sinc=r=44100:lp=15000:lptaps=1023',
        '-filter_complex','[0:a][1:a]afir=gtype=none:irfmt=mono:precision=double',
        '-t',"$Seconds",'-c:a','flac','-sample_fmt','s32')
    $null = Add-Sample $source 'control-dither.flac' 'inconclusive' 'Gain 0.91 followed by triangular dither to 16-bit; no lossy codec introduced' @(
        '-i',$pcm,'-af','volume=0.91:precision=double,aresample=osf=s16:dither_method=triangular',
        '-c:a','flac','-sample_fmt','s16')
}
foreach ($source in $sources) {
    if ((Get-FileHash -LiteralPath $source.path).Hash -ne $source.sha256) { throw 'Source changed during encoding' }
}
$manifest = [ordered]@{
    description='EBU SQAM local R&D corpus: 10 source tracks, source-group split; conservative original PCM controls, not native-master ground truth';
    sourceUrl='https://qc.ebu.io/testmaterials/523/'; downloadUrl=$downloadUrl;
    license='EBU: not for commercial purposes other than as an R&D tool; local QA only, no redistribution or packaging';
    archiveSha256=$expectedArchiveHash; archiveBytes=(Get-Item -LiteralPath $archiveFile).Length;
    splitPolicy='Track IDs 10,31,47,50 held out before evaluation; all derivatives remain grouped. Six development tracks, four heldout tracks; not 80 independent recordings.';
    ffmpeg=(& $Ffmpeg -version | Select-Object -First 1); sources=$sources.ToArray(); samples=$samples.ToArray()
}
$manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $destination 'manifest.json') -Encoding utf8
foreach ($split in @('development','heldout')) {
    $splitManifest = [ordered]@{} + $manifest
    $splitManifest.description += '; split=' + $split
    $splitManifest.samples = @($samples | Where-Object { $_.split -eq $split })
    $splitManifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $destination ('manifest-' + $split + '.json')) -Encoding utf8
}
Write-Output "Generated $($samples.Count) samples from $($sources.Count) SQAM source tracks in $destination"
