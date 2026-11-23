using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;
using AnyApiManager;
namespace AnyMapMaker {
 // Line icons drawn on a 32 x 32 grid, so they stay sharp at any DPI.
 public static class Icons {
  public static void Draw(Graphics g,string name,RectangleF r,Color c,float stroke=1.8f){
   var st=g.Save();g.SmoothingMode=SmoothingMode.AntiAlias;g.TranslateTransform(r.X,r.Y);float k=Math.Min(r.Width,r.Height)/32f;g.ScaleTransform(k,k);
   using(var p=new Pen(c,stroke){StartCap=LineCap.Round,EndCap=LineCap.Round,LineJoin=LineJoin.Round}){
    switch(name){
     case "pick":g.DrawLines(p,new[]{P(8,4),P(8,26),P(13,21),P(17,29),P(20,28),P(16,20),P(23,20),P(8,4)});break;
     case "ground":g.DrawPolygon(p,new[]{P(2,25),P(10,12),P(16,19),P(21,13),P(30,25)});break;
     case "hills":g.DrawLines(p,new[]{P(3,26),P(29,26)});g.DrawLines(p,new[]{P(16,21),P(16,5)});g.DrawLines(p,new[]{P(10,11),P(16,5),P(22,11)});break;
     case "road":g.DrawBezier(p,P(9,29),P(11,18),P(14,12),P(12,3));g.DrawBezier(p,P(23,29),P(21,18),P(22,12),P(20,3));g.DrawLine(p,P(16.5f,27),P(16.5f,24));g.DrawLine(p,P(16.5f,19),P(16.5f,16));g.DrawLine(p,P(16.5f,11),P(16.5f,8));break;
     case "fence":foreach(var x in new[]{6,16,26})g.DrawLine(p,P(x,8),P(x,28));g.DrawLine(p,P(3,14),P(29,14));g.DrawLine(p,P(3,22),P(29,22));break;
     case "prop":g.DrawLine(p,P(16,4),P(16,29));g.DrawLine(p,P(8,8),P(24,8));g.DrawLine(p,P(10,8),P(10,12));g.DrawLine(p,P(22,8),P(22,12));break;
     case "house":g.DrawLines(p,new[]{P(4,15),P(16,5),P(28,15)});g.DrawLines(p,new[]{P(7,13),P(7,27),P(25,27),P(25,13)});g.DrawLines(p,new[]{P(14,27),P(14,20),P(18,20),P(18,27)});break;
     case "loot":g.DrawRectangle(p,5,11,22,15);g.DrawLine(p,P(5,17),P(27,17));g.DrawLines(p,new[]{P(14,17),P(14,21),P(18,21),P(18,17)});g.DrawLines(p,new[]{P(9,11),P(9,7),P(23,7),P(23,11)});break;
     case "enemy":g.DrawEllipse(p,11,4,10,10);g.DrawArc(p,8,17,16,16,180,180);g.DrawLine(p,P(8,25),P(8,29));g.DrawLine(p,P(24,25),P(24,29));break;
     case "start":g.DrawEllipse(p,6,6,20,20);g.DrawEllipse(p,13,13,6,6);g.DrawLine(p,P(16,2),P(16,7));g.DrawLine(p,P(16,25),P(16,30));g.DrawLine(p,P(2,16),P(7,16));g.DrawLine(p,P(25,16),P(30,16));break;
     case "erase":g.DrawPolygon(p,new[]{P(12,27),P(4,19),P(17,6),P(27,16),P(16,27)});g.DrawLine(p,P(12,27),P(28,27));g.DrawLine(p,P(9,14),P(20,24));break;
     case "tree":g.DrawPolygon(p,new[]{P(16,3),P(25,20),P(7,20)});g.DrawLine(p,P(16,20),P(16,28));break;
     case "pole":g.DrawLine(p,P(16,4),P(16,29));g.DrawLine(p,P(8,8),P(24,8));g.DrawLine(p,P(10,8),P(10,12));g.DrawLine(p,P(22,8),P(22,12));break;
     case "factory":g.DrawLines(p,new[]{P(4,27),P(4,14),P(11,19),P(11,14),P(18,19),P(18,8),P(23,8),P(23,27),P(4,27)});g.DrawLine(p,P(28,27),P(2,27));break;
     case "barn":g.DrawLines(p,new[]{P(5,27),P(5,13),P(16,5),P(27,13),P(27,27)});g.DrawLine(p,P(3,27),P(29,27));g.DrawRectangle(p,12,18,8,9);g.DrawLine(p,P(12,18),P(20,27));g.DrawLine(p,P(20,18),P(12,27));break;
     case "shop":g.DrawRectangle(p,5,12,22,15);g.DrawLines(p,new[]{P(3,12),P(6,5),P(26,5),P(29,12),P(3,12)});g.DrawRectangle(p,9,17,6,10);break;
     case "fuel":g.DrawRectangle(p,6,6,13,21);g.DrawRectangle(p,9,9,7,6);g.DrawLines(p,new[]{P(19,12),P(24,15),P(24,24),P(27,24),P(27,12),P(24,9)});break;
     case "play":using(var b=new SolidBrush(c))g.FillPolygon(b,new[]{P(9,6),P(26,16),P(9,26)});break;
     case "expand":g.DrawLines(p,new[]{P(19,5),P(27,5),P(27,13)});g.DrawLine(p,P(27,5),P(18,14));g.DrawLines(p,new[]{P(13,27),P(5,27),P(5,19)});g.DrawLine(p,P(5,27),P(14,18));break;
    }
   }
   g.Restore(st);
  }
  static PointF P(float x,float y){return new PointF(x,y);}
 }

 // Big icon buttons down the left edge, one per tool.
 public sealed class ToolRail:Control {
  public struct Item{public Tool Tool;public string Name,Icon;}
  readonly List<Item> items=new List<Item>();int hover=-1;public Tool Current=Tool.Select;public event Action<Tool> Picked;
  public ToolRail(){Font=Theme.Font(11);Cursor=Cursors.Hand;SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer|ControlStyles.ResizeRedraw,true);}
  public void Add(Tool t,string name,string icon){items.Add(new Item{Tool=t,Name=name,Icon=icon});Invalidate();}
  int Cell {get{return Theme.S(62);}}
  Rectangle Bounds_(int i){int pad=Theme.S(8);return new Rectangle(pad,Theme.S(10)+i*(Cell+Theme.S(4)),Width-pad*2,Cell);}
  public void Pick(Tool t){Current=t;Invalidate();if(Picked!=null)Picked(t);}
  protected override void OnPaint(PaintEventArgs e){
   var g=e.Graphics;g.Clear(Theme.Side);g.SmoothingMode=SmoothingMode.AntiAlias;
   using(var line=new Pen(Theme.Line))g.DrawLine(line,Width-1,0,Width-1,Height);
   for(int i=0;i<items.Count;i++){
    var r=Bounds_(i);bool on=items[i].Tool==Current;
    if(on||i==hover)using(var path=Theme.Rounded(r,Theme.S(10)))using(var b=new SolidBrush(on?Theme.Selected:Theme.Raised)){g.FillPath(b,path);if(on)using(var pen=new Pen(Theme.Blue,1.2f))g.DrawPath(pen,path);}
    var c=on?Theme.Blue:Color.FromArgb(185,195,209);int ic=Theme.S(24);
    Icons.Draw(g,items[i].Icon,new RectangleF(r.X+(r.Width-ic)/2f,r.Y+Theme.S(8),ic,ic),c);
    TextRenderer.DrawText(g,items[i].Name,Font,new Rectangle(r.X,r.Bottom-Theme.S(22),r.Width,Theme.S(18)),on?Theme.Ink:Theme.Muted,TextFormatFlags.HorizontalCenter|TextFormatFlags.VerticalCenter|TextFormatFlags.SingleLine);
   }
  }
  int At(Point p){for(int i=0;i<items.Count;i++)if(Bounds_(i).Contains(p))return i;return -1;}
  protected override void OnMouseMove(MouseEventArgs e){int h=At(e.Location);if(h!=hover){hover=h;Invalidate();}}
  protected override void OnMouseLeave(EventArgs e){hover=-1;Invalidate();}
  protected override void OnMouseClick(MouseEventArgs e){int i=At(e.Location);if(i>=0)Pick(items[i].Tool);}
 }

 // "Things to place": picture tiles that pick a tool and its setting in one click.
 public sealed class ThingGrid:Control {
  public sealed class Thing{public string Name,Icon;public Action Choose;public Func<bool> IsOn;}
  readonly List<Thing> things=new List<Thing>();int hover=-1;public const int Columns=3;
  public ThingGrid(){Font=Theme.Font(11);Cursor=Cursors.Hand;SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer|ControlStyles.ResizeRedraw,true);}
  public void Add(string name,string icon,Action choose,Func<bool> isOn){things.Add(new Thing{Name=name,Icon=icon,Choose=choose,IsOn=isOn});Height=Rows*(CellH+Gap)-Gap;}
  int Rows {get{return (things.Count+Columns-1)/Columns;}}
  int CellH {get{return Theme.S(66);}} int Gap {get{return Theme.S(8);}}
  Rectangle Cell(int i){int w=(Width-Gap*(Columns-1))/Columns;return new Rectangle((i%Columns)*(w+Gap),(i/Columns)*(CellH+Gap),w,CellH);}
  protected override void OnPaint(PaintEventArgs e){
   var g=e.Graphics;g.Clear(Parent!=null?Parent.BackColor:Theme.Side);g.SmoothingMode=SmoothingMode.AntiAlias;
   for(int i=0;i<things.Count;i++){
    var r=Cell(i);bool on=things[i].IsOn!=null&&things[i].IsOn();
    using(var path=Theme.Rounded(r,Theme.S(10)))using(var b=new SolidBrush(on?Theme.Selected:i==hover?Theme.Line:Theme.Raised)){g.FillPath(b,path);using(var pen=new Pen(on?Theme.Blue:Theme.Line,1.2f))g.DrawPath(pen,path);}
    int ic=Theme.S(26);Icons.Draw(g,things[i].Icon,new RectangleF(r.X+(r.Width-ic)/2f,r.Y+Theme.S(9),ic,ic),on?Theme.Blue:Color.FromArgb(185,195,209));
    TextRenderer.DrawText(g,things[i].Name,Font,new Rectangle(r.X+2,r.Bottom-Theme.S(24),r.Width-4,Theme.S(20)),on?Theme.Ink:Theme.Muted,TextFormatFlags.HorizontalCenter|TextFormatFlags.VerticalCenter|TextFormatFlags.SingleLine|TextFormatFlags.EndEllipsis);
   }
  }
  int At(Point p){for(int i=0;i<things.Count;i++)if(Cell(i).Contains(p))return i;return -1;}
  protected override void OnMouseMove(MouseEventArgs e){int h=At(e.Location);if(h!=hover){hover=h;Invalidate();}}
  protected override void OnMouseLeave(EventArgs e){hover=-1;Invalidate();}
  protected override void OnMouseClick(MouseEventArgs e){int i=At(e.Location);if(i>=0){things[i].Choose();Invalidate();}}
 }

 // A small name tag in the corner of a view, with an optional button (icon) at its right.
 public sealed class ViewTag:Control {
  public string Icon;public event Action Clicked;bool hovering;
  public ViewTag(string text,string icon){Text=text;Icon=icon;Font=Theme.Semibold(10.5f);Height=Theme.S(26);SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer|ControlStyles.SupportsTransparentBackColor,true);
   Fit();if(icon!=null)Cursor=Cursors.Hand;}
  public void Fit(){Width=TextRenderer.MeasureText(Text.ToUpperInvariant(),Font).Width+Theme.S(Icon==null?16:44);}
  protected override void OnPaint(PaintEventArgs e){
   var g=e.Graphics;g.SmoothingMode=SmoothingMode.AntiAlias;var r=new Rectangle(0,0,Width-1,Height-1);
   using(var path=Theme.Rounded(r,Theme.S(7)))using(var b=new SolidBrush(hovering&&Icon!=null?Theme.Raised:Theme.Side)){g.FillPath(b,path);using(var pen=new Pen(Theme.Line))g.DrawPath(pen,path);}
   TextRenderer.DrawText(g,Text.ToUpperInvariant(),Font,new Rectangle(Theme.S(9),0,Width,Height),Theme.Muted,TextFormatFlags.Left|TextFormatFlags.VerticalCenter|TextFormatFlags.SingleLine);
   if(Icon!=null){int ic=Theme.S(15);Icons.Draw(g,Icon,new RectangleF(Width-ic-Theme.S(8),(Height-ic)/2f,ic,ic),hovering?Theme.Blue:Theme.Muted,2.4f);}
  }
  protected override void OnMouseEnter(EventArgs e){hovering=true;Invalidate();}
  protected override void OnMouseLeave(EventArgs e){hovering=false;Invalidate();}
  protected override void OnClick(EventArgs e){if(Clicked!=null)Clicked();}
 }

 // The green "Play in game" button with a play icon.
 public sealed class GoButton:Control {
  bool hovering;
  public GoButton(string text){Text=text;Font=Theme.Semibold(13);Cursor=Cursors.Hand;Height=Theme.S(32);SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer|ControlStyles.SupportsTransparentBackColor,true);BackColor=Color.Transparent;}
  public int PreferredWidth {get{return TextRenderer.MeasureText(Text,Font).Width+Theme.S(46);}}
  protected override void OnPaint(PaintEventArgs e){
   var g=e.Graphics;g.SmoothingMode=SmoothingMode.AntiAlias;var fill=hovering?ControlPaint.Light(Theme.Ok,.15f):Theme.Ok;var ink=Color.FromArgb(4,23,15);
   using(var path=Theme.Rounded(new RectangleF(0,0,Width-1,Height-1),Theme.S(6)))using(var b=new SolidBrush(fill))g.FillPath(b,path);
   int ic=Theme.S(14);Icons.Draw(g,"play",new RectangleF(Theme.S(13),(Height-ic)/2f,ic,ic),ink);
   TextRenderer.DrawText(g,Text,Font,new Rectangle(Theme.S(32),0,Width-Theme.S(38),Height),ink,TextFormatFlags.Left|TextFormatFlags.VerticalCenter|TextFormatFlags.SingleLine);
  }
  protected override void OnMouseEnter(EventArgs e){hovering=true;Invalidate();}
  protected override void OnMouseLeave(EventArgs e){hovering=false;Invalidate();}
 }

 // A road type with a plain name in the list.
 public sealed class RoadChoice {
  public string Id,Name;
  public override string ToString(){return Name;}
  public static RoadChoice[] All(){
   var list=new List<RoadChoice>();
   foreach(var id in Catalog.RoadTypes){string n;
    switch(id){case "road":n="Road (two lanes)";break;case "road_b":n="Small road";break;case "road_c":n="Dirt track";break;case "road_d":n="Gravel road";break;
     case "train_a":n="Railway";break;case "train_b":n="Railway (long)";break;case "train_flyover_a":n="Railway bridge";break;case "train_flyover_b":n="Railway bridge (long)";break;default:n=id;break;}
    list.Add(new RoadChoice{Id=id,Name=n});}
   return list.ToArray();
  }
 }
}
