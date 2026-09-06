import { cp, mkdir, rm } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { dirname, join, resolve } from 'node:path';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const output = join(root, 'public');

// Only the generated public directory is replaced; source files stay untouched.
if (dirname(output) !== root) throw new Error('Output must remain inside the repository');
await rm(output, { recursive: true, force: true });
await mkdir(output);
for (const entry of ['index.html', 'download.html', 'about.html', 'assets']) {
  await cp(join(root, entry), join(output, entry), { recursive: true });
}
console.log('Static site ready in public/ (3 HTML pages and assets only).');
