"""Explicitly authorized 2026-10-27 Mac-only replacement; never a generic overwrite switch."""
import copy
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile
import time
from urllib.parse import urlparse

MAC = 'AgPlayer-1.1.2-macOS-universal.dmg'
ARCHIVE = MAC + '.original-20261016'
REVISION = '20261027'
TAG_COMMIT = '24df8985e064cda64feee3c78e0754a2492e0617'
RELEASE_ID = 414882152
IDS = {MAC:647102605, 'AgPlayer-1.1.2-arm64-release.apk':647102603,
       'AgPlayer-Setup-1.1.2-x64.exe':647102604, 'AgPlayer-Setup-1.1.2-arm64.exe':670522430}
API = 'https://api.github.com/repos/gddjag/AgPlayer'
BUCKET = 'agplayer-releases'
PREFIX = 'releases/v1.1.2/'
HISTORY = PREFIX + 'history/20261016/'


def load_baseline():
    raw = (Path(__file__).parent.parent/'docs/releases/1.1.2-before-macos-20261027.json').read_bytes()
    if hashlib.sha256(raw).hexdigest() != 'fa201e2a3ab250d226edf5ea6f7d16fbc916e17ba16287a587a78381d7bed653':
        raise ValueError('Original manifest changed')
    return json.loads(raw)


def validate_descriptor(d):
    if set(d) != {'version','name','size','sha256','sourceCommit','stagingUrl'} or d['version'] != '1.1.2' or d['name'] != MAC:
        raise ValueError('Unexpected Mac release descriptor')
    if type(d['size']) is not int or not 0 < d['size'] < 2_000_000_000 or not re.fullmatch('[a-f0-9]{64}', d['sha256']) or not re.fullmatch('[a-f0-9]{40}', d['sourceCommit']):
        raise ValueError('Invalid size, checksum or source commit')
    u = urlparse(d['stagingUrl'])
    if u.scheme != 'https' or u.username or u.password or u.port or u.query or u.fragment or not re.fullmatch('[a-z0-9-]+\.trycloudflare\.com', u.hostname or '') or u.path != '/'+MAC:
        raise ValueError('Unexpected Mac staging URL')
    return d


def updated_manifest(baseline, d):
    latest = copy.deepcopy(baseline)
    mac = next(x for x in latest['files'] if x['name'] == MAC)
    mac.update(size=d['size'], sha256=d['sha256'], revision=REVISION)
    for key in ('r2Url','githubUrl'): mac[key] += '?revision='+REVISION
    return latest


def download_staging(d, path, fetcher=None):
    if fetcher is None:
        import requests
        fetcher = requests.get
    digest = hashlib.sha256()
    # Bounded ranges allow transient tunnel interruptions to retry without restarting 110 MB.
    with path.open('wb') as stream:
        for start in range(0,d['size'],4*1024*1024):
            end=min(start+4*1024*1024,d['size'])-1
            for attempt in range(4):
                try:
                    response=fetcher(d['stagingUrl'],timeout=(30,120),allow_redirects=False,
                                     headers={'Range':f'bytes={start}-{end}'})
                    if response.status_code!=206 or response.headers.get('Content-Range')!=f"bytes {start}-{end}/{d['size']}":
                        raise ValueError('Unexpected staging range response')
                    raw=response.content
                    if len(raw)!=end-start+1: raise ValueError('Incomplete staging range')
                    break
                except Exception:
                    if attempt==3: raise
                    time.sleep(2)
            digest.update(raw);stream.write(raw)
    if path.stat().st_size!=d['size'] or digest.hexdigest()!=d['sha256']:
        raise ValueError('Staging checksum mismatch')


def validate_assets(assets, d):
    baseline = load_baseline()
    if any(re.search(r'\.(exe|dmg|apk)$', a['name'], re.I) and a['name'] not in IDS for a in assets):
        raise ValueError('Unexpected installer asset')
    for f in baseline['files']:
        if f['name'] == MAC: continue
        matches = [a for a in assets if a['name'] == f['name']]
        if len(matches) != 1 or any(matches[0].get(k) != v for k,v in dict(id=IDS[f['name']], state='uploaded',size=f['size'],digest='sha256:'+f['sha256']).items()):
            raise ValueError('Other platform asset changed: '+f['name'])
    original = next(f for f in baseline['files'] if f['name']==MAC)
    history = [a for a in assets if a['name']==ARCHIVE]
    current = [a for a in assets if a['name']==MAC]
    if len(current)>1 or len(history)>1: raise ValueError('Duplicate Mac assets')
    for a in history:
        if any(a.get(k)!=v for k,v in dict(id=IDS[MAC],state='uploaded',size=original['size'],digest='sha256:'+original['sha256']).items()):
            raise ValueError('Historical Mac asset changed')
    if not current:
        if history: return 'archived'
        raise ValueError('Original Mac history missing')
    a=current[0]
    if history and isinstance(a.get('id'),int) and a['id']>0 and a['id']!=IDS[MAC] and a.get('state')=='starter' and a.get('size')==0:
        return 'incomplete'
    if a.get('state')!='uploaded': raise ValueError('Incomplete Mac asset')
    if a.get('id')==IDS[MAC] and a.get('size')==original['size'] and a.get('digest')=='sha256:'+original['sha256'] and not history:
        return 'original'
    if history and a.get('id')!=IDS[MAC] and a.get('size')==d['size'] and a.get('digest')=='sha256:'+d['sha256']:
        return 'updated'
    raise ValueError('Unrecognized Mac replacement')


def main():
    import boto3
    import requests
    d=validate_descriptor(json.loads((Path(__file__).parent.parent/'docs/releases/1.1.2-macos-republish-20261027.json').read_text()))
    baseline=load_baseline(); latest=updated_manifest(baseline,d)
    token=os.environ['GH_TOKEN']
    headers={'Authorization':'Bearer '+token,'Accept':'application/vnd.github+json','X-GitHub-Api-Version':'2022-11-28'}
    def gh(path, method='GET', **kw):
        r=requests.request(method,API+path,headers=headers,timeout=120,allow_redirects=False,**kw)
        r.raise_for_status(); return r.json()
    def snapshot():
        release=gh('/releases/tags/v1.1.2'); tag=gh('/git/ref/tags/v1.1.2')
        if release['id']!=RELEASE_ID or release['draft'] or release['prerelease'] or release['target_commitish']!=TAG_COMMIT or tag['object']!={'sha':TAG_COMMIT,'type':'commit','url':API+'/git/commits/'+TAG_COMMIT}:
            raise ValueError('Release/tag identity changed')
        assets=gh(f'/releases/{RELEASE_ID}/assets?per_page=100')
        if len(assets)>=100: raise ValueError('Unexpected asset pagination')
        return release,validate_assets(assets,d)
    s3=boto3.client('s3',endpoint_url='https://50fb972f4425789ab744ba4c6614221e.r2.cloudflarestorage.com',region_name='auto')
    def get(key): return s3.get_object(Bucket=BUCKET,Key=key)['Body'].read()
    def verify_bytes(raw, f):
        if len(raw)!=f['size'] or hashlib.sha256(raw).hexdigest()!=f['sha256']:
            raise ValueError('Object checksum mismatch: '+f['name'])
    def put_once(key, raw, content_type='application/octet-stream', digest=None):
        from botocore.exceptions import ClientError
        try:
            existing=get(key)
        except ClientError as e:
            if e.response['Error']['Code'] not in ('NoSuchKey','404'): raise
        else:
            if existing!=raw: raise ValueError('Historical archive conflict: '+key)
            return
        s3.put_object(Bucket=BUCKET,Key=key,Body=raw,ContentType=content_type,
                      Metadata={'sha256':digest or hashlib.sha256(raw).hexdigest()})
        if get(key)!=raw: raise ValueError('Archive verification failed')
    release,state=snapshot()
    current_raw=get('updates/latest.json'); current=json.loads(current_raw)
    if current not in (baseline,latest): raise ValueError('Current latest changed; refusing promotion')
    # Preflight all existing R2 bytes and other platform identities before any remote mutation.
    old_mac=None
    for f in baseline['files']:
        raw=get(PREFIX+f['name'])
        if f['name'] != MAC:
            verify_bytes(raw,f)
        elif len(raw)==f['size'] and hashlib.sha256(raw).hexdigest()==f['sha256']:
            old_mac=raw
        else:
            verify_bytes(raw,d)
    with tempfile.TemporaryDirectory(prefix='agplayer-macos-publish-') as tmp:
        path=Path(tmp)/MAC
        download_staging(d,path)
        size=d['size']
        original=next(x for x in baseline['files'] if x['name']==MAC)
        if old_mac is not None:
            put_once(HISTORY+MAC,old_mac,digest=original['sha256'])
        else:
            verify_bytes(get(HISTORY+MAC),original)
        baseline_raw=(Path(__file__).parent.parent/'docs/releases/1.1.2-before-macos-20261027.json').read_bytes()
        put_once(HISTORY+'latest.json',baseline_raw,'application/json')
        old_sums=''.join(f["sha256"]+'  '+f['name']+'\n' for f in baseline['files']).encode()
        put_once(HISTORY+'SHA256SUMS',old_sums,'text/plain')
        # A rerun resumes safely after the old asset rename or after the new upload.
        release,state=snapshot()
        if state=='incomplete':
            assets=gh(f'/releases/{RELEASE_ID}/assets?per_page=100')
            failed=next(a for a in assets if a['name']==MAC)
            if failed['id']==IDS[MAC] or failed['state']!='starter' or failed['size']!=0:
                raise ValueError('Incomplete upload changed before cleanup')
            response=requests.delete(API+f"/releases/assets/{failed['id']}",headers=headers,timeout=120,allow_redirects=False)
            response.raise_for_status()
            _,state=snapshot()
        if state=='original':
            gh(f'/releases/assets/{IDS[MAC]}','PATCH',json={'name':ARCHIVE,'label':'Original macOS 1.1.2 package (2026-10-16), preserved'})
            _,state=snapshot()
        if state=='archived':
            with path.open('rb') as stream:
                response=requests.post(f'https://uploads.github.com/repos/gddjag/AgPlayer/releases/{RELEASE_ID}/assets',
                    params={'name':MAC},headers=headers|{'Content-Type':'application/octet-stream','Content-Length':str(size)},
                    data=stream,timeout=(30,900),allow_redirects=False)
            response.raise_for_status()
        _,state=snapshot()
        if state!='updated': raise ValueError('GitHub new asset verification failed')
        # This fixed, reviewed replacement is the only code path permitted to overwrite this Mac object.
        with path.open('rb') as stream:
            s3.put_object(Bucket=BUCKET,Key=PREFIX+MAC,Body=stream,ContentType='application/x-apple-diskimage',
                          CacheControl='no-cache',Metadata={'sha256':d['sha256'],'revision':REVISION,'sourcecommit':d['sourceCommit']})
        verify_bytes(get(PREFIX+MAC),d)
        note='\n\n### macOS 1.1.2 修订（2026-10-27）\n修复菜单栏图标点击崩溃、缺失 BPM 后台补算与沙滩电音节普通音乐人物动作。显示版本保持 1.1.2；Windows、Android 包不变。旧 Mac 包以 `.original-20261016` 后缀保留，原 tag 不移动。应用私有源码提交：`'+d['sourceCommit']+'`。本包为 ad-hoc 签名，未经 Apple 公证。已安装 1.1.2 的用户请从官网下载覆盖安装。\n'
        body=release.get('body') or ''
        if '### macOS 1.1.2 修订（2026-10-27）' not in body:
            gh(f'/releases/{RELEASE_ID}','PATCH',json={'body':body+note})
        for f in latest['files']: verify_bytes(get(PREFIX+f['name']),f)
        sums=''.join(f['sha256']+'  '+f['name']+'\n' for f in latest['files']).encode()
        s3.put_object(Bucket=BUCKET,Key=PREFIX+'SHA256SUMS',Body=sums,ContentType='text/plain; charset=utf-8',CacheControl='no-cache')
        latest_raw=(json.dumps(latest,ensure_ascii=False,indent=2)+'\n').encode()
        for key in ('updates/macos/latest.json','updates/latest.json'):
            # Fail if another publisher has changed the shared release during this run.
            if json.loads(get('updates/latest.json')) not in (baseline,latest): raise ValueError('Concurrent latest update')
            s3.put_object(Bucket=BUCKET,Key=key,Body=latest_raw,ContentType='application/json; charset=utf-8',CacheControl='no-cache')
            if get(key)!=latest_raw: raise ValueError('Published manifest readback failed')
        snapshot()
        print(json.dumps({'version':'1.1.2','revision':REVISION,'sourceCommit':d['sourceCommit'],
                          'macSha256':d['sha256'],'otherPlatformsUnchanged':True,'historyPreserved':True}))

if __name__=='__main__': main()
