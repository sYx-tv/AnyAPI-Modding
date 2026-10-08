"""Reader for Anymaker's compiled geocode program (bin/game.gcl).

The file is a cache of asmjit-generated x64 machine code plus full debug metadata.
Layout (all integers little-endian u32, strings are u32 length + UTF-8 bytes):

  files    : u32 n, n x (u32 file_id, str path)
  globals  : u32 n, n x (str type, str name)
  init     : u32 0, u32 n, n x str global_name      (global construction order)
  funcs    : u32 n, n x Function
  types    : u32 0, u32 n, n x Type

Field meanings are documented in docs/reference/gcl_format.md.
"""
import struct
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


class Reader:
    def __init__(self, data, off=0):
        self.d = data
        self.o = off

    def u32(self):
        v = struct.unpack_from('<I', self.d, self.o)[0]
        self.o += 4
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


class Program:
    def __init__(self, path):
        with open(path, 'rb') as fh:
            d = fh.read()
        r = Reader(d)
        self.files = {}
        for _ in range(r.u32()):
            i = r.u32()
            self.files[i] = r.s()
        self.globals = [(r.s(), r.s()) for _ in range(r.u32())]
        self.init_marker = r.u32()
        self.init_order = [r.s() for _ in range(r.u32())]
        self.functions = [self._func(r) for _ in range(r.u32())]
        self.types_marker = r.u32()
        self.types = [self._type(r) for _ in range(r.u32())]
        assert r.o == len(d), (r.o, len(d))
        self.type_by_name = {t.name: t for t in self.types}
        self.func_by_sig = {}
        for f in self.functions:
            self.func_by_sig.setdefault(f.sig, f)

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


def main(argv=None):
    import argparse
    ap = argparse.ArgumentParser(description='Print record counts of a game.gcl file.')
    ap.add_argument('gcl', help='path to bin/game.gcl')
    a = ap.parse_args(argv)
    p = Program(a.gcl)
    print(len(p.files), 'files', len(p.globals), 'globals', len(p.functions), 'functions', len(p.types), 'types')


if __name__ == '__main__':
    main()
