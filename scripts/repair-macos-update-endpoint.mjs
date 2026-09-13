import { mkdir, readFile } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';
import { buildLatest } from './sync-release-r2.mjs';

const endpoint = 'https://50fb972f4425789ab744ba4c6614221e.r2.cloudflarestorage.com';
const bucket = 'agplayer-releases';
const expectedFiles = [
  ['AgPlayer-1.0.3-macOS-universal.dmg', 89616142,
   '2d2bb542b5bc6927ef91745de6ca71f5a55e5d71e9bfa6adb7ece24684aa99ab'],
  ['AgPlayer-Setup-1.0.3-x64.exe', 36203853,
   '44247c0ffa169e19aa6b5e23d62d47f1b57affbc95ae738c0e184e81c8d28bed']
];

export async function repairLegacyManifest(tag, directory, run = execFileSync) {
  if (tag !== 'v1.0.3') throw new Error('This repair is restricted to published v1.0.3');
  if (!process.env.AWS_ACCESS_KEY_ID || !process.env.AWS_SECRET_ACCESS_KEY) {
    throw new Error('R2 access key secrets are required');
  }
  await mkdir(directory, { recursive: true });
  const aws = args => run('aws', [...args, '--endpoint-url', endpoint, '--region', 'auto'],
                          { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] });
  const sourcePath = join(directory, 'published-latest.json');
  const source = JSON.parse(aws(['s3api', 'get-object', '--bucket', bucket,
                                 '--key', 'updates/latest.json', sourcePath]));
  const bytes = await readFile(sourcePath);
  const latest = JSON.parse(bytes.toString('utf8'));
  if (latest.tag !== tag || latest.version !== '1.0.3'
      || JSON.stringify(latest) !== JSON.stringify(buildLatest(latest))) {
    throw new Error('Published latest is not the expected canonical v1.0.3 manifest');
  }
  if (latest.files.length !== expectedFiles.length
      || expectedFiles.some(([name, size, sha256]) =>
           !latest.files.some(file => file.name === name && file.size === size
                                       && file.sha256 === sha256))) {
    throw new Error('Published installer records differ from the reviewed v1.0.3 release');
  }
  if (typeof source.ETag !== 'string' || !source.ETag) {
    throw new Error('Published manifest ETag is required');
  }
  // This is the only remote write. The source ETag condition prevents copying
  // a manifest that changed after validation. No installer path is written.
  aws(['s3api', 'copy-object', '--bucket', bucket,
       '--key', 'updates/macos/latest.json',
       '--copy-source', `${bucket}/updates/latest.json`,
       '--copy-source-if-match', source.ETag,
       '--metadata-directive', 'REPLACE',
       '--content-type', 'application/json; charset=utf-8', '--cache-control', 'no-cache']);
  const targetPath = join(directory, 'legacy-latest.json');
  aws(['s3api', 'get-object', '--bucket', bucket,
       '--key', 'updates/macos/latest.json', targetPath]);
  if (!(await readFile(targetPath)).equals(bytes)) {
    throw new Error('Legacy manifest readback differs from the published manifest');
  }
  console.log('Verified legacy macOS update manifest v1.0.3; installer objects unchanged');
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  try {
    await repairLegacyManifest(process.env.RELEASE_TAG,
      join(process.env.RUNNER_TEMP || process.cwd(), 'agplayer-legacy-manifest-repair'));
  } catch (error) {
    // Do not expose subprocess environments, credentials, or signed URLs.
    console.error(error.code ? `Legacy manifest repair failed (${error.code})` : error.message);
    process.exitCode = 1;
  }
}
