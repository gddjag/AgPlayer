import hashlib,json,os,subprocess,tempfile
from pathlib import Path
from urllib.parse import quote,unquote,urlsplit

tag='v1.1.6'
repo='gddjag/AgPlayer'
bucket='agplayer-releases'
endpoint='https://50fb972f4425789ab744ba4c6614221e.r2.cloudflarestorage.com'
metadata=json.loads(Path('docs/releases/v1.1.6-verified-assets.json').read_text())
assert os.environ['GITHUB_REPOSITORY']==repo and metadata['tag']==tag and len(metadata['files'])==8
assert os.environ.get('AWS_ACCESS_KEY_ID') and os.environ.get('AWS_SECRET_ACCESS_KEY')
def aws(*args):
    return subprocess.check_output(['aws','--endpoint-url',endpoint,*args])
def checksum(path):
    with path.open('rb') as stream: return hashlib.file_digest(stream,'sha256').hexdigest()
def write_manifest(key,raw,path):
    path.write_bytes(raw)
    aws('s3','cp',str(path),f's3://{bucket}/{key}','--only-show-errors',
        '--content-type','application/json; charset=utf-8','--cache-control','no-cache')
    assert aws('s3','cp',f's3://{bucket}/{key}','-','--only-show-errors') == raw

with tempfile.TemporaryDirectory() as t:
    target=Path(t)/'asset'
    manifest=(json.dumps(metadata['variants'],ensure_ascii=False,indent=2)+'\n').encode()
    if metadata.get('promoteVariants'):
        assert aws('s3','cp',f's3://{bucket}/releases/{tag}/desktop-variants.json','-','--only-show-errors') == manifest
        for key in ('updates/latest.json','updates/macos/latest.json'):
            current=json.loads(aws('s3','cp',f's3://{bucket}/{key}','-','--only-show-errors'))
            assert current['tag']==tag
        write_manifest('updates/desktop-variants.json',manifest,target)
        print('Verified promoted desktop variants manifest',flush=True)
    else:
        release=json.loads(subprocess.check_output(['gh','api',f'/repos/{repo}/releases/{metadata["githubReleaseId"]}']))
        assert release['tag_name']==tag and release['draft']
        assets={a['name']:a for a in release['assets']}
        for f in metadata['files']:
            name,sha=f['name'],f['sha256']
            gh=unquote(urlsplit(f['githubUrl']).path.split('/')[-1])
            assert '/' not in name and name.startswith(('AgPlayer-1.1.6-','AgPlayer-Setup-1.1.6-'))
            assert gh==name.replace('（内置伴奏分离模型版本）','-bundled-models')
            asset=assets[gh]
            assert asset['size']==f['size'] and asset.get('digest') in ('sha256:'+sha,None)
            with target.open('wb') as out:
                subprocess.run(['gh','api','-H','Accept: application/octet-stream',
                    f'/repos/{repo}/releases/assets/{asset["id"]}'],stdout=out,check=True)
            assert target.stat().st_size==f['size'] and checksum(target)==sha
            key=f'releases/{tag}/{name}'
            uri=f's3://{bucket}/{key}'
            kind={'.exe':'application/octet-stream','.dmg':'application/x-apple-diskimage','.apk':'application/vnd.android.package-archive'}[target.with_name(name).suffix]
            current=subprocess.run(['aws','--endpoint-url',endpoint,'s3api','head-object','--bucket',bucket,'--key',key],capture_output=True)
            if current.returncode:
                assert b'(404)' in current.stderr or b'Not Found' in current.stderr
                aws('s3','cp',str(target),uri,'--only-show-errors','--content-type',kind,
                    '--metadata',f'sha256={sha},sourcecommit={metadata["sourceCommit"]}')
            target.unlink()
            aws('s3','cp',uri,str(target),'--only-show-errors')
            assert target.stat().st_size==f['size'] and checksum(target)==sha
            target.unlink()
            print('Verified',key,sha,flush=True)
        write_manifest(f'releases/{tag}/desktop-variants.json',manifest,target)
