import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { validateDescriptor, appendArmInstaller } from './append-windows-arm64-1.1.2.mjs';
const name = 'AgPlayer-Setup-1.1.2-arm64.exe';
const bytes = Buffer.from('local ARM installer test fixture');
const hash = createHash('sha256').update(bytes).digest('hex');
const descriptor = () => ({ version: '1.1.2', name,
  stagingUrl: `https://release-test.trycloudflare.com/${name}`, size: bytes.length, sha256: hash });
const oldFiles = () => [
  { id: 647102603, name: 'AgPlayer-1.1.2-arm64-release.apk', size: 237844183,
    digest: 'sha256:d1c1739fff87f9d49f9f04a0d5d53ee0c5bed2ab70f1bb596659bb2ee8374777', state: 'uploaded' },
  { id: 647102605, name: 'AgPlayer-1.1.2-macOS-universal.dmg', size: 114609080,
    digest: 'sha256:912b2c9659cf0e71ebb586155bee61ad15ac66cbb4688510109b89227a0ff993', state: 'uploaded' },
  { id: 647102604, name: 'AgPlayer-Setup-1.1.2-x64.exe', size: 32027584,
    digest: 'sha256:fb5821b0931bbb56cad83284757ec7a05a14128f949d16b1ac6b582f4365b778', state: 'uploaded' }
];

async function withFixture(callback, options = {}) {
  const directory = await mkdtemp(join(tmpdir(), 'agplayer-arm-append-'));
  const files = options.files ?? oldFiles();
  const uploads = [];
  const requests = [];
  const fetcher = async (url, request = {}) => {
    requests.push({ url, request });
    if (url.endsWith('/releases/tags/v1.1.2')) return Response.json({id: 414882152, tag_name: 'v1.1.2',
      target_commitish: '24df8985e064cda64feee3c78e0754a2492e0617', draft: false, prerelease: false,
      ...options.release});
    if (url.endsWith('/git/ref/tags/v1.1.2')) return Response.json({object: {type:'commit',
      sha: options.tagSha ?? '24df8985e064cda64feee3c78e0754a2492e0617'}});
    if (url.includes('/releases/414882152/assets?per_page=100')) return Response.json(files);
    if (url === descriptor().stagingUrl) {
      assert.equal(request.headers?.Authorization, undefined, 'GitHub token must not reach staging');
      assert.equal(request.redirect, 'error');
      return options.stagingResponse?.() ?? new Response(options.stagingBytes ?? bytes);
    }
    assert.equal(url, 'https://uploads.github.com/repos/gddjag/AgPlayer/releases/414882152/assets?name=AgPlayer-Setup-1.1.2-arm64.exe');
    assert.equal(request.method, 'POST');
    assert.equal(request.redirect, 'error');
    assert.equal(request.headers.Authorization, 'Bearer test-only');
    const chunks = [];
    for await (const chunk of request.body) chunks.push(Buffer.from(chunk));
    const body = Buffer.concat(chunks);
    uploads.push(body);
    const asset = {id: 999, name, state:'uploaded', size: body.length,
      digest: 'sha256:' + createHash('sha256').update(body).digest('hex')};
    files.push(asset);
    options.afterUpload?.(files);
    return Response.json(asset, {status: 201});
  };
  try { await callback({ directory, fetcher, files, uploads, requests }); }
  finally { await rm(directory, {recursive:true, force:true}); }
}

const append = fixture => appendArmInstaller(descriptor(), fixture.directory,
  { fetcher: fixture.fetcher, token: 'test-only' });

test('requires ready verified metadata and an exact HTTPS temporary ARM URL', () => {
  assert.deepEqual(validateDescriptor(descriptor()), descriptor());
  for (const url of [
    `http://release-test.trycloudflare.com/${name}`, `https://trycloudflare.com/${name}`,
    `https://release-test.trycloudflare.com.evil.example/${name}`,
    `https://release-test.trycloudflare.com:8443/${name}`,
    `https://user:password@release-test.trycloudflare.com/${name}`,
    `https://release-test.trycloudflare.com/${name}?token=x`,
    `https://release-test.trycloudflare.com/${name}#fragment`,
    'https://release-test.trycloudflare.com/AgPlayer-Setup-1.1.2-x64.exe'
  ]) assert.throws(() => validateDescriptor({...descriptor(), stagingUrl:url}));
  for (const change of [{size:0}, {size:1.5}, {sha256:'pending'}, {version:'1.1.3'},
    {name:'AgPlayer-Setup-1.1.2-x64.exe'}, {ready:false}, {sha256:[hash]}]) {
    assert.throws(() => validateDescriptor({...descriptor(), ...change}));
  }
  assert.throws(() => validateDescriptor(null));
});

test('appends only the verified ARM bytes and keeps the original three assets unchanged', async () => {
  await withFixture(async f => {
    const before = structuredClone(f.files);
    assert.equal(await append(f), 'uploaded');
    assert.deepEqual(f.uploads, [bytes]);
    assert.deepEqual(f.files.slice(0, 3), before);
    assert.equal(f.files.length, 4);
    assert.ok(f.requests.filter(r => r.url.includes('api.github.com')).every(r => !r.request.method || r.request.method === 'GET'));
  });
});

test('an existing ARM asset with the exact digest and size is reused without staging or upload', async () => {
  await withFixture(async f => {
    assert.equal(await append(f), 'reused');
    assert.equal(f.uploads.length, 0);
    assert.equal(f.requests.some(r => r.url.includes('trycloudflare.com')), false);
  }, {files:[...oldFiles(), {id:999, name, size:bytes.length, digest:'sha256:'+hash, state:'uploaded'}]});
});

test('refuses drifted release, tag or historical asset identities before any binary upload', async () => {
  const variants = [{release:{id:1}}, {release:{draft:true}}, {release:{prerelease:true}},
    {tagSha:'0'.repeat(40)}, ...[{id:1}, {size:1}, {digest:'sha256:'+'0'.repeat(64)}, {state:'starter'}]
      .map(change => {const files=oldFiles(); Object.assign(files[2],change);return {files};})];
  for (const options of variants) await withFixture(async f => {
    await assert.rejects(append(f));
    assert.equal(f.uploads.length, 0);
  }, options);
});

test('conflicting or duplicate ARM assets are never overwritten', async () => {
  const asset = {id:999, name, size:bytes.length, digest:'sha256:'+hash, state:'uploaded'};
  for (const extra of [[{...asset, size:1}], [{...asset, digest:'sha256:'+'0'.repeat(64)}],
    [{...asset, state:'starter'}], [asset, {...asset,id:1000}]]) {
    await withFixture(async f => {
      await assert.rejects(append(f));
      assert.equal(f.uploads.length, 0);
    }, {files:[...oldFiles(), ...extra]});
  }
});

test('wrong length, wrong hash and redirects fail before uploading', async () => {
  for (const options of [{stagingBytes:bytes.subarray(1)}, {stagingBytes:Buffer.alloc(bytes.length)},
    {stagingBytes:Buffer.concat([bytes,Buffer.from('x')])},
    {stagingResponse:() => new Response(null,{status:302, headers:{location:'https://example.com/file'}})}]) {
    await withFixture(async f => {
      await assert.rejects(append(f));
      assert.equal(f.uploads.length, 0);
    }, options);
  }
});

test('rechecks historical assets after upload so a concurrent replacement stops the sync', async () => {
  await withFixture(async f => {
    await assert.rejects(append(f));
    assert.equal(f.uploads.length, 1);
  }, {afterUpload: files => { files[0].id = 1; }});
});
