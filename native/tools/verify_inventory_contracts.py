"""Read-only native inventory/layout verification. Requires capstone. Usage: python verify_inventory_contracts.py game.gcl records.json"""
from pathlib import Path
import json,re,hashlib,sys
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
s=Path(__file__).resolve().parent.parent;records={r['index']:r for r in json.loads(Path(sys.argv[2]).read_text(encoding='utf-8'))};blob=Path(sys.argv[1]).read_bytes();contracts=json.loads((s/'INVENTORY_ACTION_CONTRACTS.json').read_text(encoding='utf-8'));text=(s/'anyapi_inventory_patterns.h').read_text(encoding='utf-8')
assert hashlib.sha256(blob).hexdigest()=='01f889b4062c7a1bfa8b27506a27ecd559289eee7529768f8e88fc3aa3dd301c'
for name,c in contracts['functions'].items():
 r=records[c['index']];body=blob[r['body_offset']:r['body_offset']+r['code_size']];assert r['signature']==c['signature'] and r['code_size']==c['code_size'];assert hashlib.sha256(body).hexdigest()==c['sha256'];assert c['dependencies']=={hex(r['code_size']+i*8):d[1] for i,d in enumerate(r['dependencies'])};array=re.search('INV_'+name+r'\[\]\s*=\s*\{([^}]+)\}',text)[1];assert bytes(int(x,16) for x in re.findall('0x([0-9a-fA-F]{2})',array))==body
 if name=='SCREEN_LAYOUT':
  instructions=list(Cs(CS_ARCH_X86,CS_MODE_64).disasm(body[:21],0));assert sum(i.size for i in instructions)==21;assert all('rip' not in i.op_str and not i.mnemonic.startswith('j') and i.mnemonic!='call' for i in instructions)
print('Verified six exact inventory/layout native bodies and dependency tables; 21-byte relocation-free screen layout entry.')
