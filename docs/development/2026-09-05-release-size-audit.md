# Release 体积与轻量化核查

2026-09-05；隔离工作树 `D:/ai/AgPlayer/.worktrees/full-ui-performance-20260905`。先完成只读测量，再经授权修改正式打包过滤与确认未使用的资源白名单；不删除既有包或仓库素材，不生成安装包。以下 MiB 均为 1,048,576 字节。首表是调查开始时的二进制快照，后续共享 UI 构建会改变 EXE；资源精简的比较基线另见末节。

## 测量边界

| 对象 | 字节 | MiB | 含义 |
| --- | ---: | ---: | --- |
| 当前 `build/release/app` 全目录 | 251,365,861 | 239.721 | 开发构建目录，包含对象、静态库、生成源码；不是发布包 |
| 当前 `AgPlayer.exe` | 11,102,208 | 10.588 | 已构建主应用，包含静态业务代码、QML AOT 与资源 |
| 当前 `AgSeparationWorker.exe` | 510,464 | 0.487 | 按需分离 worker；不包含 AI 模型和完整推理运行时 |
| 当前根目录 `Qt6*.dll` | 50,045,808 | 47.727 | 含正式打包脚本会剔除的非 Basic 风格 DLL |
| 当前 FFmpeg 与音频编解码 DLL | 20,169,216 | 19.235 | avcodec/avformat/avutil/swscale/swresample/LAME/Opus/Ogg/Vorbis |
| 当前部署 `qml/` | 3,982,244 | 3.798 | 456 个文件，含非 Basic 风格和工具类型描述 |
| 历史 main `build/package/AgPlayer` | 123,884,417 | 118.145 | 现有实际暂存目录，12:43 历史包；不代表本轮最终产物 |
| 历史安装器 `AgPlayer-Setup-1.0.0-x64.exe` | 35,594,025 | 33.945 | 12:43 历史产物，仅作口径参考 |

构建目录另有约 45.1 MiB `agplayer_app_qml.lib`、47.15 MiB `CMakeFiles`、21.98 MiB `.rcc` 生成源码、24.25 MiB `vc_redist.x64.exe`。正式 `scripts/package-windows.ps1` 不复制这些构建中间文件，清理它们不会缩小正式安装包。历史安装器启用 `lzma2/ultra64` 和 solid compression；不能将磁盘字节减少直接等同于压缩安装器减少。

没有发现本地适用的额外 AGENTS.md；按任务授权、已有安装器合同和脚本约束检查。未修改 main 历史包。

## 当前打包逻辑已经做到的减项

正式脚本只复制主 EXE、worker、音频依赖白名单和许可证，再由 windeployqt 扫描 QML。它已经剔除 `qmltooling` 与六套非 Basic Controls 风格。用本机 Qt 6.7.0 执行相同参数的 `--dry-run --json`，512 个候选文件中已有剔除规则覆盖 297 个、8,499,642 字节（8.106 MiB）；这是既有效果，不能重复归功于本轮优化。

清单保存在 `build/qa/size-windeploy-dry-run.json`，该文件带有 windeployqt 的说明行；解析时从首个 `{` 开始。只写报告，没有创建目标部署目录。当前 shell 没有 SDK DXC 搜索环境，dry-run 报告未找到 dxcompiler/dxil；因此 74,102,082 字节的保留 Qt 候选清单不是完整最终安装大小。

## 推荐的最小新减项

在正式脚本的既有 staging 安全边界内，剔除部署 `qml/` 下的 `*.qmltypes`，保留全部 `qmldir`、`.qml`、JS、DLL 和许可证。当前 dry-run 和历史净包一致：16 个文件合计 **1,164,172 字节（1.110 MiB）**。这些文件描述供 Qt Creator 等 QML 工具读取的类型，不提供运行时实现。[Qt 官方模块定义文档](https://doc.qt.io/qt-6.10/qtqml-modules-qmldir.html)、[Qt 官方 qmltyperegistrar 文档](https://doc.qt.io/qt-6/qtqml-tooling-qmltyperegistrar.html)

这只适合应用运行时发布目录；开发 SDK/QML 工具环境保留类型描述。收益是可核算的安装后磁盘占用，压缩包收益需将来授权打包后实测。

验证要求：临时目录中证明只删除 `.qmltypes`，保留同目录 `qmldir`、QML、JS、插件和许可证；安装器合同测试；最终隔离暂存目录净 PATH 下完成所有 QML 页面、文件/颜色对话框及退出回归。不因脚本合同通过宣称最终安装验收通过。

## 大文件为什么暂不删

| 项目 | 可见占用 | 判断 |
| --- | ---: | --- |
| `opengl32sw.dll` | 20,639,888 B | 软件 OpenGL 回退；直接删会收缩显卡/后端兼容范围，需单独验证 OpenGL、D3D 和软件场景 |
| `dxcompiler.dll` + `dxil.dll` | 15,825,264 B | 图形 shader 编译支持；PE 未直接导入不代表不会按需加载，不能仅凭未使用某后端删除 |
| `d3dcompiler_47.dll` | 历史包 4,741,488 B | Windows 图形后端依赖，本机 dry-run 源版本不同；不能以本机系统已安装代替干净环境验证 |
| 未被现有包 PE 直接导入的 5 个 CRT 文件 | 797,296 B | 全包 EXE/DLL 导入表扫描未引用 concrt/atomic_wait/codecvt_ids/vccorlib/threads；仍有动态加载与 CRT 分发版本兼容问题，本轮保留完整 app-local CRT |
| FFmpeg | 19.235 MiB（连同外部音频库） | manifest 已关闭默认 features，当前需要音频、视频、封面、转换功能；进一步裁 codec 会改变支持范围 |
| `Qt6Widgets.dll` | 6,574,224 B | QApplication、系统窗口/对话框现有实现需要，移除会成为架构改造 |

未提出删除 TLS、图像格式、SVG、原生对话框插件等来换体积。AI 模型、Python 环境、ONNX Runtime 仍为外部按需资源；它们的额外安装量应另列，不能偷偷计入基础包，也不能为了缩包删除已装用户模型。

## EXE、资源和重复项

PE 节分析：主 EXE `.text` 约 5.45 MiB，`.rdata` 约 4.55 MiB，Windows `.rsrc` 约 69.5 KiB；Qt 内嵌资源还会进入只读数据，不能仅凭 `.rsrc` 大小判断全部资源体积。Release 使用 `/O2 /Ob2 /DNDEBUG`，没有看到启用跨目标 IPO/LTCG。

调查开始时资源显式列表为三份品牌资源加图标：源总量 270,842 字节（0.258 MiB），后续取消嵌入的内容见末节。仓库中的几张 1–2 MiB 海豚概念 PNG 不在 `BRAND_FILES` 中，也不被打包复制；删除它们不会缩小应用。当前主 EXE 包含编译 QML，不能删除 AOT 缓存或原始 QML 而假定所有动态加载/诊断路径仍有效。

历史实际暂存目录按文件长度预分组后进行 SHA-256 去重扫描，仅发现两个内容相同的 215 字节 `.qmltypes`，额外重复量仅 215 字节，已被上述 `.qmltypes` 减项覆盖。没有可直接移除的重复大型 DLL。

可留作下一轮的配置实验：在单独构建目录用 `check_ipo_supported()` 限定受支持的 Release targets 试验 IPO/LTCG，比较同编译器 EXE/worker 大小、完整测试和既有性能基准。未实测前没有 MB 收益承诺。为缩代码切换全项目 `/O1`、删 RTTI/异常、UPX 或禁用视频均不符合当前功能/性能合同。

## 发现的旧暂存脚本合同偏差

调查时 `tools/stage_release.ps1` 与正式脚本不是同一发布路径：它广泛复制 Qt DLL/运行时目录，未执行非 Basic 风格清理、复制 `vc_redist.x64.exe`，而且缺少 `swscale-*.dll` 与许可证复制。当前 AgPlayer.exe 的 PE 导入表直接要求 `swscale-9.dll`，缺少该文件的目录不能作为可运行包。正式脚本已正确包含 swscale 和许可证。主线后续已删除这个旧暂存旁路，统一使用正式脚本；不能把缺依赖造成的减少当作收益。

## 已实施的最小修改与进一步核查

- `scripts/package-windows.ps1` 新增发布 QML 类型描述清理函数，并在现有风格清理后调用；返回文件数和字节数，路径限定 staging；保留 Qt 安装树与其他发布内容。
- `tests/scripts/package_qml_metadata_test.ps1` 只从生产脚本 AST 加载该函数，不执行任何构建/打包命令。在真实临时目录验证递归只删 `.qmltypes`、删除两份共 8 字节、保留 qmldir/QML/JS/DLL/LICENSE/备份扩展名和目录外文件、重复执行/缺目录无操作。已通过。
- `installer_contract_test.ps1` 已挂接该行为测试；整份安装器合同也已通过。Diff Review 与 whitespace 检查通过。
- 初步 dry-run 源文件长度求和为 **16 份、1,164,172 字节**；随后在全新 `build/qa/qmltypes-smoke-0be79c21a8934cae8e9e71e56e3172ab` 中复制实际 Qt 运行文件，并独立核验绝对路径后调用生产清理函数，实际移除同样 16 份、1,164,172 字节，结果见 `build/qa/size-qml-metadata-measurement.json`。没有删除既有包或 Qt 安装树。早先一个组合临时复制/清理命令被自动审批以 `blocked by policy` 拒绝，未执行；之后采用只追加创建与独立路径核验的分步方式成功完成测量，未绕过该拒绝。

调查开始时图标收录为 99 份显式 SVG 白名单而非整套主题；没有 FontLoader 或捆绑 ttf/otf。该时点 `build/release/app/AgPlayer` 中 79 QML/1 JS/99 SVG/2 PNG 等是开发工具副本，正式脚本和历史实际包都不含 `AgPlayer/` 或 `qml/AgPlayer/`，因此没有额外可减的发布双份应用 QML。Qt translations 已通过 `--no-translations` 排除，应用 zh/en 翻译仍必需。Qt runtime QML 的文件选择器及回退加载未证明可省略，保留。

回溯 Theme.icon、SideNavigation 图标映射、stemIcon、Settings/工具栏固定数组和所有状态三元表达式后，确认 19 个旧 SVG 无运行引用；`logo-lockup.png` 无运行引用，仅由图像测试以磁盘 URL 使用。经授权从 `app/CMakeLists.txt` 取消这 20 个资源的嵌入；主线随后删除 19 个无调用 SVG 源文件，保留 logo-lockup 磁盘测试 fixture。原始素材共 **87,186 字节**（19 SVG 12,526 字节 + logo 74,660 字节）；这不是压缩 PE 净收益。逐项清单和候选补丁保存在 `build/qa/unused-embedded-resources-evidence.md`、`build/qa/unused-embedded-resources.patch.txt`。取消 LibraryManager 功能由主线单独处理，不混入这个资源变更。

最初 16:06:06 EXE 快照 11,109,888 字节之后混入过其他 UI 修正，不用作资源净收益的基线。主代理冻结其他生产源码，仅切换这 20 个资源条目执行受控双构建，实测 **11,102,720 → 10,999,296 字节，净减少 103,424 字节（101 KiB）**。证据为 `build/qa/size-controlled-before.json` 和 `size-controlled-after.json`；before SHA-256 `99F819F455003CCEBEC8343EB0290C81FF8091D2FC50835D9522E244EFDBC399`，after SHA-256 `82FFBC98B4CE5377967E928D14AB1F707206428E70F266279116100B5080E012`。PE 对齐及资源索引等会使实际差值与原始素材字节不同。资源后 28/28 QML 与包合同检查通过（79.65 s，由主代理执行）。

## 独立运行验收完成

最终验证目录为 `build/qa/qmltypes-smoke-d973ce1214804377bf6bdcd2ecc50ee9`。旧 QA 准备脚本对 manifest 的绝对插件目标错误使用 Substring，导致临时运行目录缺少 Qt platform 插件；主代理将这个 QA 脚本改为 GetRelativePath 并建立新目录。该问题不在生产打包脚本中，旧目录的启动尝试不计通过。新目录使用可重定位的 patched Qt6Core、相邻 `qml/`、`qt.conf` 和隔离 PATH；生产清理函数实际移除 16 个 `.qmltypes`、1,164,172 字节，最终递归检查 `.qmltypes` 残留为 0。

| 实际检查 | 结果 | 证据（相对最终验证目录） |
| --- | --- | --- |
| 播放器、设置、格式转换、音频编辑四个真实页面 | 4/4：进程 Exit 0、截图存在、日志无 WARN/ERROR/FATAL；主代理已查看四图 | `qa-runtime-results.json`、`qa-player/settings/conversion/editor.png` 及对应 `.log` |
| QML 导入路径 | 导入 trace 记录在 `qa-*.log`；只见隔离运行树与 qrc 导入，未出现 `D:/Qt` 或 `vcpkg` 引用 | 四份 `qa-*.log`；JSON 中 ImportTraceBytes 是 stderr 长度，不代表实际导入日志为空 |
| 所有公开格式真实导出并重开、无损 profile 读回 | 两个业务用例及 init/cleanup 共 4/4 通过，958 ms | `qa-real-conversion.txt` |
| 原生文件/颜色对话框打开与关闭 | 两个业务用例及 init/cleanup 共 4/4 通过，无 warning，2097 ms | `qa-native-dialogs-final.txt` |
| 生产格式转换对话框按钮角色与滚动详情 | 一个业务用例及 init/cleanup 共 3/3 通过，128 ms | `qa-format-dialogs.txt` |

原生对话框初轮在 close 后立即销毁测试环境出现 QCRITICAL；测试等待关闭完成后再析构，最终上述日志无 warning，未修改生产逻辑。转换与对话框验证临时添加 QtTest/QuickTest DLL、QtTest QML 和测试 runner；这些仅为 QA 辅助文件，不计基础发布体积。

主线集成另完成全量 163/163 测试（347.60 s）、构建/lint 和 18 张深浅主题截图，通过结果由主代理汇总；本子任务核对了上述独立目录的 JSON、导入日志和三个测试日志。本轮四页运行冒烟不等于全平台、正式安装器或人工听音验收。未生成正式安装包或发布，最终安装器体积仍需在授权发布时对当时的新产物重新记录。
