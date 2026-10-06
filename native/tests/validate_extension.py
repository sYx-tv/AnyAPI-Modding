"""Source/ledger reconciliation; execution is covered by the C++/DLL fixtures."""
from pathlib import Path
import json,re
root=Path(__file__).resolve().parents[1]
header=(root/'anymaker_mod_extension.h').read_text()
names=re.findall(r'AnyExtResult \(\*(\w+)\)',header)
assert len(names)==7 and len(set(names))==7
implementation=(root/'runtime_extension.inc').read_text()
assert not any(name in implementation for name in ('safe_read_memory(','safe_read_native(','g_get_transform(','LoadLibrary'))
assert 'AnymakerGetExtensionV1' in implementation
assert 'p33_token_accepts' in implementation and 'e.p33_epoch!=epoch' in implementation
assert 'bytes<sizeof(*out)' in implementation and 'ANY_EXT_STALE_TOKEN' in implementation
gate=json.loads((root/'NATIVE_GATE.json').read_text())
assert not gate['native_startup_approved'] and not gate['deployment_approved']
source=(root/'dinput8_proxy.cpp').read_text()
loader=source.split('static DWORD WINAPI loader_thread')[1]
assert loader.index('return 0;\n    log_line')<loader.index('g_ctx={')
assert 'g_p29_world_revision.fetch_add(1)' not in source
assert 'new_lifetime=!e.live || e.p33_epoch!=g_p29_world_revision.load()' in source
assert 'e.identity.live && e.p33_epoch!=g_p29_world_revision.load()' in source
audit=json.loads((root/'COMPATIBILITY_AUDIT.json').read_text())
assert len(audit['hooks'])==165
assert all(row['evidence']['payload_valid']==row['evidence']['outcome_verified']=='NOT_TESTED' for row in audit['hooks'])
native=json.loads((root/'NETWORK_AND_INVENTORY_AUDIT.json').read_text())
assert len(native['pipelines'])==143 and len(native['inventory_candidates'])==46
assert {p['type_id_getter'][0]['static_id'] for p in native['pipelines']}==set(range(1,144))
print('PASS: seven registry-only extension slots, 165 current static rows, 143 pipelines, 46 inventory candidates and enforced native/deployment gates.')
