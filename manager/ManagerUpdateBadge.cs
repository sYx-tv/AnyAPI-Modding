using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace AnyApiManager {
 // Title-bar chip bound to a ManagerUpdater: "Manager X is ready  [Restart to update]".
 // Hidden until an update exists; the button fills with download progress. Clicking raises
 // Install. The window owns placement (it re-lays out on SizeChanged); this control only
 // reflects updater state.
 public sealed class ManagerUpdateBadge:Control {
  readonly ManagerUpdater updater;bool hovering;readonly Font label=Theme.Font(12),button=Theme.Semibold(11.5f);
  public event Action Install;
  public ManagerUpdateBadge(ManagerUpdater updater){
   this.updater=updater;Visible=false;Cursor=Cursors.Hand;Height=Theme.S(24);
   SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer|ControlStyles.ResizeRedraw,true);
   updater.Changed+=Sync;Sync();
  }
  void Sync(){Visible=updater.UpdateAvailable;Enabled=updater.State==ManagerUpdateState.Available||updater.State==ManagerUpdateState.Failed;Cursor=Enabled?Cursors.Hand:Cursors.Default;Width=PreferredWidth;Invalidate();}
  public string Lead {get{return updater.Release==null?"":updater.State==ManagerUpdateState.Failed?"Update didn't finish":"Manager "+updater.Release.Version+" is ready";}}
  public string Caption {get{
   switch(updater.State){
    case ManagerUpdateState.Downloading:return updater.Progress<0?"Downloading...":"Downloading "+(int)(updater.Progress*100)+"%";
    case ManagerUpdateState.Restarting:return "Restarting...";
    case ManagerUpdateState.Failed:return "Retry update";
    default:return updater.Release==null?"":"Restart to update";
   }
  }}
  int ButtonWidth {get{return TextRenderer.MeasureText("Downloading 100%",button).Width+Theme.S(14);}}
  int PreferredWidth {get{return Theme.S(10)+TextRenderer.MeasureText(Lead,label).Width+Theme.S(8)+ButtonWidth+Theme.S(3);}}
  protected override void OnMouseEnter(EventArgs e){hovering=true;Invalidate();base.OnMouseEnter(e);}
  protected override void OnMouseLeave(EventArgs e){hovering=false;Invalidate();base.OnMouseLeave(e);}
  protected override void OnClick(EventArgs e){base.OnClick(e);if(Enabled&&Install!=null)Install();}
  protected override void Dispose(bool disposing){if(disposing)updater.Changed-=Sync;base.Dispose(disposing);}
  protected override void OnPaint(PaintEventArgs e){
   var g=e.Graphics;g.Clear(Parent==null?Theme.Side:Parent.BackColor);g.SmoothingMode=SmoothingMode.AntiAlias;Color accent=updater.State==ManagerUpdateState.Failed?Theme.Warn:Theme.Blue;
   using(var path=Theme.Rounded(new RectangleF(.5f,.5f,Width-1.5f,Height-1.5f),Theme.S(5)))using(var fill=new SolidBrush(Theme.Mix(accent,Theme.Side,.12f)))using(var border=new Pen(Theme.Mix(accent,Theme.Side,.38f))){g.FillPath(fill,path);g.DrawPath(border,path);}
   TextRenderer.DrawText(g,Lead,label,new Rectangle(Theme.S(10),0,Width,Height),Theme.Ink,TextFormatFlags.VerticalCenter|TextFormatFlags.Left|TextFormatFlags.SingleLine);
   var b=new RectangleF(Width-ButtonWidth-Theme.S(3),Theme.S(3),ButtonWidth,Height-Theme.S(6));Color fillColor=hovering&&Enabled?ControlPaint.Light(accent,.12f):accent;
   using(var path=Theme.Rounded(b,Theme.S(3))){
    using(var track=new SolidBrush(Theme.Mix(accent,Theme.Side,.35f)))g.FillPath(track,path);
    // Progress fills the button left to right; other states fill it completely.
    double p=updater.State==ManagerUpdateState.Downloading?Math.Max(0,updater.Progress):1;
    if(p>0){var clip=g.Clip;g.SetClip(new RectangleF(b.X,0,(float)(b.Width*p),Height));using(var bar=new SolidBrush(fillColor))g.FillPath(bar,path);g.Clip=clip;}
   }
   TextRenderer.DrawText(g,Caption,button,Rectangle.Round(b),Theme.AccentInk,TextFormatFlags.VerticalCenter|TextFormatFlags.HorizontalCenter|TextFormatFlags.SingleLine);
  }
 }
}
