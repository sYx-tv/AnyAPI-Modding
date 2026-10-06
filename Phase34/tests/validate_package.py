from pathlib import Path
import re,json,sys,struct
root=Path(__file__).resolve().parents[1]
s=(root/'dinput8_proxy.cpp').read_text();inc=(root/'phase27_runtime_audit.inc').read_text();obs=(root/'phase27_inventory_observer.inc').read_text()
assert 'std::filesystem::directory_iterator(mod_dir' not in s
assert 'LoadLibraryW(ent.path()' not in s
assert 'if(!p27_build_matches()) return 0;' in s
assert 'original_slot,trampoline' in (root/'phase27_detours.inc').read_text()
assert 'RtlAddFunctionTable' in (root/'phase27_detours.inc').read_text()
assert obs.count('ANY_INVENTORY_UNAVAILABLE')==3 and 'p26_replace_dependency' not in obs
hook_contracts=json.loads((root/'HOOK_AUDIT.json').read_text());assert len(hook_contracts)==165
specs={label:int(overwrite) for label,overwrite in re.findall(r'\{"([^"]+)",BODY_\w+,(\d+),',(root/'phase27_hook_specs.h').read_text())}
for c in hook_contracts:
 assert specs[c['label']]==c['overwrite']
 assert c['label'] in inc+(root/'phase272_routes.inc').read_text()
 uw=bytes.fromhex(c['unwind_hex']);assert uw[0]==1 and uw[1]<=c['overwrite'] and uw[3]==0
# Each concrete hook forwards through its own published slot, not a caller-local temporary.
for m in re.finditer(r'install_detour\(\s*"[^\"]+"[\s\S]*?\(uintptr_t\)&(\w+)\s*,\s*([^\n]+)',s+inc+(root/'phase272_runtime.inc').read_text()):
 assert '(void**)&g_' in m.group(2),m.group(0)
assert len(re.findall('InterlockedIncrement64\\(&g_p27_hook_calls\\[',s+obs+(root/'phase27_actor_teardown.inc').read_text()+(root/'phase272_routes.inc').read_text()))==165
# ABI version did not change merely to imply that quarantined APIs work.
assert '#define ANYMAKER_MOD_API_VERSION 16u' in (root/'anymaker_mod_api.h').read_text()
if len(sys.argv)>1:
 b=Path(sys.argv[1]).read_bytes();records=json.loads(Path(sys.argv[2]).read_text())
 pats={name:bytes(int(x,16) for x in re.findall(r'0x([0-9A-Fa-f]{2})',txt)) for name,txt in re.findall(r'static const unsigned char (BODY_\w+)\[\]=\{(.*?)\};',(root/'phase27_signatures.h').read_text(),re.S)}
 for entry in json.loads((root/'SIGNATURE_AUDIT.json').read_text()):
  r=records[entry['index']];pat=pats[entry['symbol']];assert len(pat)<=r['code_size']
  assert b[r['body_offset']:r['body_offset']+len(pat)]==pat
  assert r['signature']==entry['signature']
 for hook in hook_contracts:
  entry=next(e for e in json.loads((root/'SIGNATURE_AUDIT.json').read_text()) if e['symbol']==hook['symbol']);r=records[entry['index']]
  p=r['body_offset']+r['blob_size'];n=struct.unpack_from('<I',b,p+4)[0]
  assert b[p+8:p+8+n].hex()==hook['unwind_hex']
# Every new observer has an exact named incoming dependency (or the unique network load body).
rows=json.loads((root/'EVENT_ROUTE_AUDIT.json').read_text());assert len(rows)==141
assert len({row['slot'] for row in rows})==141
for row in rows:
 assert specs[row['label']]==row['overwrite']
assert 'r.dep_offset && !g_p272_dispatch_owner' in (root/'phase272_runtime.inc').read_text()
if len(sys.argv)>1:
 for proof in json.loads((root/'COMPONENT_METADATA_AUDIT.json').read_text()):
  record=records[proof['index']];address=record['body_offset']+proof['offset'];pattern=bytes.fromhex(proof['bytes']);assert b[address:address+len(pattern)]==pattern
 owner=records[36904]
 for row in rows[:139]:
  i=(row['dependency_offset']-owner['code_size'])//8
  assert owner['dependencies'][i]==[4,row['signature']]
 assert set(re.findall(r'Anymaker\w+Fn (\w+);',(root/'anymaker_mod_api.h').read_text().split('struct AnymakerModContextV16 {')[1].split('};')[0]))==set(x['slot'] for x in json.loads((root/'API_COVERAGE.json').read_text()))
print('PASS: package guard, mod isolation, 165 hook publications/counters, quarantine and static contracts')
