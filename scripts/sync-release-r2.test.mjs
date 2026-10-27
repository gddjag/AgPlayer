import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, readFile, writeFile, rm } from 'node:fs/promises';
import { readFileSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createHash } from 'node:crypto';
import { validateTag, selectAssets, githubRequest, upload } from './sync-release-r2.mjs';
import * as releaseSync from './sync-release-r2.mjs';

test('shared releases require matching Windows and Universal macOS packages', () => {
  const tag = 'v1.0.3';
  const release = { tag_name: tag, published_at: 'now' };
  const windows = { name: 'AgPlayer-Setup-1.0.3-x64.exe', id: 1, state: 'uploaded', size: 42 };
  const macos = { name: 'AgPlayer-1.0.3-macOS-universal.dmg', id: 2, state: 'uploaded', size: 84 };
  assert.throws(() => selectAssets(release, [windows], tag), /Synchronized/);
  assert.throws(() => selectAssets(release, [macos], tag), /Synchronized/);
  assert.throws(() => selectAssets(release, [windows, {...macos, name: 'AgPlayer-1.0.2-macOS-universal.dmg'}], tag), /Synchronized/);
  assert.equal(selectAssets(release, [windows, macos, {name:'notes.txt'}], tag).length, 2);
  const latest = releaseSync.buildLatest({ tag, publishedAt:'2026-09-13T00:00:00Z', releaseNotesUrl:'https://github.com/gddjag/AgPlayer/releases/tag/v1.0.3', files:[windows,macos].map(x=>({...x,sha256:'a'.repeat(64)})) });
  assert.equal(latest.files[1].r2Url, 'https://download.agplayer.com/releases/v1.0.3/AgPlayer-1.0.3-macOS-universal.dmg');
});

test('1.0.9 and later releases also require the matching Android arm64 package', () => {
  const tag = 'v1.0.9';
  const release = { tag_name: tag, published_at: 'now' };
  const assets = [
    { name: 'AgPlayer-Setup-1.0.9-x64.exe', id: 1, state: 'uploaded', size: 42 },
    { name: 'AgPlayer-1.0.9-macOS-universal.dmg', id: 2, state: 'uploaded', size: 84 },
    { name: 'AgPlayer-1.0.9-arm64-release.apk', id: 3, state: 'uploaded', size: 126 }
  ];
  assert.throws(() => selectAssets(release, assets.slice(0, 2), tag), /Android/);
  assert.equal(selectAssets(release, assets, tag).length, 3);
});

test('accept only stable, canonical version tags', () => {
  assert.equal(validateTag('v1.0.0'), 'v1.0.0');
  for (const tag of ['1.0.0', 'v1.0.0-beta', 'v01.0.0', '../v1.0.0', undefined]) assert.throws(() => validateTag(tag));
});

test('require a complete official release and safe EXE names', () => {
  const release = { tag_name: 'v1.0.0', published_at: 'now' };
  const asset = { name: 'AgPlayer-Setup-1.0.0-x64.exe', id: 1, state: 'uploaded', size: 42 };
  assert.equal(selectAssets(release, [asset, { name: 'notes.txt' }], 'v1.0.0').length, 1);
  assert.throws(() => selectAssets(release, [], 'v1.0.0'), /No installer/);
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
    assert.equal(calls.length, 4);
    assert.ok(calls[1].includes('s3://agplayer-releases/releases/v1.0.0/SHA256SUMS'));
    assert.ok(calls[3].includes('s3://agplayer-releases/updates/latest.json'));
    const firstRun = JSON.stringify(calls);
    calls.length = 0;
    await upload('v1.0.0', directory, run);
    assert.equal(JSON.stringify(calls), firstRun);
    const failedCalls = [];
    await assert.rejects(upload('v1.0.0', directory, (_, args) => { failedCalls.push(args); throw new Error('upload failed'); }), /upload failed/);
    assert.equal(failedCalls.length, 1);
    await writeFile(join(directory, file.name), 'corrupt');
    await assert.rejects(upload('v1.0.0', directory, () => assert.fail('Must not upload changed files')), /Local installer changed/);
  } finally {
    delete process.env.AWS_ACCESS_KEY_ID;
    delete process.env.AWS_SECRET_ACCESS_KEY;
    await rm(directory, { recursive: true, force: true });
  }
});

test('shared releases publish the same latest bytes to legacy macOS and shared endpoints', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'agplayer-legacy-alias-test-'));
  process.env.AWS_ACCESS_KEY_ID = 'test-only';
  process.env.AWS_SECRET_ACCESS_KEY = 'test-only';
  try {
    const bytes = Buffer.from('abc');
    const hash = createHash('sha256').update(bytes).digest('hex');
    const files = ['AgPlayer-Setup-1.0.4-x64.exe', 'AgPlayer-1.0.4-macOS-universal.dmg']
      .map(name => ({ name, size: bytes.length, sha256: hash }));
    for (const file of files) await writeFile(join(directory, file.name), bytes);
    await writeFile(join(directory, 'manifest.json'), JSON.stringify({
      tag: 'v1.0.4', publishedAt: '2026-09-14T00:00:00Z',
      releaseNotesUrl: 'https://github.com/gddjag/AgPlayer/releases/tag/v1.0.4', files
    }));
    await writeFile(join(directory, 'SHA256SUMS'), files.map(file => `${hash}  ${file.name}\n`).join(''));
    const publications = new Map();
    await upload('v1.0.4', directory, (_, args) => {
      if (args[1] === 'head-object') return JSON.stringify({ ContentLength: 3, Metadata: { sha256: hash } });
      if (args[1] === 'get-object') throw new Error('NoSuchKey');
      if (args[1] === 'cp' && args[3].includes('/updates/')) {
        assert.ok(args.includes('no-cache'));
        publications.set(args[3], args[2]);
      }
      return '';
    });
    assert.ok(publications.has('s3://agplayer-releases/updates/macos/latest.json'));
    assert.equal(publications.get('s3://agplayer-releases/updates/macos/latest.json'),
                 publications.get('s3://agplayer-releases/updates/latest.json'));
    assert.equal([...publications.keys()].at(-1), 's3://agplayer-releases/updates/latest.json');
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

test('prune removes only superseded release packages after verifying all current files', async () => {
  assert.equal(typeof releaseSync.pruneOldReleases, 'function');
  process.env.AWS_ACCESS_KEY_ID = 'test-only';
  process.env.AWS_SECRET_ACCESS_KEY = 'test-only';
  try {
  const tag = 'v1.0.9';
  const files = [
    { name: 'AgPlayer-Setup-1.0.9-x64.exe', size: 42, sha256: 'a'.repeat(64) },
    { name: 'AgPlayer-1.0.9-macOS-universal.dmg', size: 84, sha256: 'b'.repeat(64) },
    { name: 'AgPlayer-1.0.9-arm64-release.apk', size: 126, sha256: 'c'.repeat(64) }
  ];
  const latest = releaseSync.buildLatest({ tag, publishedAt: '2026-09-21T00:00:00Z',
    releaseNotesUrl: 'https://github.com/gddjag/AgPlayer/releases/tag/v1.0.9', files });
  const keys = [
    'releases/v1.0.5/AgPlayer-Setup-1.0.5-x64.exe',
    'releases/v1.0.6/AgPlayer-1.0.6-macOS-universal.dmg',
    'releases/v1.0.8/AgPlayer-1.0.8-arm64-release.apk',
    'releases/v1.0.6/SHA256SUMS',
    'releases/v1.0.6/release-notes.txt',
    'releases/v1.0.9/AgPlayer-Setup-1.0.9-x64.exe',
    'releases/v1.0.10/AgPlayer-Setup-1.0.10-x64.exe'
  ];
  const deleted = [];
  const verifiedRemoved = [];
  const run = (_, args) => {
    if (args[0] === 's3' && args[1] === 'cp') return JSON.stringify(latest);
    if (args[1] === 'head-object') {
      const key = args[args.indexOf('--key') + 1];
      if (deleted.includes(key)) {
        verifiedRemoved.push(key);
        const error = new Error('Not Found');
        error.stderr = '404 Not Found';
        throw error;
      }
      const file = files.find(item => args.includes(`releases/${tag}/${item.name}`));
      assert.ok(file);
      return JSON.stringify({ ContentLength: file.size, Metadata: { sha256: file.sha256 } });
    }
    if (args[1] === 'list-objects-v2') return JSON.stringify({ IsTruncated: false, Contents: keys.map(Key => ({ Key })) });
    if (args[1] === 'delete-object') { deleted.push(args[args.indexOf('--key') + 1]); return ''; }
    assert.fail(`Unexpected AWS command: ${args.join(' ')}`);
  };
  await releaseSync.pruneOldReleases(tag, run);
  assert.deepEqual(deleted, [keys[0], keys[1], keys[2], keys[3]]);
  assert.deepEqual(verifiedRemoved, [keys[0], keys[1], keys[2], keys[3]]);
  } finally {
    delete process.env.AWS_ACCESS_KEY_ID;
    delete process.env.AWS_SECRET_ACCESS_KEY;
  }
});

test('prune refuses stale metadata or an unverified current package before deleting', async () => {
  assert.equal(typeof releaseSync.pruneOldReleases, 'function');
  process.env.AWS_ACCESS_KEY_ID = 'test-only';
  process.env.AWS_SECRET_ACCESS_KEY = 'test-only';
  try {
  const tag = 'v1.0.7';
  const files = [
    { name: 'AgPlayer-Setup-1.0.7-x64.exe', size: 42, sha256: 'a'.repeat(64) },
    { name: 'AgPlayer-1.0.7-macOS-universal.dmg', size: 84, sha256: 'b'.repeat(64) }
  ];
  const latest = releaseSync.buildLatest({ tag, publishedAt: '2026-09-21T00:00:00Z',
    releaseNotesUrl: 'https://github.com/gddjag/AgPlayer/releases/tag/v1.0.7', files });
  let deletes = 0;
  const run = (_, args) => {
    if (args[1] === 'delete-object') deletes++;
    if (args[0] === 's3' && args[1] === 'cp') return JSON.stringify(latest);
    if (args[1] === 'head-object') return JSON.stringify({ ContentLength: 0, Metadata: {} });
    if (args[1] === 'list-objects-v2') return JSON.stringify({ IsTruncated: false, Contents: [{ Key: 'releases/v1.0.6/SHA256SUMS' }] });
    return '';
  };
  await assert.rejects(releaseSync.pruneOldReleases(tag, run), /verification/i);
  assert.equal(deletes, 0);
  const stale = (_, args) => args[0] === 's3' && args[1] === 'cp'
    ? JSON.stringify({ ...latest, tag: 'v1.0.6' }) : run(_, args);
  await assert.rejects(releaseSync.pruneOldReleases(tag, stale), /latest|tag/i);
  assert.equal(deletes, 0);
  } finally {
    delete process.env.AWS_ACCESS_KEY_ID;
    delete process.env.AWS_SECRET_ACCESS_KEY;
  }
});


test('1.1.2 onward requires native Windows ARM64 as well as x64, macOS and Android', () => {
  for (const version of ['1.1.2', '1.1.3', '2.0.0']) {
    const tag = `v${version}`;
    const release = { tag_name: tag, published_at: 'now' };
    const assets = [`AgPlayer-Setup-${version}-x64.exe`,
      `AgPlayer-${version}-macOS-universal.dmg`, `AgPlayer-${version}-arm64-release.apk`,
      `AgPlayer-Setup-${version}-arm64.exe`]
      .map((name, index) => ({name, id: index + 1, state: 'uploaded', size: 42}));
    assert.throws(() => selectAssets(release, assets.slice(0, 3), tag), /ARM64/);
    assert.equal(selectAssets(release, assets, tag).length, 4);
    assert.throws(() => selectAssets(release, assets.filter(a => !a.name.endsWith('-x64.exe')), tag), /Synchronized/);
  }
});


async function withAppendFixture(callback) {
  const directory = await mkdtemp(join(tmpdir(), 'agplayer-append-test-'));
  process.env.AWS_ACCESS_KEY_ID = 'test-only';
  process.env.AWS_SECRET_ACCESS_KEY = 'test-only';
  try {
    const names = ['AgPlayer-1.1.2-arm64-release.apk', 'AgPlayer-1.1.2-macOS-universal.dmg',
      'AgPlayer-Setup-1.1.2-arm64.exe', 'AgPlayer-Setup-1.1.2-x64.exe'];
    const files = [];
    for (const name of names) {
      const bytes = Buffer.from(`local fixture for ${name}`);
      await writeFile(join(directory, name), bytes);
      files.push({ name, size: bytes.length, sha256: createHash('sha256').update(bytes).digest('hex') });
    }
    const manifest = { tag: 'v1.1.2', publishedAt: '2026-10-16T00:00:00Z',
      releaseNotesUrl: 'https://github.com/gddjag/AgPlayer/releases/tag/v1.1.2', files };
    await writeFile(join(directory, 'manifest.json'), JSON.stringify(manifest));
    await writeFile(join(directory, 'SHA256SUMS'), files.map(f => `${f.sha256}  ${f.name}\n`).join(''));
    const arm = files.find(f => f.name === 'AgPlayer-Setup-1.1.2-arm64.exe');
    const objects = new Map(files.filter(f => f !== arm).map(f => [f.name,
      { ContentLength: f.size, Metadata: { sha256: f.sha256 } }]));
    const writes = [];
    const heads = [];
    const run = (_, args) => {
      if (args[1] === 'head-object') {
        const name = args[args.indexOf('--key') + 1].split('/').at(-1);
        heads.push(name);
        if (!objects.has(name)) throw new Error('An error occurred (404) when calling the HeadObject operation: Not Found');
        const object = objects.get(name);
        if (object instanceof Error) throw object;
        return typeof object === 'string' ? object : JSON.stringify(object);
      }
      if (args[1] === 'get-object') throw new Error('NoSuchKey');
      if (args[1] === 'cp') {
        const name = args[3].split('/').at(-1);
        const bytes = readFileSync(args[2]);
        writes.push({ name, bytes, args });
        if (name.endsWith('.exe') || name.endsWith('.apk') || name.endsWith('.dmg')) {
          objects.set(name, { ContentLength: bytes.length,
            Metadata: { sha256: args[args.indexOf('--metadata') + 1].slice('sha256='.length) } });
        }
        return '';
      }
      assert.fail(`Unexpected AWS operation: ${args[1]}`);
    };
    await callback({ directory, files, arm, objects, writes, heads, run });
  } finally {
    delete process.env.AWS_ACCESS_KEY_ID;
    delete process.env.AWS_SECRET_ACCESS_KEY;
    await rm(directory, { recursive: true, force: true });
  }
}

test('append reuses the three immutable packages and writes only missing ARM64 before promoting all four', async () => {
  await withAppendFixture(async ({ directory, files, arm, writes, heads, run }) => {
    await upload('v1.1.2', directory, run);
    assert.deepEqual(writes.filter(w => /\.(exe|apk|dmg)$/.test(w.name)).map(w => w.name), ['AgPlayer-Setup-1.1.2-arm64.exe']);
    assert.deepEqual(writes.find(w => w.name === arm.name).bytes, await readFile(join(directory, arm.name)));
    assert.equal(heads.filter(name => name === arm.name).length, 2);
    assert.equal(heads.length, 5);
    assert.deepEqual(writes.map(w => w.name), [arm.name, 'SHA256SUMS', 'latest.json', 'latest.json']);
    const latest = JSON.parse(writes.at(-1).bytes.toString());
    assert.equal(latest.files.length, 4);
    assert.deepEqual(latest.files.map(({name, size, sha256}) => ({name, size, sha256})), files);
    assert.ok(writes.at(-1).args.includes('s3://agplayer-releases/updates/latest.json'));
  });
});

test('existing size, hash or absent metadata conflicts abort before writing any object', async () => {
  for (const change of [{ ContentLength: 1 }, { Metadata: { sha256: '0'.repeat(64) } }, { Metadata: {} }]) {
    await withAppendFixture(async ({ directory, objects, writes, run }) => {
      const name = 'AgPlayer-Setup-1.1.2-x64.exe';
      objects.set(name, {...objects.get(name), ...change});
      await assert.rejects(upload('v1.1.2', directory, run), /existing|immutable|conflict/i);
      assert.equal(writes.length, 0);
    });
  }
});

test('head access errors, transport errors and malformed responses never become missing packages', async () => {
  for (const response of [new Error('AccessDenied (403)'), new Error('network failed'),
    new Error('404 credentials file not found'), new Error('NoSuchBucket'), 'invalid NoSuchKey json']) {
    await withAppendFixture(async ({ directory, objects, writes, run }) => {
      objects.set('AgPlayer-Setup-1.1.2-arm64.exe', response);
      await assert.rejects(upload('v1.1.2', directory, run));
      assert.equal(writes.length, 0);
    });
  }
});

test('explicit NoSuchKey permits upload, but failed post-upload verification cannot promote metadata', async () => {
  await withAppendFixture(async ({ directory, arm, objects, writes, run }) => {
    objects.set(arm.name, new Error('An error occurred (NoSuchKey) when calling the HeadObject operation: The specified key does not exist'));
    const failVerification = (command, args) => {
      const result = run(command, args);
      if (args[1] === 'cp' && args[3].endsWith('/' + arm.name)) objects.set(arm.name, {ContentLength: 0, Metadata: {}});
      return result;
    };
    await assert.rejects(upload('v1.1.2', directory, failVerification), /verification failed/i);
    assert.deepEqual(writes.map(w => w.name), [arm.name]);
  });
});

test('current metadata read errors and malformed JSON cannot be treated as an absent latest object', async () => {
  for (const response of [new Error('404 credentials file not found'), 'invalid NoSuchKey json']) {
    await withAppendFixture(async ({directory, writes, run}) => {
      const invalidCurrent = (command, args) => {
        if (args[1] === 'get-object') {
          if (response instanceof Error) throw response;
          writeFileSync(args[args.indexOf('updates/latest.json') + 1], response);
          return '';
        }
        return run(command, args);
      };
      await assert.rejects(upload('v1.1.2', directory, invalidCurrent));
      assert.equal(writes.some(w => w.name === 'latest.json'), false);
    });
  }
});

test('latest retains an explicit same-version Mac revision without changing other installer URLs', () => {
  const tag = 'v1.1.2';
  const files = [
    {name:'AgPlayer-1.1.2-macOS-universal.dmg',size:10,sha256:'a'.repeat(64),revision:'20261027'},
    {name:'AgPlayer-Setup-1.1.2-x64.exe',size:10,sha256:'b'.repeat(64)}
  ];
  const input = {tag,publishedAt:'2026-10-16T00:00:00Z',releaseNotesUrl:`https://github.com/gddjag/AgPlayer/releases/tag/${tag}`,files};
  const result = releaseSync.buildLatest(input);
  assert.equal(result.files[0].revision,'20261027');
  assert.ok(result.files[0].r2Url.endsWith('.dmg?revision=20261027'));
  assert.ok(result.files[0].githubUrl.endsWith('.dmg?revision=20261027'));
  assert.ok(result.files[1].r2Url.endsWith('.exe'));
  for (const revision of ['../20261027','20261027&x=1','',20261027]) {
    assert.throws(() => releaseSync.buildLatest({...input,files:[{...files[0],revision}]}), /revision/i);
  }
  assert.throws(() => releaseSync.buildLatest({...input,files:[{...files[1],revision:'20261027'}]}), /revision/i);
});

test('ordinary same-version sync retains a verified Mac revision from current latest', async () => {
  await withAppendFixture(async ({directory, files, writes, run}) => {
    const input = JSON.parse(await readFile(join(directory,'manifest.json'),'utf8'));
    input.files = files.map(file => file.name.endsWith('.dmg') ? {...file,revision:'20261027'} : file);
    const current = releaseSync.buildLatest(input);
    const mac = current.files.find(file => file.name.endsWith('.dmg'));
    delete mac.revision;
    mac.revision = '20261027'; // Python publisher appends this field after the URLs.
    const withCurrent = (command,args) => {
      if (args[1] === 'get-object') {
        writeFileSync(args[args.indexOf('updates/latest.json')+1],JSON.stringify(current));
        return '';
      }
      return run(command,args);
    };
    await upload('v1.1.2',directory,withCurrent);
    const promoted = JSON.parse(writes.at(-1).bytes);
    assert.deepEqual(promoted,current);
  });
});
