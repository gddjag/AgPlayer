(() => {
  'use strict';

  const manifestUrl = '/updates/latest.json';
  const variantsManifestUrl = '/updates/desktop-variants.json';
  const publishedDesktopVariants = {
  "schemaVersion": 1,
  "kind": "desktop-download-variants",
  "version": "1.1.2",
  "tag": "v1.1.2",
  "files": [
    {
      "id": "windows-models",
      "name": "AgPlayer-Setup-1.1.2-x64-arm64（内置伴奏分离模型版本）.exe",
      "architectures": [
        "x64",
        "arm64"
      ],
      "bundledModels": true,
      "size": 281176468,
      "sha256": "392af905b0a96daf68783901afe28b0c085fe7d055c59e1e135ccc1f938aaade",
      "sourceCommit": "a351bdb13302e38520d195df5405d40ddac0a23e",
      "r2Url": "https://download.agplayer.com/releases/v1.1.2/AgPlayer-Setup-1.1.2-x64-arm64%EF%BC%88%E5%86%85%E7%BD%AE%E4%BC%B4%E5%A5%8F%E5%88%86%E7%A6%BB%E6%A8%A1%E5%9E%8B%E7%89%88%E6%9C%AC%EF%BC%89.exe",
      "githubUrl": "https://github.com/gddjag/AgPlayer/releases/download/v1.1.2/AgPlayer-Setup-1.1.2-x64-arm64-bundled-models.exe"
    },
    {
      "id": "macos-arm64",
      "name": "AgPlayer-1.1.2-macOS-arm64.dmg",
      "architectures": [
        "arm64"
      ],
      "bundledModels": false,
      "size": 56498670,
      "sha256": "ebd4ebb314557ac6ac5551069f0a327d5fc6c959af697d8a15000725e12a3b26",
      "sourceCommit": "a351bdb13302e38520d195df5405d40ddac0a23e",
      "r2Url": "https://download.agplayer.com/releases/v1.1.2/AgPlayer-1.1.2-macOS-arm64.dmg",
      "githubUrl": "https://github.com/gddjag/AgPlayer/releases/download/v1.1.2/AgPlayer-1.1.2-macOS-arm64.dmg"
    },
    {
      "id": "macos-x86_64",
      "name": "AgPlayer-1.1.2-macOS-x86_64.dmg",
      "architectures": [
        "x86_64"
      ],
      "bundledModels": false,
      "size": 63109379,
      "sha256": "649f361d9acacab4aa14599fd1472b729523c52fcd446dfec7b42de424f60734",
      "sourceCommit": "a351bdb13302e38520d195df5405d40ddac0a23e",
      "r2Url": "https://download.agplayer.com/releases/v1.1.2/AgPlayer-1.1.2-macOS-x86_64.dmg",
      "githubUrl": "https://github.com/gddjag/AgPlayer/releases/download/v1.1.2/AgPlayer-1.1.2-macOS-x86_64.dmg"
    },
    {
      "id": "macos-models",
      "name": "AgPlayer-1.1.2-macOS-universal（内置伴奏分离模型版本）.dmg",
      "architectures": [
        "arm64",
        "x86_64"
      ],
      "bundledModels": true,
      "size": 453804812,
      "sha256": "40f792bddd77e8955c9cdac4d5f138b5cfc5bd44b843c99a29e2bc4ff8e1575e",
      "sourceCommit": "a351bdb13302e38520d195df5405d40ddac0a23e",
      "r2Url": "https://download.agplayer.com/releases/v1.1.2/AgPlayer-1.1.2-macOS-universal%EF%BC%88%E5%86%85%E7%BD%AE%E4%BC%B4%E5%A5%8F%E5%88%86%E7%A6%BB%E6%A8%A1%E5%9E%8B%E7%89%88%E6%9C%AC%EF%BC%89.dmg",
      "githubUrl": "https://github.com/gddjag/AgPlayer/releases/download/v1.1.2/AgPlayer-1.1.2-macOS-universal-bundled-models.dmg"
    }
  ]
};
  const maxManifestBytes = 65536;
  const requestTimeoutMs = 10000;
  const stableVersion = /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/;
  const publishedWindowsRelease = Object.freeze({
    version: '1.1.2',
    size: 33111791,
    githubUrl: 'https://github.com/gddjag/AgPlayer/releases/download/v1.1.2/AgPlayer-Setup-1.1.2-x64.exe?revision=20261028',
    r2Url: 'https://download.agplayer.com/releases/v1.1.2/AgPlayer-Setup-1.1.2-x64.exe?revision=20261028',
    sha256: '390D69FB32D3C6177E3BBB925B8A174ABBEFC0A494A39BAC560114DF9C32820C'
  });
  const publishedWindowsArmRelease = Object.freeze({
    version: '1.1.2',
    size: 29635362,
    githubUrl: 'https://github.com/gddjag/AgPlayer/releases/download/v1.1.2/AgPlayer-Setup-1.1.2-arm64.exe?revision=20261028',
    r2Url: 'https://download.agplayer.com/releases/v1.1.2/AgPlayer-Setup-1.1.2-arm64.exe?revision=20261028',
    sha256: '4EAAD3326064BAB5C7D4EF069A81B47F8185431CF3BA11DB0141061995FC03BB'
  });
  const publishedMacRelease = Object.freeze({
    version: '1.1.2',
    size: 115237077,
    githubUrl: 'https://github.com/gddjag/AgPlayer/releases/download/v1.1.2/AgPlayer-1.1.2-macOS-universal.dmg?revision=20261028',
    r2Url: 'https://download.agplayer.com/releases/v1.1.2/AgPlayer-1.1.2-macOS-universal.dmg?revision=20261028',
    sha256: 'F4CA11BDD3A599E43B3E78478063B79748BED1715A1379BCB6580005D581B94F'
  });
  const publishedAndroidRelease = Object.freeze({
    version: '1.1.2',
    size: 237844183,
    githubUrl: 'https://github.com/gddjag/AgPlayer/releases/download/v1.1.2/AgPlayer-1.1.2-arm64-release.apk',
    r2Url: 'https://download.agplayer.com/releases/v1.1.2/AgPlayer-1.1.2-arm64-release.apk',
    sha256: 'D1C1739FFF87F9D49F9F04A0D5D53EE0C5BED2AB70F1BB596659BB2EE8374777'
  });

  function selectRelease(manifest, platform = 'windows', architecture = 'x64') {
    if (!manifest || manifest.schemaVersion !== 1 || typeof manifest.version !== 'string' || !stableVersion.test(manifest.version)) return null;
    const tag = `v${manifest.version}`;
    if (manifest.tag !== tag
      || typeof manifest.publishedAt !== 'string'
      || !Number.isFinite(Date.parse(manifest.publishedAt))
      || manifest.releaseNotesUrl !== `https://github.com/gddjag/AgPlayer/releases/tag/${tag}`
      || !Array.isArray(manifest.files)) return null;
    const names = {
      windows: `AgPlayer-Setup-${manifest.version}-${architecture}.exe`,
      macos: `AgPlayer-${manifest.version}-macOS-universal.dmg`,
      android: `AgPlayer-${manifest.version}-arm64-release.apk`
    };
    const name = names[platform];
    if (!name) return null;
    const matches = manifest.files.filter(item => item?.name === name);
    const file = matches[0];
    const expectedArchitecture = platform === 'windows' ? architecture : platform === 'macos' ? 'universal' : 'arm64';
    if (!file || matches.length !== 1 || (file.architecture !== undefined && file.architecture !== expectedArchitecture) || !Number.isSafeInteger(file.size) || file.size <= 0 || !/^[a-f0-9]{64}$/.test(file.sha256)) return null;
    const encodedName = encodeURIComponent(name);
    if (file.revision !== undefined && (!['macos', 'windows'].includes(platform) || typeof file.revision !== 'string' || !/^\d{8}$/.test(file.revision))) return null;
    const revisionQuery = file.revision === undefined ? '' : `?revision=${file.revision}`;
    const githubUrl = `https://github.com/gddjag/AgPlayer/releases/download/${tag}/${encodedName}${revisionQuery}`;
    const r2Url = `https://download.agplayer.com/releases/${tag}/${encodedName}${revisionQuery}`;
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

  function selectDesktopVariants(manifest) {
    if (!manifest || manifest.schemaVersion !== 1 || manifest.kind !== 'desktop-download-variants'
      || manifest.version !== publishedWindowsRelease.version || manifest.tag !== `v${manifest.version}`
      || !Array.isArray(manifest.files) || manifest.files.length !== 4) return null;
    const version = manifest.version;
    const specs = {
      'windows-models': [`AgPlayer-Setup-${version}-x64-arm64（内置伴奏分离模型版本）.exe`, ['x64', 'arm64'], true, `AgPlayer-Setup-${version}-x64-arm64-bundled-models.exe`],
      'macos-arm64': [`AgPlayer-${version}-macOS-arm64.dmg`, ['arm64'], false],
      'macos-x86_64': [`AgPlayer-${version}-macOS-x86_64.dmg`, ['x86_64'], false],
      'macos-models': [`AgPlayer-${version}-macOS-universal（内置伴奏分离模型版本）.dmg`, ['arm64', 'x86_64'], true, `AgPlayer-${version}-macOS-universal-bundled-models.dmg`]
    };
    const releases = {};
    for (const file of manifest.files) {
      const spec = specs[file?.id];
      if (!spec || releases[file.id] || file.name !== spec[0] || file.bundledModels !== spec[2]
        || !Array.isArray(file.architectures) || JSON.stringify(file.architectures) !== JSON.stringify(spec[1])
        || !Number.isSafeInteger(file.size) || file.size <= 0 || !/^[a-f0-9]{64}$/.test(file.sha256)
        || !/^[a-f0-9]{40}$/.test(file.sourceCommit)) return null;
      const encoded = encodeURIComponent(file.name);
      const githubEncoded = encodeURIComponent(spec[3] || file.name);
      if (file.githubUrl !== `https://github.com/gddjag/AgPlayer/releases/download/${manifest.tag}/${githubEncoded}`
        || file.r2Url !== `https://download.agplayer.com/releases/${manifest.tag}/${encoded}`) return null;
      releases[file.id] = { ...file, version, sha256: file.sha256.toUpperCase() };
    }
    return releases;
  }

  function releaseFilename(release) {
    return decodeURIComponent(release.r2Url.split('?')[0].split('/').at(-1));
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
    if (!windows.querySelector('.platform-meta')) windows.querySelector('h2')?.after(details('downloadPage.windows.system', AG.t('downloadPage.windows.system'), 'AgPlayer-Setup-1.1.2-x64.exe', '31.6 MB'));
    if (!macos.querySelector('.platform-meta')) macos.querySelector('h2')?.after(details('downloadPage.macos.system', AG.t('downloadPage.macos.system'), 'AgPlayer-1.1.2-macOS-universal.dmg', '109.9 MB'));
    if (!android.querySelector('.platform-meta')) android.querySelector('h2')?.after(details('downloadPage.android.system', AG.t('downloadPage.android.system'), 'AgPlayer-1.1.2-arm64-release.apk', '226.8 MB'));
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
    github.title = decodeURIComponent(release.githubUrl.split("?")[0].split("/").at(-1));
    const filename = document.querySelector(`#${platform} [data-meta="filename"]`);
    const size = document.querySelector(`#${platform} [data-meta="size"]`);
    const checksum = document.querySelector(`#${platform}-sha256`);
    if (filename) filename.textContent = releaseFilename(release);
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
    const architecture = document.querySelector('#windows-architecture');
    const armOption = document.querySelector('#windows-architecture option[value="arm64"]');
    const bundledOption = document.querySelector('#windows-architecture option[value="bundled"]');
    const macArchitecture = document.querySelector('#macos-architecture');
    const macOptions = ['arm64', 'x86_64', 'bundled'].map(value => document.querySelector(`#macos-architecture option[value="${value}"]`));
    const windowsReleases = { x64: publishedWindowsRelease, arm64: publishedWindowsArmRelease };
    const macReleases = { universal: publishedMacRelease };
    const renderSelectedMacRelease = () => renderLinkedRelease('macos', macReleases[macArchitecture?.value] || macReleases.universal);
    macArchitecture?.addEventListener('change', renderSelectedMacRelease);
    const renderVariantOptions = () => {
      if (bundledOption) bundledOption.disabled = !windowsReleases.bundled;
      ['arm64', 'x86_64', 'bundled'].forEach((key, i) => { if (macOptions[i]) macOptions[i].disabled = !macReleases[key]; });
    };
    renderVariantOptions();
    if (architecture) architecture.value = 'x64';
    const renderArchitectureOption = () => {
      if (!armOption) return;
      armOption.disabled = !windowsReleases.arm64;
      armOption.dataset.i18n = windowsReleases.arm64 ? 'downloadPage.windows.arm64' : 'downloadPage.windows.arm64Pending';
      armOption.textContent = AG.t(armOption.dataset.i18n);
    };
    renderArchitectureOption();
    let activeRelease = publishedWindowsRelease;
    const renderRelease = release => {
      activeRelease = release;
      enable(primary, release.r2Url);
      enable(github, release.githubUrl);
      github.title = decodeURIComponent(release.githubUrl.split("?")[0].split("/").at(-1));
      const filename = document.querySelector('#windows [data-meta="filename"]');
      const size = document.querySelector('#windows [data-meta="size"]');
      if (filename) filename.textContent = releaseFilename(release);
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
    architecture?.addEventListener('change', () => {
      const release = windowsReleases[architecture.value];
      if (release) renderRelease(release);
    });
    renderRelease(publishedWindowsRelease);
    renderLinkedRelease('macos', publishedMacRelease);
    renderLinkedRelease('android', publishedAndroidRelease);
    const applyDesktopVariants = manifest => {
      const variants = selectDesktopVariants(manifest);
      if (!variants) return;
      windowsReleases.bundled = variants['windows-models'];
      Object.assign(macReleases, {arm64: variants['macos-arm64'], x86_64: variants['macos-x86_64'], bundled: variants['macos-models']});
      renderVariantOptions();
      renderRelease(windowsReleases[architecture?.value] || windowsReleases.x64);
      renderSelectedMacRelease();
    };
    const loadDesktopVariants = async () => {
      if (!bundledOption && !macArchitecture) return;
      applyDesktopVariants(publishedDesktopVariants);
      const abort = new AbortController();
      const deadline = setTimeout(() => abort.abort(), requestTimeoutMs);
      try {
        const response = await fetch(variantsManifestUrl, {cache:'no-store', credentials:'omit', headers:{Accept:'application/json'}, signal:abort.signal});
        if (!response.ok) return;
        const text = await readManifest(response);
        if (text !== null) applyDesktopVariants(JSON.parse(text));
      } catch (_) {
        // Keep the verified variant fallbacks if this independent endpoint is unavailable.
      } finally {
        clearTimeout(deadline);
      }
    };
    void loadDesktopVariants();
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
      const armRelease = selectRelease(manifest, 'windows', 'arm64');
      const hasArmFile = manifest.files?.some(file => file?.name === `AgPlayer-Setup-${manifest.version}-arm64.exe`);
      if (!release || !macRelease || !androidRelease) return;
      const [major, minor, patch] = manifest.version.split('.').map(BigInt);
      const requiresArm = major > 1n || (major === 1n && (minor > 1n || (minor === 1n && patch > 2n)));
      // Legacy three-file manifests remain usable; any supplied ARM64 file must validate.
      if ((requiresArm || hasArmFile) && !armRelease) return;
      windowsReleases.x64 = release;
      windowsReleases.arm64 = armRelease;
      renderArchitectureOption();
      renderRelease(windowsReleases[architecture?.value] || release);
      macReleases.universal = macRelease;
      renderSelectedMacRelease();
      renderLinkedRelease('android', androidRelease);
    } catch (_) {
      // Keep the last published downloads when live metadata is unavailable.
    } finally {
      clearTimeout(timeout);
    }
  }

  if (document.body?.dataset.page === 'download') void hydrateDownloads();
})();
