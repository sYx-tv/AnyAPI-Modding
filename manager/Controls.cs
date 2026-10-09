using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;
namespace AnyApiManager {
 // On/off switch. Toggled raises when the user flips it; Checked can also be set from code.
 public sealed class ToggleSwitch:Control {
  bool on,hovering;public event Action Toggled;
  public bool Checked {get{return on;}set{if(on!=value){on=value;Invalidate();}}}
  public ToggleSwitch(){Size=new Size(Theme.S(30),Theme.S(16));Cursor=Cursors.Hand;SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer|ControlStyles.Selectable,true);TabStop=true;}
  protected override void OnMouseEnter(EventArgs e){hovering=true;Invalidate();base.OnMouseEnter(e);}
  protected override void OnMouseLeave(EventArgs e){hovering=false;Invalidate();base.OnMouseLeave(e);}
  protected override void OnClick(EventArgs e){base.OnClick(e);Flip();}
  protected override void OnKeyDown(KeyEventArgs e){base.OnKeyDown(e);if(e.KeyCode==Keys.Space||e.KeyCode==Keys.Enter)Flip();}
  void Flip(){if(!Enabled)return;on=!on;Invalidate();if(Toggled!=null)Toggled();}
  protected override void OnPaint(PaintEventArgs e){Draw(e.Graphics,ClientRectangle,on,Enabled,hovering,Parent==null?Theme.Panel:Parent.BackColor);}
  public static void Draw(Graphics g,Rectangle r,bool on,bool enabled,bool hover,Color ground){
   g.SmoothingMode=SmoothingMode.AntiAlias;using(var clear=new SolidBrush(ground))g.FillRectangle(clear,r);
   Color track=on?Theme.Mix(Theme.Blue,Theme.Side,.42f):hover?Theme.Mix(Theme.Ink,Theme.SwitchOff,.08f):Theme.SwitchOff,knob=on?Theme.Blue:Theme.Muted;
   if(!enabled){track=Theme.Raised;knob=Theme.Dim;}
   var t=new RectangleF(r.X,r.Y,r.Width-1,r.Height-1);using(var p=Theme.Rounded(t,t.Height/2))using(var b=new SolidBrush(track))g.FillPath(b,p);
   float k=r.Height-Theme.S(4);float x=on?r.Right-k-Theme.S(2)-1:r.X+Theme.S(2);using(var b=new SolidBrush(knob))g.FillEllipse(b,x,r.Y+Theme.S(2),k,k);
  }
 }

 // Small tab strip (Library / Installed / Local).
 public sealed class Segmented:Control {
  readonly string[] items;readonly Rectangle[] rects;int selected,hover=-1;public event Action Changed;
  public int SelectedIndex {get{return selected;}}
  public Segmented(params string[] items){this.items=items;rects=new Rectangle[items.Length];Font=Theme.Semibold(12);Cursor=Cursors.Hand;SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer|ControlStyles.ResizeRedraw,true);
   int x=Theme.S(2);for(int i=0;i<items.Length;i++){int w=TextRenderer.MeasureText(items[i],Font).Width+Theme.S(16);rects[i]=new Rectangle(x,Theme.S(2),w,Theme.S(26));x+=w;}Size=new Size(x+Theme.S(2),Theme.S(30));}
  Rectangle Item(int index){return rects[index];}
  int At(Point p){for(int i=0;i<items.Length;i++)if(Item(i).Contains(p))return i;return -1;}
  protected override void OnMouseMove(MouseEventArgs e){int h=At(e.Location);if(h!=hover){hover=h;Invalidate();}base.OnMouseMove(e);}
  protected override void OnMouseLeave(EventArgs e){hover=-1;Invalidate();base.OnMouseLeave(e);}
  protected override void OnMouseClick(MouseEventArgs e){int i=At(e.Location);if(i>=0&&i!=selected){selected=i;Invalidate();if(Changed!=null)Changed();}base.OnMouseClick(e);}
  protected override void OnPaint(PaintEventArgs e){
   var g=e.Graphics;g.Clear(Parent==null?Theme.Window:Parent.BackColor);g.SmoothingMode=SmoothingMode.AntiAlias;
   using(var p=Theme.Rounded(new RectangleF(0,0,Width-1,Height-1),Theme.S(6)))using(var b=new SolidBrush(Theme.Raised))g.FillPath(b,p);
   for(int i=0;i<items.Length;i++){var r=Item(i);if(i==selected)using(var p=Theme.Rounded(r,Theme.S(4)))using(var b=new SolidBrush(Theme.Panel))g.FillPath(b,p);
    TextRenderer.DrawText(g,items[i],Font,r,i==selected||i==hover?Theme.Ink:Theme.Muted,TextFormatFlags.HorizontalCenter|TextFormatFlags.VerticalCenter|TextFormatFlags.SingleLine);}
  }
 }

 // Accent colour picker.
 public sealed class SwatchPicker:Control {
  public event Action<Accent> Picked;
  public SwatchPicker(){Size=new Size(Theme.Accents.Length*Theme.S(30),Theme.S(24));Cursor=Cursors.Hand;SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer,true);}
  protected override void OnMouseClick(MouseEventArgs e){int i=e.X/Theme.S(30);if(i>=0&&i<Theme.Accents.Length&&Picked!=null)Picked(Theme.Accents[i]);base.OnMouseClick(e);}
  protected override void OnMouseMove(MouseEventArgs e){int i=e.X/Theme.S(30);string tip=i>=0&&i<Theme.Accents.Length?Theme.Accents[i].Name:"";if(AccessibleDescription!=tip){AccessibleDescription=tip;}base.OnMouseMove(e);}
  protected override void OnPaint(PaintEventArgs e){
   var g=e.Graphics;g.Clear(Parent==null?Theme.Panel:Parent.BackColor);g.SmoothingMode=SmoothingMode.AntiAlias;int s=Theme.S(22);
   for(int i=0;i<Theme.Accents.Length;i++){var r=new RectangleF(i*Theme.S(30)+1,1,s,s);using(var p=Theme.Rounded(r,Theme.S(5)))using(var b=new SolidBrush(Theme.Accents[i].Color))g.FillPath(b,p);
    if(Theme.Accents[i]==Theme.Current)using(var p=Theme.Rounded(new RectangleF(r.X-1,r.Y-1,r.Width+1,r.Height+1),Theme.S(6)))using(var pen=new Pen(Theme.Ink,Theme.S(2)))g.DrawPath(pen,p);}
  }
 }

 // Mod rows: initials tile, name and version, one-line description, and an enable switch or state pill.
 public sealed class ModRow {public Package Package;public string State;public bool On,CanToggle;}
 public sealed class ModList:Control {
  readonly List<ModRow> rows=new List<ModRow>();int selected=-1,hover=-1,scroll;bool hoverSwitch;
  readonly Font name=Theme.Semibold(13),version=Theme.Mono(11),desc=Theme.Font(12),tile=Theme.Mono(11.5f),pill=Theme.Semibold(11);
  public event Action SelectionChanged;public event Action<ModRow> ToggleRequested;
  public string EmptyText="No mods match.";
  int RowHeight {get{return Theme.S(58);}}
  public ModList(){SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer|ControlStyles.ResizeRedraw|ControlStyles.Selectable,true);TabStop=true;BackColor=Theme.Panel;}
  public ModRow Selected {get{return selected>=0&&selected<rows.Count?rows[selected]:null;}}
  public int Count {get{return rows.Count;}}
  public void SetRows(IEnumerable<ModRow> items,string keepId){rows.Clear();rows.AddRange(items);selected=rows.FindIndex(r=>r.Package.Id==keepId);if(selected<0&&rows.Count>0)selected=0;Clamp();Invalidate();if(SelectionChanged!=null)SelectionChanged();}
  void Clamp(){scroll=Math.Max(0,Math.Min(scroll,rows.Count*RowHeight-Height));}
  void Select(int i){if(i<0||i>=rows.Count||i==selected)return;selected=i;int top=i*RowHeight;if(top<scroll)scroll=top;else if(top+RowHeight>scroll+Height)scroll=top+RowHeight-Height;Clamp();Invalidate();if(SelectionChanged!=null)SelectionChanged();}
  Rectangle SwitchRect(int i){int w=Theme.S(30),h=Theme.S(16);return new Rectangle(Width-Theme.S(14)-w,i*RowHeight-scroll+(RowHeight-h)/2,w,h);}
  int At(Point p){int i=(p.Y+scroll)/RowHeight;return i>=0&&i<rows.Count?i:-1;}
  protected override void OnResize(EventArgs e){Clamp();base.OnResize(e);}
  protected override void OnMouseWheel(MouseEventArgs e){scroll-=e.Delta/120*RowHeight;Clamp();Invalidate();base.OnMouseWheel(e);}
  protected override void OnMouseMove(MouseEventArgs e){int h=At(e.Location);bool sw=h>=0&&rows[h].CanToggle&&SwitchRect(h).Contains(e.Location);if(h!=hover||sw!=hoverSwitch){hover=h;hoverSwitch=sw;Invalidate();}Cursor=h>=0?Cursors.Hand:Cursors.Default;base.OnMouseMove(e);}
  protected override void OnMouseLeave(EventArgs e){hover=-1;Invalidate();base.OnMouseLeave(e);}
  protected override void OnMouseDown(MouseEventArgs e){Focus();int i=At(e.Location);if(i>=0){Select(i);if(rows[i].CanToggle&&SwitchRect(i).Contains(e.Location)&&ToggleRequested!=null)ToggleRequested(rows[i]);}base.OnMouseDown(e);}
  protected override bool IsInputKey(Keys k){return k==Keys.Up||k==Keys.Down||base.IsInputKey(k);}
  protected override void OnKeyDown(KeyEventArgs e){if(e.KeyCode==Keys.Down)Select(selected+1);else if(e.KeyCode==Keys.Up)Select(selected-1);else if(e.KeyCode==Keys.Space&&Selected!=null&&Selected.CanToggle&&ToggleRequested!=null)ToggleRequested(Selected);base.OnKeyDown(e);}
  public static string Initials(string n){string s=n.StartsWith("Any")&&n.Length>3?n.Substring(3):n;return (s.Length>2?s.Substring(0,2):s).ToUpperInvariant();}
  protected override void OnPaint(PaintEventArgs e){
   var g=e.Graphics;g.Clear(Theme.Panel);
   if(rows.Count==0){TextRenderer.DrawText(g,EmptyText,desc,ClientRectangle,Theme.Muted,TextFormatFlags.HorizontalCenter|TextFormatFlags.VerticalCenter);return;}
   int first=scroll/RowHeight;
   for(int i=first;i<rows.Count&&i*RowHeight-scroll<Height;i++){
    var row=rows[i];int y=i*RowHeight-scroll;var r=new Rectangle(0,y,Width,RowHeight);
    if(i==selected){using(var b=new SolidBrush(Theme.Mix(Theme.Blue,Theme.Panel,.07f)))g.FillRectangle(b,r);using(var b=new SolidBrush(Theme.Blue))g.FillRectangle(b,0,y,Theme.S(2),RowHeight);}
    else if(i==hover)using(var b=new SolidBrush(Color.FromArgb(17,23,33)))g.FillRectangle(b,r);
    if(i<rows.Count-1)using(var p=new Pen(Theme.Line))g.DrawLine(p,0,y+RowHeight-1,Width,y+RowHeight-1);
    int t=Theme.S(32),tx=Theme.S(14),ty=y+(RowHeight-t)/2;g.SmoothingMode=SmoothingMode.AntiAlias;
    ModArt.Draw(g,new Rectangle(tx,ty,t,t),row.Package,tile);
    int right=Width-Theme.S(14);
    if(row.CanToggle){var sr=SwitchRect(i);ToggleSwitch.Draw(g,sr,row.On,Enabled,i==hover&&hoverSwitch,i==selected?Theme.Mix(Theme.Blue,Theme.Panel,.07f):i==hover?Color.FromArgb(17,23,33):Theme.Panel);right=sr.X-Theme.S(12);}
    else right-=Theme.Pill(g,row.State,pill,Theme.Muted,right,y+RowHeight/2)+Theme.S(12);
    int x=tx+t+Theme.S(12);var nameSize=TextRenderer.MeasureText(g,row.Package.Name,name);
    TextRenderer.DrawText(g,row.Package.Name,name,new Rectangle(x,y+Theme.S(10),right-x,nameSize.Height),Theme.Ink,TextFormatFlags.Left|TextFormatFlags.SingleLine|TextFormatFlags.EndEllipsis);
    if(x+nameSize.Width<right)TextRenderer.DrawText(g,row.Package.Version,version,new Rectangle(x+nameSize.Width,y+Theme.S(12),right-x-nameSize.Width,nameSize.Height),Theme.Dim,TextFormatFlags.Left|TextFormatFlags.SingleLine|TextFormatFlags.EndEllipsis);
    TextRenderer.DrawText(g,row.Package.Description,desc,new Rectangle(x,y+Theme.S(31),right-x,Theme.S(18)),Theme.Muted,TextFormatFlags.Left|TextFormatFlags.SingleLine|TextFormatFlags.EndEllipsis);
   }
   int total=rows.Count*RowHeight;if(total>Height){int bar=Math.Max(Theme.S(24),Height*Height/total),top=(Height-bar)*scroll/Math.Max(1,total-Height);using(var p=Theme.Rounded(new RectangleF(Width-Theme.S(5),top+2,Theme.S(3),bar-4),Theme.S(1.5f)))using(var b=new SolidBrush(Theme.Line))g.FillPath(b,p);}
  }
 }

 // Text box drawn as a rounded field; the inner TextBox has no border of its own.
 public sealed class Field:Panel {
  public readonly TextBox Box=new TextBox();bool focused;
  [System.Runtime.InteropServices.DllImport("user32.dll",CharSet=System.Runtime.InteropServices.CharSet.Unicode)]static extern IntPtr SendMessage(IntPtr h,int msg,IntPtr w,string l);
  public Field(string placeholder,bool mono=false){
   BackColor=Theme.Raised;Height=Theme.S(32);SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer|ControlStyles.ResizeRedraw,true);Cursor=Cursors.IBeam;
   Box.BorderStyle=BorderStyle.None;Box.BackColor=Theme.Raised;Box.ForeColor=Theme.Ink;Box.Font=mono?Theme.Mono(12.5f):Theme.Font(13);Controls.Add(Box);
   Box.HandleCreated+=(s,e)=>{if(placeholder!="")SendMessage(Box.Handle,0x1501,new IntPtr(1),placeholder);};
   Box.GotFocus+=(s,e)=>{focused=true;Invalidate();};Box.LostFocus+=(s,e)=>{focused=false;Invalidate();};Click+=(s,e)=>Box.Focus();
  }
  protected override void OnLayout(LayoutEventArgs e){int pad=Theme.S(10);Box.SetBounds(pad,(Height-Box.Height)/2,Width-pad*2,Box.Height);base.OnLayout(e);}
  protected override void OnPaintBackground(PaintEventArgs e){var g=e.Graphics;g.Clear(Parent==null?Theme.Panel:Parent.BackColor);g.SmoothingMode=SmoothingMode.AntiAlias;
   using(var p=Theme.Rounded(new RectangleF(.5f,.5f,Width-1.5f,Height-1.5f),Theme.S(6)))using(var b=new SolidBrush(Theme.Raised))using(var pen=new Pen(focused?Theme.Blue:Theme.Line)){g.FillPath(b,p);g.DrawPath(pen,p);}}
 }

 // Recent manager actions, newest first, with the time they happened.
 public sealed class ActivityLog:Control {
  readonly List<KeyValuePair<DateTime,string>> items=new List<KeyValuePair<DateTime,string>>();readonly Font mono=Theme.Mono(12);
  public ActivityLog(){SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer|ControlStyles.ResizeRedraw,true);BackColor=Theme.Panel;}
  public void Add(string text){if(string.IsNullOrWhiteSpace(text)||(items.Count>0&&items[0].Value==text))return;items.Insert(0,new KeyValuePair<DateTime,string>(DateTime.Now,text));if(items.Count>12)items.RemoveAt(items.Count-1);Invalidate();}
  protected override void OnPaint(PaintEventArgs e){var g=e.Graphics;g.Clear(Theme.Panel);int line=Theme.S(22),y=0;
   if(items.Count==0){TextRenderer.DrawText(g,"Nothing yet. Installs, updates and launches show up here.",mono,new Point(0,0),Theme.Dim);return;}
   foreach(var item in items){if(y+line>Height)break;TextRenderer.DrawText(g,item.Key.ToString("HH:mm"),mono,new Point(0,y),Theme.Dim);
    TextRenderer.DrawText(g,item.Value,mono,new Rectangle(Theme.S(58),y,Width-Theme.S(58),line),y==0?Theme.Ink:Theme.Muted,TextFormatFlags.Left|TextFormatFlags.SingleLine|TextFormatFlags.EndEllipsis);y+=line;}}
 }

 public enum WindowGlyph {Minimize,Maximize,Close}
 public sealed class WindowButton:Control {
  readonly WindowGlyph glyph;bool hovering;public bool Maximized;
  public WindowButton(WindowGlyph glyph){this.glyph=glyph;Size=new Size(Theme.S(44),Theme.S(30));SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer,true);AccessibleName=glyph.ToString();}
  protected override void OnMouseEnter(EventArgs e){hovering=true;Invalidate();base.OnMouseEnter(e);}
  protected override void OnMouseLeave(EventArgs e){hovering=false;Invalidate();base.OnMouseLeave(e);}
  protected override void OnPaint(PaintEventArgs e){var g=e.Graphics;g.Clear(hovering?(glyph==WindowGlyph.Close?Color.FromArgb(196,43,58):Theme.Raised):Theme.Side);
   Color ink=hovering?Color.White:Theme.Muted;int cx=Width/2,cy=Height/2,s=Theme.S(5);
   using(var pen=new Pen(ink,Math.Max(1f,Theme.Scale))){switch(glyph){
    case WindowGlyph.Minimize:g.DrawLine(pen,cx-s,cy,cx+s,cy);break;
    case WindowGlyph.Maximize:if(Maximized){g.DrawRectangle(pen,cx-s,cy-s+Theme.S(2),s*2-Theme.S(2),s*2-Theme.S(2));g.DrawLines(pen,new[]{new Point(cx-s+Theme.S(2),cy-s),new Point(cx+s,cy-s),new Point(cx+s,cy+s-Theme.S(2))});}else g.DrawRectangle(pen,cx-s,cy-s,s*2,s*2);break;
    case WindowGlyph.Close:g.SmoothingMode=SmoothingMode.AntiAlias;g.DrawLine(pen,cx-s,cy-s,cx+s,cy+s);g.DrawLine(pen,cx-s,cy+s,cx+s,cy-s);break;}}}
 }
}
