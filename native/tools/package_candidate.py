"""Package a tested local candidate without changing the stable download catalog."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile

root = Path(__file__).resolve().parents[2]
build = root / 'build'
tests = ET.parse(build / 'api-027-tests.xml').getroot()
if int(tests.get('tests', '0')) != 32 or int(tests.get('failures', '0')):
    raise SystemExit('A passing complete candidate test record is required.')
out = root / 'release' / 'AnyAPI-0.27.0-Candidate'
out.mkdir(parents=True, exist_ok=True)
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
manifest_path = root / 'native' / 'BUILD_MANIFEST.json'
manifest = json.loads(manifest_path.read_text(encoding='utf-8-sig'))
manifest.update(revision=27, api_version='0.27.0', status='CANDIDATE',
    game_version='0.1.23', steam_build_id=25755694, tests_passed=32,
    game_identity_sha256={
        'game.exe': '97ea559fb630c9beff217824fb91eab31497af20394d12276a533846efb9f610',
        'bin/game.gcl': '17cc55267fc865c76de33723f876cf5c64d9bedda443fead65a7d23c417c0aa1'},
    new_services=['anyapi.build v1', 'anyapi.client_tasks v1'],
    native_patterns_audited=112, native_patterns_refreshed=17,
    current_profile_gameplay_acceptance='PENDING',
    previous_profile_acceptance='Existing acceptance fields refer to earlier game profiles.',
    host_sha256=digest(build / 'Release' / 'dinput8.dll'))
manifest.pop('new_service', None)
for name, key in [('AnyMap', 'map_sha256'), ('AnyGraphics', 'graphics_sha256'), ('AnyHelpers', 'helpers_sha256')]:
    manifest[key] = digest(build / 'Release' / (name + '.dll'))
manifest_path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
caps_path = root / 'native' / 'CURRENT_CAPABILITIES.json'
caps = json.loads(caps_path.read_text(encoding='utf-8-sig'))
caps.update(game_version='0.1.23', steam_build_id=25755694,
    current_profile_acceptance='CANDIDATE_AUTOMATED_TESTS_PASSED_GAMEPLAY_PENDING')
caps['base_api']['anyapi_build_v1.h'] = dict(version=1, service='anyapi.build',
    capabilities=['copied supported profile and actual file hashes', 'exact build guard status'],
    validation='automated service ABI checks; runtime acceptance pending')
caps['base_api']['anyapi_client_tasks_v1.h'] = dict(version=1, service='anyapi.client_tasks',
    capabilities=['owned bounded client tick callbacks', 'world epoch filtering', 'pending ticket cancellation'],
    limits=dict(pending=128, per_sample=32),
    limitations=['client only', 'nonblocking callbacks', 'no hot unload', 'discarded jobs have no callback'],
    validation='queue, initialization ownership, epochs and service fixtures')
caps_path.write_text(json.dumps(caps, indent=2) + '\n', encoding='utf-8')
subprocess.run([sys.executable, str(root / 'manager' / 'prepare_guide.py'),
    '--output', str(out / 'developer')], check=True)
shutil.copy2(build / 'api-027-tests.xml', out / 'tests.xml')
shutil.copy2(manifest_path, out / 'build-manifest.json')
readme = '''# AnyAPI 0.27.0 candidate

Built for Anymaker 0.1.23, Steam build 25755694, Windows x64.
All 32 automated native checks pass. In-game validation is pending.

This is a development package. The public manager download feed is unchanged.
The API ZIP contains only dinput8.dll. The optional mod ZIP contains rebuilt
AnyHelpers, AnyInventory, AnyStorage, AnyMap and AnyGraphics DLLs.
The developer folder contains the matching source-only starter SDK and guide.

Close Anymaker before testing. Back up the existing loader, mods and manager
receipts together. The loader belongs beside game.exe; mod DLLs belong in
AnyAPI and Modding/mods. These archives do not update manager compatibility
receipts automatically. Use a separate test installation or a matching candidate
catalog rather than overwriting a manager-controlled installation blindly.

Test settings and controls, item search, storage transfers/sort, map/minimap and
graphics in a copied save. Check framework logs for rejected native contracts.
Joining-client and server validation remain separate work.
'''
(out / 'README.md').write_text(readme, encoding='utf-8')
for filename, entries in [
    ('AnyAPI-0.27.0-Candidate.zip', [('dinput8.dll', 'dinput8.dll')]),
    ('Optional-Mods-0.27.0-Candidate.zip', [(n + '.dll', 'AnyAPI and Modding/mods/' + n + '.dll')
        for n in ['AnyHelpers', 'AnyInventory', 'AnyStorage', 'AnyMap', 'AnyGraphics']])]:
    with zipfile.ZipFile(out / filename, 'w', zipfile.ZIP_DEFLATED) as archive:
        for binary, destination in entries:
            archive.write(build / 'Release' / binary, destination)
        archive.writestr('CANDIDATE.md', readme)
files = sorted(p for p in out.rglob('*') if p.is_file() and p.name != 'checksums.json')
(out / 'checksums.json').write_text(json.dumps({p.relative_to(out).as_posix(): digest(p)
    for p in files}, indent=2) + '\n', encoding='utf-8')
print('Candidate package:', out)
