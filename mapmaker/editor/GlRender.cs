using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Windows.Forms;
namespace AnyMapMaker {
 // GPU drawing for the 3D preview through OpenGL, which ships with Windows (opengl32.dll) and every graphics driver.
 // It draws into an off-screen framebuffer (4x antialiased when the driver allows) owned by a hidden window, reads
 // the picture back and hands it to the view, which paints its dots and labels on top as before. The triangles stay
 // on the graphics card between frames, so turning the camera costs almost nothing. If anything here fails, the view
 // falls back to the software renderer in Render3D.cs.
 public sealed class GlRender : IDisposable {
  // ---------------------------------------------------------------- Win32 and OpenGL 1.1
  [StructLayout(LayoutKind.Sequential)]struct PFD{public ushort nSize,nVersion;public uint dwFlags;public byte iPixelType,cColorBits,cRedBits,cRedShift,cGreenBits,cGreenShift,cBlueBits,cBlueShift,cAlphaBits,cAlphaShift,cAccumBits,cAccumRedBits,cAccumGreenBits,cAccumBlueBits,cAccumAlphaBits,cDepthBits,cStencilBits,cAuxBuffers,iLayerType,bReserved;public uint dwLayerMask,dwVisibleMask,dwDamageMask;}
  [DllImport("user32.dll")]static extern IntPtr GetDC(IntPtr h);
  [DllImport("user32.dll")]static extern int ReleaseDC(IntPtr h,IntPtr dc);
  [DllImport("gdi32.dll")]static extern int ChoosePixelFormat(IntPtr dc,ref PFD p);
  [DllImport("gdi32.dll")]static extern bool SetPixelFormat(IntPtr dc,int format,ref PFD p);
  [DllImport("opengl32.dll")]static extern IntPtr wglCreateContext(IntPtr dc);
  [DllImport("opengl32.dll")]static extern bool wglMakeCurrent(IntPtr dc,IntPtr rc);
  [DllImport("opengl32.dll")]static extern bool wglDeleteContext(IntPtr rc);
  [DllImport("opengl32.dll")]static extern IntPtr wglGetProcAddress(string name);
  [DllImport("opengl32.dll")]static extern void glViewport(int x,int y,int w,int h);
  [DllImport("opengl32.dll")]static extern void glClearColor(float r,float g,float b,float a);
  [DllImport("opengl32.dll")]static extern void glClear(uint mask);
  [DllImport("opengl32.dll")]static extern void glEnable(uint cap);
  [DllImport("opengl32.dll")]static extern void glDisable(uint cap);
  [DllImport("opengl32.dll")]static extern void glDepthMask(bool on);
  [DllImport("opengl32.dll")]static extern void glBlendFunc(uint s,uint d);
  [DllImport("opengl32.dll")]static extern void glPolygonOffset(float factor,float units);
  [DllImport("opengl32.dll")]static extern void glMatrixMode(uint mode);
  [DllImport("opengl32.dll")]static extern void glLoadMatrixd(double[] m);
  [DllImport("opengl32.dll")]static extern void glLoadIdentity();
  [DllImport("opengl32.dll")]static extern void glEnableClientState(uint a);
  [DllImport("opengl32.dll")]static extern void glDisableClientState(uint a);
  [DllImport("opengl32.dll")]static extern void glVertexPointer(int size,uint type,int stride,IntPtr p);
  [DllImport("opengl32.dll")]static extern void glColorPointer(int size,uint type,int stride,IntPtr p);
  [DllImport("opengl32.dll")]static extern void glDrawArrays(uint mode,int first,int count);
  [DllImport("opengl32.dll")]static extern void glFogi(uint p,int v);
  [DllImport("opengl32.dll")]static extern void glFogf(uint p,float v);
  [DllImport("opengl32.dll")]static extern void glFogfv(uint p,float[] v);
  [DllImport("opengl32.dll")]static extern void glReadPixels(int x,int y,int w,int h,uint format,uint type,int[] data);
  [DllImport("opengl32.dll")]static extern void glPixelStorei(uint p,int v);
  [DllImport("opengl32.dll")]static extern void glBegin(uint mode);
  [DllImport("opengl32.dll")]static extern void glEnd();
  [DllImport("opengl32.dll")]static extern void glColor4ub(byte r,byte g,byte b,byte a);
  [DllImport("opengl32.dll")]static extern void glVertex3d(double x,double y,double z);
  [DllImport("opengl32.dll")]static extern uint glGetError();
  const uint COLOR=0x4000,DEPTH=0x100,DEPTH_TEST=0x0B71,BLEND=0x0BE2,FOG=0x0B60,CULL=0x0B44,OFFSET_FILL=0x8037,MULTISAMPLE=0x809D;
  const uint SRC_ALPHA=0x0302,ONE_MINUS_SRC_ALPHA=0x0303,PROJECTION=0x1701,MODELVIEW=0x1700,VERTEX_ARRAY=0x8074,COLOR_ARRAY=0x8076;
  const uint FLOAT=0x1406,UBYTE=0x1401,TRIANGLES=4,QUADS=7,FOG_MODE=0x0B65,FOG_START=0x0B63,FOG_END=0x0B64,FOG_COLOR=0x0B66,LINEAR=0x2601;
  const uint BGRA=0x80E1,PACK_ALIGNMENT=0x0D05;
  // Framebuffer and buffer objects (OpenGL 3.0 / ARB, loaded by name).
  const uint FRAMEBUFFER=0x8D40,READ_FRAMEBUFFER=0x8CA8,DRAW_FRAMEBUFFER=0x8CA9,RENDERBUFFER=0x8D41,COLOR_ATTACHMENT0=0x8CE0,DEPTH_ATTACHMENT=0x8D00,FRAMEBUFFER_COMPLETE=0x8CD5;
  const uint RGBA8=0x8058,DEPTH24=0x81A6,ARRAY_BUFFER=0x8892,STATIC_DRAW=0x88E4,NEAREST=0x2600;
  delegate void GenFn(int n,uint[] ids);delegate void BindFn(uint target,uint id);delegate void StorageFn(uint target,uint format,int w,int h);
  delegate void StorageMsFn(uint target,int samples,uint format,int w,int h);delegate void AttachFn(uint target,uint attach,uint rbTarget,uint rb);
  delegate uint StatusFn(uint target);delegate void BlitFn(int x0,int y0,int x1,int y1,int dx0,int dy0,int dx1,int dy1,uint mask,uint filter);
  delegate void BufferDataFn(uint target,IntPtr size,IntPtr data,uint usage);
  GenFn glGenFramebuffers,glGenRenderbuffers,glGenBuffers,glDeleteFramebuffers,glDeleteRenderbuffers,glDeleteBuffers;
  BindFn glBindFramebuffer,glBindRenderbuffer,glBindBuffer;StorageFn glRenderbufferStorage;StorageMsFn glRenderbufferStorageMultisample;
  AttachFn glFramebufferRenderbuffer;StatusFn glCheckFramebufferStatus;BlitFn glBlitFramebuffer;BufferDataFn glBufferData;
  T Fn<T>(string name) where T:class{var p=wglGetProcAddress(name);if(p==IntPtr.Zero||(long)p<=3||(long)p==-1)p=wglGetProcAddress(name+"EXT");if(p==IntPtr.Zero||(long)p<=3||(long)p==-1)throw new NotSupportedException(name);return Marshal.GetDelegateForFunctionPointer(p,typeof(T)) as T;}

  // ---------------------------------------------------------------- set-up
  readonly NativeWindow host=new NativeWindow();IntPtr dc,rc;
  public readonly string Info;
  public GlRender(){
   host.CreateHandle(new CreateParams{Caption="AnyMapMaker GL",Width=16,Height=16,Style=0});
   dc=GetDC(host.Handle);
   var pfd=new PFD{nSize=(ushort)Marshal.SizeOf(typeof(PFD)),nVersion=1,dwFlags=0x4|0x20|0x1,iPixelType=0,cColorBits=32,cDepthBits=24};   // DRAW_TO_WINDOW|SUPPORT_OPENGL|DOUBLEBUFFER
   int f=ChoosePixelFormat(dc,ref pfd);if(f==0||!SetPixelFormat(dc,f,ref pfd))throw new NotSupportedException("no OpenGL pixel format");
   rc=wglCreateContext(dc);if(rc==IntPtr.Zero||!wglMakeCurrent(dc,rc))throw new NotSupportedException("no OpenGL context");
   glGenFramebuffers=Fn<GenFn>("glGenFramebuffers");glGenRenderbuffers=Fn<GenFn>("glGenRenderbuffers");glGenBuffers=Fn<GenFn>("glGenBuffers");
   glDeleteFramebuffers=Fn<GenFn>("glDeleteFramebuffers");glDeleteRenderbuffers=Fn<GenFn>("glDeleteRenderbuffers");glDeleteBuffers=Fn<GenFn>("glDeleteBuffers");
   glBindFramebuffer=Fn<BindFn>("glBindFramebuffer");glBindRenderbuffer=Fn<BindFn>("glBindRenderbuffer");glBindBuffer=Fn<BindFn>("glBindBuffer");
   glRenderbufferStorage=Fn<StorageFn>("glRenderbufferStorage");glFramebufferRenderbuffer=Fn<AttachFn>("glFramebufferRenderbuffer");
   glCheckFramebufferStatus=Fn<StatusFn>("glCheckFramebufferStatus");glBufferData=Fn<BufferDataFn>("glBufferData");
   try{glRenderbufferStorageMultisample=Fn<StorageMsFn>("glRenderbufferStorageMultisample");glBlitFramebuffer=Fn<BlitFn>("glBlitFramebuffer");}catch(NotSupportedException){}
   Info="OpenGL"+(glBlitFramebuffer!=null?", 4x antialiasing":"");
  }
  void Current(){wglMakeCurrent(dc,rc);}

  // ---------------------------------------------------------------- framebuffers
  int fw,fh;uint fbo,color,depth,msFbo,msColor,msDepth;int samples;
  void Size(int w,int h){
   if(w==fw&&h==fh&&fbo!=0)return;
   FreeTargets();fw=w;fh=h;var ids=new uint[1];
   glGenFramebuffers(1,ids);fbo=ids[0];glBindFramebuffer(FRAMEBUFFER,fbo);
   glGenRenderbuffers(1,ids);color=ids[0];glBindRenderbuffer(RENDERBUFFER,color);glRenderbufferStorage(RENDERBUFFER,RGBA8,w,h);glFramebufferRenderbuffer(FRAMEBUFFER,COLOR_ATTACHMENT0,RENDERBUFFER,color);
   glGenRenderbuffers(1,ids);depth=ids[0];glBindRenderbuffer(RENDERBUFFER,depth);glRenderbufferStorage(RENDERBUFFER,DEPTH24,w,h);glFramebufferRenderbuffer(FRAMEBUFFER,DEPTH_ATTACHMENT,RENDERBUFFER,depth);
   if(glCheckFramebufferStatus(FRAMEBUFFER)!=FRAMEBUFFER_COMPLETE)throw new NotSupportedException("framebuffer incomplete");
   samples=0;
   if(glRenderbufferStorageMultisample!=null&&glBlitFramebuffer!=null){
    // Antialiased copy: draw there, then resolve into the plain one before reading.
    glGenFramebuffers(1,ids);msFbo=ids[0];glBindFramebuffer(FRAMEBUFFER,msFbo);
    glGenRenderbuffers(1,ids);msColor=ids[0];glBindRenderbuffer(RENDERBUFFER,msColor);glRenderbufferStorageMultisample(RENDERBUFFER,4,RGBA8,w,h);glFramebufferRenderbuffer(FRAMEBUFFER,COLOR_ATTACHMENT0,RENDERBUFFER,msColor);
    glGenRenderbuffers(1,ids);msDepth=ids[0];glBindRenderbuffer(RENDERBUFFER,msDepth);glRenderbufferStorageMultisample(RENDERBUFFER,4,DEPTH24,w,h);glFramebufferRenderbuffer(FRAMEBUFFER,DEPTH_ATTACHMENT,RENDERBUFFER,msDepth);
    if(glCheckFramebufferStatus(FRAMEBUFFER)==FRAMEBUFFER_COMPLETE&&glGetError()==0)samples=4;
    else{glBindFramebuffer(FRAMEBUFFER,0);glDeleteFramebuffers(1,new[]{msFbo});glDeleteRenderbuffers(2,new[]{msColor,msDepth});msFbo=msColor=msDepth=0;}
   }
  }
  void FreeTargets(){
   if(fbo!=0){glDeleteFramebuffers(1,new[]{fbo});glDeleteRenderbuffers(2,new[]{color,depth});fbo=color=depth=0;}
   if(msFbo!=0){glDeleteFramebuffers(1,new[]{msFbo});glDeleteRenderbuffers(2,new[]{msColor,msDepth});msFbo=msColor=msDepth=0;}
  }

  // ---------------------------------------------------------------- geometry on the card
  sealed class Batch{public uint Pos,Col;public int Count;}
  Batch terrain,objects;WorldMesh uploadedMesh;List<WorldMesh.T> uploadedObjects;Vec3 origin;
  // World coordinates are ~100 km; they go up relative to the map's centre so 32-bit floats stay precise.
  Batch Upload(List<WorldMesh.T> tris,Batch old){
   if(old!=null)glDeleteBuffers(2,new[]{old.Pos,old.Col});
   var pos=new float[tris.Count*9];var col=new byte[tris.Count*12];int p=0,c=0;
   foreach(var t in tris){
    foreach(var v in new[]{t.A,t.B,t.C}){pos[p++]=(float)(v.X-origin.X);pos[p++]=(float)(v.Y-origin.Y);pos[p++]=(float)(v.Z-origin.Z);col[c++]=t.Col.R;col[c++]=t.Col.G;col[c++]=t.Col.B;col[c++]=(byte)(t.Alpha*255);}
   }
   var b=new Batch{Count=tris.Count*3};var ids=new uint[2];glGenBuffers(2,ids);b.Pos=ids[0];b.Col=ids[1];
   var hp=GCHandle.Alloc(pos,GCHandleType.Pinned);var hc=GCHandle.Alloc(col,GCHandleType.Pinned);
   try{glBindBuffer(ARRAY_BUFFER,b.Pos);glBufferData(ARRAY_BUFFER,(IntPtr)(pos.Length*4),hp.AddrOfPinnedObject(),STATIC_DRAW);
    glBindBuffer(ARRAY_BUFFER,b.Col);glBufferData(ARRAY_BUFFER,(IntPtr)col.Length,hc.AddrOfPinnedObject(),STATIC_DRAW);}
   finally{hp.Free();hc.Free();glBindBuffer(ARRAY_BUFFER,0);}
   return b;
  }
  void DrawBatch(Batch b){
   if(b==null||b.Count==0)return;
   glEnableClientState(VERTEX_ARRAY);glEnableClientState(COLOR_ARRAY);
   glBindBuffer(ARRAY_BUFFER,b.Pos);glVertexPointer(3,FLOAT,0,IntPtr.Zero);
   glBindBuffer(ARRAY_BUFFER,b.Col);glColorPointer(4,UBYTE,0,IntPtr.Zero);
   glDrawArrays(TRIANGLES,0,b.Count);
   glBindBuffer(ARRAY_BUFFER,0);glDisableClientState(VERTEX_ARRAY);glDisableClientState(COLOR_ARRAY);
  }

  // ---------------------------------------------------------------- one frame
  int[] pixels;
  public void Render(WorldMesh mesh,Raster cam,double horizonY,Bitmap into){
   Current();int w=cam.W,h=cam.H;Size(w,h);
   if(mesh!=uploadedMesh){origin=mesh.Centre;terrain=Upload(mesh.Tris,terrain);uploadedMesh=mesh;uploadedObjects=null;}
   if(!ReferenceEquals(mesh.Objects,uploadedObjects)||mesh.ObjectsVersion!=objectsVersion){objects=Upload(mesh.Objects,objects);uploadedObjects=mesh.Objects;objectsVersion=mesh.ObjectsVersion;}
   glBindFramebuffer(FRAMEBUFFER,samples>0?msFbo:fbo);glViewport(0,0,w,h);
   var sea=Color.FromArgb(40,104,140);
   glDisable(DEPTH_TEST);glDisable(FOG);glDisable(CULL);glDepthMask(true);
   glClearColor(sea.R/255f,sea.G/255f,sea.B/255f,1);glClear(COLOR|DEPTH);
   // Sky: a gradient from the top of the picture down to the horizon.
   glMatrixMode(PROJECTION);glLoadIdentity();glMatrixMode(MODELVIEW);glLoadIdentity();
   double hy=horizonY<0?-1:1-2*Math.Min(h,horizonY)/h;
   glBegin(QUADS);Col(Color.FromArgb(95,159,214));glVertex3d(-1,1,0);glVertex3d(1,1,0);Col(cam.Fog);glVertex3d(1,hy,0);glVertex3d(-1,hy,0);glEnd();
   // Camera.
   double near=Math.Max(1.5,cam.FogNear*0.0006),far=Math.Max(60000,cam.FogFar*4);
   double f=1/Math.Tan(cam.Fov/2),a=(double)w/h;
   glMatrixMode(PROJECTION);glLoadMatrixd(new[]{f/a,0,0,0, 0,f,0,0, 0,0,(far+near)/(near-far),-1, 0,0,2*far*near/(near-far),0});
   Vec3 e=cam.Eye-origin,r=cam.Right,u=cam.Up,fw=cam.Fwd;
   glMatrixMode(MODELVIEW);glLoadMatrixd(new[]{r.X,u.X,-fw.X,0, r.Y,u.Y,-fw.Y,0, r.Z,u.Z,-fw.Z,0, -Raster.Dot(r,e),-Raster.Dot(u,e),Raster.Dot(fw,e),1});
   glEnable(DEPTH_TEST);
   glEnable(FOG);glFogi(FOG_MODE,(int)LINEAR);glFogf(FOG_START,(float)cam.FogNear);glFogf(FOG_END,(float)cam.FogFar);glFogfv(FOG_COLOR,new[]{cam.Fog.R/255f,cam.Fog.G/255f,cam.Fog.B/255f,1});
   DrawBatch(terrain);
   // Roads and models a hair toward the camera, so a road on the ground always shows.
   glEnable(OFFSET_FILL);glPolygonOffset(-1,-4);DrawBatch(objects);glDisable(OFFSET_FILL);
   // See-through sea at height 0.
   glEnable(BLEND);glBlendFunc(SRC_ALPHA,ONE_MINUS_SRC_ALPHA);glDepthMask(false);
   double s=Math.Max(80000,far);double cx=cam.Eye.X-origin.X,cz=cam.Eye.Z-origin.Z,y=-origin.Y;
   glBegin(QUADS);glColor4ub(46,118,156,199);glVertex3d(cx-s,y,cz-s);glVertex3d(cx+s,y,cz-s);glVertex3d(cx+s,y,cz+s);glVertex3d(cx-s,y,cz+s);glEnd();
   glDepthMask(true);glDisable(BLEND);glDisable(FOG);
   if(samples>0){glBindFramebuffer(READ_FRAMEBUFFER,msFbo);glBindFramebuffer(DRAW_FRAMEBUFFER,fbo);glBlitFramebuffer(0,0,w,h,0,0,w,h,COLOR,NEAREST);}
   glBindFramebuffer(FRAMEBUFFER,fbo);
   if(pixels==null||pixels.Length!=w*h)pixels=new int[w*h];
   glPixelStorei(PACK_ALIGNMENT,4);glReadPixels(0,0,w,h,BGRA,UBYTE,pixels);
   glBindFramebuffer(FRAMEBUFFER,0);
   uint err=glGetError();if(err!=0)throw new InvalidOperationException("OpenGL error 0x"+err.ToString("x"));
   // OpenGL rows run bottom to top.
   var data=into.LockBits(new Rectangle(0,0,w,h),ImageLockMode.WriteOnly,PixelFormat.Format32bppArgb);
   try{for(int row=0;row<h;row++)Marshal.Copy(pixels,(h-1-row)*w,data.Scan0+row*data.Stride,w);}
   finally{into.UnlockBits(data);}
  }
  int objectsVersion=-1;
  static void Col(Color c){glColor4ub(c.R,c.G,c.B,255);}

  public void Dispose(){
   try{if(rc!=IntPtr.Zero){Current();FreeTargets();if(terrain!=null)glDeleteBuffers(2,new[]{terrain.Pos,terrain.Col});if(objects!=null)glDeleteBuffers(2,new[]{objects.Pos,objects.Col});wglMakeCurrent(IntPtr.Zero,IntPtr.Zero);wglDeleteContext(rc);rc=IntPtr.Zero;}}catch{}
   if(dc!=IntPtr.Zero){ReleaseDC(host.Handle,dc);dc=IntPtr.Zero;}
   try{host.DestroyHandle();}catch{}
  }
 }
}
