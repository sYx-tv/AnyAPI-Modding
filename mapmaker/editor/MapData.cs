using System;
using System.Collections.Generic;
using System.IO;
using System.Text.RegularExpressions;
namespace AnyMapMaker {
 public struct Vec3 {
  public double X,Y,Z;
  public Vec3(double x,double y,double z){X=x;Y=y;Z=z;}
  public static Vec3 operator+(Vec3 a,Vec3 b){return new Vec3(a.X+b.X,a.Y+b.Y,a.Z+b.Z);}
  public static Vec3 operator-(Vec3 a,Vec3 b){return new Vec3(a.X-b.X,a.Y-b.Y,a.Z-b.Z);}
  public static Vec3 operator*(Vec3 a,double s){return new Vec3(a.X*s,a.Y*s,a.Z*s);}
  public double Length {get{return Math.Sqrt(X*X+Y*Y+Z*Z);}}
  public static bool TryRead(object v,out Vec3 result){
   result=new Vec3();var a=v as List<object>;
   if(a==null||a.Count<3)return false;
   var x=a[0] as JsonNumber;var y=a[1] as JsonNumber;var z=a[2] as JsonNumber;
   if(x==null||y==null||z==null)return false;
   result=new Vec3(x.Value,y.Value,z.Value);return true;
  }
  public List<object> ToJson(){return new List<object>{new JsonNumber(X),new JsonNumber(Y),new JsonNumber(Z)};}
 }

 // The game's world is a grid of triangles. Cell (x,z) is the parallelogram spanned by
 // g_tile_grid_x_dir (512,0,0) and g_tile_grid_y_dir (256,0,256*sqrt3); t=0 is its lower-left
 // triangle and t=1 the upper-right one. Tile height is in 32 m steps (g_tile_height_increment).
 // These match native/generate_road_graph.py, which is validated against the shipped roads.
 public struct TileKey : IEquatable<TileKey> {
  public readonly int X,Z,T;
  public TileKey(int x,int z,int t){X=x;Z=z;T=t;}
  public bool Equals(TileKey o){return X==o.X&&Z==o.Z&&T==o.T;}
  public override bool Equals(object o){return o is TileKey&&Equals((TileKey)o);}
  public override int GetHashCode(){return (X*73856093)^(Z*19349663)^(T*83492791);}
  // The game pads x and z to three digits (tile_091_111_0.json).
  public string BaseName {get{return "tile_"+X.ToString("D3")+"_"+Z.ToString("D3")+"_"+T;}}
  public string FileName {get{return BaseName+".json";}}
  static readonly Regex Name=new Regex(@"^tile_(-?\d+)_(-?\d+)_(\d)\.json$",RegexOptions.IgnoreCase);
  public static bool TryParse(string fileName,out TileKey key){
   key=new TileKey();var m=Name.Match(fileName);if(!m.Success)return false;
   key=new TileKey(int.Parse(m.Groups[1].Value),int.Parse(m.Groups[2].Value),int.Parse(m.Groups[3].Value));return true;
  }
  public override string ToString(){return X+", "+Z+(T==0?" (lower)":" (upper)");}
 }

 public static class Grid {
  public const double Edge=512, Height=443.40500673763256, Step=32; // Height = 256*sqrt(3)
  // Where the game puts new players (server_scene.get_spawn_position, fixed in game.exe 0.1.25).
  public static readonly Vec3 SpawnCreative=new Vec3(83949,0,55851), SpawnSurvival=new Vec3(85611,0,49871);
  public static Vec3 Corner(int x,int z){return new Vec3(Edge*x+Edge/2*z,0,Height*z);}
  public static Vec3[] Corners(TileKey k){
   if(k.T==0)return new[]{Corner(k.X,k.Z),Corner(k.X+1,k.Z),Corner(k.X,k.Z+1)};
   return new[]{Corner(k.X+1,k.Z),Corner(k.X+1,k.Z+1),Corner(k.X,k.Z+1)};
  }
  public static Vec3 Origin(TileKey k,int height){
   return new Vec3(Edge*k.X+Edge/2*k.Z+(k.T==1?Edge:Edge/2),height*Step,Height*(k.Z+(k.T==1?2.0/3:1.0/3)));
  }
  // Which triangle a world point (x,z) is in.
  public static TileKey At(double x,double z){
   double gz=z/Height,gx=(x-Edge/2*gz)/Edge;
   int cx=(int)Math.Floor(gx),cz=(int)Math.Floor(gz);
   double fx=gx-cx,fz=gz-cz;
   return new TileKey(cx,cz,fx+fz<1?0:1);
  }
 }

 // One tile file. The JSON object is kept whole; the editor reads and writes known parts
 // and leaves everything else exactly as the game wrote it.
 public sealed class Tile {
  public TileKey Key; public JsonObject Data; public bool Dirty;
  public string TerrainAtLoad;          // definition|height|orientation when loaded, to spot terrain changes
  public string Terrain {get{return Definition+"|"+Height+"|"+Orientation;}}
  public string Definition {get{return Data.Str("definition_name")??"";}}
  public int Height {get{return (int)Data.Num("height",0);}}
  public int Orientation {get{return (int)Data.Num("orientation",0);}}
  public Vec3 Origin {get{return Grid.Origin(Key,Height);}}
 }

 public sealed class MapPack {
  public const int FormatVersion=1;
  public string Name="Untitled map", Author="", Description="", Base="stock";
  public string Folder;                 // null until saved
  public readonly Dictionary<TileKey,Tile> Tiles=new Dictionary<TileKey,Tile>();
  public readonly List<string> LoadErrors=new List<string>();
  // Tiles (base names like tile_088_122_0) whose stock vegetation (trees, bushes) still fits their terrain. The
  // CustomMap mod serves the game's vegetation only for these; any other tile has none unless the pack has its own.
  public readonly HashSet<string> StockVegetation=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
  public int MinX,MaxX,MinZ,MaxZ;

  public static MapPack LoadTiles(string tilesFolder,string name){
   var pack=new MapPack{Name=name};
   if(!Directory.Exists(tilesFolder))throw new DirectoryNotFoundException("No tiles folder at "+tilesFolder);
   foreach(var path in Directory.GetFiles(tilesFolder,"*.json")){
    TileKey key;if(!TileKey.TryParse(Path.GetFileName(path),out key))continue;
    try{
     var data=Json.Parse(File.ReadAllText(path)) as JsonObject;
     if(data==null){pack.LoadErrors.Add(Path.GetFileName(path)+": not a JSON object");continue;}
     var tile=new Tile{Key=key,Data=data};tile.TerrainAtLoad=tile.Terrain;pack.Tiles[key]=tile;
    }catch(Exception e){pack.LoadErrors.Add(Path.GetFileName(path)+": "+e.Message);}
   }
   pack.UpdateBounds();return pack;
  }

  // A map pack folder: map.json + tiles/0/*.json (see docs/development/map-format.md).
  public static MapPack Open(string folder){
   var manifest=Json.Parse(File.ReadAllText(Path.Combine(folder,"map.json"))) as JsonObject;
   if(manifest==null)throw new InvalidDataException("map.json is not a JSON object");
   var pack=LoadTiles(Path.Combine(folder,"tiles","0"),manifest.Str("name")??Path.GetFileName(folder));
   var keep=manifest.Arr("stock_vegetation");pack.StockVegetation.Clear();
   if(keep!=null)foreach(var v in keep){var n=v as string;if(n!=null)pack.StockVegetation.Add(n);}
   else foreach(var k in pack.Tiles.Keys)pack.StockVegetation.Add(k.BaseName);   // packs from before this list existed
   pack.Author=manifest.Str("author")??"";pack.Description=manifest.Str("description")??"";pack.Base=manifest.Str("base")??"stock";
   pack.Folder=folder;return pack;
  }

  public void Save(string folder,string gameBuild){
   string tiles=Path.Combine(folder,"tiles","0");Directory.CreateDirectory(tiles);
   var manifest=new JsonObject();
   manifest["format"]=new JsonNumber(FormatVersion.ToString());manifest["name"]=Name;manifest["author"]=Author;manifest["description"]=Description;
   manifest["base"]=Base;manifest["game_build"]=gameBuild??"";manifest["tiles"]=new JsonNumber(Tiles.Count.ToString());
   // Terrain changed (or land removed or added) since the stock world: its stock vegetation no longer fits.
   foreach(var tile in Tiles.Values)if(tile.TerrainAtLoad!=tile.Terrain)StockVegetation.Remove(tile.Key.BaseName);
   var alive=new HashSet<string>(StringComparer.OrdinalIgnoreCase);foreach(var k in Tiles.Keys)alive.Add(k.BaseName);
   StockVegetation.RemoveWhere(n=>!alive.Contains(n));
   var names=new List<string>(StockVegetation);names.Sort(StringComparer.OrdinalIgnoreCase);
   manifest["stock_vegetation"]=names.ConvertAll(n=>(object)n);
   File.WriteAllText(Path.Combine(folder,"map.json"),Json.Write(manifest,true));
   bool sameFolder=string.Equals(Path.GetFullPath(folder).TrimEnd('\\','/'),Folder==null?null:Path.GetFullPath(Folder).TrimEnd('\\','/'),StringComparison.OrdinalIgnoreCase);
   var keep=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
   foreach(var tile in Tiles.Values){
    keep.Add(tile.Key.FileName);
    if(sameFolder&&!tile.Dirty)continue;
    File.WriteAllText(Path.Combine(tiles,tile.Key.FileName),Json.Write(tile.Data,true));tile.Dirty=false;
   }
   // A pack's own vegetation file for a tile whose terrain changed would float or sink: drop it.
   string veg=Path.Combine(folder,"tiles","vegetation");
   if(Directory.Exists(veg))foreach(var path in Directory.GetFiles(veg,"tile_*")){
    string n=Path.GetFileName(path);int cut=n.IndexOf("_vegetation",StringComparison.OrdinalIgnoreCase);
    if(cut>0&&!StockVegetation.Contains(n.Substring(0,cut)))File.Delete(path);
   }
   // Tiles deleted in the editor (ocean) disappear from the pack too.
   foreach(var path in Directory.GetFiles(tiles,"tile_*.json"))if(!keep.Contains(Path.GetFileName(path)))File.Delete(path);
   Folder=folder;
  }

  public void UpdateBounds(){
   bool first=true;
   foreach(var k in Tiles.Keys){
    if(first){MinX=MaxX=k.X;MinZ=MaxZ=k.Z;first=false;continue;}
    MinX=Math.Min(MinX,k.X);MaxX=Math.Max(MaxX,k.X);MinZ=Math.Min(MinZ,k.Z);MaxZ=Math.Max(MaxZ,k.Z);
   }
  }
  public Tile Get(TileKey k){Tile t;return Tiles.TryGetValue(k,out t)?t:null;}
 }

 // The installed game. Read-only: the editor never writes into the game folder except
 // for map packs under "AnyAPI and Modding/maps".
 public sealed class GameInstall {
  public readonly string Folder;
  public GameInstall(string folder){Folder=folder;}
  public string Data {get{return Path.Combine(Folder,"rom","data");}}
  public string StockTiles {get{return Path.Combine(Data,"tiles","0");}}
  public string MapsFolder {get{return Path.Combine(Folder,"AnyAPI and Modding","maps");}}
  public bool Valid {get{return File.Exists(Path.Combine(Folder,"game.exe"))&&Directory.Exists(StockTiles);}}
  public string BuildId {get{
   try{
    string manifest=Path.GetFullPath(Path.Combine(Folder,"..","..","appmanifest_4435340.acf"));
    if(!File.Exists(manifest))return "";
    var m=Regex.Match(File.ReadAllText(manifest),"\"buildid\"\\s+\"(\\d+)\"");return m.Success?m.Groups[1].Value:"";
   }catch{return "";}
  }}
  public static string Guess(){
   foreach(var root in new[]{Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),@"C:\Program Files (x86)",@"D:\SteamLibrary",@"E:\SteamLibrary"}){
    if(string.IsNullOrEmpty(root))continue;
    foreach(var candidate in new[]{Path.Combine(root,"Steam","steamapps","common","Anymaker"),Path.Combine(root,"steamapps","common","Anymaker")})
     if(new GameInstall(candidate).Valid)return candidate;
   }
   return null;
  }
  // Ids from a definitions file, for palettes (props, junctions, road types).
  public List<string> DefinitionIds(string fileName){
   var ids=new List<string>();string path=Path.Combine(Data,fileName);
   if(!File.Exists(path))return ids;
   try{Collect(Json.Parse(File.ReadAllText(path)),ids,0);}catch{}
   return ids;
  }
  static void Collect(object v,List<string> ids,int depth){
   if(depth>4)return;
   var o=v as JsonObject;
   if(o!=null){
    string id=o.Str("id")??o.Str("name");
    if(id!=null&&depth>0){ids.Add(id);return;}
    foreach(var kv in o)Collect(kv.Value,ids,depth+1);return;
   }
   var a=v as List<object>;if(a!=null)foreach(var x in a)Collect(x,ids,depth+1);
  }
 }
}
