import json
import os
import subprocess
from pathlib import Path

repo = 'gddjag/AgPlayer'
tag = 'v1.1.6'
revision = '20261124'
bucket = 'agplayer-releases'
endpoint = 'https://50fb972f4425789ab744ba4c6614221e.r2.cloudflarestorage.com'
meta = json.loads(Path('docs/releases/v1.1.6-wave-cleanup.json').read_text())
assert os.environ['GITHUB_REPOSITORY'] == repo and meta['tag'] == tag
assert meta['revision'] == revision and len(meta['replacements']) == 7

def gh(*args):
    return subprocess.check_output(['gh', 'api', *args])

def aws(*args):
    return subprocess.run(['aws', '--endpoint-url', endpoint, *args], capture_output=True)

def object_info(key):
    response = aws('s3api', 'head-object', '--bucket', bucket, '--key', key)
    if response.returncode:
        assert b'(404)' in response.stderr or b'Not Found' in response.stderr, response.stderr
        return None
    return json.loads(response.stdout)

release_path = f'repos/{repo}/releases/tags/{tag}'
release = json.loads(gh(release_path))
assert release['id'] == meta['releaseId'] and not release['draft']
assert json.loads(gh(f'repos/{repo}/git/ref/tags/{tag}'))['object']['sha'] == meta['tagObject']
assets = {item['name']: item for item in release['assets']}

for path, section in [('updates/latest.json','latest'),('updates/macos/latest.json','latest'),
                      ('updates/desktop-variants.json','variants')]:
    result = aws('s3', 'cp', f's3://{bucket}/{path}', '-', '--only-show-errors')
    result.check_returncode()
    assert json.loads(result.stdout) == meta['new'][section]
assert len({record['alias'] for record in meta['replacements']}) == 7
for record in meta['replacements']:
    old, new, alias = record['old'], record['new'], record['alias']
    name = new['name']
    assert name == old['name'] and '/' not in name and '/' not in alias
    assert name.startswith(('AgPlayer-1.1.6-', 'AgPlayer-Setup-1.1.6-'))
    assert name.endswith(('.exe','.dmg'))
    current = assets[alias]
    assert current['digest'] == 'sha256:'+new['sha256'] and current['size'] == new['size']
    assert object_info(f'releases/{tag}/{name}')['ContentLength'] == new['size']
    former = assets.get(f'{alias}.before-wave-{revision}')
    if former:
        assert former['digest'] == 'sha256:'+old['sha256'] and former['size'] == old['size']
    key = f'releases/{tag}/history/before-wave-{revision}/{name}'
    history = object_info(key)
    assert history is None or history['ContentLength'] == old['size']

for record in meta['replacements']:
    alias, name = record['alias'], record['old']['name']
    former = assets.get(f'{alias}.before-wave-{revision}')
    if former:
        gh('-X', 'DELETE', f'repos/{repo}/releases/assets/{former["id"]}')
    key = f'releases/{tag}/history/before-wave-{revision}/{name}'
    if object_info(key) is not None:
        aws('s3api','delete-object','--bucket',bucket,'--key',key).check_returncode()
    assert object_info(key) is None
    print(f'Removed and verified previous R2 package: {name}',flush=True)

names = {item['name'] for item in json.loads(gh(release_path))['assets']}
for record in meta['replacements']:
    alias = record['alias']
    assert alias in names and f'{alias}.before-wave-{revision}' not in names
print('Removed and verified seven previous GitHub packages',flush=True)
