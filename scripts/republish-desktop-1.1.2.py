"""Fixed, explicitly authorized desktop 1.1.2 revision; never a generic overwrite switch."""
import copy
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile
import time
from urllib.parse import urlparse

REVISION = '20261028'
SOURCE_COMMIT = '33a93c0052c80c73f934c7e28f5a2b00dd301066'
TAG_COMMIT = '24df8985e064cda64feee3c78e0754a2492e0617'
RELEASE_ID = 414882152
MAC = 'AgPlayer-1.1.2-macOS-universal.dmg'
ANDROID = 'AgPlayer-1.1.2-arm64-release.apk'
DESKTOP = (MAC, 'AgPlayer-Setup-1.1.2-arm64.exe', 'AgPlayer-Setup-1.1.2-x64.exe')
IDS = {ANDROID: 647102603, MAC: 682104549,
       DESKTOP[1]: 670522430, DESKTOP[2]: 647102604}
ARCHIVES = {name: name + '.before-desktop-20261028' for name in DESKTOP}
EARLIER_MAC = dict(id=647102605, name=MAC + '.original-20261016', state='uploaded',
                   size=114609080, digest='sha256:912b2c9659cf0e71ebb586155bee61ad15ac66cbb4688510109b89227a0ff993')
API = 'https://api.github.com/repos/gddjag/AgPlayer'
UPLOAD_API = 'https://uploads.github.com/repos/gddjag/AgPlayer'
BUCKET = 'agplayer-releases'
PREFIX = 'releases/v1.1.2/'
HISTORY = PREFIX + 'history/before-desktop-20261028/'
ENDPOINTS = ('updates/macos/latest.json', 'updates/latest.json')
BASELINE_PATH = Path(__file__).parent.parent / 'docs/releases/1.1.2-before-desktop-20261028.json'
BASELINE_SHA256 = '309eb6dce37b5417febbd25fda2011600435a3f0a2358677d5e3d507a0dfa36e'
DESCRIPTOR_PATH = Path(__file__).parent.parent / 'docs/releases/1.1.2-desktop-republish-20261028.json'


def load_baseline_bytes():
    raw = BASELINE_PATH.read_bytes()
    if hashlib.sha256(raw).hexdigest() != BASELINE_SHA256:
        raise ValueError('Pinned published baseline changed')
    return raw


def load_baseline():
    return json.loads(load_baseline_bytes())


def validate_descriptor(descriptor):
    if (not isinstance(descriptor, dict) or set(descriptor) != {'version', 'revision', 'files'}
            or descriptor['version'] != '1.1.2' or descriptor['revision'] != REVISION
            or not isinstance(descriptor['files'], list) or len(descriptor['files']) != 3):
        raise ValueError('Unexpected desktop descriptor')
    names = []
    for f in descriptor['files']:
        if not isinstance(f, dict) or set(f) != {'name', 'size', 'sha256', 'sourceCommit', 'stagingUrl'}:
            raise ValueError('Unexpected installer descriptor fields')
        if (f['name'] not in DESKTOP or type(f['size']) is not int or not 0 < f['size'] < 2_000_000_000
                or not isinstance(f['sha256'], str) or not re.fullmatch('[a-f0-9]{64}', f['sha256'])
                or f['sourceCommit'] != SOURCE_COMMIT or not isinstance(f['stagingUrl'], str)):
            raise ValueError('Invalid installer identity, size, checksum or source commit')
        try:
            u = urlparse(f['stagingUrl'])
            valid = (u.scheme == 'https' and not u.username and not u.password and not u.port
                     and not u.query and not u.fragment
                     and re.fullmatch('[a-z0-9-]+\.trycloudflare\.com', u.hostname or '')
                     and u.path == '/' + f['name'])
        except ValueError:
            valid = False
        if not valid:
            raise ValueError('Unexpected installer staging URL')
        names.append(f['name'])
    if set(names) != set(DESKTOP):
        raise ValueError('All three distinct desktop installers are required')
    return descriptor


def updated_manifest(baseline, descriptor):
    latest = copy.deepcopy(baseline)
    replacements = {f['name']: f for f in validate_descriptor(descriptor)['files']}
    for f in latest['files']:
        if f['name'] in replacements:
            d = replacements[f['name']]
            f.update(size=d['size'], sha256=d['sha256'], revision=REVISION)
            f['githubUrl'] = f['githubUrl'].split('?')[0] + '?revision=' + REVISION
            f['r2Url'] = f['r2Url'].split('?')[0] + '?revision=' + REVISION
    return latest


def verify_bytes(raw, expected):
    if len(raw) != expected['size'] or hashlib.sha256(raw).hexdigest() != expected['sha256']:
        raise ValueError('Installer checksum mismatch: ' + expected['name'])


def verify_file(path, expected):
    digest = hashlib.sha256()
    size = 0
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            size += len(block)
            digest.update(block)
    if size != expected['size'] or digest.hexdigest() != expected['sha256']:
        raise ValueError('Installer file checksum mismatch: ' + expected['name'])


def download_staging(descriptor, path, fetcher=None, attempts=4):
    if fetcher is None:
        import requests
        fetcher = requests.get
    with path.open('wb') as stream:
        for start in range(0, descriptor['size'], 4 * 1024 * 1024):
            end = min(start + 4 * 1024 * 1024, descriptor['size']) - 1
            for attempt in range(attempts):
                try:
                    response = fetcher(descriptor['stagingUrl'], timeout=(30, 120), allow_redirects=False,
                                       headers={'Range': f'bytes={start}-{end}'})
                    if (response.status_code != 206
                            or response.headers.get('Content-Range') != f"bytes {start}-{end}/{descriptor['size']}"
                            or len(response.content) != end - start + 1):
                        raise ValueError('Invalid or incomplete staging range')
                    stream.write(response.content)
                    break
                except Exception:
                    if attempt + 1 == attempts:
                        raise
                    time.sleep(2)
    verify_file(path, descriptor)


def validate_assets(assets, descriptor, baseline=None):
    baseline = load_baseline() if baseline is None else baseline
    replacements = {f['name']: f for f in validate_descriptor(descriptor)['files']}
    originals = {f['name']: f for f in baseline['files']}
    if len(assets) >= 100 or len({a['id'] for a in assets}) != len(assets):
        raise ValueError('Unexpected asset pagination or duplicate IDs')
    allowed = set(IDS) | set(ARCHIVES.values()) | {EARLIER_MAC['name']}
    if any(re.search(r'\.(exe|dmg|apk)$', a['name'], re.I) and a['name'] not in allowed for a in assets):
        raise ValueError('Unexpected installer asset')
    def matches(name):
        result = [a for a in assets if a['name'] == name]
        if len(result) > 1:
            raise ValueError('Duplicate installer asset: ' + name)
        return result[0] if result else None
    def metadata(asset, expected):
        return asset is not None and all(asset.get(k) == v for k, v in expected.items())
    android = originals[ANDROID]
    if not metadata(matches(ANDROID), dict(id=IDS[ANDROID], state='uploaded', size=android['size'], digest='sha256:' + android['sha256'])):
        raise ValueError('Android asset changed')
    if not metadata(matches(EARLIER_MAC['name']), EARLIER_MAC):
        raise ValueError('Earlier Mac history changed or disappeared')
    states = {}
    old_ids = set(IDS.values()) | {EARLIER_MAC['id']}
    for name in DESKTOP:
        current, history = matches(name), matches(ARCHIVES[name])
        original = originals[name]
        old = dict(id=IDS[name], state='uploaded', size=original['size'], digest='sha256:' + original['sha256'])
        if history is not None and not metadata(history, old):
            raise ValueError('Historical desktop asset conflict: ' + name)
        if history is None:
            if not metadata(current, old):
                raise ValueError('Original desktop asset or history missing: ' + name)
            states[name] = 'original'
        elif current is None:
            states[name] = 'archived'
        elif type(current.get('id')) is not int or current['id'] <= 0 or current['id'] in old_ids:
            raise ValueError('Invalid replacement asset identity')
        elif metadata(current, dict(state='starter', size=0, digest=None)):
            states[name] = 'incomplete'
        elif metadata(current, dict(state='uploaded', size=replacements[name]['size'], digest='sha256:' + replacements[name]['sha256'])):
            states[name] = 'updated'
        else:
            raise ValueError('Unknown desktop replacement: ' + name)
    return states


class Remote:
    def __init__(self):
        import boto3
        import requests
        self.requests = requests
        self.headers = {'Authorization': 'Bearer ' + os.environ['GH_TOKEN'],
                        'Accept': 'application/vnd.github+json', 'X-GitHub-Api-Version': '2022-11-28'}
        self.s3 = boto3.client('s3', endpoint_url='https://50fb972f4425789ab744ba4c6614221e.r2.cloudflarestorage.com', region_name='auto')

    def github(self, path, method='GET', **kwargs):
        response = self.requests.request(method, API + path, headers=self.headers, timeout=120, allow_redirects=False, **kwargs)
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
            u = urlparse(url)
            if (u.scheme != 'https' or u.hostname not in {'release-assets.githubusercontent.com', 'objects.githubusercontent.com'}
                    or u.username or u.password or u.port not in (None, 443) or u.fragment):
                raise ValueError('Unexpected GitHub asset redirect')
            response.close()
            response = self.requests.get(url, timeout=(30, 900), allow_redirects=False, stream=True)
        response.raise_for_status()
        if response.status_code != 200:
            raise ValueError('GitHub asset readback failed')
        size = 0
        with response, path.open('wb') as stream:
            for block in response.iter_content(1024 * 1024):
                size += len(block)
                if size > asset['size']:
                    raise ValueError('GitHub asset readback exceeds verified size')
                stream.write(block)

    def get(self, key):
        from botocore.exceptions import ClientError
        try:
            return self.s3.get_object(Bucket=BUCKET, Key=key)['Body'].read()
        except ClientError as error:
            if error.response['Error']['Code'] in ('NoSuchKey', '404'):
                raise FileNotFoundError(key) from error
            raise

    def put(self, key, body, content_type, metadata=None):
        kwargs = dict(Bucket=BUCKET, Key=key, ContentType=content_type, Metadata=metadata or {}, CacheControl='no-cache')
        if isinstance(body, Path):
            with body.open('rb') as stream:
                self.s3.put_object(Body=stream, **kwargs)
        else:
            self.s3.put_object(Body=body, **kwargs)

    def staging(self, descriptor, path):
        download_staging(descriptor, path)


def publish(descriptor, remote):
    validate_descriptor(descriptor)
    baseline_raw = load_baseline_bytes()
    baseline = json.loads(baseline_raw)
    latest = updated_manifest(baseline, descriptor)
    latest_raw = (json.dumps(latest, ensure_ascii=False, indent=2) + '\n').encode()
    originals = {f['name']: f for f in baseline['files']}
    replacements = {f['name']: f for f in descriptor['files']}
    old_sums = ''.join(f['sha256'] + '  ' + f['name'] + '\n' for f in baseline['files']).encode()
    new_sums = ''.join(f['sha256'] + '  ' + f['name'] + '\n' for f in latest['files']).encode()

    def snapshot():
        release = remote.github('/releases/tags/v1.1.2')
        tag = remote.github('/git/ref/tags/v1.1.2')
        if (release['id'] != RELEASE_ID or release['draft'] or release['prerelease']
                or release['target_commitish'] != TAG_COMMIT
                or tag['object'] != {'sha': TAG_COMMIT, 'type': 'commit', 'url': API + '/git/commits/' + TAG_COMMIT}):
            raise ValueError('Original release or tag identity changed')
        assets = remote.github(f'/releases/{RELEASE_ID}/assets?per_page=100')
        return release, assets, validate_assets(assets, descriptor, baseline)

    def metadata_state():
        current = {}
        for key in ENDPOINTS:
            raw = remote.get(key)
            if json.loads(raw) not in (baseline, latest):
                raise ValueError('Unknown published manifest: ' + key)
            current[key] = raw
        return current

    def optional_get(key):
        try:
            return remote.get(key)
        except FileNotFoundError:
            return None

    def check_archive(key, raw):
        existing = optional_get(key)
        if existing is not None and existing != raw:
            raise ValueError('Historical archive conflict: ' + key)

    def put_once(key, raw, content_type='application/octet-stream'):
        check_archive(key, raw)
        if optional_get(key) is None:
            remote.put(key, raw, content_type, {'sha256': hashlib.sha256(raw).hexdigest()})
        if remote.get(key) != raw:
            raise ValueError('Historical archive readback failed: ' + key)

    # No remote writes until all existing identities/bytes, history conflicts,
    # and all three staged installers have been checked.
    _, verified_assets, states = snapshot()
    current_metadata = metadata_state()
    if (json.loads(current_metadata['updates/latest.json']) == latest
            and any(state != 'updated' for state in states.values())):
        raise ValueError('Promoted manifest has incomplete GitHub assets')
    if remote.get(PREFIX + 'SHA256SUMS') not in (old_sums, new_sums):
        raise ValueError('Unknown published checksum list')
    old_desktop = {}
    for f in baseline['files']:
        raw = remote.get(PREFIX + f['name'])
        if f['name'] == ANDROID:
            verify_bytes(raw, f)
            continue
        try:
            verify_bytes(raw, f)
            old_desktop[f['name']] = raw
        except ValueError:
            verify_bytes(raw, replacements[f['name']])
            archived = optional_get(HISTORY + f['name'])
            if archived is None:
                raise ValueError('Replaced R2 object has no historical bytes: ' + f['name'])
            verify_bytes(archived, f)
            old_desktop[f['name']] = archived
    archives = {HISTORY + name: old_desktop[name] for name in DESKTOP}
    archives[HISTORY + 'latest.json'] = baseline_raw
    archives[HISTORY + 'SHA256SUMS'] = old_sums
    for key, raw in archives.items():
        check_archive(key, raw)
    with tempfile.TemporaryDirectory(prefix='agplayer-desktop-publish-') as directory:
        tmp = Path(directory)
        for name in DESKTOP:
            if states[name] == 'updated':
                # Completed uploads remain re-verifiable after a temporary
                # staging tunnel expires; exact new identities were pinned above.
                asset = next(a for a in verified_assets if a['name'] == name)
                remote.readback_asset(asset, tmp / name)
            else:
                remote.staging(replacements[name], tmp / name)
            verify_file(tmp / name, replacements[name])
        snapshot()
        metadata_state()
        for key, raw in archives.items():
            content_type = 'application/json' if key.endswith('.json') else 'text/plain' if key.endswith('SHA256SUMS') else 'application/octet-stream'
            put_once(key, raw, content_type)

        for name in DESKTOP:
            _, assets, states = snapshot()
            if states[name] == 'incomplete':
                failed = next(a for a in assets if a['name'] == name)
                # validate_assets has checked new identity + starter + size=0 + digest=None.
                remote.github(f"/releases/assets/{failed['id']}", 'DELETE')
                _, _, states = snapshot()
            if states[name] == 'original':
                remote.github(f'/releases/assets/{IDS[name]}', 'PATCH',
                              json={'name': ARCHIVES[name], 'label': 'Published before desktop revision 20261028; preserved'})
                _, _, states = snapshot()
            if states[name] == 'archived':
                remote.upload(tmp / name, name)
            _, assets, states = snapshot()
            if states[name] != 'updated':
                raise ValueError('GitHub replacement did not finish: ' + name)
            replacement = next(a for a in assets if a['name'] == name)
            readback = tmp / ('readback-' + name)
            remote.readback_asset(replacement, readback)
            verify_file(readback, replacements[name])
            raw = remote.get(PREFIX + name)
            if raw != old_desktop[name]:
                verify_bytes(raw, replacements[name])
            if raw != readback.read_bytes():
                # The sole overwrite path is restricted to the three pinned desktop names.
                remote.put(PREFIX + name, tmp / name,
                           'application/x-apple-diskimage' if name == MAC else 'application/vnd.microsoft.portable-executable',
                           {'sha256': replacements[name]['sha256'], 'revision': REVISION,
                            'sourcecommit': SOURCE_COMMIT})
            verify_bytes(remote.get(PREFIX + name), replacements[name])

        release, assets, states = snapshot()
        if any(state != 'updated' for state in states.values()):
            raise ValueError('Incomplete desktop replacement set')
        for f in latest['files']:
            verify_bytes(remote.get(PREFIX + f['name']), f)
        note_marker = '### 桌面版 1.1.2 修订（2026-10-28）'
        if note_marker not in (release.get('body') or ''):
            note = ('\n\n' + note_marker + '\n更新 macOS Universal、Windows x64 与原生 ARM64 官网包，优化 BPM 恢复和播放界面性能。'
                    '显示版本保持 1.1.2；已安装同版的用户请从官网重新下载覆盖安装。Android 包不变。'
                    '三份此前桌面包均以 `.before-desktop-20261028` 后缀保留，原 tag 不移动。'
                    '应用私有源码提交：`' + SOURCE_COMMIT + '`。\n')
            remote.github(f'/releases/{RELEASE_ID}', 'PATCH', json={'body': (release.get('body') or '') + note})
        metadata_state()
        if remote.get(PREFIX + 'SHA256SUMS') != new_sums:
            remote.put(PREFIX + 'SHA256SUMS', new_sums, 'text/plain; charset=utf-8')
        if remote.get(PREFIX + 'SHA256SUMS') != new_sums:
            raise ValueError('Published checksum list readback failed')
        # One common manifest is promoted only after the entire new desktop set
        # and the unchanged Android have passed GitHub/R2 readback verification.
        for key in ENDPOINTS:
            current = metadata_state()
            if current[key] != latest_raw:
                remote.put(key, latest_raw, 'application/json; charset=utf-8')
            if remote.get(key) != latest_raw:
                raise ValueError('Published manifest readback failed: ' + key)
        _, assets, _ = snapshot()
        return {'published': True, 'version': '1.1.2', 'revision': REVISION,
                'sourceCommit': SOURCE_COMMIT, 'originalTagCommit': TAG_COMMIT,
                'androidUnchanged': True, 'historyPreserved': True,
                'allThreeGitHubAndAllFourR2ReadbacksVerified': True,
                'assets': [a for a in assets if a['name'] in DESKTOP]}


def main():
    descriptor = validate_descriptor(json.loads(DESCRIPTOR_PATH.read_text()))
    print(json.dumps(publish(descriptor, Remote()), ensure_ascii=False))


if __name__ == '__main__':
    main()
