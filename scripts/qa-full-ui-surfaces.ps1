param(
    [string]$BuildRoot = 'D:/ai/AgPlayer/build/release',
    [string]$OutputRoot = 'D:/ai/AgPlayer/build/qa/full-ui-baseline',
    [string[]]$Themes = @('dark', 'light'),
    [string[]]$Only = @(),
    [string]$Scale = '1',
    [switch]$Compact
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $OutputRoot | Out-Null
$runtimeRoot = Join-Path $BuildRoot 'vcpkg_installed/x64-windows/bin'
$env:PATH = "D:/Qt/6.7.0/msvc2019_64/bin;$runtimeRoot;" + $env:PATH
$env:QT_SCALE_FACTOR = $Scale
$fixture = Join-Path $BuildRoot 'tests/fixtures/sine-440hz.wav'
$formats = Join-Path $OutputRoot 'input-fixtures'
New-Item -ItemType Directory -Force $formats | Out-Null
foreach ($fixtureName in @('sine-440hz.wav','gapless-a.wav','gapless-b.wav')) {
    Copy-Item -LiteralPath (Join-Path $BuildRoot "tests/fixtures/$fixtureName") -Destination $formats -Force
}
$videoFixture = Join-Path $BuildRoot 'tests/fixtures/video/video-with-audio.avi'
$surfaces = [ordered]@{}
foreach ($shell in @('classic','integrated','rolling')) {
    $surfaces[$shell] = @('--qa-player-shell',$shell,'--qa-play',$fixture,'--qa-screenshot-main')
}
$surfaces['mini'] = @('--qa-play',$fixture,'--qa-screenshot-mini')
$surfaces['immersive'] = @('--qa-play',$fixture,'--qa-immersive','--qa-screenshot-main')
$surfaces['list'] = @('--qa-import-folder',$formats,'--qa-screenshot-list')
$surfaces['tags'] = @('--qa-import-folder',$formats,'--qa-list-category','tags','--qa-tag','Demo','--qa-screenshot-list')
$surfaces['equalizer'] = @('--qa-open-equalizer','--qa-equalizer-size','1080','480','--qa-screenshot-main')
$surfaces['video'] = @('--qa-player-shell','integrated','--qa-play',$videoFixture,'--qa-screenshot-main')
$surfaces['video-fullscreen'] = @('--qa-play',$videoFixture,'--qa-video-fullscreen','--qa-screenshot-main')
0..6 | ForEach-Object { $surfaces["settings-$_"] = @('--qa-open-settings','--qa-settings-section',"$_",'--qa-screenshot-main') }
0..5 | ForEach-Object { $surfaces["tool-$_"] = @('--qa-tool',"$_",'--qa-screenshot-tools') }
$results = @()
foreach ($appearance in $Themes) {
    foreach ($surface in $surfaces.Keys) {
        if ($Only.Count -gt 0 -and $surface -notin $Only) { continue }
        $stem = "$appearance-$surface"
        $imagePath = Join-Path $OutputRoot "$stem.png"
        $runtimeLog = Join-Path $OutputRoot "$stem.log"
        foreach ($previousCapture in @($imagePath, $runtimeLog)) {
            if (Test-Path -LiteralPath $previousCapture) {
                Remove-Item -LiteralPath $previousCapture
            }
        }
        $state = Join-Path $OutputRoot ("state-$stem-" + [guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Force $state | Out-Null
        $sizeArguments = @()
        if ($Compact) {
            if ($surface -like 'tool-*') {
                $sizeArguments = @('--qa-tools-size','880','560')
            } elseif ($surface -eq 'classic') {
                $sizeArguments = @('--qa-width','612','--qa-height','232')
            } elseif ($surface -in @('integrated','rolling')) {
                $sizeArguments = @('--qa-width','1180','--qa-height','720')
            }
        }
        $arguments = @('--qa-test-mode','--qa-language','zh','--qa-theme',$appearance,
            '--qa-library',(Join-Path $state 'library.json'),'--qa-log',$runtimeLog,
            '--qa-exit-after-ms','15000') + $sizeArguments + $surfaces[$surface] + @($imagePath)
        $timer = [Diagnostics.Stopwatch]::StartNew()
        $process = Start-Process -FilePath (Join-Path $BuildRoot 'app/AgPlayer.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
        if (!$process.WaitForExit(25000)) {
            $process.Kill()
            $process.WaitForExit()
        }
        $process.Refresh()
        $width = 0; $height = 0
        if (Test-Path -LiteralPath $imagePath) {
            $bitmap = [Drawing.Image]::FromFile($imagePath)
            $width = $bitmap.Width; $height = $bitmap.Height
            $bitmap.Dispose()
        }
        $logText = if (Test-Path -LiteralPath $runtimeLog) { Get-Content -LiteralPath $runtimeLog -Raw } else { 'missing log' }
        $clean = $logText -notmatch '\[(WARN|ERROR|FATAL)\]|QQml|ReferenceError|TypeError|missing log'
        $captureScale = [regex]::Match($logText, 'QA capture scaling: dpr= ([0-9.]+) logical= QSize\((\d+), (\d+)\) pixels= QSize\((\d+), (\d+)\)')
        $actualDpr = if ($captureScale.Success) { $captureScale.Groups[1].Value } else { '' }
        $pixelWidth = if ($captureScale.Success) { [int]$captureScale.Groups[4].Value } else { 0 }
        $pixelHeight = if ($captureScale.Success) { [int]$captureScale.Groups[5].Value } else { 0 }
        $results += [pscustomobject]@{Theme=$appearance; Surface=$surface; Scale=$Scale; ActualDpr=$actualDpr; Exit=$process.ExitCode; Width=$width; Height=$height; PixelWidth=$pixelWidth; PixelHeight=$pixelHeight; LogClean=$clean; ElapsedMs=$timer.ElapsedMilliseconds; Pass=($process.ExitCode -eq 0 -and $width -gt 0 -and $clean -and $captureScale.Success -and $pixelWidth -gt 0)}
        $results | Export-Csv -LiteralPath (Join-Path $OutputRoot 'matrix.csv') -Encoding utf8 -NoTypeInformation
        Write-Output "$stem : $($results[-1].Pass) ${width}x${height}"
    }
}
if ($results.Pass -contains $false) { exit 1 }
