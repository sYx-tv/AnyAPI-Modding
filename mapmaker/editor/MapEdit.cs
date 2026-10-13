using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
namespace AnyMapMaker {
 // Editing operations on a map pack. Everything the editor places is written in the same shape the game's own
 // tiles use (checked against the 0.1.25 stock world, see docs/development/map-format.md):
 //  - positions are world coordinates in metres; an object belongs to the tile its position falls in
 //  - "rot" is radians about the vertical axis; an object's local +X points along (cos rot, -sin rot) in (x, z)
 //  - roads are cubic Bezier links: control points pos_a+dir_a and pos_b+dir_b, handles about 0.3 x the link length,
 //    stored in the tile that holds pos_b
 //  - lower triangles (t=0) use plain terrain pieces, upper ones (t=1) the matching "_in"/"_inv" pieces
 public sealed class FenceStyle {
  public string Name, Post, Panel; public double Spacing; // post to post, metres
  public FenceStyle(string name,string post,string panel,double spacing){Name=name;Post=post;Panel=panel;Spacing=spacing;}
  public override string ToString(){return Name;}
  // Spacings measured from the stock world's fence rows.
  public static readonly FenceStyle[] All={
   new FenceStyle("Wooden fence (post a, panel d)","fence_post_a","fence_panel_d",2.52),
   new FenceStyle("Picket fence (post a, panel a)","fence_post_a","fence_panel_a",2.52),
   new FenceStyle("Fence b","fence_post_b","fence_panel_b",2.52),
   new FenceStyle("Fence c","fence_post_c","fence_panel_c",2.68),
   new FenceStyle("Fence e","fence_post_e","fence_panel_e",2.52),
   new FenceStyle("Tall security fence","large_fence_post_a","large_fence_panel_a",10.04),
   new FenceStyle("Metal road divider","metal_divider_post_a","metal_divider_rail_a",3.16),
   new FenceStyle("Posts only (post a)","fence_post_a",null,2.52),
  };
 }

 public sealed class Catalog {
  public readonly List<string> TileDefinitions=new List<string>(), Props=new List<string>();
  public static readonly string[] RoadTypes={"road","road_b","road_c","road_d","train_a","train_b","train_flyover_a","train_flyover_b"};
  public static readonly string[] BuildingFilters={"house","town","industrial","farm","roadside","office","research","gas_station","sawmill"};
  public static Catalog Load(GameInstall game){
   var c=new Catalog();
   if(game!=null){
    foreach(var id in game.DefinitionIds("tile_definitions.json"))if(!c.TileDefinitions.Contains(id))c.TileDefinitions.Add(id);
    foreach(var id in game.DefinitionIds("prop_definitions.json"))if(!c.Props.Contains(id))c.Props.Add(id);
   }
   if(c.TileDefinitions.Count==0)c.TileDefinitions.AddRange(new[]{"tile_flat_a","tile_flat_a_in"});
   if(c.Props.Count==0)c.Props.AddRange(new[]{"lamp_post_a","fence_post_a","fence_panel_d","barrier_a","bench_a"});
   c.TileDefinitions.Sort(StringComparer.Ordinal);c.Props.Sort(StringComparer.Ordinal);
   return c;
  }
  static bool IsUpper(string d){return d.EndsWith("_in")||d.Contains("_in_")||d.EndsWith("_inv")||d.Contains("_inv_");}
  // Pieces offered in the palette: the lower-triangle names; the editor swaps in the upper variant per tile.
  public IEnumerable<string> PaletteNames(){return TileDefinitions.Where(d=>!IsUpper(d));}
  public string ForTriangle(string name,int t){
   if(t==0||IsUpper(name))return name;
   foreach(var suffix in new[]{"_in","_inv"})if(TileDefinitions.Contains(name+suffix))return name+suffix;
   return name;
  }
 }

 public sealed class MapEditor {
  public readonly MapPack Pack;
  readonly Stack<Dictionary<TileKey,string>> undo=new Stack<Dictionary<TileKey,string>>(), redo=new Stack<Dictionary<TileKey,string>>();
  Dictionary<TileKey,string> pending;
  readonly Dictionary<TileKey,string> loadedTerrain=new Dictionary<TileKey,string>();   // survives undo/redo
  public MapEditor(MapPack pack){Pack=pack;}
  public bool CanUndo {get{return undo.Count>0;}}
  public bool CanRedo {get{return redo.Count>0;}}

  // ---------------------------------------------------------------- undo (whole-tile snapshots, null = no tile)
  public void Begin(){if(pending==null)pending=new Dictionary<TileKey,string>();}
  public bool Commit(){var p=pending;pending=null;if(p==null||p.Count==0)return false;undo.Push(p);redo.Clear();return true;}
  void Touch(TileKey k){
   if(pending==null||pending.ContainsKey(k))return;
   var t=Pack.Get(k);pending[k]=t==null?null:Json.Write(t.Data,false);
   if(!loadedTerrain.ContainsKey(k))loadedTerrain[k]=t==null?null:t.TerrainAtLoad;
  }
  public bool Undo(){return Swap(undo,redo);}
  public bool Redo(){return Swap(redo,undo);}
  bool Swap(Stack<Dictionary<TileKey,string>> from,Stack<Dictionary<TileKey,string>> to){
   if(from.Count==0)return false;
   var snap=from.Pop();var back=new Dictionary<TileKey,string>();
   foreach(var kv in snap){
    var cur=Pack.Get(kv.Key);back[kv.Key]=cur==null?null:Json.Write(cur.Data,false);
    if(kv.Value==null)Pack.Tiles.Remove(kv.Key);
    else{string was;loadedTerrain.TryGetValue(kv.Key,out was);Pack.Tiles[kv.Key]=new Tile{Key=kv.Key,Data=(JsonObject)Json.Parse(kv.Value),Dirty=true,TerrainAtLoad=was};}
   }
   to.Push(back);Pack.UpdateBounds();return true;
  }

  // ---------------------------------------------------------------- helpers
  static List<object> List(JsonObject o,string key){var l=o.Arr(key);if(l==null){l=new List<object>();o[key]=l;}return l;}
  static JsonNumber N(double v){return new JsonNumber(Math.Round(v,4));}
  static List<object> V(Vec3 p){return new List<object>{N(p.X),N(p.Y),N(p.Z)};}
  public static double Angle(double dx,double dz){double r=Math.Atan2(-dz,dx);return r<0?r+2*Math.PI:r;}

  Tile TileAt(Vec3 p,bool touch){var k=Grid.At(p.X,p.Z);if(touch)Touch(k);return Pack.Get(k);}

  // Ground height at a point. Flat pieces are flat at height*32; elsewhere use the nearest thing already placed in
  // the same tile, which the game put on the ground. Falls back to the tile's base height.
  public double GroundY(double x,double z){
   var t=Pack.Get(Grid.At(x,z));if(t==null)return 0;
   double baseY=t.Height*Grid.Step;string d=t.Definition;
   if(d.StartsWith("tile_flat")||d.StartsWith("airfield")||d.StartsWith("trainyard_tile_flat"))return baseY;
   double best=60*60,y=baseY;
   foreach(var kv in t.Data){
    var list=kv.Value as List<object>;if(list==null||kv.Key=="junctions")continue;
    foreach(var item in list){
     var o=item as JsonObject;if(o==null)continue;
     foreach(var key in new[]{"pos","pos_a","pos_b"}){
      Vec3 p;if(!Vec3.TryRead(o[key],out p))continue;
      double dd=(p.X-x)*(p.X-x)+(p.Z-z)*(p.Z-z);if(dd<best){best=dd;y=p.Y;}
     }
    }
   }
   return y;
  }
  public Vec3 Ground(double x,double z,double lift){return new Vec3(x,GroundY(x,z)+lift,z);}

  // ---------------------------------------------------------------- placing
  public bool AddProp(string def,double x,double z,double rot,double lift){
   var p=Ground(x,z,lift);var t=TileAt(p,true);if(t==null)return false;
   var o=new JsonObject();o["def"]=def;o["pos"]=V(p);o["rot"]=N(rot);
   List(t.Data,"prop_spawn_zones").Add(o);t.Dirty=true;return true;
  }
  public int AddFence(FenceStyle s,double x0,double z0,double x1,double z1){
   double dx=x1-x0,dz=z1-z0,len=Math.Sqrt(dx*dx+dz*dz);if(len<s.Spacing*0.5)return 0;
   int spans=Math.Max(1,(int)Math.Round(len/s.Spacing));double rot=Angle(dx,dz);int n=0;
   for(int i=0;i<=spans;i++){
    double f=(double)i/spans*(spans*s.Spacing/len);
    if(AddProp(s.Post,x0+dx*f,z0+dz*f,rot,0))n++;
    if(s.Panel!=null&&i<spans){double g=(i+0.5)/spans*(spans*s.Spacing/len);if(AddProp(s.Panel,x0+dx*g,z0+dz*g,rot,0))n++;}
   }
   return n;
  }
  // A road through the clicked points: one link per pair, tangents from the neighbours (Catmull-Rom).
  public int AddRoad(string type,IList<Vec3> clicks){
   if(clicks.Count<2)return 0;
   var pts=clicks.Select(c=>Ground(c.X,c.Z,0)).ToList();
   var tan=new Vec3[pts.Count];
   for(int i=0;i<pts.Count;i++){
    Vec3 a=pts[Math.Max(0,i-1)],b=pts[Math.Min(pts.Count-1,i+1)],d=b-a;double l=d.Length;
    tan[i]=l>0?d*(1/l):new Vec3(1,0,0);
   }
   int n=0;
   for(int i=0;i+1<pts.Count;i++){
    Vec3 a=pts[i],b=pts[i+1];double len=(b-a).Length;if(len<1)continue;
    var t=TileAt(b,true);if(t==null)continue;
    var o=new JsonObject();
    o["pos_a"]=V(a);o["pos_b"]=V(b);o["dir_a"]=V(tan[i]*(len*0.3));o["dir_b"]=V(tan[i+1]*(-len*0.3));
    o["normal_a"]=V(new Vec3(0,1,0));o["normal_b"]=V(new Vec3(0,1,0));o["type"]=type;
    List(t.Data,"junction_links").Add(o);t.Dirty=true;n++;
   }
   return n;
  }
  public bool AddBuildingSpot(string filter,int size,double x,double z,double rot){
   var p=Ground(x,z,0);var t=TileAt(p,true);if(t==null)return false;
   var o=new JsonObject();o["pos"]=V(p);o["rot"]=N(rot);o["filter"]=filter;if(size>0)o["size"]=new JsonNumber(size.ToString());
   List(t.Data,"spawn_zones").Add(o);t.Dirty=true;return true;
  }
  public bool AddLoot(double x,double z,double rot){
   var p=Ground(x,z,0.02);var t=TileAt(p,true);if(t==null)return false;
   var o=new JsonObject();o["pos"]=V(p);o["rot"]=N(rot);
   List(t.Data,"loot_spawn_zones").Add(o);t.Dirty=true;return true;
  }
  public bool AddEnemySpawn(double x,double z){
   var p=Ground(x,z,0.02);var t=TileAt(p,true);if(t==null)return false;
   var o=new JsonObject();o["pos"]=V(p);o["waypoints"]=new List<object>();
   List(t.Data,"actor_spawn_zones").Add(o);t.Dirty=true;return true;
  }

  // ---------------------------------------------------------------- erasing
  static readonly string[] PointLists={"prop_spawn_zones","spawn_zones","loot_spawn_zones","actor_spawn_zones","dead_drop_spawn_zones"};
  // Removes placed things and road links within radius of (x,z). Junctions are tile-local and left alone.
  public int Erase(double x,double z,double radius,bool things,bool roads){
   int n=0;double r2=radius*radius;
   var near=new List<TileKey>();
   foreach(var t in Pack.Tiles.Values){var o=t.Origin;double dx=o.X-x,dz=o.Z-z;if(dx*dx+dz*dz<(radius+600)*(radius+600))near.Add(t.Key);}
   foreach(var k in near){
    var t=Pack.Get(k);bool touched=false;
    foreach(var key in things?PointLists:new string[0]){
     var list=t.Data.Arr(key);if(list==null)continue;
     for(int i=list.Count-1;i>=0;i--){
      var o=list[i] as JsonObject;Vec3 p;if(o==null||!Vec3.TryRead(o["pos"],out p))continue;
      if((p.X-x)*(p.X-x)+(p.Z-z)*(p.Z-z)<=r2){if(!touched){Touch(k);touched=true;}list.RemoveAt(i);n++;}
     }
    }
    var links=roads?t.Data.Arr("junction_links"):null;
    if(links!=null)for(int i=links.Count-1;i>=0;i--){
     var o=links[i] as JsonObject;Vec3 a,b,da,db;
     if(o==null||!Vec3.TryRead(o["pos_a"],out a)||!Vec3.TryRead(o["pos_b"],out b))continue;
     Vec3.TryRead(o["dir_a"],out da);Vec3.TryRead(o["dir_b"],out db);
     foreach(var p in MapScene.Bezier(a,b,da,db))
      if((p.X-x)*(p.X-x)+(p.Z-z)*(p.Z-z)<=r2){if(!touched){Touch(k);touched=true;}links.RemoveAt(i);n++;break;}
    }
    if(touched)t.Dirty=true;
   }
   return n;
  }

  // ---------------------------------------------------------------- terrain
  public static JsonObject NewTile(string def,int height,int orientation){
   var o=new JsonObject();o["definition_name"]=def;
   if(orientation!=0)o["orientation"]=new JsonNumber(orientation.ToString());
   if(height!=0)o["height"]=new JsonNumber(height.ToString());
   foreach(var k in new[]{"spawn_zones","junctions","junction_links","prop_spawn_zones","loot_spawn_zones","actor_spawn_zones","dead_drop_spawn_zones"})o[k]=new List<object>();
   return o;
  }
  static void SetInt(JsonObject o,string key,int v){if(v==0)o.Remove(key);else o[key]=new JsonNumber(v.ToString());}
  // Paints a terrain piece onto a triangle (creating land where there was ocean). Placed things keep their spots,
  // except that a height change moves them up or down with the ground.
  public bool PaintTile(TileKey k,Catalog cat,string piece,int? height,int? orientation){
   Touch(k);var t=Pack.Get(k);
   string def=cat.ForTriangle(piece,k.T);
   if(t==null){
    t=new Tile{Key=k,Data=NewTile(def,height??NeighbourHeight(k),orientation??0),Dirty=true};
    Pack.Tiles[k]=t;Pack.UpdateBounds();return true;
   }
   t.Data["definition_name"]=def;
   if(orientation!=null)SetInt(t.Data,"orientation",orientation.Value);
   if(height!=null)SetHeight(t,height.Value);
   t.Dirty=true;return true;
  }
  public bool SetHeight(TileKey k,int height){Touch(k);var t=Pack.Get(k);if(t==null)return false;SetHeight(t,height);t.Dirty=true;return true;}
  void SetHeight(Tile t,int height){
   height=Math.Max(-6,Math.Min(16,height));
   double dy=(height-t.Height)*Grid.Step;SetInt(t.Data,"height",height);
   if(dy==0)return;
   foreach(var kv in t.Data){
    var list=kv.Value as List<object>;if(list==null||kv.Key=="junctions")continue;  // junctions are tile-local
    foreach(var item in list){
     var o=item as JsonObject;if(o==null)continue;
     foreach(var key in new[]{"pos","pos_a","pos_b"}){Vec3 p;if(Vec3.TryRead(o[key],out p))o[key]=V(new Vec3(p.X,p.Y+dy,p.Z));}
     var wps=o.Arr("waypoints");
     if(wps!=null)for(int i=0;i<wps.Count;i++){Vec3 p;if(Vec3.TryRead(wps[i],out p))wps[i]=V(new Vec3(p.X,p.Y+dy,p.Z));}
    }
   }
  }
  public bool Rotate(TileKey k){Touch(k);var t=Pack.Get(k);if(t==null)return false;SetInt(t.Data,"orientation",(t.Orientation+1)%3);t.Dirty=true;return true;}
  public bool RemoveTile(TileKey k){if(Pack.Get(k)==null)return false;Touch(k);Pack.Tiles.Remove(k);Pack.UpdateBounds();return true;}
  int NeighbourHeight(TileKey k){
   foreach(var n in new[]{new TileKey(k.X,k.Z,1-k.T),new TileKey(k.X+(k.T==0?-1:1),k.Z,1-k.T),new TileKey(k.X,k.Z+(k.T==0?-1:1),1-k.T)}){
    var t=Pack.Get(n);if(t!=null)return t.Height;
   }
   return 0;
  }

  // A flat island of w x h grid cells (two triangles each) centred on (cx, cz), surrounded by ocean.
  public static MapPack BlankIsland(Catalog cat,int cx,int cz,int w,int h,int height){
   var pack=new MapPack{Name="My island",Base="blank"};
   for(int x=cx-w/2;x<cx-w/2+w;x++)for(int z=cz-h/2;z<cz-h/2+h;z++)for(int t=0;t<2;t++){
    var k=new TileKey(x,z,t);pack.Tiles[k]=new Tile{Key=k,Data=NewTile(cat.ForTriangle("tile_flat_a",t),height,0),Dirty=true};
   }
   pack.UpdateBounds();return pack;
  }
 }
}
