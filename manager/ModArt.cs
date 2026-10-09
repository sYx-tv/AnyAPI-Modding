using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;
namespace AnyApiManager {
 // Small illustrated tiles for mods. Known mods get a glyph that says what they do; others
 // (new catalog entries, local DLLs) get their initials on a colour derived from the id.
 public enum ModGlyph {Initials,Sliders,Bag,Crate,Pin,Sun,Clock,Wheel,Scale,Wave,Bulb,Mirror,Chart}
 public static class ModArt {
  sealed class Info {public ModGlyph Glyph;public string Category;public Info(ModGlyph g,string c){Glyph=g;Category=c;}}
  static readonly Dictionary<string,Info> known=new Dictionary<string,Info>(StringComparer.OrdinalIgnoreCase){
   {"anyhelpers",new Info(ModGlyph.Sliders,"Framework")},{"anyinventory",new Info(ModGlyph.Bag,"Inventory")},{"anystorage",new Info(ModGlyph.Crate,"Inventory")},
   {"anymap",new Info(ModGlyph.Pin,"Navigation")},{"anygraphics",new Info(ModGlyph.Sun,"Graphics")},{"anyclock",new Info(ModGlyph.Clock,"HUD")},
   {"anyquickwheel",new Info(ModGlyph.Wheel,"Building")},{"anybalance",new Info(ModGlyph.Scale,"Building")},{"enginesound",new Info(ModGlyph.Wave,"Vehicles")},
   {"anylights",new Info(ModGlyph.Bulb,"Vehicles")},{"anymirror",new Info(ModGlyph.Mirror,"Building")},{"anybuildstats",new Info(ModGlyph.Chart,"Building")}};
  public static string Category(Package p){Info i;return p.Local?"Local mod":known.TryGetValue(p.Id??"",out i)?i.Category:"Mod";}
  static ModGlyph GlyphOf(Package p){Info i;return !p.Local&&known.TryGetValue(p.Id??"",out i)?i.Glyph:ModGlyph.Initials;}
  // Stable hue per mod id so a mod keeps its colour between sessions.
  static Color Tint(Package p){if(p.Local)return Theme.Warn;int h=17;foreach(char c in p.Id??"")h=h*31+c;double hue=(h&0x7fffffff)%360;return FromHsv(hue,.55,.95);}
  static Color FromHsv(double h,double s,double v){int i=(int)(h/60)%6;double f=h/60-Math.Floor(h/60),p=v*(1-s),q=v*(1-f*s),t=v*(1-(1-f)*s);double r,g,b;
   switch(i){case 0:r=v;g=t;b=p;break;case 1:r=q;g=v;b=p;break;case 2:r=p;g=v;b=t;break;case 3:r=p;g=q;b=v;break;case 4:r=t;g=p;b=v;break;default:r=v;g=p;b=q;break;}
   return Color.FromArgb((int)(r*255),(int)(g*255),(int)(b*255));}
  public static void Draw(Graphics g,Rectangle r,Package p,Font initials){
   g.SmoothingMode=SmoothingMode.AntiAlias;Color tint=Tint(p);
   using(var path=Theme.Rounded(r,Math.Max(3,r.Width/5f)))using(var fill=new LinearGradientBrush(r,Theme.Mix(tint,Theme.Raised,.30f),Theme.Mix(tint,Theme.Raised,.10f),45f))using(var edge=new Pen(Theme.Mix(tint,Theme.Raised,.35f))){g.FillPath(fill,path);g.DrawPath(edge,path);}
   var glyph=GlyphOf(p);if(glyph==ModGlyph.Initials){TextRenderer.DrawText(g,ModList.Initials(p.Name??"?"),initials,r,tint,TextFormatFlags.HorizontalCenter|TextFormatFlags.VerticalCenter);return;}
   int inset=r.Width/5;var box=new Rectangle(r.X+inset,r.Y+inset,r.Width-inset*2,r.Height-inset*2);float u=box.Width/16f;Func<float,float,PointF> P=(x,y)=>new PointF(box.X+x*u,box.Y+y*u);
   using(var pen=new Pen(tint,Math.Max(1.2f,1.5f*u)){LineJoin=LineJoin.Round,StartCap=LineCap.Round,EndCap=LineCap.Round})using(var dot=new SolidBrush(tint)){
    switch(glyph){
     case ModGlyph.Sliders:foreach(var row in new[]{new[]{3f,10f},new[]{8f,5f},new[]{13f,11f}}){g.DrawLine(pen,P(1,row[0]),P(15,row[0]));g.FillEllipse(dot,box.X+row[1]*u-2*u,box.Y+row[0]*u-2*u,4*u,4*u);}break;
     case ModGlyph.Bag:using(var path=Theme.Rounded(new RectangleF(P(2,5).X,P(2,5).Y,12*u,10*u),1.5f*u))g.DrawPath(pen,path);g.DrawArc(pen,P(5,1).X,P(5,1).Y,6*u,7*u,180,180);break;
     case ModGlyph.Crate:g.DrawRectangle(pen,P(2,3).X,P(2,3).Y,12*u,11*u);g.DrawLine(pen,P(2,7),P(14,7));g.DrawLine(pen,P(6.5f,7),P(6.5f,10));g.DrawLine(pen,P(9.5f,7),P(9.5f,10));g.DrawLine(pen,P(6.5f,10),P(9.5f,10));break;
     case ModGlyph.Pin:using(var path=new GraphicsPath()){path.AddArc(P(3,1).X,P(3,1).Y,10*u,10*u,150,240);path.AddLine(P(12.3f,8.5f),P(8,15));path.AddLine(P(8,15),P(3.7f,8.5f));g.DrawPath(pen,path);}g.DrawEllipse(pen,P(6,4).X,P(6,4).Y,4*u,4*u);break;
     case ModGlyph.Sun:g.DrawEllipse(pen,P(5,5).X,P(5,5).Y,6*u,6*u);for(int i=0;i<8;i++){double a=i*Math.PI/4;g.DrawLine(pen,P(8+(float)Math.Cos(a)*5,8+(float)Math.Sin(a)*5),P(8+(float)Math.Cos(a)*7.2f,8+(float)Math.Sin(a)*7.2f));}break;
     case ModGlyph.Clock:g.DrawEllipse(pen,P(1,1).X,P(1,1).Y,14*u,14*u);g.DrawLines(pen,new[]{P(8,4),P(8,8),P(11,10)});break;
     case ModGlyph.Wheel:g.DrawEllipse(pen,P(1,1).X,P(1,1).Y,14*u,14*u);g.FillEllipse(dot,P(6.5f,6.5f).X,P(6.5f,6.5f).Y,3*u,3*u);for(int i=0;i<5;i++){double a=-Math.PI/2+i*2*Math.PI/5;g.DrawLine(pen,P(8+(float)Math.Cos(a)*2.5f,8+(float)Math.Sin(a)*2.5f),P(8+(float)Math.Cos(a)*7,8+(float)Math.Sin(a)*7));}break;
     case ModGlyph.Scale:g.DrawLine(pen,P(8,2),P(8,14));g.DrawLine(pen,P(5,14),P(11,14));g.DrawLine(pen,P(2,4),P(14,4));g.DrawLines(pen,new[]{P(0.5f,10),P(2.5f,4.5f),P(4.5f,10)});g.DrawLine(pen,P(0.5f,10),P(4.5f,10));g.DrawLines(pen,new[]{P(11.5f,10),P(13.5f,4.5f),P(15.5f,10)});g.DrawLine(pen,P(11.5f,10),P(15.5f,10));break;
     case ModGlyph.Wave:float[] h={3,7,11,6,13,8,4};for(int i=0;i<h.Length;i++){float x=1.5f+i*2.15f;g.DrawLine(pen,P(x,8-h[i]/2),P(x,8+h[i]/2));}break;
     case ModGlyph.Bulb:g.DrawEllipse(pen,P(3,1).X,P(3,1).Y,10*u,10*u);g.DrawLine(pen,P(6,13),P(10,13));g.DrawLine(pen,P(6.5f,15),P(9.5f,15));break;
     case ModGlyph.Mirror:using(var dash=new Pen(tint,Math.Max(1f,u)){DashStyle=DashStyle.Dash})g.DrawLine(dash,P(8,0.5f),P(8,15.5f));g.DrawPolygon(pen,new[]{P(1,4),P(6,8),P(1,12)});g.DrawPolygon(pen,new[]{P(15,4),P(10,8),P(15,12)});break;
     case ModGlyph.Chart:g.DrawLine(pen,P(1,15),P(15,15));g.DrawLine(pen,P(4,15),P(4,9));g.DrawLine(pen,P(8,15),P(8,4));g.DrawLine(pen,P(12,15),P(12,7));break;
    }
   }
  }
 }

 // Large tile shown in the Mods details panel.
 public sealed class ModTile:Control {
  Package package;readonly Font font=Theme.Mono(17);
  public ModTile(){Size=new Size(Theme.S(56),Theme.S(56));SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer|ControlStyles.ResizeRedraw,true);}
  public Package Package {get{return package;}set{package=value;Visible=value!=null;Invalidate();}}
  protected override void OnPaint(PaintEventArgs e){e.Graphics.Clear(Parent==null?Theme.Panel:Parent.BackColor);if(package!=null)ModArt.Draw(e.Graphics,new Rectangle(0,0,Width-1,Height-1),package,font);}
 }
}
