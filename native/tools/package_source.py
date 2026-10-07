"""Package repository source, public headers and docs without game assets."""
import argparse, hashlib, zipfile
from pathlib import Path
root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser();parser.add_argument('destination',type=Path);args=parser.parse_args()
args.destination.parent.mkdir(parents=True,exist_ok=True)
files=[p for name in ('native','sdk','docs','manager') for p in (root/name).rglob('*') if p.is_file() and p.suffix.lower() in {'.h','.inc','.cpp','.cs','.md','.json','.py','.ps1','.txt','.manifest','.ico','.hlsl','.rc','.in','.def'} and not set(p.relative_to(root).parts)&{'output','ready-to-upload','__pycache__','publishing','developer'}]
files.extend(root/name for name in ('README.md','SOURCE_BUILD.md','catalog.json','.gitignore'))
with zipfile.ZipFile(args.destination,'w',zipfile.ZIP_DEFLATED) as z:
    for p in sorted(files): z.write(p,p.relative_to(root).as_posix())
print('Source archive:',args.destination)
print('SHA-256:',hashlib.sha256(args.destination.read_bytes()).hexdigest())
