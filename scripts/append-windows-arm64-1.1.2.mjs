import { createReadStream, createWriteStream } from 'node:fs';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { Readable, Transform } from 'node:stream';
import { pipeline } from 'node:stream/promises';
import { pathToFileURL } from 'node:url';
import { githubRequest } from './sync-release-r2.mjs';

const api = 'https://api.github.com/repos/gddjag/AgPlayer';
const releaseId = 414882152;
const tagCommit = '24df8985e064cda64feee3c78e0754a2492e0617';
const armName = 'AgPlayer-Setup-1.1.2-arm64.exe';
const historical = [
  { id: 647102603, name: 'AgPlayer-1.1.2-arm64-release.apk', size: 237844183,
    digest: 'sha256:d1c1739fff87f9d49f9f04a0d5d53ee0c5bed2ab70f1bb596659bb2ee8374777' },
  { id: 647102605, name: 'AgPlayer-1.1.2-macOS-universal.dmg', size: 114609080,
    digest: 'sha256:912b2c9659cf0e71ebb586155bee61ad15ac66cbb4688510109b89227a0ff993' },
  { id: 647102604, name: 'AgPlayer-Setup-1.1.2-x64.exe', size: 32027584,
    digest: 'sha256:fb5821b0931bbb56cad83284757ec7a05a14128f949d16b1ac6b582f4365b778' }
];

export function validateDescriptor(value) {
  if (!value || Object.keys(value).sort().join(',') !== 'name,sha256,size,stagingUrl,version'
      || value.version !== '1.1.2' || value.name !== armName
      || !Number.isSafeInteger(value.size) || value.size <= 0 || value.size > 2_000_000_000
      || typeof value.sha256 !== 'string' || !/^[a-f0-9]{64}$/.test(value.sha256)) throw new Error('ARM64 descriptor is not ready or has invalid metadata');
  let url;
  try { url = new URL(value.stagingUrl); } catch { throw new Error('Invalid ARM64 staging URL'); }
  if (url.protocol !== 'https:' || url.port || url.username || url.password || url.search || url.hash
      || !/^[a-z0-9](?:[a-z0-9-]*[a-z0-9])?\.trycloudflare\.com$/.test(url.hostname)
      || url.pathname !== '/' + armName || url.href !== value.stagingUrl) throw new Error('Unexpected ARM64 staging URL');
  return value;
}

function verifyArmAsset(asset, descriptor) {
  if (!Number.isSafeInteger(asset.id) || asset.id <= 0 || asset.name !== armName
      || asset.state !== 'uploaded' || asset.size !== descriptor.size
      || asset.digest !== 'sha256:' + descriptor.sha256) throw new Error('Existing ARM64 asset conflicts with verified metadata');
}

async function snapshot(descriptor, fetcher) {
  const read = async path => (await githubRequest(api + path, false, fetcher)).json();
  const release = await read('/releases/tags/v1.1.2');
  const tag = await read('/git/ref/tags/v1.1.2');
  if (release.id !== releaseId || release.tag_name !== 'v1.1.2' || release.draft || release.prerelease
      || release.target_commitish !== tagCommit || tag.object?.type !== 'commit' || tag.object.sha !== tagCommit) {
    throw new Error('Published release or original tag identity changed');
  }
  const assets = [];
  for (let page = 1; ; page++) {
    const batch = await read(`/releases/${releaseId}/assets?per_page=100&page=${page}`);
    if (!Array.isArray(batch)) throw new Error('Invalid release assets response');
    assets.push(...batch);
    if (batch.length < 100) break;
  }
  for (const expected of historical) {
    const matches = assets.filter(asset => asset.name === expected.name);
    if (matches.length !== 1 || matches[0].state !== 'uploaded'
        || Object.entries(expected).some(([key, value]) => matches[0][key] !== value)) {
      throw new Error(`Historical release asset changed: ${expected.name}`);
    }
  }
  const arm = assets.filter(asset => asset.name === armName);
  if (arm.length > 1) throw new Error('Duplicate ARM64 release assets');
  if (arm.length) verifyArmAsset(arm[0], descriptor);
  if (assets.some(asset => /\.(exe|dmg|apk)$/i.test(asset.name)
      && asset.name !== armName && !historical.some(file => file.name === asset.name))) {
    throw new Error('Unexpected installer in the original release');
  }
  return arm.length > 0;
}

export async function appendArmInstaller(input, directory, { fetcher = fetch, token = process.env.GH_TOKEN } = {}) {
  const descriptor = validateDescriptor(input);
  if (!token) throw new Error('GitHub release upload token is required');
  if (await snapshot(descriptor, fetcher)) return 'reused';
  // The token is never sent to staging, and staging redirects are never followed.
  const response = await fetcher(descriptor.stagingUrl, {redirect:'error', credentials:'omit',
    signal:AbortSignal.timeout(900_000), headers:{Accept:'application/octet-stream'}});
  if (response.status !== 200 || !response.body) throw new Error('ARM64 staging download failed');
  const path = join(directory, armName);
  let size = 0;
  const hash = createHash('sha256');
  const verify = new Transform({transform(chunk, encoding, callback) {
    size += chunk.length;
    if (size > descriptor.size) { callback(new Error('ARM64 staging download exceeds the verified size')); return; }
    hash.update(chunk);
    callback(null, chunk);
  }});
  await pipeline(Readable.fromWeb(response.body), verify, createWriteStream(path, {flags:'wx'}));
  if (size !== descriptor.size || hash.digest('hex') !== descriptor.sha256) throw new Error('ARM64 staging size or SHA-256 mismatch');
  // Recheck immediately before the sole remote mutation; an identical concurrent append is reusable.
  if (await snapshot(descriptor, fetcher)) return 'reused';
  const uploaded = await fetcher(`https://uploads.github.com/repos/gddjag/AgPlayer/releases/${releaseId}/assets?name=${armName}`, {
    method:'POST', redirect:'error', signal:AbortSignal.timeout(900_000), duplex:'half',
    headers:{Accept:'application/vnd.github+json', Authorization:`Bearer ${token}`,
      'X-GitHub-Api-Version':'2022-11-28', 'Content-Type':'application/octet-stream', 'Content-Length':String(size)},
    body:createReadStream(path)
  });
  if (uploaded.status !== 201) throw new Error(`GitHub ARM64 append failed (${uploaded.status}); existing assets were not overwritten`);
  verifyArmAsset(await uploaded.json(), descriptor);
  if (!await snapshot(descriptor, fetcher)) throw new Error('Uploaded ARM64 asset is missing');
  return 'uploaded';
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  let directory;
  try {
    const input = JSON.parse(await readFile(new URL('../docs/releases/1.1.2-windows-arm64.json', import.meta.url), 'utf8'));
    directory = await mkdtemp(join(tmpdir(), 'agplayer-arm64-append-'));
    console.log(`Verified ARM64 asset ${await appendArmInstaller(input, directory)}`);
  } catch (error) {
    console.error(error.code === 'ENOENT' ? 'ARM64 descriptor is not ready' : error.message);
    process.exitCode = 1;
  } finally {
    if (directory) await rm(directory, {recursive:true, force:true});
  }
}
