"""Compile-tests the generated header and the examples with MSVC (x64).

Usage:
    python tools/validate_compile.py --sdk . [--out validation/compile_result.json]

Finds Visual Studio with vswhere, runs vcvars64 and compiles:
  * a translation unit that includes include/anymaker_sdk_types.hpp (checks every static_assert)
  * every examples/*.cpp as an object file (/c), so examples are type-checked but not linked.
A pass here proves the layouts are self-consistent C++, not that they match the running game; that is
what validation/probe.py checks.
"""
import argparse
import glob
import json
import os
import subprocess
import sys
import tempfile
import time


def find_vcvars():
    vswhere = os.path.join(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)'),
                           'Microsoft Visual Studio', 'Installer', 'vswhere.exe')
    if not os.path.exists(vswhere):
        return None
    out = subprocess.run([vswhere, '-latest', '-products', '*', '-requires',
                          'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'],
                         capture_output=True, text=True).stdout.strip().splitlines()
    for p in out:
        v = os.path.join(p, 'VC', 'Auxiliary', 'Build', 'vcvars64.bat')
        if os.path.exists(v):
            return v
    return None


def compile_one(vcvars, src, inc, workdir):
    obj = os.path.join(workdir, os.path.splitext(os.path.basename(src))[0] + '.obj')
    bat = os.path.join(workdir, 'cc.bat')
    with open(bat, 'w') as fh:
        fh.write('@echo off\ncall "%s" >nul\ncl /nologo /std:c++17 /EHsc /W3 /c /I "%s" /Fo"%s" "%s"\n'
                 % (vcvars, os.path.abspath(inc), obj, os.path.abspath(src)))
    t = time.time()
    r = subprocess.run(['cmd', '/d', '/c', bat], capture_output=True, text=True)
    return {'source': os.path.relpath(src), 'ok': r.returncode == 0, 'seconds': round(time.time() - t, 1),
            'output': (r.stdout + r.stderr)[-4000:]}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--sdk', required=True, help='SDK root (contains include/ and examples/)')
    ap.add_argument('--out', default=None)
    a = ap.parse_args(argv)
    vcvars = find_vcvars()
    if not vcvars:
        print('MSVC not found (install Visual Studio with the C++ x64 tools)', file=sys.stderr)
        return 2
    inc = os.path.join(a.sdk, 'include')
    work = tempfile.mkdtemp(prefix='anymaker_sdk_cc_')
    tu = os.path.join(work, 'header_only.cpp')
    with open(tu, 'w') as fh:
        fh.write('#include "anymaker_sdk_types.hpp"\nint main() { return 0; }\n')
    results = [compile_one(vcvars, tu, inc, work)]
    results[0]['source'] = 'include/anymaker_sdk_types.hpp (header-only TU)'
    for src in sorted(glob.glob(os.path.join(a.sdk, 'examples', '*.cpp'))):
        results.append(compile_one(vcvars, src, inc, work))
    for r in results:
        print('%-60s %s (%.1fs)' % (r['source'], 'OK' if r['ok'] else 'FAIL', r['seconds']))
        if not r['ok']:
            print(r['output'])
    rep = {'compiler': 'MSVC via ' + vcvars, 'when_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
           'evidence_class': 'compile-tested (not runtime-tested)', 'results': results}
    if a.out:
        os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
        with open(a.out, 'w', encoding='utf-8') as fh:
            json.dump(rep, fh, indent=1)
    return 0 if all(r['ok'] for r in results) else 1


if __name__ == '__main__':
    sys.exit(main())
