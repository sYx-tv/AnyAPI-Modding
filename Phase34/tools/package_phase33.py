"""Create a self-contained source/test ZIP with an explicit Phase33 manifest."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('destination',type=Path)
args=parser.parse_args()
excluded={'build','build-linux','test_logs','__pycache__','.git'}
files=sorted(p for p in root.rglob('*') if p.is_file() and not set(p.relative_to(root).parts)&excluded and p.suffix not in {'.pyc','.zip'})
files=[p for p in files if p.name!='PACKAGE_SHA256.json']
manifest={p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
manifest_path=root/'PACKAGE_SHA256.json'
manifest_path.write_text(json.dumps(manifest,indent=2)+'\n')
files.append(manifest_path)
args.destination.parent.mkdir(parents=True,exist_ok=True)
with zipfile.ZipFile(args.destination,'w',zipfile.ZIP_DEFLATED) as archive:
    for path in files: archive.write(path,Path('Phase33')/path.relative_to(root))
print(f'Packaged {len(files)} files: {args.destination} ({args.destination.stat().st_size} bytes)')
print('ZIP SHA256:',hashlib.sha256(args.destination.read_bytes()).hexdigest())
