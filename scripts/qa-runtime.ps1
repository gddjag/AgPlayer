function Get-AgPlayerQtPrefix {
    param([Parameter(Mandatory = $true)][string]$CachePath)

    $qtDirectoryLine = Select-String -LiteralPath $CachePath `
        -Pattern "^Qt6_DIR:[^=]*=(.+)$" | Select-Object -First 1
    if ($null -ne $qtDirectoryLine) {
        $qtDirectory = $qtDirectoryLine.Matches[0].Groups[1].Value
        return [System.IO.Path]::GetFullPath(
            (Join-Path $qtDirectory "..\..\.."))
    }

    $prefixLine = Select-String -LiteralPath $CachePath `
        -Pattern "^CMAKE_PREFIX_PATH:[^=]*=(.+)$" | Select-Object -First 1
    if ($null -ne $prefixLine) {
        $prefixes = $prefixLine.Matches[0].Groups[1].Value -split ";"
        foreach ($prefix in $prefixes) {
            if (Test-Path -LiteralPath (Join-Path $prefix "bin")) {
                return $prefix
            }
        }
    }

    throw "Neither a usable Qt6_DIR nor CMAKE_PREFIX_PATH was found in $CachePath"
}

function Get-AgPlayerRuntimePaths {
    param(
        [Parameter(Mandatory = $true)][string]$BuildRoot,
        [Parameter(Mandatory = $true)][string]$CachePath
    )

    $qtPrefix = Get-AgPlayerQtPrefix -CachePath $CachePath
    return @(
        (Join-Path $qtPrefix "bin"),
        (Join-Path $BuildRoot "vcpkg_installed/x64-windows/debug/bin"),
        (Join-Path $BuildRoot "vcpkg_installed/x64-windows/bin")
    ) | Where-Object { Test-Path -LiteralPath $_ }
}

function Start-AgPlayerProcess {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$BuildRoot,
        [Parameter(Mandatory = $true)][string]$CachePath,
        [Parameter(Mandatory = $true)][string]$AppPath,
        [string[]]$ArgumentList = @(),
        [switch]$Wait
    )

    $runtimePaths = Get-AgPlayerRuntimePaths `
        -BuildRoot $BuildRoot -CachePath $CachePath
    $originalPath = $env:Path
    try {
        $env:Path = ($runtimePaths -join ";") + ";" + $originalPath
        $startParameters = @{
            FilePath = $AppPath
            PassThru = $true
        }
        if ($ArgumentList.Count -gt 0) {
            $startParameters.ArgumentList = $ArgumentList
        }
        $process = Start-Process @startParameters
        if ($Wait) {
            $process.WaitForExit()
        }
        return $process
    }
    finally {
        $env:Path = $originalPath
    }
}
