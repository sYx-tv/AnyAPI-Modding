"""Package the public SDK plus the supplied metadata reference, without game assets."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import zipfile

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--reference', type=Path, required=True)
args = ap.parse_args()
root = Path(__file__).resolve().parents[2]
generated = root / 'build/experimental-all.hpp'
subprocess.run([sys.executable, str(root / 'sdk/experimental/tools/bind.py'),
                '--reference', str(args.reference), '--all', '--out', str(generated)], check=True)
out = root / 'release/AnyAPI-Experimental-SDK.zip'
out.parent.mkdir(exist_ok=True)
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
    for directory in [root / 'sdk', root / 'docs/api']:
        for p in sorted(directory.rglob('*')):
            if p.is_file() and p.suffix in ('.md', '.py', '.h', '.hpp', '.cpp'):
                archive.write(p, p.relative_to(root).as_posix())
    for directory in ['json', 'docs']:
        for p in sorted((args.reference / directory).rglob('*')):
            if p.is_file() and p.suffix in ('.json', '.md'):
                archive.write(p, 'sdk/experimental/reference/' + p.relative_to(args.reference).as_posix())
    archive.write(args.reference / 'README.md', 'sdk/experimental/reference/README.md')
    for p in [generated, generated.with_suffix('.json')]:
        archive.write(p, 'sdk/experimental/generated/' + p.name)
with zipfile.ZipFile(out) as archive:
    assert archive.testzip() is None
print(json.dumps({'file': str(out), 'bytes': out.stat().st_size,
                  'sha256': hashlib.sha256(out.read_bytes()).hexdigest()}))
