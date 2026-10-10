"""Refresh the legacy event-route telemetry for a new game build.

The legacy routes (native/legacy_routes.inc) find each server event handler through a call cell of
server.replicator_context.recv_event: `dispatch_owner + dep_offset`, where dep_offset is
code_end + 8 * (relocation index of `server.on_event(..., client_peer.data.event.<route name>)`).
Game updates that add or remove events change that function's size and relocation order, so the
dispatcher bytes (native/legacy_dispatch_owner.h) and every dep_offset must be regenerated together.

    python native/tools/refresh_legacy_routes.py --gcl "<Anymaker>/bin/game.gcl" [--apply]

A route whose event no longer exists gets dep_offset 0, which leaves it uninstalled (it then falls
back to its exact handler body, which no longer matches). The routes only count calls; nothing in
the public API depends on them. Handler bodies are still matched exactly before a detour installs.
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'sdk' / 'experimental' / 'tools'))
import gcl  # noqa: E402

DISPATCHER = '() server.replicator_context.recv_event (server.replicator_context, const ptr<replication.slave.object>, binary.parser)'
ROUTE = re.compile(r'^(\{"(\w+)","\w+",\w+,sizeof\(\w+\),\d+,\d+,\w+,\d+,\d+,)(\d+)(\})', re.M)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--gcl', type=Path, required=True)
    ap.add_argument('--apply', action='store_true')
    a = ap.parse_args()
    prog = gcl.Program(str(a.gcl))
    fn = prog.func_by_sig[DISPATCHER]
    syms = [s for _, s in fn.relocs]
    routes_path = ROOT / 'native' / 'legacy_routes.inc'
    text = routes_path.read_bytes().decode('utf-8')
    changed, dropped = 0, []

    def remap(m):
        nonlocal changed
        name, old = m[2], int(m[3])
        if not old:
            return m[0]
        hits = [i for i, s in enumerate(syms) if 'server.on_event' in s and s.endswith('client_peer.data.event.%s)' % name)]
        new = fn.code_end + 8 * hits[0] if len(hits) == 1 else 0
        if not new:
            dropped.append(name)
        changed += new != old
        return m[1] + str(new) + m[4]

    text = ROUTE.sub(remap, text)
    owner_path = ROOT / 'native' / 'legacy_dispatch_owner.h'
    owner = owner_path.read_bytes().decode('utf-8')
    body = fn.blob[:fn.code_end]
    owner_new = re.sub(r'(P272_DISPATCH_OWNER\[\]\s*=\s*\{)[^}]*(\})',
                       lambda m: m[1] + ','.join('0x%02x' % b for b in body) + m[2], owner)
    print('dispatcher code_end %d, %d relocations; %d dep_offsets changed; no event for: %s'
          % (fn.code_end, len(syms), changed, ', '.join(dropped) or 'none'))
    if a.apply:
        routes_path.write_bytes(text.encode('utf-8'))
        owner_path.write_bytes(owner_new.encode('utf-8'))


if __name__ == '__main__':
    main()
