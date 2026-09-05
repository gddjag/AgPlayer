param([string]$PackageScript = (Join-Path $PSScriptRoot '../../scripts/package-windows.ps1'))

$ErrorActionPreference = 'Stop'
# Load only the production cleanup function, never the build/deploy/installer
# commands in the rest of the packaging script.
$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile(
    [IO.Path]::GetFullPath($PackageScript), [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -gt 0) { throw "Packaging script has syntax errors: $parseErrors" }
$functionAst = $ast.Find({ param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq 'Remove-QmlToolingMetadata'
}, $false)
if ($null -eq $functionAst) { throw 'Missing production QML tooling cleanup function' }
. ([scriptblock]::Create($functionAst.Extent.Text))

$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('agplayer-qml-metadata-' + [guid]::NewGuid().ToString('N'))
$expectedRoot = [IO.Path]::GetFullPath($testRoot)
$stage = Join-Path $testRoot 'stage'
$module = Join-Path $stage 'qml/QtQuick/Test'
try {
    New-Item -ItemType Directory -Path $module -Force | Out-Null
    $metadata = Join-Path $module 'plugins.qmltypes'
    [IO.File]::WriteAllBytes($metadata, [byte[]](1, 2, 3, 4, 5))
    $nested = Join-Path $module 'nested'
    New-Item -ItemType Directory -Path $nested | Out-Null
    [IO.File]::WriteAllBytes((Join-Path $nested 'types.qmltypes'), [byte[]](6, 7, 8))
    $kept = @('qmldir', 'Button.qml', 'Helper.js', 'plugin.dll', 'LICENSE.txt', 'types.qmltypes.bak')
    foreach ($name in $kept) {
        [IO.File]::WriteAllText((Join-Path $module $name), 'runtime data')
    }
    $outside = Join-Path $testRoot 'outside.qmltypes'
    [IO.File]::WriteAllText($outside, 'outside staging')
    $nonQml = Join-Path $stage 'keep.qmltypes'
    [IO.File]::WriteAllText($nonQml, 'outside deployed qml tree')
    $result = Remove-QmlToolingMetadata -StageDirectory $stage
    if ($result.Files -ne 2 -or $result.Bytes -ne 8) { throw "Wrong cleanup accounting: $result" }
    if (Test-Path -LiteralPath $metadata) { throw 'Tooling metadata was not removed' }
    foreach ($name in $kept) {
        if ([IO.File]::ReadAllText((Join-Path $module $name)) -ne 'runtime data') {
            throw "Runtime content changed: $name"
        }
    }
    if (-not (Test-Path -LiteralPath $outside) -or -not (Test-Path -LiteralPath $nonQml)) {
        throw 'Cleanup escaped the deployed QML subtree'
    }
    $again = Remove-QmlToolingMetadata -StageDirectory $stage
    if ($again.Files -ne 0 -or $again.Bytes -ne 0) { throw 'Cleanup is not idempotent' }
    $absent = Remove-QmlToolingMetadata -StageDirectory (Join-Path $testRoot 'absent')
    if ($absent.Files -ne 0 -or $absent.Bytes -ne 0) { throw 'Missing QML directory must be a no-op' }
    Write-Output 'QML metadata cleanup: selective removal, byte accounting, outside-file preservation and repeat/missing-directory checks passed.'
} finally {
    if (Test-Path -LiteralPath $testRoot) {
        $resolved = (Resolve-Path -LiteralPath $testRoot).Path
        if ($resolved -ne $expectedRoot) { throw "Unexpected cleanup path: $resolved" }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
