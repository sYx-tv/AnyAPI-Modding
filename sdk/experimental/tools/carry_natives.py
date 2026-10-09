"""Carry runtime-only native RVAs from the previous build when their code region did not move.

Some natives (the `$string_*` helpers) have no static thunk store, so their RVA only comes from a
runtime probe (validation/probe.py). After a game update, sdk_codegen.py stops on them until a new
probe runs. If the --neighbours nearest statically known natives on both sides kept exactly the
same RVA, the code between them kept its size, and the previous RVA very likely still holds. This tool
copies such RVAs into the new natives.json as static evidence, with the neighbours as the basis.
A later probe overwrites them with runtime evidence.

    python tools/carry_natives.py --old <previous json> --new <new json> [--neighbours 1] [--max-gap 0x20000]

Natives whose neighbourhood moved are listed and left alone; run the probe for those.
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import dump_json, load_json  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--old', required=True)
    ap.add_argument('--new', required=True)
    ap.add_argument('--neighbours', type=int, default=1)
    ap.add_argument('--max-gap', type=lambda v: int(v, 0), default=0x20000, help='largest bracket allowed')
    a = ap.parse_args()
    old = load_json(os.path.join(a.old, 'natives.json'))
    path = os.path.join(a.new, 'natives.json')
    new = load_json(path)
    by_sig = {n['sig']: n for n in new}
    pairs = [(int(o['thunk_rva'], 16), int(by_sig[o['sig']]['thunk_rva'], 16)) for o in old
             if o.get('thunk_rva') and by_sig.get(o['sig'], {}).get('thunk_rva')]
    carried, left = [], []
    for o in old:
        n = by_sig.get(o['sig'])
        rt = (o.get('runtime') or {}).get('rva')
        if not n or n.get('thunk_rva') or not rt:
            continue
        r = int(rt, 16)
        below = sorted(p for p in pairs if p[0] < r)[-a.neighbours:]
        above = sorted(p for p in pairs if p[0] > r)[:a.neighbours]
        near = below + above
        if len(below) == len(above) == a.neighbours and above[-1][0] - below[0][0] <= a.max_gap \
                and all(x == y for x, y in near):
            n['thunk_rva'] = rt
            n['evidence'] = 'static'
            n['basis'] = ('previous build runtime RVA %s carried over: the %d nearest statically known natives on '
                          'each side (%#x-%#x) kept identical RVAs' % (rt, a.neighbours, below[0][0], above[-1][0]))
            carried.append(o['sig'])
        else:
            left.append(o['sig'])
    dump_json(new, path)
    print('carried %d natives; %d need a runtime probe' % (len(carried), len(left)))
    for s in left:
        print('  probe:', s)


if __name__ == '__main__':
    main()
