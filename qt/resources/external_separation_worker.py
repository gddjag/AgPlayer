"""AgPlayer's isolated audio-separator bridge (not an embedded playback dependency).

Uses audio-separator's public API, MIT licensed by its upstream authors:
https://github.com/nomadkaraoke/python-audio-separator
"""
import contextlib
import hashlib
import faulthandler
import json
import os
import platform
import inspect
from pathlib import Path
import shutil
import sys
import threading
import tempfile

WIRE = sys.stdout
LOCK = threading.Lock()
DONE = threading.Event()


def emit(kind, request, payload):
    with LOCK:
        if kind == 'progress' and DONE.is_set():
            return
        WIRE.write(json.dumps(dict(protocol=1, type=kind, requestId=request,
                                   payload=payload), ensure_ascii=True) + '\n')
        WIRE.flush()


def prepare_environment():
    import imageio_ffmpeg
    tools = Path(sys.executable).parent / 'agplayer-tools'
    tools.mkdir(exist_ok=True)
    ffmpeg = tools / ('ffmpeg.exe' if sys.platform == 'win32' else 'ffmpeg')
    if not ffmpeg.exists():
        shutil.copyfile(imageio_ffmpeg.get_ffmpeg_exe(), ffmpeg)
    if sys.platform != 'win32':
        ffmpeg.chmod(0o700)
    os.environ['PATH'] = str(tools) + os.pathsep + os.environ.get('PATH', '')


def select_device(torch, requested, system=None, machine=None):
    system = sys.platform if system is None else system
    machine = platform.machine() if machine is None else machine
    mps = (system == 'darwin' and machine.lower() in ('arm64', 'aarch64')
           and hasattr(torch.backends, 'mps') and torch.backends.mps.is_available())
    if requested == 'gpu' and not mps:
        raise ValueError('此设备未提供可用 MPS；请选择自动或 CPU 后重试')
    return 'mps' if requested != 'cpu' and mps else 'cpu'


def separate_with_names(separator, input_path):
    if 'custom_output_names' in inspect.signature(separator.separate).parameters:
        return separator.separate(input_path, custom_output_names={
            'Vocals': 'agplayer-vocals', 'Instrumental': 'agplayer-instrumental'})
    # Intel Mac's last official PyTorch release uses audio-separator 0.24.1.
    return separator.separate(input_path, primary_output_name='agplayer-instrumental',
                              secondary_output_name='agplayer-vocals')


def configure_model_resampling(separator, system=None, machine=None):
    system = sys.platform if system is None else system
    machine = platform.machine() if machine is None else machine
    if system == 'darwin' and machine.lower() in ('arm64', 'aarch64'):
        # samplerate 0.1.0's nominally universal wheel bundles only an Intel
        # dylib. VR already uses scipy's polyphase path for MPS; use the same
        # path for Apple Silicon CPU and CPU fallback, without another runtime.
        for band in separator.model_instance.model_params.param['band'].values():
            band['res_type'] = 'polyphase'


def run(request, payload, probe=False):
    DONE.clear()
    faulthandler.dump_traceback_later(60, file=sys.stderr)
    stage = ['model_loading']
    def heartbeat():
        while not DONE.wait(2):
            emit('progress', request, dict(fraction=0, stage=stage[0]))
    threading.Thread(target=heartbeat, daemon=True).start()
    try:
        if payload.get('device', 'auto') == 'gpu' and sys.platform != 'darwin':
            raise ValueError('当前 Python VR 适配器仅支持 CPU，请选择自动或 CPU 后重试')
        with contextlib.redirect_stdout(sys.stderr):
            print('Initializing FFmpeg and Python imports', file=sys.stderr, flush=True)
            prepare_environment()
            print('Loading PyTorch', file=sys.stderr, flush=True)
            # Never silently execute unsupported MPS operators on CPU while
            # reporting that this model passed GPU inference.
            os.environ['PYTORCH_ENABLE_MPS_FALLBACK'] = '0'
            import torch
            device = select_device(torch, 'auto' if probe else payload.get('device', 'auto'))
            torch.set_num_threads(min(4, max(1, (os.cpu_count() or 2) // 2)))
            from audio_separator.separator import Separator
            print('Validating local VR model', file=sys.stderr, flush=True)
            model = Path(payload['modelFiles'][0]).resolve(strict=True)
            if model.name.lower() != '5_hp-karaoke-uvr.pth':
                raise ValueError('此 Python 适配器仅验证了 5_HP-Karaoke-UVR.pth，请选择已支持的模型')
            with model.open('rb') as source:
                checksum = hashlib.sha256()
                for chunk in iter(lambda: source.read(1024 * 1024), b''):
                    checksum.update(chunk)
                digest = checksum.hexdigest()
            if digest != 'fe00891defbb61f4261500af22f7624f1a3df8dc75fa3998d1aece02e6be4537':
                raise ValueError('VR 模型完整性校验失败，拒绝执行未知 PyTorch 文件')
            probe_files = tempfile.TemporaryDirectory(prefix='agplayer-vr-probe-') if probe else None
            if probe:
                import numpy as np
                import soundfile as sf
                payload = dict(payload, outputDirectory=probe_files.name,
                               inputPath=str(Path(probe_files.name) / 'probe.wav'),
                               stems=['vocals', 'instrumental'], extension='wav')
                tone = (0.01 * np.sin(np.arange(44100) * (2 * np.pi * 440 / 44100))).astype(np.float32)
                sf.write(payload['inputPath'], np.column_stack((tone, tone)), 44100)
            root = Path(payload['outputDirectory']).resolve(strict=True)
            destination = Path(tempfile.mkdtemp(prefix='agplayer-vr-', dir=root))
            class LocalVrSeparator(Separator):
                def setup_torch_device(self, system_info):
                    self.torch_device_cpu = torch.device('cpu')
                    self.torch_device_mps = torch.device('mps') if device == 'mps' else None
                    self.torch_device = self.torch_device_mps or self.torch_device_cpu
                    self.onnx_execution_provider = ['CPUExecutionProvider']

                # UVR metadata for tail MD5 f6ea8473ff86017b5ebd586ccacf156b;
                # full SHA checked above. No mutable remote metadata/download.
                def download_model_files(self, model_filename):
                    return model_filename, 'VR', model.stem, str(model), None

                def load_model_data_using_hash(self, model_path):
                    return {'vr_model_param': '4band_v2_sn',
                            'primary_stem': 'Instrumental', 'is_karaoke': True}

            separator = LocalVrSeparator(model_file_dir=str(model.parent),
                                  output_dir=str(destination), output_format=payload.get('extension', 'wav').upper(),
                                  use_soundfile=True, normalization_threshold=0.9,
                                  vr_params={'batch_size': 1, 'window_size': 512,
                                             'aggression': 5, 'enable_tta': False,
                                             'enable_post_process': False,
                                             'post_process_threshold': 0.2,
                                             'high_end_process': 'mirroring'})
            def write_stem(filename, samples):
                # audio-separator 0.30.2's soundfile writer ignores output_dir
                # and can interleave floats as int16. Keep export local and
                # preserve the floating-point two-channel array explicitly.
                import numpy as np
                import soundfile as sf
                samples = np.asarray(samples, dtype=np.float32)
                if samples.ndim != 2 or samples.shape[1] != 2 or not np.isfinite(samples).all():
                    raise ValueError('VR 输出不是有效的双声道音频')
                peak = float(np.max(np.abs(samples)))
                if peak > 1:
                    samples = samples / peak * 0.99
                output = destination / Path(filename).name
                if output.suffix.lower() == '.mp3':
                    from pydub import AudioSegment
                    pcm = (samples * 32767).astype(np.int16)
                    AudioSegment(pcm.tobytes(), sample_width=2, frame_rate=44100,
                                 channels=2).export(str(output), format='mp3', bitrate='320k')
                else:
                    sf.write(output, samples, 44100, subtype='PCM_24')
            stage[0] = 'inference'
            fallback_reason = ''
            try:
                separator.load_model(model_filename=model.name)
                configure_model_resampling(separator)
                separator.model_instance.write_audio = write_stem
                files = separate_with_names(separator, payload['inputPath'])
            except Exception as gpu_error:
                if device != 'mps' or (not probe and payload.get('device') == 'gpu'):
                    raise
                fallback_reason = '当前模型 MPS 推理失败，已回退 CPU：' + str(gpu_error)
                device = 'cpu'
                separator.setup_torch_device(None)
                separator.load_model(model_filename=model.name)
                configure_model_resampling(separator)
                separator.model_instance.write_audio = write_stem
                files = separate_with_names(separator, payload['inputPath'])
            outputs = []
            for stem in payload['stems']:
                matches = [destination / name for name in files
                           if ('agplayer-' + stem) in Path(name).stem.lower()]
                if len(matches) != 1 or not matches[0].is_file():
                    raise RuntimeError('分离输出音轨缺失：' + stem)
                outputs.append(str(matches[0]))
            if device == 'mps':
                torch.mps.synchronize()
        DONE.set()
        if probe:
            emit('probe', request, dict(cpu=True, gpu=device == 'mps', provider='mps' if device == 'mps' else 'python-vr-cpu',
                                       modelValidated=True, gpuReason=fallback_reason or
                                       ('当前 VR 模型已通过 MPS 推理验证' if device == 'mps' else '当前 VR 模型已通过 CPU 推理验证')))
        else:
            emit('result', request, dict(outputs=outputs, device='gpu' if device == 'mps' else 'cpu',
                                        provider='mps' if device == 'mps' else 'python-vr-cpu',
                                        fallbackReason=fallback_reason))
    except Exception as error:
        DONE.set()
        if probe:
            emit('probe', request, dict(cpu=False, gpu=False, cpuValidated=True,
                                       cpuReason=str(error), gpuReason=str(error), modelValidated=False))
        else:
            emit('error', request, dict(code='external_runtime', message=str(error), retryable=True))
    finally:
        faulthandler.cancel_dump_traceback_later()
        DONE.set()


def main():
    for line in sys.stdin:
        message = json.loads(line)
        request = message['requestId']
        kind = message['type']
        if kind == 'hello':
            emit('hello', request, dict(protocol=1, worker='agplayer-python-vr'))
        elif kind == 'start':
            # NumPy/OpenBLAS DLL initialization on Windows must run on the main
            # thread. The Qt owner terminates this isolated process on cancel;
            # the lightweight heartbeat thread keeps long imports responsive.
            run(request, message['payload'])
        elif kind == 'probe':
            run(request, message['payload'], probe=True)
        elif kind == 'cancel':
            DONE.set()
            emit('cancel', request, {})
        elif kind == 'shutdown':
            return


if __name__ == '__main__':
    if '--verify' in sys.argv:
        prepare_environment()
        from audio_separator.separator import Separator
        import torch
        import soundfile
        import subprocess
        # Exercise the actual installed CPU runtime and executable, not only
        # package metadata. Model compatibility is probed separately per card.
        if not torch.isfinite(torch.ones((2, 2)) @ torch.ones((2, 2))).all():
            raise RuntimeError('PyTorch CPU 验证失败')
        subprocess.run(['ffmpeg', '-version'], check=True, stdout=subprocess.DEVNULL,
                       stderr=subprocess.PIPE, timeout=15)
        print('agplayer-python-vr-ready')
    else:
        main()
