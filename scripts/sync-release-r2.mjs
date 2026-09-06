import { createReadStream, createWriteStream } from 'node:fs';
import { mkdir, readFile, stat, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';
import { Readable } from 'node:stream';
import { pipeline } from 'node:stream/promises';

const api = 'https://api.github.com/repos/gddjag/AgPlayer';
const endpoint = 'https://50fb972f4425789ab744ba4c6614221e.r2.cloudflarestorage.com';
const bucket = 'agplayer-releases';
const allowedHosts = new Set(['api.github.com', 'release-assets.githubusercontent.com', 'objects.githubusercontent.com']);

export function validateTag(tag) {
  if (!/^v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/.test(tag ?? '')) {
    throw new Error('A stable release tag vX.Y.Z is required');
  }
  return tag;
}

export function selectAssets(release, assets, tag) {
  if (release.tag_name !== tag || release.draft || release.prerelease || !release.published_at) {
    throw new Error('Release must be published, non-draft, and non-prerelease');
  }
  const selected = assets.filter(asset => /\.exe$/i.test(asset.name));
  if (!selected.length) throw new Error('No EXE assets: upload the installers, then rerun with the release tag');
  const names = new Set();
  for (const asset of selected) {
    if (!/^[a-zA-Z0-9][a-zA-Z0-9._-]*\.exe$/i.test(asset.name) || names.has(asset.name.toLowerCase())) {
      throw new Error('EXE names must be unique portable filenames using letters, digits, dots, underscores, or hyphens');
    }
    if (!Number.isSafeInteger(asset.id) || asset.id <= 0 || asset.state !== 'uploaded' || !Number.isSafeInteger(asset.size) || asset.size <= 0) {
      throw new Error(`EXE is not fully uploaded: ${asset.name}`);
    }
    names.add(asset.name.toLowerCase());
  }
  return selected.sort((a, b) => a.name.localeCompare(b.name, 'en'));
}

// Follow only official asset redirects. Never forward the GitHub token to a CDN.
export async function githubRequest(url, binary = false, fetcher = fetch) {
  for (let redirects = 0; redirects < 5; redirects++) {
    const parsed = new URL(url);
    if (parsed.protocol !== 'https:' || parsed.port || parsed.username || parsed.password || !allowedHosts.has(parsed.hostname)) {
      throw new Error('Unexpected GitHub download host');
    }
    const headers = { Accept: binary ? 'application/octet-stream' : 'application/vnd.github+json', 'User-Agent': 'AgPlayer-release-sync' };
    if (parsed.hostname === 'api.github.com') {
      headers['X-GitHub-Api-Version'] = '2022-11-28';
      if (process.env.GH_TOKEN) headers.Authorization = `Bearer ${process.env.GH_TOKEN}`;
    }
    const response = await fetcher(url, { headers, redirect: 'manual', signal: AbortSignal.timeout(120_000) });
    if ([301, 302, 303, 307, 308].includes(response.status)) {
      const location = response.headers.get('location');
      await response.body?.cancel();
      if (!location) throw new Error('GitHub redirect has no destination');
      url = new URL(location, url).href;
      continue;
    }
    if (!response.ok) throw new Error(`GitHub request failed (${response.status})`);
    return response;
  }
  throw new Error('Too many GitHub asset redirects');
}

async function snapshot(tag) {
  const release = await (await githubRequest(`${api}/releases/tags/${tag}`)).json();
  if (!Number.isSafeInteger(release.id)) throw new Error('Invalid release ID');
  const assets = [];
  for (let page = 1; ; page++) {
    const batch = await (await githubRequest(`${api}/releases/${release.id}/assets?per_page=100&page=${page}`)).json();
    if (!Array.isArray(batch)) throw new Error('Invalid release asset list');
    assets.push(...batch);
    if (batch.length < 100) break;
  }
  return selectAssets(release, assets, tag).map(({ id, name, size, digest, updated_at }) => ({ id, name, size, digest, updated_at }));
}

async function sha256(path) {
  const hash = createHash('sha256');
  for await (const chunk of createReadStream(path)) hash.update(chunk);
  return hash.digest('hex');
}

export async function download(tag, directory) {
  const assets = await snapshot(tag);
  await mkdir(directory, { recursive: true });
  const files = [];
  for (const asset of assets) {
    const path = join(directory, asset.name);
    const response = await githubRequest(`${api}/releases/assets/${asset.id}`, true);
    await pipeline(Readable.fromWeb(response.body), createWriteStream(path));
    if ((await stat(path)).size !== asset.size) throw new Error(`Incomplete download: ${asset.name}`);
    const hash = await sha256(path);
    if (asset.digest && asset.digest !== `sha256:${hash}`) throw new Error(`GitHub digest mismatch: ${asset.name}`);
    files.push({ name: asset.name, size: asset.size, sha256: hash });
  }
  if (JSON.stringify(assets) !== JSON.stringify(await snapshot(tag))) throw new Error('Release assets changed during download; rerun after all uploads finish');
  await writeFile(join(directory, 'manifest.json'), JSON.stringify({ tag, files }));
  await writeFile(join(directory, 'SHA256SUMS'), files.map(file => `${file.sha256}  ${file.name}\n`).join(''));
  console.log(`Verified ${files.length} EXE asset(s) for ${tag}`);
}

export async function upload(tag, directory, run = execFileSync) {
  if (!process.env.AWS_ACCESS_KEY_ID || !process.env.AWS_SECRET_ACCESS_KEY) throw new Error('R2 access key secrets are required');
  const manifest = JSON.parse(await readFile(join(directory, 'manifest.json'), 'utf8'));
  if (manifest.tag !== tag || !Array.isArray(manifest.files) || !manifest.files.length) throw new Error('Invalid download manifest');
  // Validate everything before any remote write, including the final checksum file.
  selectAssets({ tag_name: tag, published_at: true }, manifest.files.map((file, i) => ({ ...file, id: i + 1, state: 'uploaded' })), tag);
  for (const file of manifest.files) {
    if ((await stat(join(directory, file.name))).size !== file.size || await sha256(join(directory, file.name)) !== file.sha256) throw new Error('Local EXE changed after verification');
  }
  const sums = manifest.files.map(file => `${file.sha256}  ${file.name}\n`).join('');
  if (await readFile(join(directory, 'SHA256SUMS'), 'utf8') !== sums) throw new Error('Checksum file changed after verification');
  const aws = args => run('aws', [...args, '--endpoint-url', endpoint, '--region', 'auto'], { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] });
  for (const file of manifest.files) {
    const key = `releases/${tag}/${file.name}`;
    aws(['s3', 'cp', join(directory, file.name), `s3://${bucket}/${key}`, '--only-show-errors', '--content-type', 'application/octet-stream', '--metadata', `sha256=${file.sha256}`]);
    const head = JSON.parse(aws(['s3api', 'head-object', '--bucket', bucket, '--key', key]));
    if (head.ContentLength !== file.size || head.Metadata?.sha256 !== file.sha256) throw new Error(`R2 object verification failed: ${file.name}`);
  }
  // No latest pointer, recursive sync, or deletion; publish the manifest last.
  aws(['s3', 'cp', join(directory, 'SHA256SUMS'), `s3://${bucket}/releases/${tag}/SHA256SUMS`, '--only-show-errors', '--content-type', 'text/plain; charset=utf-8', '--cache-control', 'no-cache']);
  console.log(`Synced ${manifest.files.length} EXE asset(s) and SHA256SUMS to releases/${tag}/`);
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  try {
    const tag = validateTag(process.env.RELEASE_TAG);
    const directory = join(process.env.RUNNER_TEMP || process.cwd(), 'agplayer-release-sync', tag);
    if (process.argv[2] === 'download') await download(tag, directory);
    else if (process.argv[2] === 'upload') await upload(tag, directory);
    else throw new Error('Expected download or upload command');
  } catch (error) {
    // Avoid printing signed redirect URLs, subprocess environments, or credentials.
    console.error(error.code ? `Release sync failed (${error.code})` : error.message);
    process.exitCode = 1;
  }
}
