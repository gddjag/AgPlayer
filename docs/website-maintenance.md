# AgPlayer 官方网站

纯 HTML、CSS、原生 JavaScript；无需 npm、构建步骤或在线字体。

- `index.html`：首页、波形、功能、音频工具、迷你播放器、隐私与下载入口。
- `download.html`：独立下载页；Windows、macOS 与 Android 官网版从受校验的正式版本清单更新，HarmonyOS 引导到应用市场。
- `about.html`：独立关于页。
- `assets/css/site.css`：布局、深色样式和响应式断点。
- `assets/js/translations.js`：实际使用的中英文文案。
- `assets/js/audio-demo.js`、`waveform-renderer.js`：浏览器本地播放与音频波形。

直接用静态服务器预览此目录；执行 `node scripts/build-site.mjs` 可生成 Cloudflare Pages 使用的 `public/`，包含页面、资源和 Android 隐私政策。`public/` 为生成目录，不作为源码修改入口。

发布前执行 `node --test scripts/download-manifest.test.mjs scripts/sync-release-r2.test.mjs scripts/site-release.test.mjs`。网页从同源 `/updates/latest.json` 读取下载清单，从 1.1.2 起须包含同版本的 Windows x64 EXE、Windows 原生 ARM64 EXE、Universal DMG 和 Android ARM64 APK，固定官方 GitHub/R2 URL、文件大小和 SHA-256 均验证通过后才一起切换。接口不可用时保留最后已发布的 1.1.2；仅修改版本文案不会生成或发布安装包。

每次发布须将 R2 `https://download.agplayer.com/updates/latest.json` 实际已发布且核验通过的清单原样复制到本目录的 `updates/latest.json`，随官网构建同步部署，并同步所有已发布 fallback 的版本、文件名、大小与 SHA-256。网页使用同源请求避免跨域限制；原生应用仍使用原有 R2 更新地址。

`.github/workflows/sync-release-r2.yml` 沿用现有发布资产到 R2 的同步流程，仅在正式 GitHub Release 发布或手动指定正式 tag 时运行。先下载核验安装包，再上传并发布更新清单；不会自动清理历史发布。所需凭据使用 GitHub 原有 secrets，不存入网站源码。鸿蒙、华为 APK 与微软商店包由用户自行上传，不放入官网自动更新清单。

语言先使用浏览器首选语言，支持手动切换并保存在本机；这不等于实际国家/地区识别。后续若需要实际地区，可接入发布平台提供的地区信息。

本地试听不会上传音频。浏览器可解码的格式与桌面版可能不同。四种模式使用真实解码/播放数据；算法来源、版本差异及验收范围见 `docs/development/html-preview-validation.md`。八项音频工具为桌面版功能介绍。

## 搜索与 AI 搜索发现

规范域名为 `https://agplayer.com`，通过 canonical、站点地图和 Cloudflare 域名级 301 跳转保持一致。2026-10-26 已部署 Single Redirect `AgPlayer canonical www to apex`：匹配 `(http.host eq "www.agplayer.com" and not http.request.uri.path in {"/baidu_verify_codeva-WXajBvTPGJ.html" "/baidu_verify_codeva-WXajBvTPGJ"})`，动态目标为 `concat("https://agplayer.com", http.request.uri.path)`，状态码 301，并保留查询字符串。百度验证文件两种路径豁免跳转，继续在 www 原址返回 200。规则仅匹配 www，不影响 `download.agplayer.com` 的 R2 下载和更新接口。Pages 的 `_redirects` 不支持按来源域名重定向，不要把此规则迁入 `_redirects`。Cloudflare Pages 自动把 `.html` 地址跳转为无扩展名地址，站点地图和 canonical 因此使用 `/download`、`/about` 等实际最终地址。保留根目录 `404.html`，防止不存在的地址被 Pages 当成 SPA 首页并返回 200。

用户提供的 `baidu_verify_codeva-WXajBvTPGJ.html` 必须原样放在网站根目录并纳入构建；`_redirects` 为其 .html 地址做同源 200 代理，避免验证请求遇到 Pages 默认的去扩展名跳转。首页同时保留百度生成的 `baidu-site-verification` 标签。验证通过后不要删除验证文件或标签。

`robots.txt` 允许搜索与 AI 搜索爬虫访问，并声明 `https://agplayer.com/sitemap.xml`。`sitemap.xml` 只列正式页面，不收录设计稿、原型或安装包。新增正式页面时同步更新。首页、下载页和关于页各自包含标题、简介、canonical 与 JSON-LD；语言切换须保留各页对应的标题和简介。

`llms.txt` 提供简明产品信息和官方链接，是辅助发现文件，不代表任何 AI 平台已经收录。不要添加虚构评价、评分、下载量、认证或“保证 AI 推荐”等内容。AI 搜索爬虫仍须能从 CDN 正常抓取实际页面；仅伪装 User-Agent 的本机测试不能证明官方爬虫 IP 的网络可达性。

IndexNow 的公开验证值保存在 `indexnow-key.txt`，提交时使用 `keyLocation: https://agplayer.com/indexnow-key.txt`，并以站点地图中的规范 URL 为准。必须先确认线上验证文件和新版页面已部署，再提交 `https://api.indexnow.org/indexnow`；200 仅表示接收，202 表示仍待验证，均不等于收录。协议参与引擎共享提交，无需对同一更新反复轰炸接口。

百度通过搜索资源平台的站点验证和“普通收录”提交；Google 使用 Search Console 的站点验证、站点地图及 URL 检查；Bing 可在 Webmaster Tools 查看索引状态并提交站点地图。搜狗、360 等使用各自当前有效的站长后台。账号、手机验证、条款接受和密钥只能由已有授权的账号流程完成；百度统计 ID 不是站长验证值。不要将私有提交 token 写进 HTML、Git、`llms.txt` 或公开日志。

发布前运行 `node --test scripts/*.test.mjs`。上线后检查 robots 的正文与类型、站点地图 XML、canonical、随机不存在地址的 404、百度验证文件的原样 200 响应，以及旧版下载和隐私政策地址仍可访问。站长平台的“已接收”、搜索索引和 AI 回答引用是不同状态，分别记录，不承诺收录时限。

## Windows 双架构下载

下载页在同一 Windows 卡片中明确选择 x64（Intel / AMD 64 位）或 ARM64（Snapdragon 等 Windows ARM 电脑）；文件名、大小、R2 / GitHub 下载和 SHA-256 随选择一起切换。首页 Windows 入口进入此卡片。只有官方精确文件名、GitHub / R2 URL、正数文件大小和 SHA-256 均通过校验的 ARM64 记录才能启用；原有三个 fallback 保持原样，未完成验证前不填充 ARM64 fallback。

既有三文件 1.1.2 清单继续提供 x64、macOS、Android 下载。此次为同一发布补充 `AgPlayer-Setup-1.1.2-arm64.exe`，保留原 tag、原三份安装包及其摘要。新发布同步门禁自 1.1.2 起要求四份包；网页在 1.1.2 之后的版本也仅在四份记录全部通过校验时共同切换。清单继续采用 schemaVersion 1，以精确文件名区分 Windows 架构；可选 architecture 字段如出现须与文件名一致。

当前版本只补充 ARM64 时，不直接使用会重传全部安装包的 `sync-release-r2.mjs upload` 覆盖现有三个对象。先追加并核验 GitHub / R2 的 ARM64 安装包，再保留原三份记录生成四文件清单、更新校验清单与原有更新端点，将实际发布清单原样复制到 `updates/latest.json` 后构建部署官网。所有正式安装包和源码统一追加到项目约定的单一版本备份目录，不另建 `final`、`published` 或 `all-platforms` 目录。
