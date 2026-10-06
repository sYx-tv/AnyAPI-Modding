"""Synthetic reporter fixtures only; not native sender/host gameplay evidence."""
from pathlib import Path
import json,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='phase33_network_') as directory:
    tmp=Path(directory);identity=tmp/'identity.json';identity.write_text(json.dumps(dict(game_exe_sha256='exe',game_gcl_sha256='gcl')))
    guard='[pid=1] PHASE27_BUILD_GUARD status=MATCH exe_sha256=exe gcl_sha256=gcl\n'
    log=tmp/'a.log';duplicate=tmp/'duplicate.log';old=tmp/'old.log';output=tmp/'out.json'
    data=guard+'[pid=1] PHASE33_PIPELINE event=pickup handler_calls=5 sender=NOT_INSTRUMENTED\n'
    data+='[pid=1] PHASE33_PIPELINE event=pickup handler_calls=2 sender=NOT_INSTRUMENTED\n'
    data+='[pid=2] PHASE33_PIPELINE event=pickup handler_calls=99\n'
    data+='[pid=1] PHASE33_PIPELINE event=drop handler_calls=not_a_number\n'
    log.write_text(data);duplicate.write_text(data);old.write_text(guard.replace('gcl_sha256=gcl','gcl_sha256=old')+'[pid=1] PHASE33_PIPELINE event=pickup handler_calls=20\n')
    subprocess.run([sys.executable,str(root/'tools/review_network_evidence.py'),str(log),str(duplicate),str(old),'--identity',str(identity),'--output',str(output)],check=True)
    report=json.loads(output.read_text());assert len(report['captures'])==1
    assert not report['captures'][0]['entry_evidence_eligible']
    assert {r['reason'] for r in report['rejected']}=={'COUNTER_RESTART','NO_MATCHING_CURRENT_BUILD_GUARD','MALFORMED_COUNTER'}
    assert not report['sender_delivery_verified'] and not report['host_outcome_verified']
print('PASS: networking reporter deduplicates contents, separates PID/build guards, detects counter restarts and preserves missing delivery/outcome proof.')
