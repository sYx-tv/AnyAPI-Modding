"""Build and query a searchable SQLite index of the SDK json.

    python tools/sdk_query.py --json json --db json/sdk_index.sqlite build
    python tools/sdk_query.py --db json/sdk_index.sqlite find "inventory add item"     # full-text search
    python tools/sdk_query.py --db json/sdk_index.sqlite show "T:server_scene.item"    # one symbol + xrefs
    python tools/sdk_query.py --db json/sdk_index.sqlite callers "FN:() server_tick ()"
    python tools/sdk_query.py --db json/sdk_index.sqlite sql "select count(*) from functions where side='server'"

Tables: symbols(id, kind, name, owner, side, evidence, detail), functions(id, sig, name, owner, side, native,
code_unique, abi, threads, file, line, runtime_located), types(id, name, kind, size, parent, flags),
fields(id, owner, name, type, offset, size), globals(id, name, type), natives(id, sig, rva, evidence,
runtime_rva), xrefs(src, dst, kind) with kind in calls|vcalls|uses_global|checks_type|field_type|param_type|
returns|parent|vslot|binds_native; plus an FTS5 table `search` over id/name/detail.
"""
import argparse
import json
import os
import sqlite3
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import id_func, id_global, id_native, id_type, load_json, strip_const  # noqa: E402

SCHEMA = '''
create table symbols(id text primary key, kind text, name text, owner text, side text, evidence text, detail text);
create table functions(id text primary key, sig text, name text, owner text, side text, native int, code_unique int,
  abi text, threads text, file text, line int, ret text, params text);
create table types(id text primary key, name text, kind text, size int, align int, parent text, flags int,
  nfields int, nmethods int);
create table fields(id text primary key, owner text, name text, type text, offset int, size int);
create table globals(id text primary key, name text, type text, init_order int, anchored int);
create table natives(id text primary key, sig text, rva text, evidence text, runtime_rva text);
create table bindings(id text primary key, rva text, generic text);
create table xrefs(src text, dst text, kind text);
create index xs on xrefs(src); create index xd on xrefs(dst); create index xk on xrefs(kind);
create virtual table search using fts5(id, kind, name, detail, tokenize="unicode61 tokenchars '_.<>$'");
'''


def base_types(t):
    """Type names referenced by a declared type string (ptr<X> -> X, vector<ptr<X>> -> X ...)."""
    t = strip_const(t or '')
    out = []
    for w in ('ptr<', 'ref<', 'vector<', 'array<', 'map<'):
        if t.startswith(w) and t.endswith('>'):
            out.append(t)
            for part in t[len(w):-1].split(','):
                out += base_types(part.strip())
            return out
    return [t] if t else []


def build(a):
    J = a.json
    if os.path.exists(a.db):
        os.remove(a.db)
    db = sqlite3.connect(a.db)
    db.executescript(SCHEMA)
    T = load_json(os.path.join(J, 'types.json'))
    F = load_json(os.path.join(J, 'functions.json'))
    G = load_json(os.path.join(J, 'globals.json'))
    N = load_json(os.path.join(J, 'natives.json'))
    B = load_json(os.path.join(J, 'native_bindings.json')) if os.path.exists(os.path.join(J, 'native_bindings.json')) else []
    xr = []
    srch = []
    for n, t in T.items():
        db.execute('insert into types values(?,?,?,?,?,?,?,?,?)', (t['id'], n, t['kind'], t['size'], t['align'],
                   t['parent'], t['flags'], len(t['fields']), len(t['vtable'])))
        detail = ' '.join([f['name'] or '' for f in t['fields']] + [e['name'] for e in t['enum_values']])
        db.execute('insert into symbols values(?,?,?,?,?,?,?)', (t['id'], t['kind'], n, None, None, t['evidence'], detail[:2000]))
        srch.append((t['id'], t['kind'], n, detail[:4000]))
        if t['parent']:
            xr.append((t['id'], id_type(t['parent']), 'parent'))
        for f in t['fields']:
            if f['name'] is None:
                continue
            db.execute('insert or ignore into fields values(?,?,?,?,?,?)', (f['id'], t['id'], f['name'], f['type'], f['offset'], f['size']))
            for bt in base_types(f['type']):
                if bt in T:
                    xr.append((f['id'], id_type(bt), 'field_type'))
        for v in t['vtable']:
            if v['sig'] and v['introduced_by'] == n:
                xr.append((t['id'], id_func(v['sig']), 'vslot'))
    for f in F:
        params = [p['type'] for p in f['params'] if p['name'] != '$ret']
        db.execute('insert into functions values(?,?,?,?,?,?,?,?,?,?,?,?,?)', (
            f['id'], f['sig'], f['name'], f['owner_type'], f['side'], int(f['native']), int(f['code_signature_unique_in_gcl']),
            f.get('abi_check'), ','.join(f['threads'] or []), f['source']['file'], f['source']['line'], f['return'], json.dumps(params)))
        db.execute('insert or ignore into symbols values(?,?,?,?,?,?,?)', (f['id'], 'native_decl' if f['native'] else 'function',
                   f['name'], id_type(f['owner_type']) if f['owner_type'] else None, f['side'], f['evidence'], f['sig']))
        srch.append((f['id'], 'function', f['name'], f['sig'] + ' ' + (f['source']['file'] or '')))
        for c in f['calls']:
            xr.append((f['id'], id_func(c), 'calls'))
        for c in f['virtual_calls']:
            xr.append((f['id'], id_func(c), 'vcalls'))
        for g in f['globals_used']:
            xr.append((f['id'], id_global(g), 'uses_global'))
        for t in f['types_checked']:
            xr.append((f['id'], id_type(t), 'checks_type'))
        for p in params:
            for bt in base_types(p):
                if bt in T:
                    xr.append((f['id'], id_type(bt), 'param_type'))
        for bt in base_types(f['return'] or ''):
            if bt in T:
                xr.append((f['id'], id_type(bt), 'returns'))
    for g in G:
        db.execute('insert into globals values(?,?,?,?,?)', (g['id'], g['name'], g['type'], g['init_order'],
                                                             int(bool(g['address'].get('locate')))))
        db.execute('insert or ignore into symbols values(?,?,?,?,?,?,?)', (g['id'], 'global', g['name'], None, None, g['evidence'], g['type']))
        srch.append((g['id'], 'global', g['name'], g['type']))
    for n in N:
        db.execute('insert into natives values(?,?,?,?,?)', (n['id'], n['sig'], n.get('thunk_rva'), n['evidence'],
                                                             (n.get('runtime') or {}).get('rva')))
        srch.append((n['id'], 'native', n['name'], n['sig']))
    for b in B:
        db.execute('insert or ignore into bindings values(?,?,?)', (b['id'], b['rva'], ','.join(b['generic_natives'])))
        for g in b['generic_natives']:
            xr.append((b['id'], g, 'binds_native'))
    db.executemany('insert into xrefs values(?,?,?)', xr)
    db.executemany('insert into search values(?,?,?,?)', srch)
    db.commit()
    print('indexed', len(T), 'types', len(F), 'functions', len(G), 'globals', len(N), 'natives', len(xr), 'xrefs')


def q(db, sql, args=()):
    return db.execute(sql, args).fetchall()


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--json', default=None)
    ap.add_argument('--db', required=True)
    ap.add_argument('cmd', choices=['build', 'find', 'show', 'callers', 'callees', 'sql'])
    ap.add_argument('arg', nargs='?')
    ap.add_argument('--limit', type=int, default=40)
    a = ap.parse_args(argv)
    if a.cmd == 'build':
        return build(a)
    db = sqlite3.connect(a.db)
    if a.cmd == 'find':
        terms = ' '.join('"%s"*' % w.replace('"', '') for w in a.arg.split())
        for r in q(db, 'select id, kind from search where search match ? order by rank limit ?', (terms, a.limit)):
            print('%-12s %s' % (r[1], r[0]))
    elif a.cmd == 'show':
        for r in q(db, 'select * from symbols where id=?', (a.arg,)):
            print(r)
        if a.arg.startswith('T:'):
            for r in q(db, 'select offset, name, type, size from fields where owner=? order by offset', (a.arg,)):
                print('  +0x%04x %-30s %s (%s)' % r)
        print('-- references from'); [print('  ', k, d) for d, k in q(db, 'select dst, kind from xrefs where src=? limit ?', (a.arg, a.limit))]
        print('-- references to'); [print('  ', k, s) for s, k in q(db, 'select src, kind from xrefs where dst=? limit ?', (a.arg, a.limit))]
    elif a.cmd in ('callers', 'callees'):
        col, other = ('dst', 'src') if a.cmd == 'callers' else ('src', 'dst')
        for (r,) in q(db, "select %s from xrefs where %s=? and kind in ('calls','vcalls') limit ?" % (other, col), (a.arg, a.limit)):
            print(r)
    elif a.cmd == 'sql':
        for r in q(db, a.arg):
            print(r)


if __name__ == '__main__':
    main()
