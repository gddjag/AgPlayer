"""Re-publish the eight pinned website installers; Store and Harmony are outside scope.

Use an isolated instance of the successful Demucs publisher's state machine. Only
this revision's immutable inputs, installer identities and public feeds differ.
"""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import re
from urllib.parse import quote, urlparse

_spec = importlib.util.spec_from_file_location(
    'agplayer_website_updates_engine', Path(__file__).with_name('republish-macos-demucs-1.1.2.py'))
_engine = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_engine)
_shared = _engine._shared
encode, verify_file, download_staging = _engine.encode, _engine.verify_file, _engine.download_staging
VERSION, REVISION = '1.1.2', '20261030'
SOURCE_COMMIT = 'd7d523ee00172da05289667f102f0ad8d35f07c1'
TAG_COMMIT = '24df8985e064cda64feee3c78e0754a2492e0617'
RELEASE_ID = 414882152
API, UPLOAD_API, BUCKET = _engine.API, _engine.UPLOAD_API, _engine.BUCKET
ROOT = Path(__file__).parent.parent
PREFIX = 'releases/v1.1.2/'
HISTORY = PREFIX + 'history/before-website-updates-20261030/'
SUFFIX = '.before-website-updates-20261030'
BASELINE_PATHS = {kind: ROOT / f'docs/releases/1.1.2-before-website-updates-{kind}-20261030.json'
                  for kind in ('assets', 'latest', 'variants')}
BASELINE_SHA256 = {
    'assets': 'e6febb28af82f7784c084f53acf7ecabd2d6c301cfaa52140bdd8ff65c7ade34',
    'latest': '6ab13c44a43abf206b5811307c43e8274397249a53bf49db184a9411b122589f',
    'variants': 'c37d33d0e9c72e4672b6653e4463b6516047507b88d57ea31ad680b06f4927be',
}
DESCRIPTOR_PATH = ROOT / 'docs/releases/1.1.2-website-updates-20261030.json'
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
GITHUB_NAMES = {name: name.replace('（内置伴奏分离模型版本）', '-bundled-models') for name, _, _ in SPECS.values()}
LATEST_KEYS, VARIANT_KEYS = _engine.LATEST_KEYS, _engine.VARIANT_KEYS
SUMS_KEY, VARIANT_SUMS_KEY = _engine.SUMS_KEY, _engine.VARIANT_SUMS_KEY
HISTORY_METADATA_NAMES = _engine.HISTORY_METADATA_NAMES
CANONICAL_KEYS = {PREFIX + name for name in GITHUB_NAMES} | set(LATEST_KEYS) | set(VARIANT_KEYS) | {SUMS_KEY, VARIANT_SUMS_KEY}
HISTORY_KEYS = {HISTORY + name for name in GITHUB_NAMES} | {HISTORY + name for name in HISTORY_METADATA_NAMES}
checksum_bytes = _engine.checksum_bytes


def load_baselines():
    raw = {kind: path.read_bytes() for kind, path in BASELINE_PATHS.items()}
    for kind, value in raw.items():
        if hashlib.sha256(value).hexdigest() != BASELINE_SHA256[kind]:
            raise ValueError('Pinned published baseline changed: ' + kind)
    baselines = {kind: json.loads(value) for kind, value in raw.items()}
    assets = baselines['assets']
    if (not isinstance(assets, list) or len(assets) != 8
            or {a.get('name') for a in assets} != set(GITHUB_NAMES.values())
            or len({a.get('id') for a in assets}) != 8
            or any(type(a.get('id')) is not int or a['id'] <= 0 for a in assets)):
        raise ValueError('Unexpected eight pinned website assets')
    # Full API records contain volatile download counters/timestamps/uploader fields.
    # Keep only immutable identity/content fields for comparisons; archive raw bytes.
    baselines['assets'] = dict(releaseId=RELEASE_ID, targetCommit=TAG_COMMIT, tag='v1.1.2',
        assets=[{key: asset[key] for key in ('id', 'name', 'label', 'state', 'size', 'digest')} for asset in assets])
    if (len(baselines['latest']['files']) != 4 or len(baselines['variants']['files']) != 4
            or {f['name'] for kind in ('latest', 'variants') for f in baselines[kind]['files']} != set(GITHUB_NAMES)):
        raise ValueError('Unexpected baseline website installer set')
    return raw, baselines


def validate_descriptor(descriptor):
    if (not re.fullmatch('[a-f0-9]{40}', SOURCE_COMMIT) or SOURCE_COMMIT == '0' * 40
            or not isinstance(descriptor, dict) or set(descriptor) != {'version', 'revision', 'sourceCommit', 'files'}
            or descriptor['version'] != VERSION or descriptor['revision'] != REVISION
            or descriptor['sourceCommit'] != SOURCE_COMMIT or not isinstance(descriptor['files'], list)
            or len(descriptor['files']) != 8):
        raise ValueError('Unexpected website update descriptor or unfrozen source commit')
    seen = set()
    for file in descriptor['files']:
        if not isinstance(file, dict) or set(file) != {'id', 'name', 'architectures', 'bundledModels', 'size', 'sha256', 'sourceCommit', 'stagingUrl'}:
            raise ValueError('Unexpected installer descriptor fields')
        spec = SPECS.get(file['id']) if isinstance(file['id'], str) else None
        if (spec is None or file['id'] in seen or file['name'] != spec[0]
                or type(file['architectures']) is not list or file['architectures'] != spec[1]
                or file['bundledModels'] is not spec[2] or type(file['size']) is not int
                or not 0 < file['size'] < 2_000_000_000 or file['sourceCommit'] != SOURCE_COMMIT
                or not isinstance(file['sha256'], str) or not re.fullmatch('[a-f0-9]{64}', file['sha256'])
                or not isinstance(file['stagingUrl'], str)):
            raise ValueError('Invalid installer identity, architecture, models, bytes or provenance')
        try:
            url = urlparse(file['stagingUrl'])
            valid = (url.scheme == 'https' and url.username is None and url.password is None
                     and url.netloc == url.hostname and not url.port and not url.query and not url.fragment
                     and re.fullmatch(r'[a-z0-9]+(?:-[a-z0-9]+)+\.trycloudflare\.com', url.hostname or '')
                     and url.path == '/' + quote(file['name'], safe=''))
        except ValueError:
            valid = False
        if not valid:
            raise ValueError('Unexpected canonical encoded staging URL')
        seen.add(file['id'])
    return descriptor


def public_manifests(descriptor):
    validate_descriptor(descriptor)
    _, baselines = load_baselines()
    manifests = {kind: copy.deepcopy(baselines[kind]) for kind in ('latest', 'variants')}
    files = {file['name']: file for file in descriptor['files']}
    for manifest in manifests.values():
        for file in manifest['files']:
            new = files[file['name']]
            file.update(size=new['size'], sha256=new['sha256'], sourceCommit=SOURCE_COMMIT)
            # Android's existing website/native validators accept neither revision nor URL queries.
            if new['id'] == 'android-arm64':
                file.pop('revision', None)
                query = ''
            else:
                file['revision'] = REVISION
                query = '?revision=' + REVISION
            file['r2Url'] = 'https://download.agplayer.com/' + PREFIX + quote(new['name'], safe='') + query
            file['githubUrl'] = ('https://github.com/gddjag/AgPlayer/releases/download/v1.1.2/'
                                 + quote(GITHUB_NAMES[new['name']], safe='') + query)
    return manifests


def _configure_engine():
    # An isolated module instance avoids mutating the original publisher or its tests.
    for name in ('VERSION', 'REVISION', 'SOURCE_COMMIT', 'TAG_COMMIT', 'RELEASE_ID', 'PREFIX', 'HISTORY', 'SUFFIX',
                 'SPECS', 'GITHUB_NAMES', 'LATEST_KEYS', 'VARIANT_KEYS', 'SUMS_KEY', 'VARIANT_SUMS_KEY',
                 'CANONICAL_KEYS', 'HISTORY_KEYS', 'load_baselines', 'validate_descriptor', 'public_manifests'):
        setattr(_engine, name, globals()[name])


class Remote(_engine.Remote):
    """Keep ETag conditions and immutable archives from the reviewed transport."""
    def put(self, key, body, content_type, metadata=None):
        if key in {PREFIX + name for name in GITHUB_NAMES}:
            content_type = ('application/vnd.android.package-archive' if key.endswith('.apk')
                            else 'application/x-apple-diskimage' if key.endswith('.dmg') else 'application/octet-stream')
        return super().put(key, body, content_type, metadata)


def publish(descriptor, remote):
    _configure_engine()
    receipt = _engine.publish(descriptor, remote)
    receipt.pop('allFourGitHubAndAllEightR2ReadbacksVerified')
    receipt['allEightGitHubAndR2ReadbacksVerified'] = True
    return receipt


_configure_engine()
if __name__ == '__main__':
    print(json.dumps(publish(json.loads(DESCRIPTOR_PATH.read_text()), Remote()), ensure_ascii=False))
