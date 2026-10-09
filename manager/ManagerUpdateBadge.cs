using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace AnyApiManager {
 // Minimal "update available" control bound to a ManagerUpdater. Hidden until an update exists;
 // shows the version, download progress and restart state. Clicking raises Install. The window
 // owns layout and restyling; this control only reflects updater state.
 public sealed class ManagerUpdateBadge:Control {
  readonly ManagerUpdater updater;bool hovering;
  public event Action Install;
  public ManagerUpdateBadge(ManagerUpdater updater){
   this.updater=updater;Visible=false;Cursor=Cursors.Hand;Font=Theme.Font(13,true);
   SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer|ControlStyles.ResizeRedraw,true);
   updater.Changed+=Sync;Sync();
  }
  void Sync(){Visible=updater.UpdateAvailable;Enabled=updater.State==ManagerUpdateState.Available||updater.State==ManagerUpdateState.Failed;Cursor=Enabled?Cursors.Hand:Cursors.Default;Invalidate();}
  public string Caption {get{
   switch(updater.State){
    case ManagerUpdateState.Downloading:return updater.Progress<0?"Downloading...":"Downloading "+(int)(updater.Progress*100)+"%";
    case ManagerUpdateState.Restarting:return "Restarting...";
    case ManagerUpdateState.Failed:return "Retry update";
    default:return updater.Release==null?"":"Update to "+updater.Release.Version;
   }
  }}
  protected override void OnMouseEnter(EventArgs e){hovering=true;Invalidate();base.OnMouseEnter(e);}
  protected override void OnMouseLeave(EventArgs e){hovering=false;Invalidate();base.OnMouseLeave(e);}
  protected override void OnClick(EventArgs e){base.OnClick(e);if(Enabled&&Install!=null)Install();}
  protected override void Dispose(bool disposing){if(disposing)updater.Changed-=Sync;base.Dispose(disposing);}
  protected override void OnPaint(PaintEventArgs e){
   var g=e.Graphics;g.Clear(Parent==null?Theme.Background:Parent.BackColor);g.SmoothingMode=SmoothingMode.AntiAlias;
   var bounds=new RectangleF(0,0,Width-1,Height-1);Color accent=hovering&&Enabled?ControlPaint.Light(Theme.Blue,.15f):Theme.Blue;
   using(var path=Theme.Rounded(bounds,8)){
    using(var fill=new SolidBrush(Theme.Selected))g.FillPath(fill,path);
    // Progress fills the badge left to right; idle and failed states fill it completely.
    double p=updater.State==ManagerUpdateState.Downloading?Math.Max(0,updater.Progress):1;
    if(p>0){var clip=g.Clip;g.SetClip(new RectangleF(0,0,(float)(Width*p),Height));using(var bar=new SolidBrush(accent))g.FillPath(bar,path);g.Clip=clip;}
   }
   TextRenderer.DrawText(g,Caption,Font,ClientRectangle,Theme.Ink,TextFormatFlags.VerticalCenter|TextFormatFlags.HorizontalCenter|TextFormatFlags.SingleLine|TextFormatFlags.EndEllipsis);
  }
 }
}
