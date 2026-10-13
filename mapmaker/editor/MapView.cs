using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;
namespace AnyMapMaker {
 // Top-down view of the world. +X is east (right), +Z is north (up). Drag with the right or
 // middle mouse button to pan, scroll to zoom, left click to select a tile.
 public enum Tool {Select,Terrain,Height,Road,Fence,Prop,Building,Loot,Enemy,Erase}

 public sealed class MapView : Control {
  public MapPack Pack; public MapScene Scene;
  public TileKey? Hovered, Selected;
  public event Action<TileKey?> SelectionChanged;
  public event Action<string> StatusChanged;
  // Editing. The form does the edit; the view reports where. Positions are world metres (x, z).
  public Tool Tool=Tool.Select;
  public double EraseRadius=8;
  public event Action<TileKey,MouseButtons,Keys> TilePainted;          // terrain / height: once per triangle per stroke
  public event Action<double,double,double?> Placed;                     // point tools: x, z, rotation from a drag (null = none)
  public event Action<double,double,double,double> LineDrawn;            // fence: from, to
  public event Action<List<Vec3>> RoadFinished;                           // road: clicked points (Enter / double click)
  public event Action<double,double> Erased;                             // erase: centre (drag repeats)
  public event Action StrokeEnded;
  public readonly List<Vec3> RoadPoints=new List<Vec3>();
  bool stroking; Point strokeStart; double strokeX,strokeZ; Point mouse;
  readonly HashSet<TileKey> strokeTiles=new HashSet<TileKey>();
  double centerX, centerZ, scale=0.02; // pixels per metre
  Point dragStart; double dragCX, dragCZ; bool dragging;
  int minHeight, maxHeight;

  public MapView(){
   SetStyle(ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer|ControlStyles.UserPaint|ControlStyles.ResizeRedraw|ControlStyles.Selectable,true);
   BackColor=Color.FromArgb(22,48,74);
  }

  public void SetMap(MapPack pack,MapScene scene){
   Pack=pack;Scene=scene;Hovered=Selected=null;
   minHeight=int.MaxValue;maxHeight=int.MinValue;
   foreach(var t in pack.Tiles.Values){minHeight=Math.Min(minHeight,t.Height);maxHeight=Math.Max(maxHeight,t.Height);}
   if(pack.Tiles.Count==0){minHeight=0;maxHeight=1;}
   FitAll();
  }
  public void ShowScene(MapScene scene){Scene=scene;Invalidate();}

  public void FitAll(){
   if(Pack==null||Pack.Tiles.Count==0){centerX=centerZ=0;scale=0.02;Invalidate();return;}
   var a=Grid.Corner(Pack.MinX,Pack.MinZ);var b=Grid.Corner(Pack.MaxX+1,Pack.MaxZ+1);
   double minX=Math.Min(Grid.Corner(Pack.MinX,Pack.MaxZ+1).X,a.X),maxX=Math.Max(Grid.Corner(Pack.MaxX+1,Pack.MinZ).X,b.X);
   centerX=(minX+maxX)/2;centerZ=(a.Z+b.Z)/2;
   scale=Math.Min(Math.Max(1,Width-40)/(maxX-minX),Math.Max(1,Height-40)/Math.Max(1,b.Z-a.Z));
   Invalidate();
  }

  public PointF ToScreen(double x,double z){return new PointF((float)((x-centerX)*scale+Width/2.0),(float)(-(z-centerZ)*scale+Height/2.0));}
  public void ToWorld(Point p,out double x,out double z){x=(p.X-Width/2.0)/scale+centerX;z=-(p.Y-Height/2.0)/scale+centerZ;}

  Color TerrainColor(Tile t){
   string d=t.Definition.ToLowerInvariant();
   if(d.Contains("ocean")||d.Contains("water")||d.Contains("sea"))return Color.FromArgb(38,86,128);
   double f=maxHeight>minHeight?(double)(t.Height-minHeight)/(maxHeight-minHeight):0.5;
   // Lowland green -> hills olive -> rock -> snow.
   Color[] stops={Color.FromArgb(198,190,140),Color.FromArgb(86,140,74),Color.FromArgb(120,132,70),Color.FromArgb(128,112,92),Color.FromArgb(236,238,242)};
   double s=f*(stops.Length-1);int i=Math.Min(stops.Length-2,(int)s);double r=s-i;
   Color c=Color.FromArgb((int)(stops[i].R+(stops[i+1].R-stops[i].R)*r),(int)(stops[i].G+(stops[i+1].G-stops[i].G)*r),(int)(stops[i].B+(stops[i+1].B-stops[i].B)*r));
   // Light shading from the orientation so neighbouring pieces read apart.
   int shade=(t.Orientation%3)*6-6;
   return Color.FromArgb(Clamp(c.R+shade),Clamp(c.G+shade),Clamp(c.B+shade));
  }
  static int Clamp(int v){return v<0?0:v>255?255:v;}

  public void Draw(Graphics g){
   g.Clear(BackColor);
   if(Pack==null){
    TextRenderer.DrawText(g,"Open the stock world or a map pack from the File menu.",Font,ClientRectangle,Color.FromArgb(200,210,225),TextFormatFlags.HorizontalCenter|TextFormatFlags.VerticalCenter);
    return;
   }
   g.SmoothingMode=scale>0.05?SmoothingMode.AntiAlias:SmoothingMode.None;
   double x0,z0,x1,z1;ToWorld(new Point(0,Height),out x0,out z0);ToWorld(new Point(Width,0),out x1,out z1);
   var terrain=MapScene.Find("terrain");var grid=MapScene.Find("grid");
   var pts=new PointF[3];
   using(var gridPen=new Pen(grid.Color,1)){
    foreach(var t in Pack.Tiles.Values){
     var c=Grid.Corners(t.Key);
     if(c[0].Z>z1+500||c[2].Z<z0-500)continue;
     double mx=Math.Min(c[0].X,c[2].X),xx=Math.Max(c[1].X,c[2].X);
     if(xx<x0-600||mx>x1+600)continue;
     for(int i=0;i<3;i++)pts[i]=ToScreen(c[i].X,c[i].Z);
     if(terrain.Visible)using(var b=new SolidBrush(TerrainColor(t)))g.FillPolygon(b,pts);
     if(grid.Visible&&scale>0.03)g.DrawPolygon(gridPen,pts);
    }
   }
   if(MapScene.Find("roads").Visible&&Scene!=null){
    float w=(float)Math.Max(1.5,8*scale);
    using(var casing=new Pen(Color.FromArgb(60,52,44),w+2){LineJoin=LineJoin.Round})
    using(var road=new Pen(MapScene.Find("roads").Color,w){LineJoin=LineJoin.Round})
    using(var path=new Pen(Color.FromArgb(176,150,110),Math.Max(1f,w*0.5f)){DashStyle=DashStyle.Dash}){
     foreach(var r in Scene.Roads){
      var sp=new PointF[r.Points.Length];for(int i=0;i<sp.Length;i++)sp[i]=ToScreen(r.Points[i].X,r.Points[i].Z);
      if(!OnScreen(sp))continue;
      bool isRoad=r.Type.StartsWith("road",StringComparison.OrdinalIgnoreCase);
      if(isRoad){g.DrawLines(casing,sp);g.DrawLines(road,sp);}else g.DrawLines(path,sp);
     }
    }
   }
   if(Scene!=null){
    float size=(float)Math.Max(3,Math.Min(10,6*scale*10));
    foreach(var m in Scene.Markers){
     var layer=MapScene.Find(m.Layer);if(!layer.Visible)continue;
     var p=ToScreen(m.Position.X,m.Position.Z);
     if(p.X<-10||p.Y<-10||p.X>Width+10||p.Y>Height+10)continue;
     using(var b=new SolidBrush(layer.Color)){
      if(m.Layer=="spawn_zones"){
       double sz=Math.Max(6,m.Source.Num("size",1)*8*scale);float hs=(float)Math.Min(40,sz/2);
       var st=g.Save();g.TranslateTransform(p.X,p.Y);g.RotateTransform((float)(-m.RotationY*180/Math.PI));
       using(var pen=new Pen(layer.Color,1.5f))g.DrawRectangle(pen,-hs,-hs,hs*2,hs*2);
       g.Restore(st);
      }else if(m.Layer=="junctions")g.FillEllipse(b,p.X-size*0.7f,p.Y-size*0.7f,size*1.4f,size*1.4f);
      else g.FillRectangle(b,p.X-size/2,p.Y-size/2,size,size);
     }
     if(scale>0.6&&m.Layer!="junctions")TextRenderer.DrawText(g,m.Label,Font,new Point((int)p.X+6,(int)p.Y-6),Color.FromArgb(230,235,243));
    }
   }
   DrawPreview(g);
   DrawOutline(g,Hovered,Color.FromArgb(160,255,255,255),1);
   DrawOutline(g,Selected,Color.FromArgb(255,214,102),2.5f);
   DrawScaleBar(g);
  }
  void DrawPreview(Graphics g){
   using(var pen=new Pen(Color.FromArgb(255,214,102),2){DashStyle=DashStyle.Dash}){
    if(Tool==Tool.Road&&RoadPoints.Count>0){
     var pts=new List<PointF>();foreach(var p in RoadPoints)pts.Add(ToScreen(p.X,p.Z));pts.Add(mouse);
     if(pts.Count>1)g.DrawLines(pen,pts.ToArray());
     foreach(var p in pts)g.FillEllipse(Brushes.Gold,p.X-3,p.Y-3,6,6);
    }
    if(stroking&&(Tool==Tool.Fence||Tool==Tool.Prop||Tool==Tool.Building||Tool==Tool.Loot)){
     var a=ToScreen(strokeX,strokeZ);g.DrawLine(pen,a,mouse);
    }
   }
   if(Tool==Tool.Erase){
    float r=(float)Math.Max(3,EraseRadius*scale);
    using(var pen=new Pen(Color.FromArgb(255,90,100),1.5f))g.DrawEllipse(pen,mouse.X-r,mouse.Y-r,r*2,r*2);
   }
  }
  public void ClearRoad(){RoadPoints.Clear();Invalidate();}
  public void FinishRoad(){if(RoadPoints.Count>=2&&RoadFinished!=null)RoadFinished(new List<Vec3>(RoadPoints));RoadPoints.Clear();Invalidate();}
  void PaintAt(Point p,MouseButtons b){
   double x,z;ToWorld(p,out x,out z);var k=Grid.At(x,z);
   if(strokeTiles.Add(k)&&TilePainted!=null)TilePainted(k,b,ModifierKeys);
  }
  bool OnScreen(PointF[] sp){foreach(var p in sp)if(p.X>-50&&p.Y>-50&&p.X<Width+50&&p.Y<Height+50)return true;return false;}
  void DrawOutline(Graphics g,TileKey? key,Color color,float width){
   if(key==null)return;var c=Grid.Corners(key.Value);
   var pts=new PointF[3];for(int i=0;i<3;i++)pts[i]=ToScreen(c[i].X,c[i].Z);
   using(var pen=new Pen(color,width))g.DrawPolygon(pen,pts);
  }
  void DrawScaleBar(Graphics g){
   double metres=Math.Pow(10,Math.Floor(Math.Log10(150/scale)));
   foreach(var m in new[]{5.0,2.0})if(metres*m*scale<=150){metres*=m;break;}
   float len=(float)(metres*scale);int y=Height-24;
   using(var pen=new Pen(Color.White,2)){g.DrawLine(pen,16,y,16+len,y);g.DrawLine(pen,16,y-5,16,y+5);g.DrawLine(pen,16+len,y-5,16+len,y+5);}
   TextRenderer.DrawText(g,metres>=1000?(metres/1000)+" km":metres+" m",Font,new Point(20+(int)len,y-9),Color.White);
  }

  protected override void OnPaint(PaintEventArgs e){Draw(e.Graphics);}
  protected override void OnMouseDown(MouseEventArgs e){
   Focus();
   if(e.Button==MouseButtons.Right||e.Button==MouseButtons.Middle){dragging=true;dragStart=e.Location;dragCX=centerX;dragCZ=centerZ;Cursor=Cursors.SizeAll;}
   else if(e.Button==MouseButtons.Left&&Pack!=null){
    double x,z;ToWorld(e.Location,out x,out z);var k=Grid.At(x,z);
    if(Tool==Tool.Select||Tool==Tool.Terrain||Tool==Tool.Height){
     Selected=Pack.Get(k)!=null?(TileKey?)k:null;Invalidate();
     if(SelectionChanged!=null)SelectionChanged(Selected);
    }
    stroking=true;strokeStart=e.Location;strokeX=x;strokeZ=z;strokeTiles.Clear();
    if(Tool==Tool.Terrain||Tool==Tool.Height)PaintAt(e.Location,e.Button);
    else if(Tool==Tool.Erase&&Erased!=null)Erased(x,z);
    else if(Tool==Tool.Road){RoadPoints.Add(new Vec3(x,0,z));Invalidate();}
   }
  }
  protected override void OnMouseDoubleClick(MouseEventArgs e){if(Tool==Tool.Road&&e.Button==MouseButtons.Left)FinishRoad();}
  protected override bool IsInputKey(Keys k){return k==Keys.Enter||k==Keys.Escape||k==Keys.Back||base.IsInputKey(k);}
  protected override void OnKeyDown(KeyEventArgs e){
   if(Tool!=Tool.Road)return;
   if(e.KeyCode==Keys.Enter)FinishRoad();
   else if(e.KeyCode==Keys.Escape)ClearRoad();
   else if(e.KeyCode==Keys.Back&&RoadPoints.Count>0){RoadPoints.RemoveAt(RoadPoints.Count-1);Invalidate();}
  }
  protected override void OnMouseUp(MouseEventArgs e){
   dragging=false;Cursor=Cursors.Default;
   if(!stroking||e.Button!=MouseButtons.Left)return;
   stroking=false;
   double x,z;ToWorld(e.Location,out x,out z);
   bool moved=Math.Abs(e.X-strokeStart.X)+Math.Abs(e.Y-strokeStart.Y)>6;
   if(Tool==Tool.Fence&&moved&&LineDrawn!=null)LineDrawn(strokeX,strokeZ,x,z);
   else if((Tool==Tool.Prop||Tool==Tool.Building||Tool==Tool.Loot||Tool==Tool.Enemy)&&Placed!=null)
    Placed(strokeX,strokeZ,moved?(double?)MapEditor.Angle(x-strokeX,z-strokeZ):null);
   if(StrokeEnded!=null)StrokeEnded();
   Invalidate();
  }
  protected override void OnMouseMove(MouseEventArgs e){
   mouse=e.Location;
   if(dragging){centerX=dragCX-(e.X-dragStart.X)/scale;centerZ=dragCZ+(e.Y-dragStart.Y)/scale;Invalidate();return;}
   if(stroking&&(Tool==Tool.Terrain||Tool==Tool.Height))PaintAt(e.Location,MouseButtons.Left);
   if(stroking&&Tool==Tool.Erase&&Erased!=null){double ex,ez;ToWorld(e.Location,out ex,out ez);Erased(ex,ez);}
   if(Tool!=Tool.Select)Invalidate();
   if(Pack==null)return;
   double x,z;ToWorld(e.Location,out x,out z);var k=Grid.At(x,z);
   if(Hovered==null||!Hovered.Value.Equals(k)){Hovered=k;Invalidate();}
   if(StatusChanged!=null){
    var t=Pack.Get(k);
    StatusChanged(string.Format("x {0:0} m, z {1:0} m   tile {2}   {3}",x,z,k,t==null?"ocean (no tile)":t.Definition+", height "+t.Height+" ("+t.Height*32+" m), orientation "+t.Orientation));
   }
  }
  protected override void OnMouseWheel(MouseEventArgs e){
   double x,z;ToWorld(e.Location,out x,out z);
   scale*=e.Delta>0?1.25:0.8;scale=Math.Max(0.002,Math.Min(20,scale));
   // Keep the point under the cursor fixed.
   centerX=x-(e.X-Width/2.0)/scale;centerZ=z+(e.Y-Height/2.0)/scale;Invalidate();
  }
 }
}
