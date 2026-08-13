# Task 1 report — extensible voice-clone contract RED tests

## 实现

- 新增 `voice_clone_manifest_test`：定义 `VoiceCloneRegistry::loadBuiltIn(path)` 和 `discoverUserModels(root, knownAdapterIds)` 的期望契约。
  - 固定并验证首发四个稳定 ID、Qwen/Index/CosyVoice 的 Adapter/Runtime 映射、仅 IndexTTS 的许可接受门禁、官方 HTTPS 来源，以及未批准模型的拒绝。
  - 覆盖有效本地 Qwen Manifest、重复 ID、未知 Adapter、用户 Manifest 注入命令、缺失文件、绝对路径、`..` 路径、Windows junction 逃逸和无哈希的 `local-unverified` 状态。
- 新增 `voice_clone_capability_schema_test`：定义 `validateCapabilitySchema(object)` 与 `validateParameters(schema, values)` 的期望契约。
  - 覆盖 `basic`/`advanced` 分组、bool/enum/int/double/string/file 控件、默认值和范围、条件可见性、协议不匹配及未知提交参数拒绝。
- 新增只含数据的 Qwen 本地 Manifest 夹具；没有命令、脚本或可执行文件。
- 将两个目标按现有 QtTest `foreach(qt_test ...)` 模式接入 CMake，并向 Manifest 测试提供夹具根目录编译定义。

## 测试

按 VS 2022 x64 开发环境执行。原有 `build/debug` 缓存指向 MinGW，因此没有复用它；使用与 `windows-msvc-debug` 预设相同配置的隔离目录 `build/msvc-debug`。

```powershell
cmd.exe /d /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --preset windows-msvc-debug -B build/msvc-debug && cmake --build build/msvc-debug --target voice_clone_manifest_test voice_clone_capability_schema_test'
ctest --test-dir build/msvc-debug -C Debug -R "voice_clone_(manifest|capability_schema)_test" --output-on-failure
```

## RED 证据

- CMake 配置成功，编译器为 `...\Hostx64\x64\cl.exe`。
- 构建按预期失败，原因是尚未存在生产接口头：
  - `voice_clone_capability_schema.hpp`: `fatal error C1083: No such file or directory`。
  - 先前同一 RED 构建亦确认 `voice_clone_registry.hpp`: `fatal error C1083: No such file or directory`。
- 由于两个可执行文件未生成，CTest 发现两个测试但均 `Not Run`；这是该 RED 状态的后果，而非测试通过声明。

## 文件

- `tests/CMakeLists.txt`
- `tests/fixtures/voice-clone/models/local-qwen/agplayer-model.json`
- `tests/qt/voice_clone_manifest_test.cpp`
- `tests/qt/voice_clone_capability_schema_test.cpp`

## 自审

- `git diff --check` 通过。
- 未修改任何生产源、QML、Registry、Adapter 或 Runtime 实现。
- 夹具只声明相对数据文件与 SHA-256；有效测试在临时目录创建空的数据文件。
- 内置模型列表严格固定为批准的四项；被取消模型名称仅以拆分字符串出现在测试断言/负例中，未进入产品夹具。

## 疑虑

- 这是刻意 RED 的测试先行提交，生产头和实现尚不存在，不能执行测试断言。
- `loadBuiltIn(path)` 的严格白名单行为、发现结果类型以及校验结果访问器均由本测试定义；后续生产实现需要遵守这些形状，或连同测试以受审方式调整。
- Windows junction 测试调用 `mklink /J`，在标准 Windows 测试环境中不需管理员权限；若企业策略禁用 `cmd.exe`，该个案需要替代的受控 junction 夹具机制。
