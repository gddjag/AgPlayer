import test from 'node:test';
import assert from 'node:assert/strict';
import { existsSync } from 'node:fs';
import { readFile } from 'node:fs/promises';
import { join } from 'node:path';
import { runInNewContext } from 'node:vm';

const scriptPath = join(import.meta.dirname, '..', 'assets', 'js', 'downloads.js');
const downloadPagePath = join(import.meta.dirname, '..', 'download.html');

function fakeElement() {
  const attributes = new Map([['aria-disabled', 'true']]);
  const classes = new Set(['disabled']);
  const listeners = new Map();
  const element = {
    disabled: true,
    textContent: '',
    dataset: {},
    classList: {
      contains: name => classes.has(name),
      remove: name => classes.delete(name)
    },
    addEventListener: (name, listener) => listeners.set(name, listener),
    setAttribute: (name, value) => attributes.set(name, value),
    removeAttribute: name => attributes.delete(name),
    hasAttribute: name => attributes.has(name),
    click: () => listeners.get('click')?.(),
    change: () => listeners.get('change')?.()
  };
  return element;
}

function streamedResponse(body, { ok = true, contentLength = true, chunkSize = body.length } = {}) {
  const bytes = new TextEncoder().encode(body);
  const chunks = [];
  for (let offset = 0; offset < bytes.length; offset += chunkSize) chunks.push(bytes.slice(offset, offset + chunkSize));
  let index = 0;
  return {
    ok,
    headers: { get: name => contentLength && name.toLowerCase() === 'content-length' ? String(bytes.length) : null },
    body: { getReader: () => ({
      read: async () => index < chunks.length ? { done: false, value: chunks[index++] } : { done: true },
      cancel: async () => {},
      releaseLock: () => {}
    }) }
  };
}

async function runDownloadScript(response, timers = {}, withVariants = false, withPublishedVariants = false) {
  assert.ok(existsSync(scriptPath), 'download manifest integration script must exist');
  const architecture = fakeElement();
  architecture.value = 'x64';
  const armOption = fakeElement();
  const bundledOption = fakeElement();
  const macArchitecture = fakeElement();
  macArchitecture.value = '';
  const macOptions = ['arm64', 'x86_64', 'bundled'].map(() => fakeElement());
  const windowsFilename = fakeElement();
  const macFilename = fakeElement();
  const primary = fakeElement();
  const github = fakeElement();
  const status = fakeElement();
  const checksum = fakeElement();
  const checksumValue = fakeElement();
  const checksumCopy = fakeElement();
  const macPrimary = fakeElement();
  const macGithub = fakeElement();
  const macChecksum = fakeElement();
  const macChecksumCopy = fakeElement();
  const androidPrimary = fakeElement();
  const androidGithub = fakeElement();
  const androidChecksum = fakeElement();
  const androidChecksumCopy = fakeElement();
  macChecksum.textContent = '54FD7E5CA2317F41F9EB24039CFD562DFB7F21FDA11E736110152F1792DF5F58';
  androidChecksum.textContent = 'E0BB91145396ADDBFDEB1CE73C4303EE808EF017E34EAEB9F04129C8066B53A7';
  checksum.setAttribute('hidden', '');
  const documentListeners = new Map();
  const calls = [];
  const navigations = [];
  const clipboardWrites = [];
  const elements = new Map([
    ['#windows-architecture', architecture],
    ['#windows-architecture option[value="arm64"]', armOption],
    ['.download-primary', primary],
    ['.download-github', github],
    ['#windows-download-status', status],
    ['#windows-checksum', checksum],
    ['#windows-sha256', checksumValue],
    ['#windows-sha256-copy', checksumCopy],
    ['#macos .download-macos-primary', macPrimary],
    ['#macos .download-macos-github', macGithub],
    ['#macos-sha256', macChecksum],
    ['#macos-sha256-copy', macChecksumCopy],
    ['#android .download-android-primary', androidPrimary],
    ['#android .download-android-github', androidGithub],
    ['#android-sha256', androidChecksum],
    ['#android-sha256-copy', androidChecksumCopy]
  ]);
  if (withVariants) {
    elements.set('#windows-architecture option[value="bundled"]', bundledOption);
    elements.set('#macos-architecture', macArchitecture);
    ['arm64', 'x86_64', 'bundled'].forEach((value, i) => elements.set(`#macos-architecture option[value="${value}"]`, macOptions[i]));
    elements.set('#windows [data-meta="filename"]', windowsFilename);
    elements.set('#macos [data-meta="filename"]', macFilename);
  }
  const context = {
    document: {
      body: { dataset: { page: 'download' } },
      querySelector: selector => elements.get(selector),
      addEventListener: (name, listener) => documentListeners.set(name, listener)
    },
    fetch: async (url, options) => {
      calls.push({ url, options });
      return typeof response === 'function' ? response(url, options) : response;
    },
    location: { assign: url => navigations.push(url) },
    navigator: { clipboard: { writeText: async text => clipboardWrites.push(text) } },
    AG: { t: (key, values) => `${key}:${values?.version ?? ""}` },
    AbortController,
    TextDecoder,
    Uint8Array,
    setTimeout: timers.setTimeout ?? setTimeout,
    clearTimeout: timers.clearTimeout ?? clearTimeout,
    console
  };
  let source = await readFile(scriptPath, 'utf8');
  if (!withPublishedVariants) source = source.replace(/const publishedDesktopVariants = [\s\S]*?;\n  const maxManifestBytes/, 'const publishedDesktopVariants = null;\n  const maxManifestBytes');
  runInNewContext(source, context, { filename: scriptPath });
  await new Promise(resolve => setTimeout(resolve, 0));
  return { architecture, armOption, bundledOption, macArchitecture, macOptions, windowsFilename, macFilename, primary, github, status, checksum, checksumValue, checksumCopy, macPrimary, macGithub, macChecksum, macChecksumCopy, androidPrimary, androidGithub, androidChecksum, androidChecksumCopy, calls, navigations, clipboardWrites, documentListeners };
}

function manifest(overrides = {}) {
  const version = '1.0.4';
  const name = `AgPlayer-Setup-${version}-x64.exe`;
  return {
    schemaVersion: 1,
    version,
    tag: `v${version}`,
    publishedAt: '2026-09-08T06:00:00Z',
    releaseNotesUrl: `https://github.com/gddjag/AgPlayer/releases/tag/v${version}`,
    files: [{
      name,
      size: 123456,
      sha256: 'a'.repeat(64),
      githubUrl: `https://github.com/gddjag/AgPlayer/releases/download/v${version}/${name}`,
      r2Url: `https://download.agplayer.com/releases/v${version}/${name}`
    }, macFile(version), androidFile(version)],
    ...overrides
  };
}

function macFile(version = '1.0.4') {
  const name = `AgPlayer-${version}-macOS-universal.dmg`;
  return { name, size: 89747916, sha256: 'b'.repeat(64),
    githubUrl: `https://github.com/gddjag/AgPlayer/releases/download/v${version}/${name}`,
    r2Url: `https://download.agplayer.com/releases/v${version}/${name}` };
}

function androidFile(version = '1.0.4') {
  const name = `AgPlayer-${version}-arm64-release.apk`;
  return { name, size: 239792754, sha256: 'c'.repeat(64),
    githubUrl: `https://github.com/gddjag/AgPlayer/releases/download/v${version}/${name}`,
    r2Url: `https://download.agplayer.com/releases/v${version}/${name}` };
}

test('one verified manifest updates all downloadable platforms and rejects a forged route', async () => {
  const result = await runDownloadScript(streamedResponse(JSON.stringify(manifest())));
  assert.equal(result.macPrimary.href, macFile().r2Url);
  assert.equal(result.macGithub.href, macFile().githubUrl);
  assert.equal(result.macChecksum.textContent, 'B'.repeat(64));
  assert.equal(result.androidPrimary.href, androidFile().r2Url);
  assert.equal(result.androidGithub.href, androidFile().githubUrl);
  assert.equal(result.androidChecksum.textContent, 'C'.repeat(64));
  const bad = manifest();
  bad.files[1].r2Url = 'https://example.com/forged.dmg';
  const rejected = await runDownloadScript(streamedResponse(JSON.stringify(bad)));
  assert.equal(rejected.macPrimary.href, 'https://download.agplayer.com/releases/v1.1.2/AgPlayer-1.1.2-macOS-universal.dmg?revision=20261030');
  assert.equal(rejected.checksumValue.textContent, '6A508E91AD9218589B1113EDB10314105B4852B8B38474A40A1D8DCA44A63DE2');
});

test('macOS HTML fallback links the accepted package and official opening guide', async () => {
  const html = await readFile(downloadPagePath, 'utf8');
  assert.match(html, /href="https:\/\/support\.apple\.com\/zh-cn\/102445"/);
  assert.match(html, /尚未经过 Apple 公证/);
  assert.match(html, /href="https:\/\/download\.agplayer\.com\/releases\/v1\.1\.2\/AgPlayer-1\.1\.2-macOS-arm64\.dmg\?revision=20261030"/);
  assert.match(html, /id="macos-sha256-copy"[^>]*data-i18n="downloadPage.checksum.copy"/);
});

test('macOS copy uses the displayed checksum for fallback and live metadata', async () => {
  const fallback = await runDownloadScript({ok:false});
  fallback.macChecksumCopy.click();
  assert.deepEqual(fallback.clipboardWrites, ['54FD7E5CA2317F41F9EB24039CFD562DFB7F21FDA11E736110152F1792DF5F58']);
  const live = await runDownloadScript(streamedResponse(JSON.stringify(manifest())));
  live.macChecksumCopy.click();
  assert.deepEqual(live.clipboardWrites, ['B'.repeat(64)]);
});

test('valid official manifest enables both trusted Windows download routes', async () => {
  const result = await runDownloadScript(streamedResponse(JSON.stringify(manifest()), { chunkSize: 17 }));
  assert.equal(result.calls.length, 1);
  assert.equal(result.calls[0].url, '/updates/latest.json');
  assert.equal(result.calls[0].options.cache, 'no-store');
  assert.equal(result.calls[0].options.credentials, 'omit');
  assert.equal(result.calls[0].options.headers.Accept, 'application/json');
  for (const button of [result.primary, result.github]) {
    assert.equal(button.disabled, false);
    assert.equal(button.classList.contains('disabled'), false);
    assert.equal(button.hasAttribute('aria-disabled'), false);
  }
  result.primary.click();
  result.github.click();
  assert.deepEqual(result.navigations, [
    'https://download.agplayer.com/releases/v1.0.4/AgPlayer-Setup-1.0.4-x64.exe',
    'https://github.com/gddjag/AgPlayer/releases/download/v1.0.4/AgPlayer-Setup-1.0.4-x64.exe'
  ]);
  assert.equal(result.status.hidden, true);
});

test('published Windows release exposes the real uppercase SHA-256 and copies it', async () => {
  const expected = 'C3E05C887D6DC0161ADBB6E2E8340792A5BD7212D738BE035D120B5E4918238A';
  const payload = manifest({ files: [{
    ...manifest().files[0],
    sha256: expected.toLowerCase()
  }, macFile(), androidFile()] });
  const result = await runDownloadScript(streamedResponse(JSON.stringify(payload)));

  assert.equal(result.checksum.hasAttribute('hidden'), false);
  assert.equal(result.checksumValue.textContent, expected);
  assert.equal(result.checksumCopy.disabled, false);
  result.checksumCopy.click();
  await new Promise(resolve => setTimeout(resolve, 0));
  assert.deepEqual(result.clipboardWrites, [expected]);
});

test('published 1.1.2 remains available when the live manifest cannot be read', async () => {
  const cases = [
    { ok: false },
    streamedResponse('x'.repeat(65537)),
    streamedResponse(JSON.stringify(manifest({ releaseNotesUrl: 'https://example.com/fake-release' }))),
    streamedResponse(JSON.stringify(manifest({ files: [{ ...manifest().files[0], r2Url: 'https://example.com/AgPlayer.exe' }] })))
  ];
  for (const response of cases) {
    const result = await runDownloadScript(response);
    assert.equal(result.primary.disabled, false);
    assert.equal(result.github.disabled, false);
    assert.equal(result.checksum.hasAttribute('hidden'), false);
    assert.equal(result.checksumValue.textContent, '6A508E91AD9218589B1113EDB10314105B4852B8B38474A40A1D8DCA44A63DE2');
    result.primary.click();
    result.github.click();
    assert.deepEqual(result.navigations, [
      'https://download.agplayer.com/releases/v1.1.2/AgPlayer-Setup-1.1.2-x64.exe?revision=20261030',
      'https://github.com/gddjag/AgPlayer/releases/download/v1.1.2/AgPlayer-Setup-1.1.2-x64.exe?revision=20261030'
    ]);
  }
});

test('download page preserves the FAQ and exposes Android plus HarmonyOS cards', async () => {
  const html = await readFile(downloadPagePath, 'utf8');
  assert.match(html, /class="download-faq"/);
  assert.match(html, /id="android"/);
  assert.match(html, /AgPlayer-1\.1\.2-arm64-release\.apk/);
  assert.match(html, /id="harmonyos"/);
  assert.match(html, /HarmonyOS 鸿蒙应用市场下载原生鸿蒙 PC 版和手机版/);
  assert.match(html, /downloads\.js\?v=20261030-website-updates-2/);
});

test('chunked manifest cancels the stream as soon as it exceeds 64 KiB', async () => {
  const bytes = new TextEncoder().encode('x'.repeat(65537));
  let read = 0;
  let cancelled = false;
  const response = {
    ok: true,
    headers: { get: () => null },
    body: { getReader: () => ({
      read: async () => read < bytes.length
        ? { done: false, value: bytes.slice(read, read += 16384) }
        : { done: true },
      cancel: async () => { cancelled = true; },
      releaseLock: () => {}
    }) }
  };
  const result = await runDownloadScript(response);
  assert.equal(cancelled, true);
  assert.equal(result.primary.disabled, false);
});

test('manifest request aborts after the ten second deadline', async () => {
  let signal;
  const result = await runDownloadScript((_, options) => {
    signal = options.signal;
    return new Promise((_, reject) => signal.addEventListener('abort', () => reject(signal.reason)));
  }, {
    setTimeout: callback => { queueMicrotask(callback); return 1; },
    clearTimeout: () => {}
  });
  assert.equal(signal.aborted, true);
  assert.equal(result.primary.disabled, false);
  assert.equal(result.github.disabled, false);
});

function dualWindowsManifest(version = '1.1.2') {
  const file = (name, hash) => ({ name, size: 123456, sha256: hash.repeat(64),
    githubUrl: `https://github.com/gddjag/AgPlayer/releases/download/v${version}/${name}`,
    r2Url: `https://download.agplayer.com/releases/v${version}/${name}` });
  return manifest({ version, tag: `v${version}`,
    releaseNotesUrl: `https://github.com/gddjag/AgPlayer/releases/tag/v${version}`,
    files: [file(`AgPlayer-Setup-${version}-x64.exe`, 'a'),
      file(`AgPlayer-Setup-${version}-arm64.exe`, 'd'), macFile(version), androidFile(version)] });
}

test('Windows architecture selection changes both official routes and copied checksum', async () => {
  const result = await runDownloadScript(streamedResponse(JSON.stringify(dualWindowsManifest())));
  assert.equal(result.armOption.disabled, false);
  result.architecture.value = 'arm64';
  result.architecture.change();
  result.primary.click();
  result.github.click();
  result.checksumCopy.click();
  assert.deepEqual(result.navigations, [
    'https://download.agplayer.com/releases/v1.1.2/AgPlayer-Setup-1.1.2-arm64.exe',
    'https://github.com/gddjag/AgPlayer/releases/download/v1.1.2/AgPlayer-Setup-1.1.2-arm64.exe']);
  assert.equal(result.checksumValue.textContent, 'D'.repeat(64));
  assert.deepEqual(result.clipboardWrites, ['D'.repeat(64)]);
  result.architecture.value = 'x64';
  result.architecture.change();
  result.primary.click();
  assert.equal(result.navigations.at(-1), 'https://download.agplayer.com/releases/v1.1.2/AgPlayer-Setup-1.1.2-x64.exe');
});

test('legacy three-file manifest keeps x64 usable without inventing an ARM64 download', async () => {
  const live = dualWindowsManifest();
  live.files.splice(1, 1);
  const result = await runDownloadScript(streamedResponse(JSON.stringify(live)));
  assert.equal(result.armOption.disabled, true);
  assert.equal(result.checksumValue.textContent, 'A'.repeat(64));
  assert.equal(result.primary.disabled, false);
});

test('future Windows release requires both architectures before switching any platform', async () => {
  const live = dualWindowsManifest('1.1.3');
  live.files.splice(1, 1);
  const result = await runDownloadScript(streamedResponse(JSON.stringify(live)));
  assert.equal(result.checksumValue.textContent, '6A508E91AD9218589B1113EDB10314105B4852B8B38474A40A1D8DCA44A63DE2');
  assert.equal(result.macPrimary.href, 'https://download.agplayer.com/releases/v1.1.2/AgPlayer-1.1.2-macOS-universal.dmg?revision=20261030');
});

test('forged ARM64 URLs, sizes, hashes and duplicate filenames reject the complete manifest', async () => {
  for (const change of [{r2Url: 'https://example.com/arm64.exe'},
    {githubUrl: 'https://github.com/other/AgPlayer/releases/download/v1.1.2/AgPlayer-Setup-1.1.2-arm64.exe'},
    {size: 0}, {sha256: 'invalid'}, {architecture: 'x64'}, {duplicate: true}]) {
    const live = dualWindowsManifest();
    if (change.duplicate) live.files.push({...live.files[1]});
    else Object.assign(live.files[1], change);
    const result = await runDownloadScript(streamedResponse(JSON.stringify(live)));
    // Reject the whole untrusted manifest and retain both verified published fallbacks.
    assert.equal(result.armOption.disabled, false);
    assert.equal(result.checksumValue.textContent, '6A508E91AD9218589B1113EDB10314105B4852B8B38474A40A1D8DCA44A63DE2');
    result.architecture.value = 'arm64';
    result.architecture.change();
    result.primary.click();
    assert.equal(result.navigations.at(-1), 'https://download.agplayer.com/releases/v1.1.2/AgPlayer-Setup-1.1.2-arm64.exe?revision=20261030');
    assert.equal(result.checksumValue.textContent, '6FB68552D90CA806AA2E40F52B34A58AAF0D94C868313F2C7C8FD28444D2BA44');
  }
});

test('same-version Mac revision uses verified canonical paths with cache-busting queries', async () => {
  const value = manifest();
  const mac = value.files[1];
  mac.revision = '20261027';
  mac.r2Url += '?revision=20261027';
  mac.githubUrl += '?revision=20261027';
  const result = await runDownloadScript(streamedResponse(JSON.stringify(value)));
  assert.equal(result.macPrimary.href, mac.r2Url);
  assert.equal(result.macGithub.href, mac.githubUrl);
  assert.equal(result.macChecksum.textContent, 'B'.repeat(64));
  for (const revision of ['../20261027', '20261027&host=evil', '', 20261027]) {
    const bad = structuredClone(value);
    bad.files[1].revision = revision;
    const rejected = await runDownloadScript(streamedResponse(JSON.stringify(bad)));
    assert.notEqual(rejected.macChecksum.textContent, 'B'.repeat(64));
  }
});

test('Windows desktop revisions preserve architecture selection and exact official URLs', async () => {
  const value = manifest();
  const x64 = value.files[0];
  const arm64 = { ...x64, name: x64.name.replace('-x64.exe', '-arm64.exe'), sha256: 'd'.repeat(64) };
  arm64.githubUrl = x64.githubUrl.replace('-x64.exe', '-arm64.exe');
  arm64.r2Url = x64.r2Url.replace('-x64.exe', '-arm64.exe');
  value.files.push(arm64);
  for (const file of [x64, arm64]) {
    file.revision = '20261028';
    file.r2Url += '?revision=20261028';
    file.githubUrl += '?revision=20261028';
  }
  const result = await runDownloadScript(streamedResponse(JSON.stringify(value)));
  assert.equal(result.primary.dataset.downloadUrl, x64.r2Url);
  result.architecture.value = 'arm64';
  result.architecture.change();
  assert.equal(result.primary.dataset.downloadUrl, arm64.r2Url);
  assert.equal(result.github.dataset.downloadUrl, arm64.githubUrl);
  assert.equal(result.checksumValue.textContent, 'D'.repeat(64));
  for (const change of [{ revision: '../20261028' }, { revision: 20261028 }, { r2Url: x64.r2Url + '&host=evil' }]) {
    const bad = structuredClone(value);
    Object.assign(bad.files[0], change);
    const rejected = await runDownloadScript(streamedResponse(JSON.stringify(bad)));
    assert.notEqual(rejected.primary.dataset.downloadUrl, x64.r2Url);
  }
  const androidRevision = structuredClone(value);
  const android = androidRevision.files[2];
  android.revision = '20261028';
  android.r2Url += '?revision=20261028';
  android.githubUrl += '?revision=20261028';
  const rejected = await runDownloadScript(streamedResponse(JSON.stringify(androidRevision)));
  assert.notEqual(rejected.androidPrimary.href, android.r2Url);
});

function desktopVariantsManifest() {
  const version = '1.1.2';
  const specs = [
    ['windows-models', `AgPlayer-Setup-${version}-x64-arm64（内置伴奏分离模型版本）.exe`, ['x64', 'arm64'], true],
    ['macos-arm64', `AgPlayer-${version}-macOS-arm64.dmg`, ['arm64'], false],
    ['macos-x86_64', `AgPlayer-${version}-macOS-x86_64.dmg`, ['x86_64'], false],
    ['macos-models', `AgPlayer-${version}-macOS-universal（内置伴奏分离模型版本）.dmg`, ['arm64', 'x86_64'], true]
  ];
  return { schemaVersion: 1, kind: 'desktop-download-variants', version, tag: `v${version}`,
    files: specs.map(([id, name, architectures, bundledModels], i) => ({id, name, architectures, bundledModels,
      size: 1000 + i, sha256: String(i + 1).repeat(64), sourceCommit: 'a'.repeat(40),
      r2Url: `https://download.agplayer.com/releases/v${version}/${encodeURIComponent(name)}`,
      githubUrl: `https://github.com/gddjag/AgPlayer/releases/download/v${version}/${encodeURIComponent(name.replace("（内置伴奏分离模型版本）", "-bundled-models"))}`})) };
}

function variantsResponse(variants) {
  return url => streamedResponse(JSON.stringify(url === '/updates/desktop-variants.json' ? variants : dualWindowsManifest()));
}

test('desktop variants select Windows combined models and real Mac chip packages with readable filenames', async () => {
  const variants = desktopVariantsManifest();
  const result = await runDownloadScript(variantsResponse(variants), {}, true);
  assert.equal(result.bundledOption.disabled, false);
  assert.equal(result.macArchitecture.value, 'arm64');
  assert.equal(result.macPrimary.href, variants.files[1].r2Url);
  assert.equal(result.macFilename.textContent, variants.files[1].name);
  result.architecture.value = 'bundled'; result.architecture.change();
  assert.equal(result.primary.dataset.downloadUrl, variants.files[0].r2Url);
  assert.equal(result.github.dataset.downloadUrl, variants.files[0].githubUrl);
  assert.equal(result.windowsFilename.textContent, variants.files[0].name);
  assert.equal(result.checksumValue.textContent, '1'.repeat(64));
  result.macArchitecture.value = 'x86_64'; result.macArchitecture.change();
  assert.equal(result.macPrimary.href, variants.files[2].r2Url);
  result.macArchitecture.value = 'bundled'; result.macArchitecture.change();
  assert.equal(result.macPrimary.href, variants.files[3].r2Url);
  assert.equal(result.macFilename.textContent, variants.files[3].name);
  result.macChecksumCopy.click();
  assert.deepEqual(result.clipboardWrites, ['4'.repeat(64)]);
  assert.equal(result.calls.filter(call => call.url === '/updates/latest.json').length, 1);
});

test('forged, incomplete or wrong-architecture variants never enable extra installer choices', async () => {
  const corruptions = [
    v => { v.files[0].r2Url = 'https://example.com/fake.exe'; },
    v => { v.files[0].githubUrl = `https://github.com/gddjag/AgPlayer/releases/download/v1.1.2/${encodeURIComponent(v.files[0].name)}`; },
    v => { v.files[0].architectures = ['x64']; },
    v => { v.files[3].bundledModels = false; },
    v => { v.files[1].name = 'other.dmg'; },
    v => { v.files[1].size = 0; },
    v => { v.files[1].sha256 = 'bad'; },
    v => { v.files[1].sourceCommit = 'main'; },
    v => { v.files.pop(); },
    v => { v.files.push(v.files[1]); }
  ];
  for (const corrupt of corruptions) {
    const variants = desktopVariantsManifest(); corrupt(variants);
    const result = await runDownloadScript(variantsResponse(variants), {}, true);
    assert.equal(result.bundledOption.disabled, true);
    assert.equal(result.macOptions.some(option => !option.disabled), false);
    assert.equal(result.primary.disabled, false);
  }
});

test('late base manifest cannot replace a selected Mac model variant', async () => {
  let finishBase;
  const baseResponse = new Promise(resolve => { finishBase = resolve; });
  const variants = desktopVariantsManifest();
  const result = await runDownloadScript(url => url === '/updates/desktop-variants.json'
    ? streamedResponse(JSON.stringify(variants)) : baseResponse, {}, true);
  result.macArchitecture.value = 'bundled'; result.macArchitecture.change();
  finishBase(streamedResponse(JSON.stringify(dualWindowsManifest())));
  await new Promise(resolve => setTimeout(resolve, 0));
  assert.equal(result.macPrimary.href, variants.files[3].r2Url);
  assert.equal(result.macChecksum.textContent, '4'.repeat(64));
});


test('unavailable or forged live variants retain the exact published model download fallbacks', async () => {
  const published = JSON.parse(await readFile(join(import.meta.dirname, '..', 'updates', 'desktop-variants.json'), 'utf8'));
  const result = await runDownloadScript(variantsResponse({schemaVersion:1, files:[]}), {}, true, true);
  assert.equal(result.bundledOption.disabled, false);
  assert.equal(result.macPrimary.href, published.files.find(f => f.id === 'macos-arm64').r2Url);
  result.architecture.value = 'bundled'; result.architecture.change();
  assert.equal(result.primary.dataset.downloadUrl, published.files.find(f => f.id === 'windows-models').r2Url);
  result.macArchitecture.value = 'bundled'; result.macArchitecture.change();
  assert.equal(result.macChecksum.textContent, published.files.find(f => f.id === 'macos-models').sha256.toUpperCase());
});


test('live variant refresh preserves a selected Intel or bundled Mac download', async () => {
  const published = JSON.parse(await readFile(join(import.meta.dirname, '..', 'updates', 'desktop-variants.json'), 'utf8'));
  for (const value of ['x86_64', 'bundled']) {
    let finishVariants;
    const pending = new Promise(resolve => { finishVariants = resolve; });
    const result = await runDownloadScript(url => url === '/updates/desktop-variants.json' ? pending : streamedResponse(JSON.stringify(dualWindowsManifest())), {}, true, true);
    result.macArchitecture.value = value; result.macArchitecture.change();
    finishVariants(streamedResponse(JSON.stringify(published)));
    await new Promise(resolve => setTimeout(resolve, 0));
    assert.equal(result.macArchitecture.value, value);
    assert.equal(result.macFilename.textContent, published.files.find(f => f.id === (value === 'bundled' ? 'macos-models' : 'macos-x86_64')).name);
  }
});

test('same-version Mac variants use only exact dated revision URLs and keep user selection', async () => {
  const variants = desktopVariantsManifest();
  for (const file of variants.files.filter(file => file.id.startsWith('macos-'))) {
    file.revision = '20261029';
    file.r2Url += '?revision=20261029';
    file.githubUrl += '?revision=20261029';
  }
  const result = await runDownloadScript(variantsResponse(variants), {}, true);
  assert.equal(result.macArchitecture.value, 'arm64');
  assert.equal(result.macPrimary.href, variants.files[1].r2Url);
  result.macArchitecture.value = 'bundled'; result.macArchitecture.change();
  assert.equal(result.macPrimary.href, variants.files[3].r2Url);
  assert.equal(result.macGithub.href, variants.files[3].githubUrl);
  for (const corrupt of [
    v => {v.files[1].revision = 'latest';},
    v => {v.files[1].revision = 20261029;},
    v => {v.files[1].r2Url += '&mirror=evil';},
    v => {v.files[1].githubUrl = v.files[1].githubUrl.replace('20261029','20261028');},
    v => {v.files[0].revision = '20261029';v.files[0].r2Url += '?revision=20261030';v.files[0].githubUrl += '?revision=20261029';}
  ]) {
    const invalid = structuredClone(variants); corrupt(invalid);
    const rejected = await runDownloadScript(variantsResponse(invalid), {}, true);
    assert.equal(rejected.macOptions.some(option => !option.disabled), false);
  }
});

test('all desktop choices accept exact same-version revision URLs', async () => {
  const variants = desktopVariantsManifest();
  for (const file of variants.files) {
    file.revision = '20261030';
    file.r2Url += '?revision=20261030';
    file.githubUrl += '?revision=20261030';
  }
  const result = await runDownloadScript(variantsResponse(variants), {}, true);
  result.architecture.value = 'bundled'; result.architecture.change();
  assert.equal(result.primary.dataset.downloadUrl, variants.files[0].r2Url);
  assert.equal(result.macPrimary.href, variants.files[1].r2Url);
});
