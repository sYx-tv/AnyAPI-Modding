"""Package the local AnyBalance test build without changing the public catalog."""
from pathlib import Path
import json, hashlib, shutil, zipfile
import xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[2]
tests=ET.parse(root/'build/balance-tests.xml').getroot()
if int(tests.get('tests',0))<50 or int(tests.get('failures',0)) or int(tests.get('errors',0)):
    raise SystemExit('All native checks must pass before packaging.')
out=root/'release/AnyBalance-1.0.0-Candidate'
packages=out/'manager-packages'
packages.mkdir(parents=True,exist_ok=True)
published=root.parent/'graphics_clear_air_release'
catalog=json.loads((published/'catalog.json').read_text(encoding='utf-8-sig'))
api=catalog['Api'][0]
api.update(Version='0.33.0',Revision=33,Description='Native framework with Properties-tool creation balance snapshots.')
catalog['Mods']=[p for p in catalog['Mods'] if p['Id']!='anybalance']
catalog['Mods'].append(dict(Id='anybalance',Name='AnyBalance',Version='1.0.0',Revision=1,MinimumApi=33,
    Description='Native centre of mass, local balance offsets and bounds with Properties Tool equipped.',
    FileHashes={'AnyAPI and Modding/mods/AnyBalance.dll':''},GameBuilds=api['GameBuilds']))
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for entry in catalog['Api']+catalog['Mods']:
    name=entry['Name']+'-'+entry['Version']+'.zip'
    target=packages/name
    if entry['Id'] in ('anyapi','anybalance'):
        dll=root/'build/Release'/('dinput8.dll' if entry['Id']=='anyapi' else 'AnyBalance.dll')
        member=next(iter(entry['FileHashes']))
        with zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED) as z:z.write(dll,member)
        entry['Sha256']=sha(target)
        entry['FileHashes']={member:sha(dll)}
        entry['Url']='https://github.com/sYx-tv/AnyAPI-Modding/releases/download/v0.33.0/'+name
    else:shutil.copy2(published/'manager/publishing/assets'/name,target)
    assert sha(target)==entry['Sha256']
    with zipfile.ZipFile(target) as z:
        for member,digest in entry['FileHashes'].items():assert hashlib.sha256(z.read(member)).hexdigest()==digest
(packages/'catalog.json').write_text(json.dumps(catalog,indent=2)+'\n',encoding='utf-8')
shutil.copy2(root/'build/balance-tests.xml',out/'tests.xml')
shutil.copy2(root/'docs/mods/balance.md',out/'README.md')
(out/'checks.json').write_text(json.dumps(dict(NativeTests=int(tests.get('tests')),AllPassed=True,
    LocalInstallation='PENDING',GameplayAcceptance='PENDING',PublicCatalogUpdated=False),indent=2)+'\n')
print('Checksum-verified AnyBalance candidate:',out)
