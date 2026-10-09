using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;
namespace AnyApiManager {
 // Manager 1.4 look: a dark, dense window with titled panels and one user-chosen accent.
 // Every size goes through S() so layout follows the Windows display scale (100-200%).
 public sealed class Accent {public string Name;public Color Color,Ink;}
 public static class Theme {
  public static readonly Color Window=Color.FromArgb(9,12,18),Side=Color.FromArgb(11,15,23),Panel=Color.FromArgb(14,19,28),Raised=Color.FromArgb(21,27,39),Line=Color.FromArgb(26,34,48),
   Ink=Color.FromArgb(230,235,243),Muted=Color.FromArgb(125,136,153),Dim=Color.FromArgb(75,85,102),Ok=Color.FromArgb(61,220,151),Warn=Color.FromArgb(242,184,75),Bad=Color.FromArgb(255,93,108),SwitchOff=Color.FromArgb(34,43,58);
  // Older names kept for the shared controls.
  public static readonly Color Background=Window,Surface=Panel,Border=Line;
  public static readonly Accent[] Accents={
   new Accent{Name="Aqua",Color=Color.FromArgb(56,200,240),Ink=Color.FromArgb(3,16,24)},
   new Accent{Name="Violet",Color=Color.FromArgb(143,125,255),Ink=Color.FromArgb(11,7,32)},
   new Accent{Name="Rose",Color=Color.FromArgb(255,95,143),Ink=Color.FromArgb(31,5,16)},
   new Accent{Name="Amber",Color=Color.FromArgb(245,181,68),Ink=Color.FromArgb(29,18,3)},
   new Accent{Name="Mint",Color=Color.FromArgb(67,224,164),Ink=Color.FromArgb(3,23,15)}};
  public static Accent Current=Accents[0];
  public static Color Blue {get{return Current.Color;}}
  public static Color AccentInk {get{return Current.Ink;}}
  public static Color Selected {get{return Mix(Current.Color,Window,.14f);}}
  public static Accent FindAccent(string name){foreach(var a in Accents)if(string.Equals(a.Name,name,StringComparison.OrdinalIgnoreCase))return a;return Accents[0];}
  public static Color Mix(Color a,Color b,float amount){return Color.FromArgb((int)(b.R+(a.R-b.R)*amount),(int)(b.G+(a.G-b.G)*amount),(int)(b.B+(a.B-b.B)*amount));}

  public static float Scale=1;
  public static void InitScale(){try{using(var g=Graphics.FromHwnd(IntPtr.Zero))Scale=Math.Max(1f,g.DpiX/96f);}catch{Scale=1;}}
  public static int S(float px){return (int)Math.Round(px*Scale);}
  public static Font Font(float px,bool bold=false){return new Font("Segoe UI",px*Scale,bold?FontStyle.Bold:FontStyle.Regular,GraphicsUnit.Pixel);}
  public static Font Semibold(float px){try{return new Font("Segoe UI Semibold",px*Scale,FontStyle.Regular,GraphicsUnit.Pixel);}catch{return Font(px,true);}}
  public static Font Mono(float px){return new Font("Consolas",px*Scale,FontStyle.Regular,GraphicsUnit.Pixel);}
  public static GraphicsPath Rounded(RectangleF r,float radius){float d=radius*2;var p=new GraphicsPath();if(d<=0){p.AddRectangle(r);return p;}p.AddArc(r.X,r.Y,d,d,180,90);p.AddArc(r.Right-d,r.Y,d,d,270,90);p.AddArc(r.Right-d,r.Bottom-d,d,d,0,90);p.AddArc(r.X,r.Bottom-d,d,d,90,90);p.CloseFigure();return p;}
  // Uppercase, letter-spaced caption used for panel headers and sidebar groups.
  public static void Caption(Graphics g,string text,Font font,Color color,int x,int y,int height){
   float left=x;foreach(char c in text.ToUpperInvariant()){string s=c.ToString();TextRenderer.DrawText(g,s,font,new Point((int)left,y+(height-font.Height)/2),color,TextFormatFlags.NoPadding);left+=TextRenderer.MeasureText(g,s,font,Size.Empty,TextFormatFlags.NoPadding).Width+font.Size*.14f;}
  }
  // Small rounded status tag (Up to date, Enabled, Local ...).
  public static int Pill(Graphics g,string text,Font font,Color color,int right,int centerY,bool filledRight=true){
   var size=TextRenderer.MeasureText(g,text,font,Size.Empty,TextFormatFlags.NoPadding);int w=size.Width+S(16),h=S(20);var r=new Rectangle(filledRight?right-w:right,centerY-h/2,w,h);
   g.SmoothingMode=SmoothingMode.AntiAlias;using(var path=Rounded(r,h/2f))using(var b=new SolidBrush(color==Muted?Raised:Mix(color,Panel,.13f)))g.FillPath(b,path);
   TextRenderer.DrawText(g,text,font,r,color,TextFormatFlags.HorizontalCenter|TextFormatFlags.VerticalCenter|TextFormatFlags.NoPadding);return w;
  }
  public static void Dot(Graphics g,Color color,int cx,int cy,int size){g.SmoothingMode=SmoothingMode.AntiAlias;using(var glow=new SolidBrush(Color.FromArgb(60,color)))g.FillEllipse(glow,cx-size,cy-size,size*2,size*2);using(var b=new SolidBrush(color))g.FillEllipse(b,cx-size/2f,cy-size/2f,size,size);}
 }

 // Titled group panel. Children are laid out by the owner below HeaderHeight.
 public sealed class Box:Panel {
  public string Title;public string Note="";public Color NoteColor=Theme.Muted;public bool NotePill;
  public static int HeaderHeight {get{return Theme.S(36);}}
  readonly Font caption=Theme.Semibold(11),note=Theme.Font(12),pill=Theme.Semibold(11);
  public Box(string title){Title=title;BackColor=Theme.Panel;ForeColor=Theme.Ink;SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer|ControlStyles.ResizeRedraw,true);}
  public void SetNote(string text,Color color,bool asPill){Note=text??"";NoteColor=color;NotePill=asPill;Invalidate(new Rectangle(0,0,Width,HeaderHeight));}
  protected override void OnPaintBackground(PaintEventArgs e){
   var g=e.Graphics;g.Clear(Parent==null?Theme.Window:Parent.BackColor);g.SmoothingMode=SmoothingMode.AntiAlias;
   using(var p=Theme.Rounded(new RectangleF(0,0,Width-1,Height-1),Theme.S(8)))using(var fill=new SolidBrush(Theme.Panel))using(var border=new Pen(Theme.Line)){g.FillPath(fill,p);g.DrawPath(border,p);}
   int h=HeaderHeight;using(var line=new Pen(Theme.Line))g.DrawLine(line,1,h,Width-2,h);
   Theme.Caption(g,Title,caption,Theme.Muted,Theme.S(14),0,h);
   if(Note!=""){if(NotePill)Theme.Pill(g,Note,pill,NoteColor,Width-Theme.S(12),h/2);else TextRenderer.DrawText(g,Note,note,new Rectangle(Theme.S(120),0,Width-Theme.S(134),h),NoteColor,TextFormatFlags.Right|TextFormatFlags.VerticalCenter|TextFormatFlags.EndEllipsis|TextFormatFlags.SingleLine);}
  }
 }
}
