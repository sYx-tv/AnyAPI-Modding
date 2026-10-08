"""Structured comparison of two SDK databases (json folders written by sdk_analyze.py).

Usage:
    python tools/sdk_diff.py --old work/json-b-prev-sdk --new json --out reports/diff-prev-to-installed

Writes <out>/diff.json (machine-readable, every change with stable IDs) and <out>/README.md (summary).

Change classes. The point is to separate "moved" from "changed":
  natives    moved_only        thunk RVA differs, masked thunk bytes identical (relinked, same code)
             bytes_changed     masked thunk bytes differ (behavior may differ)
             resolved/lost     evidence changed between metadata and unknown
  functions  unchanged         same signature, code bytes identical (record offset moves are ignored:
                               gcl code has no static address)
             padding_only      only unreferenced pool padding differs (no behavioral change)
             body_changed_constants     instructions identical, a referenced literal value differs
             body_changed_constants_tail  only bytes after a small literal differ (likely padding)
             body_changed_instructions  instruction bytes differ (codegen or behavior changed)
             deps_changed      relocation list differs (calls/globals/types it references)
             locator_broken    old code signature no longer matches the new code prefix
             return_changed    same name+params, return type differs
  types      layout_changed    size, field offset or field type changed (headers must be regenerated)
             fields_added/removed, parent_changed, align_changed
             vtable_changed    slot count or slot signatures changed (slot-index dispatch breaks)
  enums      values_renumbered a name kept its spelling but its position (numeric value) moved
             values_added/removed
  globals    type_changed, added, removed, init_order_moved, anchor_changed
"""
import argparse
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import dump_json, load_json, write_text  # noqa: E402

TEMPLATE = ('ptr<', 'ref<', 'vector<', 'array<', 'map<', 'const ')


def top_ns(name):
    n = name
    for pre in TEMPLATE:
        if n.startswith(pre):
            return '(template instantiations)'
    return n.split('.', 1)[0]


def load(d):
    j = {k: load_json(os.path.join(d, k + '.json')) for k in ('build_identity', 'types', 'functions', 'globals', 'natives')}
    ap = os.path.join(d, 'anchors.json')
    j['anchors'] = load_json(ap) if os.path.exists(ap) else {'globals': {}, 'functions': {}}
    return j


def fid(f):
    return f.get('id') or 'FN:' + f['sig']


def fkey(f):
    if f.get('key'):
        return f['key']
    from common import id_func_name, parse_sig
    _, name, ptypes = parse_sig(f['sig'])
    return id_func_name(name, ptypes)


def diff_types(o, n):
    out = collections.defaultdict(list)
    for name in sorted(set(o) | set(n)):
        a, b = o.get(name), n.get(name)
        if a is None:
            out['added'].append({'id': 'T:' + name, 'name': name, 'size': b['size'], 'kind': b['kind']})
            continue
        if b is None:
            out['removed'].append({'id': 'T:' + name, 'name': name, 'size': a['size'], 'kind': a['kind']})
            continue
        ch = {}
        if a['size'] != b['size']:
            ch['size'] = [a['size'], b['size']]
        if a['align'] != b['align']:
            ch['align'] = [a['align'], b['align']]
        if (a['parent'] or None) != (b['parent'] or None):
            ch['parent'] = [a['parent'], b['parent']]
        if a['flags'] != b['flags']:
            ch['flags'] = [a['flags'], b['flags']]
        fa = {f['name'] or f['id']: f for f in a['fields']}
        fb = {f['name'] or f['id']: f for f in b['fields']}
        added = [{'name': k, 'type': fb[k]['type'], 'offset': fb[k]['offset']} for k in fb if k not in fa]
        removed = [{'name': k, 'type': fa[k]['type'], 'offset': fa[k]['offset']} for k in fa if k not in fb]
        moved = [{'name': k, 'old_offset': fa[k]['offset'], 'new_offset': fb[k]['offset']}
                 for k in fa if k in fb and fa[k]['offset'] != fb[k]['offset']]
        retyped = [{'name': k, 'old_type': fa[k]['type'], 'new_type': fb[k]['type']}
                   for k in fa if k in fb and fa[k]['type'] != fb[k]['type']]
        if added:
            ch['fields_added'] = added
        if removed:
            ch['fields_removed'] = removed
        if moved:
            ch['fields_moved'] = moved
        if retyped:
            ch['fields_retyped'] = retyped
        # enum
        if a['kind'] == 'enum' or b['kind'] == 'enum':
            va = {e['name']: e['value'] for e in a['enum_values']}
            vb = {e['name']: e['value'] for e in b['enum_values']}
            ren = [{'name': k, 'old': va[k], 'new': vb[k]} for k in va if k in vb and va[k] != vb[k]]
            if ren:
                ch['values_renumbered'] = ren
            if set(vb) - set(va):
                ch['values_added'] = sorted(set(vb) - set(va), key=lambda k: vb[k])
            if set(va) - set(vb):
                ch['values_removed'] = sorted(set(va) - set(vb), key=lambda k: va[k])
        # vtable
        sa = [v['sig'] for v in a['vtable']]
        sb = [v['sig'] for v in b['vtable']]
        if sa != sb:
            vc = {'old_count': len(sa), 'new_count': len(sb), 'slots': []}
            for i in range(max(len(sa), len(sb))):
                x = sa[i] if i < len(sa) else None
                y = sb[i] if i < len(sb) else None
                if x != y:
                    vc['slots'].append({'slot': i, 'old': x, 'new': y})
            # did existing slots shift? (a method present in both at different indices)
            pos_a = {s: i for i, s in enumerate(sa)}
            vc['shifted'] = [{'sig': s, 'old_slot': pos_a[s], 'new_slot': i} for i, s in enumerate(sb)
                             if s in pos_a and pos_a[s] != i]
            ch['vtable'] = vc
        if not ch:
            continue
        cls = []
        if 'size' in ch or moved or retyped or removed or 'parent' in ch:
            cls.append('layout_changed')
        elif added:
            cls.append('fields_added_no_move')
        if 'align' in ch:
            cls.append('align_changed')
        if 'values_renumbered' in ch:
            cls.append('values_renumbered')
        elif 'values_added' in ch or 'values_removed' in ch:
            cls.append('enum_members_changed')
        if 'vtable' in ch:
            cls.append('vtable_shifted' if ch['vtable']['shifted'] else 'vtable_changed')
        if 'flags' in ch and not cls:
            cls.append('flags_changed')
        out['changed'].append({'id': 'T:' + name, 'name': name, 'kind': b['kind'], 'classes': cls, 'changes': ch,
                               'system': top_ns(name)})
    return out


def _plausible_f64(b):
    if len(b) != 8:
        return False
    e = (int.from_bytes(b[4:], 'little') >> 20) & 0x7ff
    return 0x3b0 <= e <= 0x450


def const_change(wa, wb):
    """Joint comparison of referenced constant windows (same instruction stream on both sides).
    'constants'        a window differs in its first byte, a string differs, or both sides hold
                       plausible but different f64 values -> a literal value changed
    'constants_tail'   windows differ only after byte 0 and are not both plausible f64: most likely the
                       compiler's uninitialised padding after a bool/s32 literal (low confidence)"""
    if wa is None or wb is None or len(wa) != len(wb):
        return 'constants'
    tail = False
    for x, y in zip(wa, wb):
        if x == y:
            continue
        lea = x.startswith('L') and y.startswith('L')
        bx, by = bytes.fromhex(x.lstrip('L')), bytes.fromhex(y.lstrip('L'))
        if not lea or len(bx) != 8 or len(by) != 8 or bx[0] != by[0]:
            return 'constants'
        if _plausible_f64(bx) and _plausible_f64(by):
            return 'constants'
        tail = True
    return 'constants_tail' if tail else 'constants'


def diff_functions(o, n):
    out = collections.defaultdict(list)
    oa = {fid(f): f for f in o}
    na = {fid(f): f for f in n}
    okey = collections.defaultdict(list)
    for f in o:
        okey[fkey(f)].append(f)
    stats = collections.Counter()
    for i, b in na.items():
        a = oa.get(i)
        if a is None:
            cands = [x for x in okey.get(fkey(b), []) if fid(x) not in na]
            if cands:
                a = cands[0]
                out['return_changed'].append({'id': i, 'old_sig': a['sig'], 'new_sig': b['sig']})
                stats['return_changed'] += 1
                continue
            out['added'].append({'id': i, 'sig': b['sig'], 'side': b['side'], 'native': b['native'],
                                 'system': top_ns(b['name'])})
            stats['added_native' if b['native'] else 'added'] += 1
            continue
        if a['native'] != b['native']:
            out['native_status_changed'].append({'id': i, 'old_native': a['native'], 'new_native': b['native']})
            stats['native_status_changed'] += 1
            continue
        if b['native']:
            stats['native_decl_same'] += 1
            continue
        rec = {'id': i, 'sig': b['sig'], 'system': top_ns(b['name']), 'side': b['side']}
        changed = False
        ha, hb = a.get('code_hash') or {}, b.get('code_hash') or {}
        if ha and hb and ha['raw'] != hb['raw']:
            if ha['insn'] != hb['insn']:
                rec['body_changed'] = 'instructions'
            elif ha['consts'] != hb['consts']:
                rec['body_changed'] = const_change(ha.get('const_windows'), hb.get('const_windows'))
            else:
                rec['body_changed'] = None
                stats['padding_only'] += 1
            if rec['body_changed']:
                rec['code_size'] = [a['gcl']['code_size'], b['gcl']['code_size']]
                changed = True
            else:
                del rec['body_changed']
        if a.get('relocs_sha1') and a.get('relocs_sha1') != b.get('relocs_sha1'):
            ca, cb = set(a['calls']), set(b['calls'])
            ga, gb = set(a['globals_used']), set(b['globals_used'])
            d = {}
            if cb - ca:
                d['calls_added'] = sorted(cb - ca)
            if ca - cb:
                d['calls_removed'] = sorted(ca - cb)
            if gb - ga:
                d['globals_added'] = sorted(gb - ga)
            if ga - gb:
                d['globals_removed'] = sorted(ga - gb)
            rec['deps_changed'] = d or {'note': 'relocation order or virtual/type references changed'}
            changed = True
        pa = [(p['name'], p['type']) for p in a['params']]
        pb = [(p['name'], p['type']) for p in b['params']]
        if pa != pb:
            rec['param_names_changed'] = {'old': pa, 'new': pb}
            changed = True
        osig = a.get('code_signature')
        if osig and a.get('code_signature_unique_in_gcl'):
            ob = bytes.fromhex(osig.replace(' ', ''))
            nb_sig = b.get('code_signature') or ''
            nb = bytes.fromhex(nb_sig.replace(' ', ''))
            if not (nb.startswith(ob) or ob.startswith(nb)) or not b.get('code_signature_unique_in_gcl'):
                rec['locator_broken'] = True
                changed = True
        if changed:
            out['changed'].append(rec)
            for k in ('deps_changed', 'locator_broken', 'param_names_changed'):
                if k in rec:
                    stats[k] += 1
            if rec.get('body_changed'):
                stats['body_changed_' + rec['body_changed']] += 1
        else:
            stats['unchanged'] += 1
    for i, a in oa.items():
        if i not in na and not any(r['old_sig'] == a['sig'] for r in out['return_changed']):
            out['removed'].append({'id': i, 'sig': a['sig'], 'native': a['native'], 'system': top_ns(a['name'])})
            stats['removed_native' if a['native'] else 'removed'] += 1
    return out, stats


def diff_natives(o, n):
    out = collections.defaultdict(list)
    oa = {x['sig']: x for x in o}
    na = {x['sig']: x for x in n}
    stats = collections.Counter()
    for s in sorted(set(oa) | set(na)):
        a, b = oa.get(s), na.get(s)
        if a is None:
            out['added'].append({'id': 'N:' + s, 'rva': b.get('thunk_rva')})
            stats['added'] += 1
            continue
        if b is None:
            out['removed'].append({'id': 'N:' + s, 'rva': a.get('thunk_rva')})
            stats['removed'] += 1
            continue
        ra, rb = a.get('thunk_rva'), b.get('thunk_rva')
        if ra and rb:
            same_bytes = a.get('thunk_signature') == b.get('thunk_signature')
            if ra == rb and same_bytes:
                stats['unchanged'] += 1
            elif same_bytes:
                out['moved_only'].append({'id': 'N:' + s, 'old_rva': ra, 'new_rva': rb})
                stats['moved_only'] += 1
            else:
                out['bytes_changed'].append({'id': 'N:' + s, 'old_rva': ra, 'new_rva': rb,
                                             'old_signature': a.get('thunk_signature'),
                                             'new_signature': b.get('thunk_signature'),
                                             'new_signature_unique': b.get('thunk_signature_unique')})
                stats['bytes_changed'] += 1
        elif ra and not rb:
            out['lost'].append({'id': 'N:' + s, 'old_rva': ra})
            stats['lost'] += 1
        elif rb and not ra:
            out['resolved'].append({'id': 'N:' + s, 'new_rva': rb})
            stats['resolved'] += 1
        else:
            stats['unknown_both'] += 1
    return out, stats


def diff_globals(o, n, ao, an):
    out = collections.defaultdict(list)
    oa = {g['name']: g for g in o}
    na = {g['name']: g for g in n}
    for k in sorted(set(oa) | set(na)):
        a, b = oa.get(k), na.get(k)
        if a is None:
            out['added'].append({'id': 'G:' + k, 'type': b['type']})
        elif b is None:
            out['removed'].append({'id': 'G:' + k, 'type': a['type']})
        else:
            if a['type'] != b['type']:
                out['type_changed'].append({'id': 'G:' + k, 'old': a['type'], 'new': b['type']})
            if a.get('init_order') != b.get('init_order'):
                out['init_order_moved'].append({'id': 'G:' + k, 'old': a.get('init_order'), 'new': b.get('init_order')})
            x, y = ao.get(k), an.get(k)
            if x and y and (x['anchor_sig'], x['slot_index']) != (y['anchor_sig'], y['slot_index']):
                out['anchor_changed'].append({'id': 'G:' + k, 'old': [x['anchor_sig'], x['slot_index']],
                                              'new': [y['anchor_sig'], y['slot_index']]})
    return out


def identity_row(ident):
    f = ident['files']
    return {'label': ident.get('label'), 'steam_build_id': (ident.get('steam') or {}).get('build_id'),
            'game.exe': f.get('game.exe', {}).get('sha256'), 'game.gcl': f.get('game.gcl', {}).get('sha256'),
            'exe_pe_timestamp': f.get('game.exe', {}).get('pe_timestamp'), 'gcl_counts': ident.get('gcl_counts')}


def render_md(d):
    L = []
    w = L.append
    o, n = d['old'], d['new']
    w('# Build comparison: %s -> %s' % (o['label'], n['label']))
    w('')
    w('Generated by `tools/sdk_diff.py`. Every entry in `diff.json` carries the stable symbol ID used across the SDK.')
    w('')
    w('| | old | new |')
    w('|---|---|---|')
    for k in ('steam_build_id', 'game.exe', 'game.gcl', 'exe_pe_timestamp'):
        w('| %s | `%s` | `%s` |' % (k, o.get(k), n.get(k)))
    w('| gcl counts | %s | %s |' % (o['gcl_counts'], n['gcl_counts']))
    w('')
    s = d['summary']
    w('## Summary')
    w('')
    w('| Area | Result |')
    w('|---|---|')
    for k, v in s.items():
        w('| %s | %s |' % (k, ', '.join('%s %s' % (a, b) for a, b in v.items()) if isinstance(v, dict) else v))
    w('')
    w('### What moved without changing')
    w('')
    w('- **Natives moved only: %d.** Thunk RVAs differ but the masked thunk bytes are identical, so code that '
      'locates them by byte signature keeps working; hard-coded RVAs do not.' % s['natives'].get('moved_only', 0))
    w('- **gcl functions unchanged: %d.** gcl code has no static address; identical code bytes mean '
      'identical behaviour and identical locator signatures.' % s['functions'].get('unchanged', 0))
    w('')
    w('### What actually changed')
    w('')
    tc = d['types']['changed']
    lay = [t for t in tc if 'layout_changed' in t['classes'] and not t['name'].startswith(TEMPLATE)]
    w('#### Types with layout changes (%d, excluding template instantiations)' % len(lay))
    w('')
    if lay:
        w('| Type | Size | Fields added | Fields removed | Fields moved | Retyped |')
        w('|---|---|---|---|---|---|')
        for t in lay[:200]:
            c = t['changes']
            w('| `%s` | %s | %s | %s | %s | %s |' % (
                t['name'], '%s -> %s' % tuple(c['size']) if 'size' in c else 'same',
                ', '.join('`%s`@0x%x' % (x['name'], x['offset']) for x in c.get('fields_added', [])[:6]) or '',
                ', '.join('`%s`' % x['name'] for x in c.get('fields_removed', [])[:6]) or '',
                len(c.get('fields_moved', [])) or '',
                ', '.join('`%s`' % x['name'] for x in c.get('fields_retyped', [])[:4]) or ''))
        if len(lay) > 200:
            w('')
            w('%d more in diff.json.' % (len(lay) - 200))
    w('')
    en = [t for t in tc if 'values_renumbered' in t['classes']]
    w('#### Enums whose numeric values moved (%d)' % len(en))
    w('')
    w('A renumbered enum silently changes meaning for any mod that stores or compares raw values.')
    w('')
    for t in en[:80]:
        c = t['changes']
        w('- `%s`: %d renumbered; added %s' % (t['name'], len(c['values_renumbered']),
                                              ', '.join('`%s`' % x for x in c.get('values_added', [])[:8]) or 'none'))
    w('')
    vt = [t for t in tc if any(c.startswith('vtable') for c in t['classes'])]
    w('#### Virtual tables changed (%d)' % len(vt))
    w('')
    for t in vt[:80]:
        v = t['changes']['vtable']
        w('- `%s`: %d -> %d slots%s' % (t['name'], v['old_count'], v['new_count'],
                                         '; %d existing methods shifted slot' % len(v['shifted']) if v['shifted'] else ''))
    w('')
    nb = d['natives'].get('bytes_changed', [])
    w('#### Natives whose code changed (%d)' % len(nb))
    w('')
    for x in nb[:60]:
        w('- `%s` %s -> %s' % (x['id'][2:], x['old_rva'], x['new_rva']))
    w('')
    fc = d['functions']['changed']
    by_sys = collections.Counter(f['system'] for f in fc if f.get('body_changed') in ('instructions', 'constants'))
    w('#### gcl functions with changed instructions or literal values, by namespace')
    w('')
    w('Excludes %d functions whose only difference is bytes after a small literal (`constants_tail`, most likely '
      'compiler padding) and functions whose only difference is unreferenced pool padding.' %
      sum(1 for f in fc if f.get('body_changed') == 'constants_tail'))
    w('')
    w('| Namespace | Functions |')
    w('|---|---|')
    for k, v in by_sys.most_common(40):
        w('| `%s` | %d |' % (k, v))
    w('')
    lb = [f for f in fc if f.get('locator_broken')]
    w('%d previously unique code signatures no longer match (see `locator_broken` in diff.json). '
      'Regenerated anchors in the new json replace them.' % len(lb))
    w('')
    for k in ('added', 'removed'):
        items = [f for f in d['functions'][k] if not f.get('native')]
        w('#### gcl functions %s (%d)' % (k, len(items)))
        w('')
        for f in items[:120]:
            w('- `%s`' % f['sig'])
        if len(items) > 120:
            w('- ... %d more in diff.json' % (len(items) - 120))
        w('')
    g = d['globals']
    w('#### Globals')
    w('')
    for k in ('added', 'removed', 'type_changed'):
        w('- %s: %s' % (k, ', '.join('`%s`' % x['id'][2:] for x in g.get(k, [])) or 'none'))
    w('')
    return '\n'.join(L) + '\n'


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--old', required=True, help='json folder of the older build')
    ap.add_argument('--new', required=True, help='json folder of the newer build')
    ap.add_argument('--out', required=True, help='report folder')
    a = ap.parse_args(argv)
    O, N = load(a.old), load(a.new)
    types = diff_types(O['types'], N['types'])
    funcs, fstats = diff_functions(O['functions'], N['functions'])
    nat, nstats = diff_natives(O['natives'], N['natives'])
    glob = diff_globals(O['globals'], N['globals'], O['anchors'].get('globals', {}), N['anchors'].get('globals', {}))
    tcls = collections.Counter(c for t in types['changed'] for c in t['classes'])
    d = {
        'old': identity_row(O['build_identity']), 'new': identity_row(N['build_identity']),
        'summary': {
            'types': {'added': len(types['added']), 'removed': len(types['removed']), **dict(tcls)},
            'functions': dict(fstats),
            'natives': dict(nstats),
            'globals': {k: len(v) for k, v in glob.items()},
        },
        'types': types, 'functions': funcs, 'natives': nat, 'globals': glob,
    }
    os.makedirs(a.out, exist_ok=True)
    dump_json(d, os.path.join(a.out, 'diff.json'))
    write_text(os.path.join(a.out, 'README.md'), render_md(d))
    print(d['summary'])


if __name__ == '__main__':
    main()
