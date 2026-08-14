from __future__ import annotations

import os
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from common.agvoice_protocol import WorkerError, existing_file, run_worker, temporary_output_path


def control(key, label, kind, group, default, **extra):
    return {"key": key, "label": label, "description": label, "type": kind,
            "group": group, "default": default, "required": False, **extra}


SCHEMA = {
    "protocolVersion": 1,
    "groups": [{"id": "basic", "label": "基础设置"}, {"id": "advanced", "label": "高级设置"}],
    "parameters": [
        control("language", "语言", "enum", "basic", "ZH", options=["ZH", "EN", "JA", "ES", "AR"]),
        control("durationFactor", "时长倍率", "double", "basic", 1.0, minimum=0.5, maximum=2.0, step=0.05),
        control("emotionMode", "情绪控制", "enum", "basic", "none", options=["none", "reference", "preset", "text"]),
        control("emotionAudioPath", "情绪参考音频", "file", "advanced", "", extensions=["wav", "mp3", "flac", "m4a"], visibleWhen={"key": "emotionMode", "equals": "reference"}),
        control("emotionPreset", "情绪预设", "enum", "advanced", "calm", options=["happy", "angry", "sad", "afraid", "disgusted", "melancholic", "surprised", "calm"], visibleWhen={"key": "emotionMode", "equals": "preset"}),
        control("emotionText", "情绪描述", "string", "advanced", "", maximumLength=500, visibleWhen={"key": "emotionMode", "equals": "text"}),
        control("emotionIntensity", "情绪强度", "double", "advanced", 0.6, minimum=0.0, maximum=1.0, step=0.05),
        control("useRandom", "情绪随机采样", "bool", "advanced", False),
        control("device", "运行设备", "enum", "advanced", "auto", options=["auto", "cuda", "cpu"]),
        control("useBf16", "使用 BF16", "bool", "advanced", True),
        control("useCudaKernel", "CUDA 融合内核", "bool", "advanced", False),
        control("useDeepSpeed", "DeepSpeed", "bool", "advanced", False),
        control("maxTextTokensPerSegment", "每段最大 Token", "int", "advanced", 120, minimum=20, maximum=240, step=10),
        control("intervalSilenceMs", "分段静音", "int", "advanced", 200, minimum=0, maximum=2000, step=50),
        control("textNormalization", "文本规范化", "bool", "advanced", True),
    ],
}


class IndexEngine:
    def __init__(self):
        self.model = None
        self._signature = None

    @staticmethod
    def load_signature(parameters):
        return (parameters["device"], parameters["useBf16"], parameters["useCudaKernel"],
                parameters["useDeepSpeed"], parameters["emotionMode"] == "text")

    def validate_generation(self, payload, parameters):
        if not payload.get("referenceAudioPath"):
            raise WorkerError("INVALID_REQUEST", "IndexTTS requires referenceAudioPath")
        mode = parameters["emotionMode"]
        if mode == "reference" and not parameters["emotionAudioPath"]:
            raise WorkerError("INVALID_PARAMETERS", "emotionAudioPath is required for reference emotion mode")
        if mode == "text" and not parameters["emotionText"].strip():
            raise WorkerError("INVALID_PARAMETERS", "emotionText is required for text emotion mode")

    def load(self, model_root, parameters, token):
        signature = self.load_signature(parameters)
        if self.model is not None and signature == self._signature:
            return
        self.unload()
        required = [
            "config.yaml", "gpt.pth", "s2mel.pth", "codec.pth", "wav2vec2bert_stats.pt",
            "feat1.pt", "feat2.pt", "multilingual_zh_ja_yue_char_del.tiktoken",
            "hf_cache/campplus_cn_common.bin", "hf_cache/semantic_codec_model.safetensors",
            "hf_cache/w2v-bert-2.0/config.json", "hf_cache/w2v-bert-2.0/preprocessor_config.json",
            "hf_cache/w2v-bert-2.0/model.safetensors", "hf_cache/bigvgan/config.json",
            "hf_cache/bigvgan/bigvgan_generator.pt",
        ]
        if parameters["emotionMode"] == "text":
            required += [
                "qwen0.6bemo4-merge/config.json", "qwen0.6bemo4-merge/model.safetensors",
                "qwen0.6bemo4-merge/tokenizer.json", "qwen0.6bemo4-merge/tokenizer_config.json",
                "qwen0.6bemo4-merge/special_tokens_map.json",
                "qwen0.6bemo4-merge/added_tokens.json", "qwen0.6bemo4-merge/merges.txt",
                "qwen0.6bemo4-merge/vocab.json", "qwen0.6bemo4-merge/chat_template.jinja",
            ]
        missing = [name for name in required if not (model_root / name).is_file()]
        if missing:
            raise WorkerError(
                "MODEL_INCOMPLETE",
                f"IndexTTS model or auxiliary cache is missing {missing[0]}; install it before starting the Worker",
            )
        token.raise_if_cancelled()
        from indextts.infer_v2_5 import IndexTTS2

        device = None if parameters["device"] == "auto" else ("cuda:0" if parameters["device"] == "cuda" else "cpu")
        try:
            self.model = IndexTTS2(
                cfg_path=str(model_root / "config.yaml"), model_dir=str(model_root),
                use_bf16=parameters["useBf16"], device=device,
                use_cuda_kernel=parameters["useCudaKernel"],
                use_deepspeed=parameters["useDeepSpeed"],
                use_qwen_emo=parameters["emotionMode"] == "text",
            )
        except FileNotFoundError as error:
            raise WorkerError("MODEL_INCOMPLETE", f"IndexTTS asset is missing: {error.filename or error}") from error
        except OSError as error:
            message = str(error)
            if "no file named" in message.lower() or "not found in directory" in message.lower():
                raise WorkerError("MODEL_INCOMPLETE", f"IndexTTS asset is missing: {message}") from error
            raise
        self._signature = signature

    def generate(self, text, reference, output, parameters, token, progress, request_id):
        if self.model is None:
            raise WorkerError("MODEL_NOT_LOADED", "IndexTTS model is not loaded")
        mode = parameters["emotionMode"]
        kwargs = {
            "spk_audio_prompt": str(reference), "text": text,
            "lang": parameters["language"], "duration_factor": parameters["durationFactor"],
            "emo_alpha": parameters["emotionIntensity"], "use_random": parameters["useRandom"],
            "max_text_tokens_per_segment": parameters["maxTextTokensPerSegment"],
            "interval_silence": parameters["intervalSilenceMs"],
            "text_normalization": parameters["textNormalization"], "verbose": False,
        }
        if mode == "reference":
            kwargs["emo_audio_prompt"] = str(existing_file(parameters["emotionAudioPath"], "emotionAudioPath"))
        elif mode == "preset":
            names = ["happy", "angry", "sad", "afraid", "disgusted", "melancholic", "surprised", "calm"]
            kwargs["emo_vector"] = [parameters["emotionIntensity"] if name == parameters["emotionPreset"] else 0.0 for name in names]
            kwargs["emo_alpha"] = 1.0
        elif mode == "text":
            kwargs.update({"use_emo_text": True, "emo_text": parameters["emotionText"]})
        token.raise_if_cancelled()
        progress("generate", request_id, "synthesizing", None)
        temp = temporary_output_path(output)
        try:
            self.model.infer(output_path=str(temp), **kwargs)
            token.raise_if_cancelled()
            os.replace(temp, output)
        finally:
            temp.unlink(missing_ok=True)

    def unload(self):
        self.model = None
        self._signature = None


if __name__ == "__main__":
    raise SystemExit(run_worker(IndexEngine(), SCHEMA, "indextts25"))
