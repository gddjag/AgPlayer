import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('desktop_publish', Path(__file__).with_name('republish-desktop-1.1.2.py'))
publish = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publish)


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def encoded(value):
    return (json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode()


class FakeRemote:
    def __init__(self, baseline, descriptor, old_bytes, new_bytes):
        self.old_bytes, self.new_bytes = old_bytes, new_bytes
        self.assets = [dict(id=publish.IDS[f['name']], name=f['name'], state='uploaded',
                            size=f['size'], digest='sha256:' + f['sha256']) for f in baseline['files']]
        self.assets.append(copy.deepcopy(publish.EARLIER_MAC))
        self.release = dict(id=publish.RELEASE_ID, draft=False, prerelease=False,
                            target_commitish=publish.TAG_COMMIT, body='Original release notes')
        self.tag = dict(object=dict(sha=publish.TAG_COMMIT, type='commit', url=publish.API + '/git/commits/' + publish.TAG_COMMIT))
        self.r2 = {publish.PREFIX + name: raw for name, raw in old_bytes.items()}
        for endpoint in publish.ENDPOINTS:
            self.r2[endpoint] = encoded(baseline)
        self.r2[publish.PREFIX + 'SHA256SUMS'] = ''.join(f['sha256'] + '  ' + f['name'] + '\n' for f in baseline['files']).encode()
        self.r2[publish.PREFIX + 'history/20261016/' + publish.MAC] = b'preserved earlier history'
        self.events = []
        self.next_id = 900000000
        self.fail_upload = None
        self.corrupt_staging = None
        self.corrupt_github = None
        self.corrupt_readback_key = None
        self.staging_unavailable = False
        self.raise_after_put_key = None

    def github(self, path, method='GET', **kwargs):
        if path == '/releases/tags/v1.1.2':
            return copy.deepcopy(self.release)
        if path == '/git/ref/tags/v1.1.2':
            return copy.deepcopy(self.tag)
        if path == f'/releases/{publish.RELEASE_ID}/assets?per_page=100':
            return copy.deepcopy(self.assets)
        if method == 'PATCH' and path == f'/releases/{publish.RELEASE_ID}':
            self.events.append(('notes',))
            self.release.update(kwargs['json'])
            return copy.deepcopy(self.release)
        asset = next(a for a in self.assets if path == f"/releases/assets/{a['id']}")
        if method == 'PATCH':
            self.events.append(('rename', asset['name']))
            asset.update(kwargs['json'])
            return copy.deepcopy(asset)
        if method == 'DELETE':
            self.events.append(('delete', asset['id']))
            self.assets.remove(asset)
            return None
        raise AssertionError((path, method))

    def upload(self, path, name):
        self.events.append(('upload', name))
        self.next_id += 1
        if self.fail_upload == name:
            self.assets.append(dict(id=self.next_id, name=name, state='starter', size=0, digest=None))
            raise OSError('simulated interrupted GitHub upload')
        raw = path.read_bytes()
        self.assets.append(dict(id=self.next_id, name=name, state='uploaded', size=len(raw), digest='sha256:' + digest(raw)))

    def readback_asset(self, asset, path):
        self.events.append(('github-readback', asset['name']))
        raw = self.new_bytes[asset['name']]
        path.write_bytes(b'x' * len(raw) if self.corrupt_github == asset['name'] else raw)

    def get(self, key):
        if key not in self.r2:
            raise FileNotFoundError(key)
        return b'wrong readback' if self.corrupt_readback_key == key else self.r2[key]

    def put(self, key, body, content_type, metadata=None):
        self.events.append(('put', key))
        self.r2[key] = body.read_bytes() if isinstance(body, Path) else body
        if key == self.raise_after_put_key:
            self.raise_after_put_key = None
            raise OSError('simulated interruption after atomic object write')

    def staging(self, descriptor, path):
        if self.staging_unavailable:
            raise OSError('temporary staging tunnel expired')
        self.events.append(('staging', descriptor['name']))
        raw = self.new_bytes[descriptor['name']]
        path.write_bytes(b'x' * len(raw) if self.corrupt_staging == descriptor['name'] else raw)


class RepublishDesktopTests(unittest.TestCase):
    def setUp(self):
        self.old_bytes = {name: ('old:' + name).encode() for name in publish.IDS}
        self.new_bytes = {name: ('verified-new:' + name).encode() for name in publish.DESKTOP}
        self.baseline = publish.load_baseline()
        for f in self.baseline['files']:
            raw = self.old_bytes[f['name']]
            f.update(size=len(raw), sha256=digest(raw))
        self.descriptor = dict(version='1.1.2', revision='20261028', files=[
            dict(name=name, size=len(raw), sha256=digest(raw), sourceCommit=publish.SOURCE_COMMIT,
                 stagingUrl='https://verified-release.trycloudflare.com/' + name)
            for name, raw in self.new_bytes.items()])
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        baseline_path = Path(self.temp.name) / 'baseline.json'
        baseline_path.write_bytes(encoded(self.baseline))
        self.patch_path = patch.object(publish, 'BASELINE_PATH', baseline_path)
        self.patch_hash = patch.object(publish, 'BASELINE_SHA256', digest(encoded(self.baseline)))
        self.patch_path.start()
        self.patch_hash.start()
        self.addCleanup(self.patch_path.stop)
        self.addCleanup(self.patch_hash.stop)
        self.remote = FakeRemote(self.baseline, self.descriptor, self.old_bytes, self.new_bytes)

    def mutations(self):
        return [e for e in self.remote.events if e[0] in {'put', 'rename', 'upload', 'delete', 'notes'}]

    def test_promotes_all_three_desktop_packages_together_after_readbacks(self):
        receipt = publish.publish(self.descriptor, self.remote)
        self.assertTrue(receipt['published'])
        expected = publish.updated_manifest(self.baseline, self.descriptor)
        for endpoint in publish.ENDPOINTS:
            self.assertEqual(self.remote.r2[endpoint], encoded(expected))
        promotions = [i for i, e in enumerate(self.remote.events) if e[0] == 'put' and e[1] in publish.ENDPOINTS]
        self.assertEqual(len(promotions), 2)
        for name in publish.DESKTOP:
            self.assertLess(self.remote.events.index(('github-readback', name)), min(promotions))
            self.assertLess(self.remote.events.index(('put', publish.PREFIX + name)), min(promotions))
            self.assertEqual(self.remote.r2[publish.HISTORY + name], self.old_bytes[name])
            history = next(a for a in self.remote.assets if a['name'] == publish.ARCHIVES[name])
            self.assertEqual(history['id'], publish.IDS[name])
        self.assertEqual(self.remote.r2[publish.HISTORY + 'latest.json'], encoded(self.baseline))
        self.assertEqual(self.remote.r2[publish.PREFIX + publish.ANDROID], self.old_bytes[publish.ANDROID])
        self.assertEqual(next(f for f in expected['files'] if f['name'] == publish.ANDROID), self.baseline['files'][0])
        self.assertEqual(self.remote.r2[publish.PREFIX + 'history/20261016/' + publish.MAC], b'preserved earlier history')
        self.assertEqual(self.remote.tag['object']['sha'], publish.TAG_COMMIT)

    def test_rerun_preserves_all_history_and_never_uploads_twice(self):
        publish.publish(self.descriptor, self.remote)
        first_r2 = copy.deepcopy(self.remote.r2)
        first_assets = copy.deepcopy(self.remote.assets)
        self.remote.events.clear()
        publish.publish(self.descriptor, self.remote)
        self.assertEqual(self.remote.r2, first_r2)
        self.assertEqual(self.remote.assets, first_assets)
        self.assertFalse(self.mutations())

    def test_completed_revision_can_be_reverified_after_staging_tunnel_expires(self):
        publish.publish(self.descriptor, self.remote)
        self.remote.events.clear()
        self.remote.staging_unavailable = True
        try:
            receipt = publish.publish(self.descriptor, self.remote)
        except OSError as error:
            self.fail('A completed revision must use its verified GitHub assets: ' + str(error))
        self.assertTrue(receipt['published'])
        self.assertFalse(self.mutations())
        self.assertFalse(any(e[0] == 'staging' for e in self.remote.events))

    def test_partial_upload_resumes_only_verified_starter_asset(self):
        self.remote.fail_upload = publish.DESKTOP[1]
        with self.assertRaises(OSError):
            publish.publish(self.descriptor, self.remote)
        for endpoint in publish.ENDPOINTS:
            self.assertEqual(self.remote.r2[endpoint], encoded(self.baseline))
        self.remote.fail_upload = None
        publish.publish(self.descriptor, self.remote)
        self.assertEqual(json.loads(self.remote.r2['updates/latest.json']), publish.updated_manifest(self.baseline, self.descriptor))
        self.assertEqual(len([e for e in self.remote.events if e[0] == 'delete']), 1)
        for name in publish.DESKTOP:
            self.assertEqual(len([a for a in self.remote.assets if a['name'] == name]), 1)
            self.assertEqual(len([a for a in self.remote.assets if a['name'] == publish.ARCHIVES[name]]), 1)

    def test_interrupted_first_manifest_write_resumes_second_endpoint_without_reupload(self):
        self.remote.raise_after_put_key = publish.ENDPOINTS[0]
        with self.assertRaises(OSError):
            publish.publish(self.descriptor, self.remote)
        latest = encoded(publish.updated_manifest(self.baseline, self.descriptor))
        self.assertEqual(self.remote.r2[publish.ENDPOINTS[0]], latest)
        self.assertEqual(self.remote.r2[publish.ENDPOINTS[1]], encoded(self.baseline))
        publish.publish(self.descriptor, self.remote)
        self.assertEqual(self.remote.r2[publish.ENDPOINTS[1]], latest)
        self.assertEqual(len([e for e in self.remote.events if e[0] == 'upload']), 3)

    def test_invalid_descriptors_reject_versions_names_sizes_hashes_sources_and_urls(self):
        invalid = [dict(self.descriptor, revision='20261029'), dict(self.descriptor, version='1.1.3'),
                   dict(self.descriptor, files=self.descriptor['files'][:2]),
                   dict(self.descriptor, files=[self.descriptor['files'][0]] * 3)]
        for changes in [{'name': publish.ANDROID}, {'size': 0}, {'size': True}, {'sha256': 'bad'},
                        {'sourceCommit': 'b' * 40}, {'stagingUrl': 'https://evil.example/file'},
                        {'stagingUrl': self.descriptor['files'][0]['stagingUrl'] + '?x=1'},
                        {'stagingUrl': self.descriptor['files'][0]['stagingUrl'].replace('https:', 'http:')}]:
            d = copy.deepcopy(self.descriptor)
            d['files'][0].update(changes)
            invalid.append(d)
        for d in invalid:
            with self.subTest(descriptor=d), self.assertRaises(ValueError):
                publish.publish(d, self.remote)
        self.assertEqual(self.remote.events, [])

    def test_changed_android_github_or_r2_aborts_before_any_write(self):
        android = next(a for a in self.remote.assets if a['name'] == publish.ANDROID)
        android['digest'] = 'sha256:' + 'f' * 64
        with self.assertRaises(ValueError):
            publish.publish(self.descriptor, self.remote)
        self.assertFalse(self.mutations())
        android['digest'] = 'sha256:' + digest(self.old_bytes[publish.ANDROID])
        self.remote.r2[publish.PREFIX + publish.ANDROID] = b'drifted Android'
        with self.assertRaises(ValueError):
            publish.publish(self.descriptor, self.remote)
        self.assertFalse(self.mutations())

    def test_staging_failure_in_third_package_makes_no_remote_mutations(self):
        self.remote.corrupt_staging = publish.DESKTOP[2]
        with self.assertRaises(ValueError):
            publish.publish(self.descriptor, self.remote)
        self.assertEqual(len([e for e in self.remote.events if e[0] == 'staging']), 3)
        self.assertFalse(self.mutations())

    def test_history_conflicts_abort_before_renames_uploads_or_manifest_writes(self):
        for key in [publish.HISTORY + publish.MAC, publish.HISTORY + 'latest.json', publish.HISTORY + 'SHA256SUMS']:
            self.remote.r2[key] = b'conflicting historical bytes'
            with self.subTest(key=key), self.assertRaises(ValueError):
                publish.publish(self.descriptor, self.remote)
            self.assertFalse(self.mutations())
            del self.remote.r2[key]

    def test_unknown_assets_or_tag_or_manifest_are_not_adopted(self):
        original_tag = copy.deepcopy(self.remote.tag)
        self.remote.tag['object']['sha'] = '0' * 40
        with self.assertRaises(ValueError):
            publish.publish(self.descriptor, self.remote)
        self.remote.tag = original_tag
        original_latest = self.remote.r2['updates/latest.json']
        self.remote.r2['updates/latest.json'] = b'{"version":"unknown"}'
        with self.assertRaises(ValueError):
            publish.publish(self.descriptor, self.remote)
        self.remote.r2['updates/latest.json'] = original_latest
        self.remote.assets.append(dict(id=999, name='unexpected.exe', state='uploaded', size=1, digest=None))
        with self.assertRaises(ValueError):
            publish.publish(self.descriptor, self.remote)
        self.assertFalse(self.mutations())

    def test_bad_github_readback_never_promotes(self):
        self.remote.corrupt_github = publish.DESKTOP[2]
        with self.assertRaises(ValueError):
            publish.publish(self.descriptor, self.remote)
        self.assertFalse(any(e[0] == 'put' and e[1] in publish.ENDPOINTS for e in self.remote.events))

    def test_mixed_verified_states_resume_without_touching_android_or_old_archive(self):
        name = publish.MAC
        old = next(a for a in self.remote.assets if a['name'] == name)
        old['name'] = publish.ARCHIVES[name]
        self.remote.assets.append(dict(id=9001, name=name, state='uploaded', size=len(self.new_bytes[name]), digest='sha256:' + digest(self.new_bytes[name])))
        self.remote.r2[publish.HISTORY + name] = self.old_bytes[name]
        self.remote.r2[publish.PREFIX + name] = self.new_bytes[name]
        publish.publish(self.descriptor, self.remote)
        self.assertFalse(('upload', name) in self.remote.events)
        self.assertEqual(self.remote.r2[publish.PREFIX + publish.ANDROID], self.old_bytes[publish.ANDROID])

    def test_asset_validator_rejects_history_loss_wrong_partial_or_duplicate_name(self):
        for mutation in ['history-loss', 'wrong-starter', 'duplicate-name', 'old-history-drift']:
            assets = copy.deepcopy(self.remote.assets)
            if mutation == 'history-loss':
                a = next(a for a in assets if a['name'] == publish.MAC)
                a.update(id=9999, size=len(self.new_bytes[publish.MAC]), digest='sha256:' + digest(self.new_bytes[publish.MAC]))
            elif mutation == 'wrong-starter':
                a = next(a for a in assets if a['name'] == publish.MAC)
                a['name'] = publish.ARCHIVES[publish.MAC]
                assets.append(dict(id=9999, name=publish.MAC, state='starter', size=17, digest=None))
            elif mutation == 'duplicate-name':
                assets.append(dict(assets[0], id=9999))
            else:
                next(a for a in assets if a['name'] == publish.EARLIER_MAC['name'])['size'] = 2
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                publish.validate_assets(assets, self.descriptor)

    def test_github_readback_sends_auth_only_to_api_and_restricts_redirect_hosts(self):
        class Response:
            def __init__(self, code, location=None):
                self.status_code = code
                self.headers = {'Location': location} if location else {}
            def raise_for_status(self): pass
            def close(self): pass
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def iter_content(self, size): return iter([b'bytes'])
        class Requests:
            def __init__(self, location):
                self.responses = [Response(302, location), Response(200)]
                self.calls = []
            def get(self, url, **kwargs):
                self.calls.append((url, kwargs))
                return self.responses.pop(0)
        remote = publish.Remote.__new__(publish.Remote)
        remote.headers = {'Authorization': 'Bearer test-only-token'}
        remote.requests = Requests('https://release-assets.githubusercontent.com/verified?signature=test')
        path = Path(self.temp.name) / 'readback.dmg'
        remote.readback_asset(dict(id=123, size=5), path)
        self.assertEqual(path.read_bytes(), b'bytes')
        self.assertEqual(remote.requests.calls[0][1]['headers']['Authorization'], 'Bearer test-only-token')
        self.assertNotIn('headers', remote.requests.calls[1][1])
        for url in ['https://evil.example/asset', 'http://release-assets.githubusercontent.com/file',
                    'https://user:pass@release-assets.githubusercontent.com/file']:
            remote.requests = Requests(url)
            with self.subTest(url=url), self.assertRaises(ValueError):
                remote.readback_asset(dict(id=123, size=5), path)
            self.assertEqual(len(remote.requests.calls), 1)

    def test_ranges_are_bounded_and_never_receive_auth_or_follow_redirects(self):
        raw = b'a' * (4 * 1024 * 1024 + 17)
        d = self.descriptor['files'][0] | dict(size=len(raw), sha256=digest(raw))
        calls = []
        class Response:
            status_code = 206
        def fetch(url, **kwargs):
            calls.append(kwargs)
            start, end = map(int, kwargs['headers']['Range'].split('=')[1].split('-'))
            r = Response()
            r.headers = {'Content-Range': f'bytes {start}-{end}/{len(raw)}'}
            r.content = raw[start:end + 1]
            return r
        path = Path(self.temp.name) / 'range.dmg'
        publish.download_staging(d, path, fetch, attempts=1)
        self.assertEqual(path.read_bytes(), raw)
        self.assertEqual(len(calls), 2)
        for call in calls:
            self.assertFalse(call['allow_redirects'])
            self.assertEqual(set(call['headers']), {'Range'})
        Response.status_code = 302
        with self.assertRaises(ValueError):
            publish.download_staging(d, path, fetch, attempts=1)


if __name__ == '__main__':
    unittest.main()
