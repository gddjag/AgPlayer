param(
    [Parameter(Mandatory=$true)][string]$Ffmpeg,
    [string]$InputManifest = 'build/qa/lossless-sqam/manifest-development.json',
    [string]$OutputDirectory = 'build/qa/lossless-robustness'
)
$ErrorActionPreference = 'Stop'
$manifestPath = (Resolve-Path -LiteralPath $InputManifest).Path
$sourceRoot = Split-Path $manifestPath
$inputData = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$destination = [IO.Path]::GetFullPath($OutputDirectory)
if ([StringComparer]::OrdinalIgnoreCase.Equals($destination,$sourceRoot)) { throw 'Use a separate output directory' }
[IO.Directory]::CreateDirectory($destination) | Out-Null
$records = [Collections.Generic.List[object]]::new()
foreach ($sample in $inputData.samples) {
    if ($sample.split -ne 'development' -or $sample.name -notmatch '(aac|mp3)-hidden|44-to-96') { continue }
    $upsample = $sample.truth -eq 'upsample'
    $source = Join-Path $sourceRoot $sample.name
    $before = (Get-FileHash -LiteralPath $source).Hash
    if ($before -ne $sample.sha256) { throw "Source hash mismatch: $source" }
    foreach ($variant in @('gain-trim','noise','strong-noise')) {
        $name = [IO.Path]::GetFileNameWithoutExtension($sample.name) + '-' + $variant + '.flac'
        $output = Join-Path $destination $name
        $arguments = @('-i',$source)
        if ($variant -eq 'gain-trim') {
            # Retain the compression history while destroying absolute framing offset.
            $arguments += @('-af','atrim=start_sample=137,asetpts=PTS-STARTPTS,volume=0.73:precision=double')
        } else {
            # Full-band low-level noise must not become proof of an uncompressed source.
            $noiseRate = if ($upsample) { 96000 } else { 44100 }
            $amplitude = if ($variant -eq 'strong-noise') { '0.003' } else { '0.000177827941' }
            $mix = if ($variant -eq 'strong-noise') {
                '[0:a]volume=0.1:precision=double[quiet];[quiet][1:a]amix=inputs=2:duration=first:normalize=0'
            } else { '[0:a][1:a]amix=inputs=2:duration=first:normalize=0' }
            $arguments += @('-f','lavfi','-i',"anoisesrc=r=${noiseRate}:a=${amplitude}:d=12:seed=1907",
                '-filter_complex',$mix)
        }
        $arguments += @('-map_metadata','-1','-c:a','flac','-sample_fmt','s32')
        & $Ffmpeg -nostdin -hide_banner -loglevel error -y @arguments $output
        if ($LASTEXITCODE -ne 0) { throw "Encoding failed: $name" }
        $records.Add([ordered]@{name=$name;truth=$sample.truth;sourceGroup=$sample.sourceGroup;
            split='development';transformation=$variant;sourceSha256=$before;arguments=$arguments;
            sha256=(Get-FileHash -LiteralPath $output).Hash;bytes=(Get-Item -LiteralPath $output).Length})
    }
    if ((Get-FileHash -LiteralPath $source).Hash -ne $before) { throw 'Source changed' }
}
if ($records.Count -eq 0) { throw 'No development samples found' }
[ordered]@{description='Development-only compression-history robustness controls; gain/trim and low-level added noise do not erase prior encoding history. Not independent recordings.';
    inputManifest=$manifestPath;inputManifestSha256=(Get-FileHash -LiteralPath $manifestPath).Hash;
    ffmpeg=(& $Ffmpeg -version | Select-Object -First 1);samples=$records.ToArray()} |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $destination 'manifest.json') -Encoding utf8
Write-Output "Generated $($records.Count) robustness samples"
