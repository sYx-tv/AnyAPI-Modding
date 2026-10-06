using System.Drawing;
using System.Windows.Forms;
namespace AnyApiManager {
 public sealed class ModernButton:Button {
  bool hovering;
  public ModernButton(){SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer,true);}
  protected override void OnMouseEnter(System.EventArgs e){hovering=true;Invalidate();base.OnMouseEnter(e);}
  protected override void OnMouseLeave(System.EventArgs e){hovering=false;Invalidate();base.OnMouseLeave(e);}
  protected override void OnPaint(PaintEventArgs e){Color fill=Enabled?hovering?ControlPaint.Light(BackColor,.12f):BackColor:Color.FromArgb(32,40,51);using(var b=new SolidBrush(fill))e.Graphics.FillRectangle(b,ClientRectangle);TextRenderer.DrawText(e.Graphics,Text,Font,ClientRectangle,Enabled?ForeColor:Color.FromArgb(134,148,167),TextFormatFlags.HorizontalCenter|TextFormatFlags.VerticalCenter|TextFormatFlags.SingleLine);if(Focused&&Enabled){using(var pen=new Pen(Color.FromArgb(72,149,242)))e.Graphics.DrawRectangle(pen,1,1,Width-3,Height-3);}}
 }
}
