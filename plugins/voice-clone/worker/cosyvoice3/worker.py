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
        control("inferenceMode", "推理模式", "enum", "basic", "zeroShot", options=["zeroShot", "crossLingual", "instruct"]),
        control("speed", "语速", "double", "basic", 1.0, minimum=0.5, maximum=2.0, step=0.05),
        control("promptText", "参考音频文本", "string", "advanced", "", maximumLength=4000, visibleWhen={"key": "inferenceMode", "equals": "zeroShot"}),
        control("instruction", "方言 / 风格指令", "string", "advanced", "", maximumLength=1000, visibleWhen={"key": "inferenceMode", "equals": "instruct"}),
        control("fp16", "使用 FP16", "bool", "advanced", False),
        control("textFrontend", "文本前端", "bool", "advanced", True),
        control("streaming", "流式推理", "bool", "advanced", False),
        control("seed", "随机种子", "int", "advanced", 0, minimum=0, maximum=2147483647, step=1),
    ],
}


def reference_prompt(value: str) -> str:
    return value if "<|endofprompt|>" in value else f"You are a helpful assistant.<|endofprompt|>{value}"


def instruction_prompt(value: str) -> str:
    return value if "<|endofprompt|>" in value else f"You are a helpful assistant. {value}<|endofprompt|>"


class CosyVoiceEngine:
    def __init__(self):
        self.model = None

    def validate_generation(self, payload, parameters):
        if not payload.get("referenceAudioPath"):
            raise WorkerError("INVALID_REQUEST", "CosyVoice3 requires referenceAudioPath")
        if parameters["inferenceMode"] == "zeroShot" and not parameters["promptText"].strip():
            raise WorkerError("INVALID_PARAMETERS", "promptText is required for zeroShot mode")
        if parameters["inferenceMode"] == "instruct" and not parameters["instruction"].strip():
            raise WorkerError("INVALID_PARAMETERS", "instruction is required for instruct mode")

    def load(self, model_root, parameters, token):
        required = ["cosyvoice3.yaml", "campplus.onnx", "speech_tokenizer_v3.onnx"]
        missing = [name for name in required if not (model_root / name).is_file()]
        if missing:
            raise WorkerError("MODEL_INCOMPLETE", f"CosyVoice3 model is missing {missing[0]}")
        token.raise_if_cancelled()
        from cosyvoice.cli.cosyvoice import AutoModel

        self.model = AutoModel(model_dir=str(model_root), fp16=parameters["fp16"])

    def generate(self, text, reference, output, parameters, token, progress, request_id):
        if self.model is None:
            raise WorkerError("MODEL_NOT_LOADED", "CosyVoice3 model is not loaded")
        import torch
        import torchaudio

        torch.manual_seed(parameters["seed"])
        if torch.cuda.is_available():
            torch.cuda.manual_seed_all(parameters["seed"])
        common = {"stream": parameters["streaming"], "speed": parameters["speed"],
                  "text_frontend": parameters["textFrontend"]}
        mode = parameters["inferenceMode"]
        if mode == "zeroShot":
            pieces = self.model.inference_zero_shot(text, reference_prompt(parameters["promptText"]), str(reference), **common)
        elif mode == "crossLingual":
            pieces = self.model.inference_cross_lingual(reference_prompt(text), str(reference), **common)
        else:
            pieces = self.model.inference_instruct2(text, instruction_prompt(parameters["instruction"]), str(reference), **common)
        waveforms = []
        progress("generate", request_id, "synthesizing", None)
        for piece in pieces:
            token.raise_if_cancelled()
            waveforms.append(piece["tts_speech"])
        if not waveforms:
            raise WorkerError("OUTPUT_INVALID", "CosyVoice3 returned no audio")
        temp = temporary_output_path(output)
        try:
            torchaudio.save(str(temp), torch.cat(waveforms, dim=1), self.model.sample_rate)
            token.raise_if_cancelled()
            os.replace(temp, output)
        finally:
            temp.unlink(missing_ok=True)

    def unload(self):
        self.model = None


if __name__ == "__main__":
    raise SystemExit(run_worker(CosyVoiceEngine(), SCHEMA, "cosyvoice3"))
