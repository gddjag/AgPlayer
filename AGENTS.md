# Website maintenance

- Keep the public repository root `README.md` as the AgPlayer product introduction with its screenshots. Website implementation, build and release notes belong in `docs/website-maintenance.md`; a website sync must never replace the product README with developer notes. Preserve the README's referenced images when publishing website files.

- Preserve the Baidu analytics script at the end of `index.html` during future updates and deployments.
- The authorized site ID is `d753c9d11730ddacf686e8ccddce6e11`. Include it exactly once on the homepage.
- Keep local previews and published source consistent; do not remove or replace the analytics integration without an explicit user request.

- 2026-10-28 用户要求：官网页面不展示每次发布的最新功能、修复摘要、版本更新说明或同版本重新安装提示。下载卡片保留系统要求、架构、文件信息、下载入口、校验值和必要的渠道或安全说明；不要在后续发版时重新加入此次删除的 Windows/macOS 更新摘要、macOS 通用包说明及 Android 功能说明。

# Synchronized releases

- Publish verified Windows x64 and native ARM64, Universal macOS, and ARM64 Android website installers under the same version and release tag. Promote the shared download manifest only after all four matching installers are available and their sizes and SHA-256 checksums are verified.
- Keep the macOS download, checksum, actual signing/notarization status, and Apple opening guide (`https://support.apple.com/zh-cn/102445`) together. Do not describe ad-hoc signing as Developer ID signing or notarization. State Android signing changes truthfully; never promise upgrade compatibility across different signing keys.
- Archive the exact published installers and matching full source, including this website, before completing each release. Back up all platform source, scripts, resources, public configuration, and release records to `/Volumes/软件文档/AgPlayer 备份/YYYY-MM-DD-vX.Y.Z`; never overwrite an earlier backup or include private signing material in public packages or Git.
- Record the source commit, all platform versions, channels, and artifact checksums. Never move an existing release tag to misrepresent its source. HarmonyOS, Huawei Android, and Microsoft Store submissions are performed by the user unless separately authorized.
- The website reads `/updates/latest.json` from its own origin to avoid cross-origin browser restrictions. On every release, copy the actual verified, published R2 manifest byte-for-byte into `updates/latest.json`, include it in the website build, and keep every published fallback download record aligned. Native app update endpoints and fixed official GitHub/R2 URL validation remain unchanged.

- From v1.1.2 onward, Windows website releases require both `AgPlayer-Setup-X.Y.Z-x64.exe` and `AgPlayer-Setup-X.Y.Z-arm64.exe`. The existing v1.1.2 release may be augmented only after the native ARM64 installer is verified; preserve its existing x64, macOS, and Android assets, tag, and source provenance. Historical three-file manifests must keep serving x64; never invent ARM64 links, hashes, or sizes before verification.
- Use a single canonical backup folder per version under the project root release agreement, including the full source, all platforms/channels, website, and exact final packages; append verified ARM64 to the existing version backup rather than creating another version folder. Keep private signing recovery data separately in `/Volumes/软件文档/AgPlayer 备份/私有签名资料`.
