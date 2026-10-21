"""Plan the release that follows a game-update port: versions, packages and catalog changes.

After `game_update.py --apply`, this tool:

  1. finds the shipped mods that must be rebuilt: those on the experimental SDK (their source
     includes a `*_bindings.h` or `anyapi_experimental.hpp`, so the DLL embeds the old build's
     hashes) and those that include a file whose game patterns the port rewrote (from
     <out>/audit/pattern_audit.json). With --apply, each gets a patch version bump in
     native/CMakeLists.txt;
  2. writes <out>/release-plan.json: the tag, the new game build, every package to ship (AnyAPI plus
     those mods: name, version, DLL path under the build dir and ZIP entry; a bundle ships from
     bundles/<config>/ and replaces the standalone target of the same name) and the release.json "Packages" overrides that
     list only the new build, with descriptions moved to the new game version;
  3. prints the add_game_build.py command that adds the new build to every other (service-only)
     mod in the catalog.

    python native/tools/port_release_plan.py --version 0.1.25 --steam-build 25900000
        --exe <sha256> --gcl <sha256> --api-version 0.37.0 [--out build/game-update] [--apply]

The laptop publish step packages exactly what release-plan.json lists.
"""
import argparse
import json
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NATIVE = ROOT / 'native'
CMAKE = NATIVE / 'CMakeLists.txt'


INCLUDE_DIRS = [NATIVE, ROOT / 'sdk' / 'experimental' / 'include', ROOT / 'sdk' / 'include']


def includes(src, seen=None):
    """src (a path relative to native/) and every file it includes, recursively, as paths relative to native/.
    Includes resolve like the build: beside the including file first, then native/ and the SDK include dirs."""
    seen = seen if seen is not None else set()
    if src in seen:
        return seen
    seen.add(src)
    path = (NATIVE / src).resolve()
    if not path.is_file():
        return seen
    for inc in re.findall(r'#include "([\w./]+)"', path.read_bytes().decode('utf-8', 'replace')):
        for base in [path.parent] + INCLUDE_DIRS:
            if (base / inc).is_file():
                includes(os.path.relpath((base / inc).resolve(), NATIVE).replace(os.sep, '/'), seen)
                break
    return seen


def shipped_targets(cmake):
    """(CMake target, catalog name, sources, DLL path under the build dir) for every mod DLL. A bundle ships under its
    output name from bundles/<config>/; a standalone target whose product a bundle also ships is not released."""
    bundles = [(t, out, srcs.split(), 'bundles/Release/%s.dll' % out)
               for t, out, srcs in re.findall(r'anyapi_bundle\((\w+) (\w+) ([^)]*)\)', cmake)]
    outputs = {b[1] for b in bundles}
    standalone = []
    for target, srcs in re.findall(r'add_library\((\w+) SHARED ([^)]*)\)', cmake):
        product = re.search(r'anyapi_version_resource\(%s (\w+)\)' % target, cmake)
        name = product[1] if product else target
        if target != 'dinput8' and name not in outputs:
            standalone.append((target, name, srcs.split(), 'Release/%s.dll' % name))
    return standalone + bundles


def targets_to_rebuild(cmake, touched):
    """(target, name, dll) -> reason, for every mod DLL that embeds the game build or code the port changed."""
    out = {}
    for target, name, srcs, dll in shipped_targets(cmake):
        files = set()
        for src in srcs:
            if src.endswith('.cpp'):
                includes(src, files)
        if any(re.search(r'_bindings\.h$|anyapi_experimental\.hpp$', f) for f in files):
            out[target, name, dll] = 'experimental SDK'
        elif files & touched:
            out[target, name, dll] = 'port changed ' + ', '.join(sorted(files & touched))
    return out


def version_block(cmake, target):
    return re.search(r'(?:if|elseif)\(target STREQUAL "%s"\)\r?\n    set\(ANYAPI_VERSION_COMMA "([\d,]+)"\)\r?\n'
                     r'    set\(ANYAPI_VERSION_TEXT "([\d.]+)"\)' % target, cmake)


def add_version_block(cmake, target, version):
    """A bundle without its own block builds as the 1.0.0 fallback; give it a block just before that fallback."""
    m = re.search(r'  elseif\(target STREQUAL "AnyClock" OR target MATCHES "Bundle\$"\)\r?\n', cmake)
    if not m:
        sys.exit('no version block for %s in native/CMakeLists.txt' % target)
    nl = '\r\n' if '\r\n' in m[0] else '\n'
    block = ('  elseif(target STREQUAL "%s")' + nl + '    set(ANYAPI_VERSION_COMMA "%s,0")' + nl +
             '    set(ANYAPI_VERSION_TEXT "%s")' + nl) % (target, version.replace('.', ','), version)
    return cmake[:m.start()] + block + cmake[m.start():]


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

    packages = [dict(name='AnyAPI', id='anyapi', version=a.api_version, dll='Release/dinput8.dll', entry='dinput8.dll')]
    overrides = {'AnyAPI': {'GameBuilds': [build], 'Description': old_api['Description'].replace(previous, a.version)}}
    skipped = []
    touched = set()
    audit = a.out / 'audit' / 'pattern_audit.json'
    if audit.is_file():
        touched = {r['file'] for r in json.loads(audit.read_text(encoding='utf-8'))['patterns']
                   if r['status'] in ('refreshable', 'resized', 'shifted', 'changed', 'moved')}
    for (target, name, dll), reason in sorted(targets_to_rebuild(cmake, touched).items()):
        entry = by_name.get(name)
        if not entry:
            skipped.append(name)  # built from source only (not in the catalog), nothing to release
            continue
        major, minor, patch = (int(x) for x in entry['Version'].split('.'))
        m = version_block(cmake, target)
        if m and tuple(int(x) for x in m[2].split('.')) > (major, minor, patch):
            new = m[2]  # already bumped (a re-run, or an unreleased change); ship what the build says
        else:
            new = '%d.%d.%d' % (major, minor, patch + 1)
            if m:
                cmake = cmake[:m.start(1)] + new.replace('.', ',') + ',0' + cmake[m.end(1):m.start(2)] + new + cmake[m.end(2):]
            elif target.endswith('Bundle'):
                cmake = add_version_block(cmake, target, new)
            else:
                sys.exit('no version block for %s in native/CMakeLists.txt' % target)
        packages.append(dict(name=name, id=entry['Id'], version=new, dll=dll,
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
