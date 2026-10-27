import importlib.util
import unittest
import tempfile
import hashlib
from pathlib import Path

spec = importlib.util.spec_from_file_location('publish', Path(__file__).with_name('republish-macos-1.1.2.py'))
publish = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publish)

class RepublishTests(unittest.TestCase):
    def descriptor(self):
        return dict(version='1.1.2', name=publish.MAC, size=100, sha256='a'*64,
                    sourceCommit='b'*40, stagingUrl='https://verified-test.trycloudflare.com/'+publish.MAC)

    def test_descriptor_restricts_version_and_staging(self):
        self.assertEqual(publish.validate_descriptor(self.descriptor())['version'], '1.1.2')
        for changes in ({'version':'1.1.3'}, {'size':0}, {'sha256':'abc'}, {'sourceCommit':'main'},
                        {'stagingUrl':'http://verified-test.trycloudflare.com/'+publish.MAC},
                        {'stagingUrl':'https://evil.example/'+publish.MAC},
                        {'stagingUrl':'https://verified-test.trycloudflare.com/'+publish.MAC+'?x=1'}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                publish.validate_descriptor(self.descriptor() | changes)

    def test_preserves_other_platforms_and_revision_is_explicit(self):
        baseline = publish.load_baseline()
        latest = publish.updated_manifest(baseline, self.descriptor())
        for before, after in zip(baseline['files'], latest['files']):
            if before['name'] != publish.MAC:
                self.assertEqual(before, after)
            else:
                self.assertEqual(after['sha256'], 'a'*64)
                self.assertEqual(after['revision'], '20261027')
                self.assertTrue(after['r2Url'].endswith('?revision=20261027'))
        self.assertEqual(latest['publishedAt'], baseline['publishedAt'])

    def test_remote_assets_accept_only_old_or_verified_new_and_keep_history(self):
        descriptor = self.descriptor()
        assets = [dict(id=publish.IDS[x['name']],state='uploaded',name=x['name'],size=x['size'],digest='sha256:'+x['sha256'])
                  for x in publish.load_baseline()['files']]
        self.assertEqual(publish.validate_assets(assets, descriptor), 'original')
        archived = [x | {'name':publish.ARCHIVE} if x['name']==publish.MAC else x for x in assets]
        self.assertEqual(publish.validate_assets(archived, descriptor), 'archived')
        new = dict(id=999999,name=publish.MAC,state='uploaded',size=100,digest='sha256:'+'a'*64)
        self.assertEqual(publish.validate_assets(archived+[new], descriptor), 'updated')
        self.assertEqual(publish.validate_assets(archived+[new | {'state':'starter','size':0,'digest':None}], descriptor), 'incomplete')
        for broken in (assets+[new], archived+[new | {'digest':'sha256:'+'c'*64}],
                       [x | {'size':1} if x['name'].endswith('.exe') else x for x in assets], [new]):
            with self.subTest(broken=broken), self.assertRaises(ValueError):
                publish.validate_assets(broken, descriptor)

    def test_staging_uses_bounded_ranges_and_verifies_full_digest(self):
        raw = b'installer bytes'
        d = self.descriptor() | {'size':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}
        class Response:
            status_code = 206
            headers = {'Content-Range':f'bytes 0-{len(raw)-1}/{len(raw)}'}
            content = raw
        calls = []
        def fetch(url, **kw):
            calls.append(kw)
            return Response()
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/publish.MAC
            publish.download_staging(d,path,fetch)
            self.assertEqual(path.read_bytes(),raw)
            self.assertEqual(calls[0]['headers']['Range'],f'bytes=0-{len(raw)-1}')
            Response.content = b'x'*len(raw)
            with self.assertRaises(ValueError): publish.download_staging(d,path,fetch)

if __name__ == '__main__': unittest.main()
