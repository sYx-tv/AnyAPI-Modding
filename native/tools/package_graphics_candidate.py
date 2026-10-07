"""Refresh the local native-graphics candidate without changing public releases."""
from pathlib import Path
import hashlib
import json
import shutil
import xml.etree.ElementTree as ET
import zipfile

root = Path(__file__).resolve().parents[2]
out = root / 'release/AnyGraphics-0.28.0-Candidate'
packages = out / 'manager-packages'
record = root / 'build/graphics-quality-tests.xml'
tests = ET.parse(record).getroot()
if int(tests.get('tests', 0)) != 35 or int(tests.get('failures', 0)) or int(tests.get('errors', 0)):
    raise SystemExit('All 35 native checks must pass before candidate packaging.')
hash_file = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
catalog_path = packages / 'catalog.json'
catalog = json.loads(catalog_path.read_text(encoding='utf-8-sig'))
for package in catalog['Api'] + catalog['Mods']:
    if package['Id'] not in ('anyapi', 'anygraphics'):
        continue
    file_name = 'dinput8.dll' if package['Id'] == 'anyapi' else 'AnyGraphics.dll'
    source = root / 'build/Release' / file_name
    relative = next(iter(package['FileHashes']))
    path = packages / (package['Name'] + '-' + package['Version'] + '.zip')
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as archive:
        archive.write(source, relative)
    package['Sha256'] = hash_file(path)
    package['FileHashes'] = {relative: hash_file(source)}
    package['Description'] = ('Native mod framework with scene antialiasing before HUD.'
        if package['Id'] == 'anyapi' else 'Native graphics options and scene SMAA before HUD.')
    with zipfile.ZipFile(path) as archive:
        assert len(archive.infolist()) == 1
        assert hashlib.sha256(archive.read(relative)).hexdigest() == package['FileHashes'][relative]
catalog_path.write_text(json.dumps(catalog, indent=2) + '\n', encoding='utf-8')
checks_path = out / 'checks.json'
checks = json.loads(checks_path.read_text(encoding='utf-8-sig'))
checks.update(NativeTests=35, AllPassed=True, LocalInstallation='PENDING',
    SceneAABoundaryLiveAcceptance='ACCEPTED: 2560x1440 source in loaded world; original composition before native UI',
    ReplacementAA='SMAA_AND_ENHANCED_SMAA_IMPLEMENTED_BEFORE_HUD; in-game visual/FPS acceptance pending',
    SMAAGpuValidation='PASSED: 15 cases, explicit reflected sampler bindings, enhanced edge blending, preserved straight edges, all quality levels, smooth edges, flat colours, crisp restored HUD, repeated frames, resizing and RGBA/BGRA',
    SettingsCount=20, VisualAcceptance='PENDING: Enhanced SMAA and native clouds/grass/foliage', FpsMeasurements='PENDING')
checks['Files'] = {relative: digest for package in catalog['Api'] + catalog['Mods'] for relative, digest in package['FileHashes'].items()}
checks_path.write_text(json.dumps(checks, indent=2) + '\n', encoding='utf-8')
shutil.copy2(record, out / 'tests.xml')
(out / 'README.md').write_text('''# AnyGraphics native-rendering test build

AnyAPI and AnyGraphics 0.28.0 for Anymaker 0.1.23 / Steam 25755694.
All 35 automated checks pass. The pre-HUD scene boundary was observed in a
loaded world. Enhanced SMAA and scene-detail visual/FPS acceptance is pending.

Settings > Graphics > AnyGraphics · Modded > Antialiasing > Enhanced SMAA (scene).
Choose SMAA quality and Apply Changes. Compare Off, FXAA and SMAA while looking
at the same building/vehicle edges. Check HUD text stays sharp. Switch AA off,
change resolution, and check the normal game settings still work.
Under Advanced options, test Cloud rendering, Grass rendering and Foliage
rendering (Game setting / Off / On). Apply/Cancel use the game's own buttons.

Use the candidate installer to preserve manager receipts and a rollback backup.
Only the API and AnyGraphics have changed; four other mod packages remain
0.27.0. The public GitHub catalog is unchanged pending live acceptance.
''', encoding='utf-8')
print('Verified native-graphics candidate:', out)
