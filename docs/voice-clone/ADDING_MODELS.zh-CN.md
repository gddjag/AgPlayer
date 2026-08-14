# 不改 UI 添加人声克隆模型

普通用户可以添加“仅数据”的模型目录，再点击“刷新模型”。播放器不会从模型目录执行 Python、DLL、脚本或命令。

## 可复制的目录与清单

生产解析器只扫描两层目录：

`<AG Player 可移植根>/models/voice-clone/<模型目录>/<版本目录>/agplayer-model.json`

例如把下列清单保存为：

`models/voice-clone/local-qwen/2026.08.14/agplayer-model.json`

并把 `config.json`、`model.safetensors` 放在同一目录：

```json
{
  "schemaVersion": 1,
  "stableId": "local/qwen-demo",
  "displayName": "我的 Qwen 本地模型",
  "description": "由用户自行放入的 Qwen 架构权重。",
  "adapterId": "qwen",
  "runtimeId": "qwen",
  "revision": "2026.08.14",
  "source": {
    "provider": "Hugging Face",
    "url": "https://huggingface.co/Qwen/Qwen3-TTS-12Hz-0.6B-Base"
  },
  "license": {
    "name": "Apache-2.0",
    "url": "https://github.com/QwenLM/Qwen3-TTS/blob/022e286b98fbec7e1e916cb940cdf532cd9f488e/LICENSE",
    "revision": "022e286b98fbec7e1e916cb940cdf532cd9f488e"
  },
  "files": [
    { "path": "config.json" },
    { "path": "model.safetensors" }
  ]
}
```

省略每个文件的 `sha256` 时，条目会显示为 `local-unverified`。若填写 `sha256`，必须是对应文件的真实 64 位十六进制 SHA-256；全部匹配后才显示 `ready`。未知字段、重复路径、越界路径、可执行文件、未知 Adapter 或不可信 URL 都会被拒绝。

当前受信任 Adapter ID：

- `qwen`：Qwen3-TTS Base 系列；
- `indextts25`：IndexTTS-2.5；
- `cosyvoice3`：Fun-CosyVoice3。

本地 `indextts25` 模型还必须完整声明并精确匹配 AG Player 批准的 bilibili 自定义许可与 MaskGCT CC-BY-NC-4.0 双许可身份；缺少、改写或伪造任一项都会 fail-closed。界面会要求逐项接受，且 MaskGCT 资产仅限非商业用途。

## 新推理架构

同一推理架构的新权重可复用现有 Adapter；高级设置由 Adapter 的能力 schema 动态展开，无需改 UI。

若现有 Adapter 无法运行新模型，开发者必须单独发布经过签名和审核的 **Adapter Pack**，声明协议版本、Worker、参数 schema、探测规则和许可身份。数据模型包只包含 JSON 与模型资产；Adapter Pack 才能包含受信任的推理代码，两者不得合并。
