"""Name the functions in AnyPerformance's profiler lines.

The profiler logs game logic (game.gcl) functions by code size and their first 32 bytes, because the game places them in
memory at run time. This tool finds each one in game.gcl and prints its signature and source file.

    python native/tools/map_profile.py anymaker_modding.log path/to/bin/game.gcl [--db sdk_index.sqlite] [--out profile.md]

--db (the SDK reference's json/sdk_index.sqlite) also names game.exe natives by RVA. Prints the last report per thread,
or every report with --all.
"""
import argparse
import re
import sqlite3
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gcl_metadata import Program  # noqa: E402

LINE = re.compile(r"(?:Profile )?(\w+) (self|total) #(\d+): self ([\d.]+)% total ([\d.]+)% \| (.*)$")
HEAD = re.compile(r"Profile (\w+) thread: (\d+) samples in (\d+) s")


def gcl_index(program):
    by_prefix = {}
    for f in program.functions:
        by_prefix.setdefault(bytes(f.blob[:32]), []).append(f)
    return by_prefix


def name_gcl(what, program, by_prefix, blobs):
    m = re.match(r"gcl (?:size (\d+) )?(block )?code((?: [0-9a-f]{2})+)", what)
    if not m:
        return None
    size = int(m.group(1)) if m.group(1) else None
    code = bytes(int(x, 16) for x in m.group(3).split())
    if not m.group(2) and size is not None:
        cands = by_prefix.get(code, [])
        exact = [f for f in cands if f.code_end == size] or cands
        if exact:
            f = exact[0]
            more = f" (+{len(exact) - 1} with the same start)" if len(exact) > 1 else ""
            return f"{f.sig}  [{program.files.get(f.file_id, '?')}:{f.line}]{more}"
    # A 64-byte block inside a function: search every body.
    hits = [f for f, blob in blobs if code in blob]
    if hits:
        f = hits[0]
        more = f" (+{len(hits) - 1} more)" if len(hits) > 1 else ""
        return f"inside {f.sig}  [{program.files.get(f.file_id, '?')}:{f.line}]{more}"
    return "not found in this game.gcl (other game build?)"


def name_exe(what, natives):
    m = re.match(r"game\.exe\+0x([0-9a-f]+)", what, re.I)
    if not m or not natives:
        return None
    rva = int(m.group(1), 16)
    best = None
    for start, sig in natives:
        if start <= rva:
            best = (start, sig)
        else:
            break
    if best and rva - best[0] < 0x4000:
        return f"native {best[1]} (+0x{rva - best[0]:x})"
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("gcl")
    ap.add_argument("--db")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--out")
    a = ap.parse_args()
    program = Program(a.gcl)
    by_prefix = gcl_index(program)
    blobs = [(f, bytes(f.blob[:f.code_end])) for f in program.functions]
    natives = []
    if a.db:
        db = sqlite3.connect(a.db)
        natives = sorted((int(r[1], 16) if isinstance(r[1], str) else r[1], r[0])
                         for r in db.execute("select sig, rva from natives where rva is not null and rva != ''"))
    reports = []   # (head, [lines])
    for raw in open(a.log, encoding="utf-8", errors="replace"):
        h = HEAD.search(raw)
        if h:
            reports.append((h.groups(), []))
            continue
        m = LINE.search(raw)
        if m and reports:
            reports[-1][1].append(m.groups())
    if not a.all:
        last = {}
        for r in reports:
            last[r[0][0]] = r
        reports = list(last.values())
    out = []
    for (thread, samples, seconds), lines in reports:
        out.append(f"\n## {thread} thread: {samples} samples in {seconds} s\n")
        for _, kind, rank, self_pct, total_pct, what in lines:
            name = name_gcl(what, program, by_prefix, blobs) or name_exe(what, natives) or what
            out.append(f"{kind:5} #{rank:>2}  self {self_pct:>5}%  total {total_pct:>5}%  {name}")
    text = "\n".join(out) + "\n"
    if a.out:
        Path(a.out).write_text(text, encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()
