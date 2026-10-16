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

test('Android 1.1.2 signing compatibility is stated briefly in both languages', async () => {
  const html = await readFile(new URL('download.html', root), 'utf8');
  assert.match(html, /id="android"[\s\S]*?data-i18n="downloadPage\.android\.signing"/);
  const context = { window: {} };
  runInNewContext(await readFile(new URL('assets/js/translations.js', root), 'utf8'), context);
  const zh = context.window.AG_COPY.zh.downloadPage.android.signing;
  const en = context.window.AG_COPY.en.downloadPage.android.signing;
  assert.equal(zh, 'Android 1.1.2 使用新签名，无法覆盖安装旧版 APK。');
  assert.equal(en, 'Android 1.1.2 uses a new signing key and cannot install over an older APK.');
});
