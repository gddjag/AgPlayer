# AgPlayer 官方网站

纯 HTML、CSS、原生 JavaScript；无需 npm、构建步骤或在线字体。

- `index.html`：首页、波形、功能、音频工具、迷你播放器、隐私与下载入口。
- `download.html`：独立下载页；Windows、macOS 与 Android 官网版从受校验的正式版本清单更新，HarmonyOS 引导到应用市场。
- `about.html`：独立关于页。
- `assets/css/site.css`：布局、深色样式和响应式断点。
- `assets/js/translations.js`：实际使用的中英文文案。
- `assets/js/audio-demo.js`、`waveform-renderer.js`：浏览器本地播放与音频波形。

直接用静态服务器预览此目录；执行 `node scripts/build-site.mjs` 可生成 Cloudflare Pages 使用的 `public/`，包含页面、资源和 Android 隐私政策。`public/` 为生成目录，不作为源码修改入口。

发布前执行 `node --test scripts/download-manifest.test.mjs scripts/sync-release-r2.test.mjs scripts/site-release.test.mjs`。下载清单来自 `https://download.agplayer.com/updates/latest.json`，须包含同版本的 EXE、Universal DMG 和 ARM64 APK，固定官方 URL、文件大小和 SHA-256 均验证通过后才一起切换。接口不可用时保留最后已发布的 1.1.2；仅修改版本文案不会生成或发布安装包。

`.github/workflows/sync-release-r2.yml` 沿用现有发布资产到 R2 的同步流程，仅在正式 GitHub Release 发布或手动指定正式 tag 时运行。先下载核验安装包，再上传并发布更新清单；不会自动清理历史发布。所需凭据使用 GitHub 原有 secrets，不存入网站源码。鸿蒙、华为 APK 与微软商店包由用户自行上传，不放入官网自动更新清单。

语言先使用浏览器首选语言，支持手动切换并保存在本机；这不等于实际国家/地区识别。后续若需要实际地区，可接入发布平台提供的地区信息。

本地试听不会上传音频。浏览器可解码的格式与桌面版可能不同。四种模式使用真实解码/播放数据；算法来源、版本差异及验收范围见 `docs/development/html-preview-validation.md`。八项音频工具为桌面版功能介绍。
