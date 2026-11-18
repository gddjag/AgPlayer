import { cp, mkdir, readFile, rm, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { dirname, join, resolve } from 'node:path';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const output = join(root, 'public');
const read = name => readFile(join(root, name), 'utf8');
const [home, download] = await Promise.all([read('index.html'), read('download.html')]);

// Use the release-maintained download page for both entry points, including its verified package links.
function downloadPart(name) {
  const start = '<!-- ' + name + ':start -->';
  const end = '<!-- ' + name + ':end -->';
  const first = download.indexOf(start);
  const last = download.indexOf(end);
  if (first < 0 || last < first) throw new Error('Missing download block: ' + name);
  return download.slice(first + start.length, last).replaceAll('ag-icon-', 'one-download-icon-');
}
const picker = `<div class="platform-picker" role="group" aria-label="选择下载平台" data-i18n-aria="edition.platformLabel">
  <button type="button" data-platform-choice="windows" aria-controls="windows">Windows</button>
  <button type="button" data-platform-choice="macos" aria-controls="macos">macOS</button>
  <button type="button" data-platform-choice="android" aria-controls="android">Android</button>
  <button type="button" data-platform-choice="harmonyos" aria-controls="harmonyos">HarmonyOS</button>
</div>`;
const cards = '<div class="platforms download-platforms">';
const content = downloadPart('shared-download')
  .replaceAll('<h2', '<h3').replaceAll('</h2>', '</h3>')
  .replace('<h1 ', '<h2 id="download-title" ').replace('</h1>', '</h2>');
const downloadScript = download.match(/<script defer src="[^"]*assets\/js\/downloads\.js\?v=[^"]+"><\/script>/)?.[0];
const slot = /<!-- shared-download-slot:start -->[\s\S]*?<!-- shared-download-slot:end -->/;
const faqSlot = '<!-- shared-faq-slot -->';
if (!content.includes(cards) || !downloadScript || !slot.test(home) || !home.includes('<!-- shared-download-script -->')) {
  throw new Error('Cannot assemble the single-page download section.');
}
if (!home.includes(faqSlot)) throw new Error('Missing homepage FAQ slot.');
const page = home.replace(slot, () => content.replace(cards, picker + cards) + downloadPart('shared-download-icons'))
  .replace('<!-- shared-download-script -->', downloadScript)
  .replace(faqSlot, () => downloadPart('shared-faq'));

// Only the generated public directory is replaced; source files stay untouched.
if (dirname(output) !== root) throw new Error('Output must remain inside the repository');
await rm(output, { recursive: true, force: true });
await mkdir(output);
for (const entry of ['listen.html', 'download.html', 'about.html', 'support.html', 'assets', 'privacy', 'updates', 'immersive-stage.html', 'CNAME', 'docs/releases', '404.html', 'robots.txt', 'sitemap.xml', 'llms.txt', 'indexnow-key.txt', '_redirects', 'baidu_verify_codeva-WXajBvTPGJ.html']) {
  await cp(join(root, entry), join(output, entry), { recursive: true });
}
await writeFile(join(output, 'index.html'), page);
console.log('Static site ready in public/.');
