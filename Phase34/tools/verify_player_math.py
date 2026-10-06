from pathlib import Path
import json,hashlib,re,sys
S=Path(__file__).resolve().parent.parent;rs={r['index']:r for r in json.loads(Path(sys.argv[1] if len(sys.argv)>1 else 'CURRENT_GCL_RECORDS_20261005.json').read_text(encoding='utf-8'))};b=Path(r'C:\Program Files (x86)\Steam\steamapps\common\Anymaker\bin\game.gcl').read_bytes();cs=json.loads((S/'PLAYER_MATH_CONTRACTS.json').read_text(encoding='utf-8'));text=(S/'phase34_native_patterns.h').read_text(encoding='utf-8')
assert hashlib.sha256(b).hexdigest()==cs['game_gcl_sha256']
for name,c in cs['functions'].items():
 r=rs[c['index']];body=b[r['body_offset']:r['body_offset']+r['code_size']];assert c['signature']==r['signature'];assert hashlib.sha256(body).hexdigest()==c['sha256'];assert c['dependencies']=={hex(r['code_size']+i*8):d[1] for i,d in enumerate(r['dependencies'])}
 array=re.search(r'P34_'+name+r'\[\]\s*=\s*\{([^}]+)\}',text)[1];assert bytes(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{2})',array))==body
assert cs['functions']['CAMERA_MATH']['dependencies']['0x11a0']=='(mat33) mat33.rotation_y_axis (const f64)';assert cs['functions']['CAMERA_MATH']['dependencies']['0x11d8']=='(vec3) operator_mul (const mat33, const vec3)'
r=next(x for x in rs.values() if x['signature']=='(collision.ray) camera_actor.get_ray (const camera_actor)');import struct
assert [struct.unpack_from('<d',b,r['body_offset']+i)[0] for i in [0xf8,0xf8,0x100]]==[0,0,1]
assert cs['functions']['VIEW_OWNER']['dependencies']['0x410']=='(mat34) client_scene.actor.get_view_transform (const client_scene.actor)'
print('Verified four exact native camera/view bodies and dependencies; native ray uses +Z.')
