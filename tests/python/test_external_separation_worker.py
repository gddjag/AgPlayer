"""Protocol boundary tests; heavy inference is validated separately with a real model."""
import json
from pathlib import Path
import subprocess
import sys
import unittest


class WorkerProtocolTest(unittest.TestCase):
    def test_forced_gpu_reports_cpu_only_before_importing_heavy_dependencies(self):
        script = Path(__file__).resolve().parents[2] / 'qt/resources/external_separation_worker.py'
        messages = [dict(protocol=1, type='start', requestId='gpu', payload={'device': 'gpu'}),
                    dict(protocol=1, type='shutdown', requestId='gpu', payload={})]
        result = subprocess.run([sys.executable, '-S', str(script)],
                                input=''.join(json.dumps(m) + '\n' for m in messages),
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        terminal = [json.loads(line) for line in result.stdout.splitlines()
                    if json.loads(line)['type'] in ('result', 'error')]
        self.assertEqual(terminal[0]['type'], 'error')
        self.assertIn('CPU', terminal[0]['payload']['message'])

    def test_handshake_does_not_require_importing_inference_dependencies(self):
        script = Path(__file__).resolve().parents[2] / 'qt/resources/external_separation_worker.py'
        messages = [dict(protocol=1, type='hello', requestId='h', payload={}),
                    dict(protocol=1, type='shutdown', requestId='h', payload={})]
        result = subprocess.run([sys.executable, '-S', str(script)],
                                input=''.join(json.dumps(m) + '\n' for m in messages),
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        hello = json.loads(result.stdout.splitlines()[0])
        self.assertEqual(hello['requestId'], 'h')
        self.assertEqual(hello['payload']['protocol'], 1)


if __name__ == '__main__':
    unittest.main()
