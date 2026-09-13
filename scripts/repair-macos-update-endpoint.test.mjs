import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, rm } from 'node:fs/promises';
import { writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { buildLatest } from './sync-release-r2.mjs';
import { repairLegacyManifest } from './repair-macos-update-endpoint.mjs';

const published = buildLatest({
  tag: 'v1.0.3', publishedAt: '2026-09-12T15:07:51Z',
  releaseNotesUrl: 'https://github.com/gddjag/AgPlayer/releases/tag/v1.0.3',
  files: [
    { name: 'AgPlayer-1.0.3-macOS-universal.dmg', size: 89616142,
      sha256: '2d2bb542b5bc6927ef91745de6ca71f5a55e5d71e9bfa6adb7ece24684aa99ab' },
    { name: 'AgPlayer-Setup-1.0.3-x64.exe', size: 36203853,
      sha256: '44247c0ffa169e19aa6b5e23d62d47f1b57affbc95ae738c0e184e81c8d28bed' }
  ]
});

for (const scenario of ['success', 'wrong-version', 'wrong-installer', 'changed-source', 'bad-readback']) {
  test(`manifest-only repair: ${scenario}`, async () => {
    const directory = await mkdtemp(join(tmpdir(), 'agplayer-manifest-repair-test-'));
    process.env.AWS_ACCESS_KEY_ID = 'test-only';
    process.env.AWS_SECRET_ACCESS_KEY = 'test-only';
    try {
      const source = structuredClone(published);
      if (scenario === 'wrong-version') source.version = '1.0.4';
      if (scenario === 'wrong-installer') source.files[0].sha256 = '0'.repeat(64);
      const bytes = Buffer.from(JSON.stringify(source) + '\n');
      const writtenKeys = [];
      const run = (command, args) => {
        assert.equal(command, 'aws');
        assert.equal(args[0], 's3api');
        const key = args[args.indexOf('--key') + 1];
        if (args[1] === 'get-object') {
          const data = key === 'updates/macos/latest.json' && scenario === 'bad-readback'
            ? Buffer.from('{}') : bytes;
          writeFileSync(args[args.indexOf('--key') + 2], data);
          return JSON.stringify({ ETag: '"reviewed-etag"' });
        }
        assert.equal(args[1], 'copy-object');
        assert.equal(key, 'updates/macos/latest.json');
        assert.equal(args[args.indexOf('--copy-source') + 1],
                     'agplayer-releases/updates/latest.json');
        assert.equal(args[args.indexOf('--copy-source-if-match') + 1], '"reviewed-etag"');
        assert.ok(args.includes('no-cache'));
        if (scenario === 'changed-source') throw new Error('PreconditionFailed');
        writtenKeys.push(key);
        return '{}';
      };
      if (scenario === 'success') {
        await repairLegacyManifest('v1.0.3', directory, run);
        assert.deepEqual(writtenKeys, ['updates/macos/latest.json']);
      } else {
        await assert.rejects(repairLegacyManifest('v1.0.3', directory, run),
          scenario === 'wrong-version' ? /canonical v1.0.3/
          : scenario === 'wrong-installer' ? /installer records differ/
          : scenario === 'changed-source' ? /PreconditionFailed/ : /readback differs/);
        assert.equal(writtenKeys.length, scenario === 'bad-readback' ? 1 : 0);
      }
    } finally {
      delete process.env.AWS_ACCESS_KEY_ID;
      delete process.env.AWS_SECRET_ACCESS_KEY;
      await rm(directory, { recursive: true, force: true });
    }
  });
}
