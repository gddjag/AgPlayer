"""AgPlayer's isolated audio-separator bridge (not an embedded playback dependency).

Uses audio-separator's public API, MIT licensed by its upstream authors:
https://github.com/nomadkaraoke/python-audio-separator
"""
import contextlib
import hashlib
import faulthandler
import json
import os
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
    ffmpeg = tools / 'ffmpeg.exe'
    if not ffmpeg.exists():
        shutil.copyfile(imageio_ffmpeg.get_ffmpeg_exe(), ffmpeg)
    os.environ['PATH'] = str(tools) + os.pathsep + os.environ.get('PATH', '')


def run(request, payload):
    faulthandler.dump_traceback_later(60, file=sys.stderr)
    stage = ['model_loading']
    def heartbeat():
        while not DONE.wait(2):
            emit('progress', request, dict(fraction=0, stage=stage[0]))
    threading.Thread(target=heartbeat, daemon=True).start()
    try:
        with contextlib.redirect_stdout(sys.stderr):
            print('Initializing FFmpeg and Python imports', file=sys.stderr, flush=True)
            prepare_environment()
            print('Loading PyTorch', file=sys.stderr, flush=True)
            import torch
            torch.set_num_threads(min(4, max(1, (os.cpu_count() or 2) // 2)))
            from audio_separator.separator import Separator
            print('Validating local VR model', file=sys.stderr, flush=True)
            model = Path(payload['modelFiles'][0]).resolve(strict=True)
            if model.name.lower() != '5_hp-karaoke-uvr.pth':
                raise ValueError('此 Python 适配器仅验证了 5_HP-Karaoke-UVR.pth，请选择已支持的模型')
            with model.open('rb') as source:
                digest = hashlib.file_digest(source, 'sha256').hexdigest()
            if digest != 'fe00891defbb61f4261500af22f7624f1a3df8dc75fa3998d1aece02e6be4537':
                raise ValueError('VR 模型完整性校验失败，拒绝执行未知 PyTorch 文件')
            root = Path(payload['outputDirectory']).resolve(strict=True)
            destination = Path(tempfile.mkdtemp(prefix='agplayer-vr-', dir=root))
            class LocalVrSeparator(Separator):
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
            separator.load_model(model_filename=model.name)
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
            separator.model_instance.write_audio = write_stem
            stage[0] = 'inference'
            files = separator.separate(payload['inputPath'],
                                       custom_output_names={'Vocals': 'agplayer-vocals',
                                                            'Instrumental': 'agplayer-instrumental'})
            outputs = []
            for stem in payload['stems']:
                matches = [destination / name for name in files
                           if ('agplayer-' + stem) in Path(name).stem.lower()]
                if len(matches) != 1 or not matches[0].is_file():
                    raise RuntimeError('分离输出音轨缺失：' + stem)
                outputs.append(str(matches[0]))
        DONE.set()
        emit('result', request, dict(outputs=outputs, device='cpu', provider='python-vr-cpu'))
    except Exception as error:
        DONE.set()
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
        print('agplayer-python-vr-ready')
    else:
        main()
