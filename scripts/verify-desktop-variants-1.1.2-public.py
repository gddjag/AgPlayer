"""Read the new desktop choices through their exact unauthenticated public URLs."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent


def fetch(url, path):
    subprocess.run(['curl', '--fail', '--location', '--silent', '--show-error', '--retry', '3',
                    '--retry-all-errors', '--connect-timeout', '20', '--max-time', '900',
                    url, '--output', str(path)], check=True)


def main():
    variants_raw = (ROOT / 'updates/desktop-variants.json').read_bytes()
    variants = json.loads(variants_raw)
    canonical_raw = (ROOT / 'updates/latest.json').read_bytes()
    assert len(variants['files']) == 4 and variants['version'] == '1.1.2'
    records = []
    with tempfile.TemporaryDirectory(prefix='agplayer-public-variants-') as directory:
        temp = Path(directory)
        for attempt in range(20):
            result = subprocess.run(['curl', '-fLsS', '--connect-timeout', '20', '--max-time', '60',
                                     'https://agplayer.com/updates/desktop-variants.json', '-o', str(temp / 'site.json')])
            if result.returncode == 0 and (temp / 'site.json').read_bytes() == variants_raw:
                break
            if attempt == 19:
                raise ValueError('Website did not deploy the exact verified desktop variants')
            time.sleep(20)
        for endpoint in ['https://download.agplayer.com/updates/desktop-variants.json',
                         'https://download.agplayer.com/releases/v1.1.2/desktop-variants.json']:
            fetch(endpoint, temp / 'manifest')
            assert (temp / 'manifest').read_bytes() == variants_raw, endpoint
        for endpoint in ['https://agplayer.com/updates/latest.json',
                         'https://download.agplayer.com/updates/latest.json',
                         'https://download.agplayer.com/updates/macos/latest.json']:
            fetch(endpoint, temp / 'manifest')
            assert (temp / 'manifest').read_bytes() == canonical_raw, endpoint
        for file in variants['files']:
            for route in ('r2Url', 'githubUrl'):
                fetch(file[route], temp / 'installer')
                assert (temp / 'installer').stat().st_size == file['size'], (file['name'], route)
                with (temp / 'installer').open('rb') as stream:
                    digest = hashlib.sha256()
                    for block in iter(lambda: stream.read(1024 * 1024), b''):
                        digest.update(block)
                assert digest.hexdigest() == file['sha256'], (file['name'], route)
            records.append({key: file[key] for key in ('id', 'name', 'size', 'sha256')})
            print('Verified both public routes:', file['name'], flush=True)
        for relative in ('download.html', 'assets/js/downloads.js', 'assets/js/translations.js', 'assets/css/premium.css'):
            fetch('https://agplayer.com/' + relative, temp / 'source')
            assert (temp / 'source').read_bytes() == (ROOT / relative).read_bytes(), relative
        fetch('https://agplayer.com/', temp / 'index.html')
        assert (temp / 'index.html').read_text().count('d753c9d11730ddacf686e8ccddce6e11') == 1
    receipt = dict(version='1.1.2', files=records, allFourPublicGitHubAndR2ByteIdentical=True,
                   canonicalFeedsUnchanged=True, variantsManifestsByteIdentical=True,
                   websiteSourceByteIdentical=True, analyticsPreserved=True)
    Path('public-desktop-variants-verification.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + '\n')
    print('PUBLIC_DESKTOP_VARIANTS_VERIFIED ' + json.dumps(receipt, ensure_ascii=False))


if __name__ == '__main__':
    main()
