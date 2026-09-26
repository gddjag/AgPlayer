"""Protocol boundary tests; heavy inference is validated separately with a real model."""
import json
import platform
from pathlib import Path
import subprocess
import sys
import unittest
import importlib.util
from types import SimpleNamespace
from unittest.mock import patch
import tempfile


SCRIPT = Path(__file__).resolve().parents[2] / 'qt/resources/external_separation_worker.py'
SPEC = importlib.util.spec_from_file_location('agplayer_vr_worker', SCRIPT)
WORKER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(WORKER)


class WorkerProtocolTest(unittest.TestCase):
    def test_apple_silicon_cpu_resampling_does_not_load_intel_only_samplerate(self):
        bands = {1: {'res_type': 'sinc_fastest'}, 2: {'res_type': 'sinc_medium'}}
        separator = SimpleNamespace(model_instance=SimpleNamespace(
            model_params=SimpleNamespace(param={'band': bands})))
        WORKER.configure_model_resampling(separator, 'darwin', 'x86_64')
        self.assertEqual(bands[1]['res_type'], 'sinc_fastest')
        WORKER.configure_model_resampling(separator, 'win32', 'AMD64')
        self.assertEqual(bands[2]['res_type'], 'sinc_medium')
        WORKER.configure_model_resampling(separator, 'darwin', 'arm64')
        self.assertTrue(all(band['res_type'] == 'polyphase' for band in bands.values()))

    def test_mps_selection_requires_apple_silicon_and_available_backend(self):
        torch = SimpleNamespace(backends=SimpleNamespace(mps=SimpleNamespace(is_available=lambda: True)))
        self.assertEqual(WORKER.select_device(torch, 'auto', 'darwin', 'arm64'), 'mps')
        self.assertEqual(WORKER.select_device(torch, 'cpu', 'darwin', 'arm64'), 'cpu')
        self.assertEqual(WORKER.select_device(torch, 'auto', 'darwin', 'x86_64'), 'cpu')
        self.assertEqual(WORKER.select_device(torch, 'auto', 'win32', 'AMD64'), 'cpu')
        with self.assertRaisesRegex(ValueError, 'CPU'):
            WORKER.select_device(torch, 'gpu', 'darwin', 'x86_64')
        torch.backends.mps.is_available = lambda: False
        self.assertEqual(WORKER.select_device(torch, 'auto', 'darwin', 'arm64'), 'cpu')

    def test_intel_legacy_and_current_output_names_keep_stem_order(self):
        class Legacy:
            def separate(self, path, primary_output_name=None, secondary_output_name=None):
                return path, primary_output_name, secondary_output_name
        class Current:
            def separate(self, path, custom_output_names=None):
                return path, custom_output_names
        self.assertEqual(WORKER.separate_with_names(Legacy(), 'song.wav'),
                         ('song.wav', 'agplayer-instrumental', 'agplayer-vocals'))
        self.assertEqual(WORKER.separate_with_names(Current(), 'song.wav'),
                         ('song.wav', {'Vocals': 'agplayer-vocals', 'Instrumental': 'agplayer-instrumental'}))

    def test_macos_ffmpeg_is_named_and_marked_executable(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            executable = root / 'python'
            source = root / 'bundled-ffmpeg'
            source.write_bytes(b'ffmpeg fixture')
            module = SimpleNamespace(get_ffmpeg_exe=lambda: str(source))
            with patch.dict(sys.modules, {'imageio_ffmpeg': module}), \
                 patch.object(WORKER.sys, 'platform', 'darwin'), \
                 patch.object(WORKER.sys, 'executable', str(executable)), \
                 patch.dict(WORKER.os.environ, {}, clear=False):
                WORKER.prepare_environment()
                output = root / 'agplayer-tools/ffmpeg'
                self.assertEqual(output.read_bytes(), source.read_bytes())
                self.assertFalse((root / 'agplayer-tools/ffmpeg.exe').exists())
                self.assertEqual(WORKER.os.environ['PATH'].split(WORKER.os.pathsep)[0], str(output.parent))

    def test_forced_gpu_reports_policy_or_missing_runtime_before_inference(self):
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
        if sys.platform == 'darwin' and platform.machine() == 'arm64':
            # ARM permits MPS; -S deliberately omits the optional runtime.
            # It must report that missing dependency, not a CPU-only policy.
            self.assertIn('No module named', terminal[0]['payload']['message'])
        else:
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
