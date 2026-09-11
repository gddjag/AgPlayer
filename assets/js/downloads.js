(() => {
  'use strict';

  const manifestUrl = 'https://download.agplayer.com/updates/latest.json';
  const maxManifestBytes = 65536;
  const requestTimeoutMs = 10000;
  const stableVersion = /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/;
  const publishedWindowsRelease = Object.freeze({
    version: '1.0.1',
    githubUrl: 'https://github.com/gddjag/AgPlayer/releases/download/v1.0.1/AgPlayer-Setup-1.0.1-x64.exe',
    r2Url: 'https://download.agplayer.com/releases/v1.0.1/AgPlayer-Setup-1.0.1-x64.exe',
    sha256: '42CBAB739E254C776B60F48EE544D42C6FF2A97676D6DCC220FEBC3A4B584D30'
  });

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
    return { version: manifest.version, githubUrl, r2Url, sha256: file.sha256.toUpperCase() };
  }

  function enable(button, url) {
    button.dataset.downloadUrl = url;
    if (button.dataset.downloadBound !== 'true') {
      button.addEventListener('click', () => location.assign(button.dataset.downloadUrl));
      button.dataset.downloadBound = 'true';
    }
    button.disabled = false;
    button.classList.remove('disabled');
    button.removeAttribute('aria-disabled');
  }

  function ensureDownloadDetails() {
    const windows = document.querySelector('#windows');
    const macos = document.querySelector('#macos');
    if (!windows || !macos) return;
    const details = (systemKey, systemText, filename, size) => {
      const list = document.createElement('dl');
      list.className = 'platform-meta';
      list.innerHTML = `<div><dt data-i18n="downloadPage.meta.system">${AG.t('downloadPage.meta.system')}</dt><dd data-i18n="${systemKey}">${systemText}</dd></div><div><dt data-i18n="downloadPage.meta.filename">${AG.t('downloadPage.meta.filename')}</dt><dd>${filename}</dd></div><div><dt data-i18n="downloadPage.meta.size">${AG.t('downloadPage.meta.size')}</dt><dd>${size}</dd></div>`;
      return list;
    };
    if (!windows.querySelector('.platform-meta')) windows.querySelector('h2')?.after(details('downloadPage.windows.system', AG.t('downloadPage.windows.system'), 'AgPlayer-Setup-1.0.1-x64.exe', '34.5 MB'));
    if (!macos.querySelector('.platform-meta')) macos.querySelector('h2')?.after(details('downloadPage.macos.system', AG.t('downloadPage.macos.system'), AG.t('downloadPage.meta.pending'), '—'));
    if (!windows.querySelector('.release-checksum')) {
      windows.insertAdjacentHTML('beforeend', `<div id="windows-checksum" class="release-checksum" hidden><span data-i18n="downloadPage.checksum.label">${AG.t('downloadPage.checksum.label')}</span><code id="windows-sha256"></code><button type="button" id="windows-sha256-copy" disabled aria-disabled="true" data-i18n="downloadPage.checksum.copy">${AG.t('downloadPage.checksum.copy')}</button></div>`);
    }
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
    ensureDownloadDetails();
    const primary = document.querySelector('.download-primary');
    const github = document.querySelector('.download-github');
    const status = document.querySelector('#windows-download-status');
    const checksum = document.querySelector('#windows-checksum');
    const checksumValue = document.querySelector('#windows-sha256');
    const checksumCopy = document.querySelector('#windows-sha256-copy');
    if (!primary || !github || !status || !checksum || !checksumValue || !checksumCopy) return;
    let activeRelease = publishedWindowsRelease;
    const renderStatus = () => { status.textContent = AG.t('downloadPage.windows.available', { version: activeRelease.version }); };
    const renderRelease = release => {
      activeRelease = release;
      enable(primary, release.r2Url);
      enable(github, release.githubUrl);
      checksumValue.textContent = release.sha256;
      checksum.removeAttribute('hidden');
      checksumCopy.disabled = false;
      checksumCopy.removeAttribute('aria-disabled');
      status.removeAttribute('data-i18n');
      renderStatus();
    };
    checksumCopy.addEventListener('click', () => {
      void navigator.clipboard?.writeText(activeRelease.sha256).catch(() => {});
    });
    document.addEventListener('ag:languagechange', renderStatus);
    renderRelease(publishedWindowsRelease);
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
      renderRelease(release);
    } catch (_) {
      // Keep the honest pre-release state for unavailable or malformed metadata.
    } finally {
      clearTimeout(timeout);
    }
  }

  if (document.body?.dataset.page === 'download') void hydrateDownloads();
})();

