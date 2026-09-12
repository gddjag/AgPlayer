param(
    [Parameter(Mandatory = $true)][string]$AppPath,
    [Parameter(Mandatory = $true)][string]$FixturePath
)

$first = $null
$second = $null
$previousQpaPlatform = $env:QT_QPA_PLATFORM
$previousAppData = $env:APPDATA
$previousLocalAppData = $env:LOCALAPPDATA
$testDataRoot = Join-Path $env:TEMP ("AgPlayer-single-instance-" + [guid]::NewGuid())
$instanceKey = [guid]::NewGuid().ToString('N')
try {
    # The Windows single-instance contract owns a native mutex and window
    # activation path.  The offscreen platform does not initialize that
    # native path, so it cannot validate the production behavior.
    Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
    $env:APPDATA = Join-Path $testDataRoot 'Roaming'
    $env:LOCALAPPDATA = Join-Path $testDataRoot 'Local'
    New-Item -ItemType Directory -Force -Path $env:APPDATA, $env:LOCALAPPDATA | Out-Null
    $logPath = Join-Path $testDataRoot 'primary.log'
    $libraryPath = Join-Path $testDataRoot 'library.json'
    $first = Start-Process -FilePath $AppPath -ArgumentList @(
        '--qa-log', $logPath, '--qa-library', $libraryPath,
        '--qa-instance-key', $instanceKey
    ) -PassThru
    Start-Sleep -Milliseconds 1500
    if ($first.HasExited) { throw "first AgPlayer instance exited early" }

    $second = Start-Process -FilePath $AppPath -ArgumentList @(
        '--qa-instance-key', $instanceKey, $FixturePath
    ) -PassThru
    if (-not $second.WaitForExit(2000)) {
        throw "second AgPlayer instance did not exit"
    }
    if ($second.ExitCode -ne 0) {
        throw "second AgPlayer instance exited with $($second.ExitCode)"
    }

    $deadline = [DateTime]::UtcNow.AddSeconds(8)
    $imported = $false
    $fixtureStem = [IO.Path]::GetFileNameWithoutExtension($FixturePath)
    while ([DateTime]::UtcNow -lt $deadline -and -not $imported) {
        if (Test-Path -LiteralPath $libraryPath) {
            $imported = (Get-Content -LiteralPath $libraryPath -Raw) -match [regex]::Escape($fixtureStem)
        }
        if (-not $imported) { Start-Sleep -Milliseconds 100 }
    }
    if (-not $imported) {
        throw 'second-instance media path was not imported by the primary instance'
    }
} finally {
    if ($second -and -not $second.HasExited) { Stop-Process -Id $second.Id -Force }
    if ($first -and -not $first.HasExited) { Stop-Process -Id $first.Id -Force }
    $env:QT_QPA_PLATFORM = $previousQpaPlatform
    $env:APPDATA = $previousAppData
    $env:LOCALAPPDATA = $previousLocalAppData
    if (Test-Path -LiteralPath $testDataRoot) {
        Remove-Item -LiteralPath $testDataRoot -Recurse -Force
    }
}
