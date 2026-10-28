"""Append four verified desktop choices to v1.1.2 without changing app update feeds."""
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile
import time
from urllib.parse import quote, urlparse

VERSION = '1.1.2'
REVISION = '20261028'
SOURCE_COMMIT = 'a351bdb13302e38520d195df5405d40ddac0a23e'
TAG_COMMIT = '24df8985e064cda64feee3c78e0754a2492e0617'
RELEASE_ID = 414882152
API = 'https://api.github.com/repos/gddjag/AgPlayer'
UPLOAD_API = 'https://uploads.github.com/repos/gddjag/AgPlayer'
BUCKET = 'agplayer-releases'
PREFIX = 'releases/v1.1.2/'
ENDPOINTS = ('updates/latest.json', 'updates/macos/latest.json')
VARIANT_ENDPOINTS = (PREFIX + 'desktop-variants.json', 'updates/desktop-variants.json')
SUMS_KEY = PREFIX + 'DESKTOP-VARIANTS-SHA256SUMS'
BASELINE_PATH = Path(__file__).parent.parent / 'docs/releases/1.1.2-before-bundled-models-20261028.json'
BASELINE_SHA256 = '6cf68c6d46a5516b74e84e1d3ed19a0a87db961c7af311662f42b9278aad5be3'
DESCRIPTOR_PATH = Path(__file__).parent.parent / 'docs/releases/1.1.2-desktop-variants-20261028.json'
SPECS = {
    'windows-models': ('AgPlayer-Setup-1.1.2-x64-arm64（内置伴奏分离模型版本）.exe', ['x64', 'arm64'], True),
    'macos-arm64': ('AgPlayer-1.1.2-macOS-arm64.dmg', ['arm64'], False),
    'macos-x86_64': ('AgPlayer-1.1.2-macOS-x86_64.dmg', ['x86_64'], False),
    'macos-models': ('AgPlayer-1.1.2-macOS-universal（内置伴奏分离模型版本）.dmg', ['arm64', 'x86_64'], True),
}
BASE_IDS = {'AgPlayer-1.1.2-arm64-release.apk': 647102603,
            'AgPlayer-1.1.2-macOS-universal.dmg': 684098555,
            'AgPlayer-Setup-1.1.2-arm64.exe': 684099469,
            'AgPlayer-Setup-1.1.2-x64.exe': 684099740}


def encode(value):
    return (json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode()


def load_baseline_bytes():
    raw = BASELINE_PATH.read_bytes()
    if hashlib.sha256(raw).hexdigest() != BASELINE_SHA256:
        raise ValueError('Pinned published baseline changed')
    return raw


def validate_descriptor(descriptor):
    if (not isinstance(descriptor, dict) or set(descriptor) != {'version', 'revision', 'files'}
            or descriptor['version'] != VERSION or descriptor['revision'] != REVISION
            or not isinstance(descriptor['files'], list) or len(descriptor['files']) != 4):
        raise ValueError('Unexpected desktop variants descriptor')
    seen = set()
    for file in descriptor['files']:
        if not isinstance(file, dict) or set(file) != {'id', 'name', 'architectures', 'bundledModels',
                                                       'size', 'sha256', 'sourceCommit', 'stagingUrl'}:
            raise ValueError('Unexpected variant fields')
        spec = SPECS.get(file['id']) if isinstance(file['id'], str) else None
        if (spec is None or file['id'] in seen or file['name'] != spec[0]
                or file['architectures'] != spec[1] or type(file['architectures']) is not list
                or file['bundledModels'] is not spec[2]
                or type(file['size']) is not int or not 0 < file['size'] < 2_000_000_000
                or not isinstance(file['sha256'], str) or not re.fullmatch('[a-f0-9]{64}', file['sha256'])
                or file['sourceCommit'] != SOURCE_COMMIT or not isinstance(file['stagingUrl'], str)):
            raise ValueError('Invalid variant identity, architecture, models, size, checksum or provenance')
        try:
            url = urlparse(file['stagingUrl'])
            valid = (url.scheme == 'https' and url.username is None and url.password is None
                     and url.netloc == url.hostname and not url.port
                     and not url.query and not url.fragment
                     and re.fullmatch('[a-z0-9]+(?:-[a-z0-9]+)+\.trycloudflare\.com', url.hostname or '')
                     and url.path == '/' + quote(file['name'], safe=''))
        except ValueError:
            valid = False
        if not valid:
            raise ValueError('Unexpected encoded staging URL')
        seen.add(file['id'])
    return descriptor


def public_manifest(descriptor):
    validate_descriptor(descriptor)
    files = []
    for file in descriptor['files']:
        public = {key: value for key, value in file.items() if key != 'stagingUrl'}
        encoded = quote(file['name'], safe='')
        public.update(r2Url='https://download.agplayer.com/' + PREFIX + encoded,
                      githubUrl='https://github.com/gddjag/AgPlayer/releases/download/v1.1.2/' + encoded)
        files.append(public)
    return dict(schemaVersion=1, kind='desktop-download-variants', version=VERSION, tag='v1.1.2', files=files)


def verify_file(path, expected):
    digest, size = hashlib.sha256(), 0
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            size += len(block)
            digest.update(block)
    if size != expected['size'] or digest.hexdigest() != expected['sha256']:
        raise ValueError('Installer checksum mismatch: ' + expected['name'])


def download_staging(file, path, fetcher=None, attempts=4):
    if fetcher is None:
        import requests
        fetcher = requests.get
    with path.open('wb') as stream:
        for start in range(0, file['size'], 4 * 1024 * 1024):
            end = min(start + 4 * 1024 * 1024, file['size']) - 1
            for attempt in range(attempts):
                try:
                    response = fetcher(file['stagingUrl'], timeout=(30, 120), allow_redirects=False,
                                       headers={'Range': f'bytes={start}-{end}'})
                    if (response.status_code != 206
                            or response.headers.get('Content-Range') != f"bytes {start}-{end}/{file['size']}"
                            or len(response.content) != end - start + 1):
                        raise ValueError('Invalid staging range')
                    stream.write(response.content)
                    break
                except Exception:
                    if attempt + 1 == attempts:
                        raise
                    time.sleep(2)
    verify_file(path, file)


class Remote:
    def __init__(self):
        import boto3
        import requests
        self.requests = requests
        self.headers = {'Authorization': 'Bearer ' + os.environ['GH_TOKEN'],
                        'Accept': 'application/vnd.github+json', 'X-GitHub-Api-Version': '2022-11-28'}
        self.s3 = boto3.client('s3', endpoint_url='https://50fb972f4425789ab744ba4c6614221e.r2.cloudflarestorage.com', region_name='auto')

    def github(self, path, method='GET', **kwargs):
        response = self.requests.request(method, API + path, headers=self.headers, timeout=120,
                                         allow_redirects=False, **kwargs)
        response.raise_for_status()
        return None if response.status_code == 204 else response.json()

    def upload(self, path, name):
        with path.open('rb') as stream:
            response = self.requests.post(UPLOAD_API + f'/releases/{RELEASE_ID}/assets', params={'name': name},
                headers=self.headers | {'Content-Type': 'application/octet-stream', 'Content-Length': str(path.stat().st_size)},
                data=stream, timeout=(30, 900), allow_redirects=False)
        response.raise_for_status()

    def readback_asset(self, asset, path):
        response = self.requests.get(API + f"/releases/assets/{asset['id']}",
            headers=self.headers | {'Accept': 'application/octet-stream'}, timeout=(30, 900), allow_redirects=False, stream=True)
        if response.status_code in (301, 302, 303, 307, 308):
            url = response.headers.get('Location', '')
            parsed = urlparse(url)
            if (parsed.scheme != 'https' or parsed.hostname not in {'release-assets.githubusercontent.com', 'objects.githubusercontent.com'}
                    or parsed.username or parsed.password or parsed.port not in (None, 443) or parsed.fragment):
                raise ValueError('Unexpected GitHub asset redirect')
            response.close()
            response = self.requests.get(url, timeout=(30, 900), allow_redirects=False, stream=True)
        response.raise_for_status()
        if response.status_code != 200:
            raise ValueError('GitHub readback did not return bytes')
        size = 0
        with response, path.open('wb') as stream:
            for block in response.iter_content(1024 * 1024):
                size += len(block)
                if size > asset['size']:
                    raise ValueError('GitHub readback exceeds verified size')
                stream.write(block)

    def object(self, key):
        from botocore.exceptions import ClientError
        try:
            return self.s3.get_object(Bucket=BUCKET, Key=key)['Body']
        except ClientError as error:
            if error.response['Error']['Code'] in ('NoSuchKey', '404'):
                raise FileNotFoundError(key) from error
            raise

    def get(self, key):
        with self.object(key) as stream:
            return stream.read()

    def readback_object(self, key, path):
        with self.object(key) as stream, path.open('wb') as output:
            for block in stream.iter_chunks(1024 * 1024):
                output.write(block)

    def put(self, key, body, content_type, metadata=None):
        allowed = {PREFIX + spec[0] for spec in SPECS.values()} | set(VARIANT_ENDPOINTS) | {SUMS_KEY}
        if key not in allowed:
            raise ValueError('Forbidden R2 mutation: ' + key)
        # The server rejects racing creations too: no existing R2 key is replaced.
        kwargs = dict(Bucket=BUCKET, Key=key, ContentType=content_type, Metadata=metadata or {},
                      CacheControl='no-cache', IfNoneMatch='*')
        if isinstance(body, Path):
            with body.open('rb') as stream:
                self.s3.put_object(Body=stream, **kwargs)
        else:
            self.s3.put_object(Body=body, **kwargs)

    def staging(self, file, path):
        download_staging(file, path)


def publish(descriptor, remote):
    validate_descriptor(descriptor)
    baseline_raw = load_baseline_bytes()
    baseline = json.loads(baseline_raw)
    base_sums = ''.join(file['sha256'] + '  ' + file['name'] + '\n' for file in baseline['files']).encode()
    manifest_raw = encode(public_manifest(descriptor))
    sums = ''.join(file['sha256'] + '  ' + file['name'] + '\n' for file in descriptor['files']).encode()
    new_files = {file['name']: file for file in descriptor['files']}
    preserved_assets = None

    def snapshot():
        nonlocal preserved_assets
        release = remote.github('/releases/tags/v1.1.2')
        tag = remote.github('/git/ref/tags/v1.1.2')
        if (release['id'] != RELEASE_ID or release['draft'] or release['prerelease']
                or release['target_commitish'] != TAG_COMMIT
                or tag['object'].get('sha') != TAG_COMMIT or tag['object'].get('type') != 'commit'):
            raise ValueError('Published release or tag identity changed')
        assets = remote.github(f'/releases/{RELEASE_ID}/assets?per_page=100')
        if (len(assets) >= 100 or len({a['id'] for a in assets}) != len(assets)
                or len({a['name'] for a in assets}) != len(assets)):
            raise ValueError('Unexpected asset pagination or duplicate identity')
        by_name = {a['name']: a for a in assets}
        for file in baseline['files']:
            asset = by_name.get(file['name'], {})
            if (asset.get('id') != BASE_IDS[file['name']] or asset.get('state') != 'uploaded'
                    or asset.get('size') != file['size'] or asset.get('digest') != 'sha256:' + file['sha256']):
                raise ValueError('Published base asset changed: ' + file['name'])
        history = {a['id']: {key: a.get(key) for key in ('name', 'size', 'state', 'digest')}
                   for a in assets if a['name'] not in new_files}
        if preserved_assets is not None and history != preserved_assets:
            raise ValueError('Historical assets changed during publication')
        preserved_assets = history
        states = {}
        for name, file in new_files.items():
            asset = by_name.get(name)
            if asset is None:
                states[name] = 'missing'
            elif type(asset.get('id')) is not int or asset['id'] <= 0 or asset['id'] in preserved_assets:
                raise ValueError('Invalid new asset identity')
            elif asset.get('state') == 'starter' and asset.get('size') == 0 and asset.get('digest') is None:
                states[name] = 'starter'
            elif (asset.get('state') == 'uploaded' and asset.get('size') == file['size']
                    and asset.get('digest') == 'sha256:' + file['sha256']):
                states[name] = 'uploaded'
            else:
                raise ValueError('Unknown same-name GitHub asset conflict: ' + name)
        return by_name, states

    def optional_get(key):
        try:
            return remote.get(key)
        except FileNotFoundError:
            return None

    def check_metadata():
        if remote.get(PREFIX + 'SHA256SUMS') != base_sums:
            raise ValueError('Canonical checksum list changed')
        for key in ENDPOINTS:
            if remote.get(key) != baseline_raw:
                raise ValueError('Canonical update feed changed: ' + key)
        for key, expected in [(SUMS_KEY, sums), *[(key, manifest_raw) for key in VARIANT_ENDPOINTS]]:
            current = optional_get(key)
            if current is not None and current != expected:
                raise ValueError('Existing variants metadata conflict: ' + key)

    def put_once(key, raw, content_type):
        current = optional_get(key)
        if current is None:
            remote.put(key, raw, content_type)
        elif current != raw:
            raise ValueError('Append-only metadata conflict: ' + key)
        if remote.get(key) != raw:
            raise ValueError('Metadata readback failed: ' + key)

    by_name, states = snapshot()
    check_metadata()
    with tempfile.TemporaryDirectory(prefix='agplayer-desktop-variants-') as directory:
        tmp = Path(directory)
        def verify_bases():
            current, _ = snapshot()
            check_metadata()
            for file in baseline['files']:
                remote.readback_asset(current[file['name']], tmp / 'base-github')
                verify_file(tmp / 'base-github', file)
                remote.readback_object(PREFIX + file['name'], tmp / 'base-r2')
                verify_file(tmp / 'base-r2', file)

        verify_bases()
        # Collect and hash all four new files before the first remote mutation.
        r2_present = set()
        for name, file in new_files.items():
            local = tmp / name
            try:
                remote.readback_object(PREFIX + name, tmp / 'existing-r2')
            except FileNotFoundError:
                pass
            else:
                verify_file(tmp / 'existing-r2', file)
                r2_present.add(name)
            if states[name] == 'uploaded':
                remote.readback_asset(by_name[name], local)
            elif name in r2_present:
                (tmp / 'existing-r2').replace(local)
            else:
                remote.staging(file, local)
            verify_file(local, file)
        snapshot()
        check_metadata()
        for name, file in new_files.items():
            assets, current = snapshot()
            if current[name] == 'starter':
                # Only the four explicitly new names, zero bytes and starter state are deletable.
                remote.github(f"/releases/assets/{assets[name]['id']}", 'DELETE')
                _, current = snapshot()
            if current[name] == 'missing':
                remote.upload(tmp / name, name)
            assets, current = snapshot()
            if current[name] != 'uploaded':
                raise ValueError('Incomplete GitHub variant upload: ' + name)
            remote.readback_asset(assets[name], tmp / 'new-github')
            verify_file(tmp / 'new-github', file)
            if name not in r2_present:
                remote.put(PREFIX + name, tmp / name, 'application/octet-stream',
                           {'sha256': file['sha256'], 'sourcecommit': SOURCE_COMMIT, 'revision': REVISION})
            remote.readback_object(PREFIX + name, tmp / 'new-r2')
            verify_file(tmp / 'new-r2', file)
        # Re-read every base and new asset immediately before exposing any metadata.
        verify_bases()
        assets, states = snapshot()
        for name, file in new_files.items():
            if states[name] != 'uploaded':
                raise ValueError('Incomplete variant set')
            remote.readback_asset(assets[name], tmp / 'final-github')
            verify_file(tmp / 'final-github', file)
            remote.readback_object(PREFIX + name, tmp / 'final-r2')
            verify_file(tmp / 'final-r2', file)
        check_metadata()
        put_once(SUMS_KEY, sums, 'text/plain; charset=utf-8')
        for key in VARIANT_ENDPOINTS:
            put_once(key, manifest_raw, 'application/json; charset=utf-8')
        check_metadata()
        snapshot()
    return dict(published=True, version=VERSION, revision=REVISION, sourceCommit=SOURCE_COMMIT,
                originalTagCommit=TAG_COMMIT, canonicalFeedsUnchanged=True, baseAssetsUnchanged=True,
                allFourGitHubAndR2ReadbacksVerified=True, manifest=json.loads(manifest_raw))


if __name__ == '__main__':
    descriptor = json.loads(DESCRIPTOR_PATH.read_text())
    print(json.dumps(publish(descriptor, Remote()), ensure_ascii=False))
