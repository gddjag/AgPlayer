import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { runInNewContext } from 'node:vm';

const root = new URL('../', import.meta.url);

test('published build includes a crawlable sitemap, ownership proof and real error page', async () => {
  execFileSync(process.execPath, ['scripts/build-site.mjs'], { cwd: root });
  const read = name => readFile(new URL(`public/${name}`, root), 'utf8');
  const robots = await read('robots.txt');
  assert.match(robots, /User-agent: \*[\s\S]*Allow: \//);
  assert.match(robots, /Sitemap: https:\/\/agplayer.com\/sitemap.xml/);
  const sitemap = await read('sitemap.xml');
  const urls = [...sitemap.matchAll(/<loc>(.*?)<\/loc>/g)].map(match => match[1]);
  assert.deepEqual(urls, ['https://agplayer.com/', 'https://agplayer.com/download', 'https://agplayer.com/about', 'https://agplayer.com/support', 'https://agplayer.com/privacy/android']);
  assert.match((await read('indexnow-key.txt')).trim(), /^[a-f0-9]{32}$/);
  assert.match(await read('404.html'), /noindex/);
  assert.match(await read('llms.txt'), /https:\/\/agplayer.com\/download/);
  for (const [file, url] of [['index.html', urls[0]], ['download.html', urls[1]], ['about.html', urls[2]]]) {
    const html = await read(file);
    assert.ok(html.includes(`<link rel="canonical" href="${url}">`), file);
    const schema = JSON.parse(html.match(/<script type="application\/ld\+json">([\s\S]*?)<\/script>/)[1]);
    assert.equal(schema['@context'], 'https://schema.org');
  }
});

test('language switching preserves page-specific search titles and descriptions', async () => {
  const translations = await readFile(new URL('assets/js/translations.js', root), 'utf8');
  const i18n = await readFile(new URL('assets/js/i18n.js', root), 'utf8');
  for (const [page, zh, en] of [
    ['home', 'AgPlayer 官网 - 免费本地音乐播放器与音频工具', 'AgPlayer — Free Local Music Player & Audio Tools'],
    ['download', 'AgPlayer 官方下载 - Windows、macOS、Android 与鸿蒙', 'Download AgPlayer — Windows, macOS, Android & HarmonyOS'],
    ['about', '关于 AgPlayer - 免费、无广告的本地音频播放器', 'About AgPlayer — Free, Ad-free Local Audio Player'],
  ]) {
    let description;
    const document = {
      body: { dataset: { page } }, documentElement: {},
      querySelectorAll: () => [],
      querySelector: () => ({ setAttribute: (_, value) => { description = value; } }),
      dispatchEvent() {},
    };
    const context = { window: {}, document, localStorage: { getItem: () => 'zh', setItem() {} }, navigator: {}, Event: class {} };
    runInNewContext(translations, context);
    runInNewContext(i18n, context);
    assert.equal(document.title, zh);
    const chineseDescription = description;
    context.window.AG.setLanguage('en');
    assert.equal(document.title, en);
    assert.notEqual(description, chineseDescription);
    assert.ok(description.includes('AgPlayer'));
  }
});
