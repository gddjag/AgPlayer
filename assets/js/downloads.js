(() => {
  'use strict';

  const manifestUrl = 'https://download.agplayer.com/updates/latest.json';
  const maxManifestBytes = 65536;
  const requestTimeoutMs = 10000;
  const stableVersion = /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/;
  const publishedWindowsRelease = Object.freeze({
    version: '1.0.9',
    size: 36691029,
    githubUrl: 'https://github.com/gddjag/AgPlayer/releases/download/v1.0.9/AgPlayer-Setup-1.0.9-x64.exe',
    r2Url: 'https://download.agplayer.com/releases/v1.0.9/AgPlayer-Setup-1.0.9-x64.exe',
    sha256: '23BFD1616449506E1727D77ABE13E1A2258AB11886EEF24F67885F61A78C1C3D'
  });
  const publishedMacRelease = Object.freeze({
    version: '1.0.9',
    size: 95010781,
    githubUrl: 'https://github.com/gddjag/AgPlayer/releases/download/v1.0.9/AgPlayer-1.0.9-macOS-universal.dmg',
    r2Url: 'https://download.agplayer.com/releases/v1.0.9/AgPlayer-1.0.9-macOS-universal.dmg',
    sha256: '85AE4BBEFCAA189544C531817E92787141DEAA8FC4C68F615D584F718E8EF779'
  });
  const publishedAndroidRelease = Object.freeze({
    version: '1.0.9',
    size: 239792754,
    githubUrl: 'https://github.com/gddjag/AgPlayer/releases/download/v1.0.9/AgPlayer-1.0.9-arm64-release.apk',
    r2Url: 'https://download.agplayer.com/releases/v1.0.9/AgPlayer-1.0.9-arm64-release.apk',
    sha256: '4813D65F4F5A3CA197664BFE45A512B8C00D95AF879F44C2669A179A8278ECEC'
  });

  function selectRelease(manifest, platform = 'windows') {
    if (!manifest || manifest.schemaVersion !== 1 || typeof manifest.version !== 'string' || !stableVersion.test(manifest.version)) return null;
    const tag = `v${manifest.version}`;
    if (manifest.tag !== tag
      || typeof manifest.publishedAt !== 'string'
      || !Number.isFinite(Date.parse(manifest.publishedAt))
      || manifest.releaseNotesUrl !== `https://github.com/gddjag/AgPlayer/releases/tag/${tag}`
      || !Array.isArray(manifest.files)) return null;
    const names = {
      windows: `AgPlayer-Setup-${manifest.version}-x64.exe`,
      macos: `AgPlayer-${manifest.version}-macOS-universal.dmg`,
      android: `AgPlayer-${manifest.version}-arm64-release.apk`
    };
    const name = names[platform];
    if (!name) return null;
    const file = manifest.files.find(item => item?.name === name);
    if (!file || !Number.isSafeInteger(file.size) || file.size <= 0 || !/^[a-f0-9]{64}$/.test(file.sha256)) return null;
    const encodedName = encodeURIComponent(name);
    const githubUrl = `https://github.com/gddjag/AgPlayer/releases/download/${tag}/${encodedName}`;
    const r2Url = `https://download.agplayer.com/releases/${tag}/${encodedName}`;
    if (file.githubUrl !== githubUrl || file.r2Url !== r2Url) return null;
    return { version: manifest.version, size: file.size, githubUrl, r2Url, sha256: file.sha256.toUpperCase() };
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
    const android = document.querySelector('#android');
    if (!windows || !macos || !android) return;
    const details = (systemKey, systemText, filename, size) => {
      const list = document.createElement('dl');
      list.className = 'platform-meta';
      list.innerHTML = `<div><dt data-i18n="downloadPage.meta.system">${AG.t('downloadPage.meta.system')}</dt><dd data-i18n="${systemKey}">${systemText}</dd></div><div><dt data-i18n="downloadPage.meta.filename">${AG.t('downloadPage.meta.filename')}</dt><dd data-meta="filename">${filename}</dd></div><div><dt data-i18n="downloadPage.meta.size">${AG.t('downloadPage.meta.size')}</dt><dd data-meta="size">${size}</dd></div>`;
      return list;
    };
    if (!windows.querySelector('.platform-meta')) windows.querySelector('h2')?.after(details('downloadPage.windows.system', AG.t('downloadPage.windows.system'), 'AgPlayer-Setup-1.0.9-x64.exe', '35.0 MB'));
    if (!macos.querySelector('.platform-meta')) macos.querySelector('h2')?.after(details('downloadPage.macos.system', AG.t('downloadPage.macos.system'), 'AgPlayer-1.0.9-macOS-universal.dmg', '待发布'));
    if (!android.querySelector('.platform-meta')) android.querySelector('h2')?.after(details('downloadPage.android.system', AG.t('downloadPage.android.system'), 'AgPlayer-1.0.9-arm64-release.apk', '228.7 MB'));
    const platforms = windows.closest('.platforms');
    let checksum = document.querySelector('#windows-checksum');
    if (!checksum) {
      checksum = document.createElement('div');
      checksum.id = 'windows-checksum';
      checksum.className = 'release-checksum';
      checksum.hidden = true;
      checksum.innerHTML = `<div class="checksum-row"><span data-i18n="downloadPage.checksum.windowsLabel">${AG.t('downloadPage.checksum.windowsLabel')}</span><code id="windows-sha256"></code><button type="button" id="windows-sha256-copy" disabled aria-disabled="true" data-i18n="downloadPage.checksum.copy">${AG.t('downloadPage.checksum.copy')}</button></div>`;
    }
    if (platforms && checksum.previousElementSibling !== platforms) platforms.insertAdjacentElement('afterend', checksum);
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

  function renderLinkedRelease(platform, release) {
    const primary = document.querySelector(`#${platform} .download-${platform}-primary`);
    const github = document.querySelector(`#${platform} .download-${platform}-github`);
    if (!primary || !github) return;
    primary.href = release.r2Url;
    github.href = release.githubUrl;
    const filename = document.querySelector(`#${platform} [data-meta="filename"]`);
    const size = document.querySelector(`#${platform} [data-meta="size"]`);
    const checksum = document.querySelector(`#${platform}-sha256`);
    const suffix = platform === 'macos' ? 'macOS-universal.dmg' : 'arm64-release.apk';
    if (filename) filename.textContent = `AgPlayer-${release.version}-${suffix}`;
    if (size) size.textContent = `${(release.size / 1048576).toFixed(1)} MB`;
    if (checksum) checksum.textContent = release.sha256;
  }

  function bindChecksumCopy(platform) {
    const button = document.querySelector(`#${platform}-sha256-copy`);
    const value = document.querySelector(`#${platform}-sha256`);
    if (button && value) button.addEventListener('click', () => {
      void navigator.clipboard?.writeText(value.textContent).catch(() => {});
    });
  }

  async function hydrateDownloads() {
    ensureDownloadDetails();
    bindChecksumCopy('macos');
    bindChecksumCopy('android');
    const primary = document.querySelector('.download-primary');
    const github = document.querySelector('.download-github');
    const status = document.querySelector('#windows-download-status');
    const checksum = document.querySelector('#windows-checksum');
    const checksumValue = document.querySelector('#windows-sha256');
    const checksumCopy = document.querySelector('#windows-sha256-copy');
    if (!primary || !github || !status || !checksum || !checksumValue || !checksumCopy) return;
    let activeRelease = publishedWindowsRelease;
    const renderRelease = release => {
      activeRelease = release;
      enable(primary, release.r2Url);
      enable(github, release.githubUrl);
      const filename = document.querySelector('#windows [data-meta="filename"]');
      const size = document.querySelector('#windows [data-meta="size"]');
      if (filename) filename.textContent = `AgPlayer-Setup-${release.version}-x64.exe`;
      if (size && Number.isSafeInteger(release.size)) size.textContent = `${(release.size / 1048576).toFixed(1)} MB`;
      checksumValue.textContent = release.sha256;
      checksum.removeAttribute('hidden');
      checksumCopy.disabled = false;
      checksumCopy.removeAttribute('aria-disabled');
      status.textContent = '';
      status.hidden = true;
    };
    checksumCopy.addEventListener('click', () => {
      void navigator.clipboard?.writeText(activeRelease.sha256).catch(() => {});
    });
    renderRelease(publishedWindowsRelease);
    renderLinkedRelease('macos', publishedMacRelease);
    renderLinkedRelease('android', publishedAndroidRelease);
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
      const manifest = JSON.parse(text);
      const release = selectRelease(manifest);
      const macRelease = selectRelease(manifest, 'macos');
      const androidRelease = selectRelease(manifest, 'android');
      if (!release || !macRelease || !androidRelease) return;
      renderRelease(release);
      renderLinkedRelease('macos', macRelease);
      renderLinkedRelease('android', androidRelease);
    } catch (_) {
      // Keep the last published downloads when live metadata is unavailable.
    } finally {
      clearTimeout(timeout);
    }
  }

  if (document.body?.dataset.page === 'download') void hydrateDownloads();
})();
