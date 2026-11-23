using System;
using System.Collections.Generic;
using System.Drawing;
namespace AnyMapMaker {
 // What the map view draws, worked out from the tiles. Rebuilt after loading or editing.
 public sealed class Road {
  public TileKey Tile; public string Type; public Vec3[] Points;
 }
 public sealed class Marker {
  public TileKey Tile; public string Layer, Label; public Vec3 Position; public double RotationY; public bool HasRotation;
  public JsonObject Source;
 }
 public sealed class Layer {
  public string Key, Title; public Color Color; public bool Visible=true; public int Count;
  public Layer(string key,string title,Color color){Key=key;Title=title;Color=color;}
 }

 public sealed class MapScene {
  public readonly List<Road> Roads=new List<Road>();
  public readonly List<Marker> Markers=new List<Marker>();
  public readonly Dictionary<string,bool> LocalSpace=new Dictionary<string,bool>();
  public static readonly List<Layer> Layers=new List<Layer>{
   new Layer("terrain","Terrain",Color.FromArgb(84,128,72)),
   new Layer("roads","Roads and paths",Color.FromArgb(222,214,196)),
   new Layer("junctions","Junctions",Color.FromArgb(255,214,102)),
   new Layer("prop_spawn_zones","Props (poles, fences...)",Color.FromArgb(255,128,64)),
   new Layer("spawn_zones","Building spots",Color.FromArgb(120,170,255)),
   new Layer("loot_spawn_zones","Loot",Color.FromArgb(120,230,140)),
   new Layer("actor_spawn_zones","Enemy spawns",Color.FromArgb(255,90,100)),
   new Layer("dead_drop_spawn_zones","Dead drops",Color.FromArgb(200,140,255)),
   new Layer("other","Other points",Color.FromArgb(180,180,180)),
   new Layer("grid","Tile grid",Color.FromArgb(40,255,255,255)),
  };
  public static Layer Find(string key){foreach(var l in Layers)if(l.Key==key)return l;return Layers[Layers.Count-2];}

  static bool IsRoadLink(JsonObject o){return o.Has("pos_a")||o.Has("position_a");}
  static object Either(JsonObject o,string a,string b){return o.Has(a)?o[a]:o[b];}

  public static MapScene Build(MapPack pack){
   var scene=new MapScene();
   foreach(var l in Layers)l.Count=0;
   // Pass 1: gather point lists so each category's coordinate space can be detected.
   var raw=new List<Marker>();
   foreach(var tile in pack.Tiles.Values){
    foreach(var kv in tile.Data){
     var list=kv.Value as List<object>;if(list==null)continue;
     if(kv.Key=="junction_links"){
      foreach(var item in list){
       var o=item as JsonObject;if(o==null||!IsRoadLink(o))continue;
       Vec3 a,b,da,db;
       if(!Vec3.TryRead(Either(o,"pos_a","position_a"),out a)||!Vec3.TryRead(Either(o,"pos_b","position_b"),out b))continue;
       Vec3.TryRead(Either(o,"dir_a","direction_a"),out da);Vec3.TryRead(Either(o,"dir_b","direction_b"),out db);
       scene.Roads.Add(new Road{Tile=tile.Key,Type=o.Str("type")??"",Points=Bezier(a,b,da,db)});
      }
      continue;
     }
     foreach(var item in list){
      var o=item as JsonObject;if(o==null)continue;
      Vec3 p;if(!Vec3.TryRead(Either(o,"position","pos"),out p))continue;
      string layer=kv.Key=="junctions"||kv.Key=="pathway_junctions"?"junctions":Find(kv.Key).Key;
      var m=new Marker{Tile=tile.Key,Layer=layer,Position=p,Source=o,
       Label=o.Str("def")??o.Str("definition_id")??o.Str("definition_name")??o.Str("definition")??o.Str("filter")??o.Str("foundation")??o.Str("type")??kv.Key};
      foreach(var rk in new[]{"rot","rotation_y","rotation"})if(o.Has(rk)){m.RotationY=o.Num(rk,0);m.HasRotation=true;break;}
      raw.Add(m);
     }
    }
   }
   // Pass 2: per source key, decide world vs tile-local coordinates. Junctions are local
   // (validated by the road cache); for the rest, a category is local when most of its
   // points fall outside their own triangle as world coordinates.
   var votes=new Dictionary<string,int[]>();
   foreach(var m in raw){
    int[] v;if(!votes.TryGetValue(m.Layer,out v))votes[m.Layer]=v=new int[2];
    v[Grid.At(m.Position.X,m.Position.Z).Equals(m.Tile)?0:1]++;
   }
   foreach(var kv in votes)scene.LocalSpace[kv.Key]=kv.Key=="junctions"||kv.Value[1]>kv.Value[0]*4;
   foreach(var m in raw){
    if(scene.LocalSpace[m.Layer]){var t=pack.Get(m.Tile);m.Position=m.Position+t.Origin;}
    scene.Markers.Add(m);Find(m.Layer).Count++;
   }
   Find("roads").Count=scene.Roads.Count;Find("terrain").Count=pack.Tiles.Count;
   return scene;
  }

  // Same cubic as the game's road links (control points a+da and b+db).
  public static Vec3[] Bezier(Vec3 a,Vec3 b,Vec3 da,Vec3 db){
   double len=(b-a).Length+da.Length+db.Length;
   int n=Math.Max(2,Math.Min(64,(int)Math.Ceiling(len/16)));
   var pts=new Vec3[n+1];Vec3 c1=a+da,c2=b+db;
   for(int i=0;i<=n;i++){
    double t=(double)i/n,u=1-t;
    pts[i]=a*(u*u*u)+c1*(3*u*u*t)+c2*(3*u*t*t)+b*(t*t*t);
   }
   return pts;
  }
 }
}
