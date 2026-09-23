# 专属音乐格式输入支持：实施与验收记录

## 本轮实现（2026-09-23）

- 核心新增按需打开的可定位流式输入；普通音频仍交给 FFmpeg 直接打开，不新增运行时依赖、模型或预解密临时文件。Windows 与 macOS 共享同一 C++ 实现。
- 已接入播放解码、元数据探测、波形所用的 `Decoder`、转码预检及转码输入。文件夹扫描与三个模式共享的拖入逻辑只扩展候选后缀；歌单仍记录原路径。
- 已实现文件内材料足够的 NCM、KGM/KGMA/VPR、KWM、QMC 静态/Map/RC4。NCM 容器内标题、歌手、专辑和嵌入封面可供媒体信息及支持该封面的输出格式使用。QQ 的 QTag 和旧式文件尾内嵌 EKey 可处理；STag、缺少 EKey、KGM v5 等文件明确失败，不读取客户端数据库。
- 除最初列出的 QMC0/QMC3/QMCFLAC/QMCOGG、MFLAC/MGG 外，候选后缀覆盖 QMC2/4/6/8、MFLAC0、MGG0/1/L，以及 TKM/BKC 系列。后缀只是候选；不是每个后缀的所有文件版本都经真实文件验证。
- 测试包含 Unlock Music 的五组非商用 QMC 二进制向量（静态、Map、RC4）及基于 MusicKey 格式构造器生成的九个 440 Hz 样本（含 JPEG 封面 NCM 变体）。逐一验证完整流、跨边界随机读取、FFmpeg 媒体探测、解码定位与 WAV 转换；另验证 QQ 输入转 MP3、NCM 输入转 FLAC 并保留标签和封面。测试样本及 MIT 许可声明在仓库 `tests/fixtures/proprietary/` 与 `LICENSES/`。
- Windows Release `AgPlayer` 构建通过。`proprietary_audio_input_test`、`decoder_test`、`transcoder_test`、`resource_folder_controller_test`、`import_controller_test`、`audio_file_discovery_test`、`qml_format_converter_test` 均通过。未在此轮进行真实平台文件、Mac 真机、Explorer 实际拖入或手工听音验收。
- 同一 Release 构建目录中的 `AgPlayer.exe` 从本轮开始前的 13,469,696 字节变为 13,505,024 字节，增加 35,328 字节；这不是安装包体积对比，也不是普通格式运行速度测量。本轮未打安装包或发布。

## 范围

目标是让可独立处理的网易云、QQ 音乐、酷狗、酷我本地文件沿用现有播放、歌单、元数据、波形和格式转换流程。Windows 先验收；输入处理使用可供 macOS、鸿蒙复用的 C++ 代码。汽水音乐、独立转换页面和“恢复原始格式”按钮不在范围内。

扩展名只用于发现候选文件，不能据此承诺任意加密版本可播放。第一批候选：NCM；QMC0/QMC3/QMCFLAC/QMCOGG，以及文件自身带有必要材料的 MFLAC/MGG；KGM/KGMA/VPR；KWM。具体变体以真实样本和校验结果确定。缺少文件级处理材料的变体须给出明确错误，不调用平台账号或客户端数据库，不输出伪音频。

## 已确认的仓库接入点

- 文件夹与拖入的扩展名筛选：`qt/src/audio_file_discovery.cpp`。直接拖入的文件已经走实际探测，资源文件夹的共享路由不可改动。
- 播放：`core/src/audio_engine.cpp` 的 `PlaybackDecoder` 使用 `core/src/decoder.cpp`。`DecoderOpenOptions` 已有自定义读取、定位和中断回调，普通媒体保持现有文件快速路径。
- 歌单元数据：`qt/src/import_controller.cpp` 调用 `ag_metadata_open`，最终由 `core/src/decoder.cpp` 的 `probe_media_metadata` 打开文件。歌单记录应始终保存原文件路径。
- 波形：核心波形分析使用 `Decoder::readAnalysis`；列表缩略图和主波形应复用现有提供者，不依赖播放进度。
- 格式转换：`app/qml/AgPlayer/components/tools/FormatConvertPage.qml` 的文件筛选与 `qt/src/format_converter.cpp` 的批量队列最终进入 `core/src/transcode_probe.cpp` 和 `core/src/transcoder.cpp`；两者目前自行打开 FFmpeg 输入。
- 文件关联单独由 `qt/src/file_association_controller.cpp` 管理；新增发现能力不应自动抢默认关联。

## 实施次序与可验证交付

1. **输入契约。** 在核心定义文件级判别、逻辑音频长度、按逻辑偏移读取/定位、取消和明确失败原因。普通 MP3/FLAC/WAV 的路径不经过适配层。测试覆盖错误后缀、截断头尾、越界定位、取消、缺少必要材料。
2. **逐变体解码。** 每个有样本的变体独立验证：解出的头部需被 FFmpeg 识别，完整读取有音频帧，时长和末尾可定位；同一扩展名的未知变体不能按成功处理。保存来源、版本和样本验收矩阵。非商用合成样本与开源测试向量已纳入仓库，真实平台媒体文件不纳入。
3. **播放与媒体信息。** 接入 `Decoder` 和元数据探测，验证添加/拖入、列表双击、暂停、定位、上一首/下一首、保存并重新打开歌单；缺失元数据沿用文件名回退。
4. **波形与转换。** 主波形、列表缩略波形后台完整分析；在现有批量转换队列中选择 MP3/WAV/FLAC 及当前编码器支持的其他格式。每个输出必须经过实际解码校验，失败文件不得留下看似成功的结果。
5. **跨平台与体积门槛。** Windows 完成后，在 macOS/鸿蒙复用核心并分别验证文件路径、随机定位、播放、波形及转换。不得新增模型或常驻运行时。安装包体积与普通格式播放性能均需相同配置的前后对比；无法达到用户确认的硬门槛时不宣称完成。

## 当前门槛

- 本工作区仍有上一轮 MP3 坏帧修复的未提交改动；本功能不得覆盖或混入该修复。
- 本轮开始前 Windows `build/release/app/AgPlayer.exe` 为 13,469,696 字节；这不是安装包基线。
- 需要四家真实、可用于本地验收的代表性文件路径。公开项目列出的后缀不等于 AgPlayer 已支持的变体。
- 用户已明确：不新增依赖或模型，允许少量原生代码增长。交付时仍需比较包体和普通格式性能，不把未测量的变化说成零开销。
- 用户暂无会员样本。验收先采用公开项目的非商用测试向量与自生成样本；真实平台文件变体仍需后续样本确认，不能宣称全覆盖。

## 技术依据

- FFmpeg [自定义 AVIO 读取示例](https://www.ffmpeg.org/doxygen/trunk/avio__reading_8c_source.html)说明以回调向解复用器提供输入；实际实现还需按当前 FFmpeg 头文件验证 `seek` 和 `AVSEEK_SIZE`。
- [Unlock Music 项目](https://github.com/Feng11z/unlock-music)列出多平台后缀，但不能代替变体验收。
- [QMC2 项目说明](https://github.com/bczhc/qmc-decode)明确部分 MFLAC/MGG 文件需要 EKey，不能把扩展名当作离线可处理的充分条件。

## 本轮实现与验证（2026-09-23）

- 已将按需读取、随机定位的输入适配接入现有播放解码、元数据探测和格式转换；普通媒体仍走原有 FFmpeg 文件路径。没有新增产品运行时依赖或模型。
- 已实现 NCM、带内嵌密钥或静态密钥的 QMC 变体、已知 KGM/KGMA/VPR 变体，以及已知 KWM 容器和原始 ADTS AAC 变体。缺少内嵌密钥的 QMC、需外部授权数据的 KGM v5 会明确报错。列出的候选扩展名不是全变体兼容承诺。
- 使用 Unlock Music 的 MIT 测试向量和 MusicKey 方法生成的非商业 440 Hz 样本，验证逻辑字节流、随机定位、媒体探测、播放解码入口、WAV/MP3/FLAC 转换及 NCM 标题、歌手、专辑和封面保留。
- Windows Release `AgPlayer` 编译通过；上述功能相关的 7 项测试通过。当前 EXE 为 13,505,024 字节，较本轮前增加 35,328 字节。安装包体积和普通媒体性能未测量。
- 尚无四个平台的真实专属文件样本，不能确认实际平台变体覆盖；macOS、鸿蒙编译与设备验证、手动听音和资源管理器拖入操作均未完成。本轮未打包或发布。

## 审查后修复

- 自定义 AVIO 关闭路径已释放 FFmpeg 当前缓冲区，包括播放 Decoder 路径。Windows 核心连续探测 1,000 次 NCM 的进程私有内存增量从约 66 MB 降到 20 KB。
- NCM 元数据解析现在按 JSON 顶层键定位，并读取完整歌手数组。标题为 `album`、实际专辑为 `Correct Album`、两位歌手的合成样本，探测和保留元数据的 FLAC 转换均通过。
