"""Audit/refresh recorded exact patterns against a game build. Requires capstone.

Run with --previous-json SDK/json --gcl GAME/bin/game.gcl --out report.json.
--apply updates only uniquely mapped bodies with unchanged size and instruction shape.
Dependency offsets and live resolution must still pass their native runtime checks.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from gcl_metadata import Program


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--previous-json', type=Path, required=True)
    ap.add_argument('--gcl', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--apply', action='store_true')
    a = ap.parse_args()
    program = Program(str(a.gcl))
    old = json.loads((a.previous_json / 'functions.json').read_text())
    cs = Cs(CS_ARCH_X86, CS_MODE_64)
    native = Path(__file__).resolve().parents[1]
    rows, edits = [], []
    regex = re.compile(r'((?:unsigned char|uint8_t)\s+(\w+)\[\]\s*=\s*\{)([^}]+)(\})')
    for path in sorted(native.glob('*patterns.h')):
        text = path.read_text()
        replacements = []
        for match in regex.finditer(text):
            body = bytes(int(v, 16) for v in re.findall(r'0x([0-9a-fA-F]+)', match[3]))
            if not body:
                continue
            hits = [f for f in program.functions if f.blob.startswith(body)]
            row = dict(file=path.name, pattern=match[2], size=len(body), matches=len(hits))
            if hits:
                row['status'] = 'unchanged'
            else:
                candidates = [f for f in old if f['gcl']['code_size'] == len(body)
                              and f.get('code_signature')
                              and body.startswith(bytes.fromhex(f['code_signature']))]
                if len(candidates) != 1:
                    raise RuntimeError('Ambiguous historical pattern: ' + match[2])
                previous = candidates[0]
                current = program.func_by_sig.get(previous['sig'])
                if not current or current.code_end != len(body):
                    raise RuntimeError('Body size changed: ' + match[2])
                new = current.blob[:current.code_end]
                instructions = list(cs.disasm(body, 0))
                end = max((i.address + i.size for i in instructions if i.mnemonic == 'ret'), default=0)
                if not end:
                    raise RuntimeError('No decoded return: ' + match[2])
                before = list(cs.disasm(body[:end], 0))
                after = list(cs.disasm(new[:end], 0))
                normalize = lambda i: (i.address, i.size, i.mnemonic,
                    re.sub(r'rip\s*[+-]\s*0x[0-9a-f]+', 'rip + displacement', i.op_str))
                if [normalize(i) for i in before] != [normalize(i) for i in after]:
                    raise RuntimeError('Instruction behavior changed: ' + match[2])
                calls = sorted(set(sym for kind, sym in current.relocs if kind == 4))
                # v1 omitted compiler runtime-helper edges from its call graph.
                documented_calls = [s for s in calls if not re.match(r'^\([^)]*\) \$', s)]
                if documented_calls != sorted(set(previous['calls'])):
                    raise RuntimeError('Dependencies changed: ' + match[2])
                row.update(status='refreshed', signature=previous['sig'],
                           instruction_end=end, dependency_set_unchanged=True,
                           changed_bytes=sum(x != y for x, y in zip(body, new)))
                encoded = '\n' + '\n'.join(','.join(f'0x{x:02x}' for x in new[i:i+16]) + ','
                                           for i in range(0, len(new), 16)) + '\n'
                replacements.append((match.start(3), match.end(3), encoded))
            rows.append(row)
        for start, end, replacement in reversed(replacements):
            text = text[:start] + replacement + text[end:]
        edits.append((path, text))
    report = dict(gcl_sha256=hashlib.sha256(a.gcl.read_bytes()).hexdigest(),
                  runtime_acceptance='pending',
                  matches_description='Occurrences before proposed replacements; dependency owners disambiguate shared bodies at runtime.',
                  patterns=rows)
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps(report, indent=2) + '\n')
    if a.apply:
        for path, text in edits:
            path.write_text(text)
    print('Audited', len(rows), 'patterns; refreshed', sum(x['status']=='refreshed' for x in rows))


if __name__ == '__main__':
    main()
