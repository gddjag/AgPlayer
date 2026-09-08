(() => {
  'use strict';

  const manifestUrl = 'https://download.agplayer.com/updates/latest.json';
  const maxManifestBytes = 65536;
  const requestTimeoutMs = 10000;
  const stableVersion = /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/;

  function selectWindowsRelease(manifest) {
    if (!manifest || manifest.schemaVersion !== 1 || typeof manifest.version !== 'string' || !stableVersion.test(manifest.version)) return null;
    const tag = `v${manifest.version}`;
    if (manifest.tag !== tag
      || typeof manifest.publishedAt !== 'string'
      || !Number.isFinite(Date.parse(manifest.publishedAt))
      || manifest.releaseNotesUrl !== `https://github.com/gddjag/AgPlayer/releases/tag/${tag}`
      || !Array.isArray(manifest.files)) return null;
    const name = `AgPlayer-Setup-${manifest.version}-x64.exe`;
    const file = manifest.files.find(item => item?.name === name);
    if (!file || !Number.isSafeInteger(file.size) || file.size <= 0 || !/^[a-f0-9]{64}$/.test(file.sha256)) return null;
    const encodedName = encodeURIComponent(name);
    const githubUrl = `https://github.com/gddjag/AgPlayer/releases/download/${tag}/${encodedName}`;
    const r2Url = `https://download.agplayer.com/releases/${tag}/${encodedName}`;
    if (file.githubUrl !== githubUrl || file.r2Url !== r2Url) return null;
    return { version: manifest.version, githubUrl, r2Url };
  }

  function enable(button, url) {
    button.disabled = false;
    button.classList.remove('disabled');
    button.removeAttribute('aria-disabled');
    button.addEventListener('click', () => location.assign(url));
  }

  async function readManifest(response) {
    const declaredLength = response.headers.get('content-length');
    if (declaredLength !== null && (!/^\d+$/.test(declaredLength) || Number(declaredLength) > maxManifestBytes)) return null;
    if (!response.body?.getReader) return null;
    const reader = response.body.getReader();
    const chunks = [];
    let size = 0;
    try {
      while (true) {
        const { done, value } = await reader.read();
        if (done) break;
        if (!(value instanceof Uint8Array)) return null;
        size += value.byteLength;
        if (size > maxManifestBytes) {
          await reader.cancel();
          return null;
        }
        chunks.push(value);
      }
    } finally {
      reader.releaseLock();
    }
    const bytes = new Uint8Array(size);
    let offset = 0;
    for (const chunk of chunks) {
      bytes.set(chunk, offset);
      offset += chunk.byteLength;
    }
    return new TextDecoder('utf-8', { fatal: true }).decode(bytes);
  }

  async function hydrateDownloads() {
    const primary = document.querySelector('.download-primary');
    const github = document.querySelector('.download-github');
    const status = document.querySelector('#windows-download-status');
    if (!primary || !github || !status) return;
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), requestTimeoutMs);
    try {
      const response = await fetch(manifestUrl, {
        cache: 'no-store',
        credentials: 'omit',
        headers: { Accept: 'application/json' },
        signal: controller.signal
      });
      if (!response.ok) return;
      const text = await readManifest(response);
      if (text === null) return;
      const release = selectWindowsRelease(JSON.parse(text));
      if (!release) return;
      enable(primary, release.r2Url);
      enable(github, release.githubUrl);
      status.removeAttribute('data-i18n');
      const renderStatus = () => { status.textContent = AG.t('downloadPage.windows.available', { version: release.version }); };
      renderStatus();
      document.addEventListener('ag:languagechange', renderStatus);
    } catch (_) {
      // Keep the honest pre-release state for unavailable or malformed metadata.
    } finally {
      clearTimeout(timeout);
    }
  }

  if (document.body?.dataset.page === 'download') void hydrateDownloads();
})();
