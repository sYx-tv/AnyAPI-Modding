"""Package the tested clock candidate without changing the public catalog."""
from pathlib import Path
import hashlib, json, shutil, zipfile
import xml.etree.ElementTree as ET
root = Path(__file__).resolve().parents[2]
checks = ET.parse(root / 'build/clock-tests.xml').getroot()
if int(checks.get('tests', 0)) < 42 or int(checks.get('failures', 0)) or int(checks.get('errors', 0)):
    raise SystemExit('The complete native suite must pass before packaging.')
out = root / 'release/AnyClock-1.0.0-Candidate'
packages = out / 'manager-packages'
packages.mkdir(parents=True, exist_ok=True)
catalog = json.loads((root / 'catalog.json').read_text(encoding='utf-8-sig'))
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
api = catalog['Api'][0]
api.update(Version='0.30.0', Revision=30, Description='Native mod framework with copied world time, scene antialiasing and volumetric lighting.')
clock = dict(Id='anyclock', Name='AnyClock', Version='1.0.0', Revision=1, MinimumApi=30,
             Description='Press a configurable key for a temporary native game-time HUD.',
             FileHashes={'AnyAPI and Modding/mods/AnyClock.dll': ''}, GameBuilds=api['GameBuilds'])
catalog['Mods'].append(clock)
for p in catalog['Api'] + catalog['Mods']:
    name = p['Name'] + '-' + p['Version'] + '.zip'
    if p['Id'] in ('anyapi', 'anyclock'):
        dll = root / 'build/Release' / ('dinput8.dll' if p['Id'] == 'anyapi' else 'AnyClock.dll')
        member = next(iter(p['FileHashes']))
        with zipfile.ZipFile(packages / name, 'w', zipfile.ZIP_DEFLATED) as z:
            z.write(dll, member)
        p['Sha256'] = sha(packages / name)
        p['FileHashes'] = {member: sha(dll)}
        p['Url'] = 'https://github.com/sYx-tv/AnyAPI-Modding/releases/download/v0.30.0/' + name
    else:
        shutil.copy2(root / 'manager/publishing/assets' / name, packages / name)
    assert sha(packages / name) == p['Sha256']
    with zipfile.ZipFile(packages / name) as z:
        for member, digest in p['FileHashes'].items():
            assert hashlib.sha256(z.read(member)).hexdigest() == digest
(packages / 'catalog.json').write_text(json.dumps(catalog, indent=2) + '\n')
shutil.copy2(root / 'build/clock-tests.xml', out / 'tests.xml')
shutil.copy2(root / 'docs/mods/clock.md', out / 'README.md')
(out / 'checks.json').write_text(json.dumps(dict(NativeTests=int(checks.get('tests')), AllPassed=True,
    LocalInstallation='PENDING', GameplayAcceptance='PENDING', PublicCatalogUpdated=False), indent=2) + '\n')
print('Checksum-verified candidate:', out)
