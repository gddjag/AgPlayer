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
    click: () => listeners.get('click')?.()
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

async function runDownloadScript(response, timers = {}) {
  assert.ok(existsSync(scriptPath), 'download manifest integration script must exist');
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
  macChecksum.textContent = '85AE4BBEFCAA189544C531817E92787141DEAA8FC4C68F615D584F718E8EF779';
  androidChecksum.textContent = '4813D65F4F5A3CA197664BFE45A512B8C00D95AF879F44C2669A179A8278ECEC';
  checksum.setAttribute('hidden', '');
  const documentListeners = new Map();
  const calls = [];
  const navigations = [];
  const clipboardWrites = [];
  const elements = new Map([
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
    AG: { t: (key, values) => `${key}:${values.version}` },
    AbortController,
    TextDecoder,
    Uint8Array,
    setTimeout: timers.setTimeout ?? setTimeout,
    clearTimeout: timers.clearTimeout ?? clearTimeout,
    console
  };
  runInNewContext(await readFile(scriptPath, 'utf8'), context, { filename: scriptPath });
  await new Promise(resolve => setTimeout(resolve, 0));
  return { primary, github, status, checksum, checksumValue, checksumCopy, macPrimary, macGithub, macChecksum, macChecksumCopy, androidPrimary, androidGithub, androidChecksum, androidChecksumCopy, calls, navigations, clipboardWrites, documentListeners };
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
  assert.equal(rejected.macPrimary.href, 'https://download.agplayer.com/releases/v1.0.9/AgPlayer-1.0.9-macOS-universal.dmg');
  assert.equal(rejected.checksumValue.textContent, '23BFD1616449506E1727D77ABE13E1A2258AB11886EEF24F67885F61A78C1C3D');
});

test('macOS HTML fallback links the accepted package and official opening guide', async () => {
  const html = await readFile(downloadPagePath, 'utf8');
  assert.match(html, /href="https:\/\/support\.apple\.com\/zh-cn\/102445"/);
  assert.match(html, /尚未经过 Apple 公证/);
  assert.match(html, /href="https:\/\/download\.agplayer\.com\/releases\/v1\.0\.9\/AgPlayer-1\.0\.9-macOS-universal\.dmg"/);
  assert.match(html, /id="macos-sha256-copy"[^>]*data-i18n="downloadPage.checksum.copy"/);
});

test('macOS copy uses the displayed checksum for fallback and live metadata', async () => {
  const fallback = await runDownloadScript({ok:false});
  fallback.macChecksumCopy.click();
  assert.deepEqual(fallback.clipboardWrites, ['85AE4BBEFCAA189544C531817E92787141DEAA8FC4C68F615D584F718E8EF779']);
  const live = await runDownloadScript(streamedResponse(JSON.stringify(manifest())));
  live.macChecksumCopy.click();
  assert.deepEqual(live.clipboardWrites, ['B'.repeat(64)]);
});

test('valid official manifest enables both trusted Windows download routes', async () => {
  const result = await runDownloadScript(streamedResponse(JSON.stringify(manifest()), { chunkSize: 17 }));
  assert.equal(result.calls.length, 1);
  assert.equal(result.calls[0].url, 'https://download.agplayer.com/updates/latest.json');
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

test('published 1.0.9 remains available when the live manifest cannot be read', async () => {
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
    assert.equal(result.checksumValue.textContent, '23BFD1616449506E1727D77ABE13E1A2258AB11886EEF24F67885F61A78C1C3D');
    result.primary.click();
    result.github.click();
    assert.deepEqual(result.navigations, [
      'https://download.agplayer.com/releases/v1.0.9/AgPlayer-Setup-1.0.9-x64.exe',
      'https://github.com/gddjag/AgPlayer/releases/download/v1.0.9/AgPlayer-Setup-1.0.9-x64.exe'
    ]);
  }
});

test('download page removes the FAQ divider and exposes Android plus HarmonyOS cards', async () => {
  const html = await readFile(downloadPagePath, 'utf8');
  assert.doesNotMatch(html, /class="download-faq"/);
  assert.match(html, /id="android"/);
  assert.match(html, /AgPlayer-1\.0\.9-arm64-release\.apk/);
  assert.match(html, /id="harmonyos"/);
  assert.match(html, /HarmonyOS 鸿蒙应用市场下载原生鸿蒙 PC 版和手机版/);
  assert.match(html, /downloads\.js\?v=20260927-release-109-triple/);
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
