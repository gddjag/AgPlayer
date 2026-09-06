# Read-only gate. This script never installs, launches, uninstalls or deletes files.
[CmdletBinding()]
param(
    [string]$Installer,
    [string]$ExpectedSha256,
    [switch]$DisposableEnvironmentConfirmed
)
$ErrorActionPreference = 'Stop'
# Resolve native Windows modules even when a caller has a restricted module path.
Import-Module (Join-Path $PSHOME 'Modules/Microsoft.PowerShell.Utility/Microsoft.PowerShell.Utility.psd1')
Import-Module (Join-Path $PSHOME 'Modules/Microsoft.PowerShell.Security/Microsoft.PowerShell.Security.psd1')
Import-Module (Join-Path $PSHOME 'Modules/CimCmdlets/CimCmdlets.psd1')
$blockers = [System.Collections.Generic.List[string]]::new()
if (-not $DisposableEnvironmentConfirmed) {
    $blockers.Add('disposable-environment-not-confirmed')
}
$machine = $null
try { $machine = Get-CimInstance Win32_ComputerSystem } catch {
    $blockers.Add('machine-inspection-failed')
}
$virtual = $null -ne $machine -and
    "$($machine.Manufacturer) $($machine.Model)" -match 'Virtual Machine|VMware|VirtualBox|KVM|QEMU|Parallels|Xen'
if (-not $virtual) { $blockers.Add('isolated-virtual-machine-not-detected') }
$os = $null
try {
    $os = Get-CimInstance Win32_OperatingSystem
    if ($os.ProductType -ne 1 -or $os.Version -notlike '10.0.*' -or
        -not [Environment]::Is64BitOperatingSystem -or
        $env:PROCESSOR_ARCHITECTURE -ne 'AMD64') {
        $blockers.Add('unsupported-windows-target')
    }
} catch { $blockers.Add('os-inspection-failed') }

$existing = @()
if (Test-Path -LiteralPath 'HKCU:/Software/AgPlayer') {
    $existing += 'HKCU:/Software/AgPlayer'
}
foreach ($path in @(
    (Join-Path $env:LOCALAPPDATA 'Programs/AgPlayer'),
    (Join-Path $env:LOCALAPPDATA 'AgPlayer'),
    (Join-Path $env:APPDATA 'AgPlayer'),
    (Join-Path $env:ProgramFiles 'AgPlayer')
)) {
    if (Test-Path -LiteralPath $path) { $existing += $path }
}
foreach ($base in @(
    'HKCU:/Software/Microsoft/Windows/CurrentVersion/Uninstall',
    'HKLM:/Software/Microsoft/Windows/CurrentVersion/Uninstall',
    'HKLM:/Software/WOW6432Node/Microsoft/Windows/CurrentVersion/Uninstall'
)) {
    if (Test-Path -LiteralPath $base) {
        foreach ($key in Get-ChildItem -LiteralPath $base) {
            $entry = Get-ItemProperty -LiteralPath $key.PSPath
            if ($entry.DisplayName -like '*AgPlayer*') { $existing += $key.Name }
        }
    }
}
if ($existing.Count) { $blockers.Add('existing-agplayer-installation-or-data') }
if (Get-Process -Name AgPlayer -ErrorAction SilentlyContinue) {
    $blockers.Add('agplayer-process-running')
}
$artifact = $null
if (-not $Installer) { $blockers.Add('installer-not-specified') }
elseif (-not (Test-Path -LiteralPath $Installer -PathType Leaf)) {
    $blockers.Add('installer-not-found')
} else {
    try {
    $file = Get-Item -LiteralPath $Installer
    if ($file.Extension -ine '.exe') { $blockers.Add('installer-not-exe') }
    if ($file.VersionInfo.ProductName -ne 'AgPlayer' -or
        [string]::IsNullOrWhiteSpace($file.VersionInfo.FileVersion)) {
        $blockers.Add('installer-product-or-version-invalid')
    }
    $actualHash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    if ($ExpectedSha256 -notmatch '^[A-Fa-f0-9]{64}$' -or $actualHash -ine $ExpectedSha256) {
        $blockers.Add('installer-release-hash-not-verified')
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $file.FullName
    if ($signature.Status -notin @('Valid', 'NotSigned')) {
        $blockers.Add('installer-signature-invalid')
    }
    $artifact = [ordered]@{
        path = $file.FullName
        sha256 = $actualHash
        fileVersion = $file.VersionInfo.FileVersion
        signature = [string]$signature.Status
    }
    } catch { $blockers.Add('installer-inspection-failed'); $artifact = @{ error = $_.Exception.Message } }
}
[ordered]@{
    schemaVersion = 1
    installationExecuted = $false
    ready = $blockers.Count -eq 0
    blockers = @($blockers)
    machine = if ($machine) { "$($machine.Manufacturer) $($machine.Model)" } else { $null }
    operatingSystem = if ($os) { "$($os.Caption) $($os.Version) $($os.OSArchitecture)" } else { $null }
    existingAgPlayerPaths = $existing
    installer = $artifact
    note = 'Read-only preflight, not installation acceptance. A VM snapshot and manual checks are still required.'
} | ConvertTo-Json -Depth 4
if ($blockers.Count) { exit 2 }
