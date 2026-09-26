"""Opt-in real VR inference through the same NDJSON bridge used by Qt."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time

script = Path(__file__).resolve().parents[2] / 'qt/resources/external_separation_worker.py'
destination = tempfile.mkdtemp(prefix='agplayer-real-vr-')
start = time.monotonic()
with open(Path(destination) / 'worker.log', 'w', encoding='utf8') as log:
    process = subprocess.Popen([sys.executable, str(script)], stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=log, text=True, encoding='utf8')
    def send(kind, payload):
        process.stdin.write(json.dumps(dict(protocol=1, type=kind, requestId='smoke', payload=payload)) + '\n')
        process.stdin.flush()
    send('hello', {})
    hello = json.loads(process.stdout.readline())
    assert hello['type'] == 'hello'
    send('start', dict(inputPath=sys.argv[2], modelFiles=[sys.argv[1]],
                       outputDirectory=destination, stems=['vocals', 'instrumental'], extension='wav'))
    for line in process.stdout:
        event = json.loads(line)
        print(line.strip(), flush=True)
        if event['type'] in ('error', 'result'):
            send('shutdown', {})
            process.wait(timeout=5)
            print('elapsed:', round(time.monotonic()-start, 2), 'evidence:', destination, flush=True)
            if event['type'] == 'error':
                raise SystemExit(1)
            assert event['payload']['device'] == 'cpu'
            assert event['payload']['provider'] == 'python-vr-cpu'
            assert 'CPU' in event['payload']['fallbackReason']
            import soundfile as sf
            import numpy as np
            for output in event['payload']['outputs']:
                audio, rate = sf.read(output)
                assert rate == 44100 and audio.ndim == 2 and len(audio) > 0
                assert np.isfinite(audio).all()
            break
