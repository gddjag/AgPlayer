# 沉浸视觉第三方来源与许可审计

## 审计结论

V4.6 HTML 声明其中央反应堆是对第三方浏览器实现的适配。因此该 HTML 仅用于需求、布局、参数和感知效果研究；AgPlayer 原生实现没有复制其中的 JavaScript、GLSL、常量组织或算法结构。

本分支采用 AgPlayer 自有 Qt 6.7 / QML / C++17 / QRhi 架构，从现有 `PlaybackController.spectrum`、BPM/播放位置、歌词服务和波形会话取数。仓库中未引入 Three.js、WebEngine、Electron、远程网页或第二套音频解码链。

## 来源登记

| 来源 | HTML 声明 | 官方许可依据 | 本分支处理 |
|---|---|---|---|
| `yin-yizhen/sonic-topography` | 1.1.1 / `3ff303e`，Non-Commercial Learning License | [官方 LICENSE](https://github.com/yin-yizhen/sonic-topography/blob/master/LICENSE) 仅允许学习、研究和个人非商业使用，商业或派生商业使用需明确许可 | 不复制源码、Shader、算法；只研究视频/截图呈现的产品效果 |
| `ww085213/Mineradio-LX-Music` | `public/sonic-topography-preset.js`，GPL-3.0-only | [官方 README](https://github.com/ww085213/Mineradio-LX-Music/blob/main/README.md) 声明 GPL-3.0-only | 不复制或移植该文件；避免给 AgPlayer 引入 GPL 派生义务 |
| Three.js r128 与后处理脚本 | HTML 通过 jsDelivr 远程加载 | [Three.js 官方 LICENSE](https://github.com/mrdoob/three.js/blob/dev/LICENSE) 为 MIT | 正式产品不包含或加载 Three.js；原生 QRhi 实现无需该依赖 |

## 清洁实现检查

- 最终生产代码扫描未出现 `sonic-topography`、`Mineradio`、`THREE`、`UnrealBloom`、第三方提交号或 HTML 的 `SONIC_*` 标识符。
- 原生 Shader 使用 AgPlayer 自有 uniform、实例数据、生命周期和音频特征结构；没有把 HTML 中的 GLSL 文本纳入构建。
- HTML 的本地文件选择、WebAudio analyser、整曲波形解码和 Demo 音乐未进入正式实现。
- 本分支新增依赖数为零；沉浸模式使用 Qt/QRhi 和项目已有依赖。
- 人工 Diff Review 未发现参考实现的源码片段、命名结构或运行时资源进入生产代码。

## 限定说明

本记录是工程来源审计，不是法律意见。未来若直接引入参考项目源码、Shader、预设文件或算法实现，必须重新完成许可证兼容性评估，不能沿用本次“清洁原生重写”结论。
