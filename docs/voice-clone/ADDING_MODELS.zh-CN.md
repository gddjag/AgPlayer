# 不改 UI 添加人声克隆模型

普通用户可以把模型放到模型根目录，再点击“刷新模型”。这只新增数据，不会修改播放器 UI 或加载任意代码。

## 本地模型描述文件

在 `<模型根目录>/<自定义目录>/agplayer-model.json` 保存：

```json
{
  "schemaVersion": 1,
  "stableId": "example/voice-model",
  "displayName": "我的本地模型",
  "revision": "local-copy-2026-08-14",
  "adapterId": "qwen",
  "runtimeId": "qwen",
  "trust": "local-unverified",
  "modelPath": ".",
  "capabilities": ["voice-clone"]
}
```

保存后在页面执行“刷新模型”。本地条目会标记为 `local-unverified`；AG Player 不替它验证来源、哈希、许可或可用性。目录不得指向模型根目录之外，也不得包含可执行入口。

当前内置 Adapter ID：

- `qwen`：Qwen3-TTS Base 系列；
- `indextts25`：IndexTTS-2.5；
- `cosyvoice3`：Fun-CosyVoice3。

同一推理架构的新权重通常只需复用现有 Adapter 并刷新模型；高级设置由 Adapter 的能力描述动态展开，不需要为每个模型新增页面控件。

## 新推理架构

如果模型不能由现有 Adapter 运行，模型目录不能夹带 Python、DLL、脚本或命令。开发者应单独制作经过签名和审核的 **Adapter Pack**，明确协议版本、Worker 可执行文件、参数 schema、模型探测规则和许可身份，再通过插件发布流程安装。

数据模型包与 Adapter Pack 的边界不可合并：前者仅包含声明式 JSON 和模型资产；后者才包含受信任的推理代码。这样后续增加模型时保持 UI 稳定，同时避免“放入模型目录即执行代码”。
