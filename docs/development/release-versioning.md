# 正式发布版本

正式首版从 **1.0.0** 开始。此前 1.1.x 属于内部测试编号，不作为正式版发布序列。已安装内测 1.1.2 的用户需要手动安装正式 1.0.0 一次；正常语义版本比较不会将 1.0.0 识别为 1.1.2 的升级，也不为此修改比较规则。

唯一版本源为 `cmake/AgPlayerVersion.cmake`。CMake 从它生成程序版本、关于页版本、Windows EXE 的 FileVersion/ProductVersion 和 manifest；安装器版本及文件名由同一值派生。Windows PE 表示为 `1.0.0.0`。`deployment/updates/latest.json` 是网站更新接口的模板，版本工具仅同步其 `version` 字段，不发布模板到网站。

## 普通构建与首版

```powershell
./scripts/release-version.ps1
./scripts/package-windows.ps1 -BuildDirectory build/release
```

无修改参数的版本工具只校验；普通打包沿用当前版本，不递增。首版 1.0.0 使用普通打包入口。`-SkipBuild` 也不改变版本，且会核对 EXE 的真实 PE 版本，拒绝拿旧 EXE 生成新版本安装包。

打包脚本从 `CMakeCache.txt` 的 `CMAKE_CXX_COMPILER` 定位对应 Visual Studio 安装及精确 MSVC 工具集，初始化该环境并校验实际 `cl.exe` 路径；不会自动选择其他安装的最新 Build Tools。环境只影响当前脚本进程，不更改全局 Visual Studio 设置。

这些命令说明发布机制，不代表当前已满足打包授权或验收门槛。本次仅完成版本准备和脚本验证，未生成正式安装包。

## 自动递增后续正式版本

在明确准备下一次发布时，指定当前发布基线：

```powershell
# 1.0.0 -> 1.0.1（默认 Patch）
./scripts/package-windows.ps1 -BuildDirectory build/release -NewRelease -FromVersion 1.0.0

# 1.0.1 -> 1.1.0
./scripts/package-windows.ps1 -BuildDirectory build/release -NewRelease -FromVersion 1.0.1 -Bump Minor

# 1.1.0 -> 2.0.0
./scripts/package-windows.ps1 -BuildDirectory build/release -NewRelease -FromVersion 1.1.0 -Bump Major
```

目标版本由基线和增量类型自动计算。同一基线和增量命令重试时，如果版本已等于目标则沿用目标，不再加号；如果当前版本既不是基线也不是目标，拒绝执行。每个真正的新发布都应使用当时的当前版本作为新基线。

`-NewRelease` 不允许与 `-SkipBuild` 合用。预检通过后，新发布会同步两个版本文件并重新配置 CMake，确保缓存、生成头文件和 PE 资源使用新版本，然后执行构建、部署和安装器检查。任何可捕获的构建或打包异常都会恢复这两个源文件的原始字节；保留已有安装包和构建产物。恢复后若 EXE 与版本源不同，后续 `-SkipBuild` 会被版本校验拦截，需重新构建。进程被强制终止或机器断电不属于脚本可捕获的异常，应核对版本文件和实际产物后使用同一基线重试。

只准备版本、暂不打包时，可以使用：

```powershell
./scripts/release-version.ps1 -Bump Patch -FromVersion 1.0.0
./scripts/release-version.ps1 -Version 1.0.0
```

版本工具也支持直接 `-Bump Patch/Minor/Major`，但不传基线时每次调用都会递增；可重试流程应显式传 `-FromVersion`。版本写入前检查派生契约，暂存两个文件再替换；模板写入失败会恢复 CMake 版本源。各段不得超过 Windows PE 的 65535 上限。
