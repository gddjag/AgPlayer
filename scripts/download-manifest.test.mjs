import test from 'node:test';
import assert from 'node:assert/strict';
import { existsSync } from 'node:fs';
import { readFile } from 'node:fs/promises';
import { join } from 'node:path';
import { runInNewContext } from 'node:vm';

const scriptPath = join(import.meta.dirname, '..', 'assets', 'js', 'downloads.js');

function fakeElement() {
  const attributes = new Map([['aria-disabled', 'true']]);
  const classes = new Set(['disabled']);
  const listeners = new Map();
  return {
    disabled: true,
    textContent: '',
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
    ['#windows-sha256-copy', checksumCopy]
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
  return { primary, github, status, checksum, checksumValue, checksumCopy, calls, navigations, clipboardWrites, documentListeners };
}

function manifest(overrides = {}) {
  const version = '1.0.0';
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
    }],
    ...overrides
  };
}

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
    'https://download.agplayer.com/releases/v1.0.0/AgPlayer-Setup-1.0.0-x64.exe',
    'https://github.com/gddjag/AgPlayer/releases/download/v1.0.0/AgPlayer-Setup-1.0.0-x64.exe'
  ]);
  assert.equal(result.status.textContent, 'downloadPage.windows.available:1.0.0');
});

test('published Windows release exposes the real uppercase SHA-256 and copies it', async () => {
  const expected = 'B379A969A3F61C31C6C0864F7D7EB9A2435BC268F47FDD5A52D5C70B0EE63F78';
  const payload = manifest({ files: [{
    ...manifest().files[0],
    sha256: expected.toLowerCase()
  }] });
  const result = await runDownloadScript(streamedResponse(JSON.stringify(payload)));

  assert.equal(result.checksum.hasAttribute('hidden'), false);
  assert.equal(result.checksumValue.textContent, expected);
  assert.equal(result.checksumCopy.disabled, false);
  result.checksumCopy.click();
  await new Promise(resolve => setTimeout(resolve, 0));
  assert.deepEqual(result.clipboardWrites, [expected]);
});

test('missing, oversized, or untrusted manifests keep downloads disabled', async () => {
  const cases = [
    { ok: false },
    streamedResponse('x'.repeat(65537)),
    streamedResponse(JSON.stringify(manifest({ releaseNotesUrl: 'https://example.com/fake-release' }))),
    streamedResponse(JSON.stringify(manifest({ files: [{ ...manifest().files[0], r2Url: 'https://example.com/AgPlayer.exe' }] })))
  ];
  for (const response of cases) {
    const result = await runDownloadScript(response);
    assert.equal(result.primary.disabled, true);
    assert.equal(result.github.disabled, true);
    assert.equal(result.checksum.hasAttribute('hidden'), true);
    assert.equal(result.checksumValue.textContent, '');
    assert.deepEqual(result.navigations, []);
  }
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
  assert.equal(result.primary.disabled, true);
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
  assert.equal(result.primary.disabled, true);
  assert.equal(result.github.disabled, true);
});
