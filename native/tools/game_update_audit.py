"""Check every exact game-code pattern in native/ against a new Anymaker build.

AnyAPI finds game code by exact bytes: the `unsigned char NAME[]={...}` arrays in native/*.h, *.inc
and *.cpp (gcl bodies matched by prefix at runtime, or game.exe code at a fixed RVA), plus the hex
code signatures in the experimental-SDK `*_bindings.h` files. A game update can move, change or
remove any of them. This tool reads the new game files and the previous build's SDK JSON and sorts
every pattern into one of these states:

  unchanged     - the exact bytes still start a gcl function (or sit at the same game.exe RVA)
  refreshable   - same function, same instructions, same dependencies and same code size; only
                  rip-relative pool displacements or pool padding moved. `--apply` rewrites these
  resized       - same instructions and dependencies, but the code size changed, so any
                  code_end-relative offset used with this pattern (dependency cells, `*_END`,
                  `sizeof(X)+n*8`) must be updated by hand. `--apply` rewrites the bytes only
  shifted       - same instruction layout (every instruction at the same offset, so return-address and
                  call-site offsets still hold), but numbers inside it changed: struct field or stack
                  offsets, enum values in the constant pool. `--apply` rewrites the bytes; check the
                  listed offset hints, where our own code may hard-code one of the old numbers
  changed       - the instructions or the dependency list changed. Review by hand: see the diff file
  moved         - game.exe code found at a different RVA (update the `*_RVA` constant)
  stale         - did not match the previous build either (bytes from an older game build), so
                  the update changes nothing for it
  missing       - not found and no previous function could be identified (usually also stale)

Usage (Windows or Linux, needs `pip install capstone pefile`):
    python native/tools/game_update_audit.py --game-dir "<Anymaker>" --previous-json <old SDK json>
        --out build/game-update [--apply]

`--previous-json` is the json/ folder of the SDK generated for the build the patterns came from
(for 0.1.23 / 25755694: the sdk-reference-2026.10.08 release, sdk/json). It is how a missing
pattern is traced back to its function by signature. Nothing here runs game code; a clean report
still needs the native tests and a gameplay check.
"""
import argparse
import difflib
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NATIVE = ROOT / 'native'
sys.path.insert(0, str(ROOT / 'sdk' / 'experimental' / 'tools'))
import gcl  # noqa: E402
from sdk_analyze import code_hashes  # noqa: E402
from capstone import Cs, CS_ARCH_X86, CS_MODE_64  # noqa: E402

ARRAY = re.compile(r'((?:unsigned char|uint8_t)\s+(\w+)\s*\[\s*\]\s*=\s*\{)([^}]*)(\})')
HEXSTR = re.compile(r'"((?:[0-9a-f]{2} ){7,}[0-9a-f]{2})"')
RVA = re.compile(r'\b(\w+)_RVA\s*=\s*(0x[0-9a-fA-F]+)')
MD = Cs(CS_ARCH_X86, CS_MODE_64)


def sources():
    for path in sorted(list(NATIVE.glob('*.h')) + list(NATIVE.glob('*.inc')) + list(NATIVE.glob('*.cpp'))):
        yield path, path.read_bytes().decode('utf-8')


def collect():
    """Every byte pattern in native/: (file, name, bytes, span, style) plus RVA constants."""
    rows, rvas = [], {}
    for path, text in sources():
        for m in RVA.finditer(text):
            rvas[m[1]] = (path.name, int(m[2], 16))
        for m in ARRAY.finditer(text):
            toks = [t for t in re.split(r'[\s,]+', m[3]) if t]
            if len(toks) < 8 or not all(re.fullmatch(r'0x[0-9a-fA-F]{1,2}|\d{1,3}', t) for t in toks):
                continue
            style = '02' if any(re.fullmatch(r'0x0[0-9a-fA-F]', t) for t in toks) else 'min'
            rows.append(dict(file=path.name, name=m[2], body=bytes(int(t, 0) for t in toks),
                             span=(m.start(3), m.end(3)), style=style, kind='array'))
        if path.name.endswith('_bindings.h'):
            for i, m in enumerate(HEXSTR.finditer(text)):
                rows.append(dict(file=path.name, name='hex@%d' % m.start(1), body=bytes.fromhex(m[1]),
                                 span=(m.start(1), m.end(1)), style='hexstr', kind='binding'))
    return rows, rvas


def normalize(code):
    out = []
    for i in MD.disasm(code, 0):
        op = re.sub(r'rip \+ 0x[0-9a-f]+', 'rip + disp', i.op_str)
        if i.mnemonic.startswith('j') or i.mnemonic == 'call':
            op = re.sub(r'0x[0-9a-f]+', 'target', op)
        out.append('%s %s' % (i.mnemonic, op))
    return out


def shape(code):
    """(address, size, mnemonic, operands with every number masked) per instruction."""
    return [(i.address, i.size, i.mnemonic, re.sub(r'0x[0-9a-f]+|\b\d+\b', 'N', i.op_str)) for i in MD.disasm(code, 0)]


def displacement_changes(old_code, new_code):
    """Counts of number changes between two same-shape instruction streams, e.g. {'0x18->0x20': 7}."""
    out = {}
    for a, b in zip(MD.disasm(old_code, 0), MD.disasm(new_code, 0)):
        if 'rip' in a.op_str or 'rsp' in a.op_str or a.mnemonic.startswith('j') or a.mnemonic == 'call':
            continue  # pool, branch and stack-frame numbers never reach our code
        for x, y in zip(re.findall(r'0x[0-9a-f]+|\b\d+\b', a.op_str), re.findall(r'0x[0-9a-f]+|\b\d+\b', b.op_str)):
            if x != y:
                k = '%s->%s' % (x, y)
                out[k] = out.get(k, 0) + 1
    return dict(sorted(out.items(), key=lambda kv: -kv[1]))


def offset_hints(changes):
    """Lines of our own code (not pattern arrays) that use one of the old numbers literally."""
    hints = []
    olds = {k.split('->')[0] for k in changes if int(k.split('->')[0], 16) >= 0x20}
    for path, text in sources():
        if path.name.endswith(('patterns.h', '_signatures.h', '_bindings.h')):
            continue
        for n, line in enumerate(text.splitlines(), 1):
            if line.count('0x') > 24 or '[]={' in line.replace(' ', ''):
                continue
            for o in olds:
                if re.search(r'(?<![0-9A-Za-z_])(%s|%d)(?![0-9A-Za-z_])' % (re.escape(o), int(o, 16)), line):
                    hints.append('%s:%d uses %s' % (path.name, n, o))
    return hints[:20]


def encode(body, style):
    if style == 'hexstr':
        return ' '.join('%02x' % b for b in body)
    fmt = '0x%02x' if style == '02' else '0x%x'
    return ','.join(fmt % b for b in body)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game-dir', type=Path, required=True)
    ap.add_argument('--previous-json', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--apply', action='store_true', help='rewrite refreshable, resized and shifted patterns in place')
    a = ap.parse_args()
    exe = (a.game_dir / 'game.exe').read_bytes()
    gcl_path = a.game_dir / 'bin' / 'game.gcl'
    prog = gcl.Program(str(gcl_path))
    import pefile
    pe = pefile.PE(data=exe, fast_load=True)

    def exe_at(rva, n):
        try:
            return pe.get_data(rva, n)
        except Exception:
            return b''

    print('loading previous SDK json ...', file=sys.stderr)
    old = json.loads((a.previous_json / 'functions.json').read_text(encoding='utf-8'))
    old_by_prefix = {}
    old_with_sig = [f for f in old if f.get('code_signature')]
    for f in old:
        if f.get('code_signature'):
            old_by_prefix.setdefault(bytes.fromhex(f['code_signature'])[:16], []).append(f)
    new_by_prefix = {}
    for f in prog.functions:
        if f.code_end:
            new_by_prefix.setdefault(f.blob[:16], []).append(f)

    rows, rvas = collect()
    a.out.mkdir(parents=True, exist_ok=True)
    (a.out / 'diffs').mkdir(exist_ok=True)
    edits = {}
    report = []
    for r in rows:
        body = r['body']
        row = dict(file=r['file'], name=r['name'], kind=r['kind'], size=len(body))
        rva_key = r['name'] if r['name'] in rvas else None
        if rva_key:
            rva = rvas[rva_key][1]
            row['rva'] = hex(rva)
            if exe_at(rva, len(body)) == body:
                row['status'] = 'unchanged'
            else:
                at = exe.find(body)
                if at >= 0 and exe.find(body, at + 1) < 0:
                    row.update(status='moved', new_rva=hex(pe.get_rva_from_offset(at)))
                else:
                    row['status'] = 'changed'
                    row['note'] = 'game.exe code changed; find the function again by its callers/strings'
            report.append(row)
            continue
        hits = [f for f in new_by_prefix.get(body[:16], []) if f.blob.startswith(body)] if len(body) >= 16 else \
               [f for f in prog.functions if f.code_end and f.blob.startswith(body)]
        def prefix_of(f):
            sig = bytes.fromhex(f['code_signature'])
            return body.startswith(sig) or sig.startswith(body)
        if hits:
            row.update(status='unchanged', matches=len(hits), function=hits[0].sig if len(hits) == 1 else None)
            # Same bytes, different function: the intended function changed and an old twin now matches.
            was = {f['sig'] for f in old_by_prefix.get(body[:16], []) if prefix_of(f)} if len(body) >= 16 else set()
            if was and not was & {f.sig for f in hits}:
                row.update(status='changed', note='now matches %s; it used to match %s' % (
                    row['function'] or '%d functions' % len(hits), ', '.join(sorted(was))))
            report.append(row)
            continue
        cands = [f for f in old_by_prefix.get(body[:16], []) if prefix_of(f)] or \
                [f for f in old_with_sig if len(f['code_signature']) < 47 and prefix_of(f)]
        sigs = sorted({f['sig'] for f in cands})
        if not sigs and exe.find(body) >= 0:
            row['status'] = 'unchanged'
            row['note'] = 'game.exe code'
            report.append(row)
            continue
        if len(sigs) != 1:
            row.update(status='missing', previous_candidates=sigs[:5])
            report.append(row)
            continue
        prev = cands[0]
        cur = prog.func_by_sig.get(prev['sig'])
        row['function'] = prev['sig']
        if not cur or not cur.code_end:
            row.update(status='missing', note='function no longer exists in the new build')
            report.append(row)
            continue
        old_end, new_end = prev['gcl']['code_size'], cur.code_end
        nh = code_hashes(cur.blob, new_end)
        oh = prev['code_hash']
        same_insn = nh['insn'] == oh['insn'] and nh['consts'] == oh['consts']
        same_relocs = hashlib.sha1('\n'.join('%d %s' % x for x in cur.relocs).encode()).hexdigest() == prev['relocs_sha1']
        row.update(code_end_old=old_end, code_end_new=new_end, same_instructions=same_insn, same_relocations=same_relocs)
        if len(body) >= old_end and hashlib.sha1(body[:old_end]).hexdigest() != oh['raw']:
            # The bytes never matched the previous build either (left over from an older game build).
            row.update(status='stale', note='did not match the previous build either; not a regression')
            report.append(row)
            continue
        # pattern length relative to the old body: whole code, prefix, or code plus relocation slots
        if len(body) <= old_end:
            new_len = new_end if len(body) == old_end else len(body)
        else:
            new_len = len(body) - old_end + new_end
        new_body = cur.blob[:new_len]
        if same_insn and same_relocs:
            row['status'] = 'refreshable' if old_end == new_end else 'resized'
            row['changed_bytes'] = sum(x != y for x, y in zip(body, new_body)) + abs(len(body) - len(new_body))
            edits.setdefault(r['file'], []).append((r['span'], encode(new_body, r['style'])))
        else:
            row['status'] = 'changed'
            old_code, new_code = body[:min(len(body), oh['insn_size'])], cur.blob[:nh['insn_size']]
            row['same_shape'] = len(body) >= oh['insn_size'] and shape(old_code) == shape(new_code)
            if row['same_shape']:
                row['status'] = 'shifted'
                row['number_changes'] = displacement_changes(old_code, new_code)
                row['offset_hints'] = offset_hints(row['number_changes'])
                edits.setdefault(r['file'], []).append((r['span'], encode(new_body, r['style'])))
            row['constants_changed'] = sum(x != y for x, y in zip(oh['const_windows'], nh['const_windows'])) + \
                abs(len(oh['const_windows']) - len(nh['const_windows']))
            d = list(difflib.unified_diff(normalize(old_code), normalize(new_code), 'previous', 'current', lineterm='', n=2))
            consts = ['%d: %s -> %s' % (i, x, y) for i, (x, y) in enumerate(zip(oh['const_windows'], nh['const_windows'])) if x != y]
            fn = re.sub(r'[^A-Za-z0-9_.-]+', '_', '%s__%s' % (r['file'], r['name']))[:150] + '.diff'
            (a.out / 'diffs' / fn).write_text('# %s\n# relocations unchanged: %s\n# changed pool constants (L = lea window, size unknown):\n#   %s\n%s\n' %
                                               (prev['sig'], same_relocs, '\n#   '.join(consts) or 'none', '\n'.join(d)), encoding='utf-8')
            row['diff'] = 'diffs/' + fn
            row['candidate_bytes'] = encode(new_body, 'min')
        report.append(row)

    counts = {}
    for row in report:
        counts[row['status']] = counts.get(row['status'], 0) + 1
    identity = dict(exe_sha256=hashlib.sha256(exe).hexdigest(), gcl_sha256=hashlib.sha256(gcl_path.read_bytes()).hexdigest())
    (a.out / 'pattern_audit.json').write_text(json.dumps(dict(identity=identity, counts=counts, patterns=report), indent=1) + '\n',
                                              encoding='utf-8')
    lines = ['# Pattern audit', '', 'game.exe `%s`' % identity['exe_sha256'], '', 'game.gcl `%s`' % identity['gcl_sha256'], '',
             '| State | Count |', '|---|---|'] + ['| %s | %d |' % kv for kv in sorted(counts.items())] + ['']
    for state in ('missing', 'changed', 'moved', 'shifted', 'resized', 'refreshable', 'stale'):
        rs = [x for x in report if x['status'] == state]
        if not rs:
            continue
        lines += ['## ' + state, '', '| File | Pattern | Function | Notes |', '|---|---|---|---|']
        for x in rs:
            note = x.get('diff') or x.get('note') or x.get('new_rva') or ''
            if x.get('same_shape'):
                note += ' same code layout; numbers: ' + (', '.join('%s (x%d)' % kv for kv in list(x['number_changes'].items())[:6]) or 'none')
            if x.get('constants_changed'):
                note += '; %d pool constants changed' % x['constants_changed']
            if x.get('offset_hints'):
                note += '; CHECK ' + ', '.join(x['offset_hints'])
            if 'code_end_old' in x and x['code_end_old'] != x['code_end_new']:
                note += ' code_end %d -> %d' % (x['code_end_old'], x['code_end_new'])
            lines.append('| %s | %s | %s | %s |' % (x['file'], x['name'], (x.get('function') or '').replace('|', '/'), note.strip()))
        lines.append('')
    (a.out / 'pattern_audit.md').write_text('\n'.join(lines), encoding='utf-8')
    print(json.dumps(counts))
    if a.apply:
        for name, spans in edits.items():
            path = NATIVE / name
            raw = path.read_bytes().decode('utf-8')
            for (s, e), text in sorted(spans, reverse=True):
                old_text = raw[s:e]
                lead = old_text[:len(old_text) - len(old_text.lstrip())]
                trail = old_text[len(old_text.rstrip()):]
                if '\n' in old_text.strip() and not text.startswith('"'):
                    nl = '\r\n' if '\r\n' in old_text else '\n'  # most tracked files are CRLF
                    vals = text.split(',')
                    text = nl.join(','.join(vals[i:i + 16]) + ',' for i in range(0, len(vals), 16))
                    lead, trail = nl, nl
                raw = raw[:s] + lead + text + trail + raw[e:]
            path.write_bytes(raw.encode('utf-8'))
        print('rewrote', sum(len(v) for v in edits.values()), 'patterns in', len(edits), 'files')


if __name__ == '__main__':
    main()
