using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;
using AnyApiManager;
namespace AnyMapMaker {
 // 3D view of the map: orbit, pan and zoom around it, and drag road and point nodes. Drawn with GDI+ (painter's
 // algorithm, far to near), so it needs nothing beyond what ships with Windows. Terrain pieces are drawn as flat
 // triangles at their height step with cliff walls between steps; the game's own piece shapes (hills, river beds)
 // are not in the tile files, so slopes inside a piece don't show.
 public sealed class MapView3D : Control {
  public MapPack Pack; public MapScene Scene; public MapEditor Editor;
  public double Falloff=120;           // proportional-editing radius for road nodes, metres
  public event Action<string> StatusChanged;
  public event Action NodesMoved, DragFinished;
  // Camera: orbits a target point.
  double tx,ty,tz,yaw=0.6,pitch=0.75,dist=3000;const double Fov=50*Math.PI/180;
  Vec3 eye,fwd,right,up;double focal;
  List<MapNode> nodes=new List<MapNode>();readonly List<KeyValuePair<MapNode,PointF>> shown=new List<KeyValuePair<MapNode,PointF>>();
  MapNode hover;MapEditor.NodeDrag drag;bool vertical;Vec3 grabHit;Point grabMouse;double grabDepth;
  Point last;bool orbiting,panning;

  public MapView3D(){SetStyle(ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer|ControlStyles.UserPaint|ControlStyles.ResizeRedraw|ControlStyles.Selectable,true);BackColor=Color.FromArgb(22,48,74);}

  public void SetMap(MapPack pack,MapScene scene,MapEditor editor){Pack=pack;Scene=scene;Editor=editor;Reload();FitAll();}
  public void ShowScene(MapScene scene,MapEditor editor){Scene=scene;Editor=editor;if(drag==null)Reload();Invalidate();}
  void Reload(){nodes=Editor==null?new List<MapNode>():Editor.Nodes(MapScene.Find("roads").Visible,true);hover=null;}
  public void FitAll(){
   if(Pack==null||Pack.Tiles.Count==0){tx=tz=0;ty=0;dist=3000;Invalidate();return;}
   var a=Grid.Corner(Pack.MinX,Pack.MinZ);var b=Grid.Corner(Pack.MaxX+1,Pack.MaxZ+1);
   tx=(a.X+b.X)/2;tz=(a.Z+b.Z)/2;ty=0;double size=Math.Max(Math.Abs(b.X-a.X),Math.Abs(b.Z-a.Z));
   dist=Math.Max(400,Math.Min(120000,size*1.1));Invalidate();
  }
  public void LookAt(Vec3 p,double distance){tx=p.X;ty=p.Y;tz=p.Z;dist=distance;Invalidate();}

  // ---------------------------------------------------------------- camera maths
  void Camera(){
   double cp=Math.Cos(pitch),sp=Math.Sin(pitch);
   fwd=new Vec3(cp*Math.Sin(yaw),-sp,cp*Math.Cos(yaw));
   eye=new Vec3(tx,ty,tz)-fwd*dist;
   right=Norm(Cross(new Vec3(0,1,0),fwd));up=Cross(fwd,right);
   focal=Height/2.0/Math.Tan(Fov/2);
  }
  static Vec3 Cross(Vec3 a,Vec3 b){return new Vec3(a.Y*b.Z-a.Z*b.Y,a.Z*b.X-a.X*b.Z,a.X*b.Y-a.Y*b.X);}
  static double Dot(Vec3 a,Vec3 b){return a.X*b.X+a.Y*b.Y+a.Z*b.Z;}
  static Vec3 Norm(Vec3 v){double l=v.Length;return l>0?v*(1/l):v;}
  bool Project(Vec3 p,out PointF s,out double depth){
   var v=p-eye;depth=Dot(v,fwd);s=PointF.Empty;if(depth<1)return false;
   s=new PointF((float)(Width/2.0+Dot(v,right)*focal/depth),(float)(Height/2.0-Dot(v,up)*focal/depth));return true;
  }
  Vec3 Ray(Point p){return Norm(fwd+right*((p.X-Width/2.0)/focal)+up*(-(p.Y-Height/2.0)/focal));}
  bool HitPlane(Point p,double y,out Vec3 hit){var d=Ray(p);hit=new Vec3();if(Math.Abs(d.Y)<1e-6)return false;double t=(y-eye.Y)/d.Y;if(t<=0)return false;hit=eye+d*t;return true;}

  // ---------------------------------------------------------------- drawing
  struct Poly{public PointF[] P;public double Depth;public Color C;}
  static Color Ground(Tile t){
   string d=t.Definition;int h=t.Height;
   if(d.StartsWith("shallow"))return Color.FromArgb(196,184,138);
   if(d.Contains("river")||d.Contains("lake"))return Color.FromArgb(70,120,110);
   if(d.Contains("cement")||d.Contains("cargo")||d.Contains("airfield")||d.Contains("trainyard"))return Color.FromArgb(132,134,136);
   if(h<=0)return Color.FromArgb(190,178,130);
   if(h<=3)return Color.FromArgb(92,140,74);
   if(h<=7)return Color.FromArgb(118,128,72);
   if(h<=11)return Color.FromArgb(124,112,96);
   return Color.FromArgb(232,236,242);
  }
  static Color Shade(Color c,double f){return Color.FromArgb(Clamp(c.R*f),Clamp(c.G*f),Clamp(c.B*f));}
  static Color Blend(Color a,Color b,double t){return Color.FromArgb(Clamp(a.R+(b.R-a.R)*t),Clamp(a.G+(b.G-a.G)*t),Clamp(a.B+(b.B-a.B)*t));}
  static int Clamp(double v){return v<0?0:v>255?255:(int)v;}
  static readonly Color Sea=Color.FromArgb(30,78,116),SkyTop=Color.FromArgb(14,22,36),SkyLow=Color.FromArgb(52,74,104);

  protected override void OnPaint(PaintEventArgs e){
   var g=e.Graphics;Camera();
   // Sky above the horizon, sea below it (sea level is y = 0).
   var flat=Norm(new Vec3(fwd.X,0,fwd.Z));PointF hz;double hd;float horizon=Project(new Vec3(eye.X,0,eye.Z)+flat*1e7,out hz,out hd)?hz.Y:-1;
   if(eye.Y<0)horizon=Height+1;
   if(horizon>0)using(var sky=new LinearGradientBrush(new Rectangle(0,0,Width,Math.Max(1,(int)horizon+1)),SkyTop,SkyLow,90f))g.FillRectangle(sky,0,0,Width,horizon);
   using(var sea=new SolidBrush(Sea))g.FillRectangle(sea,0,Math.Max(0,horizon),Width,Height);
   if(Pack==null){TextRenderer.DrawText(g,"Open a map to see it in 3D.",Font,ClientRectangle,Theme.Muted,TextFormatFlags.HorizontalCenter|TextFormatFlags.VerticalCenter);return;}

   var polys=new List<Poly>();bool terrain=MapScene.Find("terrain").Visible;
   if(terrain){
    var edges=new Dictionary<string,KeyValuePair<Tile,int>>();
    foreach(var t in Pack.Tiles.Values){
     var c=Grid.Corners(t.Key);double y=t.Height*Grid.Step;
     for(int i=0;i<3;i++)c[i].Y=y;
     AddPoly(polys,c,TileColor(t));
     // Cliff walls: between this tile and its neighbour, or down to the sea floor at the coast.
     for(int i=0;i<3;i++){
      Vec3 a=c[i],b=c[(i+1)%3];string k=EdgeKey(a,b);KeyValuePair<Tile,int> other;
      if(edges.TryGetValue(k,out other)){edges.Remove(k);Wall(polys,a,b,y,other.Key.Height*Grid.Step);}
      else edges[k]=new KeyValuePair<Tile,int>(t,i);
     }
    }
    foreach(var kv in edges.Values){var t=kv.Key;var c=Grid.Corners(t.Key);double y=t.Height*Grid.Step;Wall(polys,c[kv.Value],c[(kv.Value+1)%3],y,-60);}
   }
   polys.Sort((a,b)=>b.Depth.CompareTo(a.Depth));
   g.SmoothingMode=SmoothingMode.HighSpeed;
   foreach(var p in polys)using(var b=new SolidBrush(p.C))using(var pen=new Pen(p.C)){g.FillPolygon(b,p.P);g.DrawPolygon(pen,p.P);}
   g.SmoothingMode=SmoothingMode.AntiAlias;

   if(MapScene.Find("grid").Visible&&terrain&&dist<20000)using(var pen=new Pen(Color.FromArgb(28,255,255,255)))foreach(var t in Pack.Tiles.Values){
    var c=Grid.Corners(t.Key);var s=new PointF[3];bool ok=true;double d;for(int i=0;i<3&&ok;i++){c[i].Y=t.Height*Grid.Step+0.2;ok=Project(c[i],out s[i],out d);}
    if(ok)g.DrawPolygon(pen,s);
   }
   if(Scene!=null&&MapScene.Find("roads").Visible)foreach(var r in Scene.Roads){
    var pts=new List<PointF>();double depth=0;int n=0;
    foreach(var p in r.Points){PointF s;double d;if(!Project(p+new Vec3(0,0.3,0),out s,out d)){if(pts.Count>1)DrawRoad(g,r,pts,depth/n);pts.Clear();depth=0;n=0;continue;}pts.Add(s);depth+=d;n++;}
    if(pts.Count>1)DrawRoad(g,r,pts,depth/n);
   }
   if(Scene!=null)foreach(var m in Scene.Markers){
    var layer=MapScene.Find(m.Layer);if(!layer.Visible||m.Layer=="junctions")continue;
    PointF s;double d;if(!Project(m.Position,out s,out d))continue;float r=(float)Math.Max(2,Math.Min(7,2.5*focal/d));
    PointF top;double d2;if(Project(m.Position+new Vec3(0,6,0),out top,out d2))using(var pen=new Pen(Color.FromArgb(160,layer.Color)))g.DrawLine(pen,s,top);
    using(var b=new SolidBrush(layer.Color))g.FillEllipse(b,s.X-r,s.Y-r,r*2,r*2);
   }
   DrawStart(g);
   // Nodes on top.
   shown.Clear();
   foreach(var n in nodes){
    if(!n.IsRoad&&!MapScene.Find(n.ListKey).Visible)continue;
    PointF s;double d;if(!Project(n.Position+new Vec3(0,n.IsRoad?0.3:0,0),out s,out d)||s.X<-20||s.Y<-20||s.X>Width+20||s.Y>Height+20)continue;
    if(d>Math.Max(4000,dist*2.5))continue;
    shown.Add(new KeyValuePair<MapNode,PointF>(n,s));
    bool hot=n==hover||(drag!=null&&n==drag.Node);float r=hot?6:n.IsRoad?3.5f:0;if(r==0)continue;
    using(var b=new SolidBrush(hot?Theme.Blue:Color.FromArgb(235,240,248)))g.FillEllipse(b,s.X-r,s.Y-r,r*2,r*2);
    if(hot)using(var pen=new Pen(Color.White,1.5f))g.DrawEllipse(pen,s.X-r,s.Y-r,r*2,r*2);
   }
   if(drag!=null&&drag.Node.IsRoad&&Falloff>0){
    // Show the falloff circle on the ground around the dragged node.
    var c=drag.Node.Position;var ring=new List<PointF>();double d;
    for(int i=0;i<=48;i++){double a=i*Math.PI/24;PointF s;if(Project(new Vec3(c.X+Math.Cos(a)*Falloff,c.Y,c.Z+Math.Sin(a)*Falloff),out s,out d))ring.Add(s);}
    if(ring.Count>2)using(var pen=new Pen(Color.FromArgb(150,Theme.Blue),1.5f){DashStyle=DashStyle.Dash})g.DrawLines(pen,ring.ToArray());
   }
   DrawCompass(g);
  }
  Color TileColor(Tile t){
   var c=Ground(t);
   // Light from the north-west; shade the three orientations slightly so neighbouring pieces read apart.
   c=Shade(c,0.92+(t.Orientation%3)*0.04);
   if(t.Height<0)c=Blend(c,Sea,Math.Min(0.8,-t.Height*0.18));
   return c;
  }
  static string EdgeKey(Vec3 a,Vec3 b){string ka=Math.Round(a.X)+":"+Math.Round(a.Z),kb=Math.Round(b.X)+":"+Math.Round(b.Z);return string.CompareOrdinal(ka,kb)<0?ka+"|"+kb:kb+"|"+ka;}
  void AddPoly(List<Poly> polys,Vec3[] pts,Color c){
   var s=new PointF[pts.Length];double sum=0;
   for(int i=0;i<pts.Length;i++){double d;if(!Project(pts[i],out s[i],out d))return;sum+=d;}
   double minX=double.MaxValue,maxX=double.MinValue,minY=double.MaxValue,maxY=double.MinValue;
   foreach(var p in s){minX=Math.Min(minX,p.X);maxX=Math.Max(maxX,p.X);minY=Math.Min(minY,p.Y);maxY=Math.Max(maxY,p.Y);}
   if(maxX<0||maxY<0||minX>Width||minY>Height)return;
   polys.Add(new Poly{P=s,Depth=sum/pts.Length,C=c});
  }
  void Wall(List<Poly> polys,Vec3 a,Vec3 b,double y1,double y2){
   if(Math.Abs(y1-y2)<0.5)return;double hi=Math.Max(y1,y2),lo=Math.Min(y1,y2);
   var quad=new[]{new Vec3(a.X,hi,a.Z),new Vec3(b.X,hi,b.Z),new Vec3(b.X,lo,b.Z),new Vec3(a.X,lo,a.Z)};
   // Walls facing the light are brighter.
   var n=Norm(new Vec3(-(b.Z-a.Z),0,b.X-a.X));double lit=0.55+0.25*Math.Abs(n.X*0.6-n.Z*0.8);
   var c=Shade(Color.FromArgb(120,108,92),lit);if(hi<=0)c=Blend(c,Sea,0.6);
   AddPoly(polys,quad,c);
  }
  void DrawRoad(Graphics g,Road r,List<PointF> pts,double depth){
   bool train=r.Type.StartsWith("train");double metres=train?3.5:r.Type=="road_c"?5:8;
   float w=(float)Math.Max(1.2,Math.Min(60,metres*focal/Math.Max(1,depth)));
   using(var edge=new Pen(train?Color.FromArgb(70,58,48):Color.FromArgb(40,42,46),w+2){LineJoin=LineJoin.Round,StartCap=LineCap.Round,EndCap=LineCap.Round})g.DrawLines(edge,pts.ToArray());
   using(var top=new Pen(train?Color.FromArgb(150,128,100):Color.FromArgb(96,98,104),w){LineJoin=LineJoin.Round,StartCap=LineCap.Round,EndCap=LineCap.Round})g.DrawLines(top,pts.ToArray());
   if(!train&&w>6)using(var line=new Pen(Color.FromArgb(200,214,200,150),Math.Max(1,w/14)){DashStyle=DashStyle.Dash})g.DrawLines(line,pts.ToArray());
  }
  void DrawStart(Graphics g){
   var list=new List<KeyValuePair<Vec3,string>>();
   if(Pack.Spawn!=null)list.Add(new KeyValuePair<Vec3,string>(Pack.Spawn.Value,"Start"));
   else{list.Add(new KeyValuePair<Vec3,string>(Grid.SpawnCreative,"Start: Creative / Sandbox"));list.Add(new KeyValuePair<Vec3,string>(Grid.SpawnSurvival,"Start: Survival"));}
   foreach(var kv in list){
    var p=kv.Key;var t=Pack.Get(Grid.At(p.X,p.Z));double y=Pack.Spawn!=null?p.Y:t==null?0:t.Height*Grid.Step;
    PointF s,top;double d;if(!Project(new Vec3(p.X,y,p.Z),out s,out d)||!Project(new Vec3(p.X,y+40,p.Z),out top,out d))continue;
    var c=t==null?Theme.Bad:Color.White;
    using(var pen=new Pen(c,2f)){g.DrawLine(pen,s,top);g.DrawEllipse(pen,s.X-6,s.Y-6,12,12);}
    TextRenderer.DrawText(g,kv.Value+(t==null?" (in the sea!)":""),Font,new Point((int)top.X+6,(int)top.Y-8),c);
   }
  }
  void DrawCompass(Graphics g){
   // North arrow in the corner, turning with the camera.
   int cx=Width-Theme.S(34),cy=Theme.S(34),r=Theme.S(18);var n=new Vec3(0,0,1);
   double x=Dot(n,right),y=Dot(Norm(new Vec3(fwd.X,0,fwd.Z)),n);float ax=(float)(cx+x*r),ay=(float)(cy-y*r);
   using(var b=new SolidBrush(Color.FromArgb(150,Theme.Panel)))g.FillEllipse(b,cx-r-4,cy-r-4,(r+4)*2,(r+4)*2);
   using(var pen=new Pen(Theme.Bad,2.5f){EndCap=LineCap.ArrowAnchor})g.DrawLine(pen,cx,cy,ax,ay);
   TextRenderer.DrawText(g,"N",Font,new Point((int)(cx+x*(r+12))-5,(int)(cy-y*(r+12))-8),Theme.Ink);
  }

  // ---------------------------------------------------------------- mouse
  MapNode NodeAt(Point p){MapNode best=null;double bd=12*12;foreach(var kv in shown){double dx=kv.Value.X-p.X,dy=kv.Value.Y-p.Y,d=dx*dx+dy*dy;if(d<bd){bd=d;best=kv.Key;}}return best;}
  protected override void OnMouseDown(MouseEventArgs e){
   Focus();last=e.Location;Camera();
   if(e.Button==MouseButtons.Left&&Editor!=null){
    var n=NodeAt(e.Location);if(n==null)return;
    drag=Editor.StartDrag(n,nodes,Falloff);vertical=(ModifierKeys&Keys.Shift)!=0;grabMouse=e.Location;
    PointF s;Project(n.Position,out s,out grabDepth);
    if(!HitPlane(e.Location,n.Position.Y,out grabHit))grabHit=n.Position;
    Capture=true;Invalidate();
   }else if(e.Button==MouseButtons.Right&&(ModifierKeys&Keys.Shift)==0)orbiting=true;
   else if(e.Button==MouseButtons.Middle||e.Button==MouseButtons.Right)panning=true;
  }
  protected override void OnMouseMove(MouseEventArgs e){
   Camera();int dx=e.X-last.X,dy=e.Y-last.Y;last=e.Location;
   if(drag!=null){
    Vec3 offset;
    if(vertical)offset=new Vec3(0,(grabMouse.Y-e.Y)*grabDepth/focal,0);
    else{Vec3 hit;if(!HitPlane(e.Location,grabHit.Y,out hit))return;offset=new Vec3(hit.X-grabHit.X,0,hit.Z-grabHit.Z);}
    Editor.DragTo(drag,offset);
    Status(drag.Node);if(NodesMoved!=null)NodesMoved();Invalidate();return;
   }
   if(orbiting){yaw+=dx*0.006;pitch=Math.Max(0.08,Math.Min(1.5,pitch+dy*0.006));Invalidate();return;}
   if(panning){
    double k=dist/focal;var flat=Norm(new Vec3(fwd.X,0,fwd.Z));
    tx-=right.X*dx*k-flat.X*dy*k/Math.Max(0.3,Math.Sin(pitch));tz-=right.Z*dx*k-flat.Z*dy*k/Math.Max(0.3,Math.Sin(pitch));Invalidate();return;
   }
   var h=NodeAt(e.Location);if(h!=hover){hover=h;Cursor=h!=null?Cursors.Hand:Cursors.Default;Invalidate();}
   if(h!=null)Status(h);
   else if(StatusChanged!=null){Vec3 hit;if(HitPlane(e.Location,0,out hit)){var k=Grid.At(hit.X,hit.Z);var t=Pack==null?null:Pack.Get(k);
    StatusChanged("x "+hit.X.ToString("0")+" m, z "+hit.Z.ToString("0")+" m   "+(t==null?"sea":t.Definition+", height "+t.Height+" ("+t.Height*32+" m)")+"   Drag a node to move it, Shift+drag to raise or lower it. Right-drag turns, middle-drag moves, scroll zooms.");}}
  }
  void Status(MapNode n){if(StatusChanged!=null)StatusChanged((n.IsRoad?"Road node ("+n.Label+")":n.Label)+"   "+n.Position.Y.ToString("0.0")+" m up"+(n.IsRoad&&n.Ends.Count>2?", where "+n.Ends.Count+" road pieces meet":""));}
  protected override void OnMouseUp(MouseEventArgs e){
   if(drag!=null&&e.Button==MouseButtons.Left){Editor.EndDrag(drag);drag=null;Capture=false;if(DragFinished!=null)DragFinished();Reload();Invalidate();}
   orbiting=panning=false;
  }
  protected override void OnMouseWheel(MouseEventArgs e){
   // Zoom toward the point under the cursor.
   double f=Math.Pow(0.85,e.Delta/120.0);Camera();Vec3 hit;
   if(HitPlane(e.Location,ty,out hit)){tx+=(hit.X-tx)*(1-f);tz+=(hit.Z-tz)*(1-f);}
   dist=Math.Max(30,Math.Min(150000,dist*f));Invalidate();
  }
  protected override void OnMouseDoubleClick(MouseEventArgs e){
   if(e.Button!=MouseButtons.Left||NodeAt(e.Location)!=null)return;Camera();Vec3 hit;
   if(HitPlane(e.Location,ty,out hit)){var t=Pack==null?null:Pack.Get(Grid.At(hit.X,hit.Z));tx=hit.X;tz=hit.Z;ty=t==null?0:t.Height*Grid.Step;Invalidate();}
  }
  protected override bool IsInputKey(Keys k){return k==Keys.Left||k==Keys.Right||k==Keys.Up||k==Keys.Down||base.IsInputKey(k);}
  protected override void OnKeyDown(KeyEventArgs e){
   if(e.KeyCode==Keys.Left)yaw-=0.1;else if(e.KeyCode==Keys.Right)yaw+=0.1;else if(e.KeyCode==Keys.Up)pitch=Math.Min(1.5,pitch+0.08);else if(e.KeyCode==Keys.Down)pitch=Math.Max(0.08,pitch-0.08);else if(e.KeyCode==Keys.F)FitAll();else{base.OnKeyDown(e);return;}
   Invalidate();
  }
 }
}
