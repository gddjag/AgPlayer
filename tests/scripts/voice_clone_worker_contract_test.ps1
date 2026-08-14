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

$reloadContract = @'
import importlib.util
import pathlib
import sys
import tempfile

def load(path):
    spec = importlib.util.spec_from_file_location(pathlib.Path(path).parent.name + '_worker', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def defaults(schema):
    return {item['key']: item['default'] for item in schema['parameters']}

qwen = load(sys.argv[1])
q = qwen.QwenEngine()
q_default = defaults(qwen.SCHEMA)
q_device = dict(q_default, device='cpu')
q_sampling = dict(q_default, temperature=1.2)
assert q.load_signature(q_default) != q.load_signature(q_device)
assert q.load_signature(q_default) == q.load_signature(q_sampling)

index = load(sys.argv[2])
i = index.IndexEngine()
i_default = defaults(index.SCHEMA)
i_text = dict(i_default, emotionMode='text')
assert i.load_signature(i_default) != i.load_signature(i_text)
required = [
    'config.yaml', 'gpt.pth', 's2mel.pth', 'codec.pth', 'wav2vec2bert_stats.pt',
    'feat1.pt', 'feat2.pt', 'multilingual_zh_ja_yue_char_del.tiktoken',
    'hf_cache/campplus_cn_common.bin', 'hf_cache/semantic_codec_model.safetensors',
    'hf_cache/w2v-bert-2.0/config.json', 'hf_cache/w2v-bert-2.0/preprocessor_config.json',
    'hf_cache/w2v-bert-2.0/model.safetensors', 'hf_cache/bigvgan/config.json',
    'hf_cache/bigvgan/bigvgan_generator.pt', 'qwen0.6bemo4-merge/config.json',
    'qwen0.6bemo4-merge/model.safetensors',
]
with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    for name in required:
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b'contract')
    try:
        i.load(root, i_text, type('Token', (), {'raise_if_cancelled': lambda self: None})())
    except index.WorkerError as error:
        assert error.code == 'MODEL_INCOMPLETE', error.code
    else:
        raise AssertionError('missing Index emotion tokenizer was not mapped to MODEL_INCOMPLETE')

cosy = load(sys.argv[3])
c = cosy.CosyVoiceEngine()
c_default = defaults(cosy.SCHEMA)
c_fp16 = dict(c_default, fp16=True)
assert c.load_signature(c_default) != c.load_signature(c_fp16)
'@
& $python -I -s -c $reloadContract `
    (Join-Path $SourceRoot 'plugins/voice-clone/worker/qwen/worker.py') `
    (Join-Path $SourceRoot 'plugins/voice-clone/worker/indextts25/worker.py') `
    (Join-Path $SourceRoot 'plugins/voice-clone/worker/cosyvoice3/worker.py')
if ($LASTEXITCODE -ne 0) {
    throw 'Worker load-critical parameter contract failed'
}

Write-Output 'voice clone worker contracts passed'
