"""Regenerate the experimental-SDK bindings of every mod (native/*_bindings.h) for a new game build.

Each `inline constexpr func_desc NAME{...};` and `global_desc NAME{...};` keeps its name and signature;
only the located-by data (code signature, call-cell route, typeinfo slot) is replaced with what
sdk/experimental/tools/bind.py generates from the new build's SDK json. Signatures that no longer
exist, or lost every locator, are reported and left unchanged so the mod fails closed at runtime.

    python native/tools/rebind_mod_bindings.py --reference <SDK root holding json/> --build "0.1.24 / Steam build 25826614" [--apply]
"""
import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'sdk' / 'experimental' / 'tools'))
from bind import generate  # noqa: E402

FUNC = re.compile(r'(inline constexpr func_desc (\w+))\{("(?:[^"\\]|\\.)*")(.*?)\};')
GLOB = re.compile(r'(inline constexpr global_desc (\w+))\{("(?:[^"\\]|\\.)*")(.*?)\};')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--reference', type=Path, required=True)
    ap.add_argument('--build', required=True, help='text for the header comment, e.g. "0.1.24 / Steam build 25826614"')
    ap.add_argument('--apply', action='store_true')
    a = ap.parse_args()
    anchors = json.loads((a.reference / 'json' / 'anchors.json').read_text(encoding='utf-8'))['globals']
    q = lambda v: json.dumps(v or '', ensure_ascii=True)
    problems = 0
    for path in sorted((ROOT / 'native').glob('*_bindings.h')):
        text = path.read_bytes().decode('utf-8')
        changed = 0

        def func(m):
            nonlocal changed, problems
            sig = json.loads(m[3])
            try:
                header, records = generate(a.reference, [], [sig])
            except ValueError:
                print('  %s: %s no longer exists in this build' % (path.name, m[2])); problems += 1
                return m[0]
            if not records[0]['has_locator']:
                print('  %s: %s has no locator route in this build' % (path.name, m[2])); problems += 1
            body = re.search(r'inline constexpr func_desc fn_\w+\{(.*)\};', header)[1]
            new = m[1] + '{' + body + '};'
            changed += new != m[0]
            return new

        def glob(m):
            nonlocal changed, problems
            name = json.loads(m[3])
            g = anchors.get(name)
            if not g:
                print('  %s: global %s has no anchor in this build' % (path.name, name)); problems += 1
                return m[0]
            new = m[1] + '{' + ','.join([q(name), q(g['anchor_signature']), str(g['slot_offset']) + 'u']) + '};'
            changed += new != m[0]
            return new

        text = FUNC.sub(func, text)
        text = GLOB.sub(glob, text)
        text = re.sub(r'generated for Anymaker [^\r\n]*? with', 'generated for Anymaker %s with' % a.build, text, count=1)
        print('%s: %d descriptors changed' % (path.name, changed))
        if a.apply:
            path.write_bytes(text.encode('utf-8'))
    sys.exit(1 if problems else 0)


if __name__ == '__main__':
    main()
