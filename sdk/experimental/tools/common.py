"""Shared helpers for the Anymaker SDK generators.

Everything here is pure Python (no game data). Paths are always passed in by the caller so the
tools run the same on Windows and Linux.
"""
import hashlib
import json
import os
import re

TOOLS_VERSION = '2.0.0'
SCHEMA_VERSION = 2

# Symbols outside legitimate-modding scope. Matched against function names, type names, global
# names and native signatures; matching items and edges into them are dropped from every output.
EXCLUDE = re.compile(
    r'pira|banned|gameban|ban_info|ban_evasion|auth_session|auth_ticket|session_ticket|'
    r'support_tracker|crash_report|telemetr|anticheat|application\.http|g_is_anti_|'
    r'state_game_banned|end_steam_auth', re.I)

BUILTIN = {'s32': 4, 'u32': 4, 'f32': 4, 'f64': 8, 'bool': 1, 'u8': 1, 's8': 1, 'uptr': 8, 's64': 8,
           'u64': 8, 's16': 2, 'u16': 2}

# Evidence classes requested for this SDK. Every finding carries one of these.
METADATA = 'metadata'      # read directly from a recorded structure in game.gcl / game.exe
STATIC = 'static'          # derived from code patterns, naming or static call relationships
RUNTIME = 'runtime'        # observed in a running game process (validation/runtime_*.json)
UNKNOWN = 'unknown'

# Mapping from the previous SDK's vocabulary.
OLD_EVIDENCE = {'verified': METADATA, 'inferred': STATIC, 'unknown': UNKNOWN}


def excluded(*names):
    return any(n and EXCLUDE.search(n) for n in names)


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def strip_const(t):
    return t[6:] if t.startswith('const ') else t


def split_top(s):
    out, depth, cur = [], 0, ''
    for ch in s:
        if ch in '<(':
            depth += 1
        elif ch in '>)':
            depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur.strip())
            cur = ''
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def parse_sig(sig):
    """'(ret) name (p1, p2)' -> (ret, name, [p1, p2]). Parens only: one record has an unbalanced '<'."""
    depth = 0
    i = 0
    for i, ch in enumerate(sig):
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
            if depth == 0:
                break
    ret = sig[1:i]
    rest = sig[i + 2:]
    j = rest.index(' (')
    return ret, rest[:j], split_top(rest[j + 2:-1])


# ---------------------------------------------------------------- stable identifiers
# IDs are derived only from names that game.gcl records, never from offsets or record order, so the
# same symbol keeps the same ID across builds as long as the game keeps its name/signature.
def id_type(name):
    return 'T:' + name


def id_field(tname, fname):
    return 'F:%s::%s' % (tname, fname)


def id_func(sig):
    return 'FN:' + sig


def id_func_name(name, ptypes):
    """Overload key without the return type; used to match a function whose return type changed."""
    return 'FK:%s(%s)' % (name, ','.join(strip_const(p) for p in ptypes))


def id_global(name):
    return 'G:' + name


def id_native(sig):
    return 'N:' + sig


def id_vslot(root, slot):
    return 'V:%s#%d' % (root, slot)


def id_enum_value(tname, vname):
    return 'E:%s::%s' % (tname, vname)


def short_hash(sid):
    return hashlib.sha1(sid.encode('utf-8')).hexdigest()[:12]


def dump_json(obj, path, indent=1):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, 'w', encoding='utf-8', newline='\n') as fh:
        json.dump(obj, fh, indent=indent, ensure_ascii=False)
        fh.write('\n')


def load_json(path):
    with open(path, encoding='utf-8') as fh:
        return json.load(fh)


def write_text(path, text):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, 'w', encoding='utf-8', newline='\n') as fh:
        fh.write(text)


def parse_vdf(text):
    """Minimal Valve KeyValues reader (appmanifest_*.acf)."""
    toks = re.findall(r'"((?:[^"\\]|\\.)*)"|([{}])', text)
    stack = [{}]
    key = None
    for s, brace in toks:
        if brace == '{':
            d = {}
            stack[-1][key] = d
            stack.append(d)
            key = None
        elif brace == '}':
            stack.pop()
        elif key is None:
            key = s
        else:
            stack[-1][key] = s
            key = None
    return stack[0]


def find_game_files(game_dir):
    """Resolve the standard files of an Anymaker install or of a copied build folder."""
    out = {'game_dir': os.path.abspath(game_dir)}
    for rel in ('game.exe', os.path.join('bin', 'game.gcl'), 'game.gcl', 'steam_api64.dll'):
        p = os.path.join(game_dir, rel)
        if os.path.isfile(p):
            key = os.path.basename(rel)
            out.setdefault(key, p)
    # Steam layout: <library>/steamapps/common/<installdir>; manifest in <library>/steamapps
    steamapps = os.path.dirname(os.path.dirname(os.path.abspath(game_dir)))
    inst = os.path.basename(os.path.abspath(game_dir))
    if os.path.basename(steamapps).lower() == 'steamapps':
        for fn in os.listdir(steamapps):
            if fn.startswith('appmanifest_') and fn.endswith('.acf'):
                p = os.path.join(steamapps, fn)
                try:
                    st = parse_vdf(open(p, encoding='utf-8', errors='replace').read()).get('AppState', {})
                except Exception:
                    continue
                if st.get('installdir', '').lower() == inst.lower():
                    out['appmanifest'] = p
    return out
