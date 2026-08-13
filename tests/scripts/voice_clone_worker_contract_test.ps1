param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot,
    [Parameter(Mandatory = $true)]
    [string]$ClientPath
)

$ErrorActionPreference = 'Stop'

$python = $null
$uv = Get-Command uv -ErrorAction SilentlyContinue
if ($uv) {
    $candidate = (& $uv.Source python find 3.12 --system 2>$null | Select-Object -First 1)
    if ($LASTEXITCODE -eq 0 -and $candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        $python = $candidate
    }
}
if (-not $python) {
    $candidate = (Get-Command python -ErrorAction Stop).Source
    & $candidate --version 2>$null
    if ($LASTEXITCODE -ne 0) {
        throw 'A runnable Python 3.12 interpreter is required for the Worker contract test'
    }
    $python = $candidate
}
$workers = @(
    @{ Adapter = 'qwen'; Parameter = 'xVectorOnlyMode' },
    @{ Adapter = 'indextts25'; Parameter = 'durationFactor' },
    @{ Adapter = 'cosyvoice3'; Parameter = 'inferenceMode' }
)

foreach ($worker in $workers) {
    $workerPath = Join-Path $SourceRoot "plugins/voice-clone/worker/$($worker.Adapter)/worker.py"
    if (-not (Test-Path -LiteralPath $workerPath -PathType Leaf)) {
        throw "Worker is missing: $workerPath"
    }
    & $ClientPath --python $python --worker $workerPath --adapter-id $worker.Adapter `
        --expected-parameter $worker.Parameter
    if ($LASTEXITCODE -ne 0) {
        throw "$($worker.Adapter) worker contract failed with exit code $LASTEXITCODE"
    }
}

$protocolPath = Join-Path $SourceRoot 'plugins/voice-clone/worker/common/agvoice_protocol.py'
$pathContract = @'
import importlib.util
import pathlib
import sys
spec = importlib.util.spec_from_file_location(pathlib.Path(sys.argv[1]).stem, sys.argv[1])
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
temporary = module.temporary_output_path(pathlib.Path(bytes([103,101,110,101,114,97,116,101,100,46,119,97,118,46,112,97,114,116]).decode()))
assert temporary.suffix == chr(46) + chr(119) + chr(97) + chr(118), temporary
'@
& $python -I -s -c $pathContract $protocolPath
if ($LASTEXITCODE -ne 0) {
    throw 'Worker temporary output must keep a WAV suffix for official writers'
}

Write-Output 'voice clone worker contracts passed'
