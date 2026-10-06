"""Phase33 additions: exhaustive reconciliation and truthful diagnostics."""
import json,re,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
contracts=json.loads((root/'PHASE33_CONTRACTS.json').read_text())
rows=json.loads((root/'HOOK_SCENARIOS.json').read_text())
assert contracts['phase']==33
assert 'phase=33;' in (root/'review_phase33.ps1').read_text()
assert (root/'RUN_PHASE33.cmd').exists()
assert len(rows)==165 and len({r['slot'] for r in rows})==165
assert {r['hook'] for r in rows}=={r['label'] for r in contracts['hooks']}
assert len(contracts['api'])==34
assert rows[-1]['hook']=='client_actor_container_bulk_destroy'
for row in rows:
    assert row['signature']==contracts['hooks'][row['slot']]['signature']
    assert row['coverage']=='PHASE33_PENDING' and row['trigger_status']=='CANDIDATE_NOT_UI_VERIFIED'
    assert f"Slot {row['slot']}: {row['hook']}" in (root/'TEST_SEQUENCE.md').read_text()
source=(root/'runtime_diagnostics.inc').read_text()
assert 'for(size_t i=0;i<P31_HOOK_COUNT;++i)' in source
assert 'PHASE33_WINDOW_HOOK' in source and 'attribution=TEMPORAL_ONLY' in source
assert not re.search(r'\bg_(?:native|p272_original)\w*\s*\(',source)
assert 'p31_diagnostics_poll();' in (root/'dinput8_proxy.cpp').read_text()
subprocess.run([sys.executable,str(root/'tests/validate_legacy.py'),*sys.argv[1:]],check=True)
print('Phase33: 165 exact scenarios/signatures, 34 inherited API slots, real-counter windows and pending states reconciled.')
