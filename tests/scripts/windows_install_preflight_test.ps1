$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$script = Join-Path $repo 'scripts/qa-windows-install-preflight.ps1'
$result = & powershell -NoProfile -ExecutionPolicy Bypass -File $script | ConvertFrom-Json
if ($LASTEXITCODE -ne 2) { throw 'Blocked preflight must exit 2.' }
if ($result.schemaVersion -ne 1 -or $result.installationExecuted -ne $false) {
    throw 'Preflight must report its schema and never execute installation.'
}
if ($result.ready -or @($result.blockers) -notcontains 'disposable-environment-not-confirmed') {
    throw 'Default preflight must not approve an unconfirmed environment.'
}
if (@($result.blockers) -notcontains 'installer-not-specified') {
    throw 'A missing installer must be a blocker.'
}
$unrelatedExe = Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
$result = & powershell -NoProfile -ExecutionPolicy Bypass -File $script -Installer $unrelatedExe | ConvertFrom-Json
if ($LASTEXITCODE -ne 2 -or $result.ready -or
    @($result.blockers) -notcontains 'installer-product-or-version-invalid' -or
    @($result.blockers) -notcontains 'installer-release-hash-not-verified') {
    throw "An unrelated executable must be rejected: exit=$LASTEXITCODE result=$($result | ConvertTo-Json -Compress -Depth 4)"
}
'Windows installation preflight safety checks passed.'
