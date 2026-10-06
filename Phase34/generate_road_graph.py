"""Build a local routing cache from installed, authored road curves and junction routes.
Game-derived cache stays in the local delivery; source archives contain this generator only.
"""
import json,math,struct,hashlib,collections,itertools,sys,re
from pathlib import Path
base=Path(sys.argv[2]) if len(sys.argv)>2 else Path(r'C:\Program Files (x86)\Steam\steamapps\common\Anymaker')
destination=Path(sys.argv[1]) if len(sys.argv)>1 else Path('roads_revision2.bin')
data=base/'rom/data';defs={d['name']:d for d in json.loads((data/'tile_junction_definitions.json').read_text())['definitions']}
curves=[];all_tiles=[];digest=hashlib.sha256();fit_stats=collections.Counter()
def road(t):return t.startswith('road')
def add(a,b):return tuple(x+y for x,y in zip(a,b))
def sub(a,b):return tuple(x-y for x,y in zip(a,b))
def distance(a,b):return math.dist(a,b)
def rotate(v,j,sign):
 nx,ny,nz=j.get('normal',[0,1,0]);angle=j.get('rotation',0)*sign+(math.atan2(nx,nz) if math.hypot(nx,nz)>1e-12 else 0);c,s=math.cos(angle),math.sin(angle);x,y,z=v
 v=(x*c+z*s,y,-x*s+z*c)
 nx,ny,nz=j.get('normal',[0,1,0]);norm=math.sqrt(nx*nx+ny*ny+nz*nz)
 if norm<1e-9:return v
 nx,ny,nz=nx/norm,ny/norm,nz/norm;sn=math.hypot(nx,nz)
 if sn<1e-9:return v
 ax,ay,az=nz/sn,0,-nx/sn;x,y,z=v;dot=ax*x+ay*y+az*z
 return (x*ny+(ay*z-az*y)*sn+ax*dot*(1-ny),y*ny+(az*x-ax*z)*sn,z*ny+(ax*y-ay*x)*sn+az*dot*(1-ny))
def local(p,j,sign):return add(j['position'],rotate(p,j,sign))
def tile_rotate(p,angle):
 c,s=math.cos(angle),math.sin(angle);x,y,z=p;return (x*c+z*s,y,-x*s+z*c)
for path in sorted((data/'tiles/0').glob('*.json')):
 raw=path.read_bytes();digest.update(path.name.encode()+b'\0'+raw);tile=json.loads(raw);all_tiles.append((path.name,tile))
 for link in tile.get('junction_links',[]):
  if road(link.get('type','')):curves.append((link['pos_a'],link['pos_b'],link['dir_a'],link['dir_b']))
digest.update((data/'tile_junction_definitions.json').read_bytes())
endpoint_cells=collections.defaultdict(list)
for _,tile in all_tiles:
 for link in tile.get('junction_links',[]):
  for key in ['pos_a','pos_b']:
   endpoint_cells[(link.get('type',''),tuple(math.floor(v) for v in link[key]))].append(link[key])
def matches_port(point,typ):
 cell=tuple(math.floor(v) for v in point)
 return any(distance(point,e)<1 for delta in itertools.product([-1,0,1],repeat=3) for e in endpoint_cells[(typ,tuple(cell[k]+delta[k] for k in range(3)))])
for name,tile in all_tiles:
 endpoints=[(link[k],link.get('type','')) for link in tile.get('junction_links',[]) for k in ['pos_a','pos_b']]
 match=re.fullmatch(r'tile_(\d+)_(\d+)_(\d+)\.json',name)
 if not match:continue
 x,z,triangle=map(int,match.groups());height=256*math.sqrt(3)
 origin=(512*x+256*z+(512 if triangle else 256),tile.get('height',0)*32,height*(z+(2/3 if triangle else 1/3)))
 sign,angle=1,0;fit_stats['tiles_with_world_origin']+=1
 for j in tile.get('junctions',[]):
  definition=defs.get(j.get('definition_name'))
  if not definition:continue
  matches=sum(matches_port(add(origin,local(p.get('pos',[0,0,0]),j,sign)),p.get('type','')) for p in definition['points'])
  if not matches:fit_stats['unmatched_junctions']+=1;continue
  points=definition['route_points']
  # Join authored lane endpoints to their own junction's centerline port.
  # Matching is confined to the definition, with same road type/direction.
  for p in points:
   if not road(p.get('type','')):continue
   candidates=[q for q in definition['points'] if q.get('type')==p.get('type')]
   if not candidates:continue
   q=min(candidates,key=lambda q:distance(p.get('pos',[0,0,0]),q.get('pos',[0,0,0])))
   gap=distance(p.get('pos',[0,0,0]),q.get('pos',[0,0,0]))
   if .001<gap<=8:
    dp,dq=p.get('dir',[0,0,0]),q.get('dir',[0,0,0]);length=math.dist(dp,[0,0,0])*math.dist(dq,[0,0,0])
    if length and sum(a*b for a,b in zip(dp,dq))/length>.98:
     curves.append((add(origin,local(p.get('pos',[0,0,0]),j,sign)),add(origin,local(q.get('pos',[0,0,0]),j,sign)),(0,0,0),(0,0,0)));fit_stats['junction_lane_connectors']+=1
  for route in definition['routes']:
   a,b=route.get('a',0),route.get('b',0)
   if a==b or not 0<=a<len(points) or not 0<=b<len(points):continue
   p,q=points[a],points[b]
   if not road(p.get('type','')) or not road(q.get('type','')):continue
   curves.append((add(origin,tile_rotate(local(p.get('pos',[0,0,0]),j,sign),angle)),add(origin,tile_rotate(local(q.get('pos',[0,0,0]),j,sign),angle)),tile_rotate(rotate(p.get('dir',[0,0,0]),j,sign),angle),tile_rotate(rotate(q.get('dir',[0,0,0]),j,sign),angle)))
   fit_stats['authored_junction_routes']+=1
nodes=[];cells=collections.defaultdict(list);edges=[];points=[];seen=set()
def node(p):
 cell=tuple(math.floor(v) for v in p)
 for offset in itertools.product([-1,0,1],repeat=3):
  for index in cells[tuple(cell[k]+offset[k] for k in range(3))]:
   if distance(nodes[index],p)<.8:return index
 index=len(nodes);nodes.append(tuple(p));cells[cell].append(index);return index
for a,b,da,db in curves:
 if not all(math.isfinite(v) for p in [a,b,da,db] for v in p):continue
 key=tuple(round(v,3) for p in [a,b,da,db] for v in p)
 if key in seen:continue
 seen.add(key);n=max(2,min(128,math.ceil((distance(a,b)+math.dist(da,[0,0,0])+math.dist(db,[0,0,0]))/8)))
 curve=[]
 for i in range(n+1):
  t=i/n;curve.append(tuple((1-t)**3*a[k]+3*(1-t)**2*t*(a[k]+da[k])+3*(1-t)*t*t*(b[k]+db[k])+t**3*b[k] for k in range(3)))
 u,v=node(a),node(b)
 if u==v:continue
 edges.append((u,v,len(points),len(curve)));points.extend(curve)
adj=[[] for n in nodes]
for a,b,_,_ in edges:adj[a].append(b);adj[b].append(a)
visited=set();components=[]
for index in range(len(nodes)):
 if index in visited:continue
 stack=[index];visited.add(index);size=0
 while stack:
  u=stack.pop();size+=1
  for v in adj[u]:
   if v not in visited:visited.add(v);stack.append(v)
 components.append(size)
payload=struct.pack('<8sIIII3d',b'ANYROAD2',2,len(nodes),len(edges),len(points),81000.,52000.,6508.)
payload+=b''.join(struct.pack('<3d',*n) for n in nodes)
payload+=b''.join(struct.pack('<4I',*e) for e in edges)
payload+=b''.join(struct.pack('<3d',*p) for p in points)
destination.write_bytes(payload)
report={'nodes':len(nodes),'edges':len(edges),'curve_points':len(points),'connected_components':len(components),'largest_components':sorted(components,reverse=True)[:10],
 'fit_stats':dict(fit_stats),'authored_asset_digest':digest.hexdigest(),'cache_sha256':hashlib.sha256(payload).hexdigest(),
 'policy':'Road links plus validated authored junction routes only; no guessed proximity shortcuts between roads. Bidirectional centerline routing; not a traffic-law model.'}
destination.with_suffix('.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))




