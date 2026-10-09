using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace AnyApiManager {
 public sealed class MainWindow:Form {
  static readonly Color Bg=Theme.Background,PanelBg=Theme.Surface,Ink=Theme.Ink,Muted=Theme.Muted,Blue=Theme.Blue;
  readonly Preferences preferences=Engine.LoadPreferences();Catalog catalog=Engine.Bundled();readonly Catalog bundled=Engine.Bundled();
  readonly Panel content=new Panel(),apiPage=new Panel(),modsPage=new Panel(),settingsPage=new Panel(),developerPage=new Panel();readonly DeveloperPanel developer;readonly System.Collections.Generic.Dictionary<Panel,ModernButton> navigation=new System.Collections.Generic.Dictionary<Panel,ModernButton>();readonly Button playModded=new ModernButton(),playVanilla=new ModernButton();readonly Label launchNote=new Label(),summary=new Label();bool previewMode;readonly Label status=new Label();
  readonly Label apiVersion=new Label(),apiNote=new Label(),gameNote=new Label(),modTitle=new Label(),modDescription=new Label(),modNote=new Label();
  readonly TextBox gamePath=new TextBox(),search=new TextBox();readonly CheckBox installedOnly=new CheckBox();
  readonly Button apiInstall=new ModernButton(),modInstall=new ModernButton(),toggle=new ModernButton(),remove=new ModernButton();
  readonly Button importMod=new ModernButton();
  readonly Button managerUpdate=new ModernButton();readonly Label managerUpdateNote=new Label();readonly ManagerUpdater updater;
  readonly DataGridView grid=new DataGridView();readonly ToolTip tips=new ToolTip();string exeHash="",gclHash="";bool busy;Package selectedApi;
  [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern IntPtr SendMessage(IntPtr h,int msg,IntPtr w,string l);
  public MainWindow(bool preview=false){
   previewMode=preview;AutoScaleDimensions=new SizeF(96,96);AutoScaleMode=AutoScaleMode.Dpi;Text="AnyAPI Manager";BackColor=Bg;ForeColor=Ink;Font=Theme.Font(15);ClientSize=new Size(1180,790);MinimumSize=new Size(1120,800);StartPosition=FormStartPosition.CenterScreen;
   using(var icon=System.Reflection.Assembly.GetExecutingAssembly().GetManifestResourceStream("brand.ico"))Icon=new Icon(icon);
   var sidebar=new Panel{Dock=DockStyle.Left,Width=196,BackColor=PanelBg};Controls.Add(sidebar);
   sidebar.Controls.Add(Label("AnyAPI",30,24,26,155,44,true));sidebar.Controls.Add(Label("MANAGER",12,26,72,150,24,false,Muted));
   var pages=new[]{apiPage,modsPage,developerPage,settingsPage};var titles=new[]{"Overview","Mods","Develop","Settings"};var glyphs=new[]{"\u25c8","\u25a6","{ }","\u2699"};
   for(int i=0;i<pages.Length;i++){var page=pages[i];var nav=(ModernButton)Button(titles[i],16,140+i*54,164,44,()=>ShowPage(page));nav.AlignLeft=true;nav.Glyph=glyphs[i];sidebar.Controls.Add(nav);navigation.Add(page,nav);}
   var version=Label("VERSION "+ManagerUpdates.DisplayVersion,12,24,ClientSize.Height-44,150,22,false,Muted);version.Anchor=AnchorStyles.Left|AnchorStyles.Bottom;sidebar.Controls.Add(version);
   updater=new ManagerUpdater();updater.Changed+=ShowManagerUpdate;var badge=new ManagerUpdateBadge(updater){Bounds=new Rectangle(16,ClientSize.Height-92,164,38),Anchor=AnchorStyles.Left|AnchorStyles.Bottom};badge.Install+=async()=>await InstallManagerUpdate();sidebar.Controls.Add(badge);
   content.Dock=DockStyle.None;content.Bounds=new Rectangle(196,0,ClientSize.Width-196,ClientSize.Height-44);content.Anchor=AnchorStyles.Top|AnchorStyles.Bottom|AnchorStyles.Left|AnchorStyles.Right;content.Padding=new Padding(32);Controls.Add(content);content.BringToFront();
   status.Dock=DockStyle.Bottom;status.Height=44;status.Padding=new Padding(24,12,20,6);status.ForeColor=Muted;status.BackColor=Bg;status.AutoEllipsis=true;Controls.Add(status);status.BringToFront();
   foreach(var page in new[]{apiPage,modsPage,settingsPage,developerPage}){page.Dock=DockStyle.Fill;page.BackColor=Bg;content.Controls.Add(page);}
   developer=new DeveloperPanel(SetStatus);developerPage.Controls.Add(developer);BuildApi();BuildMods();BuildSettings();ShowPage(apiPage);
   if(string.IsNullOrEmpty(preferences.GamePath))preferences.GamePath=Engine.DetectGame();preferences.Repository=bundled.Repository;gamePath.Text=preferences.GamePath;
   if(!string.IsNullOrWhiteSpace(preferences.Repository))catalog=Engine.Cached(preferences.Repository)??catalog;
   Load+=async (s,e)=>{if(preview){PopulatePreview();return;}await Run(async()=>{await Scan();if(!string.IsNullOrWhiteSpace(preferences.Repository))await Connect();});
    string updated=null;try{updated=ManagerUpdater.FinishPrevious();}catch(Exception){}if(updated!=null)SetStatus(updated);await updater.Check(true);};
  }
  Label Label(string text,int size,int x,int y,int w,int h,bool bold=false,Color? color=null){return new Label{Text=text,Location=new Point(x,y),Size=new Size(w,h),Font=Theme.Font(size,bold),ForeColor=color??Ink,AutoEllipsis=true};}
  Button Button(string text,int x,int y,int w,int h,Action action){var b=new ModernButton{Text=text,Location=new Point(x,y),Size=new Size(w,h),FlatStyle=FlatStyle.Flat,BackColor=Theme.Raised,ForeColor=Ink,Cursor=Cursors.Hand};b.FlatAppearance.BorderSize=0;b.Click+=(s,e)=>{if(!busy)action();};return b;}
  void Style(Button b,string text,int x,int y,int w,int h,bool primary=false){b.Text=text;b.Bounds=new Rectangle(x,y,w,h);b.FlatStyle=FlatStyle.Flat;b.FlatAppearance.BorderSize=0;b.BackColor=primary?Blue:Theme.Raised;b.ForeColor=Ink;b.Cursor=Cursors.Hand;}
  void TextStyle(TextBox t,int x,int y,int w){t.Bounds=new Rectangle(x,y,w,34);t.BackColor=PanelBg;t.ForeColor=Ink;t.BorderStyle=BorderStyle.FixedSingle;t.Font=Theme.Font(16);}
  void ShowPage(Panel page){foreach(Control p in content.Controls)p.Visible=p==page;page.BringToFront();foreach(var nav in navigation){nav.Value.Selected=nav.Key==page;nav.Value.Invalidate();}}
  void BuildApi(){
   apiPage.Controls.Add(Label("Overview",28,0,0,740,44,true));apiPage.Controls.Add(Label("Anymaker",15,0,49,650,26,false,Muted));
   var card=new Card{Location=new Point(0,102),Size=new Size(800,210),Anchor=AnchorStyles.Left|AnchorStyles.Right|AnchorStyles.Top};
   card.Controls.Add(Label("ANYAPI",13,24,22,240,24,true,Muted));apiVersion.Bounds=new Rectangle(24,56,500,48);apiVersion.Font=Theme.Font(32,true);apiVersion.ForeColor=Ink;card.Controls.Add(apiVersion);
   apiNote.Bounds=new Rectangle(24,112,730,26);apiNote.ForeColor=Muted;card.Controls.Add(apiNote);Style(apiInstall,"Install API",24,152,160,38,true);apiInstall.Click+=async(s,e)=>{if(!busy&&selectedApi!=null)await Install(selectedApi);};card.Controls.Add(apiInstall);
   card.Controls.Add(Button("Check for updates",198,152,180,38,async()=>await Run(async()=>{await Scan();if(!string.IsNullOrWhiteSpace(preferences.Repository))await Connect();else SetStatus("Connect your repository in Settings for online updates.");})));apiPage.Controls.Add(card);
   var launch=new Card{Location=new Point(0,330),Size=new Size(800,162),Anchor=AnchorStyles.Left|AnchorStyles.Right|AnchorStyles.Top};launch.Controls.Add(Label("Play Anymaker",20,24,20,480,32,true));
   Style(playModded,"Play with mods",24,66,186,42,true);Style(playVanilla,"Play without mods",222,66,186,42);playModded.Click+=async(s,e)=>await Launch(true);playVanilla.Click+=async(s,e)=>await Launch(false);launch.Controls.AddRange(new Control[]{playModded,playVanilla});
   launchNote.Bounds=new Rectangle(24,119,740,26);launchNote.ForeColor=Muted;launch.Controls.Add(launchNote);apiPage.Controls.Add(launch);
   gameNote.Bounds=new Rectangle(0,514,790,30);gameNote.ForeColor=Muted;apiPage.Controls.Add(gameNote);summary.Bounds=new Rectangle(0,554,620,30);summary.ForeColor=Ink;apiPage.Controls.Add(summary);apiPage.Controls.Add(Button("Browse mods",0,598,150,38,()=>ShowPage(modsPage)));
   tips.SetToolTip(apiInstall,"Bundles only the API. Optional mods are downloaded separately.");tips.SetToolTip(playVanilla,"Pauses AnyAPI. Saved settings and individual mod choices are kept. Steam stays in this mode until you choose Play with mods.");
  }
  async Task Launch(bool modded){await Run(async()=>{
   if(previewMode)return;if(Engine.Running())throw new IOException("Anymaker is already running. Close it before switching modes.");await Scan();
   bool wasPaused=Engine.ApiPaused(preferences.GamePath);int revision=Engine.ApiRevision(preferences.GamePath,catalog);
   var r=await Engine.Execute(new ApplyRequest{GamePath=preferences.GamePath,Catalog=catalog,Action=modded?"prepare-modded":"prepare-vanilla"});if(!r.Success)throw new IOException(r.Message);
   Exception launchError=null;try{System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo("steam://rungameid/4435340"){UseShellExecute=true});SetStatus(modded?"Launching with your enabled mods.":"Launching with AnyAPI paused. Choose Play with mods to restore it.");}catch(Exception error){launchError=error;}
   if(launchError!=null){if(revision>0&&wasPaused!=Engine.ApiPaused(preferences.GamePath)){var restored=await Engine.Execute(new ApplyRequest{GamePath=preferences.GamePath,Catalog=catalog,Action=wasPaused?"prepare-vanilla":"prepare-modded"});if(!restored.Success)throw new IOException("Steam could not open. "+restored.Message);}throw new IOException("Steam could not open: "+launchError.Message);}
  });}
  void BuildMods(){
   modsPage.Controls.Add(Label("Mods",24,0,0,500,48,true));TextStyle(search,0,65,290);search.HandleCreated+=(s,e)=>SendMessage(search.Handle,0x1501,new IntPtr(1),"Search mods");search.TextChanged+=(s,e)=>Rows();
   installedOnly.Text="Installed only";installedOnly.Bounds=new Rectangle(305,66,140,32);installedOnly.ForeColor=Muted;installedOnly.CheckedChanged+=(s,e)=>Rows();modsPage.Controls.AddRange(new Control[]{search,installedOnly});
   Style(importMod,"Import DLL",650,65,150,38,true);importMod.Click+=async(s,e)=>{if(!busy)await ImportLocal();};modsPage.Controls.Add(importMod);
   modsPage.Controls.Add(Button("Refresh",462,65,82,38,async()=>await Run(async()=>await Scan())));modsPage.Controls.Add(Button("Folder",556,65,82,38,()=>{string folder=Rules.Target(preferences.GamePath,"AnyAPI and Modding/mods");if(Directory.Exists(folder))System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(folder){UseShellExecute=true});else SetStatus("Install AnyAPI or import a mod first.");}));
   grid.Bounds=new Rectangle(0,118,490,470);grid.Anchor=AnchorStyles.Top|AnchorStyles.Bottom|AnchorStyles.Left;grid.BackgroundColor=PanelBg;grid.BorderStyle=BorderStyle.None;grid.GridColor=PanelBg;grid.RowHeadersVisible=false;grid.AllowUserToAddRows=false;grid.AllowUserToDeleteRows=false;grid.AllowUserToResizeRows=false;grid.ReadOnly=true;grid.MultiSelect=false;grid.SelectionMode=DataGridViewSelectionMode.FullRowSelect;grid.AutoSizeColumnsMode=DataGridViewAutoSizeColumnsMode.Fill;grid.EnableHeadersVisualStyles=false;grid.ColumnHeadersBorderStyle=DataGridViewHeaderBorderStyle.None;grid.CellBorderStyle=DataGridViewCellBorderStyle.None;grid.ColumnHeadersHeight=40;grid.RowTemplate.Height=64;grid.DefaultCellStyle.BackColor=PanelBg;grid.DefaultCellStyle.ForeColor=Ink;grid.DefaultCellStyle.SelectionBackColor=Color.FromArgb(35,67,109);grid.DefaultCellStyle.SelectionForeColor=Ink;grid.DefaultCellStyle.Padding=new Padding(12,0,5,0);grid.ColumnHeadersDefaultCellStyle.BackColor=PanelBg;grid.ColumnHeadersDefaultCellStyle.ForeColor=Muted;grid.ColumnHeadersDefaultCellStyle.SelectionBackColor=PanelBg;grid.ColumnHeadersDefaultCellStyle.SelectionForeColor=Muted;grid.ColumnHeadersDefaultCellStyle.Padding=new Padding(12,0,0,0);grid.Columns.Add("name","Mod");grid.Columns.Add("state","Status");grid.Columns.Add("source","Source");grid.Columns[0].FillWeight=43;grid.Columns[1].FillWeight=34;grid.Columns[2].FillWeight=23;grid.SelectionChanged+=(s,e)=>Details();modsPage.Controls.Add(grid);
   var details=new Card{Bounds=new Rectangle(516,118,284,470),BackColor=PanelBg,Anchor=AnchorStyles.Top|AnchorStyles.Bottom|AnchorStyles.Left|AnchorStyles.Right};
   modTitle.Bounds=new Rectangle(24,24,245,64);modTitle.Font=Theme.Font(24,true);modDescription.Bounds=new Rectangle(24,100,240,125);modDescription.ForeColor=Muted;modNote.Bounds=new Rectangle(24,232,240,58);modNote.ForeColor=Muted;details.Controls.AddRange(new Control[]{modTitle,modDescription,modNote});
   Style(modInstall,"Install",24,306,244,40,true);Style(toggle,"Disable",24,360,116,38);Style(remove,"Remove",152,360,116,38);details.Controls.AddRange(new Control[]{modInstall,toggle,remove});modsPage.Controls.Add(details);details.Resize+=(s,e)=>{int w=details.Width-48;modTitle.Width=modDescription.Width=modNote.Width=w;modInstall.Width=w;toggle.Width=(w-12)/2;remove.Left=24+toggle.Width+12;remove.Width=toggle.Width;};
   modInstall.Click+=async(s,e)=>{if(!busy&&Selected()!=null)await Install(Selected());};toggle.Click+=async(s,e)=>{if(!busy&&Selected()!=null)await Change(Selected(),toggle.Text=="Enable"?"enable":"disable");};remove.Click+=async(s,e)=>{if(!busy&&Selected()!=null&&MessageBox.Show(this,"Remove "+Selected().Name+"? Saved settings will stay.","Remove mod",MessageBoxButtons.YesNo,MessageBoxIcon.Question)==DialogResult.Yes)await Change(Selected(),"remove");};tips.SetToolTip(toggle,"Takes effect next time you launch Anymaker.");tips.SetToolTip(remove,"Removes the DLL and keeps your saved settings.");
  }
  void BuildSettings(){
   settingsPage.Controls.Add(Label("Settings",24,0,0,600,48,true));settingsPage.Controls.Add(Label("Game folder",12,0,99,400,30,true));TextStyle(gamePath,0,139,650);settingsPage.Controls.Add(gamePath);
   settingsPage.Controls.Add(Button("Browse",667,135,110,38,async()=>{using(var dialog=new FolderBrowserDialog{Description="Select Anymaker (the folder containing game.exe)",SelectedPath=gamePath.Text})if(dialog.ShowDialog(this)==DialogResult.OK){gamePath.Text=dialog.SelectedPath;await SaveSetup();}}));
   settingsPage.Controls.Add(Button("Save folder",0,190,145,38,async()=>await SaveSetup()));settingsPage.Controls.Add(Label("Mod library",20,0,288,500,30,true));
   settingsPage.Controls.Add(Label("The official library is built in. No GitHub account or setup needed.",15,0,330,777,32,false,Muted));
   settingsPage.Controls.Add(Button("Refresh library",0,382,160,40,async()=>await Run(async()=>await Connect())));settingsPage.Controls.Add(Button("View on GitHub",174,382,160,40,()=>System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(bundled.Repository){UseShellExecute=true})));
   settingsPage.Controls.Add(Label("Manager",20,0,456,500,30,true));
   managerUpdateNote.Text="Version "+ManagerUpdates.DisplayVersion;managerUpdateNote.Bounds=new Rectangle(0,495,777,32);managerUpdateNote.ForeColor=Muted;settingsPage.Controls.Add(managerUpdateNote);
   Style(managerUpdate,"Check for updates",0,540,192,40,true);managerUpdate.Click+=async(s,e)=>{if(!busy)await UpdateManager();};settingsPage.Controls.Add(managerUpdate);
   tips.SetToolTip(managerUpdate,"Updates the manager EXE. Your mods and settings stay in place.");
   settingsPage.Controls.Add(Button("Open backups",0,612,150,38,()=>{string path=Rules.Target(preferences.GamePath,"AnyAPI and Modding/.manager/backups");if(Directory.Exists(path))System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(path){UseShellExecute=true});else SetStatus("No manager backups yet.");}));
  }
  async Task UpdateManager(){if(updater.UpdateAvailable)await InstallManagerUpdate();else if(!busy)await updater.Check(false);}
  async Task InstallManagerUpdate(){await Run(updater.Install);if(updater.State==ManagerUpdateState.Restarting)Application.Exit();}
  void ShowManagerUpdate(){
   managerUpdate.Text=updater.UpdateAvailable?"Update & restart":"Check for updates";
   managerUpdateNote.Text="Version "+ManagerUpdates.DisplayVersion+(updater.UpdateAvailable?" · "+updater.Release.Version+" available":updater.State==ManagerUpdateState.UpToDate?" · Up to date":"");
   if(updater.Message!=null)SetStatus(updater.State==ManagerUpdateState.Downloading&&updater.Progress>=0?updater.Message+" "+(int)(updater.Progress*100)+"%":updater.Message);
  }
  async Task SaveSetup(){await Run(async()=>{preferences.GamePath=gamePath.Text.Trim();await Scan();Engine.SavePreferences(preferences);SetStatus("Game folder saved.");});}
  async Task Scan(){
   if(string.IsNullOrWhiteSpace(preferences.GamePath)){exeHash=gclHash="";RefreshState();SetStatus("Choose your Anymaker folder in Settings.");return;}
   exeHash=gclHash="";var hashes=await Task.Run(()=>new[]{Rules.Hash(Rules.Target(preferences.GamePath,"game.exe")),Rules.Hash(Rules.Target(preferences.GamePath,"bin/game.gcl"))});exeHash=hashes[0];gclHash=hashes[1];ManagerLog.Write("Game folder "+preferences.GamePath+" game.exe "+exeHash+" game.gcl "+gclHash);RefreshState();
  }
  async Task Connect(){var fresh=await Engine.Fetch(preferences.Repository);catalog=fresh;Engine.Cache(preferences.Repository,catalog);RefreshState();SetStatus("Catalog updated. "+catalog.Mods.Count+" mods available.");}
  void RefreshState(){
   selectedApi=catalog.Api.Concat(bundled.Api).Where(p=>Rules.Matches(p,exeHash,gclHash)).OrderByDescending(p=>p.Revision).FirstOrDefault();
   int installed=0;try{if(exeHash!="")installed=Engine.ApiRevision(preferences.GamePath,catalog);}catch(Exception e){SetStatus(e.Message);}
   Package installedApi=null;try{if(exeHash!="")installedApi=Engine.MatchingInstalledApi(preferences.GamePath,catalog,exeHash,gclHash);}catch(Exception e){SetStatus(e.Message);}
   apiVersion.Text=installed>0?"Revision "+installed:"Not installed";apiNote.Text=selectedApi==null?(installedApi!=null?"Installed API matches this game build.":"Waiting for a release verified for this game build."):installed>=selectedApi.Revision?"Up to date":"Revision "+selectedApi.Revision+" is ready";apiInstall.Text=installed==0?"Install API":selectedApi!=null&&installed<selectedApi.Revision?"Update API":"Reinstall API";apiInstall.Enabled=selectedApi!=null&&!busy;
   var match=catalog.Api.Concat(bundled.Api).Concat(installedApi==null?new Package[0]:new[]{installedApi}).SelectMany(p=>p.GameBuilds).FirstOrDefault(b=>b.ExeSha256==exeHash&&b.GclSha256==gclHash);developer.InstalledRevision(installed);launchNote.Text=installed>0&&Engine.ApiPaused(preferences.GamePath)?"Mods paused · normal Steam launches use this mode too":"Your enabled mods load automatically through Steam.";playModded.Enabled=!busy&&installedApi!=null;playVanilla.Enabled=!busy&&exeHash!="";summary.Text=catalog.Mods.Count+" mods available · install the ones you want";gameNote.Text=exeHash==""?"Game folder not selected":match==null?"Game build changed. Check for a compatible API release.":"Anymaker "+match.Version+"   ·   Matched build";Rows();
  }
  void Rows(){var previous=Selected();grid.Rows.Clear();foreach(var p in (exeHash!=""?Engine.Discover(preferences.GamePath,catalog):catalog.Mods).Where(p=>p.Name.IndexOf(search.Text,StringComparison.OrdinalIgnoreCase)>=0||p.Description.IndexOf(search.Text,StringComparison.OrdinalIgnoreCase)>=0)){
    string state="Not installed";try{if(exeHash!="")state=Engine.Status(preferences.GamePath,p);}catch{}
    if(installedOnly.Checked&&state=="Not installed")continue;int i=grid.Rows.Add(p.Name,state=="Installed"?"Enabled":state,p.Local?"Local":"Library");grid.Rows[i].Tag=p;if(previous!=null&&previous.Id==p.Id)grid.Rows[i].Selected=true;
   }Details();}
  Package Selected(){return grid.SelectedRows.Count>0?grid.SelectedRows[0].Tag as Package:null;}
  void Details(){var p=Selected();modInstall.Enabled=toggle.Enabled=remove.Enabled=false;if(p==null){modTitle.Text="Choose a mod";modDescription.Text="";modNote.Text="";return;}
   modTitle.Text=p.Name;modDescription.Text=p.Description;
   if(p.Local){string localState=Engine.Status(preferences.GamePath,p);string problem=Engine.LocalProblem(preferences.GamePath,p,catalog);modNote.Text="Local mod · "+p.Version+"\n"+(problem??"Game compatibility unverified")+(p.MinimumApi>0?"\nRequires API "+p.MinimumApi:"");modInstall.Text="Local file";toggle.Text=localState=="Disabled"?"Enable":"Disable";toggle.Enabled=!busy&&(localState!="Disabled"||problem==null);remove.Enabled=!busy;return;}modNote.Text="Version "+p.Version+"\n"+(Rules.Matches(p,exeHash,gclHash)?"Compatible":"Awaiting compatible release");
   string state="Not installed";try{if(exeHash!="")state=Engine.Status(preferences.GamePath,p);}catch{}
   modInstall.Text=state=="Not installed"?"Install":state=="Disabled"?"Reinstall":"Update / reinstall";modInstall.Enabled=!busy&&Rules.Matches(p,exeHash,gclHash)&&!string.IsNullOrEmpty(p.Url);toggle.Text=state=="Disabled"?"Enable":"Disable";toggle.Enabled=remove.Enabled=!busy&&state!="Not installed"&&exeHash!="";
  }
  async Task Install(Package p){await Run(async()=>{
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
  });}
  async Task ImportLocal(){using(var dialog=new OpenFileDialog{Title="Import an AnyAPI mod",Filter="AnyAPI mod (*.dll)|*.dll",CheckFileExists=true}){if(dialog.ShowDialog(this)!=DialogResult.OK)return;string source=dialog.FileName;await Run(async()=>{
   var p=await Task.Run(()=>Engine.ImportDetails(source));string target=Rules.Target(preferences.GamePath,Rules.OnlyFile(p));bool replace=File.Exists(target)||File.Exists(target+".disabled");if(replace&&MessageBox.Show(this,"Replace "+Path.GetFileName(target)+"? The current DLL will be backed up.","Replace local mod",MessageBoxButtons.YesNo,MessageBoxIcon.Question)!=DialogResult.Yes)return;
   Directory.CreateDirectory(Engine.Data);string staged=Path.Combine(Engine.Data,Guid.NewGuid().ToString("N")+".dll");try{File.Copy(source,staged);var result=await Engine.Execute(new ApplyRequest{GamePath=preferences.GamePath,Package=p,Catalog=catalog,Action="import-local",ZipPath=staged,ReplaceUnknown=replace});if(!result.Success)throw new IOException(result.Message);SetStatus(result.Message);await Scan();}finally{if(File.Exists(staged))File.Delete(staged);}
  });}}
  async Task Change(Package p,string action){await Run(async()=>{var r=await Engine.Execute(new ApplyRequest{GamePath=preferences.GamePath,Package=p,Catalog=catalog,Action=p.Local?action+"-local":action});if(!r.Success)throw new IOException(r.Message);SetStatus(r.Message);RefreshState();});}
  async Task Run(Func<Task> task){if(busy)return;busy=true;UseWaitCursor=true;managerUpdate.Enabled=importMod.Enabled=playModded.Enabled=playVanilla.Enabled=apiInstall.Enabled=modInstall.Enabled=toggle.Enabled=remove.Enabled=false;try{await task();}catch(Exception e){ManagerLog.Error("Manager action",e);SetStatus(e is System.Net.Http.HttpRequestException?"Couldn't reach GitHub. Check your connection and try again.":e.Message);}finally{busy=false;UseWaitCursor=false;managerUpdate.Enabled=true;importMod.Enabled=exeHash!="";RefreshState();}}
  void SetStatus(string text){status.Text=text;tips.SetToolTip(status,text);}
  void PopulatePreview(){var api=bundled.Api[0];var build=api.GameBuilds[0];exeHash=build.ExeSha256;gclHash=build.GclSha256;apiVersion.Text="Revision "+api.Revision;apiNote.Text="Up to date";gameNote.Text="Anymaker "+build.Version+" · Verified build";apiInstall.Text="Reinstall API";launchNote.Text="Your enabled mods load automatically through Steam.";summary.Text=bundled.Mods.Count+" mods available · install the ones you want";developer.InstalledRevision(api.Revision);Rows();SetStatus("Ready");}
  public void CapturePreview(string file){File.WriteAllText(file+".layout", "status="+status.Bounds+" visible="+status.Visible+" text="+status.Text+" content="+content.Bounds+" scale="+CurrentAutoScaleDimensions);var pages=new[]{apiPage,modsPage,developerPage,settingsPage};var names=new[]{"","-Mods","-Develop","-Settings"};for(int i=0;i<pages.Length;++i){ShowPage(pages[i]);Refresh();using(var image=new Bitmap(Width,Height)){DrawToBitmap(image,new Rectangle(0,0,Width,Height));image.Save(Path.Combine(Path.GetDirectoryName(file),Path.GetFileNameWithoutExtension(file)+names[i]+".png"),System.Drawing.Imaging.ImageFormat.Png);}}}
 }
}
