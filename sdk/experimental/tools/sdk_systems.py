"""Generates the system map and capability matrix from tools/systems_spec.py + json/.

    python tools/sdk_systems.py --json json --docs docs

Writes json/systems.json, json/capabilities.json, docs/systems/<id>.md, docs/capability_matrix.md.
Every name in systems_spec.py is validated against the json; unknown names abort generation.
For each referenced function the generator adds: full signatures, stable IDs, side, thread tags,
how to locate it (unique code signature / caller slot / native RVA) and whether it was located in a
running process (json/runtime_located.json).
"""
import argparse
import collections
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import systems_spec as S  # noqa: E402
from common import dump_json, id_global, id_type, load_json, write_text  # noqa: E402

EXT_KEYS = [('observe', 'Observe an event'), ('change', 'Change a value or behaviour'),
            ('create_destroy', 'Create and destroy objects'), ('register', 'Register a new definition or implementation'),
            ('attach', 'Attach custom behaviour/data'), ('persist', 'Persist custom state'),
            ('replicate', 'Replicate custom state to other players')]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--json', required=True)
    ap.add_argument('--docs', required=True)
    a = ap.parse_args(argv)
    T = load_json(os.path.join(a.json, 'types.json'))
    F = load_json(os.path.join(a.json, 'functions.json'))
    G = {g['name']: g for g in load_json(os.path.join(a.json, 'globals.json'))}
    A = load_json(os.path.join(a.json, 'anchors.json'))
    N = {n['sig']: n for n in load_json(os.path.join(a.json, 'natives.json'))}
    B = {}
    bp = os.path.join(a.json, 'native_bindings.json')
    if os.path.exists(bp):
        B = {b['sig']: b for b in load_json(bp)}
    LOC = set()
    lp = os.path.join(a.json, 'runtime_located.json')
    if os.path.exists(lp):
        LOC = set(load_json(lp)['functions'])
    ident = load_json(os.path.join(a.json, 'build_identity.json'))
    by_name = collections.defaultdict(list)
    for f in F:
        by_name[f['name']].append(f)
    errors = []

    def locate(f):
        if f['native']:
            n = N.get(f['sig'])
            b = B.get(f['sig'])
            if n and n.get('thunk_rva'):
                return {'method': 'native_rva', 'rva': n['thunk_rva'], 'evidence': 'metadata',
                        'runtime_confirmed': bool(b and b['rva'] == n['thunk_rva'])}
            if b:
                return {'method': 'native_rva', 'rva': b['rva'], 'evidence': 'runtime'}
            return {'method': 'unknown', 'evidence': 'unknown'}
        e = A['functions'].get(f['sig'], {})
        out = {'runtime_located': f['id'] in LOC}
        if 'direct' in e:
            out.update({'method': 'code_signature', 'signature': e['direct']['signature']})
        elif 'via_caller' in e:
            v = e['via_caller']
            out.update({'method': 'caller_slot', 'anchor_sig': v['anchor_sig'], 'slot_offset': v['slot_offset']})
        elif 'chain' in e:
            out.update({'method': 'chain', 'root_sig': e['chain']['root_sig'], 'hops': e['chain']['hops']})
        elif 'via_typeinfo' in e:
            v = e['via_typeinfo']
            out.update({'method': 'typeinfo', 'type': v['type'], 'method_slot': v['method_slot']})
        else:
            out.update({'method': 'runtime_only' if f['id'] in LOC else 'unknown'})
        out['evidence'] = 'runtime' if out['runtime_located'] else ('metadata' if out['method'] == 'code_signature' else
                                                                     'static' if out['method'] in ('caller_slot', 'chain', 'typeinfo')
                                                                     else 'unknown')
        return out

    def fn_refs(names, ctx):
        out = []
        for nm in names:
            fs = by_name.get(nm)
            if not fs:
                errors.append('%s: unknown function %s' % (ctx, nm))
                continue
            for f in fs:
                out.append({'id': f['id'], 'sig': f['sig'], 'side': f['side'], 'threads': f['threads'],
                            'native': f['native'], 'abi': f.get('abi_check'), 'locate': locate(f),
                            'virtual_slots': [v['id'] for v in f['vtable_slots']]})
        return out

    def type_refs(names, ctx):
        out = []
        for nm in names:
            if nm not in T:
                if nm not in ('mat34',):
                    errors.append('%s: unknown type %s' % (ctx, nm))
                continue
            t = T[nm]
            out.append({'id': t['id'], 'name': nm, 'size': t['size'], 'kind': t['kind'], 'parent': t['parent'],
                        'methods': len(t['vtable'])})
        return out

    def glob_refs(names, ctx):
        out = []
        for nm in names:
            g = G.get(nm)
            if g is None:
                errors.append('%s: unknown global %s' % (ctx, nm))
                continue
            out.append({'id': g['id'], 'name': nm, 'type': g['type'], 'init_order': g['init_order'],
                        'locate': g['address']['locate']})
        return out

    systems = []
    for s in S.SYSTEMS:
        ctx = 'system ' + s['id']
        pref = tuple(n + '.' for n in s['namespaces'])
        exact = set(s['namespaces'])
        tnames = [n for n in T if n in exact or n.startswith(pref)]
        fsel = [f for f in F if f['name'].startswith(pref) or f['name'] in exact or
                (f['owner_type'] and (f['owner_type'] in exact or f['owner_type'].startswith(pref)))]
        side = collections.Counter(f['side'] for f in fsel if not f['native'])
        thr = collections.Counter(t for f in fsel if not f['native'] for t in (f['threads'] or ['unknown']))
        managers = sorted(n for n in tnames if re.search(r'(manager|container|registry|_definitions?$)', n.split('.')[-1])
                          and T[n]['kind'] == 'struct')[:60]
        events = sorted(n for n in tnames if '.event.' in n or n.startswith('messages_'))[:80]
        ext = {}
        for k, _ in EXT_KEYS:
            st, how = s['extension'].get(k, ('unresolved', ''))
            how = S.MECH.get(how, how)
            ext[k] = {'status': st, 'mechanism': how}
        systems.append({
            'id': s['id'], 'title': s['title'], 'namespaces': s['namespaces'],
            'counts': {'types': len(tnames), 'functions': sum(1 for f in fsel if not f['native']),
                       'native_decls': sum(1 for f in fsel if f['native']),
                       'by_side': dict(side), 'by_thread_tag': dict(thr),
                       'runtime_located': sum(1 for f in fsel if f['id'] in LOC)},
            'roots': {'globals': glob_refs(s['roots']['globals'], ctx), 'types': type_refs(s['roots']['types'], ctx)},
            'key_functions': fn_refs(s['key_functions'], ctx),
            'managers': [{'id': id_type(n), 'name': n, 'size': T[n]['size']} for n in managers],
            'event_types': [id_type(n) for n in events],
            'object_graph': s['object_graph'], 'side': s['side'], 'threads': s['threads'],
            'extension': ext, 'not_present': s.get('not_present', []),
            'classification_evidence': 'side and thread tags are static (namespace, source path, call-graph '
                                       'reachability); see each function\'s side_basis',
        })
    caps = []
    for (cid, cap, sysids, fnames, tnames, gnames, thread, auth, life, status, note) in S.CAPABILITIES:
        ctx = 'capability ' + cid
        caps.append({'id': cid, 'capability': cap, 'systems': sysids,
                     'functions': fn_refs(fnames, ctx), 'types': type_refs(tnames, ctx), 'globals': glob_refs(gnames, ctx),
                     'thread': thread, 'authority': auth, 'lifetime': life, 'status': status, 'note': note})
    if errors:
        print('\n'.join(errors), file=sys.stderr)
        sys.exit(1)
    build = {'steam_build_id': (ident.get('steam') or {}).get('build_id'),
             'game.exe': ident['files']['game.exe']['sha256'], 'game.gcl': ident['files']['game.gcl']['sha256']}
    dump_json({'build': build, 'systems': systems}, os.path.join(a.json, 'systems.json'))
    dump_json({'build': build, 'status_vocabulary': S.__doc__.split('Status vocabulary for extension questions:')[1].strip(),
               'capabilities': caps}, os.path.join(a.json, 'capabilities.json'))

    # ------------------------------------------------------------------ markdown
    def loc_txt(l):
        m = l.get('method')
        if m == 'native_rva':
            return 'game.exe+%s (%s%s)' % (l['rva'], l['evidence'], ', runtime-confirmed' if l.get('runtime_confirmed') else '')
        if m == 'code_signature':
            return 'code signature (%s)' % ('runtime-located' if l.get('runtime_located') else 'metadata')
        if m == 'caller_slot':
            return 'caller slot (%s)' % ('runtime-located' if l.get('runtime_located') else 'static')
        if m == 'chain':
            return 'slot chain, %d hops (%s)' % (len(l['hops']), 'runtime-located' if l.get('runtime_located') else 'static')
        if m == 'typeinfo':
            return 'typeinfo slot %d of %s (%s)' % (l['method_slot'], l['type'], 'runtime-located' if l.get('runtime_located') else 'static')
        if m == 'runtime_only':
            return 'located at runtime through cells/dispatch only'
        return 'unknown'

    for s in systems:
        L = ['# %s' % s['title'], '',
             '> Generated by `tools/sdk_systems.py` from `tools/systems_spec.py` and `json/` for Steam build %s. '
             'Function lists are complete for the key functions only; search `json/sdk_index.sqlite` for the rest.' % build['steam_build_id'], '',
             '**Object graph.** ' + s['object_graph'], '', '**Client/server.** ' + s['side'], '',
             '**Threads.** ' + s['threads'], '',
             '_Classification evidence: %s._' % s['classification_evidence'], '',
             '## Size', '',
             '| Types | gcl functions | Native declarations | By side | Located in a running game |', '|---|---|---|---|---|',
             '| %d | %d | %d | %s | %d |' % (s['counts']['types'], s['counts']['functions'], s['counts']['native_decls'],
                                            ', '.join('%s %d' % kv for kv in sorted(s['counts']['by_side'].items())),
                                            s['counts']['runtime_located']), '',
             '## Roots', '']
        for g in s['roots']['globals']:
            L.append('- global `%s` : `%s` (init order %s; %s)' % (g['name'], g['type'], g['init_order'],
                                                                 'anchored' if g['locate'] else 'no static anchor'))
        for t in s['roots']['types']:
            L.append('- type `%s` (size 0x%x%s)' % (t['name'], t['size'], ', %d virtual methods' % t['methods'] if t['methods'] else ''))
        L += ['', '## Key functions', '', '| Signature | Side | Threads | Locate |', '|---|---|---|---|']
        for f in s['key_functions']:
            L.append('| `%s` | %s | %s | %s |' % (f['sig'].replace('|', '\\|'), f['side'], ','.join(f['threads'] or []) or '-',
                                                 loc_txt(f['locate'])))
        L += ['', '## Extending this system', '', '| Question | Status | Mechanism / reason |', '|---|---|---|']
        for k, label in EXT_KEYS:
            e = s['extension'][k]
            L.append('| %s | **%s** | %s |' % (label, e['status'], e['mechanism'] or '-'))
        if s['not_present']:
            L += ['', '## Not present', ''] + ['- ' + x for x in s['not_present']]
        if s['managers']:
            L += ['', '## Managers and containers in these namespaces', '', ', '.join('`%s`' % m['name'] for m in s['managers'])]
        if s['event_types']:
            L += ['', '## Event and message types', '', ', '.join('`%s`' % e[2:] for e in s['event_types'])]
        write_text(os.path.join(a.docs, 'systems', s['id'] + '.md'), '\n'.join(L) + '\n')
    L = ['# Systems', '', '| System | Types | gcl functions | Observe | Change | Create | Register | Attach | Persist | Replicate |',
         '|---|---|---|---|---|---|---|---|---|---|']
    for s in systems:
        L.append('| [%s](%s.md) | %d | %d | %s |' % (s['title'], s['id'], s['counts']['types'], s['counts']['functions'],
                                                   ' | '.join(s['extension'][k]['status'] for k, _ in EXT_KEYS)))
    write_text(os.path.join(a.docs, 'systems', 'README.md'), '\n'.join(L) + '\n')

    L = ['# Capability matrix', '',
         '> Generated by `tools/sdk_systems.py`. Machine-readable: `json/capabilities.json` (with stable IDs, '
         'signatures and locating data for every referenced symbol). Build: Steam %s.' % build['steam_build_id'], '',
         'Status: **ready** = every primitive runtime-validated; **needs_validation** = mechanism known from metadata/static '
         'analysis, not exercised; **unresolved** = a required step is not understood.', '',
         '| Capability | Systems | Required symbols | Locating | Thread | Authority | Lifetime | Status | Notes |',
         '|---|---|---|---|---|---|---|---|---|']
    for c in caps:
        syms = ['`%s`' % f['sig'].split(' (')[0].split(') ')[-1] for f in c['functions']] + \
               ['`%s`' % g['name'] for g in c['globals']] + ['`%s`' % t['name'] for t in c['types']]
        locs = sorted({loc_txt(f['locate']) for f in c['functions']})
        L.append('| %s | %s | %s | %s | %s | %s | %s | **%s** | %s |' % (
            c['capability'], ', '.join(c['systems']), '<br>'.join(dict.fromkeys(syms)) or '-', '<br>'.join(locs) or '-',
            c['thread'], c['authority'], c['lifetime'], c['status'], c['note'] or ''))
    write_text(os.path.join(a.docs, 'capability_matrix.md'), '\n'.join(L) + '\n')
    print('systems', len(systems), 'capabilities', len(caps))


if __name__ == '__main__':
    main()
