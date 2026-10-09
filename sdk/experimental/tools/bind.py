"""Generate experimental bindings from the complete SDK reference.

python bind.py --reference path/to/reference --match audio_manager --out audio.hpp
Use --signature for one exact overload, or --all for every discovered function.
No game code executes during generation. Missing locating routes remain explicit.
"""
import argparse
import hashlib
import json
from pathlib import Path


def generate(reference, matches, signatures):
    data = reference / 'json'
    functions = json.loads((data / 'functions.json').read_text(encoding='utf-8'))
    anchors = json.loads((data / 'anchors.json').read_text(encoding='utf-8'))
    nb = data / 'native_bindings.json'   # written by validation/merge_runtime.py; absent before a probe
    bindings = {b['sig']: b for b in json.loads(nb.read_text(encoding='utf-8'))} if nb.exists() else {}
    q = lambda value: json.dumps(value or '', ensure_ascii=True)
    lines = ['#pragma once', '#include "anymaker_sdk_runtime.hpp"',
             '// Experimental addresses, not gameplay-tested wrappers. Read the matching reference.',
             'namespace anymaker::bindings {']
    records = []
    for f in functions:
        sig = f['sig']
        if matches or signatures:
            if sig not in signatures and not any(m.casefold() in sig.casefold() for m in matches):
                continue
        key = 'fn_' + hashlib.sha256(sig.encode()).hexdigest()[:20]
        a = anchors['functions'].get(sig, {})
        vc = a.get('via_caller')
        chain = a.get('chain') or a.get('cell_chain') or {}
        root, hops = (vc['anchor_signature'], [vc['slot_offset']]) if vc else (chain.get('root_signature', ''), chain.get('hops', []))
        if len(hops) > 8:
            root, hops = '', []
        vt = a.get('via_typeinfo') or {}
        direct = a.get('direct', {}).get('signature', '')
        route = '{' + q(root) + ', {' + ','.join(str(h) + 'u' for h in hops) + '}, ' + str(len(hops)) + '}'
        lines.append('// ' + sig.replace('\n', ' ') + ' | side: ' + str(f.get('side', 'unknown')))
        lines.append('inline constexpr func_desc ' + key + '{' + ','.join([q(sig), q(direct), route, q(vt.get('anchor_signature')), str(vt.get('slot_offset', 0)) + 'u', str(vt.get('method_slot', -1))]) + '};')
        rva = bindings.get(sig, {}).get('rva')
        if rva:
            rva = int(rva, 0) if isinstance(rva, str) else rva
            lines.append(f'inline constexpr uint32_t {key}_rva = {rva}u;')
        records.append({'symbol': key, 'signature': sig, 'side': f.get('side'),
                        'threads': f.get('threads'), 'abi': f.get('abi'),
                        'has_locator': bool(direct or root or vt), 'native_rva': rva,
                        'status': 'experimental; consult reference evidence'})
    for name, anchor in anchors.get('globals', {}).items():
        if signatures or (matches and not any(m.casefold() in name.casefold() for m in matches)):
            continue
        key = 'global_' + hashlib.sha256(name.encode()).hexdigest()[:20]
        lines.append('inline constexpr global_desc ' + key + '{' + q(name) + ',' + q(anchor['anchor_signature']) + ',' + str(anchor['slot_offset']) + 'u};')
        records.append({'symbol': key, 'signature': 'global:' + name, 'has_locator': True,
                        'status': 'experimental; borrowed address, ownership unchanged'})
    lines.append('} // namespace anymaker::bindings')
    if not records:
        raise ValueError('No matching signatures. Search the reference or use --all.')
    missing = set(signatures) - {r['signature'] for r in records}
    if missing:
        raise ValueError('Unknown exact signatures: ' + repr(sorted(missing)))
    return '\n'.join(lines) + '\n', records


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--reference', type=Path, required=True)
    ap.add_argument('--match', action='append', default=[])
    ap.add_argument('--signature', action='append', default=[])
    ap.add_argument('--all', action='store_true')
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    if not (args.all or args.match or args.signature):
        ap.error('Choose --match, --signature or --all.')
    header, records = generate(args.reference, args.match, args.signature)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(header, encoding='utf-8')
    args.out.with_suffix('.json').write_text(json.dumps(records, indent=2), encoding='utf-8')
    print(f'Generated {len(records)} experimental declarations: {args.out}')


if __name__ == '__main__':
    main()
