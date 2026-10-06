"""Summarize a single-process runtime log without upgrading partial coverage."""
import argparse
import hashlib
import json
import re
from pathlib import Path

parser=argparse.ArgumentParser()
parser.add_argument('log',type=Path)
parser.add_argument('output',type=Path)
args=parser.parse_args()
blob=args.log.read_bytes();text=blob.decode('utf-8',errors='replace')
pids=sorted(set(re.findall(r'\[pid=(\d+)\]',text)))
if len(pids)!=1: raise SystemExit('Expected one current-process log; do not combine historical/duplicate captures')
hooks={};callbacks={};metadata={};threads={};phase29={};api={}
for line in text.splitlines():
    for marker,destination,key in [
        ('PHASE27_HOOK_AUDIT',hooks,'target'),('PHASE272_CALLBACK',callbacks,'kind'),
        ('PHASE273_COMPONENT_METADATA',metadata,'side'),('PHASE33_THREAD',threads,'slot'),('PHASE33_API',api,'slot')]:
        if marker+' ' in line:
            fields=dict(re.findall(r'(\w+)=([^\s]+)',line.split(marker+' ',1)[1]))
            if key in fields: destination[fields[key]]=fields
    match=re.search(r'(PHASE33_(?:CONTRACT|RENDER|INVENTORY|COMPONENT_TRANSFORM|SINGLE_SESSION)) ',line)
    if match:phase29[match.group(1)]=line
result=dict(source=args.log.name,sha256=hashlib.sha256(blob).hexdigest(),pid=pids[0],lines=len(text.splitlines()),
            installed=sum(int(h.get('installed',0)) for h in hooks.values()),
            exercised=sum(int(h.get('calls',0))>0 for h in hooks.values()),hooks=hooks,callbacks=callbacks,
            metadata=metadata,phase33_threads=threads,phase33_latest=phase29,api=api,
            unexercised_hooks=[name for name,row in hooks.items() if int(row.get('calls',0))==0],
            api_status_counts={status:sum(row.get('status')==status for row in api.values()) for status in sorted({row.get('status','UNKNOWN') for row in api.values()})},
            failure_flags=['CLASS_METADATA_CONFLICTS'] if any(int(row.get('mismatch',0)) for row in metadata.values()) else [],
            phase33_status='RECORDED_REQUIRES_REVIEW' if len(threads)==165 and phase29 else 'PHASE33_RUNTIME_NOT_RECORDED',
            limitations=['Installation is not exercise or semantic acceptance.','Latest counters are not final-event guarantees.',
                         'No minidump or RAM CSV is implied by this log.', 'Mixed-process logs and duplicate copies must be reviewed separately.'])
if any(int(row.get('guard_failures',0)) or int(row.get('functional_failures',0)) for row in api.values()): result['failure_flags'].append('API_CHECK_FAILURES')
if any(int(row.get('delivery_errors',0)) or int(row.get('payload_errors',0)) for row in callbacks.values()): result['failure_flags'].append('CALLBACK_FAILURES')
if result['failure_flags']:result['phase33_status']='RUNTIME_FAILURES_RECORDED'
args.output.write_text(json.dumps(result,indent=2)+'\n')
print(f"PID {pids[0]}: installed={result['installed']}, exercised={result['exercised']}; {result['phase33_status']}")
