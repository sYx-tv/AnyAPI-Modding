"""Port AnyAPI and the experimental-SDK mods to a new Anymaker build in one run.

    python native/tools/game_update.py --game-dir "C:/Program Files (x86)/Steam/steamapps/common/Anymaker"
        --previous-sdk <SDK root of the build the repo targets now> --work build/game-update
        [--api-version 0.35.0 --api-revision 36] [--apply]

The previous SDK root is the folder that holds json/, tools/ and validation/ for the current build
(for 0.1.23 / 25755694: the sdk-reference-2026.10.08 release, sdk/; for 0.1.24 / 25826614 the
work folder of this tool's previous run). Steps, stopping at the first failure:

  1. identify the build: version string in game.exe, Steam build id, SHA-256 of game.exe and game.gcl
  2. regenerate the experimental SDK into <work>/sdk (a copy of the previous root with the repo's
     tools; runtime evidence from the old build is set aside, runtime-only native RVAs are carried
     over only where their neighbourhood did not move)
  3. audit every exact byte pattern in native/ (game_update_audit.py) and refresh what is safe
  4. regenerate the legacy event-route offsets (refresh_legacy_routes.py)
  5. re-bind the experimental-SDK mods (rebind_mod_bindings.py)
  6. copy the generated SDK headers and system docs into sdk/experimental/
  7. write the build identity (runtime_build_identity.h, runtime_build_guard.inc, BUILD_MANIFEST.json)

Without --apply nothing in the repo changes: every step reports what it would do. Read
<work>/audit/pattern_audit.md afterwards. Patterns marked changed, moved or missing, and the offset
hints of shifted ones, still need a person: see docs/development/game-updates.md. Nothing here runs
game code, and a clean run is not acceptance: build, ctest and an in-game test still decide.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
SDK_TOOLS = ROOT / 'sdk' / 'experimental' / 'tools'
sys.path.insert(0, str(SDK_TOOLS))
from common import find_game_files, parse_vdf  # noqa: E402


def step(label):
    print('\n==> ' + label, flush=True)


def run(args):
    r = subprocess.run([sys.executable] + [str(a) for a in args])
    if r.returncode:
        sys.exit('FAILED: ' + ' '.join(str(a) for a in args))


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def rewrite(path, pairs, apply):
    """Regex replacements that keep the file's bytes (and CRLF) otherwise untouched."""
    text = path.read_bytes().decode('utf-8')
    new = text
    for pattern, repl in pairs:
        new, n = re.subn(pattern, repl, new)
        if n != 1:
            sys.exit('%s: expected one match for %s, found %d' % (path.relative_to(ROOT), pattern, n))
    if new != text:
        print('  %s %s' % ('updated' if apply else 'would update', path.relative_to(ROOT)))
        if apply:
            path.write_bytes(new.encode('utf-8'))


def identify(game_dir, manifest):
    files = find_game_files(str(game_dir))
    for key in ('game.exe', 'game.gcl'):
        if key not in files:
            sys.exit('%s not found under %s' % (key, game_dir))
    versions = set(re.findall(rb'\x00v(\d+\.\d+\.\d+)\x00', Path(files['game.exe']).read_bytes()))
    version = versions.pop().decode() if len(versions) == 1 else None
    manifest = manifest or files.get('appmanifest')
    build = None
    if manifest:
        state = parse_vdf(Path(manifest).read_text(encoding='utf-8', errors='replace')).get('AppState', {})
        build = state.get('buildid')
    return dict(version=version, build=build, manifest=str(manifest) if manifest else None,
                exe=sha256(files['game.exe']), gcl=sha256(files['game.gcl']))


def prepare_sdk(previous, out):
    """New SDK root = previous root minus generated output, plus the repo's tools."""
    if out.exists():
        shutil.rmtree(out)
    shutil.copytree(previous, out, ignore=shutil.ignore_patterns('json', 'include', 'docs', 'reports', '__pycache__'))
    if (out / 'tools').exists():
        shutil.rmtree(out / 'tools')
    shutil.copytree(SDK_TOOLS, out / 'tools', ignore=shutil.ignore_patterns('__pycache__'))
    old_runtime = sorted((out / 'validation').glob('runtime_*.json')) if (out / 'validation').exists() else []
    if old_runtime:
        keep = out / 'validation' / 'previous-build'
        keep.mkdir(exist_ok=True)
        for p in old_runtime:
            p.rename(keep / p.name)
        print('  set aside %d runtime evidence files from the previous build' % len(old_runtime))
    (out / 'include').mkdir(exist_ok=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game-dir', type=Path, required=True, help='Anymaker install, or a folder with game.exe and bin/game.gcl')
    ap.add_argument('--previous-sdk', type=Path, required=True, help='SDK root (json/, tools/, validation/) of the current target build')
    ap.add_argument('--work', type=Path, required=True, help='output folder; <work>/sdk becomes the new SDK root')
    ap.add_argument('--manifest', type=Path, help='appmanifest_4435340.acf when --game-dir is a copied build')
    ap.add_argument('--api-version', help='new AnyAPI version, e.g. 0.35.0 (left unchanged when omitted)')
    ap.add_argument('--api-revision', type=int, help='new AnyAPI revision, e.g. 36')
    ap.add_argument('--skip-sdk', action='store_true', help='reuse <work>/sdk from an earlier run')
    ap.add_argument('--apply', action='store_true', help='write the changes into the repo')
    a = ap.parse_args()
    work = a.work.resolve()
    sdk = work / 'sdk'
    previous_json = a.previous_sdk.resolve() / 'json'
    if not (previous_json / 'functions.json').is_file():
        sys.exit('%s has no functions.json' % previous_json)

    step('identify the build')
    ident = identify(a.game_dir, a.manifest)
    print(json.dumps(ident, indent=2))
    if not ident['version'] or not ident['build']:
        sys.exit('game version or Steam build id unknown; pass --manifest <appmanifest_4435340.acf>')
    label = '%s / Steam build %s' % (ident['version'], ident['build'])

    if not a.skip_sdk:
        step('regenerate the experimental SDK into %s' % sdk)
        prepare_sdk(a.previous_sdk.resolve(), sdk)
        run([sdk / 'tools' / 'regenerate.py', '--game-dir', a.game_dir, '--out', sdk, '--label', 'installed-' + ident['build'],
             '--previous-json', previous_json, '--skip-compile'] + (['--manifest', ident['manifest']] if ident['manifest'] else []))

    step('audit native byte patterns')
    run([HERE / 'game_update_audit.py', '--game-dir', a.game_dir, '--previous-json', previous_json, '--out', work / 'audit']
        + (['--apply'] if a.apply else []))

    step('refresh legacy event routes')
    gcl = find_game_files(str(a.game_dir))['game.gcl']
    run([HERE / 'refresh_legacy_routes.py', '--gcl', gcl] + (['--apply'] if a.apply else []))

    step('re-bind experimental-SDK mods')
    run([HERE / 'rebind_mod_bindings.py', '--reference', sdk, '--build', label] + (['--apply'] if a.apply else []))

    step('copy SDK headers and system docs')
    copies = [(sdk / 'include' / n, ROOT / 'sdk' / 'experimental' / 'include' / n)
              for n in ('anymaker_sdk_types.hpp', 'anymaker_sdk_symbols.hpp')]
    copies += [(p, ROOT / 'sdk' / 'experimental' / 'systems' / p.name) for p in sorted((sdk / 'docs' / 'systems').glob('*.md'))]
    copies.append((sdk / 'docs' / 'capability_matrix.md', ROOT / 'sdk' / 'experimental' / 'capability_matrix.md'))
    for src, dst in copies:
        if dst.exists() and dst.read_bytes() == src.read_bytes():
            continue
        print('  %s %s' % ('copied' if a.apply else 'would copy', dst.relative_to(ROOT)))
        if a.apply:
            shutil.copyfile(src, dst)

    step('write the build identity')
    native = ROOT / 'native'
    rewrite(native / 'runtime_build_identity.h', [
        (r'// Anymaker [\d.]+, Steam \d+\.', '// Anymaker %s, Steam %s.' % (ident['version'], ident['build'])),
        (r'(P27_EXPECTED_EXE_SHA256 ")[0-9a-f]{64}', r'\g<1>' + ident['exe']),
        (r'(P27_EXPECTED_GCL_SHA256 ")[0-9a-f]{64}', r'\g<1>' + ident['gcl'])], a.apply)
    guard = [(r'(strcpy_s\(info\.game_version, ")[\d.]+', r'\g<1>' + ident['version']),
             (r'(strcpy_s\(info\.steam_build, ")\d+', r'\g<1>' + ident['build'])]
    if a.api_version:
        guard.append((r'(strcpy_s\(info\.api_version, ")[\d.]+', r'\g<1>' + a.api_version))
    if a.api_revision:
        guard.append((r'(info\.api_revision = )\d+', r'\g<1>%d' % a.api_revision))
    rewrite(native / 'runtime_build_guard.inc', guard, a.apply)
    manifest = [(r'("game_version": ")[\d.]+', r'\g<1>' + ident['version']),
                (r'("steam_build_id": )\d+', r'\g<1>' + ident['build']),
                (r'("game\.exe": ")[0-9a-f]{64}', r'\g<1>' + ident['exe']),
                (r'("bin/game\.gcl": ")[0-9a-f]{64}', r'\g<1>' + ident['gcl'])]
    if a.api_version:
        manifest.append((r'("api_version": ")[\d.]+', r'\g<1>' + a.api_version))
    if a.api_revision:
        manifest.append((r'(\n  "revision": )\d+', r'\g<1>%d' % a.api_revision))
    rewrite(native / 'BUILD_MANIFEST.json', manifest, a.apply)
    if a.api_version:
        c = a.api_version.split('.')
        rewrite(native / 'CMakeLists.txt', [
            (r'(if\(target STREQUAL "dinput8"\)\r?\n    set\(ANYAPI_VERSION_COMMA ")[\d,]+(")', r'\g<1>%s,%s,%s,0\g<2>' % tuple(c)),
            (r'(if\(target STREQUAL "dinput8"\)\r?\n    set\(ANYAPI_VERSION_COMMA "[\d,]+"\)\r?\n    set\(ANYAPI_VERSION_TEXT ")[\d.]+', r'\g<1>' + a.api_version)],
            a.apply)

    print('\nDone. Next: read %s, then follow docs/development/game-updates.md from "Review by hand".'
          % (work / 'audit' / 'pattern_audit.md'))


if __name__ == '__main__':
    main()
