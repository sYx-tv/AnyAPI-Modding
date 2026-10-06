using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;
namespace AnyApiManager {
 public static class Theme {
  public static readonly Color Background=Color.FromArgb(14,18,25),Surface=Color.FromArgb(23,29,39),Raised=Color.FromArgb(31,39,52),Ink=Color.FromArgb(236,241,249),Muted=Color.FromArgb(153,168,190),Blue=Color.FromArgb(77,148,246),Border=Color.FromArgb(39,49,65),Selected=Color.FromArgb(29,53,86);
  public static Font Font(float px,bool bold=false){return new Font("Segoe UI",px,bold?FontStyle.Bold:FontStyle.Regular,GraphicsUnit.Pixel);}
  public static GraphicsPath Rounded(RectangleF r,float radius){float d=radius*2;var p=new GraphicsPath();p.AddArc(r.X,r.Y,d,d,180,90);p.AddArc(r.Right-d,r.Y,d,d,270,90);p.AddArc(r.Right-d,r.Bottom-d,d,d,0,90);p.AddArc(r.X,r.Bottom-d,d,d,90,90);p.CloseFigure();return p;}
 }
 public sealed class Card:Panel {
  public Card(){BackColor=Theme.Surface;Padding=new Padding(22);DoubleBuffered=true;}
  protected override void OnPaintBackground(PaintEventArgs e){e.Graphics.Clear(Parent==null?Theme.Background:Parent.BackColor);e.Graphics.SmoothingMode=SmoothingMode.AntiAlias;using(var p=Theme.Rounded(new RectangleF(0,0,Width-1,Height-1),12))using(var fill=new SolidBrush(BackColor))using(var border=new Pen(Theme.Border)){e.Graphics.FillPath(fill,p);e.Graphics.DrawPath(border,p);}}
 }
}
