using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;
namespace AnyApiManager {
 public enum ButtonKind {Secondary,Primary,Ghost,Danger,Nav,Hero}
 public enum NavIcon {None,Home,Grid,Code,Gear}
 public sealed class ModernButton:Button {
  bool hovering;public bool Selected;public ButtonKind Kind=ButtonKind.Secondary;public NavIcon Icon=NavIcon.None;public string Badge="";
  // Kept for older call sites: AlignLeft draws as a sidebar item.
  public bool AlignLeft {get{return Kind==ButtonKind.Nav;}set{if(value)Kind=ButtonKind.Nav;}}
  public ModernButton(){SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer|ControlStyles.ResizeRedraw,true);FlatStyle=FlatStyle.Flat;FlatAppearance.BorderSize=0;Cursor=Cursors.Hand;Font=Theme.Semibold(12.5f);}
  protected override void OnMouseEnter(EventArgs e){hovering=true;Invalidate();base.OnMouseEnter(e);}
  protected override void OnMouseLeave(EventArgs e){hovering=false;Invalidate();base.OnMouseLeave(e);}
  protected override void OnEnabledChanged(EventArgs e){Cursor=Enabled?Cursors.Hand:Cursors.Default;base.OnEnabledChanged(e);}
  protected override void OnPaint(PaintEventArgs e){
   var g=e.Graphics;g.Clear(Parent==null?Theme.Window:Parent.BackColor);g.SmoothingMode=SmoothingMode.AntiAlias;
   var r=new RectangleF(.5f,.5f,Width-1.5f,Height-1.5f);Color fill=Color.Empty,border=Color.Empty,ink=Theme.Ink;
   switch(Kind){
    case ButtonKind.Primary:case ButtonKind.Hero:fill=hovering?ControlPaint.Light(Theme.Blue,.12f):Theme.Blue;ink=Theme.AccentInk;break;
    case ButtonKind.Secondary:fill=Theme.Raised;border=hovering?Theme.Mix(Theme.Ink,Theme.Raised,.12f):Theme.Line;break;
    case ButtonKind.Ghost:border=hovering?Theme.Mix(Theme.Ink,Theme.Panel,.18f):Theme.Line;break;
    case ButtonKind.Danger:fill=Theme.Raised;border=hovering?Theme.Bad:Theme.Line;ink=hovering?Theme.Bad:Theme.Ink;break;
    case ButtonKind.Nav:fill=Selected?Theme.Selected:hovering?Color.FromArgb(18,24,35):Color.Empty;ink=Selected||hovering?Theme.Ink:Theme.Muted;break;
   }
   if(!Enabled){ink=Theme.Dim;if(Kind==ButtonKind.Primary||Kind==ButtonKind.Hero){fill=Theme.Raised;}}
   using(var path=Theme.Rounded(r,Theme.S(Kind==ButtonKind.Hero?7:6))){
    if(Kind==ButtonKind.Hero&&Enabled)using(var lg=new LinearGradientBrush(new Rectangle(0,0,Width,Height),ControlPaint.Light(fill,.25f),fill,90f))g.FillPath(lg,path);
    else if(fill!=Color.Empty)using(var b=new SolidBrush(fill))g.FillPath(b,path);
    if(border!=Color.Empty)using(var p=new Pen(border))g.DrawPath(p,path);
    if(Focused&&ShowFocusCues&&Enabled)using(var p=new Pen(Theme.Blue))g.DrawPath(p,path);
   }
   if(Kind==ButtonKind.Nav){
    int ix=Theme.S(10),size=Theme.S(16);DrawIcon(g,Icon,new Rectangle(ix,(Height-size)/2,size,size),Selected?Theme.Blue:ink);
    var text=new Rectangle(ix+size+Theme.S(10),0,Width-ix-size-Theme.S(14),Height);
    TextRenderer.DrawText(g,Text,Font,text,ink,TextFormatFlags.VerticalCenter|TextFormatFlags.Left|TextFormatFlags.SingleLine|TextFormatFlags.EndEllipsis);
    if(Badge!="")using(var f=Theme.Mono(10.5f)){var sz=TextRenderer.MeasureText(g,Badge,f);var br=new Rectangle(Width-sz.Width-Theme.S(10),(Height-Theme.S(17))/2,sz.Width,Theme.S(17));using(var p=Theme.Rounded(br,Theme.S(3)))using(var b=new SolidBrush(Theme.Raised))g.FillPath(b,p);TextRenderer.DrawText(g,Badge,f,br,Theme.Muted,TextFormatFlags.HorizontalCenter|TextFormatFlags.VerticalCenter);}
    return;
   }
   var font=Kind==ButtonKind.Hero?Theme.Font(13.5f,true):Font;string label=Kind==ButtonKind.Hero?Text.ToUpperInvariant():Text;
   TextRenderer.DrawText(g,label,font,new Rectangle(Theme.S(8),0,Width-Theme.S(16),Height),ink,TextFormatFlags.VerticalCenter|TextFormatFlags.HorizontalCenter|TextFormatFlags.SingleLine|TextFormatFlags.EndEllipsis);
   if(Kind==ButtonKind.Hero)font.Dispose();
  }
  // Line icons drawn with GDI+ so nothing depends on Windows 11 icon fonts.
  public static void DrawIcon(Graphics g,NavIcon icon,Rectangle r,Color color){
   if(icon==NavIcon.None)return;g.SmoothingMode=SmoothingMode.AntiAlias;float u=r.Width/16f;Func<float,float,PointF> P=(x,y)=>new PointF(r.X+x*u,r.Y+y*u);
   using(var pen=new Pen(color,Math.Max(1.2f,1.6f*u)){LineJoin=LineJoin.Round,StartCap=LineCap.Round,EndCap=LineCap.Round}){
    switch(icon){
     case NavIcon.Home:g.DrawLines(pen,new[]{P(2,7.5f),P(8,2.5f),P(14,7.5f),P(14,14),P(2,14),P(2,7.5f)});g.DrawLines(pen,new[]{P(6.5f,14),P(6.5f,10),P(9.5f,10),P(9.5f,14)});break;
     case NavIcon.Grid:foreach(var o in new[]{P(2,2),P(9,2),P(2,9),P(9,9)})using(var path=Theme.Rounded(new RectangleF(o.X,o.Y,5*u,5*u),u))g.DrawPath(pen,path);break;
     case NavIcon.Code:g.DrawLines(pen,new[]{P(5.5f,4),P(2,8),P(5.5f,12)});g.DrawLines(pen,new[]{P(10.5f,4),P(14,8),P(10.5f,12)});break;
     case NavIcon.Gear:g.DrawEllipse(pen,r.X+5.8f*u,r.Y+5.8f*u,4.4f*u,4.4f*u);for(int i=0;i<8;i++){double a=i*Math.PI/4;g.DrawLine(pen,P(8+(float)Math.Cos(a)*4.6f,8+(float)Math.Sin(a)*4.6f),P(8+(float)Math.Cos(a)*6.4f,8+(float)Math.Sin(a)*6.4f));}break;
    }
   }
  }
 }
}
