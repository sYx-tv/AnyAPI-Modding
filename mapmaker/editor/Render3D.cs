using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
namespace AnyMapMaker {
 // Software 3D renderer for the map preview: a depth buffer, flat-shaded triangles with sun light and haze, and a
 // see-through sea. Pure C#, so it needs nothing beyond what ships with Windows and renders the same under Mono.
 public sealed class Raster {
  public int W,H;public int[] Px;public float[] Z;
  public Vec3 Eye,Fwd,Right,Up;public double Focal,Fov;
  public Color Fog=Color.FromArgb(182,212,234);public double FogNear=3000,FogFar=16000;
  public void Resize(int w,int h){if(w==W&&h==H&&Px!=null)return;W=Math.Max(1,w);H=Math.Max(1,h);Px=new int[W*H];Z=new float[W*H];}
  public void Camera(Vec3 eye,Vec3 fwd,Vec3 right,Vec3 up,double fov){Fov=fov;Eye=eye;Fwd=fwd;Right=right;Up=up;Focal=H/2.0/Math.Tan(fov/2);}

  // Sky: deep blue at the top to pale haze at the horizon.
  public void Clear(double horizonY){
   Color top=Color.FromArgb(95,159,214),low=Fog,sea=Color.FromArgb(40,104,140);
   for(int y=0;y<H;y++){
    double t=horizonY<=0?1:Math.Max(0,Math.Min(1,y/horizonY));t=t*t;
    int c=y<horizonY||horizonY<0?Argb(Mix(top,low,t)):Argb(sea);
    int row=y*W;for(int x=0;x<W;x++){Px[row+x]=c;Z[row+x]=float.MaxValue;}
   }
  }

  struct V{public double X,Y,Z;}  // screen x, y and view depth
  readonly V[] clip=new V[4];

  // One world-space triangle in one colour (lit by the caller); alpha < 1 blends it over what is behind.
  public void Tri(Vec3 a,Vec3 b,Vec3 c,Color col,double alpha=1,double bias=0){
   Vec3 va=a-Eye,vb=b-Eye,vc=c-Eye;
   double za=Dot(va,Fwd),zb=Dot(vb,Fwd),zc=Dot(vc,Fwd);const double near=2;
   if(za<near&&zb<near&&zc<near)return;
   // Haze by distance from the camera.
   double dist=(za+zb+zc)/3;double f=Math.Max(0,Math.Min(1,(dist-FogNear)/(FogFar-FogNear)));
   int argb=Argb(Mix(col,Fog,f*f*(3-2*f)));
   Vec3[] w={va,vb,vc};double[] z={za,zb,zc};int n=0;
   // Clip against the near plane (one or two triangles come out).
   for(int i=0;i<3;i++){
    int j=(i+1)%3;bool inI=z[i]>=near,inJ=z[j]>=near;
    if(inI)clip[n++]=ToScreen(w[i],z[i]);
    if(inI!=inJ){double t=(near-z[i])/(z[j]-z[i]);var p=w[i]+(w[j]-w[i])*t;clip[n++]=ToScreen(p,near);}
   }
   if(n<3)return;
   Fill(clip[0],clip[1],clip[2],argb,alpha,bias);
   if(n==4)Fill(clip[0],clip[2],clip[3],argb,alpha,bias);
  }
  V ToScreen(Vec3 v,double z){return new V{X=W/2.0+Dot(v,Right)*Focal/z,Y=H/2.0-Dot(v,Up)*Focal/z,Z=z};}

  // bias pulls the triangle slightly toward the camera (a share of its depth), so roads drawn on the ground win.
  void Fill(V a,V b,V c,int argb,double alpha,double bias){
   double area=(b.X-a.X)*(c.Y-a.Y)-(b.Y-a.Y)*(c.X-a.X);if(Math.Abs(area)<1e-9)return;
   int x0=Math.Max(0,(int)Math.Floor(Math.Min(a.X,Math.Min(b.X,c.X)))),x1=Math.Min(W-1,(int)Math.Ceiling(Math.Max(a.X,Math.Max(b.X,c.X))));
   int y0=Math.Max(0,(int)Math.Floor(Math.Min(a.Y,Math.Min(b.Y,c.Y)))),y1=Math.Min(H-1,(int)Math.Ceiling(Math.Max(a.Y,Math.Max(b.Y,c.Y))));
   if(x0>x1||y0>y1)return;
   // Depth is interpolated as 1/z, which is linear on screen.
   double ia=1/a.Z,ib=1/b.Z,ic=1/c.Z,inv=1/area;
   int sr=(argb>>16)&255,sg=(argb>>8)&255,sb=argb&255;int ai=(int)(alpha*256);bool blend=alpha<0.999;
   for(int y=y0;y<=y1;y++){
    double py=y+0.5;int row=y*W;
    // Find the span of this row inside the triangle, then walk it.
    double l=x0,r=x1+1;
    if(!Span(a,b,c,py,ref l,ref r))continue;
    int xs=Math.Max(x0,(int)Math.Ceiling(l-0.5)),xe=Math.Min(x1,(int)Math.Floor(r-0.5));
    for(int x=xs;x<=xe;x++){
     double px=x+0.5;
     double w0=((b.X-px)*(c.Y-py)-(b.Y-py)*(c.X-px))*inv,w1=((c.X-px)*(a.Y-py)-(c.Y-py)*(a.X-px))*inv,w2=1-w0-w1;
     float depth=(float)((1-bias)/(w0*ia+w1*ib+w2*ic));int k=row+x;
     if(depth>=Z[k])continue;
     if(blend){int d=Px[k];Px[k]=unchecked((int)0xff000000)|(((((d>>16)&255)*(256-ai)+sr*ai)>>8)<<16)|(((((d>>8)&255)*(256-ai)+sg*ai)>>8)<<8)|(((d&255)*(256-ai)+sb*ai)>>8);}
     else{Px[k]=argb;Z[k]=depth;}
    }
   }
  }
  // Where the horizontal line y = py crosses the triangle.
  static bool Span(V a,V b,V c,double py,ref double l,ref double r){
   double lo=double.MaxValue,hi=double.MinValue;
   Cross(a,b,py,ref lo,ref hi);Cross(b,c,py,ref lo,ref hi);Cross(c,a,py,ref lo,ref hi);
   if(lo>hi)return false;l=Math.Max(l,lo);r=Math.Min(r,hi);return l<=r;
  }
  static void Cross(V p,V q,double py,ref double lo,ref double hi){
   if((py<p.Y)==(py<q.Y))return;double t=(py-p.Y)/(q.Y-p.Y),x=p.X+(q.X-p.X)*t;if(x<lo)lo=x;if(x>hi)hi=x;
  }

  public void CopyTo(Bitmap bmp){
   var data=bmp.LockBits(new Rectangle(0,0,W,H),ImageLockMode.WriteOnly,PixelFormat.Format32bppArgb);
   for(int y=0;y<H;y++)Marshal.Copy(Px,y*W,data.Scan0+y*data.Stride,W);
   bmp.UnlockBits(data);
  }
  public static double Dot(Vec3 a,Vec3 b){return a.X*b.X+a.Y*b.Y+a.Z*b.Z;}
  public static int Argb(Color c){return unchecked((int)0xff000000)|(c.R<<16)|(c.G<<8)|c.B;}
  public static Color Mix(Color a,Color b,double t){return Color.FromArgb(C(a.R+(b.R-a.R)*t),C(a.G+(b.G-a.G)*t),C(a.B+(b.B-a.B)*t));}
  public static int C(double v){return v<0?0:v>255?255:(int)v;}
 }

 // The map as triangles: terrain smoothed between tile heights, roads as strips laid on it, and simple stand-in
 // models for buildings, props, trees, loot and enemies. Built once per edit, drawn every frame.
 public sealed class WorldMesh {
  public struct T{public Vec3 A,B,C;public Color Col;public double Alpha,Bias;}
  public readonly List<T> Tris=new List<T>();      // terrain, rebuilt only when the land changes
  public readonly List<T> Objects=new List<T>();   // roads and models, rebuilt after every edit
  List<T> target;double bias;   // depth bias for what is added next (roads)
  readonly MapPack pack;public readonly string Signature;
  public Vec3 Centre;public int ObjectsVersion;   // for the GPU path: where vertex coordinates are measured from; bumps on every object rebuild
  static readonly Vec3 Sun=Norm(new Vec3(-0.45,0.8,0.35));
  public const int Subdiv=4;          // each 512 m tile is cut into Subdiv^2 smaller triangles
  const double SeaFloorSteps=-0.6;    // where land meets open sea, the ground slopes down to about -19 m

  public WorldMesh(MapPack pack){
   this.pack=pack;Signature=SignatureOf(pack);target=Tris;
   if(pack.Tiles.Count>0){var a=Grid.Corner(pack.MinX,pack.MinZ);var b=Grid.Corner(pack.MaxX+1,pack.MaxZ+1);Centre=new Vec3((a.X+b.X)/2,0,(a.Z+b.Z)/2);}
   foreach(var t in pack.Tiles.Values)AddTile(t);
  }
  public void SetObjects(MapScene scene,bool roads,bool points){
   Objects.Clear();target=Objects;ObjectsVersion++;
   bias=0.0015;if(scene!=null&&roads)foreach(var r in scene.Roads)AddRoad(r);bias=0;
   if(scene!=null&&points)foreach(var m in scene.Markers){var l=MapScene.Find(m.Layer);if(l.Visible&&m.Layer!="junctions")AddModel(m);}
  }
  // Changes whenever any tile's piece, height or orientation changes, or tiles are added or removed.
  public static string SignatureOf(MapPack pack){
   long h=pack.Tiles.Count;
   foreach(var t in pack.Tiles.Values)h+=(long)(t.Key.GetHashCode()*31+t.Terrain.GetHashCode())*2654435761L;
   return h.ToString();
  }

  // ---------------------------------------------------------------- terrain
  // Height of a tile in metres; no tile = open sea floor.
  double TileY(TileKey k){var t=pack.Get(k);return t==null?SeaFloorSteps*Grid.Step:t.Height*Grid.Step;}
  // Mean height of the six tiles that meet at a grid corner.
  double CornerY(Vec3 corner){
   double sum=0;for(int i=0;i<6;i++){double a=(30+60*i)*Math.PI/180;sum+=TileY(Grid.At(corner.X+Math.Cos(a)*4,corner.Z+Math.Sin(a)*4));}
   return sum/6;
  }
  // Height along a tile edge: corner means at the ends, the mean of the two tiles at the middle (a smooth curve
  // through the three), so both tiles that share the edge get the same heights and no cracks open.
  double EdgeY(Vec3 p,Vec3 q,double s,double self,TileKey other){
   double y0=CornerY(p),y1=CornerY(q),ym=(self+TileY(other))/2;
   return y0*(1-s)*(1-2*s)+ym*4*s*(1-s)+y1*s*(2*s-1);
  }
  // Ground height anywhere: flat at the tile's height in the middle, easing to the edge heights near the edges.
  public double HeightAt(double x,double z){
   var k=Grid.At(x,z);var t=pack.Get(k);if(t==null)return SeaFloorSteps*Grid.Step;
   var c=Grid.Corners(k);double b0,b1,b2;Bary(c,x,z,out b0,out b1,out b2);
   // Follow the drawn facets (the smooth surface sampled at the subdivision points), not the curve between them.
   int n=Subdiv;double u=Math.Max(0,Math.Min(n,b1*n)),v=Math.Max(0,Math.Min(n,b2*n));
   int i=Math.Min(n-1,(int)u),j=Math.Min(n-1-i,(int)v);double fu=u-i,fv=v-j;
   Func<int,int,double> at=(ii,jj)=>Surface(k,c,Math.Max(0,1-(double)(ii+jj)/n),(double)ii/n,(double)jj/n);
   if(fu+fv<=1||i+j+1>=n)return at(i,j)+(at(i+1,j)-at(i,j))*fu+(at(i,j+1)-at(i,j))*fv;
   return at(i+1,j+1)+(at(i,j+1)-at(i+1,j+1))*(1-fu)+(at(i+1,j)-at(i+1,j+1))*(1-fv);
  }
  double Surface(TileKey k,Vec3[] c,double b0,double b1,double b2){
   double self=TileY(k);
   // Nearest edge: opposite the smallest barycentric weight.
   double m=Math.Min(b0,Math.Min(b1,b2));const double band=0.34;
   if(m>=band)return self;
   Vec3 p,q;double s;
   if(m==b0){p=c[1];q=c[2];s=b2/(b1+b2);}else if(m==b1){p=c[2];q=c[0];s=b0/(b2+b0);}else{p=c[0];q=c[1];s=b1/(b0+b1);}
   // Parametrise every shared edge the same way from both tiles: from its lower-keyed corner.
   if(Order(p,q)>0){var tmp=p;p=q;q=tmp;s=1-s;}
   var mid=(p+q)*0.5;var centre=(c[0]+c[1]+c[2])*(1.0/3);var outward=Norm(mid-centre);
   var other=Grid.At(mid.X+outward.X*4,mid.Z+outward.Z*4);
   double e=EdgeY(p,q,s,self,other);double u=m/band;u=u*u*(3-2*u);
   return e+(self-e)*u;
  }
  static int Order(Vec3 a,Vec3 b){int c=Math.Round(a.X).CompareTo(Math.Round(b.X));return c!=0?c:Math.Round(a.Z).CompareTo(Math.Round(b.Z));}
  static void Bary(Vec3[] c,double x,double z,out double b0,out double b1,out double b2){
   double d=(c[1].Z-c[2].Z)*(c[0].X-c[2].X)+(c[2].X-c[1].X)*(c[0].Z-c[2].Z);
   b0=((c[1].Z-c[2].Z)*(x-c[2].X)+(c[2].X-c[1].X)*(z-c[2].Z))/d;
   b1=((c[2].Z-c[0].Z)*(x-c[2].X)+(c[0].X-c[2].X)*(z-c[2].Z))/d;
   b2=1-b0-b1;
  }
  void AddTile(Tile t){
   var c=Grid.Corners(t.Key);int n=Subdiv;var v=new Vec3[n+1,n+1];
   for(int i=0;i<=n;i++)for(int j=0;j<=n-i;j++){
    double b1=(double)i/n,b2=(double)j/n,b0=1-b1-b2;
    var p=c[0]*b0+c[1]*b1+c[2]*b2;p.Y=Surface(t.Key,c,Math.Max(0,b0),b1,b2);v[i,j]=p;
   }
   for(int i=0;i<n;i++)for(int j=0;j<n-i;j++){
    Ground(t,v[i,j],v[i+1,j],v[i,j+1]);
    if(j<n-i-1)Ground(t,v[i+1,j],v[i+1,j+1],v[i,j+1]);
   }
   // Edges to open sea get a skirt down to the sea floor so the coast has no gap.
   for(int e=0;e<3;e++){
    Vec3 p=c[e],q=c[(e+1)%3];var mid=(p+q)*0.5;var centre=(c[0]+c[1]+c[2])*(1.0/3);var o=Norm(mid-centre);
    if(pack.Get(Grid.At(mid.X+o.X*4,mid.Z+o.Z*4))!=null)continue;
    double fy=SeaFloorSteps*Grid.Step-40;
    for(int s=0;s<n;s++){
     var a=p+(q-p)*((double)s/n);var b=p+(q-p)*((double)(s+1)/n);a.Y=HeightAt(a.X-o.X*0.5,a.Z-o.Z*0.5);b.Y=HeightAt(b.X-o.X*0.5,b.Z-o.Z*0.5);
     Add(a,b,new Vec3(b.X,fy,b.Z),Color.FromArgb(150,136,104));Add(a,new Vec3(b.X,fy,b.Z),new Vec3(a.X,fy,a.Z),Color.FromArgb(150,136,104));
    }
   }
  }
  void Ground(Tile t,Vec3 a,Vec3 b,Vec3 c){
   var nrm=Norm(Cross(b-a,c-a));if(nrm.Y<0)nrm=nrm*-1;
   double y=(a.Y+b.Y+c.Y)/3;string d=t.Definition;
   Color col;
   if(d.Contains("cement")||d.Contains("cargo")||d.Contains("airfield")||d.Contains("trainyard"))col=Color.FromArgb(150,150,146);
   else if(d.Contains("river")||d.Contains("lake"))col=Color.FromArgb(86,112,84);
   else if(y<5)col=Color.FromArgb(214,196,142);                                   // beach
   else if(y>330)col=Color.FromArgb(236,240,244);                                 // snow
   else{
    // Grass with a little variety, drier and darker higher up, rock on steep ground.
    double n=0.5+0.5*Math.Sin(a.X*0.013+a.Z*0.009)*Math.Cos(a.Z*0.011-a.X*0.004);
    col=Raster.Mix(Color.FromArgb(104,150,66),Color.FromArgb(132,164,82),n);
    if(y>140)col=Raster.Mix(col,Color.FromArgb(120,118,86),Math.Min(1,(y-140)/160));
    if(nrm.Y<0.93)col=Raster.Mix(col,Color.FromArgb(128,118,104),Math.Min(1,(0.93-nrm.Y)/0.1));
   }
   Add(a,b,c,col,nrm);
  }

  // ---------------------------------------------------------------- roads
  void AddRoad(Road r){
   bool train=r.Type.StartsWith("train");double half=train?2.2:r.Type=="road_c"?3:r.Type=="road_b"?3.5:4.5;
   Color top=train?Color.FromArgb(118,98,78):r.Type=="road_c"?Color.FromArgb(150,124,92):Color.FromArgb(62,64,68);
   var pts=r.Points;if(pts==null||pts.Length<2)return;
   var left=new Vec3[pts.Length];var rightP=new Vec3[pts.Length];
   for(int i=0;i<pts.Length;i++){
    var dir=pts[Math.Min(pts.Length-1,i+1)]-pts[Math.Max(0,i-1)];var side=Norm(new Vec3(-dir.Z,0,dir.X))*half;
    left[i]=OnGround(pts[i]+side);rightP[i]=OnGround(pts[i]-side);
   }
   double run=0;
   for(int i=0;i+1<pts.Length;i++){
    Add(left[i],left[i+1],rightP[i+1],top);Add(left[i],rightP[i+1],rightP[i],top);
    // Dashed centre line on asphalt roads.
    double len=(pts[i+1]-pts[i]).Length;
    if(!train&&r.Type!="road_c"&&((int)(run/6))%2==0){
     var m0=(left[i]+rightP[i])*0.5;var m1=(left[i+1]+rightP[i+1])*0.5;var s=Norm(new Vec3(-(m1.Z-m0.Z),0,m1.X-m0.X))*0.18;var up=new Vec3(0,0.05,0);
     Add(m0+s+up,m1+s+up,m1-s+up,Color.FromArgb(232,198,72));Add(m0+s+up,m1-s+up,m0-s+up,Color.FromArgb(232,198,72));
    }
    run+=len;
   }
  }
  // Roads keep their own height (bridges, ramps) but never sink under the ground.
  Vec3 OnGround(Vec3 p){double g=HeightAt(p.X,p.Z);return new Vec3(p.X,Math.Max(p.Y,g)+0.35,p.Z);}

  // ---------------------------------------------------------------- stand-in models
  void AddModel(Marker m){
   var p=m.Position;double gy=HeightAt(p.X,p.Z);var at=new Vec3(p.X,Math.Max(gy,p.Y>gy-3?p.Y:gy),p.Z);
   double rot=m.HasRotation?m.RotationY:0;string what=(m.Label??"").ToLowerInvariant();
   switch(m.Layer){
    case "spawn_zones":House(at,rot);break;
    case "loot_spawn_zones":Box(at,rot,1.2,0.9,0.8,Color.FromArgb(196,150,70));break;
    case "actor_spawn_zones":Box(at,0,0.6,1.8,0.6,Color.FromArgb(200,70,80));break;
    case "dead_drop_spawn_zones":Box(at,rot,0.9,0.9,0.9,Color.FromArgb(150,110,210));break;
    default:
     if(what.Contains("tree")||what.Contains("pine")||what.Contains("bush"))Tree(at,1);
     else if(what.Contains("fence"))Box(at,rot,4,1.4,0.2,Color.FromArgb(128,96,62));
     else if(what.Contains("pole")||what.Contains("lamp")||what.Contains("light"))Box(at,0,0.35,9,0.35,Color.FromArgb(110,92,72));
     else Box(at,rot,1.4,1.4,1.4,Color.FromArgb(170,160,140));
     break;
   }
  }
  public void House(Vec3 at,double rot){
   Box(at,rot,14,7,10,Color.FromArgb(220,212,196));
   // Hipped roof as four sloped faces.
   double hx=7.8,hz=5.8,y0=at.Y+7,y1=at.Y+11.5;
   var c=new[]{Rot(at,-hx,y0,-hz,rot),Rot(at,hx,y0,-hz,rot),Rot(at,hx,y0,hz,rot),Rot(at,-hx,y0,hz,rot)};
   var r0=Rot(at,-3,y1,0,rot);var r1=Rot(at,3,y1,0,rot);var roof=Color.FromArgb(150,62,48);
   Add(c[0],c[1],r1,roof);Add(c[0],r1,r0,roof);Add(c[2],c[3],r0,roof);Add(c[2],r0,r1,roof);
   Add(c[1],c[2],r1,roof);Add(c[3],c[0],r0,roof);
  }
  public void Tree(Vec3 at,double s){
   Box(at,0,0.8*s,3*s,0.8*s,Color.FromArgb(96,72,52));
   var top=at+new Vec3(0,13*s,0);const int k=6;double r=4.2*s,y=at.Y+3*s;
   for(int i=0;i<k;i++){double a0=i*2*Math.PI/k,a1=(i+1)*2*Math.PI/k;
    Add(new Vec3(at.X+Math.Cos(a0)*r,y,at.Z+Math.Sin(a0)*r),new Vec3(at.X+Math.Cos(a1)*r,y,at.Z+Math.Sin(a1)*r),top,Color.FromArgb(52,96,50));}
  }
  public void Box(Vec3 at,double rot,double sx,double sy,double sz,Color col){
   double x=sx/2,z=sz/2;
   var b=new[]{Rot(at,-x,at.Y,-z,rot),Rot(at,x,at.Y,-z,rot),Rot(at,x,at.Y,z,rot),Rot(at,-x,at.Y,z,rot)};
   var t=new Vec3[4];for(int i=0;i<4;i++)t[i]=b[i]+new Vec3(0,sy,0);
   Add(t[0],t[1],t[2],col);Add(t[0],t[2],t[3],col);
   for(int i=0;i<4;i++){int j=(i+1)%4;Add(b[i],b[j],t[j],col);Add(b[i],t[j],t[i],col);}
  }
  static Vec3 Rot(Vec3 at,double x,double y,double z,double rot){double c=Math.Cos(rot),s=Math.Sin(rot);return new Vec3(at.X+x*c-z*s,y,at.Z+x*s+z*c);}

  // ---------------------------------------------------------------- helpers
  public void Add(Vec3 a,Vec3 b,Vec3 c,Color col){var n=Norm(Cross(b-a,c-a));Add(a,b,c,col,n);}
  void Add(Vec3 a,Vec3 b,Vec3 c,Color col,Vec3 n){
   // Two-sided light: whichever side faces the sun.
   double lit=Math.Abs(Raster.Dot(n,Sun));double k=0.42+0.68*lit;
   target.Add(new T{A=a,B=b,C=c,Col=Color.FromArgb(Raster.C(col.R*k),Raster.C(col.G*k),Raster.C(col.B*k)),Alpha=1,Bias=bias});
  }
  public static Vec3 Cross(Vec3 a,Vec3 b){return new Vec3(a.Y*b.Z-a.Z*b.Y,a.Z*b.X-a.X*b.Z,a.X*b.Y-a.Y*b.X);}
  public static Vec3 Norm(Vec3 v){double l=v.Length;return l>0?v*(1/l):v;}

  public void Draw(Raster r){
   foreach(var t in Tris)r.Tri(t.A,t.B,t.C,t.Col,t.Alpha,t.Bias);
   foreach(var t in Objects)r.Tri(t.A,t.B,t.C,t.Col,t.Alpha,t.Bias);
   // Sea last, see-through, as a big grid of quads around the camera so near-plane clipping stays cheap.
   var sea=Color.FromArgb(46,118,156);double cell=2000;int n=12;
   double cx=Math.Floor(r.Eye.X/cell)*cell,cz=Math.Floor(r.Eye.Z/cell)*cell;
   for(int i=-n;i<n;i++)for(int j=-n;j<n;j++){
    double x0=cx+i*cell,z0=cz+j*cell,x1=x0+cell,z1=z0+cell;
    r.Tri(new Vec3(x0,0,z0),new Vec3(x1,0,z0),new Vec3(x1,0,z1),sea,0.78);r.Tri(new Vec3(x0,0,z0),new Vec3(x1,0,z1),new Vec3(x0,0,z1),sea,0.78);
   }
  }
 }
}
