"""Create separate manager-compatible packages for the current compatibility candidate."""
from pathlib import Path
import hashlib
import json
import zipfile

root = Path(__file__).resolve().parents[2]
manifest = json.loads((root / 'native/BUILD_MANIFEST.json').read_text())
if manifest['revision'] != 27 or manifest['tests_passed'] != 32:
    raise SystemExit('Package the passing API candidate first.')
out = root / 'release/AnyAPI-0.27.0-Candidate/manager-packages'
out.mkdir(parents=True, exist_ok=True)
build = dict(Version=manifest['game_version'], SteamBuild=manifest['steam_build_id'],
    ExeSha256=manifest['game_identity_sha256']['game.exe'],
    GclSha256=manifest['game_identity_sha256']['bin/game.gcl'])
projects = [
    ('anyapi', 'AnyAPI', 'dinput8.dll', 'Current-build in-game mod framework.'),
    ('anyhelpers', 'AnyHelpers', 'AnyHelpers.dll', 'Native Mod Controls and Mod Settings.'),
    ('anyinventory', 'AnyInventory', 'AnyInventory.dll', 'Item search, previews, favorites and mode-limited Add.'),
    ('anystorage', 'AnyStorage', 'AnyStorage.dll', 'Storage transfers, matching stacks and sorting.'),
    ('anymap', 'AnyMap', 'AnyMap.dll', 'World map, smooth minimap, markers and road guidance.'),
    ('anygraphics', 'AnyGraphics', 'AnyGraphics.dll', 'Graphics presets, bloom, sharpening and color controls.')]
catalog = dict(Schema=1, Repository='https://github.com/sYx-tv/AnyAPI-Modding', Api=[], Mods=[])
sha = lambda data: hashlib.sha256(data).hexdigest()
for ident, name, binary, description in projects:
    data = (root / 'build/Release' / binary).read_bytes()
    target = binary if ident == 'anyapi' else 'AnyAPI and Modding/mods/' + binary
    package = out / (name + '-0.27.0.zip')
    with zipfile.ZipFile(package, 'w', zipfile.ZIP_DEFLATED) as archive:
        info = zipfile.ZipInfo(target, date_time=(2026, 10, 6, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        archive.writestr(info, data)
    entry = dict(Id=ident, Name=name, Version='0.27.0', Revision=27,
        MinimumApi=0 if ident == 'anyapi' else 27,
        Description=description + ' Anymaker 0.1.23 compatibility candidate; in-game acceptance pending.',
        Url='', Sha256=sha(package.read_bytes()), FileHashes={target: sha(data)}, GameBuilds=[build])
    catalog['Api' if ident == 'anyapi' else 'Mods'].append(entry)
(out / 'catalog.json').write_text(json.dumps(catalog, indent=2) + '\n')
candidate = out.parent
files = sorted(p for p in candidate.rglob('*') if p.is_file()
    and p.name != 'checksums.json' and 'install-backups' not in p.parts)
(candidate / 'checksums.json').write_text(json.dumps({p.relative_to(candidate).as_posix(): sha(p.read_bytes())
    for p in files}, indent=2) + '\n')
print('Prepared API and five independent mod packages:', out)
