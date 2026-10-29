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

_spec = importlib.util.spec_from_file_location('demucs_publish', Path(__file__).with_name('republish-macos-demucs-1.1.2.py'))
publisher = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(publisher)


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


class FakeRemote:
    def __init__(self, baselines, raw, originals, replacements):
        self.assets = copy.deepcopy(baselines['assets']['assets'])
        self.bytes_by_id = {}
        self.originals, self.replacements = originals, replacements
        for asset in self.assets:
            original = next(name for name in originals if github_name(name) == asset['name'])
            self.bytes_by_id[asset['id']] = originals[original]
        self.r2 = {publisher.PREFIX + name: value for name, value in originals.items()}
        self.r2.update({key: raw['latest'] for key in publisher.LATEST_KEYS})
        self.r2.update({key: raw['variants'] for key in publisher.VARIANT_KEYS})
        self.r2[publisher.SUMS_KEY] = publisher.checksum_bytes(baselines['latest'])
        self.r2[publisher.VARIANT_SUMS_KEY] = publisher.checksum_bytes(baselines['variants'])
        self.r2['unrelated/history-evidence'] = b'preserve this history'
        self.release = dict(id=publisher.RELEASE_ID, draft=False, prerelease=False,
                            target_commitish=publisher.TAG_COMMIT, body='Original release notes')
        self.tag = dict(object=dict(sha=publisher.TAG_COMMIT, type='commit'))
        self.events = []
        self.next_id = 900000000
        self.fail_upload = self.corrupt_staging = self.corrupt_github = self.corrupt_r2 = None
        self.fail_after = None
        self.expired = False
        self.metadata_at_delete = []

    def mutation(self, event):
        self.events.append(event)
        if event == self.fail_after:
            self.fail_after = None
            raise OSError('interrupted after ' + repr(event))

    def github(self, path, method='GET', **kwargs):
        if method == 'GET':
            if path == '/releases/tags/v1.1.2': return copy.deepcopy(self.release)
            if path == '/git/ref/tags/v1.1.2': return copy.deepcopy(self.tag)
            if path == f'/releases/{publisher.RELEASE_ID}/assets?per_page=100': return copy.deepcopy(self.assets)
        asset = next(asset for asset in self.assets if path == f"/releases/assets/{asset['id']}")
        if method == 'PATCH':
            self.assert_rename_only(kwargs)
            original = asset['name']
            asset.update(kwargs['json'])
            self.mutation(('rename', original))
            return copy.deepcopy(asset)
        if method == 'DELETE':
            self.metadata_at_delete.append({key: self.r2[key] for key in (*publisher.LATEST_KEYS, *publisher.VARIANT_KEYS,
                                                                        publisher.SUMS_KEY, publisher.VARIANT_SUMS_KEY)})
            self.assets.remove(asset)
            self.mutation(('delete', asset['id']))
            return None
        raise AssertionError('Forbidden operation ' + method)

    @staticmethod
    def assert_rename_only(kwargs):
        if set(kwargs['json']) != {'name'}:
            raise AssertionError('Release notes/labels must not be patched')

    def upload(self, path, name):
        self.next_id += 1
        raw = path.read_bytes()
        asset = dict(id=self.next_id, name=publisher.GITHUB_NAMES[name], label=name, state='uploaded',
                     size=len(raw), digest='sha256:' + digest(raw))
        if name == self.fail_upload:
            asset.update(state='starter', size=0, digest=None)
        self.assets.append(asset)
        self.bytes_by_id[asset['id']] = raw
        self.mutation(('upload', name))
        if name == self.fail_upload:
            raise OSError('interrupted GitHub upload')

    def readback_asset(self, asset, path):
        self.events.append(('github-readback', asset['name']))
        raw = self.bytes_by_id[asset['id']]
        path.write_bytes(b'corrupt github readback' if asset['name'] == self.corrupt_github else raw)

    def get(self, key):
        if key not in self.r2: raise FileNotFoundError(key)
        self.events.append(('get', key))
        return b'corrupt r2 readback' if key == self.corrupt_r2 else self.r2[key]

    def readback_object(self, key, path):
        self.events.append(('r2-readback', key))
        path.write_bytes(self.get(key))

    def put(self, key, body, content_type, metadata=None):
        if key not in publisher.CANONICAL_KEYS | publisher.HISTORY_KEYS:
            raise AssertionError('Forbidden R2 write: ' + key)
        if key in publisher.HISTORY_KEYS and key in self.r2:
            raise AssertionError('Attempt to overwrite immutable history')
        self.r2[key] = body.read_bytes() if isinstance(body, Path) else body
        self.mutation(('put', key))

    def staging(self, file, path):
        if self.expired: raise OSError('Staging tunnel expired')
        self.events.append(('staging', file['name']))
        path.write_bytes(b'corrupt staged bytes' if file['name'] == self.corrupt_staging else self.replacements[file['name']])


def github_name(name):
    return publisher.GITHUB_NAMES.get(name, publisher._shared.GITHUB_NAMES.get(name, name))


class RepublishMacDemucsTests(unittest.TestCase):
    def setUp(self):
        _, self.baselines = publisher.load_baselines()
        self.old = {file['name']: ('old:' + file['name']).encode()
                    for kind in ('latest', 'variants') for file in self.baselines[kind]['files']}
        self.new = {spec[0]: ('new-demucs:' + spec[0]).encode() for spec in publisher.SPECS.values()}
        for kind in ('latest', 'variants'):
            for file in self.baselines[kind]['files']:
                file.update(size=len(self.old[file['name']]), sha256=digest(self.old[file['name']]))
        for asset in self.baselines['assets']['assets']:
            name = next(name for name in self.old if github_name(name) == asset['name'])
            asset.update(size=len(self.old[name]), digest='sha256:' + digest(self.old[name]))
        self.raw = {kind: publisher.encode(value) for kind, value in self.baselines.items()}
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        paths = {}
        for kind, raw in self.raw.items():
            paths[kind] = Path(self.temporary.name) / (kind + '.json')
            paths[kind].write_bytes(raw)
        for name, value in [('BASELINE_PATHS', paths), ('BASELINE_SHA256', {kind: digest(raw) for kind, raw in self.raw.items()})]:
            patcher = patch.object(publisher, name, value)
            patcher.start()
            self.addCleanup(patcher.stop)
        self.descriptor = dict(version=publisher.VERSION, revision=publisher.REVISION, sourceCommit=publisher.SOURCE_COMMIT,
            files=[dict(id=id, name=name, architectures=arch, bundledModels=models, size=len(self.new[name]),
                        sha256=digest(self.new[name]), sourceCommit=publisher.SOURCE_COMMIT,
                        stagingUrl='https://verified-mac-release.trycloudflare.com/' + quote(name, safe=''))
                   for id, (name, arch, models) in publisher.SPECS.items()])
        self.remote = FakeRemote(self.baselines, self.raw, self.old, self.new)
        self.original_r2 = copy.deepcopy(self.remote.r2)
        self.original_assets = copy.deepcopy(self.remote.assets)

    def writes(self, remote=None):
        return [e for e in (remote or self.remote).events if e[0] in {'put', 'rename', 'upload', 'delete'}]

    def assert_not_promoted(self, remote=None):
        remote = remote or self.remote
        for key in publisher.LATEST_KEYS:
            self.assertEqual(remote.r2[key], self.raw['latest'])
        for key in publisher.VARIANT_KEYS:
            self.assertEqual(remote.r2[key], self.raw['variants'])
        self.assertEqual(remote.r2[publisher.SUMS_KEY], self.original_r2[publisher.SUMS_KEY])
        self.assertEqual(remote.r2[publisher.VARIANT_SUMS_KEY], self.original_r2[publisher.VARIANT_SUMS_KEY])

    def assert_protected_unchanged(self, remote=None):
        remote = remote or self.remote
        for original in self.original_assets:
            if original['name'] not in publisher.GITHUB_NAMES.values():
                self.assertIn(original, remote.assets)
        for name, raw in self.old.items():
            if name not in self.new:
                self.assertEqual(remote.r2[publisher.PREFIX + name], raw)
        self.assertEqual(remote.r2['unrelated/history-evidence'], b'preserve this history')
        self.assertEqual(remote.tag['object']['sha'], publisher.TAG_COMMIT)
        self.assertEqual(remote.release['body'], 'Original release notes')

    def test_success_archives_four_old_macs_and_promotes_only_after_every_readback(self):
        receipt = publisher.publish(self.descriptor, self.remote)
        self.assertTrue(receipt['published'])
        self.assert_protected_unchanged()
        expected = publisher.public_manifests(self.descriptor)
        for key in publisher.LATEST_KEYS: self.assertEqual(self.remote.r2[key], publisher.encode(expected['latest']))
        for key in publisher.VARIANT_KEYS: self.assertEqual(self.remote.r2[key], publisher.encode(expected['variants']))
        self.assertEqual(len(self.remote.assets), 8)
        self.assertFalse(any(a['name'].endswith(publisher.SUFFIX) for a in self.remote.assets))
        first_write = next(i for i, e in enumerate(self.remote.events) if e[0] in {'put', 'rename', 'upload', 'delete'})
        for name in self.new:
            self.assertLess(self.remote.events.index(('staging', name)), first_write)
            self.assertEqual(self.remote.r2[publisher.HISTORY + name], self.old[name])
            asset = next(a for a in self.remote.assets if a['name'] == publisher.GITHUB_NAMES[name])
            self.assertEqual(asset['label'], name)
            self.assertEqual(asset['digest'], 'sha256:' + digest(self.new[name]))
        self.assertEqual(self.remote.r2[publisher.HISTORY + 'latest.json'], self.raw['latest'])
        self.assertEqual(self.remote.r2[publisher.HISTORY + 'desktop-variants.json'], self.raw['variants'])
        promotion = self.remote.events.index(('put', publisher.SUMS_KEY))
        for name in self.new:
            self.assertLess(self.remote.events.index(('github-readback', publisher.GITHUB_NAMES[name])), promotion)
            self.assertLess(self.remote.events.index(('put', publisher.PREFIX + name)), promotion)
        for state in self.remote.metadata_at_delete:
            self.assertEqual(state[publisher.LATEST_KEYS[0]], publisher.encode(expected['latest']))
            self.assertEqual(state[publisher.LATEST_KEYS[1]], publisher.encode(expected['latest']))
            self.assertEqual(state[publisher.VARIANT_KEYS[0]], publisher.encode(expected['variants']))
            self.assertEqual(state[publisher.VARIANT_KEYS[1]], publisher.encode(expected['variants']))
        self.assertEqual(receipt['manifestSha256'], {kind: digest(publisher.encode(value)) for kind, value in expected.items()})
        self.assertEqual(set(receipt['removedTemporaryOldAssetIds']),
                         {a['id'] for a in self.original_assets if a['name'] in publisher.GITHUB_NAMES.values()})

    def test_manifests_preserve_every_windows_android_record_and_revision_urls_are_exact(self):
        manifests = publisher.public_manifests(self.descriptor)
        for kind in ('latest', 'variants'):
            before = {file['name']: file for file in self.baselines[kind]['files']}
            for file in manifests[kind]['files']:
                if file['name'] not in self.new:
                    self.assertEqual(file, before[file['name']])
                else:
                    self.assertEqual(file['revision'], '20261029')
                    self.assertEqual(file['sourceCommit'], publisher.SOURCE_COMMIT)
                    self.assertEqual(file['r2Url'], 'https://download.agplayer.com/' + publisher.PREFIX
                                     + quote(file['name'], safe='') + '?revision=20261029')
                    self.assertEqual(file['githubUrl'], 'https://github.com/gddjag/AgPlayer/releases/download/v1.1.2/'
                                     + publisher.GITHUB_NAMES[file['name']] + '?revision=20261029')
            self.assertEqual({k: v for k, v in manifests[kind].items() if k != 'files'},
                             {k: v for k, v in self.baselines[kind].items() if k != 'files'})

    def test_complete_retry_uses_verified_mirrors_and_is_an_exact_no_op(self):
        publisher.publish(self.descriptor, self.remote)
        previous = copy.deepcopy((self.remote.r2, self.remote.assets))
        self.remote.events.clear()
        self.remote.expired = True
        publisher.publish(self.descriptor, self.remote)
        self.assertEqual((self.remote.r2, self.remote.assets), previous)
        self.assertFalse(self.writes())
        self.assertFalse(any(e[0] == 'staging' for e in self.remote.events))

    def test_invalid_descriptors_and_tampered_baselines_fail_before_any_remote_access(self):
        invalid = [self.descriptor | {'revision': '20261030'}, self.descriptor | {'sourceCommit': 'a' * 40},
                   self.descriptor | {'version': '1.1.3'}, self.descriptor | {'files': self.descriptor['files'][:3]},
                   self.descriptor | {'files': [self.descriptor['files'][0]] * 4}]
        for key, value in [('id', 'windows-models'), ('size', True), ('size', 0), ('sha256', 'F' * 64),
                           ('bundledModels', 0), ('sourceCommit', 'a' * 40), ('architectures', ['arm64', 'x86_64']),
                           ('stagingUrl', 'https://evil.example/file'), ('stagingUrl', 'https://@verified-mac-release.trycloudflare.com/x')]:
            descriptor = copy.deepcopy(self.descriptor)
            descriptor['files'][0][key] = value
            invalid.append(descriptor)
        for descriptor in invalid:
            with self.subTest(descriptor=descriptor), self.assertRaises(ValueError):
                publisher.publish(descriptor, self.remote)
        self.assertEqual(self.remote.events, [])
        for kind, path in publisher.BASELINE_PATHS.items():
            original = path.read_bytes()
            path.write_bytes(b'{}')
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                publisher.publish(self.descriptor, self.remote)
            path.write_bytes(original)
        self.assertEqual(self.remote.events, [])

    def test_fourth_staging_corruption_cannot_mutate_or_archive_anything(self):
        self.remote.corrupt_staging = list(self.new)[-1]
        with self.assertRaises(ValueError): publisher.publish(self.descriptor, self.remote)
        self.assertEqual(len([e for e in self.remote.events if e[0] == 'staging']), 4)
        self.assertFalse(self.writes())
        self.assert_not_promoted()

    def test_all_eight_r2_objects_are_checked_before_the_first_write(self):
        for name in self.old:
            remote = copy.deepcopy(self.remote)
            remote.r2[publisher.PREFIX + name] = b'unexpected bytes'
            with self.subTest(name=name), self.assertRaises(ValueError):
                publisher.publish(self.descriptor, remote)
            self.assertFalse(self.writes(remote))

    def test_other_platform_asset_ids_digests_labels_and_tag_cannot_drift(self):
        for pin in self.original_assets:
            if pin['name'] in publisher.GITHUB_NAMES.values(): continue
            for field, value in [('id', 123), ('digest', 'sha256:unknown'), ('label', 'changed')]:
                remote = copy.deepcopy(self.remote)
                next(a for a in remote.assets if a['id'] == pin['id'])[field] = value
                with self.subTest(name=pin['name'], field=field), self.assertRaises(ValueError):
                    publisher.publish(self.descriptor, remote)
                self.assertFalse(self.writes(remote))
        self.remote.tag['object']['sha'] = 'b' * 40
        with self.assertRaises(ValueError): publisher.publish(self.descriptor, self.remote)
        self.assertFalse(self.writes())

    def test_unknown_asset_and_same_name_conflict_are_rejected(self):
        self.remote.assets.append(dict(id=999, name='unreviewed.exe', state='uploaded', size=1, digest='x'))
        with self.assertRaises(ValueError): publisher.publish(self.descriptor, self.remote)
        self.assertFalse(self.writes())
        self.remote.assets.pop()
        self.remote.assets[1]['digest'] = 'sha256:unknown'
        with self.assertRaises(ValueError): publisher.publish(self.descriptor, self.remote)
        self.assertFalse(self.writes())

    def test_archive_and_feed_conflicts_fail_closed(self):
        for key in [*publisher.LATEST_KEYS, *publisher.VARIANT_KEYS, publisher.SUMS_KEY, publisher.VARIANT_SUMS_KEY,
                    publisher.HISTORY + 'latest.json', publisher.HISTORY + next(iter(self.new))]:
            remote = copy.deepcopy(self.remote)
            remote.r2[key] = b'unknown conflict'
            with self.subTest(key=key), self.assertRaises(ValueError):
                publisher.publish(self.descriptor, remote)
            self.assertFalse(self.writes(remote))

    def test_interrupted_rename_upload_r2_promotion_and_cleanup_resume_safely(self):
        first_name = next(iter(self.new))
        old_id = next(a['id'] for a in self.original_assets if a['name'] == publisher.GITHUB_NAMES[first_name])
        events = [('put', publisher.HISTORY + first_name), ('rename', publisher.GITHUB_NAMES[first_name]),
                  ('upload', first_name), ('put', publisher.PREFIX + first_name),
                  ('put', publisher.VARIANT_KEYS[0]), ('put', publisher.LATEST_KEYS[0]), ('delete', old_id)]
        for event in events:
            with self.subTest(event=event):
                remote = copy.deepcopy(self.remote)
                remote.fail_after = event
                with self.assertRaises(OSError): publisher.publish(self.descriptor, remote)
                publisher.publish(self.descriptor, remote)
                self.assert_protected_unchanged(remote)
                self.assertEqual(len(remote.assets), 8)
                self.assertFalse(any(a['name'].endswith(publisher.SUFFIX) for a in remote.assets))
                self.assertEqual(remote.r2[publisher.LATEST_KEYS[0]], publisher.encode(publisher.public_manifests(self.descriptor)['latest']))
                if event[0] in {'rename', 'upload', 'put'} and event[1] != publisher.HISTORY + first_name:
                    self.assertEqual(len([e for e in remote.events if e == ('rename', publisher.GITHUB_NAMES[first_name])]), 1)

    def test_zero_byte_new_starter_can_resume_but_nonzero_starter_cannot_be_deleted(self):
        name = next(iter(self.new))
        self.remote.fail_upload = name
        with self.assertRaises(OSError): publisher.publish(self.descriptor, self.remote)
        self.assert_not_promoted()
        self.remote.fail_upload = None
        starter = next(a for a in self.remote.assets if a['name'] == publisher.GITHUB_NAMES[name])
        bad = copy.deepcopy(self.remote)
        next(a for a in bad.assets if a['id'] == starter['id'])['size'] = 1
        bad.events.clear()
        with self.assertRaises(ValueError): publisher.publish(self.descriptor, bad)
        self.assertFalse(self.writes(bad))
        publisher.publish(self.descriptor, self.remote)
        self.assertIn(('delete', starter['id']), self.remote.events)
        self.assert_protected_unchanged()

    def test_new_github_corrupt_readback_does_not_promote_or_delete_old_rows(self):
        self.remote.corrupt_github = publisher.GITHUB_NAMES[next(iter(self.new))]
        with self.assertRaises(ValueError): publisher.publish(self.descriptor, self.remote)
        self.assert_not_promoted()
        self.assertFalse(any(e[0] == 'delete' for e in self.remote.events))
        self.assert_protected_unchanged()

    def test_new_r2_corrupt_readback_does_not_promote_or_delete_old_rows(self):
        key = publisher.PREFIX + next(iter(self.new))
        original = self.remote.put
        def put(name, *args, **kwargs):
            original(name, *args, **kwargs)
            if name == key: self.remote.corrupt_r2 = key
        self.remote.put = put
        with self.assertRaises(ValueError): publisher.publish(self.descriptor, self.remote)
        self.assert_not_promoted()
        self.assertFalse(any(e[0] == 'delete' for e in self.remote.events))

    def test_missing_old_id_before_promotion_is_rejected(self):
        name = publisher.GITHUB_NAMES[next(iter(self.new))]
        self.remote.assets = [a for a in self.remote.assets if a['name'] != name]
        with self.assertRaises(ValueError): publisher.publish(self.descriptor, self.remote)
        self.assertFalse(self.writes())

    def test_temporary_old_id_and_digest_cannot_be_substituted(self):
        name = next(iter(self.new))
        self.remote.fail_after = ('rename', publisher.GITHUB_NAMES[name])
        with self.assertRaises(OSError): publisher.publish(self.descriptor, self.remote)
        for field, value in [('id', 123), ('digest', 'sha256:unknown')]:
            remote = copy.deepcopy(self.remote)
            next(a for a in remote.assets if a['name'].endswith(publisher.SUFFIX))[field] = value
            remote.events.clear()
            with self.subTest(field=field), self.assertRaises(ValueError): publisher.publish(self.descriptor, remote)
            self.assertFalse(self.writes(remote))

    def test_new_canonical_bytes_without_old_r2_archive_are_rejected(self):
        name = next(iter(self.new))
        self.remote.r2[publisher.PREFIX + name] = self.new[name]
        with self.assertRaisesRegex(ValueError, 'historical archive'):
            publisher.publish(self.descriptor, self.remote)
        self.assertFalse(self.writes())

    def test_other_platform_drift_during_upload_is_caught_before_promotion(self):
        original = self.remote.put
        final_key = publisher.PREFIX + list(self.new)[-1]
        protected = next(a for a in self.remote.assets if a['name'].endswith('.apk'))
        def put(key, *args, **kwargs):
            original(key, *args, **kwargs)
            if key == final_key: protected['digest'] = 'sha256:unexpected-drift'
        self.remote.put = put
        with self.assertRaisesRegex(ValueError, 'Protected Windows/Android'):
            publisher.publish(self.descriptor, self.remote)
        self.assert_not_promoted()
        self.assertFalse(any(e[0] == 'delete' for e in self.remote.events))

    def test_failed_feed_readback_retains_old_rows_and_can_resume(self):
        original = self.remote.put
        key = publisher.VARIANT_KEYS[0]
        def put(name, *args, **kwargs):
            original(name, *args, **kwargs)
            if name == key: self.remote.corrupt_r2 = key
        self.remote.put = put
        with self.assertRaisesRegex(ValueError, 'readback'):
            publisher.publish(self.descriptor, self.remote)
        self.assertFalse(any(e[0] == 'delete' for e in self.remote.events))
        self.assertEqual(len([a for a in self.remote.assets if a['name'].endswith(publisher.SUFFIX)]), 4)
        self.remote.corrupt_r2 = None
        self.remote.put = original
        publisher.publish(self.descriptor, self.remote)
        self.assertEqual(len(self.remote.assets), 8)
        self.assert_protected_unchanged()

    def test_real_transport_reuses_reviewed_streaming_and_conditions_only_reviewed_keys(self):
        self.assertIs(publisher.Remote.readback_asset, publisher._shared.Remote.readback_asset)
        self.assertIs(publisher.Remote.readback_object, publisher._shared.Remote.readback_object)
        self.assertIs(publisher.download_staging, publisher._shared.download_staging)
        remote = publisher.Remote.__new__(publisher.Remote)
        calls = []
        remote.s3 = type('S3', (), {'put_object': lambda self, **kwargs: calls.append(kwargs)})()
        for name in self.old:
            if name not in self.new:
                with self.assertRaises(ValueError): remote.put(publisher.PREFIX + name, b'bad', 'application/octet-stream')
        key = publisher.PREFIX + next(iter(self.new))
        with self.assertRaises(ValueError): remote.put(key, b'not reviewed', 'application/octet-stream')
        remote._etags = {key: '"reviewed-etag"'}
        remote.put(key, b'reviewed replacement', 'application/octet-stream')
        self.assertEqual(calls[-1]['IfMatch'], '"reviewed-etag"')
        remote.put(publisher.HISTORY + next(iter(self.new)), b'old archived bytes', 'application/octet-stream')
        self.assertEqual(calls[-1]['IfNoneMatch'], '*')
        remote.object = lambda key: io.BytesIO(b'transport-payload')
        target = Path(self.temporary.name) / 'transport'
        remote.readback_object('readback', target)
        self.assertEqual(target.read_bytes(), b'transport-payload')

    def test_real_upload_cannot_target_windows_or_sanitized_bundled_aliases(self):
        remote = publisher.Remote.__new__(publisher.Remote)
        remote.headers = {}
        calls = []
        response = type('Response', (), {'raise_for_status': lambda self: None})()
        remote.requests = type('Requests', (), {'post': lambda self, *args, **kwargs: calls.append(kwargs) or response})()
        path = Path(self.temporary.name) / 'installer'
        path.write_bytes(b'new Mac')
        for name, github in publisher.GITHUB_NAMES.items():
            remote.upload(path, name)
            self.assertEqual(calls[-1]['params'], {'name': github, 'label': name})
            self.assertTrue(github.isascii())
        with self.assertRaises(ValueError): remote.upload(path, 'AgPlayer-Setup-1.1.2-x64.exe')
        with self.assertRaises(ValueError): remote.upload(path, publisher.GITHUB_NAMES[publisher.SPECS['macos-models'][0]])
        self.assertEqual(len(calls), 4)

    def test_workflow_serializes_writes_and_tests_before_publishing(self):
        text = (Path(__file__).parents[1] / '.github/workflows/republish-macos-demucs-1.1.2.yml').read_text()
        self.assertIn('group: r2-release-latest', text)
        self.assertIn('cancel-in-progress: false', text)
        self.assertIn('docs/releases/1.1.2-macos-demucs-20261029.json', text)
        self.assertLess(text.index('test_republish_macos_demucs_112.py'), text.index('python scripts/republish-macos-demucs-1.1.2.py'))
        self.assertIn('set -o pipefail', text)


if __name__ == '__main__':
    unittest.main()
