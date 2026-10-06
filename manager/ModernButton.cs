using System.Drawing;
using System.Windows.Forms;
namespace AnyApiManager {
 public sealed class ModernButton:Button {
  bool hovering;public bool Selected;public bool AlignLeft;public string Glyph="";
  public ModernButton(){SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer,true);}
  protected override void OnMouseEnter(System.EventArgs e){hovering=true;Invalidate();base.OnMouseEnter(e);}
  protected override void OnMouseLeave(System.EventArgs e){hovering=false;Invalidate();base.OnMouseLeave(e);}
  protected override void OnPaint(PaintEventArgs e){
   e.Graphics.Clear(Parent==null?Theme.Background:Parent.BackColor);e.Graphics.SmoothingMode=System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
   Color fill=Enabled?Selected?Theme.Selected:hovering?ControlPaint.Light(BackColor,.12f):BackColor:Theme.Raised;
   using(var path=Theme.Rounded(new RectangleF(0,0,Width-1,Height-1),8))using(var brush=new SolidBrush(fill)){e.Graphics.FillPath(brush,path);if(Focused&&Enabled)using(var pen=new Pen(Theme.Blue))e.Graphics.DrawPath(pen,path);}
   Color ink=Enabled?ForeColor:Theme.Muted;var bounds=new Rectangle(AlignLeft?18:0,0,Width-(AlignLeft?24:0),Height);
   if(Glyph!=""){TextRenderer.DrawText(e.Graphics,Glyph,Font,new Rectangle(16,0,26,Height),Selected?Theme.Blue:ink,TextFormatFlags.VerticalCenter|TextFormatFlags.HorizontalCenter);bounds.X=52;bounds.Width=Width-62;}
   TextRenderer.DrawText(e.Graphics,Text,Font,bounds,ink,TextFormatFlags.VerticalCenter|TextFormatFlags.SingleLine|TextFormatFlags.EndEllipsis|(AlignLeft?TextFormatFlags.Left:TextFormatFlags.HorizontalCenter));
  }
 }
}
