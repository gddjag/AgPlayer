import copy
import unittest
from clean_release_112 import clean_release, CONFIG

class CleanupTests(unittest.TestCase):
    def setUp(self):
        self.release = dict(id=CONFIG['releaseId'], tag_name=CONFIG['tag'], target_commitish=CONFIG['target'], assets=[{k:v for k,v in a.items() if k!='remove'} for a in CONFIG['assets']])
        self.deleted=[]
    def api(self, path, method='GET'):
        if method=='DELETE':
            asset_id=int(path.rsplit('/',1)[-1]); self.deleted.append(asset_id)
            self.release['assets']=[a for a in self.release['assets'] if a['id']!=asset_id]
            return None
        return copy.deepcopy(self.release)
    def test_exact_four_deleted_and_all_current_assets_preserved(self):
        receipt=clean_release(self.api)
        self.assertEqual(self.deleted,[a['id'] for a in CONFIG['assets'] if a['remove']])
        self.assertEqual(receipt['retainedCount'],8)
        self.assertEqual(clean_release(self.api)['deletedIds'],[])
    def test_metadata_drift_or_unknown_asset_blocks_all_deletes(self):
        for field,value in [('name','other.exe'),('size',1),('digest','sha256:bad'),('state','new')]:
            with self.subTest(field=field):
                self.setUp();self.release['assets'][0][field]=value
                with self.assertRaises(ValueError):clean_release(self.api)
                self.assertEqual(self.deleted,[])
        self.setUp();self.release['assets'].append(dict(self.release['assets'][0],id=123))
        with self.assertRaises(ValueError):clean_release(self.api)
        self.assertEqual(self.deleted,[])
    def test_missing_current_or_changed_tag_blocks_deletes(self):
        self.release['assets'].pop(0)
        with self.assertRaises(ValueError):clean_release(self.api)
        self.assertEqual(self.deleted,[])
        self.setUp();self.release['target_commitish']='main'
        with self.assertRaises(ValueError):clean_release(self.api)
        self.assertEqual(self.deleted,[])

if __name__=='__main__':unittest.main()
