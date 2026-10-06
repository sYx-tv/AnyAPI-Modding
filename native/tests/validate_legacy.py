"""Reconcile the shipped API/hooks; optionally verify native evidence against GCL."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--gcl', type=Path)
parser.add_argument('--records', type=Path)
args = parser.parse_args()
header = (root/'anymaker_mod_api.h').read_text()
context = header.split('struct AnymakerModContextV16 {')[1].split('};')[0]
slots = re.findall(r'Anymaker\w+Fn (\w+);', context)
coverage = json.loads((root/'API_COVERAGE.json').read_text())
contracts = json.loads((root/'PHASE33_CONTRACTS.json').read_text())
assert len(slots) == 34 and len(set(slots)) == 34
assert set(slots) == {row['slot'] for row in coverage} == {row['slot'] for row in contracts['api']}
hooks = json.loads((root/'HOOK_AUDIT.json').read_text())
assert len(hooks) == 165 and len({row['label'] for row in hooks}) == 165
assert {row['label'] for row in hooks} == {row['label'] for row in contracts['hooks']}
hook_source = '\n'.join((root/name).read_text() for name in
    ['dinput8_proxy.cpp','legacy_inventory_observer.inc','legacy_actor_teardown.inc','legacy_routes.inc'])
notes = re.findall(r'p29_note_hook\((\d+)\)',hook_source)
assert len(notes) == 165 and sorted(map(int, notes)) == list(range(165))
queries = (root/'legacy_actor_queries.inc').read_text()
assert not any(term in queries for term in ['safe_read_memory(', 'safe_read_native(', 'g_get_transform(', 'capture_client_actor_state_raw('])
assert 'ReleaseSRWLockShared(&g_client_actor_registry_lock)' in queries
assert '#include "legacy_actor_queries.inc"' in hook_source
api_names=re.findall(r'"(\w+)"',(root/'legacy_api_audit_state.h').read_text().split('P29_API_NAMES[]=')[1])
assert api_names==slots
api_audit=(root/'legacy_api_audit.inc').read_text()
assert 'p29_api_smoke();' in hook_source and 'p29_api_report();' in hook_source
assert 'p29_reset_retired_component(e,component,vehicle,p29_next_generation())' in hook_source
for slot in slots:
    assert 'g_ctx.'+slot in api_audit or slot in {'copy_item_definition_id','copy_item_definition_name','copy_item_definition_description','copy_item_definition_class','copy_item_definition_mesh_file'}

assert 'if(revision!=g_p29_world_revision.load()) return;' in hook_source
assert 'p29_world_revision.load()' in hook_source
assert 'external_mod_loading=DISABLED' in hook_source
assert 'std::filesystem::directory_iterator(mod_dir' not in hook_source
assert 'LoadLibraryW(ent.path()' not in hook_source
assert 'return false; // Detection-only Controls observer' in hook_source
for function in ['ui_draw_rect', 'ui_draw_texture', 'resolve_ui_texture']:
    body = re.search(r'static bool '+function+r'\([^;]*?\)\s*\{([\s\S]*?)\n\}',hook_source).group(1)
    assert 'return false;' in body.split('__try')[0]
subprocess.run([sys.executable,str(root/'tests/validate_package.py')] +
    ([str(args.gcl),str(args.records)] if args.gcl and args.records else []),check=True)
if bool(args.gcl) != bool(args.records):
    raise SystemExit('--gcl and --records must be supplied together')
if args.gcl:
    from capstone import Cs, CS_ARCH_X86, CS_MODE_64
    blob=args.gcl.read_bytes()
    assert hashlib.sha256(blob).hexdigest()==json.loads((root/'BUILD_IDENTITY.json').read_text())['game_gcl_sha256']
    records=json.loads(args.records.read_text())
    decoder=Cs(CS_ARCH_X86,CS_MODE_64)
    for hook in hooks:
        record=next(r for r in records if r['signature']==hook['signature'])
        region=blob[record['body_offset']:record['body_offset']+hook['overwrite']]
        instructions=list(decoder.disasm(region,0))
        assert sum(i.size for i in instructions)==hook['overwrite'],hook['label']
        assert not any('rip' in i.op_str or i.mnemonic.startswith(('j','call','ret','loop')) for i in instructions),hook['label']
    evidence=json.loads((root/'NATIVE_EVIDENCE.json').read_text())
    for entry in evidence['functions']:
        record=entry['record'];actual=records[record['index']]
        assert actual==record
        body=blob[record['body_offset']:record['body_offset']+record['code_size']]
        assert body.hex()==entry['bytes'] and hashlib.sha256(body).hexdigest()==entry['body_sha256']
    patterns=(root/'legacy_transform_patterns.h').read_text()
    for index,name in [(14072,'CLIENT_COMPONENT_WORLD'),(14073,'CLIENT_COMPONENT_GRID'),(48768,'SERVER_COMPONENT_WORLD'),(48769,'SERVER_COMPONENT_GRID')]:
        record=records[index]
        body=blob[record['body_offset']:record['body_offset']+record['code_size']]
        pattern=re.search(r'P29_'+name+r'\[\]=\{([^}]+)\}',patterns).group(1)
        assert bytes(int(value,16) for value in pattern.split(','))==body,name
        assert blob.count(body)==1, name+' full-body pattern is ambiguous'
    print(f'PASS: 165 native instruction boundaries/unwind contracts and {len(evidence["functions"])} pinned native records')
    print('PASS: four component transform full-body patterns match unique pinned GCL bodies')
print('PASS: 34 API slots, 165 hook/thread entries, copied actor queries and retained quarantines reconciled')
