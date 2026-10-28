"""Verify the published desktop revision using unauthenticated public downloads."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
DESKTOP = {'AgPlayer-1.1.2-macOS-universal.dmg', 'AgPlayer-Setup-1.1.2-x64.exe', 'AgPlayer-Setup-1.1.2-arm64.exe'}


def fetch(url, path):
    subprocess.run(['curl', '--fail', '--location', '--silent', '--show-error',
                    '--retry', '3', '--retry-all-errors', '--connect-timeout', '20',
                    '--max-time', '600', url, '--output', str(path)], check=True)


def verify(path, expected):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    assert path.stat().st_size == expected['size'], expected['name']
    assert digest.hexdigest() == expected['sha256'], expected['name']


def main():
    expected_raw = (ROOT / 'updates/latest.json').read_bytes()
    expected = json.loads(expected_raw)
    baseline = json.loads((ROOT / 'docs/releases/1.1.2-before-desktop-20261028.json').read_bytes())
    assert expected['version'] == '1.1.2' and len(expected['files']) == 4
    records = []
    with tempfile.TemporaryDirectory(prefix='agplayer-public-verify-') as directory:
        temp = Path(directory)
        # The website deployment runs independently of this verification workflow.
        for attempt in range(20):
            fetch('https://agplayer.com/updates/latest.json', temp / 'site.json')
            if (temp / 'site.json').read_bytes() == expected_raw:
                break
            if attempt == 19:
                raise ValueError('Website manifest did not reach the verified release')
            time.sleep(20)
        for endpoint in ['https://download.agplayer.com/updates/latest.json',
                         'https://download.agplayer.com/updates/macos/latest.json']:
            fetch(endpoint, temp / 'manifest.json')
            assert (temp / 'manifest.json').read_bytes() == expected_raw, endpoint
        for file in expected['files']:
            fetch(file['r2Url'], temp / 'installer')
            verify(temp / 'installer', file)
            if file['name'] in DESKTOP:
                assert file['revision'] == '20261028'
                fetch(file['githubUrl'], temp / 'installer')
                verify(temp / 'installer', file)
                before = next(item for item in baseline['files'] if item['name'] == file['name'])
                fetch('https://download.agplayer.com/releases/v1.1.2/history/before-desktop-20261028/' + file['name'], temp / 'installer')
                verify(temp / 'installer', before)
            else:
                assert file == next(item for item in baseline['files'] if item['name'] == file['name'])
            records.append({key: file[key] for key in ['name', 'size', 'sha256']})
            print('Verified', file['name'], flush=True)
        fetch('https://agplayer.com/', temp / 'index.html')
        assert (temp / 'index.html').read_text().count('d753c9d11730ddacf686e8ccddce6e11') == 1
        fetch('https://agplayer.com/download', temp / 'download.html')
        html = (temp / 'download.html').read_text()
        assert '20261028-desktop-release' in html
        assert 'https://support.apple.com/zh-cn/102445' in html
    receipt = dict(version='1.1.2', revision='20261028', files=records,
                   websiteManifestByteIdentical=True, legacyManifestByteIdentical=True,
                   githubDesktopByteIdentical=True, previousDesktopArchived=True,
                   androidUnchanged=True, analyticsPreserved=True)
    Path('public-desktop-verification.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('PUBLIC_RELEASE_VERIFIED ' + json.dumps(receipt))


if __name__ == '__main__':
    main()
