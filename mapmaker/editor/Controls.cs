using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Runtime.InteropServices;
using System.Windows.Forms;
using AnyApiManager;
namespace AnyMapMaker {
 // Controls in the manager's look. Theme, ModernButton and Box come from manager/ (compiled in by build.ps1);
 // these are the few the Map Maker needs on top. Every size goes through Theme.S() for the Windows display scale.

 // Uppercase, letter-spaced section caption (MAP, TOOLS ...).
 public sealed class Caption:Control {
  static readonly Font font=Theme.Semibold(10.5f);
  public Caption(string text){Text=text;Height=Theme.S(30);SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer|ControlStyles.ResizeRedraw,true);}
  protected override void OnPaint(PaintEventArgs e){e.Graphics.Clear(Parent==null?Theme.Side:Parent.BackColor);Theme.Caption(e.Graphics,Text,font,Theme.Dim,0,Theme.S(6),Height-Theme.S(6));}
 }

 // Wrapped helper text.
 public sealed class Hint:Label {
  public Hint(string text){Text=text;Font=Theme.Font(12);ForeColor=Theme.Muted;BackColor=Color.Transparent;AutoSize=false;}
  public void Fit(int width){Width=width;Height=TextRenderer.MeasureText(Text,Font,new Size(width,0),TextFormatFlags.WordBreak).Height+Theme.S(4);}
 }

 // Text box drawn as a rounded field; the inner TextBox has no border of its own.
 public sealed class Field:Panel {
  public readonly TextBox Box=new TextBox();bool focused;
  [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern IntPtr SendMessage(IntPtr h,int msg,IntPtr w,string l);
  public Field(string placeholder,bool mono=false){
   BackColor=Theme.Raised;Height=Theme.S(32);SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer|ControlStyles.ResizeRedraw,true);Cursor=Cursors.IBeam;
   Box.BorderStyle=BorderStyle.None;Box.BackColor=Theme.Raised;Box.ForeColor=Theme.Ink;Box.Font=mono?Theme.Mono(12.5f):Theme.Font(13);Controls.Add(Box);
   Box.HandleCreated+=(s,e)=>{if(placeholder!="")try{SendMessage(Box.Handle,0x1501,new IntPtr(1),placeholder);}catch{}};
   Box.GotFocus+=(s,e)=>{focused=true;Invalidate();};Box.LostFocus+=(s,e)=>{focused=false;Invalidate();};Click+=(s,e)=>Box.Focus();
  }
  public override string Text {get{return Box.Text;}set{Box.Text=value;}}
  protected override void OnLayout(LayoutEventArgs e){int pad=Theme.S(10);Box.SetBounds(pad,(Height-Box.Height)/2,Width-pad*2,Box.Height);base.OnLayout(e);}
  protected override void OnPaintBackground(PaintEventArgs e){var g=e.Graphics;g.Clear(Parent==null?Theme.Side:Parent.BackColor);g.SmoothingMode=SmoothingMode.AntiAlias;
   using(var p=Theme.Rounded(new RectangleF(.5f,.5f,Width-1.5f,Height-1.5f),Theme.S(6)))using(var b=new SolidBrush(Theme.Raised))using(var pen=new Pen(focused?Theme.Blue:Theme.Line)){g.FillPath(b,p);g.DrawPath(pen,p);}}
 }

 // Drop-down list drawn as a rounded field with a chevron; the open list is dark too.
 public sealed class ThemedCombo:ComboBox {
  bool hovering;
  public ThemedCombo(){
   DropDownStyle=ComboBoxStyle.DropDownList;DrawMode=DrawMode.OwnerDrawFixed;FlatStyle=FlatStyle.Flat;MaxDropDownItems=20;
   Font=Theme.Font(13);ItemHeight=Theme.S(24);BackColor=Theme.Raised;ForeColor=Theme.Ink;Cursor=Cursors.Hand;
   SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer|ControlStyles.ResizeRedraw,true);
  }
  protected override void OnMouseEnter(EventArgs e){hovering=true;Invalidate();base.OnMouseEnter(e);}
  protected override void OnMouseLeave(EventArgs e){hovering=false;Invalidate();base.OnMouseLeave(e);}
  protected override void OnSelectedIndexChanged(EventArgs e){Invalidate();base.OnSelectedIndexChanged(e);}
  protected override void OnDropDownClosed(EventArgs e){Invalidate();base.OnDropDownClosed(e);}
  protected override void OnPaint(PaintEventArgs e){
   var g=e.Graphics;g.Clear(Parent==null?Theme.Side:Parent.BackColor);g.SmoothingMode=SmoothingMode.AntiAlias;
   using(var p=Theme.Rounded(new RectangleF(.5f,.5f,Width-1.5f,Height-1.5f),Theme.S(6)))using(var b=new SolidBrush(Theme.Raised))using(var pen=new Pen(Focused||DroppedDown?Theme.Blue:hovering?Theme.Mix(Theme.Ink,Theme.Raised,.12f):Theme.Line)){g.FillPath(b,p);g.DrawPath(pen,p);}
   int cx=Width-Theme.S(16),cy=Height/2,s=Theme.S(4);
   using(var pen=new Pen(Theme.Muted,Math.Max(1.2f,1.5f*Theme.Scale)){StartCap=LineCap.Round,EndCap=LineCap.Round,LineJoin=LineJoin.Round})g.DrawLines(pen,new[]{new Point(cx-s,cy-s/2),new Point(cx,cy+s/2),new Point(cx+s,cy-s/2)});
   TextRenderer.DrawText(g,SelectedItem==null?"":SelectedItem.ToString(),Font,new Rectangle(Theme.S(10),0,Width-Theme.S(36),Height),Enabled?Theme.Ink:Theme.Dim,TextFormatFlags.Left|TextFormatFlags.VerticalCenter|TextFormatFlags.SingleLine|TextFormatFlags.EndEllipsis);
  }
  protected override void OnDrawItem(DrawItemEventArgs e){
   if(e.Index<0)return;bool sel=(e.State&DrawItemState.Selected)!=0;
   using(var b=new SolidBrush(sel?Theme.Selected:Theme.Raised))e.Graphics.FillRectangle(b,e.Bounds);
   TextRenderer.DrawText(e.Graphics,Items[e.Index].ToString(),Font,new Rectangle(e.Bounds.X+Theme.S(8),e.Bounds.Y,e.Bounds.Width-Theme.S(10),e.Bounds.Height),sel?Theme.Ink:Theme.Muted,TextFormatFlags.Left|TextFormatFlags.VerticalCenter|TextFormatFlags.SingleLine|TextFormatFlags.EndEllipsis);
  }
 }

 // Number field with − and + ends. Mouse wheel and arrow keys change it too.
 public sealed class Stepper:Control {
  int value,hover=-1;public int Minimum,Maximum;public string Suffix="";public event Action ValueChanged;
  public Stepper(int min,int max,int value){Minimum=min;Maximum=max;this.value=value;Size=new Size(Theme.S(132),Theme.S(32));Font=Theme.Font(13);TabStop=true;
   SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer|ControlStyles.ResizeRedraw|ControlStyles.Selectable,true);}
  public int Value {get{return value;}set{int v=Math.Max(Minimum,Math.Min(Maximum,value));if(v==this.value)return;this.value=v;Invalidate();if(ValueChanged!=null)ValueChanged();}}
  int Part(Point p){int w=Height;return p.X<w?0:p.X>=Width-w?2:1;}
  protected override void OnMouseMove(MouseEventArgs e){int h=Part(e.Location);if(h!=hover){hover=h;Invalidate();}Cursor=h==1?Cursors.Default:Cursors.Hand;base.OnMouseMove(e);}
  protected override void OnMouseLeave(EventArgs e){hover=-1;Invalidate();base.OnMouseLeave(e);}
  protected override void OnMouseDown(MouseEventArgs e){Focus();int step=(ModifierKeys&Keys.Shift)!=0?10:1;int p=Part(e.Location);if(p==0)Value-=step;else if(p==2)Value+=step;base.OnMouseDown(e);}
  protected override void OnMouseWheel(MouseEventArgs e){Value+=Math.Sign(e.Delta);base.OnMouseWheel(e);}
  protected override bool IsInputKey(Keys k){return k==Keys.Up||k==Keys.Down||base.IsInputKey(k);}
  protected override void OnKeyDown(KeyEventArgs e){if(e.KeyCode==Keys.Up||e.KeyCode==Keys.Right)Value++;else if(e.KeyCode==Keys.Down||e.KeyCode==Keys.Left)Value--;base.OnKeyDown(e);}
  protected override void OnGotFocus(EventArgs e){Invalidate();base.OnGotFocus(e);}
  protected override void OnLostFocus(EventArgs e){Invalidate();base.OnLostFocus(e);}
  protected override void OnPaint(PaintEventArgs e){
   var g=e.Graphics;g.Clear(Parent==null?Theme.Side:Parent.BackColor);g.SmoothingMode=SmoothingMode.AntiAlias;int w=Height;
   using(var p=Theme.Rounded(new RectangleF(.5f,.5f,Width-1.5f,Height-1.5f),Theme.S(6)))using(var b=new SolidBrush(Theme.Raised))using(var pen=new Pen(Focused?Theme.Blue:Theme.Line)){g.FillPath(b,p);g.DrawPath(pen,p);}
   using(var line=new Pen(Theme.Line)){g.DrawLine(line,w,Theme.S(6),w,Height-Theme.S(6));g.DrawLine(line,Width-w,Theme.S(6),Width-w,Height-Theme.S(6));}
   int s=Theme.S(5),cy=Height/2;
   using(var pen=new Pen(hover==0&&value>Minimum?Theme.Ink:value>Minimum?Theme.Muted:Theme.Dim,Math.Max(1.2f,1.6f*Theme.Scale))){g.DrawLine(pen,w/2-s,cy,w/2+s,cy);}
   using(var pen=new Pen(hover==2&&value<Maximum?Theme.Ink:value<Maximum?Theme.Muted:Theme.Dim,Math.Max(1.2f,1.6f*Theme.Scale))){int cx=Width-w/2;g.DrawLine(pen,cx-s,cy,cx+s,cy);g.DrawLine(pen,cx,cy-s,cx,cy+s);}
   TextRenderer.DrawText(g,value+Suffix,Font,new Rectangle(w,0,Width-w*2,Height),Theme.Ink,TextFormatFlags.HorizontalCenter|TextFormatFlags.VerticalCenter|TextFormatFlags.SingleLine);
  }
 }

 // Label with an on/off switch (the manager's ToggleSwitch, drawn inline).
 public sealed class Toggle:Control {
  bool on,hovering;public event Action Toggled;
  public bool Checked {get{return on;}set{if(on!=value){on=value;Invalidate();}}}
  public Toggle(string text,bool on){Text=text;this.on=on;Font=Theme.Font(13);Height=Theme.S(28);Cursor=Cursors.Hand;TabStop=true;
   SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer|ControlStyles.ResizeRedraw|ControlStyles.Selectable,true);}
  protected override void OnMouseEnter(EventArgs e){hovering=true;Invalidate();base.OnMouseEnter(e);}
  protected override void OnMouseLeave(EventArgs e){hovering=false;Invalidate();base.OnMouseLeave(e);}
  protected override void OnMouseDown(MouseEventArgs e){Focus();Flip();base.OnMouseDown(e);}
  protected override void OnKeyDown(KeyEventArgs e){if(e.KeyCode==Keys.Space||e.KeyCode==Keys.Enter)Flip();base.OnKeyDown(e);}
  void Flip(){on=!on;Invalidate();if(Toggled!=null)Toggled();}
  protected override void OnPaint(PaintEventArgs e){
   var g=e.Graphics;var ground=Parent==null?Theme.Side:Parent.BackColor;g.Clear(ground);
   TextRenderer.DrawText(g,Text,Font,new Rectangle(0,0,Width-Theme.S(40),Height),on||hovering?Theme.Ink:Theme.Muted,TextFormatFlags.Left|TextFormatFlags.VerticalCenter|TextFormatFlags.SingleLine|TextFormatFlags.EndEllipsis);
   DrawSwitch(g,new Rectangle(Width-Theme.S(30)-1,(Height-Theme.S(16))/2,Theme.S(30),Theme.S(16)),on,hovering,ground);
  }
  public static void DrawSwitch(Graphics g,Rectangle r,bool on,bool hover,Color ground){
   g.SmoothingMode=SmoothingMode.AntiAlias;using(var clear=new SolidBrush(ground))g.FillRectangle(clear,r);
   Color track=on?Theme.Mix(Theme.Blue,Theme.Side,.42f):hover?Theme.Mix(Theme.Ink,Theme.SwitchOff,.08f):Theme.SwitchOff,knob=on?Theme.Blue:Theme.Muted;
   var t=new RectangleF(r.X,r.Y,r.Width-1,r.Height-1);using(var p=Theme.Rounded(t,t.Height/2))using(var b=new SolidBrush(track))g.FillPath(b,p);
   float k=r.Height-Theme.S(4);float x=on?r.Right-k-Theme.S(2)-1:r.X+Theme.S(2);using(var b=new SolidBrush(knob))g.FillEllipse(b,x,r.Y+Theme.S(2),k,k);
  }
 }

 // Grid of tool buttons; the picked one is drawn in the accent colour.
 public sealed class ToolGrid:Control {
  readonly List<KeyValuePair<Tool,string>> tools=new List<KeyValuePair<Tool,string>>();int hover=-1;public Tool Current;public event Action<Tool> Picked;
  public const int Columns=3;
  public ToolGrid(){Font=Theme.Semibold(12.5f);Cursor=Cursors.Hand;SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer|ControlStyles.ResizeRedraw,true);}
  public void Add(Tool t,string name){tools.Add(new KeyValuePair<Tool,string>(t,name));Height=Rows*RowHeight;}
  int Rows {get{return (tools.Count+Columns-1)/Columns;}}
  int RowHeight {get{return Theme.S(36);}}
  Rectangle Cell(int i){int gap=Theme.S(6),w=(Width-gap*(Columns-1))/Columns;return new Rectangle(i%Columns*(w+gap),i/Columns*RowHeight,w,RowHeight-gap);}
  int At(Point p){for(int i=0;i<tools.Count;i++)if(Cell(i).Contains(p))return i;return -1;}
  public void Pick(Tool t){Current=t;Invalidate();if(Picked!=null)Picked(t);}
  protected override void OnMouseMove(MouseEventArgs e){int h=At(e.Location);if(h!=hover){hover=h;Invalidate();}base.OnMouseMove(e);}
  protected override void OnMouseLeave(EventArgs e){hover=-1;Invalidate();base.OnMouseLeave(e);}
  protected override void OnMouseDown(MouseEventArgs e){int i=At(e.Location);if(i>=0)Pick(tools[i].Key);base.OnMouseDown(e);}
  protected override void OnPaint(PaintEventArgs e){
   var g=e.Graphics;g.Clear(Parent==null?Theme.Side:Parent.BackColor);g.SmoothingMode=SmoothingMode.AntiAlias;
   for(int i=0;i<tools.Count;i++){
    var r=Cell(i);bool on=tools[i].Key==Current;
    using(var p=Theme.Rounded(new RectangleF(r.X+.5f,r.Y+.5f,r.Width-1.5f,r.Height-1.5f),Theme.S(6))){
     using(var b=new SolidBrush(on?Theme.Blue:Theme.Raised))g.FillPath(b,p);
     if(!on)using(var pen=new Pen(i==hover?Theme.Mix(Theme.Ink,Theme.Raised,.14f):Theme.Line))g.DrawPath(pen,p);
    }
    TextRenderer.DrawText(g,tools[i].Value,Font,r,on?Theme.AccentInk:i==hover?Theme.Ink:Theme.Muted,TextFormatFlags.HorizontalCenter|TextFormatFlags.VerticalCenter|TextFormatFlags.SingleLine|TextFormatFlags.EndEllipsis);
   }
  }
 }

 // Map layers with on/off switches.
 public sealed class LayerList:Control {
  int hover=-1;public event Action Changed;readonly Font count=Theme.Mono(11);
  public LayerList(){Font=Theme.Font(13);Cursor=Cursors.Hand;SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer|ControlStyles.ResizeRedraw,true);Reload();}
  int RowHeight {get{return Theme.S(30);}}
  public void Reload(){Height=MapScene.Layers.Count*RowHeight;Invalidate();}
  protected override void OnMouseMove(MouseEventArgs e){int h=e.Y/RowHeight;if(h>=MapScene.Layers.Count)h=-1;if(h!=hover){hover=h;Invalidate();}base.OnMouseMove(e);}
  protected override void OnMouseLeave(EventArgs e){hover=-1;Invalidate();base.OnMouseLeave(e);}
  protected override void OnMouseDown(MouseEventArgs e){int i=e.Y/RowHeight;if(i>=0&&i<MapScene.Layers.Count){MapScene.Layers[i].Visible=!MapScene.Layers[i].Visible;Invalidate();if(Changed!=null)Changed();}base.OnMouseDown(e);}
  protected override void OnPaint(PaintEventArgs e){
   var g=e.Graphics;var ground=Parent==null?Theme.Side:Parent.BackColor;g.Clear(ground);
   for(int i=0;i<MapScene.Layers.Count;i++){
    var l=MapScene.Layers[i];int y=i*RowHeight;bool lit=l.Visible||i==hover;
    var name=TextRenderer.MeasureText(g,l.Title,Font,Size.Empty,TextFormatFlags.NoPadding);
    TextRenderer.DrawText(g,l.Title,Font,new Rectangle(0,y,Width-Theme.S(44),RowHeight),lit?Theme.Ink:Theme.Muted,TextFormatFlags.Left|TextFormatFlags.VerticalCenter|TextFormatFlags.SingleLine|TextFormatFlags.EndEllipsis|TextFormatFlags.NoPadding);
    if(l.Count>0&&name.Width+Theme.S(8)<Width-Theme.S(90))TextRenderer.DrawText(g,l.Count.ToString("N0"),count,new Rectangle(name.Width+Theme.S(8),y,Width-Theme.S(44)-name.Width-Theme.S(8),RowHeight),Theme.Dim,TextFormatFlags.Left|TextFormatFlags.VerticalCenter|TextFormatFlags.SingleLine|TextFormatFlags.NoPadding);
    Toggle.DrawSwitch(g,new Rectangle(Width-Theme.S(30)-1,y+(RowHeight-Theme.S(16))/2,Theme.S(30),Theme.S(16)),l.Visible,i==hover,ground);
   }
  }
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

 // Dark drop-down menus.
 public sealed class DarkMenuRenderer:ToolStripRenderer {
  protected override void OnRenderToolStripBackground(ToolStripRenderEventArgs e){using(var b=new SolidBrush(Theme.Panel))e.Graphics.FillRectangle(b,e.AffectedBounds);}
  protected override void OnRenderToolStripBorder(ToolStripRenderEventArgs e){using(var p=new Pen(Theme.Line))e.Graphics.DrawRectangle(p,0,0,e.ToolStrip.Width-1,e.ToolStrip.Height-1);}
  protected override void OnRenderImageMargin(ToolStripRenderEventArgs e){}
  protected override void OnRenderMenuItemBackground(ToolStripItemRenderEventArgs e){if(e.Item.Selected&&e.Item.Enabled)using(var b=new SolidBrush(Theme.Selected))e.Graphics.FillRectangle(b,Theme.S(4),0,e.Item.Width-Theme.S(8),e.Item.Height);}
  protected override void OnRenderItemText(ToolStripItemTextRenderEventArgs e){e.TextColor=!e.Item.Enabled?Theme.Dim:e.Item.Selected?Theme.Ink:Theme.Muted;e.TextFont=Theme.Font(13);base.OnRenderItemText(e);}
  protected override void OnRenderSeparator(ToolStripSeparatorRenderEventArgs e){int y=e.Item.Height/2;using(var p=new Pen(Theme.Line))e.Graphics.DrawLine(p,Theme.S(8),y,e.Item.Width-Theme.S(8),y);}
 }

 // Small frameless dialog in the same style: a title, a body the caller fills, and buttons along the bottom.
 public class ThemedDialog:Form {
  public readonly Panel Body=new Panel();readonly FlowLayoutPanel buttons=new FlowLayoutPanel{FlowDirection=FlowDirection.RightToLeft};readonly string title;
  [DllImport("user32.dll")]static extern bool ReleaseCapture();
  [DllImport("user32.dll")]static extern IntPtr SendMessage(IntPtr h,int msg,IntPtr w,IntPtr l);
  public ThemedDialog(string title,int width,int bodyHeight){
   this.title=title;AutoScaleMode=AutoScaleMode.None;FormBorderStyle=FormBorderStyle.None;StartPosition=FormStartPosition.CenterParent;ShowInTaskbar=false;KeyPreview=true;
   BackColor=Theme.Panel;ForeColor=Theme.Ink;Font=Theme.Font(13);ClientSize=new Size(Theme.S(width),Theme.S(48)+Theme.S(bodyHeight)+Theme.S(64));Text=title;
   Body.BackColor=Theme.Panel;Body.Bounds=new Rectangle(Theme.S(20),Theme.S(48),ClientSize.Width-Theme.S(40),Theme.S(bodyHeight));Controls.Add(Body);
   buttons.BackColor=Theme.Panel;buttons.Bounds=new Rectangle(Theme.S(12),ClientSize.Height-Theme.S(56),ClientSize.Width-Theme.S(24),Theme.S(44));Controls.Add(buttons);
   MouseDown+=(s,e)=>{if(e.Button==MouseButtons.Left&&e.Y<Theme.S(44)){ReleaseCapture();SendMessage(Handle,0xA1,new IntPtr(2),IntPtr.Zero);}};
   KeyDown+=(s,e)=>{if(e.KeyCode==Keys.Escape){DialogResult=DialogResult.Cancel;Close();}};
  }
  public ModernButton AddButton(string text,DialogResult result,bool primary){
   var b=new ModernButton{Text=text,Kind=primary?ButtonKind.Primary:ButtonKind.Secondary,DialogResult=result,Height=Theme.S(34),Margin=new Padding(Theme.S(8),Theme.S(4),0,0)};
   using(var g=CreateGraphics())b.Width=Math.Max(Theme.S(96),TextRenderer.MeasureText(g,text,b.Font).Width+Theme.S(32));
   buttons.Controls.Add(b);if(primary)AcceptButton=b;if(result==DialogResult.Cancel)CancelButton=b;return b;
  }
  protected override void OnPaint(PaintEventArgs e){
   var g=e.Graphics;using(var p=new Pen(Theme.Line)){g.DrawRectangle(p,0,0,Width-1,Height-1);g.DrawLine(p,1,Theme.S(44),Width-2,Theme.S(44));}
   using(var f=Theme.Semibold(14))TextRenderer.DrawText(g,title,f,new Rectangle(Theme.S(20),0,Width-Theme.S(40),Theme.S(44)),Theme.Ink,TextFormatFlags.Left|TextFormatFlags.VerticalCenter|TextFormatFlags.SingleLine|TextFormatFlags.EndEllipsis);
  }
  // A message with buttons; returns the index of the button pressed (-1 for Esc / close).
  public static int Ask(IWin32Window owner,string title,string text,params string[] choices){
   int height;using(var f=Theme.Font(13))height=TextRenderer.MeasureText(text,f,new Size(Theme.S(440),0),TextFormatFlags.WordBreak).Height;
   using(var d=new ThemedDialog(title,480,(int)Math.Ceiling(height/Theme.Scale)+8)){
    d.Body.Controls.Add(new Label{Text=text,Dock=DockStyle.Fill,ForeColor=Theme.Muted,Font=Theme.Font(13)});
    int picked=-1;
    for(int i=0;i<choices.Length;i++){int index=i;var b=d.AddButton(choices[i],DialogResult.None,i==0);b.Click+=(s,e)=>{picked=index;d.DialogResult=DialogResult.OK;d.Close();};}
    d.ShowDialog(owner);return picked<0&&choices.Length==1?0:picked;
   }
  }
  public static void Tell(IWin32Window owner,string title,string text){Ask(owner,title,text,"OK");}
  // One-line text entry; null when cancelled.
  public static string Prompt(IWin32Window owner,string title,string label,string value,string okText){
   using(var d=new ThemedDialog(title,440,76)){
    d.Body.Controls.Add(new Label{Text=label,ForeColor=Theme.Muted,Font=Theme.Font(12.5f),Bounds=new Rectangle(0,0,d.Body.Width,Theme.S(24))});
    var field=new Field(""){Bounds=new Rectangle(0,Theme.S(30),d.Body.Width,Theme.S(34))};field.Text=value;d.Body.Controls.Add(field);
    d.AddButton(okText,DialogResult.OK,true);d.AddButton("Cancel",DialogResult.Cancel,false);
    d.Shown+=(s,e)=>{field.Box.Focus();field.Box.SelectAll();};
    return d.ShowDialog(owner)==DialogResult.OK?field.Text.Trim():null;
   }
  }
 }
}
