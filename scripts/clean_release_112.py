"""One-time, user-authorized deletion of four obsolete GitHub assets; backups retained."""
import json
import os
from pathlib import Path
import urllib.request

API = 'https://api.github.com/repos/gddjag/AgPlayer'
CONFIG = json.loads((Path(__file__).resolve().parent.parent / 'docs/releases/1.1.2-remove-obsolete-assets-20261029.json').read_text())
FIELDS = ('id', 'name', 'size', 'digest', 'state')

def api(path, method='GET'):
    request = urllib.request.Request(API + path, method=method, headers={
        'Authorization': 'Bearer ' + os.environ['GH_TOKEN'],
        'Accept': 'application/vnd.github+json', 'X-GitHub-Api-Version': '2022-11-28'})
    with urllib.request.urlopen(request, timeout=60) as response:
        return None if response.status == 204 else json.load(response)

def validate(release):
    if (release['id'], release['tag_name'], release['target_commitish']) != (CONFIG['releaseId'], CONFIG['tag'], CONFIG['target']):
        raise ValueError('Release provenance changed')
    expected = {a['id']: a for a in CONFIG['assets']}
    actual = {a['id']: a for a in release['assets']}
    if len(actual) != len(release['assets']) or actual.keys() - expected.keys():
        raise ValueError('Unknown or duplicate asset')
    if {a['id'] for a in CONFIG['assets'] if not a['remove']} - actual.keys():
        raise ValueError('Missing current installer')
    for asset_id, asset in actual.items():
        if any(asset.get(k) != expected[asset_id][k] for k in FIELDS):
            raise ValueError('Asset metadata changed: ' + str(asset_id))
    return actual

def clean_release(request=api):
    path = '/releases/' + str(CONFIG['releaseId'])
    validate(request(path))
    deleted = []
    for asset in CONFIG['assets']:
        if not asset['remove']:
            continue
        # Revalidate all surviving packages before each narrowly pinned deletion.
        actual = validate(request(path))
        if asset['id'] in actual:
            request('/releases/assets/' + str(asset['id']), method='DELETE')
            deleted.append(asset['id'])
    final = validate(request(path))
    if any(a['id'] in final for a in CONFIG['assets'] if a['remove']):
        raise ValueError('Obsolete asset still present')
    return dict(deletedIds=deleted, retainedCount=len(final), retainedAssets=list(final.values()), originalTagPreserved=True)

if __name__ == '__main__':
    receipt = clean_release()
    Path('obsolete-assets-cleanup-receipt.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(dict(deletedIds=receipt['deletedIds'], retainedCount=receipt['retainedCount'])))
