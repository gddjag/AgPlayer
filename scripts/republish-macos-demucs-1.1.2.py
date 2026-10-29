"""Publish only the four reviewed macOS 1.1.2 Demucs builds, preserving other platforms."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import tempfile
from urllib.parse import quote, urlparse

_shared_spec = importlib.util.spec_from_file_location(
    'agplayer_desktop_variants_transport', Path(__file__).with_name('publish-desktop-variants-1.1.2.py'))
_shared = importlib.util.module_from_spec(_shared_spec)
_shared_spec.loader.exec_module(_shared)
verify_file = _shared.verify_file
download_staging = _shared.download_staging
encode = _shared.encode

VERSION = '1.1.2'
REVISION = '20261029'
SOURCE_COMMIT = 'ed00b162b0a3b49164475fb5a01d638bd8e5b508'
TAG_COMMIT = '24df8985e064cda64feee3c78e0754a2492e0617'
RELEASE_ID = 414882152
API, UPLOAD_API, BUCKET = _shared.API, _shared.UPLOAD_API, _shared.BUCKET
PREFIX = 'releases/v1.1.2/'
HISTORY = PREFIX + 'history/before-demucs-20261029/'
SUFFIX = '.before-demucs-20261029'
ROOT = Path(__file__).parent.parent
BASELINE_PATHS = {
    'assets': ROOT / 'docs/releases/1.1.2-before-macos-demucs-20261029.json',
    'latest': ROOT / 'docs/releases/1.1.2-before-macos-demucs-latest-20261029.json',
    'variants': ROOT / 'docs/releases/1.1.2-before-macos-demucs-variants-20261029.json',
}
BASELINE_SHA256 = {
    'assets': '9e1eacfa33eab4cd063f3b14023789fff177f631436d46f1d6b429c38718aaa6',
    'latest': '6cf68c6d46a5516b74e84e1d3ed19a0a87db961c7af311662f42b9278aad5be3',
    'variants': '57c79517c58a59a64836c9a690f2e949d4f5506a6af2b3e464441a3f129dfd06',
}
DESCRIPTOR_PATH = ROOT / 'docs/releases/1.1.2-macos-demucs-20261029.json'
SPECS = {
    'macos-arm64': ('AgPlayer-1.1.2-macOS-arm64.dmg', ['arm64'], False),
    'macos-x86_64': ('AgPlayer-1.1.2-macOS-x86_64.dmg', ['x86_64'], False),
    'macos-models': ('AgPlayer-1.1.2-macOS-universal（内置伴奏分离模型版本）.dmg', ['arm64', 'x86_64'], True),
    'macos-universal': ('AgPlayer-1.1.2-macOS-universal.dmg', ['arm64', 'x86_64'], False),
}
GITHUB_NAMES = {spec[0]: spec[0] for spec in SPECS.values()}
GITHUB_NAMES[SPECS['macos-models'][0]] = 'AgPlayer-1.1.2-macOS-universal-bundled-models.dmg'
LATEST_KEYS = ('updates/latest.json', 'updates/macos/latest.json')
VARIANT_KEYS = (PREFIX + 'desktop-variants.json', 'updates/desktop-variants.json')
SUMS_KEY = PREFIX + 'SHA256SUMS'
VARIANT_SUMS_KEY = PREFIX + 'DESKTOP-VARIANTS-SHA256SUMS'
HISTORY_METADATA_NAMES = {'asset-pins.json', 'latest.json', 'desktop-variants.json', 'SHA256SUMS', 'DESKTOP-VARIANTS-SHA256SUMS'}
CANONICAL_KEYS = {PREFIX + name for name in GITHUB_NAMES} | set(LATEST_KEYS) | set(VARIANT_KEYS) | {SUMS_KEY, VARIANT_SUMS_KEY}
HISTORY_KEYS = {HISTORY + name for name in GITHUB_NAMES} | {HISTORY + name for name in HISTORY_METADATA_NAMES}


def load_baselines():
    raw = {}
    for kind, path in BASELINE_PATHS.items():
        raw[kind] = path.read_bytes()
        if hashlib.sha256(raw[kind]).hexdigest() != BASELINE_SHA256[kind]:
            raise ValueError('Pinned published baseline changed: ' + kind)
    return raw, {kind: json.loads(value) for kind, value in raw.items()}


def validate_descriptor(descriptor):
    if (not isinstance(descriptor, dict) or set(descriptor) != {'version', 'revision', 'sourceCommit', 'files'}
            or descriptor['version'] != VERSION or descriptor['revision'] != REVISION
            or descriptor['sourceCommit'] != SOURCE_COMMIT or not isinstance(descriptor['files'], list)
            or len(descriptor['files']) != 4):
        raise ValueError('Unexpected macOS Demucs descriptor')
    seen = set()
    for file in descriptor['files']:
        if not isinstance(file, dict) or set(file) != {'id', 'name', 'architectures', 'bundledModels',
                                                       'size', 'sha256', 'sourceCommit', 'stagingUrl'}:
            raise ValueError('Unexpected installer descriptor fields')
        spec = SPECS.get(file['id']) if isinstance(file['id'], str) else None
        if (spec is None or file['id'] in seen or file['name'] != spec[0]
                or type(file['architectures']) is not list or file['architectures'] != spec[1]
                or file['bundledModels'] is not spec[2] or type(file['size']) is not int
                or not 0 < file['size'] < 2_000_000_000 or file['sourceCommit'] != SOURCE_COMMIT
                or not isinstance(file['sha256'], str) or not re.fullmatch('[a-f0-9]{64}', file['sha256'])
                or not isinstance(file['stagingUrl'], str)):
            raise ValueError('Invalid Mac installer identity, architecture, models, size, checksum or provenance')
        try:
            url = urlparse(file['stagingUrl'])
            valid = (url.scheme == 'https' and url.username is None and url.password is None
                     and url.netloc == url.hostname and not url.port and not url.query and not url.fragment
                     and re.fullmatch(r'[a-z0-9]+(?:-[a-z0-9]+)+\.trycloudflare\.com', url.hostname or '')
                     and url.path == '/' + quote(file['name'], safe=''))
        except ValueError:
            valid = False
        if not valid:
            raise ValueError('Unexpected canonical encoded Mac staging URL')
        seen.add(file['id'])
    return descriptor


def public_manifests(descriptor):
    validate_descriptor(descriptor)
    _, baselines = load_baselines()
    latest, variants = copy.deepcopy(baselines['latest']), copy.deepcopy(baselines['variants'])
    files = {file['id']: file for file in descriptor['files']}
    for manifest in (latest, variants):
        for file in manifest['files']:
            id = file.get('id', 'macos-universal' if file['name'] == SPECS['macos-universal'][0] else '')
            if id not in files:
                continue
            new = files[id]
            file.update(size=new['size'], sha256=new['sha256'], revision=REVISION, sourceCommit=SOURCE_COMMIT)
            file['r2Url'] = ('https://download.agplayer.com/' + PREFIX + quote(new['name'], safe='')
                             + '?revision=' + REVISION)
            file['githubUrl'] = ('https://github.com/gddjag/AgPlayer/releases/download/v1.1.2/'
                                 + quote(GITHUB_NAMES[new['name']], safe='') + '?revision=' + REVISION)
    return {'latest': latest, 'variants': variants}


def checksum_bytes(manifest):
    return ''.join(file['sha256'] + '  ' + file['name'] + '\n' for file in manifest['files']).encode()


class Remote(_shared.Remote):
    """Reuse reviewed transfers; allow conditional replacement only for this Mac revision."""
    def object(self, key):
        from botocore.exceptions import ClientError
        if not hasattr(self, '_etags'):
            self._etags = {}
        try:
            response = self.s3.get_object(Bucket=BUCKET, Key=key)
        except ClientError as error:
            if error.response['Error']['Code'] in ('NoSuchKey', '404'):
                self._etags.pop(key, None)
                raise FileNotFoundError(key) from error
            raise
        self._etags[key] = response['ETag']
        return response['Body']

    def upload(self, path, name):
        if name not in GITHUB_NAMES:
            raise ValueError('Forbidden non-Mac upload')
        with path.open('rb') as stream:
            response = self.requests.post(UPLOAD_API + f'/releases/{RELEASE_ID}/assets',
                params={'name': GITHUB_NAMES[name], 'label': name},
                headers=self.headers | {'Content-Type': 'application/octet-stream', 'Content-Length': str(path.stat().st_size)},
                data=stream, timeout=(30, 900), allow_redirects=False)
        response.raise_for_status()

    def put(self, key, body, content_type, metadata=None):
        if key in HISTORY_KEYS:
            condition = {'IfNoneMatch': '*'}
        elif key in CANONICAL_KEYS and getattr(self, '_etags', {}).get(key):
            # The publisher has just hashed/read this exact object. A racing change fails closed.
            condition = {'IfMatch': self._etags[key]}
        else:
            raise ValueError('Forbidden or unverified R2 replacement: ' + key)
        kwargs = dict(Bucket=BUCKET, Key=key, ContentType=content_type, Metadata=metadata or {},
                      CacheControl='no-cache', **condition)
        if isinstance(body, Path):
            with body.open('rb') as stream:
                self.s3.put_object(Body=stream, **kwargs)
        else:
            self.s3.put_object(Body=body, **kwargs)


def publish(descriptor, remote):
    validate_descriptor(descriptor)
    baseline_raw, baselines = load_baselines()
    manifests = public_manifests(descriptor)
    new_files = {file['name']: file for file in descriptor['files']}
    original_files = {file['name']: file for manifest in (baselines['latest'], baselines['variants']) for file in manifest['files']}
    pins = {asset['name']: asset for asset in baselines['assets']['assets']}
    old_mac = {name: pins[GITHUB_NAMES[name]] for name in new_files}
    protected = {name: asset for name, asset in pins.items() if name not in GITHUB_NAMES.values()}
    old_ids = {asset['id'] for asset in pins.values()}
    if (len(original_files) != 8 or len(pins) != 8
            or baselines['assets']['releaseId'] != RELEASE_ID or baselines['assets']['targetCommit'] != TAG_COMMIT
            or baselines['assets']['tag'] != 'v1.1.2'):
        raise ValueError('Unexpected baseline asset set')
    for name, file in original_files.items():
        github_name = GITHUB_NAMES.get(name, _shared.GITHUB_NAMES.get(name, name))
        pin = pins.get(github_name, {})
        if pin.get('size') != file['size'] or pin.get('digest') != 'sha256:' + file['sha256']:
            raise ValueError('Baseline installer pins disagree: ' + name)

    old_metadata = {key: baseline_raw['latest'] for key in LATEST_KEYS}
    old_metadata.update({key: baseline_raw['variants'] for key in VARIANT_KEYS})
    old_metadata.update({SUMS_KEY: checksum_bytes(baselines['latest']), VARIANT_SUMS_KEY: checksum_bytes(baselines['variants'])})
    new_metadata = {key: encode(manifests['latest']) for key in LATEST_KEYS}
    new_metadata.update({key: encode(manifests['variants']) for key in VARIANT_KEYS})
    new_metadata.update({SUMS_KEY: checksum_bytes(manifests['latest']), VARIANT_SUMS_KEY: checksum_bytes(manifests['variants'])})
    archived_metadata = {HISTORY + 'asset-pins.json': baseline_raw['assets'],
                         HISTORY + 'latest.json': baseline_raw['latest'], HISTORY + 'desktop-variants.json': baseline_raw['variants'],
                         HISTORY + 'SHA256SUMS': old_metadata[SUMS_KEY],
                         HISTORY + 'DESKTOP-VARIANTS-SHA256SUMS': old_metadata[VARIANT_SUMS_KEY]}

    def metadata_state(require_new=False):
        state = {}
        for key, old in old_metadata.items():
            raw = remote.get(key)
            if raw not in (old, new_metadata[key]) or (require_new and raw != new_metadata[key]):
                raise ValueError('Unknown or incomplete published metadata: ' + key)
            state[key] = raw
        return state

    def matches(asset, pin, name=None):
        return asset is not None and all(asset.get(key) == (name if key == 'name' and name is not None else value)
                                         for key, value in pin.items())

    def snapshot():
        release = remote.github('/releases/tags/v1.1.2')
        tag = remote.github('/git/ref/tags/v1.1.2')
        if (release['id'] != RELEASE_ID or release['draft'] or release['prerelease']
                or release['target_commitish'] != TAG_COMMIT
                or tag['object'].get('sha') != TAG_COMMIT or tag['object'].get('type') != 'commit'):
            raise ValueError('Release or historical tag identity changed')
        assets = remote.github(f'/releases/{RELEASE_ID}/assets?per_page=100')
        allowed_names = set(pins) | {GITHUB_NAMES[name] + SUFFIX for name in new_files}
        if (len(assets) >= 100 or len({a['id'] for a in assets}) != len(assets)
                or len({a['name'] for a in assets}) != len(assets) or any(a['name'] not in allowed_names for a in assets)):
            raise ValueError('Unknown or duplicate release assets')
        indexed = {asset['name']: asset for asset in assets}
        for name, pin in protected.items():
            if not matches(indexed.get(name), pin):
                raise ValueError('Protected Windows/Android asset changed: ' + name)
        state, current, temporary = {}, {}, {}
        published = all(raw == new_metadata[key] for key, raw in metadata_state().items())
        for name, file in new_files.items():
            github_name, pin = GITHUB_NAMES[name], old_mac[name]
            asset, history = indexed.get(github_name), indexed.get(github_name + SUFFIX)
            current[name], temporary[name] = asset, history
            if history is not None and not matches(history, pin, github_name + SUFFIX):
                raise ValueError('Temporary old Mac asset conflict: ' + name)
            if matches(asset, pin):
                if history is not None:
                    raise ValueError('Duplicate old Mac identity')
                state[name] = 'original'
            elif asset is None:
                if history is None:
                    raise ValueError('Both original and temporary Mac asset are missing: ' + name)
                state[name] = 'archived'
            elif type(asset.get('id')) is not int or asset['id'] <= 0 or asset['id'] in old_ids:
                raise ValueError('Invalid replacement Mac asset identity')
            elif history is None and not published:
                raise ValueError('Old Mac asset disappeared before complete promotion')
            elif (history is not None and asset.get('state') == 'starter'
                    and asset.get('size') == 0 and asset.get('digest') is None):
                state[name] = 'starter'
            elif (asset.get('state') == 'uploaded' and asset.get('size') == file['size']
                    and asset.get('digest') == 'sha256:' + file['sha256']
                    and asset.get('label') == name):
                state[name] = 'updated'
            else:
                raise ValueError('Unknown same-name Mac replacement: ' + name)
        return current, temporary, state

    def optional_get(key):
        try:
            return remote.get(key)
        except FileNotFoundError:
            return None

    def check_archive_metadata():
        for key, raw in archived_metadata.items():
            current = optional_get(key)
            if current is not None and current != raw:
                raise ValueError('Historical metadata conflict: ' + key)

    def archive_metadata_once(key, raw):
        existing = optional_get(key)
        if existing is None:
            remote.put(key, raw, 'application/json' if key.endswith('.json') else 'text/plain; charset=utf-8')
        elif existing != raw:
            raise ValueError('Historical metadata conflict: ' + key)
        if remote.get(key) != raw:
            raise ValueError('Historical metadata readback failed: ' + key)

    current, temporary, states = snapshot()
    check_archive_metadata()
    with tempfile.TemporaryDirectory(prefix='agplayer-mac-demucs-publish-') as directory:
        tmp = Path(directory)
        r2_states = {}
        # All eight current R2 installers are checked before any remote mutation.
        for name, file in original_files.items():
            target = tmp / ('old-' + name)
            remote.readback_object(PREFIX + name, target)
            try:
                verify_file(target, file)
                r2_states[name] = 'old'
            except ValueError:
                if name not in new_files:
                    raise
                verify_file(target, new_files[name])
                r2_states[name] = 'new'
            if name in new_files:
                archived = tmp / ('archived-' + name)
                try:
                    remote.readback_object(HISTORY + name, archived)
                except FileNotFoundError:
                    if r2_states[name] != 'old':
                        raise ValueError('Replaced Mac object has no verified historical archive: ' + name)
                else:
                    verify_file(archived, file)
                    if r2_states[name] == 'new':
                        archived.replace(target)
        # A complete retry can recover bytes from the verified new GitHub mirror after tunnel expiry.
        for name, file in new_files.items():
            path = tmp / name
            if states[name] == 'updated':
                remote.readback_asset(current[name], path)
            elif r2_states[name] == 'new':
                remote.readback_object(PREFIX + name, path)
            else:
                remote.staging(file, path)
            verify_file(path, file)
        snapshot()
        check_archive_metadata()

        for name in new_files:
            # Archive every old Mac before replacing even the first canonical installer.
            archive = tmp / 'history-readback'
            try:
                remote.readback_object(HISTORY + name, archive)
            except FileNotFoundError:
                remote.put(HISTORY + name, tmp / ('old-' + name), 'application/octet-stream',
                           {'sha256': original_files[name]['sha256']})
                remote.readback_object(HISTORY + name, archive)
            verify_file(archive, original_files[name])
        for key, raw in archived_metadata.items():
            archive_metadata_once(key, raw)

        for name, file in new_files.items():
            current, temporary, states = snapshot()
            if states[name] == 'starter':
                # The snapshot pins this to a new zero-byte starter, never one of the eight old IDs.
                remote.github(f"/releases/assets/{current[name]['id']}", 'DELETE')
                current, temporary, states = snapshot()
            if states[name] == 'original':
                remote.github(f"/releases/assets/{old_mac[name]['id']}", 'PATCH',
                              json={'name': GITHUB_NAMES[name] + SUFFIX})
                current, temporary, states = snapshot()
            if states[name] == 'archived':
                remote.upload(tmp / name, name)
            current, temporary, states = snapshot()
            if states[name] != 'updated':
                raise ValueError('Mac GitHub upload is incomplete: ' + name)
            remote.readback_asset(current[name], tmp / 'new-github')
            verify_file(tmp / 'new-github', file)
            remote.readback_object(PREFIX + name, tmp / 'current-r2')
            try:
                verify_file(tmp / 'current-r2', file)
            except ValueError:
                verify_file(tmp / 'current-r2', original_files[name])
                remote.put(PREFIX + name, tmp / name, 'application/x-apple-diskimage',
                           {'sha256': file['sha256'], 'sourcecommit': SOURCE_COMMIT, 'revision': REVISION})
            remote.readback_object(PREFIX + name, tmp / 'new-r2')
            verify_file(tmp / 'new-r2', file)

        def verify_published_installers():
            current, temporary, states = snapshot()
            if any(state != 'updated' for state in states.values()):
                raise ValueError('Incomplete new Mac installer set')
            for name, file in original_files.items():
                remote.readback_object(PREFIX + name, tmp / 'final-r2')
                verify_file(tmp / 'final-r2', new_files.get(name, file))
            for name, file in new_files.items():
                remote.readback_asset(current[name], tmp / 'final-github')
                verify_file(tmp / 'final-github', file)
            return temporary

        verify_published_installers()
        check_archive_metadata()
        # Only reviewed old/new metadata states may be replaced; windows records remain byte-for-byte values.
        for key in (SUMS_KEY, VARIANT_SUMS_KEY, *VARIANT_KEYS, *reversed(LATEST_KEYS)):
            snapshot()
            state = metadata_state()
            if state[key] != new_metadata[key]:
                remote.put(key, new_metadata[key], 'application/json' if key.endswith('.json') else 'text/plain; charset=utf-8')
            if remote.get(key) != new_metadata[key]:
                raise ValueError('Promoted metadata readback failed: ' + key)
        metadata_state(require_new=True)
        # Delete only the four exact old IDs after all new readbacks and both feeds are complete.
        for name in new_files:
            current, temporary, states = snapshot()
            metadata_state(require_new=True)
            history = temporary[name]
            if history is not None:
                if not matches(history, old_mac[name], GITHUB_NAMES[name] + SUFFIX):
                    raise ValueError('Old Mac cleanup identity changed')
                remote.github(f"/releases/assets/{old_mac[name]['id']}", 'DELETE')
        verify_published_installers()
        _, temporary, _ = snapshot()
        if any(temporary.values()):
            raise ValueError('Temporary old Mac assets remain')
        metadata_state(require_new=True)
    return dict(published=True, version=VERSION, revision=REVISION, sourceCommit=SOURCE_COMMIT,
                originalTagCommit=TAG_COMMIT, preservedAssetIds=[asset['id'] for asset in protected.values()],
                removedTemporaryOldAssetIds=[asset['id'] for asset in old_mac.values()], historyPrefix=HISTORY,
                allFourGitHubAndAllEightR2ReadbacksVerified=True, manifests=manifests,
                manifestSha256={kind: hashlib.sha256(encode(value)).hexdigest() for kind, value in manifests.items()})


if __name__ == '__main__':
    print(json.dumps(publish(json.loads(DESCRIPTOR_PATH.read_text()), Remote()), ensure_ascii=False))
