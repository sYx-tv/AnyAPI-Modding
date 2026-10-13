"""Plan the release that follows a game-update port: versions, packages and catalog changes.

After `game_update.py --apply`, this tool:

  1. finds the shipped mods that must be rebuilt: those on the experimental SDK (their source
     includes a `*_bindings.h` or `anyapi_experimental.hpp`, so the DLL embeds the old build's
     hashes) and those that include a file whose game patterns the port rewrote (from
     <out>/audit/pattern_audit.json). With --apply, each gets a patch version bump in
     native/CMakeLists.txt;
  2. writes <out>/release-plan.json: the tag, the new game build, every package to ship (AnyAPI plus
     those mods: name, version, DLL and ZIP entry) and the release.json "Packages" overrides that
     list only the new build, with descriptions moved to the new game version;
  3. prints the add_game_build.py command that adds the new build to every other (service-only)
     mod in the catalog.

    python native/tools/port_release_plan.py --version 0.1.25 --steam-build 25900000
        --exe <sha256> --gcl <sha256> --api-version 0.37.0 [--out build/game-update] [--apply]

The laptop publish step packages exactly what release-plan.json lists.
"""
import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NATIVE = ROOT / 'native'
CMAKE = NATIVE / 'CMakeLists.txt'


def includes(src, seen=None):
    """src and every local file it includes, recursively."""
    seen = seen if seen is not None else set()
    if src in seen or not (NATIVE / src).is_file():
        return seen
    seen.add(src)
    for inc in re.findall(r'#include "([\w./]+)"', (NATIVE / src).read_bytes().decode('utf-8', 'replace')):
        includes(inc, seen)
    return seen


def targets_to_rebuild(cmake, touched):
    """CMake target -> reason, for every mod DLL that embeds the game build or code the port changed."""
    out = {}
    for target, src in re.findall(r'add_library\((\w+) SHARED (\w+\.cpp)', cmake):
        if target == 'dinput8':
            continue
        files = includes(src)
        if any(re.search(r'_bindings\.h$|anyapi_experimental\.hpp$', f) for f in files):
            out[target] = 'experimental SDK'
        elif files & touched:
            out[target] = 'port changed ' + ', '.join(sorted(files & touched))
    return out


def version_block(cmake, target):
    m = re.search(r'target STREQUAL "%s"\)\r?\n    set\(ANYAPI_VERSION_COMMA "([\d,]+)"\)\r?\n    set\(ANYAPI_VERSION_TEXT "([\d.]+)"\)' % target, cmake)
    return m


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--version', required=True, help='new game version, e.g. 0.1.25')
    ap.add_argument('--steam-build', type=int, required=True)
    ap.add_argument('--exe', required=True)
    ap.add_argument('--gcl', required=True)
    ap.add_argument('--api-version', required=True, help='the ported AnyAPI version')
    ap.add_argument('--out', type=Path, default=ROOT / 'build' / 'game-update')
    ap.add_argument('--apply', action='store_true', help='bump the mod versions in native/CMakeLists.txt')
    a = ap.parse_args()
    build = {'Version': a.version, 'SteamBuild': a.steam_build, 'ExeSha256': a.exe.lower(), 'GclSha256': a.gcl.lower()}
    catalog = json.loads((ROOT / 'catalog.json').read_bytes().decode('utf-8'))
    by_name = {m['Name']: m for m in catalog['Mods']}
    old_api = catalog['Api'][0]
    previous = sorted({b['Version'] for b in old_api['GameBuilds']})[-1]
    raw = CMAKE.read_bytes().decode('utf-8')
    cmake = raw

    packages = [dict(name='AnyAPI', id='anyapi', version=a.api_version, dll='dinput8.dll', entry='dinput8.dll')]
    overrides = {'AnyAPI': {'GameBuilds': [build], 'Description': old_api['Description'].replace(previous, a.version)}}
    skipped = []
    touched = set()
    audit = a.out / 'audit' / 'pattern_audit.json'
    if audit.is_file():
        touched = {r['file'] for r in json.loads(audit.read_text(encoding='utf-8'))['patterns']
                   if r['status'] in ('refreshable', 'resized', 'shifted', 'changed', 'moved')}
    for target, reason in sorted(targets_to_rebuild(cmake, touched).items()):
        product = re.search(r'anyapi_version_resource\(%s (\w+)\)' % target, cmake)
        name = product[1] if product else target
        entry = by_name.get(name)
        if not entry:
            skipped.append(name)  # built from source only (not in the catalog), nothing to release
            continue
        m = version_block(cmake, target)
        if not m:
            sys.exit('no version block for %s in native/CMakeLists.txt' % target)
        built = m[2]
        major, minor, patch = (int(x) for x in entry['Version'].split('.'))
        if tuple(int(x) for x in built.split('.')) > (major, minor, patch):
            new = built  # already bumped (a re-run, or an unreleased change); ship what the build says
        else:
            new = '%d.%d.%d' % (major, minor, patch + 1)
            cmake = cmake[:m.start(1)] + new.replace('.', ',') + ',0' + cmake[m.end(1):m.start(2)] + new + cmake[m.end(2):]
        packages.append(dict(name=name, id=entry['Id'], version=new, dll=name + '.dll',
                             entry='AnyAPI and Modding/mods/%s.dll' % name, previous=entry['Version'], reason=reason))
        overrides[name] = {'GameBuilds': [build], 'Description': entry['Description'].replace(previous, a.version)}

    shipped = {p['id'] for p in packages}
    service_only = [m['Name'] for m in catalog['Mods'] if m['Id'] not in shipped]
    plan = dict(tag='v' + a.api_version, game_build=build, previous_game_version=previous, packages=packages,
                release_json={'Packages': overrides}, service_only_mods=service_only, source_only=skipped)
    a.out.mkdir(parents=True, exist_ok=True)
    (a.out / 'release-plan.json').write_text(json.dumps(plan, indent=2) + '\n', encoding='utf-8')

    print('release %s for Anymaker %s / Steam build %d:' % (plan['tag'], a.version, a.steam_build))
    for p in packages:
        print('  %-12s %s%s' % (p['name'], p['version'], '  (was %s; %s)' % (p['previous'], p['reason']) if 'previous' in p else ''))
    if skipped:
        print('  not in the catalog, not released: ' + ', '.join(skipped))
    print('service-only mods keep their ZIPs and gain the new build at release time:')
    print('  python native/tools/add_game_build.py --version %s --steam-build %d --exe %s --gcl %s --skip %s --apply'
          % (a.version, a.steam_build, build['ExeSha256'], build['GclSha256'], ','.join(sorted(shipped - {'anyapi'}))))
    print('wrote %s' % (a.out / 'release-plan.json'))
    if cmake != raw:
        print('%s native/CMakeLists.txt mod versions' % ('bumped' if a.apply else 'would bump'))
        if a.apply:
            CMAKE.write_bytes(cmake.encode('utf-8'))


if __name__ == '__main__':
    main()
