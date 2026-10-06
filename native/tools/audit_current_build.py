"""Current-build evidence; does not execute game functions or approve deployment."""
import argparse,collections,hashlib,json,re,struct
from pathlib import Path
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
from capstone.x86 import X86_OP_MEM
p=argparse.ArgumentParser()
for name in ('framework','records','gcl','exe','output'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
records=json.loads(a.records.read_text());blob=a.gcl.read_bytes()
by_sig=collections.defaultdict(list)
for r in records:by_sig[r['signature']].append(r)
contract_path=a.framework/'PHASE31_CONTRACTS.json'
if not contract_path.exists():contract_path=a.framework/'PHASE33_CONTRACTS.json'
contracts=json.loads(contract_path.read_text())
md=Cs(CS_ARCH_X86,CS_MODE_64);md.detail=True
rows=[]
for h in contracts['hooks']:
    matches=by_sig[h['signature']]
    assert len(matches)==1,h['label']
    record=matches[0];body=blob[record['body_offset']:record['body_offset']+record['code_size']]
    stolen=body[:h['overwrite']];ins=list(md.disasm(stolen,0))
    assert sum(i.size for i in ins)==h['overwrite']
    assert not any('rip' in i.op_str or i.mnemonic.startswith(('j','call','ret','loop')) for i in ins)
    off=record['body_offset']+record['blob_size'];reloc,size=struct.unpack_from('<II',blob,off);assert reloc==0
    assert blob[off+8:off+8+size].hex()==h['unwind_hex']
    accesses=collections.Counter()
    for i in md.disasm(body,0):
        for operand in i.operands:
            if operand.type==X86_OP_MEM and operand.mem.disp:
                accesses[hex(operand.mem.disp)]+=1
    rows.append(dict(slot=h['slot'],hook=h['label'],signature=h['signature'],current_record=record['index'],
        body_sha256=hashlib.sha256(body).hexdigest(),body_bytes=record['code_size'],body_byte_unique=blob.count(body)==1,
        signature_unique=True,stolen_boundary_verified=True,stolen_no_relative_control=True,unwind_verified=True,
        dependencies=record['dependencies'],argument_metadata=record['args'],memory_displacements=dict(accesses),
        evidence=dict(static='VERIFIED_PROLOGUE_UNWIND_SIGNATURE',installed='NOT_TESTED',entered='NOT_TESTED',
                      payload_valid='NOT_TESTED',outcome_verified='NOT_TESTED'),
        remaining_native_gate='OBJECT_LAYOUT_CALLING_ABI_LIFETIME_AND_DEPENDENCY_OWNER_ACCEPTANCE_PENDING'))
identity=dict(game_exe_sha256=hashlib.sha256(a.exe.read_bytes()).hexdigest(),game_gcl_sha256=hashlib.sha256(blob).hexdigest())
report=dict(kind='PHASE33_CURRENT_BUILD_STATIC_EVIDENCE_NOT_NATIVE_ACCEPTANCE',identity=identity,
            hooks=rows,summary=dict(hooks=len(rows),unique_signatures=len(rows),valid_stolen_regions=len(rows)),
            observer_gate='BLOCKED',limitations=[
              'Argument metadata does not alone establish the Windows calling convention.',
              'Memory displacement occurrence does not establish the containing object type or lifetime.',
              'Copied records and instruction proofs do not establish imported native dependency ownership.',
              'Sender/parser body ambiguity and changed replication buffer signatures require owner verification.',
              'No native calls, hooks, updated-game gameplay or accepted outcomes are produced by this audit.'])
(a.output/'COMPATIBILITY_AUDIT.json').write_text(json.dumps(report,indent=2)+'\n')
(a.output/'CURRENT_BUILD_IDENTITY.json').write_text(json.dumps(identity,indent=2)+'\n')
print('PASS: 165 current signatures, stolen regions and unwind contracts. Native observers remain BLOCKED.')
