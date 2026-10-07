"""Package checked experimental lighting without editing the public catalog."""
from pathlib import Path
import hashlib
import json
import shutil
import xml.etree.ElementTree as ET
import zipfile

root = Path(__file__).resolve().parents[2]
out = root / 'release/AnyGraphics-0.29.0-Candidate'
packages = out / 'manager-packages'
packages.mkdir(parents=True, exist_ok=True)
tests_path = root / 'build/lighting-tests.xml'
tests = ET.parse(tests_path).getroot()
if int(tests.get('tests', 0)) != 38 or int(tests.get('failures', 0)) or int(tests.get('errors', 0)):
    raise SystemExit('All 38 checks must pass before packaging.')
catalog = json.loads((root / 'catalog.json').read_text(encoding='utf-8-sig'))
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
for package in catalog['Api'] + catalog['Mods']:
    if package['Id'] in ('anyapi', 'anygraphics'):
        package['Version'] = '0.29.0'
        package['Revision'] = 29
        if package['Id'] == 'anygraphics':
            package['MinimumApi'] = 29
        name = package['Name'] + '-' + package['Version'] + '.zip'
        member = next(iter(package['FileHashes']))
        dll = root / 'build/Release' / ('dinput8.dll' if package['Id'] == 'anyapi' else 'AnyGraphics.dll')
        with zipfile.ZipFile(packages / name, 'w', zipfile.ZIP_DEFLATED) as archive:
            archive.write(dll, member)
        package['Sha256'] = sha(packages / name)
        package['FileHashes'] = {member: sha(dll)}
        package['Url'] = 'https://github.com/sYx-tv/AnyAPI-Modding/releases/download/v0.29.0/' + name
        package['Description'] = 'Experimental HDR volumetric fog and sun shafts before bloom and HUD.'
    else:
        name = package['Name'] + '-' + package['Version'] + '.zip'
        shutil.copy2(root / 'manager/publishing/assets' / name, packages / name)
    assert sha(packages / name) == package['Sha256']
    with zipfile.ZipFile(packages / name) as archive:
        for member, digest in package['FileHashes'].items():
            assert hashlib.sha256(archive.read(member)).hexdigest() == digest
(packages / 'catalog.json').write_text(json.dumps(catalog, indent=2) + '\n')
(out / 'checks.json').write_text(json.dumps(dict(
    NativeTests=38, AllPassed=True, LocalInstallation='PENDING',
    VisualAcceptance='PENDING', FpsMeasurements='PENDING', VolumetricGpuCases=49,
    Limitations=['Up to eight native sun cascades; no temporal accumulation', 'Native-world visual acceptance pending',
                 'No point/spot light volumes or temporal accumulation'],
    Files={k: v for p in catalog['Api'] + catalog['Mods'] for k, v in p['FileHashes'].items()}
), indent=2) + '\n')
shutil.copy2(tests_path, out / 'tests.xml')
print('Verified lighting candidate:', out)
