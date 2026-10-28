import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { runInNewContext } from 'node:vm';

const root = new URL('../', import.meta.url);
test('homepage preserves the authorized analytics integration exactly once', async () => {
  const index = await readFile(new URL('index.html', root), 'utf8');
  assert.equal(index.split('d753c9d11730ddacf686e8ccddce6e11').length - 1, 1);
});

test('release sync never automatically prunes historical installers', async () => {
  const workflow = await readFile(new URL('.github/workflows/sync-release-r2.yml', root), 'utf8');
  assert.match(workflow, /sync-release-r2\.mjs download/);
  assert.match(workflow, /sync-release-r2\.mjs upload/);
  assert.doesNotMatch(workflow, /sync-release-r2\.mjs prune|aws\s+s3\s+rm|delete-object/);
});

test('same-origin published manifest matches all four exact fallback installers', async () => {
  const manifest = JSON.parse(await readFile(new URL('updates/latest.json', root), 'utf8'));
  const source = await readFile(new URL('assets/js/downloads.js', root), 'utf8');
  const build = await readFile(new URL('scripts/build-site.mjs', root), 'utf8');
  assert.match(source, /const manifestUrl = '\/updates\/latest\.json'/);
  assert.match(build, /'updates'/);
  assert.equal(manifest.version, '1.1.2');
  for (const platform of ['Windows', 'WindowsArm', 'Mac', 'Android']) {
    const match = source.match(new RegExp(`const published${platform}Release = Object.freeze\\((\\{[\\s\\S]*?\\})\\);`));
    assert.ok(match, platform);
    const fallback = runInNewContext(`(${match[1]})`);
    const file = manifest.files.find(file => file.r2Url === fallback.r2Url);
    assert.ok(file, platform);
    assert.equal(fallback.version, manifest.version);
    assert.equal(fallback.size, file.size);
    assert.equal(fallback.sha256, file.sha256.toUpperCase());
    assert.equal(fallback.githubUrl, file.githubUrl);
    const url = new URL(fallback.githubUrl);
    assert.ok(url.pathname.endsWith('/' + file.name));
    assert.equal(url.search, file.revision === undefined ? '' : `?revision=${file.revision}`);
  }
  assert.equal(manifest.files.length, 4);
});
