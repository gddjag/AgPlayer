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

## Fix round 1

### 追加覆盖

- 对批准 Registry 的完整原始 JSON 字节串进行禁用产品名扫描，并增加“禁用名只藏在 description 字段”仍须拒绝的负例。
- 逐项锁定四个模型的 `stableId / adapterId / runtimeId / requiresLicenseAcceptance` 元组；增加三个非 Index 模型误开许可门禁均被拒绝的负例。
- 明确验证夹具空文件的正确 SHA-256 可进入 `ready`，随后篡改权重文件并断言发现失败或状态不再为 `ready`。
- junction 目标改为独立 `QTemporaryDir`，通过 canonical path 断言其位于扫描 root 外，再验证链接逃逸被拒绝。
- 用户 Manifest 顶层字段按严格 allowlist 处理：未知字段、`command`、`script`、`executable`、`launcher` 均为拒绝样例；文件列表另覆盖 `.exe`、`.bat`、`.ps1` 非数据文件拒绝。
- Capability 参数增加 int 超范围/非整数、enum 非法值、string 超长、required 缺失、file 不存在和后缀不允许的拒绝边界；所有用于有效/无效文件样例的 `QFile::open` 均检查成功。
- 未添加 QML 或生产实现。

### RED 命令与输出

```powershell
cmd.exe /d /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && where cl && cmake --preset windows-msvc-debug -B build/msvc-debug'
cmd.exe /d /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build build/msvc-debug --target voice_clone_manifest_test'
cmd.exe /d /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build build/msvc-debug --target voice_clone_capability_schema_test'
ctest --test-dir build/msvc-debug -C Debug -R "voice_clone_(manifest|capability_schema)_test" --output-on-failure
```

- 配置：成功，`where cl` 指向 VS 2022 `Hostx64\x64\cl.exe`。
- Manifest 目标：按预期 RED，`voice_clone_registry.hpp` 缺失，MSVC `fatal error C1083`。
- Capability 目标：按预期 RED，`voice_clone_capability_schema.hpp` 缺失，MSVC `fatal error C1083`。
- CTest：两个测试已注册；因 RED 构建未产生可执行文件，`0% tests passed, 2 tests failed out of 2`，两项均 `Not Run`。

## Fix round 2

- 正常 Registry 基线的四条模型记录均加入合法、非空且不含禁用产品名的 `description`；成功加载后逐条断言 `model.description` 非空。
- 禁用名负例只把第一条合法记录的 `description` 替换为禁用产品名，其他字段与正常基线完全一致，并要求 `loadBuiltIn` 失败；因此“拒绝所有带 description 的 Registry”不能通过正例。
- 未修改生产代码或 QML。

### RED

```powershell
cmd.exe /d /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build build/msvc-debug --target voice_clone_manifest_test'
ctest --test-dir build/msvc-debug -C Debug -R "voice_clone_(manifest|capability_schema)_test" --output-on-failure
```

- VS 2022 x64 环境确认：`where cl` 指向 `Hostx64\x64\cl.exe`。
- Manifest 构建按预期 RED：`voice_clone_registry.hpp` 缺失，MSVC `fatal error C1083`。
- CTest 仍发现两个测试但无可执行文件：两项 `Not Run`，退出码 8；这与生产接口尚未实现的预期 RED 一致。
