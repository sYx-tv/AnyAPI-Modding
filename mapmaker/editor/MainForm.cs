using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Windows.Forms;
using AnyApiManager;
namespace AnyMapMaker {
 // The editor window, in the AnyAPI Manager's look: frameless dark window, title bar with the file actions and
 // Play in game, big tool buttons down the left, the map on top with a live 3D preview under it, and a panel on the
 // right with the tool's settings, things to place, the selected point and what the map shows.
 public sealed class MainForm : Form, IMessageFilter {
  public const string Version="0.5.0";
  static int S(float px){return Theme.S(px);}
  readonly MapView view=new MapView();
  readonly MapView3D view3d=new MapView3D();
  readonly Panel splitter=new Panel{Cursor=Cursors.HSplit};double split=0.56;   // share of the middle for the map
  readonly ViewTag mapTag=new ViewTag("Map (top down)",null), previewTag=new ViewTag("3D preview · how it looks in game","expand");
  readonly GoButton playButton=new GoButton("Play in game");
  readonly Stepper falloffBox=new Stepper(0,1000,120){Step=10,Suffix=" m"};
  bool in3d;
  readonly Panel titleBar=new Panel(), sidebar=new Panel(), statusBar=new Panel();
  readonly Field nameBox=new Field("Map name"), authorBox=new Field("Your name"), descriptionBox=new Field("A short description");
  readonly ToolRail tools=new ToolRail();
  readonly ThingGrid things=new ThingGrid();
  readonly Label toolTitle=new Label{AutoEllipsis=true}, nodeInfo=new Label();
  readonly LayerList layers=new LayerList();
  readonly Panel options=new Panel();
  readonly Label tileTitle=new Label{AutoEllipsis=true}, status=new Label{AutoEllipsis=true};
  readonly TextBox tileJson=new TextBox{Multiline=true,ScrollBars=ScrollBars.Both,WordWrap=false,AcceptsTab=true,BorderStyle=BorderStyle.None};
  readonly ModernButton applyJson=new ModernButton{Text="Apply tile changes",Kind=ButtonKind.Primary,Enabled=false};
  readonly ModernButton undoButton=new ModernButton{Text="Undo",Kind=ButtonKind.Ghost,Enabled=false}, redoButton=new ModernButton{Text="Redo",Kind=ButtonKind.Ghost,Enabled=false};
  readonly WindowButton maximize=new WindowButton(WindowGlyph.Maximize);
  readonly ContextMenuStrip more=new ContextMenuStrip{Renderer=new DarkMenuRenderer(),ShowImageMargin=false};
  string counts="";Color statusColor=Theme.Dim;
  GameInstall game; MapPack pack; bool unsaved;
  MapEditor editor; Catalog catalog=Catalog.Load(null);
  // Tool options (kept between tool switches).
  readonly ThemedCombo pieceBox=new ThemedCombo(), orientBox=new ThemedCombo(), roadBox=new ThemedCombo(), fenceBox=new ThemedCombo(), propBox=new ThemedCombo(), filterBox=new ThemedCombo();
  readonly Toggle setHeight=new Toggle("Also set height",false), eraseThings=new Toggle("Objects",true), eraseRoads=new Toggle("Roads",true);
  readonly Stepper heightBox=new Stepper(-6,16,0), rotBox=new Stepper(0,359,0){Suffix="°"}, sizeBox=new Stepper(0,5,2), radiusBox=new Stepper(1,200,8){Suffix=" m"};
  [DllImport("user32.dll")]static extern bool ReleaseCapture();
  [DllImport("user32.dll")]static extern IntPtr SendMessage(IntPtr h,int msg,IntPtr w,IntPtr l);
  [StructLayout(LayoutKind.Sequential)]struct POINT{public int X,Y;}
  [StructLayout(LayoutKind.Sequential)]struct MINMAXINFO{public POINT Reserved,MaxSize,MaxPosition,MinTrackSize,MaxTrackSize;}

  public MainForm(){
   AutoScaleMode=AutoScaleMode.None;FormBorderStyle=FormBorderStyle.None;Text="Anymaker Map Maker";BackColor=Theme.Line;ForeColor=Theme.Ink;Font=Theme.Font(13);DoubleBuffered=true;KeyPreview=true;
   ClientSize=new Size(S(1360),S(860));MinimumSize=new Size(S(1000),S(640));StartPosition=FormStartPosition.CenterScreen;
   try{Icon=Icon.ExtractAssociatedIcon(Application.ExecutablePath);}catch{}
   foreach(var bar in new Control[]{titleBar,sidebar,statusBar}){bar.BackColor=Theme.Side;Buffer(bar);}
   view.Font=Theme.Font(12);view3d.Font=Theme.Font(12);splitter.BackColor=Theme.Line;
   view.Controls.Add(mapTag);view3d.Controls.Add(previewTag);previewTag.Clicked+=()=>Show3D(!in3d);
   Controls.AddRange(new Control[]{titleBar,tools,sidebar,statusBar,view,splitter,view3d});
   BuildTitle();BuildSidebar();BuildInspector();BuildStatus();BuildSplitter();
   view.TilePainted+=OnTilePainted;view.Placed+=OnPlaced;view.LineDrawn+=OnLine;view.RoadFinished+=OnRoad;view.Erased+=OnErase;
   view.StrokeEnded+=()=>{if(editor!=null&&editor.Commit())Edited(true);};
   view.StatusChanged+=t=>SetStatus(t,Theme.Dim);view.SelectionChanged+=ShowTile;
   view3d.StatusChanged+=t=>SetStatus(t,Theme.Dim);view3d.NodeChanged+=ShowNode;
   view3d.NodesMoved+=()=>{unsaved=true;if(!rebuildPending){rebuildPending=true;BeginInvoke(new Action(()=>{rebuildPending=false;if(pack!=null)view3d.ShowScene(MapScene.Build(pack),editor);}));}};
   view3d.DragFinished+=()=>Edited(true);
   falloffBox.ValueChanged+=()=>view3d.Falloff=falloffBox.Value;
   // Edits in the map show in the 3D preview straight away (strokes rebuild it when they end).
   view.StrokeEnded+=()=>view3d.Invalidate();
   tools.Pick(Tool.Select);
   Resize+=(s,e)=>{LayoutShell();maximize.Maximized=WindowState==FormWindowState.Maximized;maximize.Invalidate();};LayoutShell();
   Shown+=(s,e)=>FindGame(false);
   FormClosing+=(s,e)=>{if(unsaved&&ThemedDialog.Ask(this,"Unsaved changes","Your map has changes that aren't saved yet.","Keep editing","Close without saving")!=1)e.Cancel=true;};
  }
  [DllImport("uxtheme.dll",CharSet=CharSet.Unicode)]static extern int SetWindowTheme(IntPtr h,string app,string idList);
  public static void DarkScrollbars(Control c){c.HandleCreated+=(s,e)=>{try{SetWindowTheme(c.Handle,"DarkMode_Explorer",null);}catch{}};}
  static void Buffer(Control c){typeof(Control).GetProperty("DoubleBuffered",System.Reflection.BindingFlags.NonPublic|System.Reflection.BindingFlags.Instance).SetValue(c,true,null);}

  // ---------------------------------------------------------------- window chrome (as in the manager)
  protected override CreateParams CreateParams {get{var cp=base.CreateParams;cp.Style|=0x00020000|0x00010000|0x00080000;cp.ClassStyle|=0x00020000;return cp;}}
  protected override void OnHandleCreated(EventArgs e){base.OnHandleCreated(e);Application.AddMessageFilter(this);}
  protected override void OnFormClosed(FormClosedEventArgs e){Application.RemoveMessageFilter(this);base.OnFormClosed(e);}
  protected override void WndProc(ref Message m){
   base.WndProc(ref m);
   if(m.Msg==0x24){// WM_GETMINMAXINFO: maximize to the monitor's work area, not over the taskbar.
    var screen=Screen.FromHandle(m.HWnd);var info=(MINMAXINFO)Marshal.PtrToStructure(m.LParam,typeof(MINMAXINFO));
    info.MaxPosition.X=screen.WorkingArea.Left-screen.Bounds.Left;info.MaxPosition.Y=screen.WorkingArea.Top-screen.Bounds.Top;info.MaxSize.X=screen.WorkingArea.Width;info.MaxSize.Y=screen.WorkingArea.Height;
    info.MinTrackSize.X=MinimumSize.Width;info.MinTrackSize.Y=MinimumSize.Height;Marshal.StructureToPtr(info,m.LParam,true);
   }
  }
  int Edge(Point p){
   if(WindowState!=FormWindowState.Normal)return 0;int z=S(6);bool l=p.X<z,r=p.X>=ClientSize.Width-z,t=p.Y<z,b=p.Y>=ClientSize.Height-z;
   return t&&l?13:t&&r?14:b&&l?16:b&&r?17:l?10:r?11:t?12:b?15:0;
  }
  public bool PreFilterMessage(ref Message m){
   if(m.Msg==0x20A&&sidebarHeight>sidebar.Height){// mouse wheel over the sidebar scrolls it (a Stepper under the cursor keeps the wheel)
    var p=sidebar.PointToClient(Cursor.Position);if(!sidebar.ClientRectangle.Contains(p)||sidebar.GetChildAtPoint(p) is Stepper)return false;
    int delta=(short)((long)m.WParam>>16);sideScroll-=delta/120*S(60);LayoutSidebar();return true;
   }
   if(m.Msg!=0x200&&m.Msg!=0x201)return false;var c=Control.FromChildHandle(m.HWnd);if(c==null||c.FindForm()!=this)return false;
   int hit=Edge(PointToClient(Cursor.Position));if(hit==0)return false;
   Cursor.Current=hit==10||hit==11?Cursors.SizeWE:hit==12||hit==15?Cursors.SizeNS:hit==13||hit==17?Cursors.SizeNWSE:Cursors.SizeNESW;
   if(m.Msg==0x201){ReleaseCapture();SendMessage(Handle,0xA1,new IntPtr(hit),IntPtr.Zero);return true;}return false;
  }
  void DragWindow(MouseEventArgs e){if(e.Button!=MouseButtons.Left||Edge(PointToClient(Cursor.Position))!=0)return;if(e.Clicks==2){ToggleMaximize();return;}ReleaseCapture();SendMessage(Handle,0xA1,new IntPtr(2),IntPtr.Zero);}
  void ToggleMaximize(){WindowState=WindowState==FormWindowState.Maximized?FormWindowState.Normal:FormWindowState.Maximized;}
  void LayoutShell(){
   int b=WindowState==FormWindowState.Maximized?0:1;var r=new Rectangle(b,b,ClientSize.Width-b*2,ClientSize.Height-b*2);int title=S(48),foot=S(30),rail=S(84),right=S(340);
   titleBar.Bounds=new Rectangle(r.X,r.Y,r.Width,title);statusBar.Bounds=new Rectangle(r.X,r.Bottom-foot,r.Width,foot);
   int top=r.Y+title,h=r.Height-title-foot;
   tools.Bounds=new Rectangle(r.X,top,rail,h);sidebar.Bounds=new Rectangle(r.Right-right,top,right,h);
   int x=r.X+rail,w=r.Width-rail-right,bar=S(6),mapH=Math.Max(S(120),Math.Min(h-bar-S(120),(int)(h*(in3d?0.22:split))));
   view.Bounds=new Rectangle(x,top,w,mapH);splitter.Bounds=new Rectangle(x,top+mapH,w,bar);view3d.Bounds=new Rectangle(x,top+mapH+bar,w,h-mapH-bar);
   mapTag.Location=new Point(S(12),S(12));previewTag.Location=new Point(S(12),S(12));
   LayoutTitle();LayoutSidebar();
  }

  // ---------------------------------------------------------------- title bar: brand, map name, file actions, Play
  readonly List<ModernButton> leftActions=new List<ModernButton>();readonly List<Control> rightActions=new List<Control>();
  void BuildTitle(){
   var minimize=new WindowButton(WindowGlyph.Minimize);var close=new WindowButton(WindowGlyph.Close);
   minimize.Click+=(s,e)=>WindowState=FormWindowState.Minimized;maximize.Click+=(s,e)=>ToggleMaximize();close.Click+=(s,e)=>Close();
   titleBar.Controls.AddRange(new Control[]{minimize,maximize,close});titleBar.MouseDown+=(s,e)=>DragWindow(e);
   Func<string,ButtonKind,Action,ModernButton> add=(text,kind,act)=>{var b=new ModernButton{Text=text,Kind=kind,Height=S(32)};if(act!=null)b.Click+=(s,e)=>act();titleBar.Controls.Add(b);return b;};
   leftActions.Add(add("New island",ButtonKind.Ghost,NewIsland));leftActions.Add(add("Open",ButtonKind.Ghost,OpenPack));
   titleBar.Controls.Add(nameBox);
   rightActions.Add(undoButton);rightActions.Add(redoButton);titleBar.Controls.AddRange(new Control[]{undoButton,redoButton});
   undoButton.Click+=(s,e)=>Undo();redoButton.Click+=(s,e)=>Redo();
   var moreButton=add("More",ButtonKind.Ghost,null);rightActions.Add(moreButton);moreButton.Click+=(s,e)=>more.Show(moreButton,new Point(0,moreButton.Height+S(4)));
   rightActions.Add(add("Save",ButtonKind.Primary,()=>Save(false)));
   rightActions.Add(playButton);titleBar.Controls.Add(playButton);playButton.Height=S(32);playButton.Click+=(s,e)=>PlayInGame();
   more.Items.Add("Map details (author, description)...",null,(s,e)=>MapDetails());
   more.Items.Add("Open the stock world",null,(s,e)=>OpenStock());
   more.Items.Add("Save as a new map...",null,(s,e)=>Save(true));
   more.Items.Add("Export a copy to another folder...",null,(s,e)=>Export());
   more.Items.Add(new ToolStripSeparator());
   more.Items.Add("Raise the whole map 32 m",null,(s,e)=>ShiftAll(1));
   more.Items.Add("Lower the whole map 32 m",null,(s,e)=>ShiftAll(-1));
   more.Items.Add(new ToolStripSeparator());
   more.Items.Add("Show the maps folder",null,(s,e)=>ShowMapsFolder());
   more.Items.Add("Game folder...",null,(s,e)=>FindGame(true));
   more.Items.Add("Fit the whole map   Ctrl+0",null,(s,e)=>{view.FitAll();view3d.FitAll();});
   more.Items.Add(new ToolStripSeparator());
   more.Items.Add("Controls",null,(s,e)=>ShowHelp());
   more.Items.Add("About",null,(s,e)=>ThemedDialog.Tell(this,"About","Anymaker Map Maker "+Version+"\nPart of AnyAPI-Modding. Maps are played in game with the CustomMap mod: pick one on the Custom Game page."));
   foreach(ToolStripItem i in more.Items){i.ForeColor=Theme.Muted;i.Font=Theme.Font(13);i.Padding=new Padding(S(4),S(3),S(4),S(3));}
   var brand=Theme.Font(13,true);var sub=Theme.Semibold(10.5f);var file=Theme.Font(12.5f);
   titleBar.Paint+=(s,e)=>{var g=e.Graphics;int x=S(16);
    using(var glow=new SolidBrush(Color.FromArgb(26,Theme.Blue))){var any=TextRenderer.MeasureText(g,"ANY",brand,Size.Empty,TextFormatFlags.NoPadding);g.SmoothingMode=SmoothingMode.AntiAlias;g.FillEllipse(glow,x-S(6),titleBar.Height/2-S(9),any.Width+S(12),S(18));}
    Theme.Caption(g,"ANY",brand,Theme.Blue,x,0,titleBar.Height);x+=(int)(TextRenderer.MeasureText(g,"ANY",brand,Size.Empty,TextFormatFlags.NoPadding).Width+brand.Size*.42f);
    Theme.Caption(g,"MAP",brand,Theme.Ink,x,0,titleBar.Height);x+=(int)(TextRenderer.MeasureText(g,"MAP",brand,Size.Empty,TextFormatFlags.NoPadding).Width+brand.Size*.42f)+S(10);
    Theme.Caption(g,"MAKER",sub,Theme.Dim,x,S(1),titleBar.Height);
    int left=nameBox.Right+S(12),right=rightActions[0].Left-S(12);
    string doc=pack==null?"No map open":unsaved?"Not saved yet":"Saved";
    if(right>left)TextRenderer.DrawText(g,doc,file,new Rectangle(left,0,right-left,titleBar.Height),unsaved?Theme.Warn:Theme.Muted,TextFormatFlags.Left|TextFormatFlags.VerticalCenter|TextFormatFlags.SingleLine|TextFormatFlags.EndEllipsis);
    using(var line=new Pen(Theme.Line))g.DrawLine(line,0,titleBar.Height-1,titleBar.Width,titleBar.Height-1);};
  }
  void LayoutTitle(){
   int x=titleBar.Width-S(4);var buttons=new[]{(Control)titleBar.Controls[0],titleBar.Controls[1],titleBar.Controls[2]};
   for(int i=buttons.Length-1;i>=0;i--){x-=buttons[i].Width;buttons[i].Location=new Point(x,(titleBar.Height-buttons[i].Height)/2-1);}
   using(var g=CreateGraphics()){
    foreach(var b in leftActions.Concat(rightActions))b.Width=b is GoButton?((GoButton)b).PreferredWidth:TextRenderer.MeasureText(g,b.Text,b.Font).Width+S(28);
    x-=S(12);for(int i=rightActions.Count-1;i>=0;i--){var b=rightActions[i];x-=b.Width;b.Location=new Point(x,(titleBar.Height-b.Height)/2);x-=b==undoButton?S(4):S(8);}
    int ax=S(190);foreach(var b in leftActions){b.Location=new Point(ax,(titleBar.Height-b.Height)/2);ax+=b.Width+S(6);}
    nameBox.SetBounds(ax+S(10),(titleBar.Height-S(32))/2,S(230),S(32));
   }
   titleBar.Invalidate();
  }

  // ---------------------------------------------------------------- right panel: the tool, things to place, the
  // selected point, and what the map shows
  readonly Caption thingsCaption=new Caption("Things to place"), nodeCaption=new Caption("Selected point"), layersCaption=new Caption("Show on the map");
  void BuildSidebar(){
   toolTitle.Font=Theme.Font(19,true);toolTitle.ForeColor=Theme.Ink;toolTitle.BackColor=Theme.Side;toolTitle.Height=S(34);
   nodeInfo.Font=Theme.Font(12.5f);nodeInfo.ForeColor=Theme.Muted;nodeInfo.BackColor=Theme.Side;
   sidebar.Controls.AddRange(new Control[]{toolTitle,options,thingsCaption,things,nodeCaption,nodeInfo,falloffBox,layersCaption,layers});
   options.BackColor=Theme.Side;
   foreach(var box in new[]{nameBox,authorBox,descriptionBox})box.Box.TextChanged+=(s,e)=>MetadataChanged();
   foreach(Tool t in Enum.GetValues(typeof(Tool)))tools.Add(t,ToolName(t),ToolIcon(t));
   tools.Picked+=SetTool;
   BuildThings();
   layers.Changed+=()=>{view.Invalidate();view3d.ShowScene(view3d.Scene,editor);};
   fenceBox.Items.AddRange(FenceStyle.All);fenceBox.SelectedIndex=0;
   roadBox.Items.AddRange(RoadChoice.All());roadBox.SelectedIndex=0;
   filterBox.Items.AddRange(Catalog.BuildingFilters);filterBox.SelectedIndex=0;
   orientBox.Items.AddRange(new object[]{"Keep turn","Turn 0","Turn 1","Turn 2"});orientBox.SelectedIndex=0;
   FillCatalogBoxes();
   radiusBox.ValueChanged+=()=>view.EraseRadius=radiusBox.Value;
   ShowNode(null);
   sidebar.Paint+=(s,e)=>{var g=e.Graphics;using(var line=new Pen(Theme.Line))g.DrawLine(line,0,0,0,sidebar.Height);
    if(sidebarHeight>sidebar.Height){int h=sidebar.Height,bar=Math.Max(S(24),h*h/sidebarHeight),top=(h-bar)*sideScroll/Math.Max(1,sidebarHeight-h);
     g.SmoothingMode=SmoothingMode.AntiAlias;using(var p=Theme.Rounded(new RectangleF(sidebar.Width-S(7),top+2,S(3),bar-4),S(1.5f)))using(var b=new SolidBrush(Theme.Line))g.FillPath(b,p);}};
  }
  // One click picks the tool and its setting.
  void BuildThings(){
   Action<string,string,Tool,Action,Func<bool>> thing=(name,icon,tool,set,on)=>things.Add(name,icon,()=>{if(set!=null)set();tools.Pick(tool);},()=>tools.Current==tool&&(on==null||on()));
   Func<string,Func<bool>> filter=f=>()=>(string)filterBox.SelectedItem==f;
   thing("House","house",Tool.Building,()=>filterBox.SelectedItem="house",filter("house"));
   thing("Farm","barn",Tool.Building,()=>filterBox.SelectedItem="farm",filter("farm"));
   thing("Town","shop",Tool.Building,()=>filterBox.SelectedItem="town",filter("town"));
   thing("Industry","factory",Tool.Building,()=>filterBox.SelectedItem="industrial",filter("industrial"));
   thing("Gas station","fuel",Tool.Building,()=>filterBox.SelectedItem="gas_station",filter("gas_station"));
   thing("Street light","pole",Tool.Prop,()=>{if(propBox.Items.Contains("lamp_post_a"))propBox.SelectedItem="lamp_post_a";},()=>(string)propBox.SelectedItem=="lamp_post_a");
   thing("Fence","fence",Tool.Fence,null,null);
   thing("Loot","loot",Tool.Loot,null,null);
   thing("Zombies","enemy",Tool.Enemy,null,null);
  }
  int sideScroll,sidebarHeight;
  void LayoutSidebar(){
   int pad=S(18),w=sidebar.ClientSize.Width-pad*2-S(4);
   sideScroll=Math.Max(0,Math.Min(sideScroll,sidebarHeight-sidebar.Height));int y=S(14)-sideScroll;
   Action<Control,int> put=(c,gap)=>{c.SetBounds(pad,y,w,c.Height);y+=c.Height+gap;};
   put(toolTitle,S(6));LayoutOptions(w);put(options,S(18));
   put(thingsCaption,S(4));put(things,S(18));
   put(nodeCaption,S(2));nodeInfo.Height=TextRenderer.MeasureText(nodeInfo.Text,nodeInfo.Font,new Size(w,0),TextFormatFlags.WordBreak).Height+S(6);put(nodeInfo,S(8));
   falloffBox.SetBounds(pad,y,S(150),S(32));y+=S(32)+S(18);
   put(layersCaption,S(2));put(layers,S(16));
   int total=y+sideScroll;if(total!=sidebarHeight){sidebarHeight=total;if(sideScroll>Math.Max(0,total-sidebar.Height)){LayoutSidebar();return;}}
   sidebar.Invalidate();
  }
  void LayoutOptions(int w){
   int y=0;
   foreach(Control c in options.Controls){
    var h=c as Hint;if(h!=null)h.Fit(w);
    if(c is Stepper)c.SetBounds(0,y,Math.Min(w,S(150)),S(32));
    else if(c is ThemedCombo)c.SetBounds(0,y,w,S(32));
    else if(c==tileJson)c.SetBounds(0,y,w,S(300));
    else c.SetBounds(0,y,w,c.Height);
    y+=c.Height+(c is Label&&!(c is Hint)?S(4):S(10));
   }
   options.Height=Math.Max(0,y-S(10));
  }
  // The point under the mouse or being dragged in the 3D preview.
  void ShowNode(MapNode n){
   if(n==null)nodeInfo.Text="Hover over a white dot in the 3D preview. Drag it to move it; hold Shift while dragging to raise or lower it.\nSmooth radius: how far a moved road point pulls the road with it.";
   else{
    string what=n.IsRoad?"Road point":n.Label;double grade=0;
    if(n.IsRoad)foreach(var e in n.Ends){Vec3 a,b;Vec3.TryRead(e.Link["pos_a"],out a);Vec3.TryRead(e.Link["pos_b"],out b);double run=Math.Sqrt((b.X-a.X)*(b.X-a.X)+(b.Z-a.Z)*(b.Z-a.Z));if(run>0.5)grade=Math.Max(grade,Math.Abs(b.Y-a.Y)/run*100);}
    nodeInfo.Text=what+"\nHeight: "+n.Position.Y.ToString("0")+" m"+(n.IsRoad?"\nSteepness here: "+grade.ToString("0")+"% ("+(grade<=8?"easy":grade<=15?"ok for most cars":grade<=25?"steep":"too steep for most vehicles")+")":"");
   }
   if(sidebar.IsHandleCreated)LayoutSidebar();
  }

  // ---------------------------------------------------------------- select tool: the clicked tile's raw data
  void BuildInspector(){
   tileTitle.Font=Theme.Font(12.5f);tileTitle.ForeColor=Theme.Ink;tileTitle.BackColor=Theme.Side;tileTitle.Height=S(40);
   DarkScrollbars(tileJson);tileJson.BackColor=Theme.Raised;tileJson.ForeColor=Theme.Ink;tileJson.Font=Theme.Mono(12);
   tileJson.TextChanged+=(s,e)=>applyJson.Enabled=view.Selected!=null&&!showingTile;
   applyJson.Height=S(34);applyJson.Click+=(s,e)=>ApplyTileJson();
   ShowTile(null);
  }

  // ---------------------------------------------------------------- the bar between the map and the 3D preview
  void BuildSplitter(){
   bool dragging=false;
   splitter.MouseDown+=(s,e)=>{if(e.Button==MouseButtons.Left)dragging=true;};
   splitter.MouseUp+=(s,e)=>dragging=false;
   splitter.MouseMove+=(s,e)=>{if(!dragging)return;var p=PointToClient(Cursor.Position);double h=view3d.Bottom-view.Top;in3d=false;split=Math.Max(0.15,Math.Min(0.85,(p.Y-view.Top)/h));LayoutShell();};
  }

  // ---------------------------------------------------------------- status bar
  void BuildStatus(){
   status.ForeColor=Theme.Muted;status.BackColor=Theme.Side;status.Font=Theme.Font(12);status.TextAlign=ContentAlignment.MiddleLeft;statusBar.Controls.Add(status);
   var mono=Theme.Mono(11);
   statusBar.Paint+=(s,e)=>{var g=e.Graphics;using(var line=new Pen(Theme.Line))g.DrawLine(line,0,0,statusBar.Width,0);Theme.Dot(g,statusColor,S(18),statusBar.Height/2,S(7));
    TextRenderer.DrawText(g,counts,mono,new Rectangle(0,0,statusBar.Width-S(14),statusBar.Height),Theme.Dim,TextFormatFlags.Right|TextFormatFlags.VerticalCenter);};
   statusBar.Resize+=(s,e)=>status.Bounds=new Rectangle(S(30),1,statusBar.Width-S(300),statusBar.Height-1);
  }
  void SetStatus(string text,Color dot){status.Text=text;statusColor=dot;statusBar.Invalidate();}

  // ---------------------------------------------------------------- keyboard
  protected override bool ProcessCmdKey(ref Message msg,Keys keys){
   bool typing=ActiveControl is TextBoxBase||(ActiveControl is Field)||(ActiveControl is ContainerControl&&((ContainerControl)ActiveControl).ActiveControl is TextBoxBase);
   if(tileJson.Focused||nameBox.Box.Focused||authorBox.Box.Focused||descriptionBox.Box.Focused)typing=true;
   switch(keys){
    case Keys.Control|Keys.S:Save(false);return true;
    case Keys.Control|Keys.Shift|Keys.S:Save(true);return true;
    case Keys.Control|Keys.O:OpenPack();return true;
    case Keys.Control|Keys.N:NewIsland();return true;
    case Keys.Control|Keys.D0:if(in3d)view3d.FitAll();else view.FitAll();return true;
    case Keys.Tab:if(typing)break;Show3D(!in3d);return true;
    case Keys.Control|Keys.Z:if(typing)break;Undo();return true;
    case Keys.Control|Keys.Y:if(typing)break;Redo();return true;
   }
   return base.ProcessCmdKey(ref msg,keys);
  }
  void Undo(){if(editor!=null&&editor.Undo())Edited(true);}
  void Redo(){if(editor!=null&&editor.Redo())Edited(true);}
  void ShowHelp(){
   ThemedDialog.Tell(this,"Controls","Scroll to zoom. Drag with the right or middle mouse button to move around. Ctrl+0 fits the whole map.\n\n"+
    "Pick a tool on the left; its options appear under it. Ctrl+Z undoes, Ctrl+Y redoes, Ctrl+S saves.\n\n"+
    "Terrain: click or drag over triangles (ocean removes land). Height: click to raise 32 m, Ctrl+click to lower. "+
    "Road: click points, double click or Enter to finish, Backspace removes the last point, Esc cancels. Fence: drag a line. "+
    "Prop, Building, Loot: click to place, or drag to aim. Start: click where players begin. Erase: click or drag. Select: click a tile to edit its raw data on the right.\n\n"+
    "Save puts the map straight into the game's maps folder. In game, pick it on the Custom Game page (Map row).");
  }

  // ---------------------------------------------------------------- game folder and files
  public static bool Headless;
  void FindGame(bool ask){
   if(Headless)return;
   string folder=ask?null:Settings.GameFolder;
   if(folder==null||!new GameInstall(folder).Valid)folder=ask?null:GameInstall.Guess();
   if(folder==null){
    if(!ask&&ThemedDialog.Ask(this,"Where is Anymaker?","The Map Maker couldn't find Anymaker in your Steam libraries. Show it the game folder once (the one with game.exe) and it will remember it.","Choose folder","Later")!=0){SetStatus("No game folder yet. Use More, Game folder... to set it.",Theme.Warn);return;}
    using(var d=new FolderBrowserDialog{Description="Choose the Anymaker game folder (the one with game.exe)"}){
     if(d.ShowDialog(this)!=DialogResult.OK)return;
     folder=GameInstall.Resolve(d.SelectedPath);
    }
    if(folder==null){ThemedDialog.Tell(this,"Not the game folder","That folder has no game.exe and rom\\data\\tiles. In Steam: right-click Anymaker, Manage, Browse local files.");return;}
   }
   game=new GameInstall(folder);Settings.GameFolder=folder;
   catalog=Catalog.Load(game);FillCatalogBoxes();
   SetStatus("Game folder: "+folder+". Maps save to "+game.MapsFolder,Theme.Ok);
  }
  bool EnsureGame(){if(game==null)FindGame(false);if(game==null)FindGame(true);return game!=null;}

  bool ConfirmDiscard(){return !unsaved||ThemedDialog.Ask(this,"Unsaved changes","Your map has changes that aren't saved yet. Throw them away?","Keep editing","Discard")==1;}

  void OpenStock(){
   if(!ConfirmDiscard()||!EnsureGame())return;
   OpenWith(()=>{var p=MapPack.LoadTiles(game.StockTiles,"My map");p.Base="stock";foreach(var k in p.Tiles.Keys)p.StockVegetation.Add(k.BaseName);foreach(var t in p.Tiles.Values)t.Dirty=true;return p;});
  }
  // Lists the maps in the game's maps folder; other folders only through "Browse".
  void OpenPack(){
   if(!ConfirmDiscard())return;
   if(game==null)FindGame(false);
   var maps=new List<string>();
   if(game!=null&&Directory.Exists(game.MapsFolder))foreach(var d in Directory.GetDirectories(game.MapsFolder))if(File.Exists(Path.Combine(d,"map.json")))maps.Add(d);
   maps.Sort(StringComparer.OrdinalIgnoreCase);
   string chosen=null;
   using(var d=new ThemedDialog("Open a map",460,300)){
    var list=new ListBox{Dock=DockStyle.Fill,BorderStyle=BorderStyle.None,BackColor=Theme.Raised,ForeColor=Theme.Ink,Font=Theme.Font(13),DrawMode=DrawMode.OwnerDrawFixed,ItemHeight=S(34),IntegralHeight=false};
    DarkScrollbars(list);foreach(var m in maps)list.Items.Add(Path.GetFileName(m));
    list.DrawItem+=(s,e)=>{if(e.Index<0)return;bool sel=(e.State&DrawItemState.Selected)!=0;using(var b=new SolidBrush(sel?Theme.Selected:Theme.Raised))e.Graphics.FillRectangle(b,e.Bounds);
     if(sel)using(var b=new SolidBrush(Theme.Blue))e.Graphics.FillRectangle(b,e.Bounds.X,e.Bounds.Y,S(2),e.Bounds.Height);
     TextRenderer.DrawText(e.Graphics,list.Items[e.Index].ToString(),list.Font,new Rectangle(e.Bounds.X+S(12),e.Bounds.Y,e.Bounds.Width-S(16),e.Bounds.Height),sel?Theme.Ink:Theme.Muted,TextFormatFlags.Left|TextFormatFlags.VerticalCenter|TextFormatFlags.SingleLine|TextFormatFlags.EndEllipsis);};
    if(maps.Count>0){list.SelectedIndex=0;d.Body.Controls.Add(list);}
    else d.Body.Controls.Add(new Label{Dock=DockStyle.Fill,ForeColor=Theme.Muted,Font=Theme.Font(13),Text=game==null?"The game folder isn't set yet, so there is no maps folder to list. Use Browse, or set it with More, Game folder.":"No maps yet. Make one with New island or Open the stock world, then Save: it goes into\n"+game.MapsFolder});
    var open=d.AddButton("Open",DialogResult.OK,true);open.Enabled=maps.Count>0;d.AddButton("Cancel",DialogResult.Cancel,false);d.AddButton("Browse...",DialogResult.Retry,false);
    list.DoubleClick+=(s,e)=>{if(list.SelectedIndex>=0){d.DialogResult=DialogResult.OK;d.Close();}};
    var r=d.ShowDialog(this);
    if(r==DialogResult.OK&&list.SelectedIndex>=0)chosen=maps[list.SelectedIndex];
    else if(r==DialogResult.Retry){
     using(var f=new FolderBrowserDialog{Description="Choose a map pack folder (it contains map.json)"}){
      if(f.ShowDialog(this)!=DialogResult.OK)return;
      if(!File.Exists(Path.Combine(f.SelectedPath,"map.json"))){ThemedDialog.Tell(this,"Not a map","That folder has no map.json.");return;}
      chosen=f.SelectedPath;
     }
    }
   }
   if(chosen!=null){string folder=chosen;OpenWith(()=>MapPack.Open(folder));}
  }
  void OpenWith(Func<MapPack> loader){
   Cursor=Cursors.WaitCursor;
   try{
    pack=loader();editor=new MapEditor(pack);var scene=MapScene.Build(pack);view.SetMap(pack,scene);view.ClearRoad();view3d.SetMap(pack,scene,editor);
    loading=true;nameBox.Text=pack.Name;authorBox.Text=pack.Author;descriptionBox.Text=pack.Description;loading=false;
    unsaved=pack.Folder==null;layers.Reload();ShowTile(null);UpdateCounts();UpdateUndo();
    SetStatus(pack.Folder==null?"New map. Save puts it in the game's maps folder.":"Opened "+pack.Folder,Theme.Ok);WarnSeaLevel();
    if(pack.LoadErrors.Count>0)ThemedDialog.Tell(this,"Some tiles were skipped",pack.LoadErrors.Count+" tile files could not be read. First: "+pack.LoadErrors[0]);
   }catch(Exception e){ThemedDialog.Tell(this,"Could not open the map",e.Message);}
   finally{Cursor=Cursors.Default;}
  }
  bool loading;
  void MetadataChanged(){if(loading||pack==null)return;pack.Name=nameBox.Text.Trim();pack.Author=authorBox.Text.Trim();pack.Description=descriptionBox.Text.Trim();unsaved=true;UpdateCounts();}
  void UpdateCounts(){counts=pack==null?"":pack.Tiles.Count.ToString("N0")+" tiles";Text="Anymaker Map Maker"+(pack==null?"":" - "+pack.Name+(unsaved?" *":""));statusBar.Invalidate();titleBar.Invalidate();}
  void UpdateUndo(){undoButton.Enabled=editor!=null&&editor.CanUndo;redoButton.Enabled=editor!=null&&editor.CanRedo;}

  // Save always goes to <game>\AnyAPI and Modding\maps\<map name>, where the Custom Game page lists it.
  void Save(bool asNew){
   if(pack==null)return;
   if(!EnsureGame()){SetStatus("Not saved: the Map Maker needs the game folder to know where maps go.",Theme.Bad);return;}
   string maps=game.MapsFolder;Directory.CreateDirectory(maps);
   string oldName=pack.Name,oldFolder=pack.Folder;
   Action undoSaveAs=()=>{if(!asNew)return;pack.Name=oldName;pack.Folder=oldFolder;loading=true;nameBox.Text=oldName;loading=false;};
   if(asNew){
    string name=ThemedDialog.Prompt(this,"Save as a new map","Name of the new map (this is what the Custom Game page shows):",pack.Name+" copy","Save");
    if(string.IsNullOrEmpty(name))return;
    loading=true;nameBox.Text=name;loading=false;pack.Name=name;pack.Folder=null;
   }
   if(pack.Name.Length==0){pack.Name="My map";loading=true;nameBox.Text=pack.Name;loading=false;}
   string want=Path.Combine(maps,SafeName(pack.Name));string folder=pack.Folder;
   bool inMaps=folder!=null&&string.Equals(Path.GetFullPath(Path.GetDirectoryName(folder.TrimEnd('\\','/'))),Path.GetFullPath(maps),StringComparison.OrdinalIgnoreCase);
   if(folder==null||!inMaps){
    // A new map, or one opened from elsewhere: it gets its own folder in the maps folder.
    if(Directory.Exists(want)&&Directory.GetFileSystemEntries(want).Length>0){
     int pick=ThemedDialog.Ask(this,"A map with this name exists","There is already a map called \""+Path.GetFileName(want)+"\" in the maps folder.","Keep both","Replace it","Cancel");
     if(pick==0)want=FreeName(maps,SafeName(pack.Name));
     else if(pick!=1){undoSaveAs();return;}
     else if(!File.Exists(Path.Combine(want,"map.json"))){undoSaveAs();ThemedDialog.Tell(this,"Can't replace it",want+" has other files in it. Rename the map and save again.");return;}
    }
    if(Path.GetFileName(want)!=SafeName(pack.Name)){pack.Name=Path.GetFileName(want);loading=true;nameBox.Text=pack.Name;loading=false;}
    foreach(var t in pack.Tiles.Values)t.Dirty=true;folder=want;
   }else if(!string.Equals(Path.GetFileName(folder.TrimEnd('\\','/')),SafeName(pack.Name),StringComparison.OrdinalIgnoreCase)){
    // Renamed in the Name box: move its folder so the Custom Game page shows the new name.
    if(Directory.Exists(want))SetStatus("Kept the folder name \""+Path.GetFileName(folder)+"\": another map is already called \""+SafeName(pack.Name)+"\".",Theme.Warn);
    else try{Directory.Move(folder,want);folder=want;pack.Folder=want;}catch(Exception e){SetStatus("Couldn't rename the map folder: "+e.Message,Theme.Warn);}
   }
   Cursor=Cursors.WaitCursor;
   try{pack.Save(folder,game.BuildId);pack.Folder=folder;unsaved=false;UpdateCounts();SetStatus("Saved \""+Path.GetFileName(folder)+"\". In game, pick it on the Custom Game page (Map row).",Theme.Ok);}
   catch(Exception e){undoSaveAs();ThemedDialog.Tell(this,"Could not save",e.Message);}
   finally{Cursor=Cursors.Default;}
  }
  void Export(){
   if(pack==null)return;
   using(var d=new FolderBrowserDialog{Description="Choose where to put a copy. A folder named after the map is created inside it."}){
    if(d.ShowDialog(this)!=DialogResult.OK)return;
    string folder=Path.Combine(d.SelectedPath,SafeName(pack.Name));
    if(Directory.Exists(folder)&&Directory.GetFileSystemEntries(folder).Length>0){ThemedDialog.Tell(this,"Folder not empty",folder+" already exists. Pick another place.");return;}
    // Save writes every tile to the copy and clears its changed mark; put the marks back so the next Save still
    // writes the tiles changed since the map was last saved.
    string keep=pack.Folder;var changed=new HashSet<Tile>();foreach(var t in pack.Tiles.Values){if(t.Dirty)changed.Add(t);t.Dirty=true;}
    try{pack.Save(folder,game==null?"":game.BuildId);SetStatus("Exported a copy to "+folder,Theme.Ok);}
    catch(Exception e){ThemedDialog.Tell(this,"Could not export",e.Message);}
    finally{pack.Folder=keep;foreach(var t in pack.Tiles.Values)t.Dirty=keep==null||changed.Contains(t);}
   }
  }
  void ShiftAll(int steps){if(!Ready())return;editor.Begin();editor.ShiftAll(steps);if(editor.Commit())Edited(true);SetStatus(steps>0?"Raised the whole map 32 m.":"Lowered the whole map 32 m.",Theme.Ok);}
  // Land at height 0 or below (other than shallow water pieces) is at sea level and shows water through it.
  void WarnSeaLevel(){
   if(pack==null)return;int low=0,land=0;
   foreach(var t in pack.Tiles.Values){if(t.Definition.StartsWith("shallow")||t.Definition.Contains("ocean"))continue;land++;if(t.Height<=0)low++;}
   if(land>0&&low*2>land)SetStatus(low+" land tiles are at sea level (height 0 or below), so water shows through them in game. Use More, Raise the whole map 32 m.",Theme.Warn);
  }
  void ShowMapsFolder(){if(!EnsureGame())return;Directory.CreateDirectory(game.MapsFolder);try{Process.Start("explorer.exe","\""+game.MapsFolder+"\"");}catch(Exception e){SetStatus(e.Message,Theme.Bad);}}
  static string SafeName(string name){foreach(char c in Path.GetInvalidFileNameChars())name=name.Replace(c,'_');name=name.Trim().TrimEnd('.');return name.Length==0?"My map":name;}
  static string FreeName(string parent,string name){for(int i=2;;i++){string p=Path.Combine(parent,name+" "+i);if(!Directory.Exists(p))return p;}}

  // ---------------------------------------------------------------- tools
  static string ToolName(Tool t){
   switch(t){case Tool.Select:return "Look";case Tool.Terrain:return "Ground";case Tool.Height:return "Hills";case Tool.Road:return "Road";
    case Tool.Fence:return "Fence";case Tool.Prop:return "Props";case Tool.Building:return "Build";case Tool.Loot:return "Loot";
    case Tool.Enemy:return "Zombies";case Tool.Start:return "Start";default:return "Erase";}
  }
  static string ToolIcon(Tool t){
   switch(t){case Tool.Select:return "pick";case Tool.Terrain:return "ground";case Tool.Height:return "hills";case Tool.Road:return "road";
    case Tool.Fence:return "fence";case Tool.Prop:return "prop";case Tool.Building:return "house";case Tool.Loot:return "loot";
    case Tool.Enemy:return "enemy";case Tool.Start:return "start";default:return "erase";}
  }
  void FillCatalogBoxes(){
   string piece=pieceBox.SelectedItem as string,prop=propBox.SelectedItem as string;
   pieceBox.Items.Clear();pieceBox.Items.Add("(ocean: remove land)");foreach(var d in catalog.PaletteNames())pieceBox.Items.Add(d);
   pieceBox.SelectedItem=piece!=null&&pieceBox.Items.Contains(piece)?piece:(pieceBox.Items.Contains("tile_flat_a")?"tile_flat_a":pieceBox.Items[0]);
   propBox.Items.Clear();foreach(var d in catalog.Props)propBox.Items.Add(d);
   if(propBox.Items.Count>0)propBox.SelectedItem=prop!=null&&propBox.Items.Contains(prop)?prop:(propBox.Items.Contains("lamp_post_a")?"lamp_post_a":propBox.Items[0]);
  }
  static Label Label(string text){return new Label{Text=text,ForeColor=Theme.Ink,Font=Theme.Font(12.5f),BackColor=Color.Transparent,Height=S(20),AutoEllipsis=true};}
  bool rebuildPending;
  // Tab or the corner button: a big 3D preview with a small map, and back.
  void Show3D(bool on){in3d=on;LayoutShell();(on?(Control)view3d:view).Focus();}
  static string ToolTitle(Tool t){
   switch(t){case Tool.Select:return "Look at a tile";case Tool.Terrain:return "Paint the ground";case Tool.Height:return "Hills and valleys";case Tool.Road:return "Draw roads";
    case Tool.Fence:return "Build fences";case Tool.Prop:return "Place props";case Tool.Building:return "Place buildings";case Tool.Loot:return "Place loot";
    case Tool.Enemy:return "Place zombies";case Tool.Start:return "Set the start point";default:return "Erase things";}
  }
  void SetTool(Tool t){
   view.Tool=t;view.ClearRoad();options.Controls.Clear();toolTitle.Text=ToolTitle(t);
   switch(t){
    case Tool.Select:options.Controls.AddRange(new Control[]{new Hint("Click a tile on the map to see its raw data here. Edit it and press Apply (for experts)."),tileTitle,tileJson,applyJson});break;
    case Tool.Terrain:
     options.Controls.AddRange(new Control[]{new Hint("Click or drag over the map to paint ground. Pick \"ocean\" to remove land."),Label("Ground piece"),pieceBox,Label("Turn"),orientBox,setHeight,heightBox});break;
    case Tool.Height:options.Controls.Add(new Hint("Click or drag on the map to raise the ground 32 m. Hold Ctrl to lower it. Roads and things on it move with it. Watch the 3D preview."));break;
    case Tool.Road:options.Controls.AddRange(new Control[]{new Hint("Click on the map to drop road points. Double click or Enter finishes, Backspace removes the last point, Esc cancels. Then drag the white dots in the 3D preview to shape hills."),Label("Road type"),roadBox});break;
    case Tool.Fence:options.Controls.AddRange(new Control[]{new Hint("Drag a line on the map to build a fence along it."),Label("Fence"),fenceBox});break;
    case Tool.Prop:options.Controls.AddRange(new Control[]{new Hint("Click on the map to place. Drag to aim it."),Label("Prop (lamp_post_a is a street light)"),propBox,Label("Turn"),rotBox});break;
    case Tool.Building:options.Controls.AddRange(new Control[]{new Hint("Click on the map to place a building spot; drag to aim its front. The game picks a building of this kind when a world is made."),Label("Kind of building"),filterBox,Label("Size (0 = any)"),sizeBox});break;
    case Tool.Loot:options.Controls.Add(new Hint("Click on the map to add a loot spot. Drag to aim it."));break;
    case Tool.Enemy:options.Controls.Add(new Hint("Click on the map to add a zombie spawn point."));break;
    case Tool.Start:options.Controls.Add(new Hint("Click on the map where players start a new game (every game mode). The green ring shows it in 3D. Without one, the game's own start points are used."));break;
    case Tool.Erase:options.Controls.AddRange(new Control[]{new Hint("Click or drag over things on the map to remove them."),Label("Brush size"),radiusBox,eraseThings,eraseRoads});break;
   }
   things.Invalidate();LayoutSidebar();view.Invalidate();view.Focus();
  }
  bool Ready(){if(pack==null||editor==null){SetStatus("Open a map first: New island, Open, or More, Open the stock world.",Theme.Warn);return false;}return true;}
  void Edited(bool rebuild){
   unsaved=true;if(rebuild){var scene=MapScene.Build(pack);view.ShowScene(scene);view3d.ShowScene(scene,editor);layers.Reload();}else{view.Invalidate();view3d.Invalidate();}
   UpdateCounts();ShowTile(view.Selected);UpdateUndo();
  }
  void OnTilePainted(TileKey k,MouseButtons b,Keys mods){
   if(!Ready())return;editor.Begin();bool done;
   if(view.Tool==Tool.Height){var t=pack.Get(k);done=t!=null&&editor.SetHeight(k,t.Height+((mods&Keys.Control)!=0?-1:1));}
   else{
    string piece=pieceBox.SelectedItem as string;
    if(pieceBox.SelectedIndex==0)done=editor.RemoveTile(k);
    else done=editor.PaintTile(k,catalog,piece,setHeight.Checked?(int?)heightBox.Value:null,orientBox.SelectedIndex>0?(int?)(orientBox.SelectedIndex-1):null);
   }
   if(done)Edited(false);
  }
  void OnPlaced(double x,double z,double? aim){
   if(!Ready())return;editor.Begin();bool done=false;double rot=aim??(rotBox.Value*Math.PI/180);
   switch(view.Tool){
    case Tool.Prop:done=editor.AddProp((string)propBox.SelectedItem,x,z,rot,0);break;
    case Tool.Building:done=editor.AddBuildingSpot((string)filterBox.SelectedItem,sizeBox.Value,x,z,rot);break;
    case Tool.Loot:done=editor.AddLoot(x,z,rot);break;
    case Tool.Enemy:done=editor.AddEnemySpawn(x,z);break;
    case Tool.Start:if(pack.Get(Grid.At(x,z))!=null){pack.Spawn=editor.Ground(x,z,2);done=true;unsaved=true;UpdateCounts();SetStatus("Start point set. Save to keep it.",Theme.Ok);view.Invalidate();}break;
   }
   if(!done)SetStatus("That spot is ocean. Paint terrain there first.",Theme.Warn);
  }
  void OnLine(double x0,double z0,double x1,double z1){if(!Ready())return;editor.Begin();SetStatus(editor.AddFence((FenceStyle)fenceBox.SelectedItem,x0,z0,x1,z1)+" fence pieces placed",Theme.Dim);}
  void OnRoad(List<Vec3> points){if(!Ready())return;editor.Begin();SetStatus(editor.AddRoad(((RoadChoice)roadBox.SelectedItem).Id,points)+" road links placed",Theme.Dim);if(editor.Commit())Edited(true);}
  void OnErase(double x,double z){if(!Ready())return;editor.Begin();if(editor.Erase(x,z,radiusBox.Value,eraseThings.Checked,eraseRoads.Checked)>0){unsaved=true;UpdateCounts();}}
  void NewIsland(){
   if(!ConfirmDiscard())return;
   int cells;
   using(var d=new ThemedDialog("New blank island",420,96)){
    d.Body.Controls.Add(new Label{Text="Size: grid cells per side (512 m each)",ForeColor=Theme.Muted,Font=Theme.Font(12.5f),Bounds=new Rectangle(0,0,d.Body.Width,S(24))});
    var size=new Stepper(2,120,16){Suffix=" cells"};size.SetBounds(0,S(30),S(170),S(32));d.Body.Controls.Add(size);
    var note=new Label{ForeColor=Theme.Dim,Font=Theme.Font(12),Bounds=new Rectangle(S(184),S(30),d.Body.Width-S(184),S(32)),TextAlign=ContentAlignment.MiddleLeft};d.Body.Controls.Add(note);
    Action show=()=>note.Text="about "+(size.Value*0.512).ToString("0.#")+" km across";show();size.ValueChanged+=show;
    d.AddButton("Create",DialogResult.OK,true);d.AddButton("Cancel",DialogResult.Cancel,false);
    if(d.ShowDialog(this)!=DialogResult.OK)return;cells=size.Value;
   }
   // Centre it on the game's fixed Creative / Sandbox start point, so new players land on the island.
   var spawn=Grid.At(Grid.SpawnCreative.X,Grid.SpawnCreative.Z);int cx=spawn.X,cz=spawn.Z;
   OpenWith(()=>MapEditor.BlankIsland(catalog,cx,cz,cells,cells,MapEditor.LandHeight));
  }

  // For --ui-shot: a small island with one of everything, Road tool selected.
  public void Demo(bool threeD){
   OpenWith(()=>MapEditor.BlankIsland(catalog,10,10,3,3,MapEditor.LandHeight));
   var c=Grid.Origin(new TileKey(10,10,0),0);editor.Begin();
   editor.AddRoad("road",new List<Vec3>{new Vec3(c.X-500,0,c.Z-100),new Vec3(c.X,0,c.Z+80),new Vec3(c.X+500,0,c.Z)});
   editor.AddFence(FenceStyle.All[0],c.X-200,c.Z-200,c.X+100,c.Z-150);
   for(int i=0;i<6;i++)editor.AddProp("lamp_post_a",c.X-400+i*160,c.Z+30,0,0);
   editor.AddBuildingSpot("house",2,c.X+150,c.Z+200,0.3);editor.AddLoot(c.X-100,c.Z+250,0);editor.Commit();Edited(true);
   pack.Spawn=new Vec3(c.X,2,c.Z+120);
   tools.Pick(Tool.Road);view.Selected=new TileKey(10,10,0);ShowTile(view.Selected);
   editor.Begin();editor.SetHeight(new TileKey(11,10,0),3);editor.SetHeight(new TileKey(10,11,0),2);editor.Commit();Edited(true);
   view.FitAll();view3d.LookAt(new Vec3(c.X,32,c.Z),900);view3d.Turn(0.9,0.38);
   if(threeD)Show3D(true);
  }

  // Saves, points the Custom Game page at this map (CustomGame remembers the last page in last.cfg) and starts the
  // game through Steam. In game: New Game, Custom Game, Start.
  void PlayInGame(){
   if(pack==null){SetStatus("Open a map first.",Theme.Warn);return;}
   Save(false);if(unsaved||pack.Folder==null)return;
   string mapName=Path.GetFileName(pack.Folder.TrimEnd('\\','/'));
   try{
    string dir=Path.Combine(game.Folder,"AnyAPI and Modding","mods","CustomGame");Directory.CreateDirectory(dir);
    string cfg=Path.Combine(dir,"last.cfg");var lines=new List<string>();
    if(File.Exists(cfg))foreach(var l in File.ReadAllLines(cfg))if(!l.StartsWith("map="))lines.Add(l);
    if(!lines.Exists(l=>l.Contains("=")))lines.Add("mode=1");   // a file needs one setting to be read; 1 = Creative
    lines.Add("map="+mapName);File.WriteAllLines(cfg,lines);
   }catch(Exception e){SetStatus("Saved, but couldn't pick the map for the Custom Game page: "+e.Message,Theme.Warn);}
   try{Process.Start("steam://rungameid/4435340");SetStatus("Starting Anymaker. Go to New Game, Custom Game: \""+mapName+"\" is already picked in the Map row. Press Start.",Theme.Ok);}
   catch(Exception e){SetStatus("Saved. Couldn't start Steam ("+e.Message+"). Start Anymaker yourself; \""+mapName+"\" is picked on the Custom Game page.",Theme.Warn);}
  }
  void MapDetails(){
   if(pack==null)return;
   using(var d=new ThemedDialog("Map details",440,150)){
    authorBox.SetBounds(0,0,d.Body.Width,S(32));descriptionBox.SetBounds(0,S(44),d.Body.Width,S(32));
    d.Body.Controls.AddRange(new Control[]{authorBox,descriptionBox});d.AddButton("Done",DialogResult.OK,true);d.ShowDialog(this);
    d.Body.Controls.Clear();
   }
  }

  bool showingTile;
  void ShowTile(TileKey? key){
   var t=key==null||pack==null?null:pack.Get(key.Value);showingTile=true;
   if(t==null){tileTitle.Text="No tile picked yet.";tileTitle.ForeColor=Theme.Muted;tileJson.Text="";applyJson.Enabled=false;showingTile=false;return;}
   tileTitle.ForeColor=Theme.Ink;tileTitle.Text=t.Key.FileName+"\n"+t.Definition+", height "+t.Height+" ("+t.Height*32+" m)";
   tileJson.Text=Json.Write(t.Data,true).Replace("\n","\r\n");applyJson.Enabled=false;showingTile=false;
  }
  void ApplyTileJson(){
   var key=view.Selected;if(key==null||pack==null)return;
   JsonObject data;
   try{data=Json.Parse(tileJson.Text) as JsonObject;if(data==null)throw new FormatException("A tile must be a JSON object { ... }");}
   catch(Exception e){ThemedDialog.Tell(this,"That isn't valid tile JSON",e.Message);return;}
   var t=pack.Get(key.Value);if(t==null){ThemedDialog.Tell(this,"No tile there","That tile was removed. Paint terrain there first, then select it again.");return;}
   t.Data=data;t.Dirty=true;unsaved=true;editor=new MapEditor(pack);  // raw edits are not undoable
   var scene=MapScene.Build(pack);view.ShowScene(scene);view3d.ShowScene(scene,editor);layers.Reload();ShowTile(key);UpdateCounts();UpdateUndo();
  }
 }

 // Remembers the game folder between runs (%AppData%\AnyMapMaker\settings.json).
 static class Settings {
  static string File_ {get{return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),"AnyMapMaker","settings.json");}}
  public static string GameFolder {
   get{try{var o=Json.Parse(File.ReadAllText(File_)) as JsonObject;return o==null?null:o.Str("game_folder");}catch{return null;}}
   set{try{Directory.CreateDirectory(Path.GetDirectoryName(File_));var o=new JsonObject();o["game_folder"]=value;File.WriteAllText(File_,Json.Write(o,true));}catch{}}
  }
 }
}
