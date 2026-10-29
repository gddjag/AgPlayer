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

test('download cards expose chip/model choices and the requested bilingual model notes', async () => {
  const html = await readFile(new URL('download.html', root), 'utf8');
  const context = {window:{}};
  runInNewContext(await readFile(new URL('assets/js/translations.js', root), 'utf8'),context);
  assert.match(html, /id="windows-architecture"[^]*?value="bundled"/);
  assert.match(html, /id="macos-architecture"[^]*?value="arm64"[^]*?value="x86_64"[^]*?value="bundled"/);
  for (const lang of ['zh','en']) {
    const page = context.window.AG_COPY[lang].downloadPage;
    for (const platform of ['windows','macos','android','harmony']) {
      assert.match(html,new RegExp(`data-i18n="downloadPage\\.${platform}\\.modelsNote"`));
      assert.equal(typeof page[platform].modelsNote,'string');
      assert.ok(page[platform].modelsNote.length>8);
    }
  }
  assert.equal(context.window.AG_COPY.zh.downloadPage.windows.modelsNote,'常规纯净版不含伴奏分离模型，可在「音频工具」中按需下载。');
  assert.equal(context.window.AG_COPY.zh.downloadPage.android.modelsNote,'已内置伴奏分离模型。');
  assert.doesNotMatch(html,/downloadPage\.(windows|macos)\.revision/);
});


test('published desktop variants have an exact embedded fallback and Apple chip HTML default', async () => {
  const variants = JSON.parse(await readFile(new URL('updates/desktop-variants.json', root), 'utf8'));
  const source = await readFile(new URL('assets/js/downloads.js', root), 'utf8');
  const match = source.match(/const publishedDesktopVariants = ([\s\S]*?);\n  const maxManifestBytes/);
  assert.ok(match);
  assert.deepEqual(JSON.parse(match[1]), variants);
  assert.equal(variants.files.length, 4);
  const arm = variants.files.find(f => f.id === 'macos-arm64');
  const html = await readFile(new URL('download.html', root), 'utf8');
  assert.ok(html.includes(`href="${arm.r2Url}"`));
  assert.ok(html.includes(arm.sha256.toUpperCase()));
  assert.equal(variants.files.filter(f => f.bundledModels).length, 2);
  for (const f of variants.files) {
    assert.equal(f.sourceCommit, f.id.startsWith('macos-')
      ? 'ed00b162b0a3b49164475fb5a01d638bd8e5b508' : 'a351bdb13302e38520d195df5405d40ddac0a23e');
    assert.ok(f.size > 50_000_000);
    if (f.bundledModels) assert.ok(f.name.includes('（内置伴奏分离模型版本）'));
    assert.equal(decodeURIComponent(new URL(f.r2Url).pathname.split('/').at(-1)), f.name);
  }
});
