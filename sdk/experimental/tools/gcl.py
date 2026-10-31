"""Reader for Anymaker's compiled geocode program (bin/game.gcl).

The file is a cache of asmjit-generated x64 machine code plus metadata. Two layouts exist
(all integers little-endian u32, strings are u32 length + UTF-8 bytes, hashes are u64):

  format 1 (up to 0.1.28), full debug metadata:
    files    : u32 n, n x (u32 file_id, str path)
    globals  : u32 n, n x (str type, str name)
    init     : u32 0, u32 n, n x str global_name      (global construction order)
    funcs    : u32 n, n x Function
    types    : u32 0, u32 n, n x Type

  format 2 (0.2.0 on), stripped: no files table and no types marker. A function record keeps
  its code but replaces the signature with the FNV-1a 64 hash of the format-1 signature text
  (natives keep the text too) and drops the source line, file, names, parameters, locals and
  line table. Function relocations and type methods/ctor/cctor/copy/dtor name functions by
  that hash. Type records lose their fields; globals, type names, sizes and enum values remain.

Format-2 signatures are recovered by hashing known signatures: pass `names` (or set
ANYMAKER_GCL_NAMES) to a previous SDK json/ folder or a format-1 game.gcl. Recovered
functions also get that source's file, line, parameters and locals; the rest are named
`() #<hash> ()`. A type whose size, alignment and parent match the source gets the source's
fields (Type.fields_source 'carried'). ANYMAKER_GCL_INFERRED (or inferred=) names an
infer_stripped.py result: signatures recovered from changed ones, and the fields of types whose
layout changed ('inferred'). Both are inferences, not records of this build.

Field meanings are documented in docs/reference/gcl_format.md.
"""
import json
import os
import struct
import sys
from dataclasses import dataclass, field

REL_KINDS = {1: 'self', 3: 'global', 4: 'func', 5: 'typeinfo', 6: 'virtual'}


@dataclass
class Function:
    offset: int
    sig: str
    flags: int
    line: int
    relocs: list          # [(kind, symbol)] ; slot i lives at code_end + 8*i
    code_end: int         # code + inline constants occupy blob[:code_end]
    blob: bytes
    unwind: bytes
    file_id: int          # -1 for compiler-generated global ctor/dtor
    name: str
    ret: str
    params: list          # [(type, name, rsp_offset)]
    locals: list          # [(type, name, rsp_offset)]
    lines: list           # [(code_offset, reserved, line_delta, [live local indices])]
    hash: int = 0         # format 2: FNV-1a 64 of the format-1 sig (the only name most records keep)

    @property
    def abs_lines(self):
        return [(o, self.line + d if d != -self.line else None, live) for o, _, d, live in self.lines]


@dataclass
class Type:
    offset: int
    kind: int             # 13 = struct/class, 14 = enum
    name: str
    parent: str
    size: int
    align: int
    flags: int
    enum: list
    methods: list         # virtual table, in slot order
    ctor: str
    cctor: str
    copy: str
    dtor: str
    reserved: int
    fields: list          # [(type, name, offset)]
    fields_source: str = ''        # format 2: 'carried' (same size, align, parent as the names source) or 'inferred'


class Reader:
    def __init__(self, data, off=0):
        self.d = data
        self.o = off

    def u32(self):
        v = struct.unpack_from('<I', self.d, self.o)[0]
        self.o += 4
        return v

    def u64(self):
        v = struct.unpack_from('<Q', self.d, self.o)[0]
        self.o += 8
        return v

    def i32(self):
        v = struct.unpack_from('<i', self.d, self.o)[0]
        self.o += 4
        return v

    def s(self):
        n = self.u32()
        v = self.d[self.o:self.o + n].decode('utf-8', 'replace')
        self.o += n
        return v

    def b(self, n):
        v = self.d[self.o:self.o + n]
        self.o += n
        return v

    def var(self):
        return (self.s(), self.s(), self.u32())


FNV_OFFSET = 0xcbf29ce484222325
FNV_PRIME = 0x100000001b3


def fnv1a64(text):
    h = FNV_OFFSET
    for c in text.encode('utf-8'):
        h = ((h ^ c) * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
    return h


def unknown_sig(h):
    return '() #%016x ()' % h


def split_sig(sig):
    """'(ret) name (a, b<c, d>)' -> (ret, name, [param types])."""
    ret_end = sig.index(') ')
    ret = sig[1:ret_end]
    rest = sig[ret_end + 2:]
    name, _, args = rest.partition(' (')
    args = args[:-1] if args.endswith(')') else args
    params, depth, cur = [], 0, ''
    for c in args:
        if c == '<':
            depth += 1
        elif c == '>':
            depth -= 1
        if c == ',' and depth == 0:
            params.append(cur.strip())
            cur = ''
        else:
            cur += c
    if cur.strip():
        params.append(cur.strip())
    return ret, name, params


class Names:
    """Signature text and debug metadata for format-2 hashes."""

    def __init__(self):
        self.sig = {}     # hash -> signature
        self.meta = {}    # signature -> (file, line, params, locals)
        self.layout = {}  # type name -> (size, align, parent, [(type, name, offset)])
        self._seen = set()

    def add(self, sig, meta=None):
        if not sig or sig.startswith('() #'):
            return
        if sig not in self._seen:
            self._seen.add(sig)
            self.sig.setdefault(fnv1a64(sig), sig)
        if meta is not None:
            self.meta.setdefault(sig, meta)

    @classmethod
    def load(cls, source):
        n = cls()
        if os.path.isdir(source):
            n._from_json(source)
        else:
            n._from_gcl(source)
        return n

    def _from_json(self, folder):
        with open(os.path.join(folder, 'functions.json'), encoding='utf-8') as fh:
            funcs = json.load(fh)
        for f in funcs:
            src = f.get('source') or {}
            params = [(p['type'], p['name'], p['spill_offset']) for p in f.get('params') or [] if p['name'] != '$ret']
            locs = [(v['type'], v['name'], v['stack_offset']) for v in f.get('locals') or []]
            self.add(f['sig'], (src.get('file'), src.get('line'), params, locs))
            for k in ('calls', 'virtual_calls', 'called_by', 'virtually_called_by'):
                for s in f.get(k) or ():
                    self.add(s)
        tpath = os.path.join(folder, 'types.json')
        if os.path.exists(tpath):
            with open(tpath, encoding='utf-8') as fh:
                types = json.load(fh)
            for t in (types.values() if isinstance(types, dict) else types):
                for v in t.get('vtable') or ():
                    self.add(v.get('sig'))
                for k in ('ctor', 'copy_ctor', 'copy', 'dtor'):
                    self.add(t.get(k))
                self.layout[t['name']] = (t.get('size'), t.get('align'), t.get('parent') or '',
                                          [(f['type'], f['name'], f['offset']) for f in t.get('fields') or ()
                                           if f.get('name') and f.get('type')])

    def _from_gcl(self, path):
        p = Program(path)
        if p.format != 1:
            raise ValueError('%s is a stripped game.gcl; use the SDK json/ folder made from it' % path)
        for f in p.functions:
            self.add(f.sig, (p.files.get(f.file_id) if f.file_id >= 0 else None, f.line, f.params, f.locals))
            for _, s in f.relocs:
                self.add(s)
        for t in p.types:
            for s in t.methods + [t.ctor, t.cctor, t.copy, t.dtor]:
                self.add(s)
            self.layout[t.name] = (t.size, t.align, t.parent, list(t.fields))


_NAMES_CACHE = {}


def load_names(source):
    if source not in _NAMES_CACHE:
        _NAMES_CACHE[source] = Names.load(source)
    return _NAMES_CACHE[source]


def detect_format(d):
    """1 when the file opens with the files table (u32 count, u32 id, str path), else 2."""
    if len(d) < 16:
        return 1
    n, first = struct.unpack_from('<II', d, 0)
    if n == 0:
        return 1
    plen = struct.unpack_from('<I', d, 8)[0]
    return 1 if 12 + plen <= len(d) and b'/' in d[12:12 + plen] else 2


class Program:
    def __init__(self, path, names=None, inferred=None):
        with open(path, 'rb') as fh:
            d = fh.read()
        r = Reader(d)
        self.format = detect_format(d)
        self.files = {}
        self.names = None
        self.inferred = {}
        inferred = inferred if inferred is not None else os.environ.get('ANYMAKER_GCL_INFERRED')
        if self.format == 2 and inferred:
            if isinstance(inferred, dict):
                self.inferred = inferred
            else:
                with open(inferred, encoding='utf-8') as fh:
                    self.inferred = json.load(fh)
        if self.format == 1:
            for _ in range(r.u32()):
                i = r.u32()
                self.files[i] = r.s()
        else:
            source = names if names is not None else os.environ.get('ANYMAKER_GCL_NAMES')
            if isinstance(source, Names):
                self.names = source
            elif source:
                self.names = load_names(source)
            else:
                print('gcl: %s is stripped and no names source was given; functions keep only their hash '
                      '(pass names= or set ANYMAKER_GCL_NAMES)' % path, file=sys.stderr)
                self.names = Names()
            extra = {int(h, 16): sig for h, sig in self.inferred.get('names', {}).items()}
            if extra:
                merged = Names()
                merged.sig, merged.meta, merged.layout = dict(self.names.sig), self.names.meta, self.names.layout
                merged.sig.update(extra)
                self.names = merged
        self.globals = [(r.s(), r.s()) for _ in range(r.u32())]
        self.init_marker = r.u32()
        self.init_order = [r.s() for _ in range(r.u32())]
        if self.format == 1:
            self.functions = [self._func(r) for _ in range(r.u32())]
            self.types_marker = r.u32()
            self.types = [self._type(r) for _ in range(r.u32())]
        else:
            file_ids = {}
            self.functions = [self._func2(r, file_ids) for _ in range(r.u32())]
            self.files = {i: f for f, i in file_ids.items()}
            self.types_marker = 0
            self.types = [self._type2(r) for _ in range(r.u32())]
        assert r.o == len(d), (r.o, len(d))
        self.type_by_name = {t.name: t for t in self.types}
        self.func_by_sig = {}
        for f in self.functions:
            self.func_by_sig.setdefault(f.sig, f)

    def resolve(self, h):
        if not h:
            return ''
        return self.names.sig.get(h) or unknown_sig(h)

    @property
    def unresolved(self):
        return [f for f in self.functions if f.sig.startswith('() #')]

    @staticmethod
    def _func(r):
        off = r.o
        sig = r.s()
        flags = r.u32()
        line = r.u32()
        rel = [(r.u32(), r.s()) for _ in range(r.u32())]
        code_end = r.u32()
        blob = r.b(r.u32())
        zero = r.u32()
        assert zero == 0
        unwind = r.b(r.u32())
        file_id = r.i32()
        name = r.s()
        ret = r.s()
        params = [r.var() for _ in range(r.u32())]
        locs = [r.var() for _ in range(r.u32())]
        lines = []
        for _ in range(r.u32()):
            o = r.u32(); z = r.u32(); dl = r.i32()
            lines.append((o, z, dl, [r.u32() for _ in range(r.u32())]))
        return Function(off, sig, flags, line, rel, code_end, blob, unwind, file_id, name, ret, params, locs, lines)

    def _func2(self, r, file_ids):
        off = r.o
        h = r.u64()
        sig = r.s() or self.resolve(h)
        flags = r.u32()
        rel = []
        for _ in range(r.u32()):
            kind = r.u32(); sym = r.s(); target = r.u64()
            rel.append((kind, sym or self.resolve(target)))
        code_end = r.u32()
        blob = r.b(r.u32())
        zero = r.u32()
        assert zero == 0
        unwind = r.b(r.u32())
        ret, name, ptypes = split_sig(sig)
        meta = self.names.meta.get(sig)
        if meta:
            path, line, params, locs = meta
            file_id = file_ids.setdefault(path, len(file_ids)) if path else -1
            line = line or 0
        else:
            file_id, line, locs = -1, 0, []
            params = [(t, 'p%d' % i, None) for i, t in enumerate(ptypes)]
        return Function(off, sig, flags, line, rel, code_end, blob, unwind, file_id, name, ret, list(params),
                        list(locs), [], h)

    @staticmethod
    def _type(r):
        off = r.o
        kind = r.u32(); name = r.s(); parent = r.s()
        size = r.u32(); align = r.u32(); flags = r.u32()
        enum = [r.s() for _ in range(r.u32())]
        methods = [r.s() for _ in range(r.u32())]
        ctor = r.s(); cctor = r.s(); copy = r.s(); dtor = r.s()
        reserved = r.u32()
        fields = [r.var() for _ in range(r.u32())]
        return Type(off, kind, name, parent, size, align, flags, enum, methods, ctor, cctor, copy, dtor, reserved, fields)

    def _type2(self, r):
        off = r.o
        kind = r.u32(); name = r.s(); parent = r.s()
        size = r.u32(); align = r.u32(); flags = r.u32()
        enum = [r.s() for _ in range(r.u32())]
        methods = [self.resolve(r.u64()) for _ in range(r.u32())]
        ctor, cctor, copy, dtor = (self.resolve(r.u64()) for _ in range(4))
        reserved = r.u32()
        fields = [r.var() for _ in range(r.u32())]
        source = ''
        old = self.names.layout.get(name)
        inferred = self.inferred.get('types', {}).get(name)
        if not fields and old and old[:3] == (size, align, parent) and old[3]:
            fields, source = list(old[3]), 'carried'
        elif not fields and inferred and inferred.get('size') == size and inferred.get('fields'):
            fields, source = [tuple(f) for f in inferred['fields']], 'inferred'
        return Type(off, kind, name, parent, size, align, flags, enum, methods, ctor, cctor, copy, dtor, reserved, fields,
                    source)


def main(argv=None):
    import argparse
    ap = argparse.ArgumentParser(description='Print record counts of a game.gcl file.')
    ap.add_argument('gcl', help='path to bin/game.gcl')
    ap.add_argument('--names', help='format 2: previous SDK json/ folder or format-1 game.gcl to recover signatures from')
    a = ap.parse_args(argv)
    p = Program(a.gcl, a.names)
    print('format', p.format, len(p.files), 'files', len(p.globals), 'globals', len(p.functions), 'functions',
          len(p.types), 'types', len(p.unresolved) if p.format == 2 else 0, 'unresolved')


if __name__ == '__main__':
    main()
