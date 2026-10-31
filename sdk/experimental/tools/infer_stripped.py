"""Infer what a stripped (format 2) game.gcl no longer records: renamed signatures and moved fields.

Names. A format-2 record names a function only by the FNV-1a 64 hash of its signature, and gcl.py
recovers the signatures the previous build already had. A function whose signature changed (a
parameter added, removed or retyped, a const or return type changed) keeps an unknown hash. For each
previous signature missing from the new build this tool hashes those one-step edits, using the
parameter and return types the build already names, and keeps every edit that hits an unknown hash.
A hit is exact (the hash matches), but which vanished function it replaces is the tool's guess.

Fields. A format-2 game.gcl records no fields. gcl.py carries a type's fields from the previous build
when its size, alignment and parent did not change; this tool covers the types that did change. It
pairs each such type's methods, ctor, dtor and copy between the previous and the new game.gcl by
signature, aligns their instructions, and counts how each memory displacement of the previous code
reads in the new code. A field's new offset is the displacement its old offset turned into most
often. Fields with no votes take their neighbours' shift when both neighbours moved by the same
amount, a lone field shifted unlike both agreeing neighbours takes theirs, and a type that only grew
by its parent's growth shifts all of its own fields by that amount. Offsets that would break field
order are dropped, never guessed.

    python tools/infer_stripped.py --old-gcl <previous game.gcl> --old-names <previous SDK json/>
        --new-gcl <new game.gcl> --new-names <previous SDK json/> --out stripped_inferred.json

The result ({"names": {hash: sig}, "types": {name: layout}}) is static evidence, not a record of the
build. It feeds gcl.py through ANYMAKER_GCL_INFERRED (Type.fields_source == 'inferred') and lists every
field it could not place.
"""
import argparse
import collections
import difflib
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gcl  # noqa: E402

LIFETIME = ('ctor', 'dtor', 'copy')
VOCAB = 3000


def recover_names(old, new):
    """{hash: sig} for unknown hashes of the new build that one edit of a vanished signature reaches."""
    unknown = {f.hash for f in new.unresolved}
    for f in new.functions:
        unknown.update(int(s_[4:20], 16) for _, s_ in f.relocs if s_.startswith('() #'))
    if not unknown:
        return {}
    have = {f.hash for f in new.functions}
    gone = sorted({f.sig for f in old.functions if gcl.fnv1a64(f.sig) not in have})
    params, rets = collections.Counter(), collections.Counter()
    for f in new.functions:
        if f.sig.startswith('() #'):
            continue
        r, _, ps = gcl.split_sig(f.sig)
        rets[r] += 1
        params.update(ps)
    vocab = [t for t, _ in params.most_common(VOCAB)]
    vocab = list(dict.fromkeys(vocab + ['const ' + t for t in vocab if not t.startswith('const ')]))
    ret_vocab = [r for r, _ in rets.most_common(300)]
    found = {}

    def hit(ret, name, ps):
        sig = '(%s) %s (%s)' % (ret, name, ', '.join(ps))
        h = gcl.fnv1a64(sig)
        if h in unknown and h not in found:
            found[h] = sig
            return True
        return False

    for g in gone:
        ret, name, ps = gcl.split_sig(g)
        quick = [ps[:i] + [ps[i][6:] if ps[i].startswith('const ') else 'const ' + ps[i]] + ps[i + 1:] for i in range(len(ps))]
        quick += [ps[:i] + ps[i + 1:] for i in range(len(ps))]
        if any([hit(ret, name, q) for q in quick] + [hit(r, name, ps) for r in ret_vocab]):
            continue
        if any([hit(ret, name, ps[:i] + [v] + ps[i:]) for i in range(len(ps) + 1) for v in vocab]):
            continue
        any([hit(ret, name, ps[:i] + [v] + ps[i + 1:]) for i in range(len(ps)) for v in vocab])
    return found


def strip_const(t):
    return t[6:] if t.startswith('const ') else t


def owner_of(f):
    try:
        _, name, ptypes = gcl.split_sig(f.sig)
    except ValueError:
        return None
    if not ptypes:
        return None
    o = strip_const(ptypes[0])
    return o if name.startswith(o + '.') or name in LIFETIME else None


def tokens(code):
    from capstone import CS_ARCH_X86, CS_MODE_64, Cs
    from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG, X86_REG_RBP, X86_REG_RIP, X86_REG_RSP
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    skip = (X86_REG_RSP, X86_REG_RBP, X86_REG_RIP, 0)
    out = []
    for ins in md.disasm(code, 0):
        tk, mems = [ins.mnemonic], []
        for op in ins.operands:
            if op.type == X86_OP_MEM:
                m = op.mem
                if m.base in skip:
                    tk.append('m')
                    continue
                tk.append('m%d.%d.%d' % (m.base, m.index, m.scale))
                mems.append(m.disp)
            elif op.type == X86_OP_REG:
                tk.append('r%d' % op.reg)
            elif op.type == X86_OP_IMM:
                tk.append('i')
        out.append((' '.join(tk), mems))
    return out


def votes_for(old_f, new_f):
    a = tokens(old_f.blob[:old_f.code_end])
    b = tokens(new_f.blob[:new_f.code_end])
    sm = difflib.SequenceMatcher(None, [t for t, _ in a], [t for t, _ in b], autojunk=False)
    res = []
    for i, j, n in sm.get_matching_blocks():
        for k in range(n):
            for do, dn in zip(a[i + k][1], b[j + k][1]):
                if do >= 0 and dn >= 0:
                    res.append((do, dn))
    return res


def infer(old, new, types=None):
    """{type name: {'fields': [(type, name, offset)], 'unresolved': [names], 'basis': str}}"""
    changed = {}
    for t in new.types:
        o = old.type_by_name.get(t.name)
        if o is None or not o.fields or t.fields:
            continue
        if types is not None and t.name not in types:
            continue
        changed[t.name] = (o, t)
    votes = collections.defaultdict(lambda: collections.defaultdict(collections.Counter))
    paired = collections.Counter()
    for f in new.functions:
        if not f.code_end:
            continue
        owner = owner_of(f)
        if owner not in changed:
            continue
        g = old.func_by_sig.get(f.sig)
        if g is None or not g.code_end:
            continue
        paired[owner] += 1
        for do, dn in votes_for(g, f):
            votes[owner][do][dn] += 1

    def size_delta(name):
        o, n = old.type_by_name.get(name), new.type_by_name.get(name)
        return (n.size - o.size) if o and n else None

    result = {}
    for name, (o, t) in sorted(changed.items()):
        fields = sorted(o.fields, key=lambda x: x[2])
        v = votes.get(name, {})
        best = {}
        for ft, fn, fo in fields:
            c = v.get(fo)
            if c:
                (dn, k), = c.most_common(1)
                if k * 2 > sum(c.values()) or len(c) == 1:
                    best[fn] = dn
        own_only = t.parent and size_delta(t.parent) == t.size - o.size and o.parent == t.parent
        placed, basis = {}, []
        if own_only:
            d = t.size - o.size
            agree = sum(1 for ft, fn, fo in fields if best.get(fn) == fo + d)
            clash = sum(1 for ft, fn, fo in fields if fn in best and best[fn] != fo + d)
            if clash <= agree:
                placed = {fn: fo + d for ft, fn, fo in fields}
                basis.append('grew only by its parent (%+d); %d code votes agree, %d disagree' % (d, agree, clash))
        if not placed:
            placed = dict(best)
            deltas = [(fn, best.get(fn, None) - fo if fn in best else None) for ft, fn, fo in fields]
            for i, (fn, dl) in enumerate(deltas):
                if dl is not None:
                    continue
                prev = next((deltas[k][1] for k in range(i - 1, -1, -1) if deltas[k][1] is not None), None)
                nxt = next((deltas[k][1] for k in range(i + 1, len(deltas)) if deltas[k][1] is not None), None)
                if prev is not None and prev == nxt:
                    placed[fn] = fields[i][2] + prev
                elif prev is None and nxt == 0:
                    placed[fn] = fields[i][2]
                elif nxt is None and prev is not None and fields[i][2] + prev < t.size:
                    placed[fn] = fields[i][2] + prev
            basis.append('code votes from %d paired functions, gaps filled from equal neighbour shifts' % paired[name])
        # a lone field shifted unlike both neighbours, which agree, is a vote confounded by another object
        shifts = [placed[fn] - fo if fn in placed else None for ft, fn, fo in fields]
        for i in range(1, len(fields) - 1):
            a_, b_, c_ = shifts[i - 1], shifts[i], shifts[i + 1]
            if a_ is not None and a_ == c_ and b_ is not None and b_ != a_:
                placed[fields[i][1]] = fields[i][2] + a_
                shifts[i] = a_
        out, last, unresolved = [], -1, []
        for ft, fn, fo in fields:
            no = placed.get(fn)
            if no is None or no <= last or no >= t.size:
                unresolved.append(fn)
                continue
            out.append((ft, fn, no))
            last = no
        result[name] = {'size': t.size, 'previous_size': o.size, 'fields': out, 'unresolved': unresolved,
                        'basis': '; '.join(basis)}
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--old-gcl', required=True)
    ap.add_argument('--old-names', help='names source for a stripped previous game.gcl (its SDK json/ folder)')
    ap.add_argument('--new-gcl', required=True)
    ap.add_argument('--new-names', required=True, help='previous SDK json/ folder (or format-1 game.gcl)')
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    old = gcl.Program(a.old_gcl, a.old_names, inferred={})
    names = gcl.Names.load(a.new_names)
    new = gcl.Program(a.new_gcl, names, inferred={})
    found = recover_names(old, new)
    print('names: %d unknown, %d recovered from changed signatures' % (len(new.unresolved), len(found)))
    names.sig.update(found)
    new = gcl.Program(a.new_gcl, names, inferred={})
    res = infer(old, new)
    with open(a.out, 'w', encoding='utf-8') as fh:
        json.dump({'names': {'%016x' % h: sig for h, sig in sorted(found.items())}, 'types': res}, fh, indent=1, sort_keys=True)
    total = sum(len(r['fields']) + len(r['unresolved']) for r in res.values())
    unresolved = sum(len(r['unresolved']) for r in res.values())
    print('types %d, fields %d, placed %d, unresolved %d' % (len(res), total, total - unresolved, unresolved))


if __name__ == '__main__':
    main()
