# Website maintenance

- Preserve the Baidu analytics script at the end of `index.html` during future updates and deployments.
- The authorized site ID is `d753c9d11730ddacf686e8ccddce6e11`. Include it exactly once on the homepage.
- Keep local previews and published source consistent; do not remove or replace the analytics integration without an explicit user request.

# Synchronized desktop releases

- From version 1.0.3 onward, publish Windows and macOS under the same version and release tag. Both verified installers must exist before promoting the shared download manifest.
- Keep the macOS download link, checksum, and Apple opening guide (`https://support.apple.com/zh-cn/102445`) together. State the actual signing/notarization status; the accepted 1.0.3 macOS package is ad-hoc signed and not notarized.
- Archive the exact published installers and their matching clean source before completing a release. Record platform-specific source commits when adding macOS to an existing Windows release; never move the existing release tag to misrepresent its source.
