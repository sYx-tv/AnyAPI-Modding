"""Builds the Anymaker SDK database (json/*.json) from game.gcl + game.exe.

Usage (Windows or Linux):
    python tools/sdk_analyze.py --game-dir "C:/Program Files (x86)/Steam/steamapps/common/Anymaker" --out json
    python tools/sdk_analyze.py --gcl path/game.gcl --exe path/game.exe [--dll extra.dll ...] --out json

Everything comes from metadata recorded in game.gcl, from game.exe's native binding registrations,
or from byte patterns. No function bodies are decompiled or emitted.

Evidence classes (see common.py):
    metadata - read directly from a recorded structure; `ref` gives gcl+0x... or game.exe+0x...
    static   - derived from code patterns, naming or static call relationships; `basis` says how
    runtime  - observed in a running process (only added by validation/merge_runtime.py)
    unknown  - not determinable from the static files
"""
import argparse
import bisect
import collections
import os
import re
import sys
import time

import capstone
import pefile
from capstone import x86 as X

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gcl  # noqa: E402
from common import (BUILTIN, METADATA, SCHEMA_VERSION, STATIC, TOOLS_VERSION, UNKNOWN, dump_json,  # noqa: E402
                    excluded, find_game_files, id_enum_value, id_field, id_func, id_func_name, id_global,
                    id_native, id_type, id_vslot, parse_sig, parse_vdf, sha256, short_hash, strip_const)

MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
MD.detail = True
REGS = ['rcx', 'rdx', 'r8', 'r9']
SIGLEN = 32

# Engine entry points that game.exe calls by name (strings in game.exe; see docs/guides/lifecycle.md).
ROOTS = {
    'on_create': '() on_create ()', 'on_destroy': '() on_destroy ()', 'on_update': '() on_update ()',
    'on_render': '() on_render ()', 'server_tick': '() server_tick ()',
    'client_tile_worker': '() client_scene.environment_tile_task.execute (client_scene.environment_tile_task)',
    'client_vehicle_worker': '() client_scene.vehicle_task.execute (client_scene.vehicle_task)',
    'server_tile_worker': '() server_scene.environment_tile_task.execute (server_scene.environment_tile_task)',
    'on_input_character': '() on_input_character (const s32)',
    'on_input_character_action': '() on_input_character_action (const application.input.e_action_text_event)',
    'on_input_pointer': '() on_input_pointer (const application.input.pointer_data, const application.input.pointer_delta)',
    'on_action_digital': '() on_action_digital (const s32, const bool)',
    'on_action_axis': '() on_action_axis (const s32, const f64)',
    'on_physics_contact_added': '() on_physics_contact_added (physics.object, physics.object)',
    'on_physics_contact_persisted': '() on_physics_contact_persisted (physics.object, physics.object)',
}
THREAD_OF_ROOT = {'server_tick': 'server', 'client_tile_worker': 'worker', 'client_vehicle_worker': 'worker',
                  'server_tile_worker': 'worker'}


def log(*a):
    print('[analyze]', *a, file=sys.stderr, flush=True)


# ====================================================================================== identity
def pe_info(path):
    pe = pefile.PE(path, fast_load=True)
    mach = pe.FILE_HEADER.Machine
    return {
        'pe_timestamp': pe.FILE_HEADER.TimeDateStamp,
        'machine': {0x8664: 'x64', 0x14c: 'x86', 0xaa64: 'arm64'}.get(mach, hex(mach)),
        'image_base': hex(pe.OPTIONAL_HEADER.ImageBase),
        'size_of_image': hex(pe.OPTIONAL_HEADER.SizeOfImage),
        'entry_point_rva': hex(pe.OPTIONAL_HEADER.AddressOfEntryPoint),
        'sections': [{'name': s.Name.rstrip(b'\0').decode('latin1'), 'rva': hex(s.VirtualAddress),
                      'vsize': hex(s.Misc_VirtualSize)} for s in pe.sections],
    }


def version_resource(path):
    try:
        pe = pefile.PE(path)
        for fi in getattr(pe, 'FileInfo', []) or []:
            for e in fi:
                for st in getattr(e, 'StringTable', []) or []:
                    for k, v in st.entries.items():
                        if k in (b'ProductVersion', b'FileVersion') and v.strip():
                            return v.decode('latin1')
    except Exception:
        pass
    return None


def build_identity(args, prog):
    files = {}
    for role, path in [('game.gcl', args.gcl), ('game.exe', args.exe)] + [(os.path.basename(d), d) for d in args.dll]:
        if not path:
            continue
        e = {'path_hint': os.path.basename(path), 'sha256': sha256(path), 'size': os.path.getsize(path)}
        if not path.lower().endswith('.gcl'):
            e.update(pe_info(path))
        files[role] = e
    ver = version_resource(args.exe) if args.exe else None
    ident = {
        'label': args.label,
        'schema_version': SCHEMA_VERSION, 'tools_version': TOOLS_VERSION,
        'generated_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
        'architecture': {'value': 'x86-64 (Windows PE32+, gcl code is x86-64 machine code)', 'evidence': METADATA,
                         'basis': 'PE FILE_HEADER.Machine of game.exe; gcl blobs disassemble as x86-64'},
        'files': files,
        'game_version': {'value': ver, 'evidence': METADATA if ver else UNKNOWN,
                         'basis': 'game.exe VERSIONINFO resource' if ver else
                         'game.exe has no version resource (FileVersionRaw 0.0.0.0); the version string comes from native '
                         'application.get_version at runtime and is not a literal in game.exe or game.gcl'},
        'steam': {'app_id': None, 'build_id': None, 'evidence': UNKNOWN, 'basis': 'no appmanifest given (--manifest)'},
        'gcl_counts': {'files': len(prog.files), 'globals': len(prog.globals), 'functions': len(prog.functions),
                       'types': len(prog.types)},
    }
    if args.manifest:
        st = parse_vdf(open(args.manifest, encoding='utf-8', errors='replace').read()).get('AppState', {})
        ident['steam'] = {
            'app_id': st.get('appid'), 'build_id': st.get('buildid'), 'target_build_id': st.get('TargetBuildID'),
            'last_updated_unix': int(st['LastUpdated']) if st.get('LastUpdated', '').isdigit() else None,
            'depots': {k: v.get('manifest') for k, v in st.get('InstalledDepots', {}).items()},
            'state_flags': st.get('StateFlags'),
            'evidence': METADATA, 'basis': 'steamapps/' + os.path.basename(args.manifest),
            'caveat': 'The manifest describes what Steam installed. If game files were replaced by hand, compare the hashes above.'}
    return ident


# ====================================================================================== natives
def find_natives(exe, by_sig):
    """Native functions are registered in game.exe by signature string; the registration stores a
    thunk pointer at node+0x28. The registration copies the signature string into a heap buffer chunk
    by chunk, sets the length with `mov dword [rbp-8], len`, then `lea rax,[rip+thunk]; mov [node+0x28],rax`.
    We rebuild the string from the copies, so pairing does not depend on xref heuristics."""
    import numpy as np
    pe = pefile.PE(exe)
    data = pe.get_memory_mapped_image()
    text = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
    tva, tsz = text.VirtualAddress, text.Misc_VirtualSize
    sigs = {}
    for m in re.finditer(rb'\(([^\x00\n]{0,200})\) ([A-Za-z_$][\w.$<>, ]*) \(([^\x00\n]*)\)\x00', data):
        sigs[m.start()] = m.group(0)[:-1].decode('latin1')
    t = np.frombuffer(data[tva:tva + tsz], dtype=np.uint8).astype(np.int64)
    disp = t[:-3] | (t[1:-2] << 8) | (t[2:-1] << 16) | (t[3:] << 24)
    disp = np.where(disp >= 2 ** 31, disp - 2 ** 32, disp)
    pos = np.arange(len(disp)) + tva + 4
    keys = np.array(sorted(sigs))
    first_ref = {}
    for extra in range(0, 5):
        tg = pos + disp + extra
        for i in np.nonzero(np.isin(tg, keys))[0]:
            s = int(tg[i])
            a = int(pos[i] - 4)
            if s not in first_ref or a < first_ref[s]:
                first_ref[s] = a
    text_lo, text_hi = tva, tva + tsz
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXCEPTION']])
    starts = sorted((e.struct.BeginAddress, e.struct.EndAddress) for e in getattr(pe, 'DIRECTORY_ENTRY_EXCEPTION', []))
    sb = [s for s, _ in starts]

    def func_bounds(rva):
        k = bisect.bisect_right(sb, rva) - 1
        if k >= 0 and starts[k][0] <= rva < starts[k][1]:
            return starts[k]
        return None

    text_bytes = data[text_lo:text_hi]
    sig_by_text = {v: k for k, v in sigs.items()}
    reg_funcs = sorted({func_bounds(r) for r in first_ref.values()} - {None})
    found = {}

    def fam(r):
        n = MD.reg_name(r)
        if n.startswith('xmm'):
            return n
        for f, al in (('rax', ('eax', 'ax', 'al')), ('rbx', ('ebx', 'bx', 'bl')), ('rcx', ('ecx', 'cx', 'cl')),
                      ('rdx', ('edx', 'dx', 'dl')), ('rsi', ('esi', 'si', 'sil')), ('rdi', ('edi', 'di', 'dil'))):
            if n == f or n in al:
                return f
        m_ = re.match(r'(r\d+)[dwb]?$', n)
        return m_.group(1) if m_ else n

    for lo, hi in reg_funcs:
        regsrc, buf, pending, last_store = {}, {}, None, None
        for ins in MD.disasm(data[lo:hi], lo):
            m = ins.mnemonic
            ops = ins.operands
            if m == 'mov' and last_store and ins.address == last_store[0] + 7 and ins.op_str.startswith('qword ptr [') \
                    and ins.op_str.endswith(', rax'):
                if pending is not None:
                    found.setdefault(pending, []).append(last_store)
                last_store = None
                continue
            if m in ('movups', 'movaps', 'movsd', 'mov', 'movzx', 'movdqu', 'movdqa', 'movq') and len(ops) == 2:
                d, sop = ops
                if d.type == X.X86_OP_REG and sop.type == X.X86_OP_MEM and sop.mem.base == X.X86_REG_RIP:
                    regsrc[fam(d.reg)] = (ins.address + ins.size + sop.mem.disp, sop.size)
                    continue
                if d.type == X.X86_OP_MEM and sop.type == X.X86_OP_REG and fam(sop.reg) in regsrc and \
                        d.mem.base not in (X.X86_REG_RBP, X.X86_REG_RSP, X.X86_REG_RIP):
                    src, sz = regsrc[fam(sop.reg)]
                    buf[d.mem.disp] = data[src:src + min(sz, d.size)]
                    continue
                if d.type == X.X86_OP_MEM and d.mem.base == X.X86_REG_RBP and d.mem.disp == -8 and \
                        sop.type == X.X86_OP_IMM and m == 'mov':
                    raw = b''
                    for off in sorted(buf):
                        if off > len(raw):
                            raw += b'\0' * (off - len(raw))
                        raw = raw[:off] + buf[off] + raw[off + len(buf[off]):]
                    txt = raw[:sop.imm].decode('latin1')
                    pending = txt if txt in sig_by_text else None
                    buf = {}
                    continue
            if m == 'lea' and len(ops) == 2 and ops[1].type == X.X86_OP_MEM and ops[1].mem.base == X.X86_REG_RIP:
                tgt = ins.address + ins.size + ops[1].mem.disp
                if text_lo <= tgt < text_hi:
                    last_store = (ins.address, tgt)
                continue

    out = []
    for sig_text, s_ in sig_by_text.items():
        if excluded(sig_text):
            continue
        ref = first_ref.get(s_)
        stores = found.get(sig_text, [])
        tg = sorted({t for _, t in stores})
        fb = func_bounds(ref) if ref else None
        ret, name, ptypes = parse_sig(sig_text)
        rec = {'id': id_native(sig_text), 'sig': sig_text, 'name': name, 'module': 'game.exe', 'string_rva': hex(s_),
               'registration_rva': hex(fb[0]) if fb else None, 'declared_in_gcl': sig_text in by_sig}
        if len(tg) == 1:
            pat, uniq = byte_sig(data, tg[0], text_bytes)
            rec.update({'thunk_rva': hex(tg[0]), 'store_rvas': [hex(a) for a, _ in stores], 'evidence': METADATA,
                        'basis': 'signature string rebuilt from the registration copy sequence, followed by thunk store '
                                 'lea+mov [node+0x28]',
                        'thunk_signature': pat, 'thunk_signature_unique': uniq,
                        'thunk_masked_hash': short_hash(pat)})
        else:
            rec.update({'thunk_rva': None, 'thunk_candidates': [hex(t) for t in tg] or None, 'evidence': UNKNOWN,
                        'basis': 'no single thunk store found after the rebuilt signature string; resolve at runtime '
                                 'through a gcl caller\'s call slot (validation/probe.py)'})
        out.append(rec)
    out.sort(key=lambda r: r['sig'])
    return out, pe


def byte_sig(data, rva, text_bytes, maxlen=48):
    """Pattern with rip-relative displacements and rel32 branch targets wildcarded."""
    pat = []
    for ins in MD.disasm(data[rva:rva + 96], rva):
        b = list(ins.bytes)
        wild = [False] * len(b)
        if ins.mnemonic.startswith(('call', 'j')) and ins.size >= 5 and ins.operands and \
                ins.operands[0].type == X.X86_OP_IMM:
            for k in range(len(b) - 4, len(b)):
                wild[k] = True
        for op in ins.operands:
            if op.type == X.X86_OP_MEM and op.mem.base == X.X86_REG_RIP:
                for k in range(ins.disp_offset, ins.disp_offset + 4):
                    wild[k] = True
        for x, w in zip(b, wild):
            pat.append(None if w else x)
        if len(pat) >= maxlen or ins.mnemonic in ('ret', 'jmp'):
            break
    rx = re.compile(b''.join(re.escape(bytes([x])) if x is not None else b'.' for x in pat), re.S)
    hits = 0
    for _ in rx.finditer(text_bytes):
        hits += 1
        if hits > 1:
            break
    return ' '.join('??' if x is None else '%02x' % x for x in pat), hits == 1



# ====================================================================================== code hashing
def _const_size(ins, op):
    m = ins.mnemonic
    if m == 'lea':
        return 0                       # pointer to a constant argument; size decided from the bytes
    if m.endswith('sd'):
        return 8
    if m.endswith('ss'):
        return 4
    return max(op.size, 1)


def _const_bytes(blob, t, n, end):
    """Bytes of one referenced constant. Operand size is used when the instruction gives one. A lea
    passes a pointer to a constant argument (bool/s32/f64/string literal) whose size the instruction
    does not give: strings are read to their NUL, everything else as an 8-byte window. The compiler
    leaves uninitialised padding after small literals, so sdk_diff.py compares windows jointly."""
    if n:
        return blob[t:min(t + n, end)]
    b = blob[t:min(t + 8, end)]
    if len(b) >= 4 and all(32 <= c < 127 for c in b[:4]):
        z = blob.find(b'\x00', t, end)
        return blob[t:z if z >= 0 else end]
    return b


def code_hashes(blob, code_end):
    """Hash a gcl code blob so build diffs can tell codegen changes from data changes.

    The blob is [instructions][inline constant pool][relocation slots]. Pool bytes that no instruction
    references are alignment padding with leftover garbage that changes between builds, so they are
    ignored. Rip-relative displacements into the pool are masked in the instruction hash (pool layout
    shifts are not behavior), and the referenced constant values are hashed in reference order.
      raw    - every byte of blob[:code_end]
      insn   - instruction bytes with pool displacements masked
      consts - referenced constant windows, in instruction order (const_windows keeps them; an 'L'
               prefix marks a lea window whose true size is unknown)
    """
    import hashlib
    pool = code_end
    refs = []
    for ins in MD.disasm(blob[:code_end], 0):
        if ins.address >= pool:
            break
        for op in ins.operands:
            if op.type == X.X86_OP_MEM and op.mem.base == X.X86_REG_RIP:
                t = ins.address + ins.size + op.mem.disp
                if ins.address + ins.size <= t < code_end:
                    pool = min(pool, t)
                    refs.append((ins.address + ins.disp_offset, t, _const_size(ins, op)))
    insn = bytearray(blob[:pool])
    windows = []
    for d, t, n in refs:
        if d + 4 <= len(insn):
            insn[d:d + 4] = bytes(4)
        windows.append(('L' if n == 0 else '') + _const_bytes(blob, t, n, code_end).hex())
    return {'raw': hashlib.sha1(blob[:code_end]).hexdigest(), 'insn': hashlib.sha1(bytes(insn)).hexdigest(),
            'consts': hashlib.sha1('|'.join(windows).encode()).hexdigest(), 'const_windows': windows,
            'insn_size': pool}


# ====================================================================================== ABI check
def prologue_spills(blob, limit):
    spills, alias, n = {}, {}, 0
    for ins in MD.disasm(blob[:min(limit, 160)], 0):
        n += 1
        if n > 24:
            break
        if ins.mnemonic == 'mov' and len(ins.operands) == 2:
            d, s = ins.operands
            if d.type == X.X86_OP_MEM and d.mem.base == X.X86_REG_RSP and s.type == X.X86_OP_REG:
                r = MD.reg_name(s.reg)
                spills[d.mem.disp] = alias.get(r, r)
            elif d.type == X.X86_OP_REG and s.type == X.X86_OP_REG and MD.reg_name(s.reg) in REGS:
                alias[MD.reg_name(d.reg)] = MD.reg_name(s.reg)
        if ins.mnemonic in ('call', 'ret'):
            break
    return spills


def abi_check(pf, ret, params):
    slots = (['$ret'] if ret else []) + [n for _, n, _ in params]
    regs = {nm: (REGS[i] if i < 4 else 'stack+0x%x' % (0x28 + 8 * (i - 4))) for i, nm in enumerate(slots)}
    if pf.code_end == 0:
        return {'status': 'native', 'arg_locations': regs}
    spills = prologue_spills(pf.blob, pf.code_end)
    checked = ok = 0
    for i, (_, _, off) in enumerate(params):
        pos = i + (1 if ret else 0)
        if pos >= 4:
            continue
        checked += 1
        if spills.get(off) == REGS[pos]:
            ok += 1
    st = 'no-register-params' if checked == 0 else ('verified' if ok == checked else 'mismatch')
    return {'status': st, 'arg_locations': regs}


# ====================================================================================== main
def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game-dir', help='Anymaker install or copied build folder (finds game.exe, bin/game.gcl, appmanifest)')
    ap.add_argument('--gcl', help='path to game.gcl (overrides --game-dir)')
    ap.add_argument('--exe', help='path to game.exe (overrides --game-dir)')
    ap.add_argument('--manifest', help='steamapps/appmanifest_<appid>.acf (found automatically for a Steam install)')
    ap.add_argument('--dll', action='append', default=[], help='extra module to fingerprint (repeatable)')
    ap.add_argument('--label', default=None, help='free-form build label stored in build_identity.json')
    ap.add_argument('--out', required=True, help='output folder for the JSON database')
    ap.add_argument('--no-natives', action='store_true', help='skip the game.exe native scan (faster)')
    args = ap.parse_args(argv)
    if args.game_dir:
        gf = find_game_files(args.game_dir)
        args.gcl = args.gcl or gf.get('game.gcl')
        args.exe = args.exe or gf.get('game.exe')
        args.manifest = args.manifest or gf.get('appmanifest')
    if not args.gcl:
        ap.error('game.gcl not found; pass --gcl or --game-dir')
    out = args.out
    t0 = time.time()
    log('reading', args.gcl)
    p = gcl.Program(args.gcl)
    files = {k: v.split('/work/ci/win32/', 1)[-1] for k, v in p.files.items()}
    ident = build_identity(args, p)

    # ---------------------------------------------------------------------------------- types
    tb = {t.name: t for t in p.types}

    def tsize(n):
        n = strip_const(n)
        if n in BUILTIN:
            return BUILTIN[n]
        if n == 'string':
            return 16
        if n.startswith('ptr<'):
            return 8
        if n.startswith('ref<'):
            return 16
        return tb[n].size if n in tb else None

    def chain(n):
        res = []
        while n in tb and n not in res:
            res.append(n)
            n = tb[n].parent
        return res

    children = collections.defaultdict(list)
    for t in p.types:
        if t.parent:
            children[t.parent].append(t.name)

    def slot_root(tname, i):
        root = tname
        for a in chain(tname)[1:]:
            if len(tb[a].methods) > i:
                root = a
        return root

    types = {}
    for t in p.types:
        if excluded(t.name):
            continue
        anc = chain(t.name)[1:]
        own = []
        for ft, fn, fo in t.fields:
            hide = excluded(fn, ft)
            own.append({'id': id_field(t.name, '@0x%x' % fo if hide else fn), 'name': None if hide else fn, 'type': None if hide else ft,
                        'offset': fo, 'size': tsize(ft), 'out_of_scope': hide or None,
                        'evidence': METADATA, 'ref': 'gcl+0x%x' % t.offset})
        vt = []
        for i, m in enumerate(t.methods):
            root = slot_root(t.name, i)
            parent_m = tb[t.parent].methods[i] if t.parent in tb and len(tb[t.parent].methods) > i else None
            vt.append({'slot': i, 'id': id_vslot(root, i), 'sig': None if excluded(m) else m, 'introduced_by': root,
                       'inherited': parent_m == m, 'overrides': parent_m if parent_m and parent_m != m else None})
        types[t.name] = {
            'id': id_type(t.name), 'name': t.name, 'kind': {13: 'struct', 14: 'enum'}.get(t.kind, t.kind),
            'size': t.size, 'align': t.align, 'flags': t.flags,
            'flag_bits': {'has_typeinfo_header': bool(t.flags & 1), 'nontrivial_lifetime': bool(t.flags & 2),
                          'is_ptr': bool(t.flags & 8), 'is_ref': bool(t.flags & 16), 'container': bool(t.flags & 64)},
            'flag_bits_evidence': STATIC,
            'flag_bits_basis': 'bit meanings inferred from which types carry them (ptr<>/ref<>/vector<> families, '
                               'types with a method table, types with ctor/dtor names)',
            'parent': t.parent or None, 'ancestors': anc, 'children': sorted(c for c in children.get(t.name, []) if not excluded(c)),
            'fields': own,
            'enum_values': [{'id': id_enum_value(t.name, n), 'name': n, 'value': i,
                             'value_evidence': STATIC} for i, n in enumerate(t.enum)],
            'vtable': vt,
            'ctor': t.ctor or None, 'copy_ctor': t.cctor or None, 'copy': t.copy or None, 'dtor': t.dtor or None,
            'ref': 'gcl+0x%x' % t.offset, 'evidence': METADATA,
        }
    log('types', len(types))

    # ---------------------------------------------------------------------------------- call graph
    by_sig = {}
    for i, f in enumerate(p.functions):
        by_sig.setdefault(f.sig, i)
    impls = collections.defaultdict(set)
    slot_of = collections.defaultdict(set)
    for t in p.types:
        for i, m in enumerate(t.methods):
            key = (slot_root(t.name, i), i)
            impls[key].add(m)
            slot_of[m].add(key)

    calls = collections.defaultdict(set)
    vcalls = collections.defaultdict(set)
    for i, f in enumerate(p.functions):
        for k, s in f.relocs:
            if k == 4 and s in by_sig:
                calls[i].add(by_sig[s])
            elif k == 6 and s in by_sig:
                vcalls[i].add(by_sig[s])
    called_by = collections.defaultdict(set)
    for a, bs in calls.items():
        for b in bs:
            called_by[b].add(a)
    vcalled_by = collections.defaultdict(set)
    for a, bs in vcalls.items():
        for b in bs:
            vcalled_by[b].add(a)

    # Side of each function from its namespace / source path, used to keep virtual-call expansion from
    # leaking server implementations into client roots and the reverse.
    def fside(i):
        f = p.functions[i]
        nm = parse_sig(f.sig)[1]
        pl = (files.get(f.file_id) if f.file_id >= 0 else '') or ''
        top = nm.split('.', 1)[0]
        if top.startswith('server') or '/gc/server/' in pl:
            return 'server'
        if top.startswith(('client', 'frontend_ui')) or '/gc/client/' in pl:
            return 'client'
        return 'shared'

    ROOT_DOMAIN = {'server_tick': 'server', 'server_tile_worker': 'server', 'client_tile_worker': 'client',
                   'client_vehicle_worker': 'client'}

    def succ(i, domain):
        res = set(calls.get(i, ()))
        for v in vcalls.get(i, ()):
            res.add(v)
            for key in slot_of.get(p.functions[v].sig, ()):
                for m in impls[key]:
                    j = by_sig.get(m)
                    if j is None:
                        continue
                    sd = fside(j)
                    if domain == 'client' and sd == 'server' or domain == 'server' and sd == 'client':
                        continue
                    res.add(j)
        return res

    reach = collections.defaultdict(set)
    for rn, rs in ROOTS.items():
        if rs not in by_sig:
            log('root missing', rs)
            continue
        domain = ROOT_DOMAIN.get(rn, 'client')   # main-thread roots drive the client
        seen = {by_sig[rs]}
        work = [by_sig[rs]]
        while work:
            x = work.pop()
            for y in succ(x, domain):
                if y not in seen:
                    seen.add(y)
                    work.append(y)
        for x in seen:
            reach[x].add(rn)

    # ---------------------------------------------------------------------------------- code signatures
    code_index = collections.defaultdict(list)
    for i, f in enumerate(p.functions):
        if f.code_end:
            code_index[f.blob[:16]].append(i)

    def unique_prefix(i):
        f = p.functions[i]
        code = f.blob[:f.code_end]
        cands = code_index.get(code[:16], [])
        n = 16
        while len(cands) > 1 and n < len(code):
            n = min(len(code), n * 2)
            cands = [j for j in cands if p.functions[j].blob[:n] == code[:n]]
        if len(cands) == 1:
            lo = max(8, n // 2)
            while lo < n:
                mid = (lo + n) // 2
                if sum(1 for j in code_index.get(code[:16], []) if p.functions[j].blob[:mid] == code[:mid]) == 1:
                    n = mid
                else:
                    lo = mid + 1
            n = max(n, min(SIGLEN, len(code)))   # 32 bytes keep it unique when scanning a whole process
            return code[:n].hex(' '), True
        return code[:min(len(code), 64)].hex(' '), False

    def side_of(name, path):
        pl = path or ''
        top = name.split('.', 1)[0]
        if top.startswith('server') or '/gc/server/' in pl:
            return 'server'
        if top.startswith(('client', 'frontend_ui')) or '/gc/client/' in pl:
            return 'client'
        return 'shared'

    def owner_type(name, ptypes):
        if ptypes:
            r = strip_const(ptypes[0])
            if name.startswith(r + '.'):
                return r
        return None

    import hashlib
    funcs = []
    abi_counts = collections.Counter()
    for i, f in enumerate(p.functions):
        ret, name, ptypes = parse_sig(f.sig)
        if excluded(f.sig, f.name):
            continue
        path = files.get(f.file_id) if f.file_id >= 0 else None
        r = sorted(reach.get(i, ()))
        threads = sorted({THREAD_OF_ROOT.get(x, 'main') for x in r}) or None
        owner = owner_type(name, ptypes)
        sig_hex, uniq = unique_prefix(i) if f.code_end else (None, False)
        abi = abi_check(f, ret, f.params)
        abi_counts[abi['status']] += 1
        funcs.append({
            'id': id_func(f.sig), 'key': id_func_name(name, ptypes), 'index': i, 'sig': f.sig, 'name': name,
            'owner_type': owner, 'is_method': owner is not None,
            'return': ret or None,
            'params': ([{'name': '$ret', 'type': ret, 'spill_offset': None, 'note': 'hidden return pointer'}] if ret else []) +
                      [{'name': n, 'type': t, 'spill_offset': o} for t, n, o in f.params],
            'arg_locations': abi['arg_locations'], 'abi_check': abi['status'],
            'locals': [{'name': n, 'type': t, 'stack_offset': o} for t, n, o in f.locals],
            'source': {'file': path, 'line': f.line if f.file_id >= 0 else None},
            'native': f.code_end == 0,
            'gcl': {'record_offset': f.offset, 'code_size': f.code_end, 'blob_size': len(f.blob), 'flags': f.flags,
                    'reloc_count': len(f.relocs)},
            'code_hash': code_hashes(f.blob, f.code_end) if f.code_end else None,
            'relocs_sha1': hashlib.sha1('\n'.join('%d %s' % r_ for r_ in f.relocs).encode()).hexdigest(),
            'code_signature': sig_hex, 'code_signature_unique_in_gcl': uniq,
            'vtable_slots': [{'id': id_vslot(t, s), 'type': t, 'slot': s} for t, s in sorted(slot_of.get(f.sig, ()))],
            'calls': sorted({p.functions[j].sig for j in calls.get(i, ()) if not excluded(p.functions[j].sig)}),
            'virtual_calls': sorted({p.functions[j].sig for j in vcalls.get(i, ()) if not excluded(p.functions[j].sig)}),
            'called_by': sorted({p.functions[j].sig for j in called_by.get(i, ()) if not excluded(p.functions[j].sig)}),
            'virtually_called_by': sorted({p.functions[j].sig for j in vcalled_by.get(i, ())
                                           if not excluded(p.functions[j].sig)}),
            'globals_used': sorted({s for k, s in f.relocs if k == 3 and not excluded(s)}),
            'types_checked': sorted({s for k, s in f.relocs if k == 5 and not excluded(s)}),
            'side': side_of(name, path), 'side_evidence': STATIC, 'side_basis': 'namespace prefix and source path',
            'reachable_from': r, 'threads': threads, 'threads_evidence': STATIC if r else UNKNOWN,
            'threads_basis': 'call-graph reachability from engine entry points; virtual calls expand to every '
                             'implementation of the same side (server roots skip client implementations and the '
                             'reverse), so tags are still a superset',
            'evidence': METADATA, 'ref': 'gcl+0x%x' % f.offset,
        })
    log('functions', len(funcs), dict(abi_counts))
    fidx = {f['index']: f for f in funcs}

    # ---------------------------------------------------------------------------------- anchors
    # Each gcl function's code is followed by 8-byte relocation slots, one per relocation, in record
    # order. Global slots hold the global's address; call slots hold a cell address whose content is the
    # callee entry (static: `mov reg,[rip+slot]; call [reg]`). validation/probe.py checks this at runtime.
    unique = {f['index'] for f in funcs if f['code_signature_unique_in_gcl'] and not f['native']}

    def rank(i, off):
        return (0 if p.functions[i].code_end >= SIGLEN else 1, off)

    glob_anchor, func_anchor = {}, {}
    for i in sorted(unique):
        pf = p.functions[i]
        for k, (kind, sym) in enumerate(pf.relocs):
            off = pf.code_end + 8 * k
            if excluded(sym):
                continue
            cand = (rank(i, off), pf.sig, off, k)
            if kind == 3:
                if sym not in glob_anchor or cand < glob_anchor[sym]:
                    glob_anchor[sym] = cand
            elif kind == 4 and sym in by_sig:
                if sym not in func_anchor or cand < func_anchor[sym]:
                    func_anchor[sym] = cand
    sig_of = {f['sig']: f for f in funcs}

    def anchor_rec(a, kind):
        _, asig, off, k = a
        return {'anchor_id': id_func(asig), 'anchor_sig': asig, 'anchor_signature': sig_of[asig]['code_signature'],
                'slot_index': k, 'slot_offset': off, 'slot_kind': kind, 'evidence': STATIC,
                'basis': 'relocation slot order recorded in game.gcl; runtime slot contents checked by validation/probe.py'}

    anchors = {'_doc': 'Locate JIT-loaded globals/functions: find the anchor by its code signature in private '
                       'executable memory, read the 8-byte slot at anchor+slot_offset. global_address slot -> address of '
                       'the global; call_cell slot -> cell -> entry point. chain: repeat slot->cell->entry for each hop. '
                       'via_typeinfo: slot -> typeinfo, [typeinfo+0x80]+8*method_slot -> cell -> entry.',
               'globals': {s: anchor_rec(a, 'global_address') for s, a in sorted(glob_anchor.items())},
               'functions': {}}
    # Multi-hop chains for functions with neither a unique signature nor a unique-signature caller:
    # BFS from every uniquely signed function over call slots (slot -> cell -> entry at each hop).
    import collections as _c
    parent = {}
    dq = _c.deque(sorted(unique))
    seen = set(unique)
    while dq:
        x = dq.popleft()
        px = p.functions[x]
        for k, (kind, sym) in enumerate(px.relocs):
            if kind != 4 or sym not in by_sig:
                continue
            y = by_sig[sym]
            if y in seen or not p.functions[y].code_end or excluded(sym):
                continue
            seen.add(y)
            parent[y] = (x, px.code_end + 8 * k)
            dq.append(y)

    # typeinfo anchors: a uniquely signed function whose typeinfo slot (kind 5) names the type
    ti_anchor = {}
    for i in sorted(unique):
        pf = p.functions[i]
        for k, (kind, sym) in enumerate(pf.relocs):
            if kind == 5 and sym in tb and not excluded(sym):
                cand = (rank(i, pf.code_end + 8 * k), pf.sig, pf.code_end + 8 * k, k)
                if sym not in ti_anchor or cand < ti_anchor[sym]:
                    ti_anchor[sym] = cand
    # Types whose only typeinfo references sit in functions shared byte-for-byte with another type
    # (0.1.24: both edge tools' state ctors). The anchor then matches several functions; the
    # runtime tries each and keeps the one whose method cell points at the target's own unique
    # code, so these routes are emitted only for targets with a unique direct signature.
    shared_ti = {}
    for i, pf in enumerate(p.functions):
        if not pf.code_end or i in unique or pf.sig not in sig_of:
            continue
        for k, (kind, sym) in enumerate(pf.relocs):
            if kind == 5 and sym in tb and sym not in ti_anchor and not excluded(sym):
                cand = (rank(i, pf.code_end + 8 * k), pf.sig, pf.code_end + 8 * k, k)
                if sym not in shared_ti or cand < shared_ti[sym]:
                    shared_ti[sym] = cand
    method_home = {}
    for t in p.types:
        if t.name in ti_anchor or t.name in shared_ti:
            for k, m in enumerate(t.methods):
                method_home.setdefault(m, (t.name, k))

    # any located caller (unique or reachable by chain) with the slot that calls a function: cell routes
    any_caller = {}
    for x in sorted(seen):
        px = p.functions[x]
        for k, (kind, sym) in enumerate(px.relocs):
            if kind == 4 and sym in by_sig:
                y = by_sig[sym]
                if p.functions[y].code_end and y not in any_caller:
                    any_caller[y] = (x, px.code_end + 8 * k)

    def chain_of(i):
        hops = []
        while i in parent:
            i, off = parent[i]
            hops.append(off)
        return i, list(reversed(hops))

    for f in funcs:
        if f['native'] and f['sig'] not in func_anchor:
            continue
        e = {}
        if f['code_signature_unique_in_gcl']:
            e['direct'] = {'signature': f['code_signature'], 'evidence': METADATA,
                           'basis': 'code prefix unique among all gcl code blobs'}
        if f['sig'] in func_anchor:
            e['via_caller'] = anchor_rec(func_anchor[f['sig']], 'call_cell')
        elif not f['native'] and f['index'] in parent:
            root, hops = chain_of(f['index'])
            if len(hops) >= 2:
                e['chain'] = {'root_id': id_func(p.functions[root].sig), 'root_sig': p.functions[root].sig,
                              'root_signature': sig_of[p.functions[root].sig]['code_signature'], 'hops': hops,
                              'evidence': STATIC,
                              'basis': 'follow call slots from a uniquely signed root: at each hop read the 8-byte slot '
                                       'at function+offset (a cell), then the cell (the next entry)'}
        if not f['native'] and 'via_caller' not in e and f['index'] in any_caller:
            x, off = any_caller[f['index']]
            root, hops = chain_of(x)
            e['cell_chain'] = {'root_id': id_func(p.functions[root].sig), 'root_sig': p.functions[root].sig,
                               'root_signature': sig_of[p.functions[root].sig]['code_signature'], 'hops': hops + [off],
                               'evidence': STATIC,
                               'basis': 'route to a call cell of this function (for hooking): follow hops like chain; '
                                        'the last slot read gives the cell itself'}
        if not f['native'] and f['sig'] in method_home:
            tn, k = method_home[f['sig']]
            shared = tn not in ti_anchor
            if not shared or 'direct' in e:
                a_ = shared_ti[tn] if shared else ti_anchor[tn]
                e['via_typeinfo'] = {'type': tn, 'method_slot': k, 'anchor_sig': a_[1],
                                     'anchor_signature': sig_of[a_[1]]['code_signature'], 'slot_offset': a_[2],
                                     'evidence': STATIC,
                                     'basis': 'typeinfo slot -> typeinfo; [typeinfo+0x80] + 8*method_slot -> cell -> entry '
                                              '(dispatch layout runtime-validated)'}
                if shared:
                    e['via_typeinfo']['shared_anchor'] = True
                    e['via_typeinfo']['basis'] += ('; the anchor code is shared with other functions: try each match '
                                                   'and keep the cell whose entry is this function (direct signature)')
        if e:
            anchors['functions'][f['sig']] = e

    # ---------------------------------------------------------------------------------- globals
    gl = []
    gfun = collections.defaultdict(set)
    for f in p.functions:
        for k, s in f.relocs:
            if k == 3:
                gfun[s].add(f.name)
    init_rank = {n: i for i, n in enumerate(p.init_order)}
    for t, n in p.globals:
        if excluded(n, t):
            continue
        users = sorted(x for x in gfun.get(n, ()) if not x.endswith(('.$ctor', '.$dtor')) and not excluded(x))
        a = glob_anchor.get(n)
        gl.append({'id': id_global(n), 'name': n, 'type': t, 'size': tsize(t), 'init_order': init_rank.get(n),
                   'ctor': n + '.$ctor', 'dtor': n + '.$dtor',
                   'used_by_count': len(users), 'used_by_sample': users[:25],
                   'address': {'value': None, 'evidence': UNKNOWN,
                               'basis': 'allocated by the JIT loader at runtime',
                               'locate': anchor_rec(a, 'global_address') if a else None},
                   'evidence': METADATA})

    # ---------------------------------------------------------------------------------- natives
    natives = []
    if args.exe and not args.no_natives:
        log('scanning natives in', args.exe)
        natives, _ = find_natives(args.exe, by_sig)
        log('natives', len(natives), collections.Counter(n['evidence'] for n in natives))

    # ---------------------------------------------------------------------------------- symbol index
    symbols = {}
    for n, t in types.items():
        symbols[t['id']] = {'kind': 'enum' if t['kind'] == 'enum' else 'type', 'name': n, 'h': short_hash(t['id'])}
        for fl in t['fields']:
            if fl['name']:
                symbols[fl['id']] = {'kind': 'field', 'name': fl['name'], 'owner': t['id'], 'offset': fl['offset'],
                                     'type': fl['type'], 'h': short_hash(fl['id'])}
        for ev in t['enum_values']:
            symbols[ev['id']] = {'kind': 'enum_value', 'name': ev['name'], 'owner': t['id'], 'value': ev['value'],
                                 'h': short_hash(ev['id'])}
        for v in t['vtable']:
            if v['introduced_by'] == n:
                symbols[v['id']] = {'kind': 'vslot', 'owner': t['id'], 'slot': v['slot'], 'sig': v['sig'],
                                    'h': short_hash(v['id'])}
    for f in funcs:
        symbols.setdefault(f['id'], {'kind': 'native_decl' if f['native'] else 'function', 'name': f['name'],
                                     'owner': id_type(f['owner_type']) if f['owner_type'] else None,
                                     'side': f['side'], 'h': short_hash(f['id'])})
    for g in gl:
        symbols[g['id']] = {'kind': 'global', 'name': g['name'], 'type': g['type'], 'h': short_hash(g['id'])}
    for nv in natives:
        symbols[nv['id']] = {'kind': 'native', 'name': nv['name'], 'rva': nv.get('thunk_rva'),
                             'gcl_decl': id_func(nv['sig']) if nv['declared_in_gcl'] else None, 'h': short_hash(nv['id'])}

    ident['counts'] = {'types': len(types), 'functions': len(funcs), 'gcl_code_functions': sum(not f['native'] for f in funcs),
                       'globals': len(gl), 'natives': len(natives),
                       'natives_with_rva': sum(1 for n in natives if n.get('thunk_rva')),
                       'functions_unique_signature': len(unique),
                       'functions_locatable': sum(1 for f in funcs if not f['native'] and (
                           f['code_signature_unique_in_gcl'] or f['sig'] in func_anchor)),
                       'functions_locatable_any_route': sum(1 for f in funcs if not f['native'] and f['sig'] in anchors['functions']),
                       'globals_anchored': sum(1 for g in gl if g['address']['locate']),
                       'abi_check': dict(abi_counts), 'symbols': len(symbols)}
    ident['excluded_scope'] = 'Anti-piracy, ban, auth-ticket, support-tracker, crash-report, telemetry and HTTP symbols ' \
                              'are dropped; fields of that kind keep their offset with name/type withheld.'

    log('writing', out)
    dump_json(ident, os.path.join(out, 'build_identity.json'))
    dump_json(types, os.path.join(out, 'types.json'), indent=None)
    dump_json(funcs, os.path.join(out, 'functions.json'), indent=None)
    dump_json(gl, os.path.join(out, 'globals.json'))
    dump_json(natives, os.path.join(out, 'natives.json'))
    dump_json(files, os.path.join(out, 'files.json'))
    dump_json(anchors, os.path.join(out, 'anchors.json'), indent=None)
    dump_json(symbols, os.path.join(out, 'symbols.json'), indent=None)
    log('done in %.0fs' % (time.time() - t0), ident['counts'])


if __name__ == '__main__':
    main()
