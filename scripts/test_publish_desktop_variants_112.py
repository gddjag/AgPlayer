import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from urllib.parse import quote

spec = importlib.util.spec_from_file_location('variants', Path(__file__).with_name('publish-desktop-variants-1.1.2.py'))
variants = importlib.util.module_from_spec(spec)
spec.loader.exec_module(variants)


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


class FakeRemote:
    def __init__(self, baseline, descriptor, old_bytes, new_bytes):
        self.old_bytes, self.new_bytes = old_bytes, new_bytes
        self.assets = [dict(id=variants.BASE_IDS[file['name']], name=file['name'], state='uploaded',
                            size=file['size'], digest='sha256:' + file['sha256']) for file in baseline['files']]
        self.assets.append(dict(id=42, name='historical-original.dmg', state='uploaded', size=8, digest='sha256:history'))
        self.r2 = {variants.PREFIX + name: raw for name, raw in old_bytes.items()}
        self.r2.update({key: variants.encode(baseline) for key in variants.ENDPOINTS})
        self.r2[variants.PREFIX + 'SHA256SUMS'] = ''.join(
            file['sha256'] + '  ' + file['name'] + '\n' for file in baseline['files']).encode()
        self.r2['history/untouched'] = b'original history'
        self.release = dict(id=variants.RELEASE_ID, draft=False, prerelease=False,
                            target_commitish=variants.TAG_COMMIT, body='original notes')
        self.tag = dict(object=dict(sha=variants.TAG_COMMIT, type='commit'))
        self.events = []
        self.corrupt_staging = self.corrupt_github = self.corrupt_r2 = self.fail_upload = None
        self.expired = False
        self.next_id = 900000000
        self.fail_after_put = None

    def github(self, path, method='GET', **kwargs):
        if method == 'GET':
            if path == '/releases/tags/v1.1.2':
                return copy.deepcopy(self.release)
            if path == '/git/ref/tags/v1.1.2':
                return copy.deepcopy(self.tag)
            if path == f'/releases/{variants.RELEASE_ID}/assets?per_page=100':
                return copy.deepcopy(self.assets)
        if method == 'DELETE':
            asset = next(a for a in self.assets if path == f"/releases/assets/{a['id']}")
            self.events.append(('delete', asset['name']))
            self.assets.remove(asset)
            return None
        raise AssertionError('Forbidden GitHub operation: ' + method + ' ' + path)

    def upload(self, path, name):
        self.events.append(('upload', name))
        self.next_id += 1
        if name == self.fail_upload:
            self.assets.append(dict(id=self.next_id, name=name, state='starter', size=0, digest=None))
            raise OSError('interrupted upload')
        raw = path.read_bytes()
        self.assets.append(dict(id=self.next_id, name=name, state='uploaded', size=len(raw), digest='sha256:' + digest(raw)))

    def readback_asset(self, asset, path):
        name = asset['name']
        self.events.append(('github-readback', name))
        raw = (self.old_bytes | self.new_bytes)[name]
        path.write_bytes(b'corrupt github' if name == self.corrupt_github else raw)

    def get(self, key):
        if key not in self.r2:
            raise FileNotFoundError(key)
        self.events.append(('get', key))
        return b'corrupt r2' if key == self.corrupt_r2 else self.r2[key]

    def readback_object(self, key, path):
        self.events.append(('r2-readback', key))
        path.write_bytes(self.get(key))

    def put(self, key, body, content_type, metadata=None):
        self.events.append(('put', key))
        if key in self.r2:
            raise AssertionError('Append-only publication attempted to overwrite ' + key)
        self.r2[key] = body.read_bytes() if isinstance(body, Path) else body
        if key == self.fail_after_put:
            self.fail_after_put = None
            raise OSError('interrupted after atomic put')

    def staging(self, file, path):
        if self.expired:
            raise OSError('staging tunnel expired')
        self.events.append(('staging', file['name']))
        path.write_bytes(b'corrupt staging' if file['name'] == self.corrupt_staging else self.new_bytes[file['name']])


class PublishDesktopVariantsTests(unittest.TestCase):
    def setUp(self):
        self.base = json.loads(variants.load_baseline_bytes())
        self.old = {name: ('old:' + name).encode() for name in variants.BASE_IDS}
        self.new = {spec[0]: ('new:' + spec[0]).encode() for spec in variants.SPECS.values()}
        for file in self.base['files']:
            file.update(size=len(self.old[file['name']]), sha256=digest(self.old[file['name']]))
        self.descriptor = dict(version=variants.VERSION, revision=variants.REVISION, files=[
            dict(id=id, name=name, architectures=arch, bundledModels=models, size=len(self.new[name]),
                 sha256=digest(self.new[name]), sourceCommit=variants.SOURCE_COMMIT,
                 stagingUrl='https://random-release-tunnel.trycloudflare.com/' + quote(name, safe=''))
            for id, (name, arch, models) in variants.SPECS.items()])
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        baseline_path = Path(self.temporary.name) / 'baseline.json'
        baseline_path.write_bytes(variants.encode(self.base))
        for name, value in [('BASELINE_PATH', baseline_path), ('BASELINE_SHA256', digest(variants.encode(self.base)))]:
            patcher = patch.object(variants, name, value)
            patcher.start()
            self.addCleanup(patcher.stop)
        self.remote = FakeRemote(self.base, self.descriptor, self.old, self.new)

    def writes(self):
        return [event for event in self.remote.events if event[0] in {'put', 'upload', 'delete'}]

    def assert_not_promoted(self):
        for key in variants.VARIANT_ENDPOINTS:
            self.assertNotIn(key, self.remote.r2)
        self.assertNotIn(variants.SUMS_KEY, self.remote.r2)

    def test_appends_all_four_and_preserves_every_original(self):
        original_r2 = copy.deepcopy(self.remote.r2)
        original_assets = copy.deepcopy(self.remote.assets)
        receipt = variants.publish(self.descriptor, self.remote)
        self.assertTrue(receipt['published'])
        for key, raw in original_r2.items():
            self.assertEqual(self.remote.r2[key], raw)
        self.assertEqual(self.remote.assets[:len(original_assets)], original_assets)
        self.assertEqual(self.remote.release['body'], 'original notes')
        expected = variants.encode(variants.public_manifest(self.descriptor))
        for key in variants.VARIANT_ENDPOINTS:
            self.assertEqual(self.remote.r2[key], expected)
        first_write = next(i for i, event in enumerate(self.remote.events) if event[0] in {'put', 'upload', 'delete'})
        for name in self.new:
            self.assertLess(self.remote.events.index(('staging', name)), first_write)
        promotion = self.remote.events.index(('put', variants.SUMS_KEY))
        for name in self.new:
            self.assertLess(max(i for i, e in enumerate(self.remote.events) if e == ('github-readback', name)), promotion)
            self.assertLess(max(i for i, e in enumerate(self.remote.events) if e == ('r2-readback', variants.PREFIX + name)), promotion)
        public = receipt['manifest']
        self.assertEqual(set(public), {'schemaVersion', 'kind', 'version', 'tag', 'files'})
        for file in public['files']:
            self.assertNotIn('stagingUrl', file)
            self.assertTrue(file['r2Url'].endswith(quote(file['name'], safe='')))
            self.assertTrue(file['githubUrl'].endswith(quote(file['name'], safe='')))

    def test_completed_run_resumes_without_staging_or_writes(self):
        variants.publish(self.descriptor, self.remote)
        self.remote.events.clear()
        self.remote.expired = True
        variants.publish(self.descriptor, self.remote)
        self.assertFalse(self.writes())
        self.assertFalse(any(e[0] == 'staging' for e in self.remote.events))

    def test_final_staging_failure_cannot_write_anything(self):
        self.remote.corrupt_staging = list(self.new)[-1]
        with self.assertRaises(ValueError):
            variants.publish(self.descriptor, self.remote)
        self.assertEqual(len([e for e in self.remote.events if e[0] == 'staging']), 4)
        self.assertFalse(self.writes())

    def test_invalid_descriptors_reject_before_remote_access(self):
        mutations = [{'version': '1.1.3'}, {'revision': '20261029'}, {'files': self.descriptor['files'][:3]},
                     {'files': [self.descriptor['files'][0]] * 4}]
        for key, value in [('id', []), ('name', list(self.old)[0]), ('architectures', ['arm64']),
                           ('bundledModels', 1), ('size', True), ('size', 0), ('size', 2_000_000_000),
                           ('sha256', 'x' * 64), ('sourceCommit', 'a' * 40),
                           ('stagingUrl', 'https://evil.example/file'),
                           ('stagingUrl', self.descriptor['files'][0]['stagingUrl'] + '?x=1'),
                           ('stagingUrl', self.descriptor['files'][0]['stagingUrl'].replace('%EF%BC%88', '（')),
                           ('stagingUrl', self.descriptor['files'][0]['stagingUrl'].replace('https://', 'https://@'))]:
            descriptor = copy.deepcopy(self.descriptor)
            descriptor['files'][0][key] = value
            mutations.append(descriptor)
        for changes in mutations:
            descriptor = changes if 'files' in changes else self.descriptor | changes
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                variants.publish(descriptor, self.remote)
        self.assertEqual(self.remote.events, [])

    def test_pinned_baseline_tampering_is_rejected(self):
        variants.BASELINE_PATH.write_bytes(b'{}')
        with self.assertRaisesRegex(ValueError, 'baseline'):
            variants.publish(self.descriptor, self.remote)
        self.assertFalse(self.writes())

    def test_every_base_metadata_and_bytes_change_aborts_before_writes(self):
        for name in self.old:
            for mode in ('asset-id', 'asset-digest', 'github-bytes', 'r2-bytes'):
                with self.subTest(name=name, mode=mode):
                    remote = copy.deepcopy(self.remote)
                    asset = next(a for a in remote.assets if a['name'] == name)
                    if mode == 'asset-id': asset['id'] += 1
                    if mode == 'asset-digest': asset['digest'] = 'sha256:' + 'a' * 64
                    if mode == 'github-bytes': remote.corrupt_github = name
                    if mode == 'r2-bytes': remote.corrupt_r2 = variants.PREFIX + name
                    with self.assertRaises(ValueError):
                        variants.publish(self.descriptor, remote)
                    self.assertFalse(any(e[0] in {'put', 'upload', 'delete'} for e in remote.events))

    def test_canonical_feeds_checksum_list_and_tag_cannot_drift(self):
        for key in [*variants.ENDPOINTS, variants.PREFIX + 'SHA256SUMS']:
            remote = copy.deepcopy(self.remote)
            remote.r2[key] = b'drift'
            with self.subTest(key=key), self.assertRaises(ValueError):
                variants.publish(self.descriptor, remote)
            self.assertFalse(any(e[0] in {'put', 'upload', 'delete'} for e in remote.events))
        self.remote.tag['object']['sha'] = 'f' * 40
        with self.assertRaises(ValueError):
            variants.publish(self.descriptor, self.remote)
        self.assertFalse(self.writes())

    def test_same_name_unknown_github_or_r2_is_never_overwritten(self):
        name = next(iter(self.new))
        self.remote.assets.append(dict(id=123, name=name, state='uploaded', size=2, digest='sha256:unknown'))
        with self.assertRaises(ValueError):
            variants.publish(self.descriptor, self.remote)
        self.assertFalse(self.writes())
        self.remote.assets.pop()
        self.remote.r2[variants.PREFIX + name] = b'unknown existing bytes'
        with self.assertRaises(ValueError):
            variants.publish(self.descriptor, self.remote)
        self.assertFalse(self.writes())

    def test_interrupted_upload_only_deletes_new_zero_byte_starter(self):
        self.remote.fail_upload = list(self.new)[1]
        with self.assertRaises(OSError):
            variants.publish(self.descriptor, self.remote)
        self.assert_not_promoted()
        self.remote.fail_upload = None
        variants.publish(self.descriptor, self.remote)
        self.assertEqual([e for e in self.remote.events if e[0] == 'delete'], [('delete', list(self.new)[1])])
        for name in self.old:
            self.assertEqual(next(a['id'] for a in self.remote.assets if a['name'] == name), variants.BASE_IDS[name])

    def test_nonzero_starter_is_not_deletable(self):
        self.remote.assets.append(dict(id=123, name=next(iter(self.new)), state='starter', size=1, digest=None))
        with self.assertRaises(ValueError):
            variants.publish(self.descriptor, self.remote)
        self.assertFalse(self.writes())

    def test_github_readback_failure_never_promotes(self):
        self.remote.corrupt_github = list(self.new)[-1]
        with self.assertRaises(ValueError):
            variants.publish(self.descriptor, self.remote)
        self.assert_not_promoted()

    def test_r2_readback_failure_never_promotes(self):
        # Corrupt bytes only after upload, leaving initial missing-object preflight intact.
        original = self.remote.put
        def put(key, *args, **kwargs):
            original(key, *args, **kwargs)
            if key == variants.PREFIX + list(self.new)[-1]: self.remote.corrupt_r2 = key
        self.remote.put = put
        with self.assertRaises(ValueError):
            variants.publish(self.descriptor, self.remote)
        self.assert_not_promoted()

    def test_interrupted_metadata_promotion_resumes_without_upload(self):
        self.remote.fail_after_put = variants.VARIANT_ENDPOINTS[0]
        with self.assertRaises(OSError):
            variants.publish(self.descriptor, self.remote)
        self.assertIn(variants.VARIANT_ENDPOINTS[0], self.remote.r2)
        self.assertNotIn(variants.VARIANT_ENDPOINTS[1], self.remote.r2)
        self.remote.events.clear()
        self.remote.expired = True
        variants.publish(self.descriptor, self.remote)
        self.assertEqual(self.writes(), [('put', variants.VARIANT_ENDPOINTS[1])])

    def test_conflicting_public_manifest_rejects_before_installers(self):
        self.remote.r2[variants.VARIANT_ENDPOINTS[0]] = b'unknown manifest'
        with self.assertRaises(ValueError):
            variants.publish(self.descriptor, self.remote)
        self.assertFalse(self.writes())

    def test_real_remote_put_is_create_only_and_cannot_touch_canonical_keys(self):
        remote = variants.Remote.__new__(variants.Remote)
        calls = []
        remote.s3 = type('S3', (), {'put_object': lambda self, **kwargs: calls.append(kwargs)})()
        for key in [*variants.ENDPOINTS, variants.PREFIX + 'SHA256SUMS', *[variants.PREFIX + name for name in self.old]]:
            with self.subTest(key=key), self.assertRaises(ValueError):
                remote.put(key, b'forbidden', 'text/plain')
        self.assertFalse(calls)
        remote.put(variants.SUMS_KEY, b'verified checksums', 'text/plain')
        self.assertEqual(calls[0]['IfNoneMatch'], '*')
        self.assertEqual(calls[0]['Key'], variants.SUMS_KEY)

    def test_r2_transport_accepts_standard_readable_http_bodies(self):
        remote = variants.Remote.__new__(variants.Remote)
        payload = b'verified installer chunk' * 100000
        remote.object = lambda key: io.BytesIO(payload)
        target = Path(self.temporary.name) / 'transport-readback'
        remote.readback_object('test-object', target)
        self.assertEqual(target.read_bytes(), payload)

    def test_workflow_serializes_release_writes_and_runs_the_dedicated_tests(self):
        workflow = (Path(__file__).parents[1] / '.github/workflows/publish-desktop-variants-1.1.2.yml').read_text()
        self.assertIn('group: r2-release-latest', workflow)
        self.assertIn('cancel-in-progress: false', workflow)
        self.assertIn('docs/releases/1.1.2-desktop-variants-20261028.json', workflow)
        self.assertIn('test_publish_desktop_variants_112.py', workflow)
        self.assertIn('python scripts/publish-desktop-variants-1.1.2.py', workflow)
        self.assertIn('set -o pipefail', workflow)
        self.assertNotIn('republish-desktop-1.1.2.py', workflow)

    def test_transport_rejects_redirects_wrong_ranges_and_wrong_hashes(self):
        file = self.descriptor['files'][0]
        response = type('Response', (), {})()
        response.status_code, response.content = 302, self.new[file['name']]
        response.headers = {'Content-Range': f"bytes 0-{file['size'] - 1}/{file['size']}"}
        with self.assertRaises(ValueError):
            variants.download_staging(file, Path(self.temporary.name) / 'download', lambda *a, **k: response, attempts=1)
        response.status_code = 206
        response.headers['Content-Range'] = 'bytes 0-1/2'
        with self.assertRaises(ValueError):
            variants.download_staging(file, Path(self.temporary.name) / 'download', lambda *a, **k: response, attempts=1)
        response.headers['Content-Range'] = f"bytes 0-{file['size'] - 1}/{file['size']}"
        response.content = b'x' * file['size']
        with self.assertRaises(ValueError):
            variants.download_staging(file, Path(self.temporary.name) / 'download', lambda *a, **k: response, attempts=1)


if __name__ == '__main__':
    unittest.main()
