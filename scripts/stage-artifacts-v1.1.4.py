import hashlib,json,os,subprocess,tempfile
from pathlib import Path
from urllib.parse import quote,unquote,urlsplit
version='1.1.4'
tag='v'+version
bucket='agplayer-releases'
endpoint='https://50fb972f4425789ab744ba4c6614221e.r2.cloudflarestorage.com'
repo='gddjag/AgPlayer'
metadata=json.loads(Path('docs/releases/v1.1.4-verified-assets.json').read_text())
assert os.environ['GITHUB_REPOSITORY']==repo and len(metadata['files'])==8 and metadata['tag']==tag
assert os.environ.get('AWS_ACCESS_KEY_ID') and os.environ.get('AWS_SECRET_ACCESS_KEY'), 'R2 account is not authorized in this workflow'
def run(*args):return subprocess.check_output(args)
def aws(*args):return run('aws','--endpoint-url',endpoint,*args)
def digest(path):
 with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
draft=json.loads(run('gh','api',f'/repos/{repo}/releases/{metadata["githubReleaseId"]}'))
assert draft['tag_name']==tag
assets={a['name']:a for a in draft['assets']}
with tempfile.TemporaryDirectory() as td:
 target=Path(td)/'download'
 for item in metadata['files']:
  name=item['name']; checksum=item['sha256']
  assert name.startswith(('AgPlayer-1.1.4-','AgPlayer-Setup-1.1.4-')) and '/' not in name
  gh=unquote(urlsplit(item['githubUrl']).path.split('/')[-1])
  assert gh==name.replace('（内置伴奏分离模型版本）','-bundled-models')
  a=assets[gh]
  assert a['size']==item['size'] and a.get('digest') in ('sha256:'+checksum,None)
  with target.open('wb') as out:
   subprocess.run(['gh','api','-H','Accept: application/octet-stream',f'/repos/{repo}/releases/assets/{a["id"]}'],stdout=out,check=True)
  assert target.stat().st_size==item['size'] and digest(target)==checksum
  key=f'releases/{tag}/{name}'
  uri=f's3://{bucket}/{key}'
  ctype={'.exe':'application/octet-stream','.dmg':'application/x-apple-diskimage','.apk':'application/vnd.android.package-archive'}[Path(name).suffix]
  # Existing staged keys are allowed only if their bytes match.
  present=subprocess.run(['aws','--endpoint-url',endpoint,'s3api','head-object','--bucket',bucket,'--key',key],capture_output=True,text=True)
  if present.returncode:
   if '(404)' not in present.stderr and 'Not Found' not in present.stderr:raise RuntimeError(present.stderr)
   aws('s3','cp',str(target),uri,'--only-show-errors','--content-type',ctype,'--metadata','sha256='+checksum+',sourcecommit='+item['sourceCommit'])
  target.unlink()
  aws('s3','cp',uri,str(target),'--only-show-errors')
  assert target.stat().st_size==item['size'] and digest(target)==checksum
  if not present.returncode and json.loads(present.stdout).get('Metadata',{}).get('sha256')!=checksum:
   # Wrangler staged this file without user metadata. The remote bytes were
   # just verified against the GitHub asset before setting its digest.
   aws('s3api','copy-object','--bucket',bucket,'--key',key,'--copy-source',quote(bucket+'/'+key,safe='/'),
       '--metadata-directive','REPLACE','--content-type',ctype,
       '--metadata','sha256='+checksum+',sourcecommit='+item['sourceCommit'])
  target.unlink()
  head=json.loads(aws('s3api','head-object','--bucket',bucket,'--key',key))
  assert head['ContentLength']==item['size'] and head['Metadata']['sha256']==checksum
  print('Verified',key,checksum,flush=True)
 variants={k:metadata['variants'][k] for k in ('schemaVersion','kind','version','tag','files')}
 raw=(json.dumps(variants,indent=2,ensure_ascii=False)+'\n').encode()
 target.write_bytes(raw)
 for key in (f'releases/{tag}/desktop-variants.json',):
  aws('s3','cp',str(target),f's3://{bucket}/{key}','--only-show-errors',
      '--content-type','application/json; charset=utf-8','--cache-control','no-cache')
  assert raw==aws('s3','cp',f's3://{bucket}/{key}','-','--only-show-errors')

