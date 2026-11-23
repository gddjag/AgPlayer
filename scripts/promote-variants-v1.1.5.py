import json, os, subprocess
assert os.environ['GITHUB_REPOSITORY']=='gddjag/AgPlayer'
endpoint='https://50fb972f4425789ab744ba4c6614221e.r2.cloudflarestorage.com'
bucket='s3://agplayer-releases/'
def aws(*args):
 return subprocess.check_output(['aws','--endpoint-url',endpoint,*args])
release=json.loads(subprocess.check_output(['gh','api','repos/gddjag/AgPlayer/releases/454904181']))
assert release['tag_name']=='v1.1.5' and not release['draft']
assert json.loads(aws('s3','cp',bucket+'updates/latest.json','-','--only-show-errors'))['version']=='1.1.5'
versioned=bucket+'releases/v1.1.5/desktop-variants.json'
canonical=bucket+'updates/desktop-variants.json'
before=aws('s3','cp',versioned,'-','--only-show-errors')
assert json.loads(before)['version']=='1.1.5'
aws('s3','cp',versioned,canonical,'--only-show-errors','--metadata-directive','REPLACE','--content-type','application/json; charset=utf-8','--cache-control','no-cache')
assert before==aws('s3','cp',canonical,'-','--only-show-errors')
print('Published and verified desktop variants v1.1.5')
