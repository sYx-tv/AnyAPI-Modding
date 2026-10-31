"""Carry runtime-only native RVAs from the previous build when their code region did not move.

Some natives (the `$string_*` helpers) have no static thunk store, so their RVA only comes from a
runtime probe (validation/probe.py). After a game update, sdk_codegen.py stops on them until a new
probe runs. If the --neighbours nearest statically known natives on both sides moved by the same
amount (often zero), the code between them kept its size, and the previous RVA plus that shift very
likely holds. This tool copies such RVAs into the new natives.json as static evidence, with the
neighbours as the basis. An RVA carried by an earlier run counts as a previous RVA too, so the chain
survives updates with no probe in between. A later probe overwrites them with runtime evidence.

    python tools/carry_natives.py --old <previous json> --new <new json> [--neighbours 1] [--max-gap 0x20000]
        [--old-exe <previous game.exe> --new-exe <new game.exe>]

When the neighbours disagree and both game.exe files are given, the previous build's code at the
old RVA (up to its first ret or jmp, call and jump targets and RIP-relative displacements masked)
is searched in the new game.exe; a single match, or the one match that shares a neighbour's shift,
is carried with that basis. Natives found by neither are listed and left alone; run the probe for those.
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import dump_json, load_json  # noqa: E402


class CodeFinder:
    """Find a previous build's function in the new game.exe by its bytes."""

    def __init__(self, old_exe, new_exe):
        import pefile
        self.old = pefile.PE(old_exe, fast_load=True).get_memory_mapped_image()
        self.new = pefile.PE(new_exe, fast_load=True).get_memory_mapped_image()
        self.found = None

    def pattern(self, rva, limit=96):
        import re
        from capstone import CS_ARCH_X86, CS_MODE_64, Cs
        from capstone.x86 import X86_OP_MEM, X86_REG_RIP
        md = Cs(CS_ARCH_X86, CS_MODE_64)
        md.detail = True
        pat, fixed = b'', 0
        for ins in md.disasm(self.old[rva:rva + limit], rva):
            b = bytes(ins.bytes)
            keep = [True] * len(b)
            if (ins.mnemonic in ('call', 'jmp') or ins.mnemonic.startswith('j')) and len(b) >= 5:
                keep[-4:] = [False] * 4
            for op in ins.operands:
                if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
                    keep[ins.disp_offset:ins.disp_offset + 4] = [False] * 4
            pat += b''.join(re.escape(bytes([c])) if k else b'.' for c, k in zip(b, keep))
            fixed += sum(keep)
            if ins.mnemonic in ('ret', 'jmp'):
                break
        return pat, fixed

    def find(self, rva, neighbour_shifts):
        import re
        self.found = None
        pat, fixed = self.pattern(rva)
        if fixed < 6:
            return None
        hits = [m.start() for m in re.finditer(pat, self.new, re.S)]
        if len(hits) > 1:
            hits = [h for h in hits if h - rva in neighbour_shifts]
        if len(hits) != 1:
            return None
        self.found = (hits[0], fixed)
        return hits[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--old', required=True)
    ap.add_argument('--new', required=True)
    ap.add_argument('--neighbours', type=int, default=1)
    ap.add_argument('--max-gap', type=lambda v: int(v, 0), default=0x20000, help='largest bracket allowed')
    ap.add_argument('--old-exe', help="previous build's game.exe, to find natives whose neighbourhood moved by their code")
    ap.add_argument('--new-exe', help="new build's game.exe")
    a = ap.parse_args()
    finder = CodeFinder(a.old_exe, a.new_exe) if a.old_exe and a.new_exe else None
    old = load_json(os.path.join(a.old, 'natives.json'))
    path = os.path.join(a.new, 'natives.json')
    new = load_json(path)
    by_sig = {n['sig']: n for n in new}
    pairs = [(int(o['thunk_rva'], 16), int(by_sig[o['sig']]['thunk_rva'], 16)) for o in old
             if o.get('thunk_rva') and by_sig.get(o['sig'], {}).get('thunk_rva')]
    carried, left = [], []
    for o in old:
        n = by_sig.get(o['sig'])
        rt = (o.get('runtime') or {}).get('rva') or \
            (o.get('thunk_rva') if str(o.get('basis', '')).startswith('previous build runtime RVA') else None)
        if not n or n.get('thunk_rva') or not rt:
            continue
        r = int(rt, 16)
        below = sorted(p for p in pairs if p[0] < r)[-a.neighbours:]
        above = sorted(p for p in pairs if p[0] > r)[:a.neighbours]
        near = below + above
        shifts = {y - x for x, y in near}
        if len(below) == len(above) == a.neighbours and above[-1][0] - below[0][0] <= a.max_gap and len(shifts) == 1:
            shift = shifts.pop()
            n['thunk_rva'] = '%#x' % (r + shift)
            n['evidence'] = 'static'
            n['basis'] = ('previous build runtime RVA %s carried over (shifted %+#x): the %d nearest statically known '
                          'natives on each side (%#x-%#x) moved by the same amount' % (rt, shift, a.neighbours, below[0][0], above[-1][0]))
            carried.append(o['sig'])
        elif finder and finder.find(r, {y - x for x, y in near}) is not None:
            hit, fixed = finder.found
            n['thunk_rva'] = '%#x' % hit
            n['evidence'] = 'static'
            n['basis'] = ('previous build runtime RVA %s carried over (shifted %+#x): its code (%d fixed bytes up to the first '
                          'ret or jmp) matches there and nowhere else a neighbour\'s shift allows' % (rt, hit - r, fixed))
            carried.append(o['sig'])
        else:
            left.append(o['sig'])
    dump_json(new, path)
    print('carried %d natives; %d need a runtime probe' % (len(carried), len(left)))
    for s in left:
        print('  probe:', s)


if __name__ == '__main__':
    main()
