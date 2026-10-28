# coding: utf-8
"""Repair only the pinned, newly uploaded Windows variant name; never touch old assets."""
import json,os,urllib.request
from pathlib import Path
API='https://api.github.com/repos/gddjag/AgPlayer'
ASSET_ID=685766331
SHA='sha256:392af905b0a96daf68783901afe28b0c085fe7d055c59e1e135ccc1f938aaade'
TARGET='AgPlayer-Setup-1.1.2-x64-arm64-bundled-models.exe'
DISPLAY_NAME='AgPlayer-Setup-1.1.2-x64-arm64（内置伴奏分离模型版本）.exe'
def api(path, data=None):
    body=None if data is None else json.dumps(data,ensure_ascii=False).encode('utf-8')
    request=urllib.request.Request(API+path,data=body,method='GET' if data is None else 'PATCH',headers={'Authorization':'Bearer '+os.environ['GH_TOKEN'],'Accept':'application/vnd.github+json','Content-Type':'application/json','X-GitHub-Api-Version':'2022-11-28'})
    with urllib.request.urlopen(request,timeout=60) as response:return json.load(response)
def main():
    before=api('/releases/414882152/assets?per_page=100')
    asset=next(a for a in before if a['id']==ASSET_ID)
    assert asset['name'] in ['AgPlayer-Setup-1.1.2-x64-arm64.exe',TARGET]
    assert asset['size']==281176468 and asset['digest']==SHA and asset['state']=='uploaded'
    preserved={a['id']:{k:a[k] for k in ['name','size','state','digest']} for a in before if a['id']!=ASSET_ID}
    result=api('/releases/assets/'+str(ASSET_ID),dict(name=TARGET,label=DISPLAY_NAME))
    after=api('/releases/414882152/assets?per_page=100')
    assert {a['id']:{k:a[k] for k in ['name','size','state','digest']} for a in after if a['id']!=ASSET_ID}==preserved
    actual=next(a for a in after if a['id']==ASSET_ID)
    assert actual['size']==281176468 and actual['digest']==SHA and actual['name']==TARGET and actual['label']==DISPLAY_NAME
    receipt=dict(assetId=ASSET_ID,requestedName=TARGET,actualName=actual['name'],githubAsciiNameVerified=actual['name']==TARGET,fullChineseLabel=actual['label'],sha256=SHA,allOtherAssetsUnchanged=True)
    Path('variant-name-repair.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(receipt,ensure_ascii=False))
if __name__=='__main__':main()
