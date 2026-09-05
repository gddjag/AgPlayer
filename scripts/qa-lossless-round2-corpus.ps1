param(
    [Parameter(Mandatory=$true)][string]$Ffmpeg,
    [string]$ArchivePath='build/qa/lossless-sqam/ebu-sqam.zip',
    [string]$OutputDirectory='build/qa/lossless-round2-corpus',
    [string]$ProtocolPath='build/qa/lossless-round2-c01-protocol.json',
    [string]$BlindDirectory='build/qa/evaluation-c01',
    [ValidateRange(8,20)][int]$Seconds=12,
    [string]$AlgorithmVersion='lossless-1.4',
    [string]$ParameterVersion='lossless-params-6'
)

$ErrorActionPreference = 'Stop'
$expectedArchiveHash =
    '7D6FCD0FC42354637291792534B61BF129612F221F8EFEF97B62E8942A8686AA'
$excerptStartSeconds = 1
$downloadUrl = 'https://qc.ebu.io/testmaterials/523/1/download/'

# Frozen before any round-2 prediction. None of these tracks belongs to the
# earlier 10-track set: 10,21,27,31,35,40,44,47,49,50.
$tracks = @(
    @{id='08';label='Violin'},
    @{id='11';label='Double bass'},
    @{id='13';label='Flute'},
    @{id='16';label='Clarinet'},
    @{id='18';label='Bassoon'},
    @{id='20';label='Saxophone'},
    @{id='22';label='Trombone'},
    @{id='25';label='Harp'},
    @{id='28';label='Side drum'},
    @{id='32';label='Triangle'},
    @{id='36';label='Xylophone'},
    @{id='39';label='Grand piano'},
    @{id='41';label='Celesta'},
    @{id='43';label='Organ'},
    @{id='45';label='Alto voice'},
    @{id='48';label='Vocal quartet'},
    @{id='51';label='French female speech'},
    @{id='55';label='Trumpet, Haydn'},
    @{id='60';label='Piano, Schubert'},
    @{id='65';label='Orchestra, R. Strauss'}
)
$excludedTracks = @('10','21','27','31','35','40','44','47','49','50')
if (@($tracks | Where-Object { $_.id -in $excludedTracks }).Count -ne 0) {
    throw 'Round-2 source lock overlaps a previously used SQAM track.'
}
if (@($tracks.id | Sort-Object -Unique).Count -ne 20) {
    throw 'Round-2 source lock must contain exactly 20 unique tracks.'
}

$ffmpegPath = (Resolve-Path -LiteralPath $Ffmpeg).Path
$archive = (Resolve-Path -LiteralPath $ArchivePath).Path
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne
        $expectedArchiveHash) {
    throw 'EBU archive differs from the reviewed version.'
}
$destination = [IO.Path]::GetFullPath($OutputDirectory)
$protocolDestination = [IO.Path]::GetFullPath($ProtocolPath)
$blindDestination = [IO.Path]::GetFullPath($BlindDirectory)
foreach ($path in @($destination, $blindDestination)) {
    if (Test-Path -LiteralPath $path) {
        if (@(Get-ChildItem -LiteralPath $path -Force).Count -ne 0) {
            throw "Output directory must be absent or empty: $path"
        }
    } else {
        $null = New-Item -ItemType Directory -Path $path
    }
}
if (Test-Path -LiteralPath $protocolDestination) {
    throw "Protocol already exists: $protocolDestination"
}
$protocolDirectory = Split-Path -Parent $protocolDestination
$null = New-Item -ItemType Directory -Path $protocolDirectory -Force
$sourceDirectory = Join-Path $destination 'sources'
$encodedDirectory = Join-Path $destination 'encoded'
$null = New-Item -ItemType Directory -Path $sourceDirectory
$null = New-Item -ItemType Directory -Path $encodedDirectory

function Invoke-Ffmpeg([string[]]$Arguments) {
    & $ffmpegPath -nostdin -hide_banner -loglevel error -y @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "FFmpeg failed: $($Arguments -join ' ')"
    }
}

$sources = [Collections.Generic.List[object]]::new()
$samples = [Collections.Generic.List[object]]::new()
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($archive)
try {
    foreach ($track in $tracks) {
        $entry = $zip.GetEntry($track.id + '.flac')
        if ($null -eq $entry) { throw "Missing SQAM track $($track.id)." }
        $sourcePath = Join-Path $sourceDirectory ($track.id + '.flac')
        [IO.Compression.ZipFileExtensions]::ExtractToFile(
            $entry, $sourcePath, $true)
        $sources.Add([ordered]@{
            sourceGroup='EBU-SQAM-round2-track-' + $track.id
            track=$track.id
            description=$track.label
            evaluationSet='heldout'
            archiveEntry=$entry.FullName
            sha256=(Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
            bytes=(Get-Item -LiteralPath $sourcePath).Length
            excerptStartSeconds=$excerptStartSeconds
            excerptDurationSeconds=$Seconds
            provenance='EBU SQAM CD lossless FLAC; original pre-CD production history is not established'
        })
    }
} finally {
    $zip.Dispose()
}

function Add-Sample(
    $Source,
    [string]$Name,
    [string]$Truth,
    [string]$Transformation,
    [string[]]$Arguments,
    [string[]]$EncoderArguments=@()
) {
    $output = Join-Path $destination $Name
    Invoke-Ffmpeg ($Arguments + @($output))
    $samples.Add([ordered]@{
        name=$Name
        truth=$Truth
        sourceGroup=$Source.sourceGroup
        evaluationSet='heldout'
        transformation=$Transformation
        arguments=$Arguments
        encoderArguments=$EncoderArguments
        sha256=(Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash
        bytes=(Get-Item -LiteralPath $output).Length
    })
    return $output
}

$codecDefinitions = @(
    @{id='mp3-q3';extension='mp3';encoder='libmp3lame';
      settings=@('-q:a','3')},
    @{id='mp3-320';extension='mp3';encoder='libmp3lame';
      settings=@('-b:a','320k')},
    @{id='aac-256';extension='m4a';encoder='aac';
      settings=@('-b:a','256k')},
    @{id='vorbis-q7';extension='ogg';encoder='libvorbis';
      settings=@('-q:a','7')},
    @{id='opus-96-5ms';extension='opus';encoder='libopus';
      settings=@('-b:a','96k','-vbr','on','-application','audio',
                 '-frame_duration','5')},
    @{id='opus-96-10ms';extension='opus';encoder='libopus';
      settings=@('-b:a','96k','-vbr','on','-application','audio',
                 '-frame_duration','10')},
    @{id='opus-96-20ms';extension='opus';encoder='libopus';
      settings=@('-b:a','96k','-vbr','on','-application','audio',
                 '-frame_duration','20')}
)

foreach ($source in $sources) {
    $track = $source.track
    $sourcePath = Join-Path $sourceDirectory ($track + '.flac')
    $pcmName = "round2-$track-control-pcm.wav"
    $pcm = Add-Sample $source $pcmName 'inconclusive' `
        'Unmodified 44.1 kHz / 16-bit PCM excerpt; source history unknown' `
        @('-ss',"$excerptStartSeconds",'-i',$sourcePath,'-t',"$Seconds",
          '-map_metadata','-1','-ar','44100','-c:a','pcm_s16le')

    $null = Add-Sample $source "round2-$track-control-fir15k.flac" `
        'lowpass_not_lossy' `
        '1023-tap linear-phase 15 kHz FIR; explicit digital low-pass negative' `
        @('-i',$pcm,'-f','lavfi','-i','sinc=r=44100:lp=15000:lptaps=1023',
          '-filter_complex',
          '[0:a][1:a]afir=gtype=none:irfmt=mono:precision=double',
          '-t',"$Seconds",'-map_metadata','-1','-c:a','flac',
          '-sample_fmt','s32')

    foreach ($codec in $codecDefinitions) {
        $encoded = Join-Path $encodedDirectory `
            ("round2-$track-$($codec.id).$($codec.extension)")
        $encodeArguments = @('-i',$pcm,'-map_metadata','-1',
            '-c:a',$codec.encoder) + @($codec.settings) + @($encoded)
        Invoke-Ffmpeg $encodeArguments
        $null = Add-Sample $source "round2-$track-$($codec.id).flac" `
            'lossy_transcode' `
            ("$($codec.encoder) $($codec.settings -join ' ') -> FLAC") `
            @('-i',$encoded,'-map_metadata','-1','-c:a','flac',
              '-sample_fmt','s32') $encodeArguments
        if ($codec.id -eq 'aac-256') {
            $null = Add-Sample $source `
                "round2-$track-aac-256-to-96.flac" 'lossy_upsample' `
                'AAC 256k decoded and resampled to 96 kHz, then stored as FLAC' `
                @('-i',$encoded,'-map_metadata','-1','-ar','96000',
                  '-c:a','flac','-sample_fmt','s32') $encodeArguments
        }
    }

    $null = Add-Sample $source "round2-$track-44-to-96.flac" 'upsample' `
        'Known 44.1 kHz PCM resampled to 96 kHz; no lossy codec introduced' `
        @('-i',$pcm,'-map_metadata','-1','-ar','96000','-c:a','flac',
          '-sample_fmt','s32')
    Write-Output "Prepared round-2 source $track ($($samples.Count)/220 samples)."
}

foreach ($source in $sources) {
    $sourcePath = Join-Path $sourceDirectory ($source.track + '.flac')
    if ((Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash -cne
            $source.sha256) {
        throw "Source changed during generation: $($source.track)"
    }
}
if ($samples.Count -ne 220) { throw "Expected 220 samples, got $($samples.Count)." }

$manifestPath = Join-Path $destination 'manifest.json'
[ordered]@{
    schemaVersion='1'
    description='Round-2 final c01 blind corpus: 20 previously unused EBU SQAM tracks; 11 fixed transformations per source; local QA only'
    sourceUrl='https://qc.ebu.io/testmaterials/523/'
    downloadUrl=$downloadUrl
    license='EBU: not for commercial purposes other than as an R&D tool; local QA only, no redistribution or packaging'
    archiveSha256=$expectedArchiveHash
    archiveBytes=(Get-Item -LiteralPath $archive).Length
    sourceLock='Tracks 08,11,13,16,18,20,22,25,28,32,36,39,41,43,45,48,51,55,60,65; fixed before round-2 predictions'
    transformationLock='PCM, FIR15k, MP3 q3, MP3 320k, AAC 256k, AAC 256k-to-96k composite, Vorbis q7, Opus 96k 5/10/20ms, 44.1-to-96k'
    ffmpeg=(& $ffmpegPath -version | Select-Object -First 1)
    sources=$sources.ToArray()
    samples=$samples.ToArray()
} | ConvertTo-Json -Depth 10 |
    Set-Content -LiteralPath $manifestPath -Encoding utf8

$relativeManifest = [IO.Path]::GetRelativePath(
    $protocolDirectory, $manifestPath)
$relativeRoot = [IO.Path]::GetRelativePath(
    $protocolDirectory, $destination)
[ordered]@{
    schemaVersion='1'
    description='Round-2 c01 source-heldout blind evaluation; correlated transformations retained for per-source equal-weight diagnostics only'
    seed='agplayer-lossless-c01-2026-09-05-fixed-before-scan'
    algorithmVersion=$AlgorithmVersion
    parameterVersion=$ParameterVersion
    coverageMinimums=[ordered]@{
        lossyPositive=20
        explicitNegative=20
    }
    sourceEqualWeightDiagnostics=[ordered]@{
        enabled=$true
        aggregation='mean_of_within_source_rates'
        evaluationSets=@('heldout','external_validation')
    }
    corpora=@([ordered]@{
        manifest=$relativeManifest
        root=$relativeRoot
        evaluationSet='heldout'
    })
} | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath $protocolDestination -Encoding utf8

$prepareScript = Join-Path $PSScriptRoot 'qa-lossless-prepare-blind.ps1'
& $prepareScript -Protocol $protocolDestination `
    -OutputDirectory $blindDestination | Out-Null
Write-Output "Prepared locked c01 blind corpus: 20 sources, 220 samples."
Write-Output "Protocol: $protocolDestination"
Write-Output "Blind manifest: $(Join-Path $blindDestination 'manifest.json')"
