using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;
using AnyApiManager;
namespace AnyMapMaker {
 // 3D view of the map: orbit, pan and zoom around it, and drag road and point nodes. The world is drawn by the
 // software renderer in Render3D.cs, so it needs nothing beyond what ships with Windows. The game's own piece shapes
 // (hills, river beds) are not in the tile files, so the ground is smoothed between tile heights as a preview.
 public sealed class MapView3D : Control {
  public MapPack Pack; public MapScene Scene; public MapEditor Editor;
  public double Falloff=120;           // proportional-editing radius for road nodes, metres
  public event Action<string> StatusChanged;
  public event Action NodesMoved, DragFinished;
  public event Action<MapNode> NodeChanged;   // hovered or dragged point (null when none)
  // Camera: orbits a target point.
  double tx,ty,tz,yaw=0.6,pitch=0.75,dist=3000;const double Fov=50*Math.PI/180;
  Vec3 eye,fwd,right,up;double focal;
  List<MapNode> nodes=new List<MapNode>();readonly List<KeyValuePair<MapNode,PointF>> shown=new List<KeyValuePair<MapNode,PointF>>();
  MapNode hover;MapEditor.NodeDrag drag;bool vertical;Vec3 grabHit;Point grabMouse;double grabDepth;
  Point last;bool orbiting,panning;

  public MapView3D(){
   SetStyle(ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer|ControlStyles.UserPaint|ControlStyles.ResizeRedraw|ControlStyles.Selectable,true);BackColor=Color.FromArgb(22,48,74);
   sharpen.Tick+=(s,e)=>{sharpen.Stop();moving=false;Invalidate();};
  }

  public void SetMap(MapPack pack,MapScene scene,MapEditor editor){Pack=pack;Scene=scene;Editor=editor;mesh=null;Reload();FitAll();}
  public void ShowScene(MapScene scene,MapEditor editor){Scene=scene;Editor=editor;objectsStale=true;if(drag==null)Reload();Invalidate();}
  void Reload(){nodes=Editor==null?new List<MapNode>():Editor.Nodes(MapScene.Find("roads").Visible,true);hover=null;}
  public void FitAll(){
   if(Pack==null||Pack.Tiles.Count==0){tx=tz=0;ty=0;dist=3000;Invalidate();return;}
   var a=Grid.Corner(Pack.MinX,Pack.MinZ);var b=Grid.Corner(Pack.MaxX+1,Pack.MaxZ+1);
   tx=(a.X+b.X)/2;tz=(a.Z+b.Z)/2;ty=0;double size=Math.Max(Math.Abs(b.X-a.X),Math.Abs(b.Z-a.Z));
   dist=Math.Max(400,Math.Min(120000,size*1.1));Invalidate();
  }
  public void LookAt(Vec3 p,double distance){tx=p.X;ty=p.Y;tz=p.Z;dist=distance;Invalidate();}
  public void Turn(double yawTo,double pitchTo){yaw=yawTo;pitch=Math.Max(0.08,Math.Min(1.5,pitchTo));Invalidate();}

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
  // ---------------------------------------------------------------- 3D frame
  readonly Raster raster=new Raster();WorldMesh mesh;Bitmap frame;bool objectsStale=true;
  readonly Timer sharpen=new Timer{Interval=180};bool moving;
  // While the camera or a node moves, frames render at half size; a moment after it stops, one sharp frame follows.
  void Moving(){moving=true;sharpen.Stop();sharpen.Start();}
  void EnsureMesh(){
   if(Pack==null){mesh=null;return;}
   if(mesh==null||mesh.Signature!=WorldMesh.SignatureOf(Pack)){mesh=new WorldMesh(Pack);objectsStale=true;}
   if(objectsStale){mesh.SetObjects(Scene,MapScene.Find("roads").Visible,true);objectsStale=false;}
  }
  // GPU drawing, made on first use; any failure switches to the software renderer for the rest of the run.
  GlRender gl;bool glOff;public string RendererInfo="software";
  public static bool PreferSoftware;   // --ui-shot and tests under Mono
  bool TryGl(){
   if(glOff||PreferSoftware)return false;if(gl!=null)return true;
   try{gl=new GlRender();RendererInfo=gl.Info;return true;}catch(Exception ex){GlFailed(ex);return false;}
  }
  void GlFailed(Exception ex){glOff=true;RendererInfo="software ("+ex.Message+")";if(gl!=null){gl.Dispose();gl=null;}if(StatusChanged!=null)StatusChanged("3D preview: the graphics card path isn't available ("+ex.Message+"), so it draws in software.");}
  protected override void Dispose(bool disposing){if(disposing&&gl!=null){gl.Dispose();gl=null;}base.Dispose(disposing);}
  public double GroundAt(double x,double z){EnsureMesh();return mesh==null?0:mesh.HeightAt(x,z);}

  protected override void OnPaint(PaintEventArgs e){
   var g=e.Graphics;Camera();
   if(Pack==null){g.Clear(Theme.Window);TextRenderer.DrawText(g,"Open a map to see it in 3D.",Font,ClientRectangle,Theme.Muted,TextFormatFlags.HorizontalCenter|TextFormatFlags.VerticalCenter);return;}
   EnsureMesh();
   // The GPU draws every frame at full size; the software fallback halves the size while the camera moves.
   bool useGl=TryGl();
   int scale=moving&&!useGl?2:1,w=Math.Max(1,Width/scale),h=Math.Max(1,Height/scale);
   raster.Resize(w,h);raster.Camera(eye,fwd,right,up,Fov);
   raster.FogNear=Math.Max(2500,dist*1.2);raster.FogFar=Math.Max(14000,dist*5);
   var flat=Norm(new Vec3(fwd.X,0,fwd.Z));PointF hz;double hd;float horizon=Project(new Vec3(eye.X,0,eye.Z)+flat*1e7,out hz,out hd)?hz.Y:-1;
   double hy=eye.Y<0?h+1:horizon/scale;
   if(frame==null||frame.Width!=w||frame.Height!=h){if(frame!=null)frame.Dispose();frame=new Bitmap(w,h,System.Drawing.Imaging.PixelFormat.Format32bppArgb);}
   bool drawn=false;
   if(useGl)try{gl.Render(mesh,raster,hy,frame);drawn=true;}catch(Exception ex){GlFailed(ex);}
   if(!drawn){raster.Clear(hy);mesh.Draw(raster);raster.CopyTo(frame);}
   g.InterpolationMode=scale>1?InterpolationMode.Bilinear:InterpolationMode.NearestNeighbor;g.PixelOffsetMode=PixelOffsetMode.Half;
   g.DrawImage(frame,new Rectangle(0,0,Width,Height),0,0,w,h,GraphicsUnit.Pixel);
   g.PixelOffsetMode=PixelOffsetMode.Default;g.SmoothingMode=SmoothingMode.AntiAlias;

   DrawStart(g);
   // Nodes on top.
   shown.Clear();
   foreach(var n in nodes){
    if(!n.IsRoad&&!MapScene.Find(n.ListKey).Visible)continue;
    PointF s;double d;if(!Project(n.Position+new Vec3(0,n.IsRoad?0.3:0,0),out s,out d)||s.X<-20||s.Y<-20||s.X>Width+20||s.Y>Height+20)continue;
    if(d>Math.Max(4000,dist*2.5))continue;
    shown.Add(new KeyValuePair<MapNode,PointF>(n,s));
    bool hot=n==hover||(drag!=null&&n==drag.Node);float r=hot?7:n.IsRoad?4.5f:0;if(r==0)continue;
    using(var shadow=new SolidBrush(Color.FromArgb(90,0,0,0)))g.FillEllipse(shadow,s.X-r,s.Y-r+1.5f,r*2,r*2);
    using(var b=new SolidBrush(hot?Theme.Blue:Color.FromArgb(245,248,252)))g.FillEllipse(b,s.X-r,s.Y-r,r*2,r*2);
    if(hot)using(var pen=new Pen(Color.White,2f))g.DrawEllipse(pen,s.X-r,s.Y-r,r*2,r*2);
   }
   if(drag!=null&&drag.Node.IsRoad&&Falloff>0){
    // Show the falloff circle on the ground around the dragged node.
    var c=drag.Node.Position;var ring=new List<PointF>();double d;
    for(int i=0;i<=48;i++){double a=i*Math.PI/24;double x=c.X+Math.Cos(a)*Falloff,z=c.Z+Math.Sin(a)*Falloff;PointF s;if(Project(new Vec3(x,Math.Max(c.Y,mesh.HeightAt(x,z))+0.5,z),out s,out d))ring.Add(s);}
    if(ring.Count>2)using(var pen=new Pen(Color.FromArgb(200,Theme.Blue),2f){DashStyle=DashStyle.Dash})g.DrawLines(pen,ring.ToArray());
   }
   DrawCompass(g);
  }
  void DrawStart(Graphics g){
   var list=new List<KeyValuePair<Vec3,string>>();
   if(Pack.Spawn!=null)list.Add(new KeyValuePair<Vec3,string>(Pack.Spawn.Value,"Start"));
   else{list.Add(new KeyValuePair<Vec3,string>(Grid.SpawnCreative,"Start: Creative / Sandbox"));list.Add(new KeyValuePair<Vec3,string>(Grid.SpawnSurvival,"Start: Survival"));}
   foreach(var kv in list){
    var p=kv.Key;var t=Pack.Get(Grid.At(p.X,p.Z));double y=Pack.Spawn!=null?p.Y:mesh.HeightAt(p.X,p.Z);
    var c=t==null?Theme.Bad:Theme.Ok;
    var ring=new List<PointF>();double d;PointF s,top;
    for(int i=0;i<=32;i++){double a=i*Math.PI/16;if(Project(new Vec3(p.X+Math.Cos(a)*6,y+0.4,p.Z+Math.Sin(a)*6),out s,out d))ring.Add(s);}
    if(!Project(new Vec3(p.X,y,p.Z),out s,out d)||!Project(new Vec3(p.X,y+40,p.Z),out top,out d))continue;
    using(var beam=new Pen(Color.FromArgb(110,c),Math.Max(3f,(float)(2.4*focal/d))))g.DrawLine(beam,s,top);
    if(ring.Count>2)using(var pen=new Pen(c,2.5f))g.DrawLines(pen,ring.ToArray());
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
    Status(drag.Node);Moving();if(NodesMoved!=null)NodesMoved();Invalidate();return;
   }
   if(orbiting||panning)Moving();
   if(orbiting){yaw+=dx*0.006;pitch=Math.Max(0.08,Math.Min(1.5,pitch+dy*0.006));Invalidate();return;}
   if(panning){
    double k=dist/focal;var flat=Norm(new Vec3(fwd.X,0,fwd.Z));
    tx-=right.X*dx*k-flat.X*dy*k/Math.Max(0.3,Math.Sin(pitch));tz-=right.Z*dx*k-flat.Z*dy*k/Math.Max(0.3,Math.Sin(pitch));Invalidate();return;
   }
   var h=NodeAt(e.Location);if(h!=hover){hover=h;Cursor=h!=null?Cursors.Hand:Cursors.Default;if(h==null&&NodeChanged!=null)NodeChanged(null);Invalidate();}
   if(h!=null)Status(h);
   else if(StatusChanged!=null){Vec3 hit;if(HitPlane(e.Location,0,out hit)){var k=Grid.At(hit.X,hit.Z);var t=Pack==null?null:Pack.Get(k);
    StatusChanged("x "+hit.X.ToString("0")+" m, z "+hit.Z.ToString("0")+" m   "+(t==null?"sea":t.Definition+", height "+t.Height+" ("+t.Height*32+" m)")+"   Drag a node to move it, Shift+drag to raise or lower it. Right-drag turns, middle-drag moves, scroll zooms.");}}
  }
  void Status(MapNode n){if(NodeChanged!=null)NodeChanged(n);if(StatusChanged!=null)StatusChanged((n.IsRoad?"Road node ("+n.Label+")":n.Label)+"   "+n.Position.Y.ToString("0.0")+" m up"+(n.IsRoad&&n.Ends.Count>2?", where "+n.Ends.Count+" road pieces meet":""));}
  protected override void OnMouseUp(MouseEventArgs e){
   if(drag!=null&&e.Button==MouseButtons.Left){Editor.EndDrag(drag);drag=null;Capture=false;if(DragFinished!=null)DragFinished();Reload();Invalidate();}
   orbiting=panning=false;
  }
  protected override void OnMouseWheel(MouseEventArgs e){
   // Zoom toward the point under the cursor.
   double f=Math.Pow(0.85,e.Delta/120.0);Camera();Vec3 hit;
   if(HitPlane(e.Location,ty,out hit)){tx+=(hit.X-tx)*(1-f);tz+=(hit.Z-tz)*(1-f);}
   dist=Math.Max(30,Math.Min(150000,dist*f));Moving();Invalidate();
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
