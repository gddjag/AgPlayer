import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, readFile, writeFile, rm } from 'node:fs/promises';
import { writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createHash } from 'node:crypto';
import { validateTag, selectAssets, githubRequest, upload } from './sync-release-r2.mjs';
import * as releaseSync from './sync-release-r2.mjs';

test('accept only stable, canonical version tags', () => {
  assert.equal(validateTag('v1.0.0'), 'v1.0.0');
  for (const tag of ['1.0.0', 'v1.0.0-beta', 'v01.0.0', '../v1.0.0', undefined]) assert.throws(() => validateTag(tag));
});

test('require a complete official release and safe EXE names', () => {
  const release = { tag_name: 'v1.0.0', published_at: 'now' };
  const asset = { name: 'AgPlayer-Setup-1.0.0-x64.exe', id: 1, state: 'uploaded', size: 42 };
  assert.equal(selectAssets(release, [asset, { name: 'notes.txt' }], 'v1.0.0').length, 1);
  assert.throws(() => selectAssets(release, [], 'v1.0.0'), /No EXE/);
  for (const change of [{ prerelease: true }, { draft: true }, { tag_name: 'v2.0.0' }]) assert.throws(() => selectAssets({ ...release, ...change }, [asset], 'v1.0.0'));
  for (const change of [{ name: '../bad.exe' }, { size: 0 }, { state: 'starter' }]) assert.throws(() => selectAssets(release, [{ ...asset, ...change }], 'v1.0.0'));
  assert.throws(() => selectAssets(release, [asset, asset], 'v1.0.0'));
});

test('redirects strip API authorization and reject external destinations', async () => {
  process.env.GH_TOKEN = 'test-only-token';
  const calls = [];
  await githubRequest('https://api.github.com/repos/gddjag/AgPlayer/releases/assets/1', true, async (url, options) => {
    calls.push({ url, options });
    return calls.length === 1 ? new Response(null, { status: 302, headers: { location: 'https://release-assets.githubusercontent.com/test' } }) : new Response('data');
  });
  assert.ok(calls[0].options.headers.Authorization);
  assert.equal(calls[1].options.headers.Authorization, undefined);
  let count = 0;
  await assert.rejects(githubRequest('https://api.github.com/test', true, async () => {
    count++;
    return new Response(null, { status: 302, headers: { location: 'https://example.com/steal' } });
  }), /Unexpected GitHub/);
  assert.equal(count, 1);
  delete process.env.GH_TOKEN;
});

test('build latest metadata with matching GitHub and R2 downloads', () => {
  assert.equal(typeof releaseSync.buildLatest, 'function');
  const latest = releaseSync.buildLatest({
    tag: 'v1.0.0',
    publishedAt: '2026-09-08T06:00:00Z',
    releaseNotesUrl: 'https://github.com/gddjag/AgPlayer/releases/tag/v1.0.0',
    files: [{ name: 'AgPlayer-Setup-1.0.0-x64.exe', size: 123456, sha256: 'a'.repeat(64) }]
  });
  assert.deepEqual(latest, {
    schemaVersion: 1,
    version: '1.0.0',
    tag: 'v1.0.0',
    publishedAt: '2026-09-08T06:00:00Z',
    releaseNotesUrl: 'https://github.com/gddjag/AgPlayer/releases/tag/v1.0.0',
    files: [{
      name: 'AgPlayer-Setup-1.0.0-x64.exe',
      size: 123456,
      sha256: 'a'.repeat(64),
      githubUrl: 'https://github.com/gddjag/AgPlayer/releases/download/v1.0.0/AgPlayer-Setup-1.0.0-x64.exe',
      r2Url: 'https://download.agplayer.com/releases/v1.0.0/AgPlayer-Setup-1.0.0-x64.exe'
    }]
  });
});

test('compare stable versions numerically without precision loss', () => {
  assert.equal(typeof releaseSync.compareVersions, 'function');
  assert.equal(releaseSync.compareVersions('1.10.0', '1.9.9'), 1);
  assert.equal(releaseSync.compareVersions('2.0.0', '2.0.0'), 0);
  assert.equal(releaseSync.compareVersions('1.9.9', '1.10.0'), -1);
  assert.equal(releaseSync.compareVersions('9007199254740993.0.0', '9007199254740992.0.0'), 1);
  assert.throws(() => releaseSync.compareVersions('1.0.0-beta', '1.0.0'));
});

test('upload checks all files before publishing checksum and latest metadata', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'agplayer-sync-test-'));
  process.env.AWS_ACCESS_KEY_ID = 'test-only';
  process.env.AWS_SECRET_ACCESS_KEY = 'test-only';
  try {
    const bytes = Buffer.from('test fixture, not a real EXE');
    const hash = createHash('sha256').update(bytes).digest('hex');
    const file = { name: 'AgPlayer.exe', size: bytes.length, sha256: hash };
    await writeFile(join(directory, file.name), bytes);
    await writeFile(join(directory, 'manifest.json'), JSON.stringify({
      tag: 'v1.0.0',
      publishedAt: '2026-09-08T06:00:00Z',
      releaseNotesUrl: 'https://github.com/gddjag/AgPlayer/releases/tag/v1.0.0',
      files: [file]
    }));
    await writeFile(join(directory, 'SHA256SUMS'), `${hash}  AgPlayer.exe\n`);
    const calls = [];
    const run = (command, args) => {
      assert.equal(command, 'aws');
      calls.push(args);
      if (args[0] === 's3api' && args[1] === 'head-object') return JSON.stringify({ ContentLength: bytes.length, Metadata: { sha256: hash } });
      if (args[0] === 's3api' && args[1] === 'get-object') {
        const error = new Error('not found');
        error.stderr = 'NoSuchKey';
        throw error;
      }
      return '';
    };
    await upload('v1.0.0', directory, run);
    assert.equal(calls.length, 5);
    assert.ok(calls[2].includes('s3://agplayer-releases/releases/v1.0.0/SHA256SUMS'));
    assert.ok(calls[4].includes('s3://agplayer-releases/updates/latest.json'));
    const firstRun = JSON.stringify(calls);
    calls.length = 0;
    await upload('v1.0.0', directory, run);
    assert.equal(JSON.stringify(calls), firstRun);
    const failedCalls = [];
    await assert.rejects(upload('v1.0.0', directory, (_, args) => { failedCalls.push(args); throw new Error('upload failed'); }), /upload failed/);
    assert.equal(failedCalls.length, 1);
    await writeFile(join(directory, file.name), 'corrupt');
    await assert.rejects(upload('v1.0.0', directory, () => assert.fail('Must not upload changed files')), /Local EXE changed/);
  } finally {
    delete process.env.AWS_ACCESS_KEY_ID;
    delete process.env.AWS_SECRET_ACCESS_KEY;
    await rm(directory, { recursive: true, force: true });
  }
});

test('upload publishes latest JSON last with no-cache and never downgrades it', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'agplayer-latest-test-'));
  process.env.AWS_ACCESS_KEY_ID = 'test-only';
  process.env.AWS_SECRET_ACCESS_KEY = 'test-only';
  try {
    const bytes = Buffer.from('test fixture, not a real EXE');
    const hash = createHash('sha256').update(bytes).digest('hex');
    const file = { name: 'AgPlayer-Setup-1.0.0-x64.exe', size: bytes.length, sha256: hash };
    const manifest = {
      tag: 'v1.0.0',
      publishedAt: '2026-09-08T06:00:00Z',
      releaseNotesUrl: 'https://github.com/gddjag/AgPlayer/releases/tag/v1.0.0',
      files: [file]
    };
    await writeFile(join(directory, file.name), bytes);
    await writeFile(join(directory, 'manifest.json'), JSON.stringify(manifest));
    await writeFile(join(directory, 'SHA256SUMS'), `${hash}  ${file.name}\n`);

    const calls = [];
    const run = (_, args) => {
      calls.push(args);
      if (args[0] === 's3api' && args[1] === 'head-object') return JSON.stringify({ ContentLength: bytes.length, Metadata: { sha256: hash } });
      if (args[0] === 's3api' && args[1] === 'get-object') {
        const error = new Error('not found');
        error.stderr = 'NoSuchKey';
        throw error;
      }
      return '';
    };
    await upload('v1.0.0', directory, run);
    const last = calls.at(-1);
    assert.ok(last.includes('s3://agplayer-releases/updates/latest.json'));
    assert.ok(last.includes('application/json; charset=utf-8'));
    assert.ok(last.includes('no-cache'));
    const latest = JSON.parse(await readFile(join(directory, 'latest.json'), 'utf8'));
    assert.equal(latest.schemaVersion, 1);
    assert.equal(latest.version, '1.0.0');

    calls.length = 0;
    const newerRun = (_, args) => {
      calls.push(args);
      if (args[0] === 's3api' && args[1] === 'head-object') return JSON.stringify({ ContentLength: bytes.length, Metadata: { sha256: hash } });
      if (args[0] === 's3api' && args[1] === 'get-object') {
        const destination = args[args.indexOf('updates/latest.json') + 1];
        writeFileSync(destination, JSON.stringify({ schemaVersion: 1, version: '2.0.0' }));
        return '';
      }
      return '';
    };
    await upload('v1.0.0', directory, newerRun);
    assert.equal(calls.some(args => args.includes('s3://agplayer-releases/updates/latest.json')), false);
  } finally {
    delete process.env.AWS_ACCESS_KEY_ID;
    delete process.env.AWS_SECRET_ACCESS_KEY;
    await rm(directory, { recursive: true, force: true });
  }
});
