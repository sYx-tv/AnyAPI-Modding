using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace AnyApiManager {
 // Frameless window: custom title bar, grouped sidebar, titled panels, status bar. Layout is
 // computed in pixels through Theme.S so it follows the Windows display scale; nothing here
 // depends on Windows 11-only APIs.
 public sealed class MainWindow:Form,IMessageFilter {
  static int S(float px){return Theme.S(px);}
  readonly Preferences preferences=Engine.LoadPreferences();Catalog catalog=Engine.Bundled();readonly Catalog bundled=Engine.Bundled();
  readonly Panel titleBar=new Panel(),sidebar=new Panel(),statusBar=new Panel(),content=new Panel(),apiPage=new Panel(),modsPage=new Panel(),settingsPage=new Panel(),developerPage=new Panel();readonly DeveloperPanel developer;
  readonly Dictionary<Panel,ModernButton> navigation=new Dictionary<Panel,ModernButton>();readonly ModernButton playModded=new ModernButton(),playVanilla=new ModernButton();readonly Label launchNote=new Label(),summary=new Label();bool previewMode;readonly Label status=new Label();
  readonly Label apiVersion=new Label(),apiNote=new Label(),gameNote=new Label(),modTitle=new Label(),modId=new Label(),modDescription=new Label(),modNote=new Label(),modHint=new Label();
  readonly Label[] gameValues={new Label(),new Label(),new Label(),new Label()};
  readonly Field gameField=new Field("Paste the Anymaker folder or game.exe path",true),searchField=new Field("Search mods");readonly TextBox gamePath,search;
  readonly Segmented modTabs=new Segmented("Library","Installed","Local");readonly ModList modList=new ModList();
  readonly ModernButton apiInstall=new ModernButton(),modInstall=new ModernButton(),toggle=new ModernButton(),remove=new ModernButton(),importMod=new ModernButton();
  readonly ModernButton managerUpdate=new ModernButton();readonly Label managerUpdateNote=new Label();readonly ManagerUpdater updater;readonly ManagerUpdateBadge badge;
  readonly ToggleSwitch checkAtLaunch=new ToggleSwitch();readonly ModTile modTile=new ModTile();readonly Label[] modKeys={new Label(),new Label(),new Label(),new Label()},modValues={new Label(),new Label(),new Label(),new Label()};readonly ModernButton installAll=new ModernButton(),updateAll=new ModernButton(),updateEverythingTop=new ModernButton();readonly ModernButton enableAll=new ModernButton(),disableAll=new ModernButton();List<Package> installable=new List<Package>(),updatable=new List<Package>(),enableable=new List<Package>(),disableable=new List<Package>();int installedRevision;readonly ActivityLog activity=new ActivityLog();
  readonly Box playBox=new Box("Play"),apiBox=new Box("AnyAPI"),gameBox=new Box("Game build"),activityBox=new Box("Recent activity"),listBox=new Box("Library"),detailBox=new Box("Details"),folderBox=new Box("Game folder"),managerBox=new Box("Manager"),appearanceBox=new Box("Appearance"),libraryBox=new Box("Mod library");
  readonly WindowButton maximize=new WindowButton(WindowGlyph.Maximize);
  readonly ToolTip tips=new ToolTip();string exeHash="",gclHash="";bool busy;Package selectedApi;
  string footerGame="No game folder",footerNote="choose it in Settings";Color footerColor=Theme.Dim,statusColor=Theme.Ok;string statusRight="";
  [DllImport("user32.dll")]static extern bool ReleaseCapture();
  [DllImport("user32.dll")]static extern IntPtr SendMessage(IntPtr h,int msg,IntPtr w,IntPtr l);
  [StructLayout(LayoutKind.Sequential)]struct POINT{public int X,Y;}
  [StructLayout(LayoutKind.Sequential)]struct MINMAXINFO{public POINT Reserved,MaxSize,MaxPosition,MinTrackSize,MaxTrackSize;}

  public MainWindow(bool preview=false){
   previewMode=preview;Theme.Current=Theme.FindAccent(preferences.Accent);gamePath=gameField.Box;search=searchField.Box;
   AutoScaleMode=AutoScaleMode.None;FormBorderStyle=FormBorderStyle.None;Text="AnyAPI Manager";BackColor=Theme.Line;ForeColor=Theme.Ink;Font=Theme.Font(13);DoubleBuffered=true;
   ClientSize=new Size(S(1180),S(760));MinimumSize=new Size(S(1040),S(700));StartPosition=FormStartPosition.CenterScreen;
   using(var icon=System.Reflection.Assembly.GetExecutingAssembly().GetManifestResourceStream("brand.ico"))Icon=new Icon(icon);
   foreach(var bar in new[]{titleBar,sidebar,statusBar})bar.BackColor=Theme.Side;content.BackColor=Theme.Window;
   foreach(var c in new Control[]{titleBar,sidebar,statusBar,content})Controls.Add(c);
   foreach(var bar in new Control[]{titleBar,sidebar,statusBar,content}){var buffered=typeof(Control).GetProperty("DoubleBuffered",System.Reflection.BindingFlags.NonPublic|System.Reflection.BindingFlags.Instance);buffered.SetValue(bar,true,null);}
   updater=new ManagerUpdater();updater.Changed+=ShowManagerUpdate;badge=new ManagerUpdateBadge(updater);badge.Install+=async()=>await InstallManagerUpdate();badge.SizeChanged+=(s,e)=>LayoutTitle();badge.VisibleChanged+=(s,e)=>LayoutTitle();
   BuildTitle();BuildSidebar();BuildStatus();
   foreach(var page in new[]{apiPage,modsPage,settingsPage,developerPage}){page.Dock=DockStyle.Fill;page.BackColor=Theme.Window;content.Controls.Add(page);}
   developer=new DeveloperPanel(SetStatus);developerPage.Controls.Add(developer);BuildApi();BuildMods();BuildSettings();ShowPage(apiPage);
   if(!Engine.IsGameFolder(preferences.GamePath)){string found=Engine.DetectGame();if(found!=""||string.IsNullOrEmpty(preferences.GamePath))preferences.GamePath=found;}preferences.Repository=bundled.Repository;gamePath.Text=preferences.GamePath;
   if(!string.IsNullOrWhiteSpace(preferences.Repository))catalog=Engine.Cached(preferences.Repository)??catalog;
   checkAtLaunch.Checked=preferences.CheckManagerUpdates;
   Load+=async (s,e)=>{if(preview){PopulatePreview();return;}await Run(async()=>{await Scan();if(!string.IsNullOrWhiteSpace(preferences.Repository))await Connect();});
    string updated=null;try{updated=ManagerUpdater.FinishPrevious();}catch(Exception failure){ManagerLog.Error("Manager update cleanup",failure);}if(updated!=null){SetStatus(updated);ManagerLog.Write(updated);}if(preferences.CheckManagerUpdates)await updater.Check(true);};
   Resize+=(s,e)=>{LayoutShell();maximize.Maximized=WindowState==FormWindowState.Maximized;maximize.Invalidate();};LayoutShell();
  }

  // ---- window chrome -------------------------------------------------------------------
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
  // Resizing a borderless window: edge hits anywhere in the window start a native resize.
  public bool PreFilterMessage(ref Message m){
   if(m.Msg!=0x200&&m.Msg!=0x201)return false;var c=Control.FromChildHandle(m.HWnd);if(c==null||c.FindForm()!=this)return false;
   int hit=Edge(PointToClient(Cursor.Position));if(hit==0)return false;
   Cursor.Current=hit==10||hit==11?Cursors.SizeWE:hit==12||hit==15?Cursors.SizeNS:hit==13||hit==17?Cursors.SizeNWSE:Cursors.SizeNESW;
   if(m.Msg==0x201){ReleaseCapture();SendMessage(Handle,0xA1,new IntPtr(hit),IntPtr.Zero);return true;}return false;
  }
  void DragWindow(MouseEventArgs e){if(e.Button!=MouseButtons.Left||Edge(PointToClient(Cursor.Position))!=0)return;if(e.Clicks==2){ToggleMaximize();return;}ReleaseCapture();SendMessage(Handle,0xA1,new IntPtr(2),IntPtr.Zero);}
  void ToggleMaximize(){WindowState=WindowState==FormWindowState.Maximized?FormWindowState.Normal:FormWindowState.Maximized;}
  void LayoutShell(){
   int b=WindowState==FormWindowState.Maximized?0:1;var r=new Rectangle(b,b,ClientSize.Width-b*2,ClientSize.Height-b*2);int title=S(38),foot=S(30),side=S(208);
   titleBar.Bounds=new Rectangle(r.X,r.Y,r.Width,title);statusBar.Bounds=new Rectangle(r.X,r.Bottom-foot,r.Width,foot);
   sidebar.Bounds=new Rectangle(r.X,r.Y+title,side,r.Height-title-foot);content.Bounds=new Rectangle(r.X+side,r.Y+title,r.Width-side,r.Height-title-foot);
   content.Padding=new Padding(S(22),S(18),S(22),S(18));LayoutTitle();LayoutSidebar();
  }
  void BuildTitle(){
   var minimize=new WindowButton(WindowGlyph.Minimize);var close=new WindowButton(WindowGlyph.Close);
   minimize.Click+=(s,e)=>WindowState=FormWindowState.Minimized;maximize.Click+=(s,e)=>ToggleMaximize();close.Click+=(s,e)=>Close();
   titleBar.Controls.AddRange(new Control[]{badge,minimize,maximize,close});titleBar.MouseDown+=(s,e)=>DragWindow(e);
   var brand=Theme.Font(13,true);var sub=Theme.Semibold(10.5f);
   titleBar.Paint+=(s,e)=>{var g=e.Graphics;int x=S(16);
    using(var glow=new SolidBrush(Color.FromArgb(26,Theme.Blue))){var any=TextRenderer.MeasureText(g,"ANY",brand,Size.Empty,TextFormatFlags.NoPadding);g.SmoothingMode=SmoothingMode.AntiAlias;g.FillEllipse(glow,x-S(6),titleBar.Height/2-S(9),any.Width+S(12),S(18));}
    Theme.Caption(g,"ANY",brand,Theme.Blue,x,0,titleBar.Height);x+=(int)(TextRenderer.MeasureText(g,"ANY",brand,Size.Empty,TextFormatFlags.NoPadding).Width+brand.Size*.42f);
    Theme.Caption(g,"API",brand,Theme.Ink,x,0,titleBar.Height);x+=(int)(TextRenderer.MeasureText(g,"API",brand,Size.Empty,TextFormatFlags.NoPadding).Width+brand.Size*.42f)+S(10);
    Theme.Caption(g,"MANAGER",sub,Theme.Dim,x,S(1),titleBar.Height);
    using(var line=new Pen(Theme.Line))g.DrawLine(line,0,titleBar.Height-1,titleBar.Width,titleBar.Height-1);};
  }
  void LayoutTitle(){
   int x=titleBar.Width-S(4);var buttons=titleBar.Controls.OfType<WindowButton>().ToArray();
   for(int i=buttons.Length-1;i>=0;i--){x-=buttons[i].Width;buttons[i].Location=new Point(x,(titleBar.Height-buttons[i].Height)/2-1);}
   badge.Location=new Point(x-S(10)-badge.Width,(titleBar.Height-badge.Height)/2-1);titleBar.Invalidate();
  }
  void BuildSidebar(){
   var pages=new[]{apiPage,modsPage,developerPage,settingsPage};var titles=new[]{"Overview","Mods","Develop","Settings"};var icons=new[]{NavIcon.Home,NavIcon.Grid,NavIcon.Code,NavIcon.Gear};
   for(int i=0;i<pages.Length;i++){var page=pages[i];var nav=new ModernButton{Text=titles[i],Kind=ButtonKind.Nav,Icon=icons[i],Font=Theme.Font(13)};nav.Click+=(s,e)=>ShowPage(page);sidebar.Controls.Add(nav);navigation.Add(page,nav);}
   var group=Theme.Semibold(10);var small=Theme.Mono(11);var body=Theme.Font(12.5f);
   sidebar.Paint+=(s,e)=>{var g=e.Graphics;
    using(var line=new Pen(Theme.Line)){g.DrawLine(line,sidebar.Width-1,0,sidebar.Width-1,sidebar.Height);int fy=sidebar.Height-S(78);g.DrawLine(line,S(12),fy,sidebar.Width-S(13),fy);
     Theme.Dot(g,footerColor,S(24),fy+S(22),S(7));TextRenderer.DrawText(g,footerGame,body,new Rectangle(S(36),fy+S(12),sidebar.Width-S(46),S(20)),Theme.Ink,TextFormatFlags.Left|TextFormatFlags.VerticalCenter|TextFormatFlags.EndEllipsis);
     TextRenderer.DrawText(g,footerNote,small,new Point(S(35),fy+S(34)),Theme.Dim);TextRenderer.DrawText(g,"manager "+ManagerUpdates.DisplayVersion,small,new Point(S(35),fy+S(50)),Theme.Dim);}
    foreach(var nav in navigation.Values)Theme.Caption(g,nav==navigation[apiPage]?"Game":nav==navigation[modsPage]?"Library":nav==navigation[developerPage]?"Build":"Manager",group,Theme.Dim,S(22),nav.Top-S(22),S(18));};
  }
  void LayoutSidebar(){int y=S(18);foreach(var nav in navigation.Values){y+=S(22);nav.Bounds=new Rectangle(S(12),y,sidebar.Width-S(25),S(34));y+=S(34)+S(14);}sidebar.Invalidate();}
  void BuildStatus(){
   status.AutoEllipsis=true;status.ForeColor=Theme.Muted;status.BackColor=Theme.Side;status.Font=Theme.Font(12);status.TextAlign=ContentAlignment.MiddleLeft;statusBar.Controls.Add(status);
   var mono=Theme.Mono(11);
   statusBar.Paint+=(s,e)=>{var g=e.Graphics;using(var line=new Pen(Theme.Line))g.DrawLine(line,0,0,statusBar.Width,0);Theme.Dot(g,statusColor,S(18),statusBar.Height/2,S(7));
    TextRenderer.DrawText(g,statusRight,mono,new Rectangle(0,0,statusBar.Width-S(14),statusBar.Height),Theme.Dim,TextFormatFlags.Right|TextFormatFlags.VerticalCenter);};
   statusBar.Resize+=(s,e)=>status.Bounds=new Rectangle(S(30),1,statusBar.Width-S(260),statusBar.Height-1);
  }

  // ---- shared helpers ------------------------------------------------------------------
  Label Label(string text,float size,bool bold=false,Color? color=null){return new Label{Text=text,Font=bold?Theme.Font(size,true):Theme.Font(size),ForeColor=color??Theme.Ink,AutoEllipsis=true,BackColor=Color.Transparent};}
  ModernButton Button(string text,ButtonKind kind,Action action){var b=new ModernButton{Text=text,Kind=kind};b.Click+=(s,e)=>{if(!busy)action();};return b;}
  void Style(ModernButton b,string text,ButtonKind kind){b.Text=text;b.Kind=kind;}
  // Page header: title on the left, optional controls flowing right to left from the edge.
  Panel Header(Panel page,string title,string crumb,params Control[] right){
   var head=new Panel{Dock=DockStyle.Top,Height=S(46),BackColor=Theme.Window};var t=Label(title,19,true);var c=Label(crumb,12,false,Theme.Dim);head.Tag=t;head.Controls.Add(t);if(crumb!="")head.Controls.Add(c);head.Controls.AddRange(right);foreach(var control in right)control.BringToFront();
   head.Resize+=(s,e)=>{t.SetBounds(0,0,TextRenderer.MeasureText(t.Text,t.Font).Width+S(6),S(32));c.SetBounds(t.Right+S(8),S(8),S(220),S(20));int x=head.Width;for(int i=right.Length-1;i>=0;i--){x-=right[i].Width;right[i].Location=new Point(x,(S(32)-right[i].Height)/2);x-=S(8);}};
   page.Controls.Add(head);return head;
  }
  static Point In(int x,int y){return new Point(S(14)+x,Box.HeaderHeight+S(14)+y);}
  void ShowPage(Panel page){foreach(Control p in content.Controls)p.Visible=p==page;page.BringToFront();foreach(var nav in navigation){nav.Value.Selected=nav.Key==page;nav.Value.Invalidate();}}
  // Two-column grid of boxes: rows of {box} (spans both columns) or {left,right}.
  void Grid(Panel page,int top,params object[] rows){
   int gap=S(14),w=page.ClientSize.Width,half=(w-gap)/2,y=top;
   foreach(var row in rows){var pair=row as Tuple<Box,Box,int>;var single=row as Tuple<Box,int>;
    if(single!=null){int h=single.Item2<0?Math.Max(S(110),page.ClientSize.Height-y):single.Item2;single.Item1.Bounds=new Rectangle(0,y,w,h);y+=h+gap;}
    else{pair.Item1.Bounds=new Rectangle(0,y,half,pair.Item3);pair.Item2.Bounds=new Rectangle(half+gap,y,w-half-gap,pair.Item3);y+=pair.Item3+gap;}}
  }

  // ---- Overview ------------------------------------------------------------------------
  void BuildApi(){
   Style(updateEverythingTop,"Update everything",ButtonKind.Primary);updateEverythingTop.Size=new Size(S(176),S(30));updateEverythingTop.Click+=async(s,e)=>{if(!busy)await UpdateEverything();};tips.SetToolTip(updateEverythingTop,"Updates AnyAPI, every installed mod with a newer release, then the manager itself.");
   var head=Header(apiPage,"Overview","Anymaker · Steam",updateEverythingTop);
   var playTitle=summary;playTitle.Font=Theme.Semibold(15);launchNote.ForeColor=Theme.Muted;launchNote.Font=Theme.Font(12.5f);
   Style(playModded,"Play with mods",ButtonKind.Hero);Style(playVanilla,"Play without mods",ButtonKind.Ghost);playModded.Size=new Size(S(196),S(50));playVanilla.Size=new Size(S(168),S(50));
   playModded.Click+=async(s,e)=>await Launch(true);playVanilla.Click+=async(s,e)=>await Launch(false);playBox.Controls.AddRange(new Control[]{playTitle,launchNote,playModded,playVanilla});
   playBox.Resize+=(s,e)=>{int top=Box.HeaderHeight;int mid=top+(playBox.Height-top)/2;playModded.Location=new Point(playBox.Width-S(14)-playModded.Width,mid-playModded.Height/2);playVanilla.Location=new Point(playModded.Left-S(8)-playVanilla.Width,playModded.Top);
    int w=Math.Max(S(120),playVanilla.Left-S(28));playTitle.SetBounds(S(14),mid-S(22),w,S(22));launchNote.SetBounds(S(14),mid+S(2),w,S(20));};
   apiVersion.Font=Theme.Font(30,true);apiVersion.ForeColor=Theme.Ink;apiNote.ForeColor=Theme.Muted;apiNote.Font=Theme.Font(12.5f);
   Style(apiInstall,"Install API",ButtonKind.Primary);apiInstall.Click+=async(s,e)=>{if(!busy&&selectedApi!=null)await Install(selectedApi);};
   var apiCheck=Button("Check for updates",ButtonKind.Secondary,async()=>await Run(async()=>{await Scan();if(!string.IsNullOrWhiteSpace(preferences.Repository))await Connect();else SetStatus("Connect your repository in Settings for online updates.");}));
   apiBox.Controls.AddRange(new Control[]{apiVersion,apiNote,apiInstall,apiCheck});
   apiBox.Resize+=(s,e)=>{apiVersion.SetBounds(S(12),Box.HeaderHeight+S(10),apiBox.Width-S(24),S(44));apiNote.SetBounds(S(14),apiVersion.Bottom,apiBox.Width-S(28),S(20));apiInstall.SetBounds(S(14),apiBox.Height-S(46),S(132),S(32));apiCheck.SetBounds(apiInstall.Right+S(8),apiInstall.Top,S(148),S(32));};
   var keys=new[]{"Version","game.exe","bin/game.gcl","Folder"};var keyLabels=keys.Select(k=>Label(k,12,false,Theme.Muted)).ToArray();
   foreach(var v in gameValues){v.Font=Theme.Mono(12);v.ForeColor=Theme.Ink;v.AutoEllipsis=true;}gameBox.Controls.AddRange(keyLabels);gameBox.Controls.AddRange(gameValues);
   gameBox.Resize+=(s,e)=>{for(int i=0;i<keys.Length;i++){int y=Box.HeaderHeight+S(14)+i*S(27);keyLabels[i].SetBounds(S(14),y,S(96),S(20));gameValues[i].SetBounds(S(112),y+S(1),gameBox.Width-S(126),S(20));}};
   tips.SetToolTip(gameValues[3],"Change it in Settings.");
   activityBox.Controls.Add(activity);activityBox.Resize+=(s,e)=>activity.SetBounds(S(14),Box.HeaderHeight+S(12),activityBox.Width-S(28),activityBox.Height-Box.HeaderHeight-S(20));
   apiPage.Controls.AddRange(new Control[]{playBox,apiBox,gameBox,activityBox});
   apiPage.Resize+=(s,e)=>Grid(apiPage,head.Height,Tuple.Create(playBox,S(104)),Tuple.Create(apiBox,gameBox,S(172)),Tuple.Create(activityBox,-1));
   tips.SetToolTip(apiInstall,"Bundles only the API. Optional mods are downloaded separately.");tips.SetToolTip(playVanilla,"Pauses AnyAPI. Saved settings and individual mod choices are kept. Steam stays in this mode until you choose Play with mods.");
  }
  async Task Launch(bool modded){await Run(async()=>{
   if(previewMode)return;if(Engine.Running())throw new IOException("Anymaker is already running. Close it before switching modes.");await Scan();
   bool wasPaused=Engine.ApiPaused(preferences.GamePath);int revision=Engine.ApiRevision(preferences.GamePath,catalog);
   var r=await Engine.Execute(new ApplyRequest{GamePath=preferences.GamePath,Catalog=catalog,Action=modded?"prepare-modded":"prepare-vanilla"});if(!r.Success)throw new IOException(r.Message);
   Exception launchError=null;try{System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo("steam://rungameid/4435340"){UseShellExecute=true});SetStatus(modded?"Launching with your enabled mods.":"Launching with AnyAPI paused. Choose Play with mods to restore it.");}catch(Exception error){launchError=error;}
   if(launchError!=null){if(revision>0&&wasPaused!=Engine.ApiPaused(preferences.GamePath)){var restored=await Engine.Execute(new ApplyRequest{GamePath=preferences.GamePath,Catalog=catalog,Action=wasPaused?"prepare-vanilla":"prepare-modded"});if(!restored.Success)throw new IOException("Steam could not open. "+restored.Message);}throw new IOException("Steam could not open: "+launchError.Message);}
  });}

  // ---- Mods ----------------------------------------------------------------------------
  void BuildMods(){
   searchField.Size=new Size(S(220),S(30));search.TextChanged+=(s,e)=>Rows();modTabs.Changed+=Rows;
   Style(importMod,"Import DLL",ButtonKind.Primary);importMod.Size=new Size(S(104),S(30));importMod.Click+=async(s,e)=>{if(!busy)await ImportLocal();};
   var refresh=Button("Refresh",ButtonKind.Ghost,async()=>await Run(async()=>await Scan()));refresh.Size=new Size(S(80),S(30));
   var folder=Button("Folder",ButtonKind.Ghost,()=>{string path=Rules.Target(preferences.GamePath,"AnyAPI and Modding/mods");if(Directory.Exists(path))System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(path){UseShellExecute=true});else SetStatus("Install AnyAPI or import a mod first.");});folder.Size=new Size(S(72),S(30));
   var head=Header(modsPage,"Mods","",modTabs,searchField,refresh,folder,importMod);head.Resize+=(s,e)=>{modTabs.Left=((Control)head.Tag).Right+S(14);};
   tips.SetToolTip(refresh,"Rescan the mods folder.");tips.SetToolTip(folder,"Open the mods folder.");
   Style(enableAll,"Enable all",ButtonKind.Ghost);Style(disableAll,"Disable all",ButtonKind.Ghost);Style(installAll,"Install all",ButtonKind.Secondary);Style(updateAll,"Update all",ButtonKind.Primary);
   enableAll.Click+=async(s,e)=>{if(!busy)await SwitchAll(true);};disableAll.Click+=async(s,e)=>{if(!busy)await SwitchAll(false);};
   tips.SetToolTip(enableAll,"Turns on every installed mod that is switched off.");tips.SetToolTip(disableAll,"Turns off every installed mod. Their files and settings stay.");
   installAll.Click+=async(s,e)=>{if(!busy)await Bulk("Install all",installable.ToList(),true,false);};updateAll.Click+=async(s,e)=>{if(!busy)await Bulk("Update all",updatable.ToList(),false,false);};   tips.SetToolTip(installAll,"Installs every compatible library mod you don't have yet, and AnyAPI first if it's missing.");tips.SetToolTip(updateAll,"Updates every installed mod that has a newer release.");
   var bulkLine=new Panel{BackColor=Theme.Line,Height=1};listBox.Controls.AddRange(new Control[]{modList,bulkLine,enableAll,disableAll,installAll,updateAll});
   listBox.Resize+=(s,e)=>{int bar=S(52),by=listBox.Height-bar;modList.SetBounds(1,Box.HeaderHeight+1,listBox.Width-2,by-Box.HeaderHeight-1);bulkLine.SetBounds(1,by,listBox.Width-2,1);int y=by+(bar-S(32))/2;
    var bulk=new[]{enableAll,disableAll,installAll,updateAll};int gap=S(8),w=(listBox.Width-S(28)-gap*3)/4;for(int i=0;i<bulk.Length;i++)bulk[i].SetBounds(S(14)+i*(w+gap),y,w,S(32));};
   modList.SelectionChanged+=Details;modList.ToggleRequested+=async row=>{if(!busy)await Change(row.Package,row.On?"disable":"enable");};
   modTitle.Font=Theme.Font(20,true);modId.Font=Theme.Mono(11.5f);modId.ForeColor=Theme.Dim;modDescription.ForeColor=Theme.Muted;modDescription.Font=Theme.Font(13);modDescription.AutoEllipsis=true;modNote.ForeColor=Theme.Muted;modNote.Font=Theme.Font(12.5f);
   modHint.Text="Changes apply the next time Anymaker starts.";modHint.ForeColor=Theme.Dim;modHint.Font=Theme.Font(12);
   Style(modInstall,"Install",ButtonKind.Primary);Style(toggle,"Disable",ButtonKind.Secondary);Style(remove,"Remove",ButtonKind.Danger);
   var release=Button("View release",ButtonKind.Ghost,()=>{var p=Selected();string page=ReleasePage(p);if(page!=null)System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(page){UseShellExecute=true});});tips.SetToolTip(release,"Opens this release on GitHub.");
   var keys=new[]{"Category","Latest","Installed","Requires"};for(int i=0;i<4;i++){modKeys[i].Text=keys[i];modKeys[i].Font=Theme.Font(12);modKeys[i].ForeColor=Theme.Muted;modValues[i].Font=Theme.Mono(12);modValues[i].ForeColor=Theme.Ink;modValues[i].AutoEllipsis=true;}
   detailBox.Controls.AddRange(new Control[]{modTile,modTitle,modId,modDescription,modNote,modInstall,toggle,remove,modHint,release});detailBox.Controls.AddRange(modKeys);detailBox.Controls.AddRange(modValues);detailBox.Tag=release;
   detailBox.Resize+=(s,e)=>{int w=detailBox.Width-S(28),x=S(14),y=Box.HeaderHeight+S(14);modTile.Location=new Point(x,y);int tx=x+modTile.Width+S(12);modTitle.SetBounds(tx,y+S(4),w-tx+x,S(30));modId.SetBounds(tx,modTitle.Bottom,w-tx+x,S(18));
    modDescription.SetBounds(x,modTile.Bottom+S(12),w,S(56));for(int i=0;i<4;i++){int ry=modDescription.Bottom+S(6)+i*S(23);modKeys[i].SetBounds(x,ry,S(84),S(20));modValues[i].SetBounds(x+S(86),ry+S(1),w-S(86),S(20));}
    modNote.SetBounds(x,modDescription.Bottom+S(6)+4*S(23)+S(4),w,S(38));
    modInstall.SetBounds(x,modNote.Bottom+S(8),w,S(34));toggle.SetBounds(x,modInstall.Bottom+S(8),(w-S(8))/2,S(32));remove.SetBounds(toggle.Right+S(8),toggle.Top,w-toggle.Width-S(8),S(32));modHint.SetBounds(x,detailBox.Height-S(34),w-S(120),S(20));((Control)detailBox.Tag).SetBounds(detailBox.Width-S(14)-S(112),detailBox.Height-S(42),S(112),S(30));};
   modInstall.Click+=async(s,e)=>{if(!busy&&Selected()!=null)await Install(Selected());};toggle.Click+=async(s,e)=>{if(!busy&&Selected()!=null)await Change(Selected(),toggle.Text=="Enable"?"enable":"disable");};remove.Click+=async(s,e)=>{if(!busy&&Selected()!=null&&MessageBox.Show(this,"Remove "+Selected().Name+"? Saved settings will stay.","Remove mod",MessageBoxButtons.YesNo,MessageBoxIcon.Question)==DialogResult.Yes)await Change(Selected(),"remove");};tips.SetToolTip(toggle,"Takes effect next time you launch Anymaker.");tips.SetToolTip(remove,"Removes the DLL and keeps your saved settings.");
   modsPage.Controls.AddRange(new Control[]{listBox,detailBox});
   modsPage.Resize+=(s,e)=>{int top=head.Height,gap=S(14),w=modsPage.ClientSize.Width,left=(int)((w-gap)*.56),h=modsPage.ClientSize.Height-top;listBox.SetBounds(0,top,left,h);detailBox.SetBounds(left+gap,top,w-left-gap,h);};
  }

  // ---- Settings ------------------------------------------------------------------------
  void BuildSettings(){
   var head=Header(settingsPage,"Settings","");
   var browse=Button("Browse",ButtonKind.Secondary,async()=>{using(var dialog=new OpenFileDialog{Title="Select game.exe in your Anymaker folder",Filter="Anymaker (game.exe)|game.exe|Programs (*.exe)|*.exe",CheckFileExists=true,InitialDirectory=Directory.Exists(gamePath.Text.Trim())?gamePath.Text.Trim():""})if(dialog.ShowDialog(this)==DialogResult.OK){gamePath.Text=dialog.FileName;await SaveSetup();}});
   var find=Button("Find automatically",ButtonKind.Ghost,async()=>{string found=Engine.DetectGame();if(found==""){SetStatus("Anymaker wasn't found automatically. Choose Browse and select game.exe.");return;}gamePath.Text=found;await SaveSetup();});
   var save=Button("Save folder",ButtonKind.Primary,async()=>await SaveSetup());
   var folderHint=Label("Paste a folder or game.exe path, browse to game.exe, or let the manager search your Steam libraries.",12,false,Theme.Muted);
   folderBox.Controls.AddRange(new Control[]{gameField,browse,find,save,folderHint});
   folderBox.Resize+=(s,e)=>{int x=S(14),y=Box.HeaderHeight+S(14);save.SetBounds(folderBox.Width-S(14)-S(112),y,S(112),S(32));find.SetBounds(save.Left-S(8)-S(148),y,S(148),S(32));browse.SetBounds(find.Left-S(8)-S(88),y,S(88),S(32));gameField.SetBounds(x,y,browse.Left-S(8)-x,S(32));folderHint.SetBounds(x,y+S(42),folderBox.Width-S(28),S(20));};

   managerUpdateNote.Text="Version "+ManagerUpdates.DisplayVersion;managerUpdateNote.ForeColor=Theme.Muted;managerUpdateNote.Font=Theme.Font(12.5f);managerBox.SetNote(ManagerUpdates.DisplayVersion,Theme.Muted,false);
   var launchTitle=Label("Check for manager updates at launch",13);var launchHint=Label("Shows Restart to update in the title bar when one is ready.",12,false,Theme.Muted);
   checkAtLaunch.Toggled+=()=>{preferences.CheckManagerUpdates=checkAtLaunch.Checked;try{Engine.SavePreferences(preferences);}catch(Exception error){ManagerLog.Error("Save preferences",error);}SetStatus(checkAtLaunch.Checked?"The manager will check for updates when it opens.":"Launch update checks are off. Use Check for updates any time.");};
   Style(managerUpdate,"Check for updates",ButtonKind.Primary);managerUpdate.Click+=async(s,e)=>{if(!busy)await UpdateManager();};tips.SetToolTip(managerUpdate,"Updates the manager EXE. Your mods and settings stay in place.");
   var backups=Button("Open backups",ButtonKind.Ghost,()=>{string path=Rules.Target(preferences.GamePath,"AnyAPI and Modding/.manager/backups");if(Directory.Exists(path))System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(path){UseShellExecute=true});else SetStatus("No manager backups yet.");});
   var log=Button("Open log",ButtonKind.Ghost,()=>{if(File.Exists(ManagerLog.LogFile))System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(ManagerLog.LogFile){UseShellExecute=true});else SetStatus("No manager log yet.");});tips.SetToolTip(log,"Opens manager.log. Attach it when you report a problem.");
   managerBox.Controls.AddRange(new Control[]{managerUpdateNote,launchTitle,launchHint,checkAtLaunch,managerUpdate,backups,log});
   managerBox.Resize+=(s,e)=>{int x=S(14),w=managerBox.Width-S(28),y=Box.HeaderHeight+S(12);managerUpdateNote.SetBounds(x,y,w,S(20));
    launchTitle.SetBounds(x,y+S(32),w-S(44),S(20));launchHint.SetBounds(x,y+S(52),w-S(44),S(20));checkAtLaunch.Location=new Point(managerBox.Width-S(14)-checkAtLaunch.Width,y+S(44));
    int by=managerBox.Height-S(46);managerUpdate.SetBounds(x,by,S(150),S(32));backups.SetBounds(managerUpdate.Right+S(8),by,S(116),S(32));log.SetBounds(backups.Right+S(8),by,S(92),S(32));};

   var accentTitle=Label("Accent",13);var accentHint=Label("Highlights, switches and the Play button.",12,false,Theme.Muted);var swatches=new SwatchPicker();
   swatches.Picked+=a=>{Theme.Current=a;preferences.Accent=a.Name;try{Engine.SavePreferences(preferences);}catch(Exception error){ManagerLog.Error("Save preferences",error);}Invalidate(true);SetStatus("Accent set to "+a.Name+".");};
   appearanceBox.Controls.AddRange(new Control[]{accentTitle,accentHint,swatches});
   appearanceBox.Resize+=(s,e)=>{int x=S(14),y=Box.HeaderHeight+S(12);accentTitle.SetBounds(x,y+S(32),S(160),S(20));accentHint.SetBounds(x,y+S(52),appearanceBox.Width-swatches.Width-S(40),S(20));swatches.Location=new Point(appearanceBox.Width-S(14)-swatches.Width,y+S(40));};

   var libTitle=Label("The official library is built in",13);var libHint=Label("No GitHub account or setup needed.",12,false,Theme.Muted);
   var refresh=Button("Refresh library",ButtonKind.Secondary,async()=>await Run(async()=>await Connect()));var github=Button("View on GitHub",ButtonKind.Ghost,()=>System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(bundled.Repository){UseShellExecute=true}));
   libraryBox.Controls.AddRange(new Control[]{libTitle,libHint,refresh,github});
   libraryBox.Resize+=(s,e)=>{int y=Box.HeaderHeight+(libraryBox.Height-Box.HeaderHeight)/2;github.SetBounds(libraryBox.Width-S(14)-S(132),y-S(16),S(132),S(32));refresh.SetBounds(github.Left-S(8)-S(132),y-S(16),S(132),S(32));libTitle.SetBounds(S(14),y-S(20),refresh.Left-S(28),S(20));libHint.SetBounds(S(14),y,refresh.Left-S(28),S(20));};
   settingsPage.Controls.AddRange(new Control[]{folderBox,managerBox,appearanceBox,libraryBox});
   settingsPage.Resize+=(s,e)=>Grid(settingsPage,head.Height,Tuple.Create(folderBox,S(116)),Tuple.Create(managerBox,appearanceBox,S(176)),Tuple.Create(libraryBox,S(92)));
  }
  async Task UpdateManager(){if(updater.UpdateAvailable)await InstallManagerUpdate();else if(!busy)await updater.Check(false);}
  async Task InstallManagerUpdate(){await Run(updater.Install);if(updater.State==ManagerUpdateState.Restarting)Application.Exit();}
  void ShowManagerUpdate(){
   managerUpdate.Text=updater.UpdateAvailable?"Update & restart":"Check for updates";
   managerUpdateNote.Text="Version "+ManagerUpdates.DisplayVersion+(updater.UpdateAvailable?" · "+updater.Release.Version+" available":updater.State==ManagerUpdateState.UpToDate?" · Up to date":"");
   managerBox.SetNote(updater.UpdateAvailable?updater.Release.Version+" ready":ManagerUpdates.DisplayVersion,updater.UpdateAvailable?Theme.Blue:Theme.Muted,updater.UpdateAvailable);
   if(!busy&&(updater.State==ManagerUpdateState.Available||updater.State==ManagerUpdateState.Idle||updater.State==ManagerUpdateState.UpToDate))Rows();
   if(updater.Message!=null)SetStatus(updater.State==ManagerUpdateState.Downloading&&updater.Progress>=0?updater.Message+" "+(int)(updater.Progress*100)+"%":updater.Message);
  }
  async Task SaveSetup(){await Run(async()=>{preferences.GamePath=Engine.ResolveGameFolder(gamePath.Text);gamePath.Text=preferences.GamePath;await Scan();Engine.SavePreferences(preferences);SetStatus("Game folder saved.");});}
  async Task Scan(){
   if(string.IsNullOrWhiteSpace(preferences.GamePath)){exeHash=gclHash="";RefreshState();SetStatus("Choose your Anymaker folder in Settings.");return;}
   exeHash=gclHash="";var hashes=await Task.Run(()=>new[]{Rules.Hash(Rules.Target(preferences.GamePath,"game.exe")),Rules.Hash(Rules.Target(preferences.GamePath,"bin/game.gcl"))});exeHash=hashes[0];gclHash=hashes[1];ManagerLog.Write("Game folder "+preferences.GamePath+" game.exe "+exeHash+" game.gcl "+gclHash);RefreshState();
  }
  async Task Connect(){var fresh=await Engine.Fetch(preferences.Repository);catalog=fresh;Engine.Cache(preferences.Repository,catalog);RefreshState();SetStatus("Catalog updated. "+catalog.Mods.Count+" mods available.");}
  void RefreshState(){
   selectedApi=catalog.Api.Concat(bundled.Api).Where(p=>Rules.Matches(p,exeHash,gclHash)).OrderByDescending(p=>p.Revision).FirstOrDefault();
   int installed=0;try{if(exeHash!="")installed=Engine.ApiRevision(preferences.GamePath,catalog);}catch(Exception e){SetStatus(e.Message);}
   installedRevision=installed;Package installedApi=null;try{if(exeHash!="")installedApi=Engine.MatchingInstalledApi(preferences.GamePath,catalog,exeHash,gclHash);}catch(Exception e){SetStatus(e.Message);}
   var installedPackage=catalog.Api.Concat(bundled.Api).Concat(installedApi==null?new Package[0]:new[]{installedApi}).FirstOrDefault(p=>p.Revision==installed);
   apiVersion.Text=installed>0?(installedPackage!=null&&!string.IsNullOrEmpty(installedPackage.Version)?installedPackage.Version:"Revision "+installed):"Not installed";
   apiNote.Text=(installed>0?"Revision "+installed+" · ":"")+(selectedApi==null?(installedApi!=null?"Installed API matches this game build.":"Waiting for a release verified for this game build."):installed>=selectedApi.Revision?"Matches this game build.":"Revision "+selectedApi.Revision+" is ready to install.");
   apiInstall.Text=installed==0?"Install API":selectedApi!=null&&installed<selectedApi.Revision?"Update API":"Reinstall API";apiInstall.Kind=installed==0||selectedApi!=null&&installed<selectedApi.Revision?ButtonKind.Primary:ButtonKind.Secondary;apiInstall.Enabled=selectedApi!=null&&!busy;
   apiBox.SetNote(installed==0?"Not installed":selectedApi!=null&&installed<selectedApi.Revision?"Update ready":selectedApi==null&&installedApi==null?"Waiting":"Up to date",installed==0?Theme.Muted:selectedApi!=null&&installed<selectedApi.Revision?Theme.Blue:selectedApi==null&&installedApi==null?Theme.Warn:Theme.Ok,true);
   var match=catalog.Api.Concat(bundled.Api).Concat(installedApi==null?new Package[0]:new[]{installedApi}).SelectMany(p=>p.GameBuilds).FirstOrDefault(b=>b.ExeSha256==exeHash&&b.GclSha256==gclHash);developer.InstalledRevision(installed);
   bool paused=installed>0&&Engine.ApiPaused(preferences.GamePath);launchNote.Text=paused?"Mods paused · normal Steam launches use this mode too":"Your enabled mods load automatically through Steam.";playModded.Enabled=!busy&&installedApi!=null;playVanilla.Enabled=!busy&&exeHash!="";
   playBox.SetNote(exeHash==""?"No game folder":installedApi==null?"API needed":paused?"Mods paused":"Mods active",exeHash==""||installedApi==null?Theme.Muted:paused?Theme.Warn:Theme.Ok,true);
   gameNote.Text=exeHash==""?"Game folder not selected":match==null?"Game build changed. Check for a compatible API release.":"Anymaker "+match.Version+"   ·   Matched build";
   gameValues[0].Text=match!=null?match.Version+(match.SteamBuild>0?" · Steam "+match.SteamBuild:""):exeHash==""?"—":"Unrecognized build";gameValues[1].Text=exeHash==""?"—":match!=null?"SHA-256 verified":exeHash.Substring(0,16)+"…";gameValues[2].Text=gclHash==""?"—":match!=null?"SHA-256 verified":gclHash.Substring(0,16)+"…";gameValues[3].Text=string.IsNullOrEmpty(preferences.GamePath)?"Not selected":preferences.GamePath;
   gameBox.SetNote(exeHash==""?"Not found":match!=null?"Verified":"Changed",exeHash==""?Theme.Muted:match!=null?Theme.Ok:Theme.Warn,true);
   footerGame=exeHash==""?"No game folder":match!=null?"Anymaker "+match.Version:"Anymaker (new build)";footerNote=exeHash==""?"choose it in Settings":match!=null?"verified build":"awaiting API release";footerColor=exeHash==""?Theme.Dim:match!=null?Theme.Ok:Theme.Warn;sidebar.Invalidate();
   statusRight="catalog "+(catalog.Api.Count>0?catalog.Api[0].Version:"")+" · "+catalog.Mods.Count+" mods";statusBar.Invalidate();
   Rows();
  }
  void Rows(){
   var previous=Selected();List<Package> all;try{all=exeHash!=""&&Engine.IsGameFolder(preferences.GamePath)?Engine.Discover(preferences.GamePath,catalog):catalog.Mods.ToList();}catch(Exception e){ManagerLog.Error("Mod scan",e);all=catalog.Mods.ToList();}var rows=new List<ModRow>();int enabled=0,present=0;installable=new List<Package>();updatable=new List<Package>();enableable=new List<Package>();disableable=new List<Package>();
   foreach(var p in all){
    string state="Not installed";try{if(exeHash!="")state=Engine.Status(preferences.GamePath,p);}catch{}
    bool on=state!="Not installed"&&state!="Disabled";if(state!="Not installed")present++;if(on)enabled++;
    bool available=!p.Local&&exeHash!=""&&Rules.Matches(p,exeHash,gclHash)&&!string.IsNullOrEmpty(p.Url);if(available&&state=="Not installed")installable.Add(p);if(available&&state=="Update / different build")updatable.Add(p);
    bool can=exeHash!=""&&state!="Not installed";if(can&&p.Local&&state=="Disabled"){try{can=Engine.LocalProblem(preferences.GamePath,p,catalog)==null;}catch{can=false;}}
    if(can&&state=="Disabled")enableable.Add(p);else if(can&&on)disableable.Add(p);
    if(modTabs.SelectedIndex==1&&state=="Not installed"||modTabs.SelectedIndex==2&&!p.Local)continue;
    if(p.Name.IndexOf(search.Text,StringComparison.OrdinalIgnoreCase)<0&&p.Description.IndexOf(search.Text,StringComparison.OrdinalIgnoreCase)<0)continue;
    rows.Add(new ModRow{Package=p,State=state,On=on,CanToggle=can});
   }
   modList.EmptyText=modTabs.SelectedIndex==2?"No local mods. Import DLL adds one.":modTabs.SelectedIndex==1?"No mods installed yet.":"No mods match.";
   modList.SetRows(rows,previous==null?null:previous.Id);listBox.Title=new[]{"Library","Installed","Local"}[modTabs.SelectedIndex];listBox.SetNote(rows.Count+(rows.Count==1?" mod":" mods"),Theme.Muted,false);
   navigation[modsPage].Badge=all.Count.ToString();navigation[modsPage].Invalidate();
   bool apiWork=ApiNeedsUpdate();int everything=updatable.Count+(apiWork?1:0)+(updater.UpdateAvailable?1:0);
   installAll.Text=installable.Count>0?"Install all ("+installable.Count+")":"Install all";updateAll.Text=updatable.Count>0?"Update all ("+updatable.Count+")":"Update all";
   enableAll.Enabled=!busy&&enableable.Count>0;disableAll.Enabled=!busy&&disableable.Count>0;enableAll.Text=enableable.Count>0?"Enable all ("+enableable.Count+")":"Enable all";
   installAll.Enabled=!busy&&installable.Count>0;updateAll.Enabled=!busy&&updatable.Count>0;updateEverythingTop.Enabled=!busy&&everything>0;updateEverythingTop.Text=everything>0?"Update everything ("+everything+")":"Everything up to date";updateEverythingTop.Kind=everything>0?ButtonKind.Primary:ButtonKind.Ghost;
   summary.Text=exeHash==""?catalog.Mods.Count+" mods available":present==0?"No mods installed yet · "+catalog.Mods.Count+" available":enabled+" of "+present+" mods enabled";
  }
  Package Selected(){var row=modList.Selected;return row==null?null:row.Package;}
  void Details(){var p=Selected();modInstall.Enabled=toggle.Enabled=remove.Enabled=false;if(p==null){modTile.Package=null;foreach(var v in modValues)v.Text="";((Control)detailBox.Tag).Visible=false;modTitle.Text="Choose a mod";modId.Text="";modDescription.Text="";modNote.Text="";detailBox.SetNote("",Theme.Muted,false);return;}
   modTitle.Text=p.Name;modId.Text=p.Id+" · "+p.Version;modDescription.Text=p.Description;modTile.Package=p;((Control)detailBox.Tag).Visible=ReleasePage(p)!=null;
   string have=InstalledVersion(p);modValues[0].Text=ModArt.Category(p);modValues[1].Text=p.Local?"—":p.Version;modValues[2].Text=have??"—";modValues[2].ForeColor=have!=null&&!p.Local&&have!=p.Version?Theme.Blue:Theme.Ink;modValues[3].Text=p.MinimumApi>0?"API revision "+p.MinimumApi:"Any API";
   if(p.Local){string localState=Engine.Status(preferences.GamePath,p);string problem=Engine.LocalProblem(preferences.GamePath,p,catalog);modNote.Text=problem??"Game compatibility unverified";detailBox.SetNote("Local",Theme.Warn,true);modInstall.Text="Local file";toggle.Text=localState=="Disabled"?"Enable":"Disable";toggle.Enabled=!busy&&(localState!="Disabled"||problem==null);remove.Enabled=!busy;return;}
   bool compatible=Rules.Matches(p,exeHash,gclHash);modNote.Text=compatible?"Compatible with this game build":"Awaiting a release for this game build";
   string state="Not installed";try{if(exeHash!="")state=Engine.Status(preferences.GamePath,p);}catch{}
   detailBox.SetNote(state=="Installed"?"Enabled":state,state=="Installed"?Theme.Ok:state=="Not installed"||state=="Disabled"?Theme.Muted:Theme.Warn,true);
   modInstall.Text=state=="Not installed"?"Install":state=="Disabled"?"Reinstall":"Update / reinstall";modInstall.Kind=state=="Installed"?ButtonKind.Secondary:ButtonKind.Primary;modInstall.Enabled=!busy&&compatible&&!string.IsNullOrEmpty(p.Url);toggle.Text=state=="Disabled"?"Enable":"Disable";toggle.Enabled=remove.Enabled=!busy&&state!="Not installed"&&exeHash!="";
  }
  async Task Install(Package p){await Run(()=>InstallCore(p));}
  async Task InstallCore(Package p){
   if(Engine.Running())throw new IOException("Close Anymaker before installing updates.");
   if(p.Id!="anyapi"&&Engine.ApiRevision(preferences.GamePath,catalog)<p.MinimumApi)throw new IOException("Install or update AnyAPI first.");
   string target=Rules.Target(preferences.GamePath,Rules.OnlyFile(p));bool replace=false;
   if(File.Exists(target)&&Engine.Status(preferences.GamePath,p)!="Installed"){
    var state=Engine.ReadInstalled(preferences.GamePath);Receipt receipt;
    bool known=state.Packages.TryGetValue(p.Id,out receipt)&&Rules.Hash(target)==receipt.Package.FileHashes[Rules.OnlyFile(p)];
    if(!known){replace=MessageBox.Show(this,"Back up and replace the existing "+Path.GetFileName(target)+"?", "Existing installation",MessageBoxButtons.YesNo,MessageBoxIcon.Question)==DialogResult.Yes;if(!replace)return;}
   }
   bool disable=false;if(p.Id=="anyapi"){var incompatible=Engine.Incompatible(preferences.GamePath,catalog);if(incompatible.Count>0){disable=MessageBox.Show(this,"Temporarily disable "+incompatible.Count+" unverified mod(s) while updating AnyAPI?\n\n"+string.Join("\n",incompatible.Keys.Select(Path.GetFileName)),"Game build changed",MessageBoxButtons.YesNo,MessageBoxIcon.Question)==DialogResult.Yes;if(!disable)return;}}
   SetStatus("Preparing "+p.Name+"…");bool embedded=p.Id=="anyapi"&&p.Sha256==bundled.Api[0].Sha256;string zip=await Engine.Acquire(p,embedded);
   try{var result=await Engine.Execute(new ApplyRequest{GamePath=preferences.GamePath,Package=p,Catalog=catalog,Action="install",ZipPath=zip,ReplaceUnknown=replace,DisableIncompatible=disable});if(!result.Success)throw new IOException(result.Message);SetStatus(result.Message);await Scan();}finally{File.Delete(zip);}
  }
  bool ApiNeedsUpdate(){return selectedApi!=null&&installedRevision<selectedApi.Revision;}
  string InstalledVersion(Package p){if(p.Local)return p.Version;try{Receipt r;return exeHash!=""&&Engine.ReadInstalled(preferences.GamePath).Packages.TryGetValue(p.Id,out r)?r.Package.Version:null;}catch{return null;}}
  static string ReleasePage(Package p){if(p==null||p.Local||string.IsNullOrEmpty(p.Url)||!p.Url.Contains("/releases/download/"))return null;return p.Url.Substring(0,p.Url.LastIndexOf('/')).Replace("/releases/download/","/releases/tag/");}
  // Installs or updates several packages in one run. Each failure is logged and skipped so
  // the rest still install; AnyAPI goes first when asked so mods meet their minimum revision.
  async Task Bulk(string what,List<Package> mods,bool api,bool manager){
   await Run(async()=>{
    if(Engine.Running())throw new IOException("Close Anymaker before installing updates.");
    var queue=new List<Package>();if(api&&ApiNeedsUpdate())queue.Add(selectedApi);queue.AddRange(mods);int done=0;var failed=new List<string>();
    for(int i=0;i<queue.Count;i++){var p=queue[i];SetStatus(what+": "+p.Name+" ("+(i+1)+" of "+queue.Count+")…");
     try{await InstallCore(p);done++;}catch(Exception e){ManagerLog.Error(what+" "+p.Name,e);failed.Add(p.Name);}}
    SetStatus(queue.Count==0?(manager&&updater.UpdateAvailable?"Mods and AnyAPI are up to date.":"Nothing to "+what.ToLowerInvariant()+"."):failed.Count==0?what+" finished: "+done+(done==1?" package":" packages")+" up to date.":what+" finished with "+failed.Count+" problem(s): "+string.Join(", ",failed)+". Details are in the log.");
   });
   if(manager&&updater.UpdateAvailable&&updater.State!=ManagerUpdateState.Failed)await InstallManagerUpdate();
  }
  // Enables or disables every installed mod in one run; takes effect at the next game start.
  async Task SwitchAll(bool enable){var targets=(enable?enableable:disableable).ToList();await Run(async()=>{
    if(Engine.Running())throw new IOException("Close Anymaker before changing mods.");string action=enable?"enable":"disable";var failed=new List<string>();
    foreach(var p in targets){var r=await Engine.Execute(new ApplyRequest{GamePath=preferences.GamePath,Package=p,Catalog=catalog,Action=p.Local?action+"-local":action});if(!r.Success){ManagerLog.Write(action+" "+p.Name+" failed: "+r.Message);failed.Add(p.Name);}}
    SetStatus(failed.Count==0?(enable?"Enabled ":"Disabled ")+targets.Count+(targets.Count==1?" mod":" mods")+". Takes effect next time Anymaker starts.":"Couldn't "+action+" "+string.Join(", ",failed)+". Details are in the log.");
  });}
  async Task UpdateEverything(){if(!updater.UpdateAvailable){try{await updater.Check(true);}catch(Exception e){ManagerLog.Error("Manager update check",e);}}await Bulk("Update everything",updatable.ToList(),true,true);}
  async Task ImportLocal(){using(var dialog=new OpenFileDialog{Title="Import an AnyAPI mod",Filter="AnyAPI mod (*.dll)|*.dll",CheckFileExists=true}){if(dialog.ShowDialog(this)!=DialogResult.OK)return;string source=dialog.FileName;await Run(async()=>{
   var p=await Task.Run(()=>Engine.ImportDetails(source));string target=Rules.Target(preferences.GamePath,Rules.OnlyFile(p));bool replace=File.Exists(target)||File.Exists(target+".disabled");if(replace&&MessageBox.Show(this,"Replace "+Path.GetFileName(target)+"? The current DLL will be backed up.","Replace local mod",MessageBoxButtons.YesNo,MessageBoxIcon.Question)!=DialogResult.Yes)return;
   Directory.CreateDirectory(Engine.Data);string staged=Path.Combine(Engine.Data,Guid.NewGuid().ToString("N")+".dll");try{File.Copy(source,staged);var result=await Engine.Execute(new ApplyRequest{GamePath=preferences.GamePath,Package=p,Catalog=catalog,Action="import-local",ZipPath=staged,ReplaceUnknown=replace});if(!result.Success)throw new IOException(result.Message);SetStatus(result.Message);await Scan();}finally{if(File.Exists(staged))File.Delete(staged);}
  });}}
  async Task Change(Package p,string action){await Run(async()=>{var r=await Engine.Execute(new ApplyRequest{GamePath=preferences.GamePath,Package=p,Catalog=catalog,Action=p.Local?action+"-local":action});if(!r.Success)throw new IOException(r.Message);SetStatus(r.Message);RefreshState();});}
  async Task Run(Func<Task> task){if(busy)return;busy=true;UseWaitCursor=true;enableAll.Enabled=disableAll.Enabled=installAll.Enabled=updateAll.Enabled=updateEverythingTop.Enabled=managerUpdate.Enabled=importMod.Enabled=playModded.Enabled=playVanilla.Enabled=apiInstall.Enabled=modInstall.Enabled=toggle.Enabled=remove.Enabled=false;try{await task();}catch(Exception e){ManagerLog.Error("Manager action",e);SetStatus(e is System.Net.Http.HttpRequestException?"Couldn't reach GitHub. Check your connection and try again.":e.Message);}finally{busy=false;UseWaitCursor=false;managerUpdate.Enabled=true;importMod.Enabled=exeHash!="";RefreshState();}}
  void SetStatus(string text){status.Text=text;tips.SetToolTip(status,text);activity.Add(text);bool bad=text!=null&&(text.StartsWith("Couldn't")||text.Contains("failed")||text.Contains("could not"));statusColor=bad?Theme.Bad:busy?Theme.Blue:Theme.Ok;statusBar.Invalidate();}
  void PopulatePreview(){var api=bundled.Api[0];var build=api.GameBuilds[0];exeHash=build.ExeSha256;gclHash=build.GclSha256;RefreshState();apiVersion.Text=api.Version;apiNote.Text="Revision "+api.Revision+" · Matches this game build.";apiBox.SetNote("Up to date",Theme.Ok,true);apiInstall.Text="Reinstall API";apiInstall.Kind=ButtonKind.Secondary;playModded.Enabled=playVanilla.Enabled=true;playBox.SetNote("Mods active",Theme.Ok,true);gameValues[1].Text=gameValues[2].Text="SHA-256 verified";gameBox.SetNote("Verified",Theme.Ok,true);developer.InstalledRevision(api.Revision);SetStatus("Catalog updated. "+bundled.Mods.Count+" mods available.");SetStatus("Ready");}
  public void CapturePreview(string file){File.WriteAllText(file+".layout", "status="+status.Bounds+" visible="+status.Visible+" text="+status.Text+" content="+content.Bounds+" scale="+CurrentAutoScaleDimensions);var pages=new[]{apiPage,modsPage,developerPage,settingsPage};var names=new[]{"","-Mods","-Develop","-Settings"};for(int i=0;i<pages.Length;++i){ShowPage(pages[i]);Refresh();using(var image=new Bitmap(Width,Height)){DrawToBitmap(image,new Rectangle(0,0,Width,Height));image.Save(Path.Combine(Path.GetDirectoryName(file),Path.GetFileNameWithoutExtension(file)+names[i]+".png"),System.Drawing.Imaging.ImageFormat.Png);}}}
 }
}
