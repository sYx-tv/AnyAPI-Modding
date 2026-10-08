"""Regenerate the whole SDK for one game build (Windows or Linux).

    python tools/regenerate.py --game-dir "C:/Program Files (x86)/Steam/steamapps/common/Anymaker" --out .
        [--previous-json ../previous/json] [--skip-compile] [--skip-index]

Runs, in order: analyze, abi facts, merge runtime evidence (validation/runtime_*.json that match the build),
layout header, symbols header, system map + capability matrix, reference docs, SQLite index, compile test,
and (with --previous-json) the build comparison. Stops at the first failing step.
"""
import argparse
import glob
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))


def run(args, label):
    t = time.time()
    print('==>', label, flush=True)
    r = subprocess.run([sys.executable] + args)
    if r.returncode != 0:
        print('FAILED:', label, file=sys.stderr)
        sys.exit(r.returncode)
    print('    done in %.0fs' % (time.time() - t), flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game-dir', required=True)
    ap.add_argument('--out', required=True, help='SDK root to write json/, include/, docs/, reports/ into')
    ap.add_argument('--label', default=None)
    ap.add_argument('--previous-json', default=None)
    ap.add_argument('--skip-compile', action='store_true')
    ap.add_argument('--skip-index', action='store_true')
    a = ap.parse_args()
    out = os.path.abspath(a.out)
    J = os.path.join(out, 'json')
    T = lambda n: os.path.join(HERE, n)
    sys.path.insert(0, HERE)
    from common import find_game_files
    gf = find_game_files(a.game_dir)
    extra = ['--dll', gf['steam_api64.dll']] if gf.get('steam_api64.dll') else []
    run([T('sdk_analyze.py'), '--game-dir', a.game_dir, '--out', J] + extra + (['--label', a.label] if a.label else []), 'analyze')
    run([T('abi_facts.py'), '--gcl', gf['game.gcl'], '--json', J], 'abi facts')
    rt = sorted(glob.glob(os.path.join(out, 'validation', 'runtime_*.json')))
    if rt:
        args = [os.path.join(out, 'validation', 'merge_runtime.py'), '--json', J]
        for r in rt:
            args += ['--runtime', r]
        run(args, 'merge runtime evidence')
    run([T('sdk_header.py'), '--json', J, '--out', os.path.join(out, 'include', 'anymaker_sdk_types.hpp')], 'layout header')
    run([T('sdk_codegen.py'), '--json', J, '--out', os.path.join(out, 'include', 'anymaker_sdk_symbols.hpp')], 'symbols header')
    run([T('sdk_systems.py'), '--json', J, '--docs', os.path.join(out, 'docs')], 'system map')
    run([T('sdk_docs.py'), '--json', J, '--docs', os.path.join(out, 'docs')], 'reference docs')
    if not a.skip_index:
        run([T('sdk_query.py'), '--json', J, '--db', os.path.join(J, 'sdk_index.sqlite'), 'build'], 'sqlite index')
    if not a.skip_compile:
        run([T('validate_compile.py'), '--sdk', out, '--out', os.path.join(out, 'validation', 'compile_result.json')],
            'compile test')
    if a.previous_json:
        run([T('sdk_diff.py'), '--old', a.previous_json, '--new', J, '--out', os.path.join(out, 'reports', 'diff-previous-to-current')],
            'build comparison')


if __name__ == '__main__':
    main()
