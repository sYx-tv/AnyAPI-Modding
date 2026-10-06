"""Review actual Phase33 pipeline observations without inferring delivery/outcomes."""
import argparse,hashlib,json,re
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('logs',type=Path,nargs='+');p.add_argument('--output',type=Path,required=True)
p.add_argument('--identity',type=Path,default=Path(__file__).resolve().parents[1]/'CURRENT_BUILD_IDENTITY.json')
a=p.parse_args();captures={};rejected=[];seen=set();identity=json.loads(a.identity.read_text())
for path in a.logs:
    with path.open('rb') as stream:digest=hashlib.file_digest(stream,'sha256').hexdigest()
    if digest in seen:continue
    seen.add(digest);guards={}
    for line in path.open(encoding='utf-8',errors='replace'):
        pid=re.search(r'\[pid=(\d+)\]',line)
        if not pid:continue
        pid=pid.group(1)
        if 'PHASE27_BUILD_GUARD ' in line:guards[pid]=dict(re.findall(r'(\w+)=([^\s]+)',line))
        if 'PHASE33_PIPELINE ' not in line:continue
        guard=guards.get(pid,{})
        if guard.get('status')!='MATCH' or guard.get('exe_sha256')!=identity['game_exe_sha256'] or guard.get('gcl_sha256')!=identity['game_gcl_sha256']:
            rejected.append(dict(path=str(path),pid=pid,reason='NO_MATCHING_CURRENT_BUILD_GUARD'));continue
        row=dict(re.findall(r'(\w+)=([^\s]+)',line));row['pid']=pid
        if 'event' not in row or not row.get('handler_calls','').isdigit():
            rejected.append(dict(path=str(path),pid=pid,reason='MALFORMED_COUNTER'));continue
        key=(digest,pid,row['event']);old=captures.get(key)
        eligible=True
        if old and (not old['entry_evidence_eligible'] or int(row['handler_calls'])<int(old['handler_calls'])):
            eligible=False;rejected.append(dict(path=str(path),reason='COUNTER_RESTART',pid=pid,event=row['event']))
        row['entry_evidence_eligible']=eligible;row['file']=str(path);captures[key]=row
report=dict(captures=[dict(file_sha256=k[0],**v) for k,v in captures.items()],rejected=rejected,
    sender_delivery_verified=False,host_outcome_verified=False,
    limitations=['No summed cumulative counters or inferred host/client role.','Handler entry is not proof of sender, parser, payload or accepted result.','Cross-process causal correlation remains pending transport/sequence ownership.'])
a.output.write_text(json.dumps(report,indent=2)+'\n')
print('Reviewed',len(captures),'file/PID/event observations; delivery/outcome remain unverified.')
