import hashlib
import json
import os
import subprocess
import tempfile
from pathlib import Path
from urllib.parse import unquote, urlsplit

repo = 'gddjag/AgPlayer'
tag = 'v1.1.6'
bucket = 'agplayer-releases'
endpoint = 'https://50fb972f4425789ab744ba4c6614221e.r2.cloudflarestorage.com'
meta = json.loads(Path('docs/releases/v1.1.6-wave-revision.json').read_text())
assert os.environ['GITHUB_REPOSITORY'] == repo and meta['tag'] == tag
revision = '20261124'
assert meta['revision'] == revision and len(meta['replacements']) == 7

def gh(*args):
    return subprocess.check_output(['gh', 'api', *args])

def aws(*args):
    return subprocess.check_output(['aws', '--endpoint-url', endpoint, *args])

def canonical_url(key):
    return f's3://{bucket}/{key}'

def download(key, path):
    aws('s3', 'cp', canonical_url(key), str(path), '--only-show-errors')

def upload(key, path, mime):
    aws('s3', 'cp', str(path), canonical_url(key), '--only-show-errors',
        '--content-type', mime, '--cache-control', 'no-cache')

def check(path, entry):
    with path.open('rb') as file:
        assert hashlib.file_digest(file, 'sha256').hexdigest() == entry['sha256'], path
    assert path.stat().st_size == entry['size'], path

def asset_bytes(asset, path):
    with path.open('wb') as stream:
        subprocess.run(['gh', 'api', '-H', 'Accept: application/octet-stream',
            f'/repos/{repo}/releases/assets/{asset["id"]}'], stdout=stream, check=True)

def rename(asset, name, label):
    result = json.loads(gh('-X', 'PATCH', f'/repos/{repo}/releases/assets/{asset["id"]}',
                           '-f', f'name={name}', '-f', f'label={label}'))
    assert result['name'] == name and result['label'] == label

def read_json(key):
    return json.loads(aws('s3', 'cp', canonical_url(key), '-', '--only-show-errors'))

def publish_json(key, payload, path):
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + '\n')
    upload(key, path, 'application/json; charset=utf-8')
    assert read_json(key) == payload

def mime_for(name):
    return 'application/x-apple-diskimage' if name.endswith('.dmg') else 'application/octet-stream'

release = json.loads(gh(f'repos/{repo}/releases/tags/{tag}'))
assert release['id'] == meta['releaseId'] and not release['draft']
reference = json.loads(gh(f'repos/{repo}/git/ref/tags/{tag}'))
assert reference['object']['sha'] == meta['tagObject']
assets = {item['name']: item for item in release['assets']}
manifest_keys = {'updates/latest.json': 'latest', 'updates/macos/latest.json': 'latest',
                 'updates/desktop-variants.json': 'variants'}
for k, id in manifest_keys.items():
    current = read_json(k)
    assert current in (meta['old'][id], meta['new'][id]), k

with tempfile.TemporaryDirectory() as temp:
    root = Path(temp)
    validation = root / 'readback'
    for idx, record in enumerate(meta['replacements']):
        old, new = record['old'], record['new']
        name, alias, staging = new['name'], record['alias'], record['staging']
        assert '/' not in name and alias == unquote(urlsplit(new['githubUrl']).path.split('/')[-1])
        assert staging == f'{alias}.wave-{revision}'
        archive_name = f'{alias}.before-wave-{revision}'
        current = assets.get(alias)
        previous = assets.get(archive_name) or current
        fresh = assets.get(staging) or current
        for asset, entry in ((previous, old), (fresh, new)):
            assert asset and asset['digest'] == 'sha256:'+entry['sha256'] and asset['size'] == entry['size'], name
        local = root / str(idx)
        asset_bytes(fresh, local)
        check(local, new)
        history = f'releases/{tag}/history/before-wave-{revision}/{name}'
        canonical = f'releases/{tag}/{name}'
        try:
            download(history, validation)
        except subprocess.CalledProcessError:
            download(canonical, validation)
            check(validation, old)
            upload(history, validation, mime_for(name))
            download(history, validation)
        check(validation, old)
        download(canonical, validation)
        with validation.open('rb') as file:
            old_or_new = hashlib.file_digest(file, 'sha256').hexdigest()
        assert old_or_new in (old['sha256'], new['sha256']), canonical
        prepared = f'releases/{tag}/revisions/wave-{revision}/{name}'
        upload(prepared, local, mime_for(name))
        download(prepared, validation)
        check(validation, new)
        print(f'Prepared and verified {name}', flush=True)

    sums = root / 'SHA256SUMS.txt'
    sums.write_text(meta['sums'])
    fresh_sums = assets.get(f'SHA256SUMS.txt.wave-{revision}') or assets['SHA256SUMS.txt']
    asset_bytes(fresh_sums, validation)
    assert validation.read_bytes() == sums.read_bytes()
    sums_key = f'releases/{tag}/SHA256SUMS'
    sums_history = f'releases/{tag}/history/before-wave-{revision}/SHA256SUMS'
    try:
        download(sums_history, validation)
    except subprocess.CalledProcessError:
        download(sums_key, validation)
        assert validation.read_text() == meta['oldR2Sums']
        upload(sums_history, validation, 'text/plain; charset=utf-8')
    assert validation.read_text() == meta['oldR2Sums']

    for idx, record in enumerate(meta['replacements']):
        new = record['new']
        alias = record['alias']
        current = assets.get(alias)
        history = f'{alias}.before-wave-{revision}'
        local = root / str(idx)
        if history not in assets:
            rename(current, history, f'Previous 1.1.6 - {new["name"]}')
        if record['staging'] in assets:
            rename(assets[record['staging']], alias, '' if alias == new['name'] else new['name'])
        canonical = f'releases/{tag}/{new["name"]}'
        upload(canonical, local, mime_for(new['name']))
        download(canonical, validation)
        check(validation, new)
        print(f'Promoted and verified {new["name"]}', flush=True)

    old_sums = f'SHA256SUMS.txt.before-wave-{revision}'
    if old_sums not in assets:
        rename(assets['SHA256SUMS.txt'], old_sums, 'Previous 1.1.6 checksums')
    if f'SHA256SUMS.txt.wave-{revision}' in assets:
        rename(fresh_sums, 'SHA256SUMS.txt', '')
    r2_sums = root / 'R2-SHA256SUMS.txt'
    r2_sums.write_text(meta['newR2Sums'])
    upload(sums_key, r2_sums, 'text/plain; charset=utf-8')
    download(sums_key, validation)
    assert validation.read_bytes() == r2_sums.read_bytes()
    for key in (f'releases/{tag}/desktop-variants.json', *manifest_keys):
        manifest = manifest_keys.get(key, 'variants')
        publish_json(key, meta['new'][manifest], validation)
    print('AgPlayer 1.1.6 wave revision manifests verified', flush=True)
