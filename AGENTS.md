# Website maintenance

- Preserve the Baidu analytics script at the end of `index.html` during future updates and deployments.
- The authorized site ID is `d753c9d11730ddacf686e8ccddce6e11`. Include it exactly once on the homepage.
- Keep local previews and published source consistent; do not remove or replace the analytics integration without an explicit user request.

# Synchronized releases

- Publish verified Windows, Universal macOS, and ARM64 Android website installers under the same version and release tag. Promote the shared download manifest only after all three matching installers are available and their sizes and SHA-256 checksums are verified.
- Keep the macOS download, checksum, actual signing/notarization status, and Apple opening guide (`https://support.apple.com/zh-cn/102445`) together. Do not describe ad-hoc signing as Developer ID signing or notarization. State Android signing changes truthfully; never promise upgrade compatibility across different signing keys.
- Archive the exact published installers and matching full source, including this website, before completing each release. Back up all platform source, scripts, resources, public configuration, and release records to `/Volumes/软件文档/AgPlayer 备份/YYYY-MM-DD-vX.Y.Z-all-platforms`; never overwrite an earlier backup or include private signing material in public packages or Git.
- Record the source commit, all platform versions, channels, and artifact checksums. Never move an existing release tag to misrepresent its source. HarmonyOS, Huawei Android, and Microsoft Store submissions are performed by the user unless separately authorized.
