using System;
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
