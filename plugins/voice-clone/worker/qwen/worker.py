from __future__ import annotations

import os
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from common.agvoice_protocol import WorkerError, run_worker, temporary_output_path


def control(key, label, kind, group, default, **extra):
    return {"key": key, "label": label, "description": label, "type": kind,
            "group": group, "default": default, "required": False, **extra}


SCHEMA = {
    "protocolVersion": 1,
    "groups": [{"id": "basic", "label": "基础设置"}, {"id": "advanced", "label": "高级设置"}],
    "parameters": [
        control("language", "语言", "enum", "basic", "Auto", options=["Auto", "Chinese", "English", "Japanese", "Korean", "German", "French", "Russian", "Portuguese", "Spanish", "Italian"]),
        control("xVectorOnlyMode", "仅使用音色向量", "bool", "advanced", False),
        control("referenceTranscript", "参考音频文本", "string", "advanced", "", maximumLength=4000),
        control("device", "运行设备", "enum", "advanced", "auto", options=["auto", "cuda", "cpu"]),
        control("dtype", "计算精度", "enum", "advanced", "auto", options=["auto", "bfloat16", "float16", "float32"]),
        control("attention", "注意力实现", "enum", "advanced", "auto", options=["auto", "sdpa", "flash_attention_2"]),
        control("temperature", "随机度", "double", "advanced", 0.9, minimum=0.1, maximum=2.0, step=0.05),
        control("topP", "Top P", "double", "advanced", 0.95, minimum=0.1, maximum=1.0, step=0.01),
        control("topK", "Top K", "int", "advanced", 50, minimum=1, maximum=200, step=1),
        control("repetitionPenalty", "重复惩罚", "double", "advanced", 1.05, minimum=0.5, maximum=2.0, step=0.05),
        control("maxNewTokens", "最大生成 Token", "int", "advanced", 2048, minimum=128, maximum=8192, step=128),
        control("nonStreamingMode", "非流式推理", "bool", "advanced", False),
        control("seed", "随机种子", "int", "advanced", 0, minimum=0, maximum=2147483647, step=1),
    ],
}


class QwenEngine:
    def __init__(self):
        self.model = None
        self._signature = None

    @staticmethod
    def load_signature(parameters):
        return (parameters["device"], parameters["dtype"], parameters["attention"])

    def validate_generation(self, payload, parameters):
        if not payload.get("referenceAudioPath"):
            raise WorkerError("INVALID_REQUEST", "Qwen Base requires referenceAudioPath")
        if not parameters["xVectorOnlyMode"] and not parameters["referenceTranscript"].strip():
            raise WorkerError("INVALID_PARAMETERS", "referenceTranscript is required unless xVectorOnlyMode is enabled")

    def load(self, model_root, parameters, token):
        signature = self.load_signature(parameters)
        if self.model is not None and signature == self._signature:
            return
        self.unload()
        if not (model_root / "config.json").is_file():
            raise WorkerError("MODEL_INCOMPLETE", "Qwen model config.json is missing")
        token.raise_if_cancelled()
        import torch
        from qwen_tts import Qwen3TTSModel

        device = parameters["device"]
        if device == "auto":
            device = "cuda:0" if torch.cuda.is_available() else "cpu"
        elif device == "cuda":
            if not torch.cuda.is_available():
                raise WorkerError("DEVICE_UNAVAILABLE", "CUDA is not available")
            device = "cuda:0"
        dtype_name = parameters["dtype"]
        if dtype_name == "auto":
            dtype_name = "bfloat16" if device.startswith("cuda") else "float32"
        dtype = {"bfloat16": torch.bfloat16, "float16": torch.float16, "float32": torch.float32}[dtype_name]
        attention = parameters["attention"]
        if attention == "auto":
            attention = "sdpa"
        self.model = Qwen3TTSModel.from_pretrained(
            str(model_root), device_map=device, dtype=dtype,
            attn_implementation=attention, local_files_only=True
        )
        self._signature = signature

    def generate(self, text, reference, output, parameters, token, progress, request_id):
        if self.model is None:
            raise WorkerError("MODEL_NOT_LOADED", "Qwen model is not loaded")
        import soundfile as sf
        import torch

        torch.manual_seed(parameters["seed"])
        if torch.cuda.is_available():
            torch.cuda.manual_seed_all(parameters["seed"])
        token.raise_if_cancelled()
        progress("generate", request_id, "synthesizing", None)
        wavs, sample_rate = self.model.generate_voice_clone(
            text=text,
            language=parameters["language"],
            ref_audio=str(reference),
            ref_text=parameters["referenceTranscript"] or None,
            x_vector_only_mode=parameters["xVectorOnlyMode"],
            non_streaming_mode=parameters["nonStreamingMode"],
            temperature=parameters["temperature"],
            top_p=parameters["topP"],
            top_k=parameters["topK"],
            repetition_penalty=parameters["repetitionPenalty"],
            max_new_tokens=parameters["maxNewTokens"],
        )
        token.raise_if_cancelled()
        temp = temporary_output_path(output)
        try:
            sf.write(str(temp), wavs[0], sample_rate)
            token.raise_if_cancelled()
            os.replace(temp, output)
        finally:
            temp.unlink(missing_ok=True)

    def unload(self):
        self.model = None
        self._signature = None


if __name__ == "__main__":
    raise SystemExit(run_worker(QwenEngine(), SCHEMA, "qwen"))
