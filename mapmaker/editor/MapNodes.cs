using System;
using System.Collections.Generic;
namespace AnyMapMaker {
 // Draggable nodes for the 3D view: road link ends (shared by every link that meets there) and placed points
 // (props, loot, building spots, enemy spawns, dead drops). Moving a road node can pull nearby nodes of the road
 // along with a smooth falloff, like proportional editing in Blender, which is how inclines and ramps are shaped.
 public sealed class MapNode {
  public Vec3 Position; public bool IsRoad; public string Label;
  internal readonly List<LinkEnd> Ends=new List<LinkEnd>();   // road node
  internal Tile Tile; internal string ListKey; internal JsonObject Point;   // point node
 }
 internal sealed class LinkEnd { public Tile Tile; public JsonObject Link; public bool IsB; }

 public sealed partial class MapEditor {
  const double NodeSnap=0.5;   // link ends closer than this are one node

  static Vec3 Read(JsonObject o,string key){Vec3 v;Vec3.TryRead(o[key],out v);return v;}
  static string Key(Vec3 p){return Math.Round(p.X/NodeSnap)+","+Math.Round(p.Z/NodeSnap);}

  // Every node in the pack. Rebuilt after each edit; cheap enough for the stock world (about 7,000 link ends).
  public List<MapNode> Nodes(bool roads,bool points){
   var nodes=new List<MapNode>();var byKey=new Dictionary<string,MapNode>();
   foreach(var tile in Pack.Tiles.Values){
    if(roads){
     var links=tile.Data.Arr("junction_links");
     if(links!=null)foreach(var item in links){
      var o=item as JsonObject;if(o==null||!o.Has("pos_a")||!o.Has("pos_b"))continue;
      foreach(bool b in new[]{false,true}){
       var p=Read(o,b?"pos_b":"pos_a");string k=Key(p);MapNode n;
       if(!byKey.TryGetValue(k,out n)){n=new MapNode{Position=p,IsRoad=true,Label=o.Str("type")??"road"};byKey[k]=n;nodes.Add(n);}
       n.Ends.Add(new LinkEnd{Tile=tile,Link=o,IsB=b});
      }
     }
    }
    if(points)foreach(var key in new[]{"prop_spawn_zones","loot_spawn_zones","spawn_zones","actor_spawn_zones","dead_drop_spawn_zones"}){
     var list=tile.Data.Arr(key);if(list==null)continue;
     foreach(var item in list){
      var o=item as JsonObject;Vec3 p;if(o==null||!Vec3.TryRead(o["pos"],out p))continue;
      nodes.Add(new MapNode{Position=p,Tile=tile,ListKey=key,Point=o,Label=o.Str("def")??o.Str("filter")??key.Replace("_spawn_zones","")});
     }
    }
   }
   return nodes;
  }

  // One drag of one node. Weights are fixed when the drag starts, so moving back and forth is exact.
  public sealed class NodeDrag {
   internal readonly List<KeyValuePair<MapNode,double>> Moving=new List<KeyValuePair<MapNode,double>>();
   internal readonly Dictionary<MapNode,Vec3> Start=new Dictionary<MapNode,Vec3>();
   public MapNode Node;
  }
  public NodeDrag StartDrag(MapNode node,List<MapNode> all,double falloff){
   Begin();IndexEnds(all);var d=new NodeDrag{Node=node};
   d.Moving.Add(new KeyValuePair<MapNode,double>(node,1));
   if(node.IsRoad&&falloff>0){
    // Only nodes on roads connected to this one, so a parallel road nearby stays put.
    var linked=Connected(node,all);
    foreach(var n in linked){
     if(n==node)continue;double dx=n.Position.X-node.Position.X,dz=n.Position.Z-node.Position.Z,dist=Math.Sqrt(dx*dx+dz*dz);
     if(dist<falloff)d.Moving.Add(new KeyValuePair<MapNode,double>(n,0.5*(1+Math.Cos(Math.PI*dist/falloff))));
    }
   }
   foreach(var kv in d.Moving){d.Start[kv.Key]=kv.Key.Position;Touch(kv.Key);}
   return d;
  }
  static List<MapNode> Connected(MapNode start,List<MapNode> all){
   var byLink=new Dictionary<JsonObject,List<MapNode>>();
   foreach(var n in all)if(n.IsRoad)foreach(var e in n.Ends){List<MapNode> l;if(!byLink.TryGetValue(e.Link,out l))byLink[e.Link]=l=new List<MapNode>();l.Add(n);}
   var seen=new HashSet<MapNode>{start};var queue=new Queue<MapNode>();queue.Enqueue(start);
   while(queue.Count>0){var n=queue.Dequeue();foreach(var e in n.Ends)foreach(var m in byLink[e.Link])if(seen.Add(m))queue.Enqueue(m);}
   return new List<MapNode>(seen);
  }
  void Touch(MapNode n){if(n.IsRoad)foreach(var e in n.Ends)Touch(e.Tile.Key);else Touch(n.Tile.Key);}

  // Moves the drag to an offset from where it started.
  public void DragTo(NodeDrag d,Vec3 offset){
   var links=new HashSet<JsonObject>();
   foreach(var kv in d.Moving){
    var n=kv.Key;var p=d.Start[n]+offset*kv.Value;
    if(!n.IsRoad&&Pack.Get(Grid.At(p.X,p.Z))==null)continue;   // points stay on land
    n.Position=p;
    if(n.IsRoad){foreach(var e in n.Ends){e.Link[e.IsB?"pos_b":"pos_a"]=V(p);links.Add(e.Link);e.Tile.Dirty=true;}}
    else{n.Point["pos"]=V(p);n.Tile.Dirty=true;Rehome(n);}
   }
   // Smooth handles around everything that moved, then keep each link in the tile that holds its pos_b.
   var touched=new HashSet<MapNode>();foreach(var kv in d.Moving)if(kv.Key.IsRoad)touched.Add(kv.Key);
   // Links one step beyond the moved ones change tangent at their shared node too.
   var ring=new HashSet<JsonObject>(links);
   foreach(var link in links)foreach(var end in new[]{Read(link,"pos_a"),Read(link,"pos_b")})foreach(var o in LinksAt(end))if(ring.Add(o))TouchLink(o);
   foreach(var link in ring)Reshape(link);
   foreach(var n in touched)foreach(var e in n.Ends)if(e.IsB)RehomeLink(e,n);
  }

  // Handles follow Catmull-Rom tangents through each node (including height, so a raised node makes a smooth
  // ramp), a third of the link's length long. dir_b points back toward pos_a, as in the stock tiles.
  void Reshape(JsonObject link){
   Vec3 a=Read(link,"pos_a"),b=Read(link,"pos_b");double len=(b-a).Length;if(len<0.01)return;
   Vec3 ta=Tangent(link,false,a,b),tb=Tangent(link,true,a,b);
   link["dir_a"]=V(ta*(len/3));link["dir_b"]=V(tb*(-len/3));
  }
  // Direction of travel (from pos_a toward pos_b) at one end of a link, from the link that continues it there.
  Vec3 Tangent(JsonObject link,bool atB,Vec3 a,Vec3 b){
   Vec3 here=atB?b:a,chord=b-a;Vec3 other;
   if(FindContinuation(link,here,out other)){
    Vec3 t=atB?other-a:b-other;double l=t.Length;if(l>0.01)return t*(1/l);
   }
   double c=chord.Length;return chord*(1/c);
  }
  // The far end of another link meeting this one at `at`, when exactly one other link meets there.
  bool FindContinuation(JsonObject link,Vec3 at,out Vec3 far){
   far=new Vec3();int found=0;string k=Key(at);
   foreach(var tile in NearTiles(at)){
    var links=tile.Data.Arr("junction_links");if(links==null)continue;
    foreach(var item in links){
     var o=item as JsonObject;if(o==null||o==link||!o.Has("pos_a"))continue;
     Vec3 pa=Read(o,"pos_a"),pb=Read(o,"pos_b");
     if(Key(pa)==k){far=pb;found++;}else if(Key(pb)==k){far=pa;found++;}
    }
   }
   return found==1;
  }
  IEnumerable<JsonObject> LinksAt(Vec3 at){
   string k=Key(at);
   foreach(var tile in NearTiles(at)){
    var links=tile.Data.Arr("junction_links");if(links==null)continue;
    foreach(var item in links){var o=item as JsonObject;if(o!=null&&o.Has("pos_a")&&(Key(Read(o,"pos_a"))==k||Key(Read(o,"pos_b"))==k))yield return o;}
   }
  }
  void TouchLink(JsonObject o){var b=Read(o,"pos_b");foreach(var tile in NearTiles(b)){var l=tile.Data.Arr("junction_links");if(l!=null&&l.Contains(o)){Touch(tile.Key);tile.Dirty=true;return;}}}
  // Links are stored by pos_b, so the ones meeting at a point live in that tile or a neighbour of it.
  IEnumerable<Tile> NearTiles(Vec3 p){
   var k=Grid.At(p.X,p.Z);
   for(int dx=-1;dx<=1;dx++)for(int dz=-1;dz<=1;dz++)for(int t=0;t<2;t++){var tile=Pack.Get(new TileKey(k.X+dx,k.Z+dz,t));if(tile!=null)yield return tile;}
  }
  void RehomeLink(LinkEnd e,MapNode n){
   var want=Pack.Get(Grid.At(n.Position.X,n.Position.Z));if(want==null||want==e.Tile)return;
   Touch(want.Key);List(e.Tile.Data,"junction_links").Remove(e.Link);List(want.Data,"junction_links").Add(e.Link);
   e.Tile.Dirty=true;want.Dirty=true;
   // The same link object is referenced from the node at its other end too.
   foreach(var other in OtherEnds(e.Link))other.Tile=want;
   e.Tile=want;
  }
  readonly Dictionary<JsonObject,List<LinkEnd>> endsByLink=new Dictionary<JsonObject,List<LinkEnd>>();
  IEnumerable<LinkEnd> OtherEnds(JsonObject link){List<LinkEnd> l;return endsByLink.TryGetValue(link,out l)?l:new List<LinkEnd>();}
  // The node list a drag works on, so moving a link to another tile updates every reference to it.
  void IndexEnds(List<MapNode> nodes){endsByLink.Clear();foreach(var n in nodes)if(n.IsRoad)foreach(var e in n.Ends){List<LinkEnd> l;if(!endsByLink.TryGetValue(e.Link,out l))endsByLink[e.Link]=l=new List<LinkEnd>();l.Add(e);}}
  void Rehome(MapNode n){
   var want=Pack.Get(Grid.At(n.Position.X,n.Position.Z));if(want==null||want==n.Tile)return;
   Touch(want.Key);List(n.Tile.Data,n.ListKey).Remove(n.Point);List(want.Data,n.ListKey).Add(n.Point);
   n.Tile.Dirty=true;want.Dirty=true;n.Tile=want;
  }
  public bool EndDrag(NodeDrag d){return Commit();}
 }
}
