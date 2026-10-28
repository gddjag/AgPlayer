# Website maintenance

- Keep the public repository root `README.md` as the AgPlayer product introduction with its screenshots. Website implementation, build and release notes belong in `docs/website-maintenance.md`; a website sync must never replace the product README with developer notes. Preserve the README's referenced images when publishing website files.

- Preserve the Baidu analytics script at the end of `index.html` during future updates and deployments.
- The authorized site ID is `d753c9d11730ddacf686e8ccddce6e11`. Include it exactly once on the homepage.
- Keep local previews and published source consistent; do not remove or replace the analytics integration without an explicit user request.

- 2026-10-28 用户要求：官网页面不展示每次发布的最新功能、修复摘要、版本更新说明或同版本重新安装提示。下载卡片保留系统要求、架构、文件信息、下载入口、校验值和必要的渠道或安全说明；不要在后续发版时重新加入此次删除的 Windows/macOS 更新摘要、macOS 通用包说明及 Android 功能说明。

# Synchronized releases

- 2026-10-28 用户新增分类：Windows 常规纯净版仍分别提供 x64、ARM64；新增自动选择原生架构的 x64／ARM64 合一内置伴奏分离模型包。Mac 常规纯净版分别提供 Apple M 系列（arm64）、Intel（x86_64），另提供 Apple／Intel Universal 内置模型包。内置包外部文件名含完整“（内置伴奏分离模型版本）”。精确分类见根 AGENTS.md；不可混装架构或把同一 Universal 文件假标成两份 thin 包。
- 新分类记录在独立 `/updates/desktop-variants.json`，逐项校验精确中文文件名、官方编码 URL、架构集合、模型包含状态、大小、SHA-256 和来源提交；原 `/updates/latest.json` 四条基础应用更新记录及历史下载保持兼容。网站脚本保留已验证分类 fallback；未验证、缺失或损坏的分类不能启用下载。
- Windows／Mac 卡片底部：“常规纯净版不含伴奏分离模型，可在「音频工具」中按需下载。”Android／鸿蒙卡片底部：“已内置伴奏分离模型。”这是用户明确要求的下载分类说明，不属于上文禁止重新加入的版本功能更新摘要。
- GitHub 会自动删除中文文件名（上传与 PATCH 均已实测），因此仅其镜像使用 `-bundled-models` 英文后缀和完整中文原文件名 label；R2 主下载、本地及商店包保留中文标记。分类清单的 R2 URL 与 GitHub URL 分别按固定精确名称校验，同一项的文件字节和摘要必须相同，不扩展为任意镜像名。

- Publish verified Windows x64 and native ARM64, Universal macOS, and ARM64 Android website installers under the same version and release tag. Promote the shared download manifest only after all four matching installers are available and their sizes and SHA-256 checksums are verified.
- Keep the macOS download, checksum, actual signing/notarization status, and Apple opening guide (`https://support.apple.com/zh-cn/102445`) together. Do not describe ad-hoc signing as Developer ID signing or notarization. State Android signing changes truthfully; never promise upgrade compatibility across different signing keys.
- Archive the exact published installers and matching full source, including this website, before completing each release. Back up all platform source, scripts, resources, public configuration, and release records to `/Volumes/软件文档/AgPlayer 备份/YYYY-MM-DD-vX.Y.Z`; never overwrite an earlier backup or include private signing material in public packages or Git.
- Record the source commit, all platform versions, channels, and artifact checksums. Never move an existing release tag to misrepresent its source. HarmonyOS, Huawei Android, and Microsoft Store submissions are performed by the user unless separately authorized.
- The website reads `/updates/latest.json` from its own origin to avoid cross-origin browser restrictions. On every release, copy the actual verified, published R2 manifest byte-for-byte into `updates/latest.json`, include it in the website build, and keep every published fallback download record aligned. Native app update endpoints and fixed official GitHub/R2 URL validation remain unchanged.

- From v1.1.2 onward, Windows website releases require both `AgPlayer-Setup-X.Y.Z-x64.exe` and `AgPlayer-Setup-X.Y.Z-arm64.exe`. The existing v1.1.2 release may be augmented only after the native ARM64 installer is verified; preserve its existing x64, macOS, and Android assets, tag, and source provenance. Historical three-file manifests must keep serving x64; never invent ARM64 links, hashes, or sizes before verification.
- Use a single canonical backup folder per version under the project root release agreement, including the full source, all platforms/channels, website, and exact final packages; append verified ARM64 to the existing version backup rather than creating another version folder. Keep private signing recovery data separately in `/Volumes/软件文档/AgPlayer 备份/私有签名资料`.
