"""Offline release-state regressions; no GitHub/R2 requests or credentials."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from urllib.parse import quote

HERE = Path(__file__).parent
spec = importlib.util.spec_from_file_location('website_updates_publisher', HERE / ('republish-website-updates-1.1.2.py' if (HERE / 'republish-website-updates-1.1.2.py').exists() else 'republish-macos-demucs-1.1.2.py'))
publisher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publisher)
fixture_spec = importlib.util.spec_from_file_location('website_updates_fake_transport', HERE / 'test_republish_macos_demucs_112.py')
fixture = importlib.util.module_from_spec(fixture_spec)
fixture_spec.loader.exec_module(fixture)
fixture.publisher = publisher
FakeRemote, digest = fixture.FakeRemote, fixture.digest
SPECS = {
    'windows-x64': ('AgPlayer-Setup-1.1.2-x64.exe', ['x64'], False),
    'windows-arm64': ('AgPlayer-Setup-1.1.2-arm64.exe', ['arm64'], False),
    'windows-models': ('AgPlayer-Setup-1.1.2-x64-arm64（内置伴奏分离模型版本）.exe', ['x64', 'arm64'], True),
    'macos-arm64': ('AgPlayer-1.1.2-macOS-arm64.dmg', ['arm64'], False),
    'macos-x86_64': ('AgPlayer-1.1.2-macOS-x86_64.dmg', ['x86_64'], False),
    'macos-models': ('AgPlayer-1.1.2-macOS-universal（内置伴奏分离模型版本）.dmg', ['arm64', 'x86_64'], True),
    'macos-universal': ('AgPlayer-1.1.2-macOS-universal.dmg', ['arm64', 'x86_64'], False),
    'android-arm64': ('AgPlayer-1.1.2-arm64-release.apk', ['arm64'], True),
}
NAMES = {name: name.replace('（内置伴奏分离模型版本）', '-bundled-models') for name, _, _ in SPECS.values()}


class WebsiteUpdatesPublisherTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.raw = {kind: (HERE.parent / f'docs/releases/1.1.2-before-website-updates-{kind}-20261030.json').read_bytes()
                    for kind in ('assets', 'latest', 'variants')}
        self.baselines = {kind: json.loads(raw) for kind, raw in self.raw.items()}
        self.old = {name: ('old:' + name).encode() for name in NAMES}
        self.new = {name: ('new:' + name).encode() for name in NAMES}
        for kind in ('latest', 'variants'):
            for file in self.baselines[kind]['files']:
                file.update(size=len(self.old[file['name']]), sha256=digest(self.old[file['name']]))
        for asset in self.baselines['assets']:
            name = next(name for name in NAMES if NAMES[name] == asset['name'])
            asset.update(size=len(self.old[name]), digest='sha256:' + digest(self.old[name]))
        self.raw = {kind: publisher.encode(value) for kind, value in self.baselines.items()}
        paths = {}
        for kind, raw in self.raw.items():
            paths[kind] = Path(self.temp.name) / (kind + '.json'); paths[kind].write_bytes(raw)
        for key, value in [('SOURCE_COMMIT', '1' * 40), ('BASELINE_PATHS', paths), ('BASELINE_SHA256', {k: digest(v) for k, v in self.raw.items()})]:
            helper = patch.object(publisher, key, value); helper.start(); self.addCleanup(helper.stop)
        self.descriptor = dict(version='1.1.2', revision='20261030', sourceCommit=publisher.SOURCE_COMMIT,
            files=[dict(id=id, name=name, architectures=arch, bundledModels=models, size=len(self.new[name]),
                        sha256=digest(self.new[name]), sourceCommit=publisher.SOURCE_COMMIT,
                        stagingUrl='https://verified-website-release.trycloudflare.com/' + quote(name, safe=''))
                   for id, (name, arch, models) in SPECS.items()])
        adapted = copy.deepcopy(self.baselines)
        adapted['assets'] = dict(assets=adapted['assets'])
        self.remote = FakeRemote(adapted, self.raw, self.old, self.new)
        # The fake only supplies transport; storage/tag behavior remains independently asserted.
        self.remote.assets = [{k: a[k] for k in ('id', 'name', 'label', 'state', 'size', 'digest')} for a in self.remote.assets]
        self.original_assets = copy.deepcopy(self.remote.assets)
        self.initial_metadata = {key: self.remote.r2[key] for key in (*publisher.LATEST_KEYS, *publisher.VARIANT_KEYS, publisher.SUMS_KEY, publisher.VARIANT_SUMS_KEY)}

    def mutations(self, remote=None):
        return [e for e in (remote or self.remote).events if e[0] in ('put', 'rename', 'upload', 'delete')]

    def assert_final(self, remote):
        self.assertEqual(len(remote.assets), 8)
        self.assertEqual({a['name'] for a in remote.assets}, set(NAMES.values()))
        for name, raw in self.new.items():
            self.assertEqual(remote.r2['releases/v1.1.2/' + name], raw)
            self.assertEqual(remote.r2['releases/v1.1.2/history/before-website-updates-20261030/' + name], self.old[name])
            asset = next(a for a in remote.assets if a['name'] == NAMES[name])
            self.assertEqual(remote.bytes_by_id[asset['id']], raw)
            self.assertEqual(asset['label'], name)
        self.assertEqual(remote.tag['object']['sha'], '24df8985e064cda64feee3c78e0754a2492e0617')
        self.assertEqual(remote.release['body'], 'Original release notes')
        self.assertEqual(remote.r2['unrelated/history-evidence'], b'preserve this history')

    def test_replaces_all_eight_only_after_complete_preflight_and_promotes_six_feeds(self):
        receipt = publisher.publish(self.descriptor, self.remote)
        self.assertTrue(receipt['published']); self.assert_final(self.remote)
        first = next(i for i, e in enumerate(self.remote.events) if e in self.mutations())
        for name in self.new:
            self.assertLess(self.remote.events.index(('staging', name)), first)
        promotion = self.remote.events.index(('put', publisher.SUMS_KEY))
        for name in self.new:
            self.assertLess(self.remote.events.index(('github-readback', NAMES[name])), promotion)
            self.assertLess(self.remote.events.index(('put', publisher.PREFIX + name)), promotion)
        for state in self.remote.metadata_at_delete:
            self.assertEqual(state, {k: self.remote.r2[k] for k in self.initial_metadata})
        self.assertEqual(set(receipt['removedTemporaryOldAssetIds']), {a['id'] for a in self.original_assets})
        self.assertEqual(self.remote.r2[publisher.HISTORY + 'asset-pins.json'], self.raw['assets'])

    def test_manifest_updates_every_record_android_has_no_query_and_desktops_have_revision(self):
        self.baselines['latest']['files'][0]['revision'] = '19990101'
        self.raw['latest'] = publisher.encode(self.baselines['latest'])
        publisher.BASELINE_PATHS['latest'].write_bytes(self.raw['latest'])
        publisher.BASELINE_SHA256['latest'] = digest(self.raw['latest'])
        try:
            manifests = publisher.public_manifests(self.descriptor)
        except ValueError as error:
            self.fail('Valid eight-platform descriptor must be accepted: ' + str(error))
        self.assertEqual([len(manifests[x]['files']) for x in ('latest', 'variants')], [4, 4])
        for kind, manifest in manifests.items():
            for file in manifest['files']:
                suffix = '' if file['name'].endswith('.apk') else '?revision=20261030'
                self.assertEqual(file['size'], len(self.new[file['name']]))
                self.assertEqual(file['sha256'], digest(self.new[file['name']]))
                self.assertEqual(file['sourceCommit'], '1' * 40)
                if file['name'].endswith('.apk'):
                    self.assertNotIn('revision', file)
                else:
                    self.assertEqual(file['revision'], '20261030')
                self.assertEqual(file['r2Url'], 'https://download.agplayer.com/releases/v1.1.2/' + quote(file['name'], safe='') + suffix)
                self.assertEqual(file['githubUrl'], 'https://github.com/gddjag/AgPlayer/releases/download/v1.1.2/' + NAMES[file['name']] + suffix)
            self.assertEqual({k: v for k, v in manifest.items() if k != 'files'}, {k: v for k, v in self.baselines[kind].items() if k != 'files'})

    def test_every_staging_or_old_r2_hash_failure_prevents_all_mutation(self):
        for name in self.new:
            for kind in ('staging', 'r2'):
                remote = copy.deepcopy(self.remote)
                if kind == 'staging': remote.corrupt_staging = name
                else: remote.r2[publisher.PREFIX + name] = b'unknown bytes'
                with self.subTest(name=name, kind=kind), self.assertRaises(ValueError): publisher.publish(self.descriptor, remote)
                self.assertFalse(self.mutations(remote))

    def test_every_mutation_can_resume_and_completed_retry_needs_no_live_tunnel(self):
        complete = copy.deepcopy(self.remote); publisher.publish(self.descriptor, complete)
        for event in self.mutations(complete):
            remote = copy.deepcopy(self.remote); remote.fail_after = event
            with self.subTest(event=event):
                with self.assertRaises(OSError): publisher.publish(self.descriptor, remote)
                publisher.publish(self.descriptor, remote); self.assert_final(remote)
        previous = copy.deepcopy((complete.r2, complete.assets)); complete.events.clear(); complete.expired = True
        publisher.publish(self.descriptor, complete)
        self.assertEqual((complete.r2, complete.assets), previous); self.assertFalse(self.mutations(complete))
        self.assertFalse(any(e[0] == 'staging' for e in complete.events))

    def test_corrupt_new_mirror_never_promotes_or_deletes_pinned_old_rows(self):
        for target in ('github', 'r2'):
            remote = copy.deepcopy(self.remote); name = next(iter(self.new))
            if target == 'github': remote.corrupt_github = NAMES[name]
            else:
                original = remote.put
                def put(key, *args, **kwargs):
                    original(key, *args, **kwargs)
                    if key == publisher.PREFIX + name: remote.corrupt_r2 = key
                remote.put = put
            with self.assertRaises(ValueError): publisher.publish(self.descriptor, remote)
            for key, raw in self.initial_metadata.items(): self.assertEqual(remote.r2[key], raw)
            self.assertFalse(any(e[0] == 'delete' for e in remote.events))

    def test_unknown_assets_tag_temporary_pin_and_history_or_feed_conflicts_fail_closed(self):
        for failure in ('tag', 'unknown', 'digest', 'id', 'history', 'feed'):
            remote = copy.deepcopy(self.remote)
            if failure == 'tag': remote.tag['object']['sha'] = 'a' * 40
            elif failure == 'unknown': remote.assets.append(dict(id=999, name='store.msixupload'))
            elif failure in ('digest', 'id'): remote.assets[0][failure] = 'unknown'
            elif failure == 'history': remote.r2[publisher.HISTORY + next(iter(self.old))] = b'unknown history'
            else: remote.r2[publisher.LATEST_KEYS[0]] = b'unknown feed'
            with self.subTest(failure=failure), self.assertRaises(ValueError): publisher.publish(self.descriptor, remote)
            self.assertFalse(self.mutations(remote))

    def test_placeholder_descriptor_or_bad_identity_fails_before_remote_access(self):
        invalid=[]
        for field,value in [('revision','20261031'),('sourceCommit','0'*40),('files',self.descriptor['files'][:7])]: invalid.append(self.descriptor | {field:value})
        for field,value in [('bundledModels',False),('architectures',['x64']),('sha256','F'*64),('size',True),('stagingUrl','https://evil.example/test')]:
            descriptor=copy.deepcopy(self.descriptor);descriptor['files'][-1][field]=value;invalid.append(descriptor)
        for descriptor in invalid:
            with self.assertRaises(ValueError): publisher.publish(descriptor,self.remote)
        self.assertFalse(self.remote.events)

    def test_zero_byte_starter_resumes_but_nonzero_starter_and_substituted_history_id_do_not(self):
        name=next(iter(self.new));self.remote.fail_upload=name
        with self.assertRaises(OSError):publisher.publish(self.descriptor,self.remote)
        self.remote.fail_upload=None
        starter=next(a for a in self.remote.assets if a['name']==NAMES[name])
        bad=copy.deepcopy(self.remote);next(a for a in bad.assets if a['id']==starter['id'])['size']=1;bad.events.clear()
        with self.assertRaises(ValueError):publisher.publish(self.descriptor,bad)
        self.assertFalse(self.mutations(bad))
        bad=copy.deepcopy(self.remote);next(a for a in bad.assets if a['name'].endswith(publisher.SUFFIX))['id']=777;bad.events.clear()
        with self.assertRaises(ValueError):publisher.publish(self.descriptor,bad)
        self.assertFalse(self.mutations(bad))
        publisher.publish(self.descriptor,self.remote);self.assert_final(self.remote)

    def test_failed_feed_readback_keeps_all_old_ids_and_resumes_after_tunnel_expiry(self):
        for key in self.initial_metadata:
            remote=copy.deepcopy(self.remote);original=remote.put
            def put(name,*args,**kwargs):
                original(name,*args,**kwargs)
                if name==key:remote.corrupt_r2=key
            remote.put=put
            with self.subTest(key=key):
                with self.assertRaisesRegex(ValueError,'readback'):publisher.publish(self.descriptor,remote)
                self.assertEqual({a['id'] for a in remote.assets if a['name'].endswith(publisher.SUFFIX)}, {a['id'] for a in self.original_assets})
                self.assertFalse(any(e[0]=='delete' for e in remote.events))
                remote.put=original;remote.corrupt_r2=None;remote.expired=True
                publisher.publish(self.descriptor,remote);self.assert_final(remote)

    def test_actual_transport_enforces_etag_scope_native_names_and_content_types(self):
        remote=publisher.Remote.__new__(publisher.Remote);calls=[]
        remote.s3=type('S3',(),{'put_object':lambda self,**kwargs:calls.append(kwargs)})()
        remote._etags={publisher.PREFIX+name:'"reviewed"' for name in self.new}
        for name in self.new:
            key=publisher.PREFIX+name
            remote.put(key,b'reviewed bytes','wrong/type')
            self.assertEqual(calls[-1]['IfMatch'],'"reviewed"')
            self.assertEqual(calls[-1]['ContentType'], 'application/vnd.android.package-archive' if name.endswith('.apk') else 'application/x-apple-diskimage' if name.endswith('.dmg') else 'application/octet-stream')
            remote.put(publisher.HISTORY+name,b'archive','application/octet-stream')
            self.assertEqual(calls[-1]['IfNoneMatch'],'*')
        for key in ['releases/v1.1.2/store.msixupload','updates/harmony/latest.json',publisher.PREFIX+'unreviewed.exe']:
            with self.assertRaises(ValueError):remote.put(key,b'forbidden','application/octet-stream')
        remote._etags={}
        with self.assertRaises(ValueError):remote.put(publisher.PREFIX+next(iter(self.new)),b'not reviewed','application/octet-stream')
        remote.headers={};uploads=[]
        response=type('Response',(),{'raise_for_status':lambda self:None})()
        remote.requests=type('Requests',(),{'post':lambda self,*args,**kwargs:uploads.append(kwargs) or response})()
        path=Path(self.temp.name)/'installer';path.write_bytes(b'payload')
        for name,github in NAMES.items():
            remote.upload(path,name)
            self.assertEqual(uploads[-1]['params'],{'name':github,'label':name})
        for name in ['store.msixupload','harmony.hap',NAMES[SPECS['windows-models'][0]]]:
            with self.assertRaises(ValueError):remote.upload(path,name)

    def test_tampered_baseline_is_rejected_before_remote_access(self):
        for path in publisher.BASELINE_PATHS.values():
            raw=path.read_bytes();path.write_bytes(b'{}')
            with self.assertRaises(ValueError):publisher.publish(self.descriptor,self.remote)
            path.write_bytes(raw)
        self.assertFalse(self.remote.events)


if __name__=='__main__':unittest.main()
