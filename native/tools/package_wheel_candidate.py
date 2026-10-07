"""Package the local equipment-wheel test build; leave the public catalog alone."""
from pathlib import Path
import json, hashlib, shutil, zipfile
import xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[2]
tests=ET.parse(root/'build/wheel-tests.xml').getroot()
if int(tests.get('tests',0))<45 or int(tests.get('failures',0)) or int(tests.get('errors',0)):
 raise SystemExit('All native checks must pass before packaging.')
out=root/'release/AnyQuickWheel-1.0.0-Candidate';packages=out/'manager-packages';packages.mkdir(parents=True,exist_ok=True)
catalog=json.loads((root/'catalog.json').read_text(encoding='utf-8-sig'))
api=catalog['Api'][0];api.update(Version='0.31.0',Revision=31,Description='Native mod framework with copied equipment and queued native hotbar selection.')
catalog['Mods']=[p for p in catalog['Mods'] if p['Id']!='anyquickwheel']
catalog['Mods'].append(dict(Id='anyquickwheel',Name='AnyQuickWheel',Version='1.0.0',Revision=1,MinimumApi=31,
 Description='Hold a configurable key, point at a hotbar tool, and release to equip.',
 FileHashes={'AnyAPI and Modding/mods/AnyQuickWheel.dll':''},GameBuilds=api['GameBuilds']))
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for p in catalog['Api']+catalog['Mods']:
 name=p['Name']+'-'+p['Version']+'.zip';target=packages/name
 if p['Id'] in ('anyapi','anyquickwheel'):
  dll=root/'build/Release'/('dinput8.dll' if p['Id']=='anyapi' else 'AnyQuickWheel.dll');member=next(iter(p['FileHashes']))
  with zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED) as z:z.write(dll,member)
  p['Sha256']=sha(target);p['FileHashes']={member:sha(dll)};p['Url']='https://github.com/sYx-tv/AnyAPI-Modding/releases/download/v0.31.0/'+name
 else:shutil.copy2(root/'manager/publishing/assets'/name,target)
 assert sha(target)==p['Sha256']
 with zipfile.ZipFile(target) as z:
  for member,digest in p['FileHashes'].items():assert hashlib.sha256(z.read(member)).hexdigest()==digest
(packages/'catalog.json').write_text(json.dumps(catalog,indent=2)+'\n')
shutil.copy2(root/'build/wheel-tests.xml',out/'tests.xml');shutil.copy2(root/'docs/mods/quick-wheel.md',out/'README.md')
(out/'checks.json').write_text(json.dumps(dict(NativeTests=int(tests.get('tests')),AllPassed=True,LocalInstallation='PENDING',GameplayAcceptance='PENDING',PublicCatalogUpdated=False),indent=2)+'\n')
print('Checksum-verified wheel candidate:',out)
