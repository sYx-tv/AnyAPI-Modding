"""Add a new game build's fingerprint to catalog packages whose DLL did not change.

After a game update, mods that only use AnyAPI services keep working with the new API build,
so their existing ZIPs stay as they are and only gain the new fingerprint:

    python native/tools/add_game_build.py --version 0.1.24 --steam-build 25826614
        --exe <game.exe sha256> --gcl <game.gcl sha256> [--skip enginesound,anymirror] [--apply]

Packages built on the experimental SDK embed the old build's hashes and are skipped by default
(--skip); release them again instead. The API is skipped too: publish the ported API with its
own GameBuilds (see --release-json). With --release-json the tool also prints the release.json
"Packages" overrides that make a new release list only the new build.
"""
import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EXPERIMENTAL = 'enginesound,anymirror,anylights'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--catalog', type=Path, default=ROOT / 'catalog.json')
    ap.add_argument('--version', required=True)
    ap.add_argument('--steam-build', type=int, required=True)
    ap.add_argument('--exe', required=True)
    ap.add_argument('--gcl', required=True)
    ap.add_argument('--skip', default=EXPERIMENTAL, help='comma-separated package ids to leave alone')
    ap.add_argument('--release-json', metavar='NAMES', help='comma-separated package names to print overrides for')
    ap.add_argument('--apply', action='store_true')
    a = ap.parse_args()
    build = {'Version': a.version, 'SteamBuild': a.steam_build, 'ExeSha256': a.exe.lower(), 'GclSha256': a.gcl.lower()}
    raw = a.catalog.read_bytes().decode('utf-8')
    catalog = json.loads(raw)
    skip = {s.strip() for s in a.skip.split(',') if s.strip()}
    added = []
    for entry in catalog['Mods']:
        if entry['Id'] in skip:
            print('  skipped %s' % entry['Name'])
            continue
        if any(b['ExeSha256'] == build['ExeSha256'] and b['GclSha256'] == build['GclSha256'] for b in entry['GameBuilds']):
            continue
        if len(entry['GameBuilds']) >= 32:
            sys.exit('%s already lists 32 game builds' % entry['Name'])
        entry['GameBuilds'].append(dict(build))
        added.append(entry['Name'])
    print('%s %s to: %s' % ('added' if a.apply else 'would add', a.version, ', '.join(added) or 'nothing'))
    if a.apply and added:
        text = json.dumps(catalog, indent=2, ensure_ascii=False) + '\n'
        if '\r\n' in raw:
            text = text.replace('\n', '\r\n')
        a.catalog.write_bytes(text.encode('utf-8'))
    if a.release_json:
        names = [n.strip() for n in a.release_json.split(',') if n.strip()]
        print(json.dumps({'Packages': {n: {'GameBuilds': [build]} for n in names}}, indent=2))


if __name__ == '__main__':
    main()
