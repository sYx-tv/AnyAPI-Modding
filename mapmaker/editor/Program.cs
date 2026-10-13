using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Forms;
namespace AnyMapMaker {
 static class Program {
  [DllImport("user32.dll")]static extern bool SetProcessDPIAware();
  [STAThread]static int Main(string[] args){
   // Headless modes for builds and tests:
   //   --self-test <tiles folder or map pack>         load, save to a temp pack, reload, compare
   //   --render <tiles folder or map pack> <out.png> [width height]
   if(args.Length>=2&&args[0]=="--self-test")return SelfTest(args[1]);
   if(args.Length>=2&&args[0]=="--ui-shot"){
    MainForm.Headless=true;Application.EnableVisualStyles();var f=new MainForm{Size=new Size(1400,900)};f.Show();f.Demo();Application.DoEvents();
    for(int i=0;i<20;i++){Application.DoEvents();System.Threading.Thread.Sleep(50);}
    using(var bmp=new Bitmap(f.Width,f.Height))using(var g=Graphics.FromImage(bmp)){g.CopyFromScreen(f.Location,Point.Empty,f.Size);bmp.Save(args[1],ImageFormat.Png);}
    return 0;
   }
   if(args.Length>=1&&args[0]=="--edit-test")return EditTest(args.Length>=2?args[1]:null);
   if(args.Length>=3&&args[0]=="--render")return Render(args[1],args[2],args.Length>=5?int.Parse(args[3]):2048,args.Length>=5?int.Parse(args[4]):2048);
   try{SetProcessDPIAware();}catch{}
   Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);
   Application.ThreadException+=(s,e)=>MessageBox.Show("Something went wrong:\n\n"+e.Exception.Message,"Map Maker",MessageBoxButtons.OK,MessageBoxIcon.Warning);
   Application.Run(new MainForm());return 0;
  }

  static MapPack LoadAny(string path){
   if(File.Exists(Path.Combine(path,"map.json")))return MapPack.Open(path);
   if(File.Exists(Path.Combine(path,"game.exe")))return MapPack.LoadTiles(new GameInstall(path).StockTiles,"stock");
   return MapPack.LoadTiles(path,Path.GetFileName(path));
  }

  static int SelfTest(string path){
   try{
    var pack=LoadAny(path);
    var scene=MapScene.Build(pack);
    Console.WriteLine("Tiles: "+pack.Tiles.Count+" (x "+pack.MinX+".."+pack.MaxX+", z "+pack.MinZ+".."+pack.MaxZ+"), unreadable: "+pack.LoadErrors.Count);
    foreach(var e in pack.LoadErrors)Console.WriteLine("  "+e);
    Console.WriteLine("Roads: "+scene.Roads.Count+", markers: "+scene.Markers.Count);
    foreach(var l in MapScene.Layers)if(l.Count>0)Console.WriteLine("  "+l.Key+": "+l.Count+(scene.LocalSpace.ContainsKey(l.Key)?(scene.LocalSpace[l.Key]?" (tile-local)":" (world)"):""));
    // Every tile's position must be inside its own triangle, and every tile must survive a round trip.
    int misplaced=0;
    foreach(var t in pack.Tiles.Values){var o=t.Origin;if(!Grid.At(o.X,o.Z).Equals(t.Key))misplaced++;}
    string temp=Path.Combine(Path.GetTempPath(),"anymapmaker-selftest-"+Guid.NewGuid().ToString("N"));
    pack.Folder=null;foreach(var t in pack.Tiles.Values)t.Dirty=true;
    pack.Save(temp,"selftest");
    var again=MapPack.Open(temp);int different=0;
    foreach(var t in pack.Tiles.Values){var u=again.Get(t.Key);if(u==null||Json.Write(u.Data,false)!=Json.Write(t.Data,false))different++;}
    // Raw text check: parse(original file) -> write compact must equal parse(saved file) -> write compact.
    Directory.Delete(temp,true);
    Console.WriteLine("Origin check: "+(misplaced==0?"ok":misplaced+" tiles outside their triangle"));
    Console.WriteLine("Round trip: "+(different==0&&again.Tiles.Count==pack.Tiles.Count?"ok":different+" tiles changed, "+again.Tiles.Count+" reloaded"));
    return misplaced==0&&different==0&&pack.LoadErrors.Count==0?0:1;
   }catch(Exception e){Console.WriteLine("Self-test failed: "+e);return 2;}
  }

  // Exercises every editing tool on a blank island (or the given game's catalog) and checks undo and a save round trip.
  static int EditTest(string gameFolder){
   int fails=0;Action<bool,string> check=(ok,what)=>{Console.WriteLine((ok?"ok   ":"FAIL ")+what);if(!ok)fails++;};
   try{
    var game=gameFolder!=null&&new GameInstall(gameFolder).Valid?new GameInstall(gameFolder):null;
    var cat=Catalog.Load(game);
    var pack=MapEditor.BlankIsland(cat,10,10,4,4,1);var ed=new MapEditor(pack);
    check(pack.Tiles.Count==32,"blank island has 4x4 cells = 32 triangles ("+pack.Tiles.Count+")");
    check(pack.Get(new TileKey(9,9,1)).Definition==cat.ForTriangle("tile_flat_a",1),"upper triangles use the _in piece ("+pack.Get(new TileKey(9,9,1)).Definition+")");
    var c=Grid.Origin(new TileKey(9,9,0),1);
    ed.Begin();check(ed.AddProp("lamp_post_a",c.X,c.Z,0,0),"place a street light");ed.Commit();
    var t=pack.Get(Grid.At(c.X,c.Z));var prop=(JsonObject)t.Data.Arr("prop_spawn_zones")[0];Vec3 pp;Vec3.TryRead(prop["pos"],out pp);
    check(Math.Abs(pp.Y-32)<0.01,"street light sits on the ground at height 1 x 32 m ("+pp.Y+")");
    ed.Begin();int f=ed.AddFence(FenceStyle.All[0],c.X,c.Z,c.X+25.2,c.Z);ed.Commit();
    check(f==21,"a 25.2 m wooden fence is 11 posts + 10 panels ("+f+")");
    double rot=((JsonObject)pack.Get(Grid.At(c.X+1,c.Z)).Data.Arr("prop_spawn_zones")[1]).Num("rot",-1);
    check(Math.Abs(rot)<1e-6||Math.Abs(rot-2*Math.PI)<1e-6,"an eastward fence has rot 0 ("+rot+")");
    ed.Begin();int r=ed.AddRoad("road",new List<Vec3>{new Vec3(c.X-300,0,c.Z),new Vec3(c.X,0,c.Z+50),new Vec3(c.X+300,0,c.Z)});ed.Commit();
    check(r==2,"a three-point road is 2 links ("+r+")");
    ed.Begin();check(ed.AddBuildingSpot("house",2,c.X+60,c.Z+60,0),"place a house spot");check(ed.AddLoot(c.X+70,c.Z,0),"place loot");check(ed.AddEnemySpawn(c.X+80,c.Z),"place an enemy spawn");ed.Commit();
    var scene=MapScene.Build(pack);
    check(scene.Roads.Count==2&&MapScene.Find("prop_spawn_zones").Count==22,"the map view shows 2 roads and 22 props");
    ed.Begin();int er=ed.Erase(c.X+12,c.Z,3,true,true);ed.Commit();
    check(er>=2,"erase removes the fence pieces under a 3 m brush ("+er+")");
    check(ed.Undo()&&MapScene.Build(pack).Markers.Count==scene.Markers.Count,"undo brings them back");
    ed.Begin();ed.SetHeight(Grid.At(c.X,c.Z),3);ed.Commit();
    Vec3.TryRead(((JsonObject)pack.Get(Grid.At(c.X,c.Z)).Data.Arr("prop_spawn_zones")[0])["pos"],out pp);
    check(Math.Abs(pp.Y-96)<0.01,"raising a tile to height 3 lifts its street light to 96 m ("+pp.Y+")");
    ed.Begin();ed.PaintTile(new TileKey(20,20,1),cat,"tile_flat_a",null,null);ed.RemoveTile(new TileKey(9,9,1));ed.Commit();
    check(pack.Get(new TileKey(20,20,1))!=null&&pack.Get(new TileKey(9,9,1))==null,"paint new land and remove land");
    string temp=Path.Combine(Path.GetTempPath(),"anymapmaker-edittest-"+Guid.NewGuid().ToString("N"));
    pack.Save(temp,"test");var again=MapPack.Open(temp);
    check(again.Spawn==null,"a map without a start point keeps the game's own");
    pack.Spawn=new Vec3(c.X,2,c.Z);pack.Save(temp,"test");var withStart=MapPack.Open(temp);
    check(withStart.Spawn!=null&&Math.Abs(withStart.Spawn.Value.X-c.X)<0.01&&Math.Abs(withStart.Spawn.Value.Y-2)<0.01,"save and reload keeps the start point");
    check(again.StockVegetation.Count==0,"a blank island keeps no stock vegetation");
    var stockLike=MapEditor.BlankIsland(cat,30,30,2,2,0);foreach(var tt in stockLike.Tiles.Values)tt.TerrainAtLoad=tt.Terrain;foreach(var k in stockLike.Tiles.Keys)stockLike.StockVegetation.Add(k.BaseName);
    var ed2=new MapEditor(stockLike);ed2.Begin();ed2.SetHeight(new TileKey(29,29,0),2);ed2.Commit();ed2.Begin();ed2.Rotate(new TileKey(30,30,1));ed2.Commit();ed2.Undo();
    string temp2=Path.Combine(Path.GetTempPath(),"anymapmaker-edittest-"+Guid.NewGuid().ToString("N"));stockLike.Save(temp2,"test");
    var v=MapPack.Open(temp2).StockVegetation;Directory.Delete(temp2,true);
    check(v.Count==7&&!v.Contains("tile_029_029_0")&&v.Contains("tile_030_030_1"),"a changed tile drops its stock vegetation, an undone change keeps it ("+v.Count+" of 8 kept)");
    check(again.Tiles.Count==pack.Tiles.Count&&MapScene.Build(again).Roads.Count==MapScene.Build(pack).Roads.Count,"save and reload keeps every tile and road");
    Directory.Delete(temp,true);
   }catch(Exception e){Console.WriteLine("FAIL "+e);fails++;}
   return fails==0?0:1;
  }

  static int Render(string path,string output,int width,int height){
   try{
    var pack=LoadAny(path);var scene=MapScene.Build(pack);
    using(var view=new MapView{Width=width,Height=height,Font=new Font("Segoe UI",9f)})
    using(var bmp=new Bitmap(width,height))
    using(var g=Graphics.FromImage(bmp)){
     view.SetMap(pack,scene);view.Draw(g);bmp.Save(output,ImageFormat.Png);
    }
    Console.WriteLine("Rendered "+pack.Tiles.Count+" tiles to "+output);return 0;
   }catch(Exception e){Console.WriteLine("Render failed: "+e);return 2;}
  }
 }
}
