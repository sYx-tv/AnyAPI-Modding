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
  static readonly Color Bg=Color.FromArgb(18,22,29),PanelBg=Color.FromArgb(25,31,40),Ink=Color.FromArgb(235,240,247),Muted=Color.FromArgb(158,171,189),Blue=Color.FromArgb(43,126,224);
  readonly Preferences preferences=Engine.LoadPreferences();Catalog catalog=Engine.Bundled();readonly Catalog bundled=Engine.Bundled();
  readonly Panel content=new Panel(),apiPage=new Panel(),modsPage=new Panel(),settingsPage=new Panel();readonly Label status=new Label();
  readonly Label apiVersion=new Label(),apiNote=new Label(),gameNote=new Label(),modTitle=new Label(),modDescription=new Label(),modNote=new Label();
  readonly TextBox gamePath=new TextBox(),repository=new TextBox(),search=new TextBox();readonly CheckBox installedOnly=new CheckBox();
  readonly Button apiInstall=new ModernButton(),modInstall=new ModernButton(),toggle=new ModernButton(),remove=new ModernButton();
  readonly DataGridView grid=new DataGridView();readonly ToolTip tips=new ToolTip();string exeHash="",gclHash="";bool busy;Package selectedApi;
  [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern IntPtr SendMessage(IntPtr h,int msg,IntPtr w,string l);
  public MainWindow(bool preview=false){
   AutoScaleDimensions=new SizeF(96,96);AutoScaleMode=AutoScaleMode.Dpi;Text="AnyAPI Manager";BackColor=Bg;ForeColor=Ink;Font=new Font("Segoe UI",10);ClientSize=new Size(1120,720);MinimumSize=Size;StartPosition=FormStartPosition.CenterScreen;
   using(var icon=System.Reflection.Assembly.GetExecutingAssembly().GetManifestResourceStream("brand.ico"))Icon=new Icon(icon);
   var sidebar=new Panel{Dock=DockStyle.Left,Width=196,BackColor=PanelBg};Controls.Add(sidebar);
   sidebar.Controls.Add(Label("AnyAPI",18,22,22,170,55,true));sidebar.Controls.Add(Label("MOD MANAGER",8,24,78,160,26,false,Muted));
   var apiNav=Button("API",22,135,152,44,()=>ShowPage(apiPage));var modNav=Button("Mods",22,189,152,44,()=>ShowPage(modsPage));var setupNav=Button("Settings",22,243,152,44,()=>ShowPage(settingsPage));sidebar.Controls.AddRange(new Control[]{apiNav,modNav,setupNav});
   var launch=Button("Launch game",22,610,152,42,()=>{try{if(exeHash!=""&&Engine.Incompatible(preferences.GamePath,catalog).Count>0){SetStatus("Some enabled mods need a compatible update. Update or disable them before launching.");ShowPage(modsPage);return;}System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo("steam://rungameid/4435340"){UseShellExecute=true});}catch(Exception e){SetStatus(e.Message);}});launch.Anchor=AnchorStyles.Left|AnchorStyles.Bottom;sidebar.Controls.Add(launch);
   sidebar.Controls.Add(Label("1.0",9,24,672,145,20,false,Muted));
   content.Dock=DockStyle.None;content.Bounds=new Rectangle(196,0,ClientSize.Width-196,ClientSize.Height-64);content.Anchor=AnchorStyles.Top|AnchorStyles.Bottom|AnchorStyles.Left|AnchorStyles.Right;content.Padding=new Padding(32);Controls.Add(content);content.BringToFront();
   status.Dock=DockStyle.Bottom;status.Height=64;status.Padding=new Padding(32,10,20,8);status.ForeColor=Muted;status.BackColor=Bg;status.AutoEllipsis=true;Controls.Add(status);status.BringToFront();
   foreach(var page in new[]{apiPage,modsPage,settingsPage}){page.Dock=DockStyle.Fill;page.BackColor=Bg;content.Controls.Add(page);}
   BuildApi();BuildMods();BuildSettings();ShowPage(apiPage);
   if(string.IsNullOrEmpty(preferences.GamePath))preferences.GamePath=Engine.DetectGame();if(string.IsNullOrEmpty(preferences.Repository))preferences.Repository=bundled.Repository;gamePath.Text=preferences.GamePath;repository.Text=preferences.Repository;
   if(!string.IsNullOrWhiteSpace(preferences.Repository))catalog=Engine.Cached(preferences.Repository)??catalog;
   Load+=async (s,e)=>{if(preview){PopulatePreview();return;}await Run(async()=>{await Scan();if(!string.IsNullOrWhiteSpace(preferences.Repository))await Connect();});};
  }
  Label Label(string text,int size,int x,int y,int w,int h,bool bold=false,Color? color=null){return new Label{Text=text,Location=new Point(x,y),Size=new Size(w,h),Font=new Font("Segoe UI",size,bold?FontStyle.Bold:FontStyle.Regular),ForeColor=color??Ink,AutoEllipsis=true};}
  Button Button(string text,int x,int y,int w,int h,Action action){var b=new ModernButton{Text=text,Location=new Point(x,y),Size=new Size(w,h),FlatStyle=FlatStyle.Flat,BackColor=Color.FromArgb(37,46,60),ForeColor=Ink,Cursor=Cursors.Hand};b.FlatAppearance.BorderSize=0;b.Click+=(s,e)=>{if(!busy)action();};return b;}
  void Style(Button b,string text,int x,int y,int w,int h,bool primary=false){b.Text=text;b.Bounds=new Rectangle(x,y,w,h);b.FlatStyle=FlatStyle.Flat;b.FlatAppearance.BorderSize=0;b.BackColor=primary?Blue:Color.FromArgb(37,46,60);b.ForeColor=Ink;b.Cursor=Cursors.Hand;}
  void TextStyle(TextBox t,int x,int y,int w){t.Bounds=new Rectangle(x,y,w,34);t.BackColor=PanelBg;t.ForeColor=Ink;t.BorderStyle=BorderStyle.FixedSingle;t.Font=new Font("Segoe UI",12);}
  void ShowPage(Panel page){foreach(Control p in content.Controls)p.Visible=p==page;page.BringToFront();}
  void BuildApi(){
   apiPage.Controls.Add(Label("Your setup",24,0,0,600,48,true));apiPage.Controls.Add(Label("Keep AnyAPI ready for your game.",11,0,52,650,30,false,Muted));
   var card=new Panel{Location=new Point(0,115),Size=new Size(800,240),BackColor=PanelBg,Anchor=AnchorStyles.Left|AnchorStyles.Right|AnchorStyles.Top};
   card.Controls.Add(Label("AnyAPI",12,24,20,240,32,true));apiVersion.Bounds=new Rectangle(24,57,500,76);apiVersion.Font=new Font("Segoe UI",26,FontStyle.Bold);apiVersion.ForeColor=Ink;card.Controls.Add(apiVersion);
   apiNote.Bounds=new Rectangle(24,143,730,30);apiNote.ForeColor=Muted;card.Controls.Add(apiNote);Style(apiInstall,"Install API",24,182,170,38,true);apiInstall.Click+=async(s,e)=>{if(!busy&&selectedApi!=null)await Install(selectedApi);};card.Controls.Add(apiInstall);apiPage.Controls.Add(card);
   gameNote.Bounds=new Rectangle(0,383,790,42);gameNote.ForeColor=Muted;apiPage.Controls.Add(gameNote);
   var refresh=Button("Check for updates",0,439,184,42,async()=>await Run(async()=>{await Scan();if(!string.IsNullOrWhiteSpace(preferences.Repository))await Connect();else SetStatus("API checked. Connect your GitHub repository in Settings for online updates.");}));apiPage.Controls.Add(refresh);
   apiPage.Controls.Add(Button("Browse mods",198,439,150,42,()=>ShowPage(modsPage)));tips.SetToolTip(apiInstall,"Installs only AnyAPI. Your mods and saved settings are kept.");
  }
  void BuildMods(){
   modsPage.Controls.Add(Label("Mods",24,0,0,500,48,true));TextStyle(search,0,65,420);search.HandleCreated+=(s,e)=>SendMessage(search.Handle,0x1501,new IntPtr(1),"Search mods");search.TextChanged+=(s,e)=>Rows();
   installedOnly.Text="Installed only";installedOnly.Bounds=new Rectangle(438,66,170,32);installedOnly.ForeColor=Muted;installedOnly.CheckedChanged+=(s,e)=>Rows();modsPage.Controls.AddRange(new Control[]{search,installedOnly});
   grid.Bounds=new Rectangle(0,118,490,470);grid.Anchor=AnchorStyles.Top|AnchorStyles.Bottom|AnchorStyles.Left;grid.BackgroundColor=PanelBg;grid.BorderStyle=BorderStyle.None;grid.GridColor=PanelBg;grid.RowHeadersVisible=false;grid.AllowUserToAddRows=false;grid.AllowUserToDeleteRows=false;grid.AllowUserToResizeRows=false;grid.ReadOnly=true;grid.MultiSelect=false;grid.SelectionMode=DataGridViewSelectionMode.FullRowSelect;grid.AutoSizeColumnsMode=DataGridViewAutoSizeColumnsMode.Fill;grid.EnableHeadersVisualStyles=false;grid.ColumnHeadersBorderStyle=DataGridViewHeaderBorderStyle.None;grid.CellBorderStyle=DataGridViewCellBorderStyle.None;grid.ColumnHeadersHeight=40;grid.RowTemplate.Height=64;grid.DefaultCellStyle.BackColor=PanelBg;grid.DefaultCellStyle.ForeColor=Ink;grid.DefaultCellStyle.SelectionBackColor=Color.FromArgb(35,67,109);grid.DefaultCellStyle.SelectionForeColor=Ink;grid.DefaultCellStyle.Padding=new Padding(12,0,5,0);grid.ColumnHeadersDefaultCellStyle.BackColor=PanelBg;grid.ColumnHeadersDefaultCellStyle.ForeColor=Muted;grid.ColumnHeadersDefaultCellStyle.SelectionBackColor=PanelBg;grid.ColumnHeadersDefaultCellStyle.SelectionForeColor=Muted;grid.ColumnHeadersDefaultCellStyle.Padding=new Padding(12,0,0,0);grid.Columns.Add("name","Mod");grid.Columns.Add("state","Status");grid.Columns[0].FillWeight=58;grid.Columns[1].FillWeight=42;grid.SelectionChanged+=(s,e)=>Details();modsPage.Controls.Add(grid);
   var details=new Panel{Bounds=new Rectangle(516,118,284,470),BackColor=PanelBg,Anchor=AnchorStyles.Top|AnchorStyles.Bottom|AnchorStyles.Left|AnchorStyles.Right};
   modTitle.Bounds=new Rectangle(20,20,245,64);modTitle.Font=new Font("Segoe UI",17,FontStyle.Bold);modDescription.Bounds=new Rectangle(20,100,240,125);modDescription.ForeColor=Muted;modNote.Bounds=new Rectangle(20,232,240,58);modNote.ForeColor=Muted;details.Controls.AddRange(new Control[]{modTitle,modDescription,modNote});
   Style(modInstall,"Install",20,306,244,38,true);Style(toggle,"Disable",20,354,116,36);Style(remove,"Remove",148,354,116,36);details.Controls.AddRange(new Control[]{modInstall,toggle,remove});modsPage.Controls.Add(details);
   modInstall.Click+=async(s,e)=>{if(!busy&&Selected()!=null)await Install(Selected());};toggle.Click+=async(s,e)=>{if(!busy&&Selected()!=null)await Change(Selected(),toggle.Text=="Enable"?"enable":"disable");};remove.Click+=async(s,e)=>{if(!busy&&Selected()!=null&&MessageBox.Show(this,"Remove "+Selected().Name+"? Saved settings will stay.","Remove mod",MessageBoxButtons.YesNo,MessageBoxIcon.Question)==DialogResult.Yes)await Change(Selected(),"remove");};tips.SetToolTip(toggle,"Takes effect next time you launch Anymaker.");tips.SetToolTip(remove,"Removes the DLL and keeps your saved settings.");
  }
  void BuildSettings(){
   settingsPage.Controls.Add(Label("Settings",24,0,0,600,48,true));settingsPage.Controls.Add(Label("Game folder",12,0,99,400,30,true));TextStyle(gamePath,0,139,650);settingsPage.Controls.Add(gamePath);
   settingsPage.Controls.Add(Button("Browse",667,135,110,38,async()=>{using(var dialog=new FolderBrowserDialog{Description="Select Anymaker (the folder containing game.exe)",SelectedPath=gamePath.Text})if(dialog.ShowDialog(this)==DialogResult.OK){gamePath.Text=dialog.SelectedPath;await SaveSetup();}}));
   settingsPage.Controls.Add(Button("Save folder",0,190,145,38,async()=>await SaveSetup()));settingsPage.Controls.Add(Label("GitHub repository",12,0,288,500,30,true));TextStyle(repository,0,330,777);repository.HandleCreated+=(s,e)=>SendMessage(repository.Handle,0x1501,new IntPtr(1),"https://github.com/your-name/AnyAPI");settingsPage.Controls.Add(repository);
   settingsPage.Controls.Add(Button("Connect",0,382,145,40,async()=>await Run(async()=>{Engine.RepositoryUrl(repository.Text);preferences.Repository=repository.Text.Trim();Engine.SavePreferences(preferences);await Connect();})));settingsPage.Controls.Add(Label("Private repositories use your GitHub CLI sign-in.",10,0,438,760,30,false,Muted));
   settingsPage.Controls.Add(Button("Open backups",0,522,150,38,()=>{string path=Rules.Target(preferences.GamePath,"AnyAPI and Modding/.manager/backups");if(Directory.Exists(path))System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(path){UseShellExecute=true});else SetStatus("No manager backups yet.");}));
  }
  async Task SaveSetup(){await Run(async()=>{preferences.GamePath=gamePath.Text.Trim();await Scan();Engine.SavePreferences(preferences);SetStatus("Game folder saved.");});}
  async Task Scan(){
   if(string.IsNullOrWhiteSpace(preferences.GamePath)){exeHash=gclHash="";RefreshState();SetStatus("Choose your Anymaker folder in Settings.");return;}
   exeHash=gclHash="";var hashes=await Task.Run(()=>new[]{Rules.Hash(Rules.Target(preferences.GamePath,"game.exe")),Rules.Hash(Rules.Target(preferences.GamePath,"bin/game.gcl"))});exeHash=hashes[0];gclHash=hashes[1];RefreshState();
  }
  async Task Connect(){var fresh=await Engine.Fetch(preferences.Repository);catalog=fresh;Engine.Cache(preferences.Repository,catalog);RefreshState();SetStatus("Catalog updated. "+catalog.Mods.Count+" mods available.");}
  void RefreshState(){
   selectedApi=catalog.Api.Concat(bundled.Api).Where(p=>Rules.Matches(p,exeHash,gclHash)).OrderByDescending(p=>p.Revision).FirstOrDefault();
   int installed=0;try{if(exeHash!="")installed=Engine.ApiRevision(preferences.GamePath,catalog);}catch(Exception e){SetStatus(e.Message);}
   apiVersion.Text=installed>0?"Revision "+installed:"Not installed";apiNote.Text=selectedApi==null?"Waiting for a release verified for this game build.":installed>=selectedApi.Revision?"Up to date":"Revision "+selectedApi.Revision+" is ready";apiInstall.Text=installed==0?"Install API":selectedApi!=null&&installed<selectedApi.Revision?"Update API":"Reinstall API";apiInstall.Enabled=selectedApi!=null&&!busy;
   var match=catalog.Api.Concat(bundled.Api).SelectMany(p=>p.GameBuilds).FirstOrDefault(b=>b.ExeSha256==exeHash&&b.GclSha256==gclHash);gameNote.Text=exeHash==""?"Game folder not selected":match==null?"Game build changed. Check for a compatible API release.":"Anymaker "+match.Version+"   ·   Verified build";Rows();
  }
  void Rows(){var previous=Selected();grid.Rows.Clear();foreach(var p in catalog.Mods.Where(p=>p.Name.IndexOf(search.Text,StringComparison.OrdinalIgnoreCase)>=0||p.Description.IndexOf(search.Text,StringComparison.OrdinalIgnoreCase)>=0)){
    string state="Not installed";try{if(exeHash!="")state=Engine.Status(preferences.GamePath,p);}catch{}
    if(installedOnly.Checked&&state=="Not installed")continue;int i=grid.Rows.Add(p.Name,state);grid.Rows[i].Tag=p;if(previous!=null&&previous.Id==p.Id)grid.Rows[i].Selected=true;
   }Details();}
  Package Selected(){return grid.SelectedRows.Count>0?grid.SelectedRows[0].Tag as Package:null;}
  void Details(){var p=Selected();modInstall.Enabled=toggle.Enabled=remove.Enabled=false;if(p==null){modTitle.Text="Choose a mod";modDescription.Text="";modNote.Text="";return;}
   modTitle.Text=p.Name;modDescription.Text=p.Description;modNote.Text="Version "+p.Version+"\n"+(Rules.Matches(p,exeHash,gclHash)?"Compatible":"Awaiting compatible release");
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
  async Task Change(Package p,string action){await Run(async()=>{var r=await Engine.Execute(new ApplyRequest{GamePath=preferences.GamePath,Package=p,Catalog=catalog,Action=action});if(!r.Success)throw new IOException(r.Message);SetStatus(r.Message);RefreshState();});}
  async Task Run(Func<Task> task){if(busy)return;busy=true;UseWaitCursor=true;apiInstall.Enabled=modInstall.Enabled=toggle.Enabled=remove.Enabled=false;try{await task();}catch(Exception e){SetStatus(e is System.Net.Http.HttpRequestException?"Couldn't reach the catalog. Check the repository URL and your connection.":e.Message);}finally{busy=false;UseWaitCursor=false;RefreshState();}}
  void SetStatus(string text){status.Text=text;tips.SetToolTip(status,text);}
  void PopulatePreview(){exeHash=bundled.Api[0].GameBuilds[0].ExeSha256;gclHash=bundled.Api[0].GameBuilds[0].GclSha256;apiVersion.Text="Revision 25";apiNote.Text="Up to date";gameNote.Text="Anymaker 0.1.21   ·   Verified build";apiInstall.Text="Reinstall API";Rows();SetStatus("Ready");}
  public void CapturePreview(string file){File.WriteAllText(file+".layout", "status="+status.Bounds+" visible="+status.Visible+" text="+status.Text+" content="+content.Bounds+" scale="+CurrentAutoScaleDimensions);var pages=new[]{modsPage,apiPage,settingsPage};var names=new[]{"","-API","-Settings"};for(int i=0;i<pages.Length;++i){ShowPage(pages[i]);Refresh();using(var image=new Bitmap(Width,Height)){DrawToBitmap(image,new Rectangle(0,0,Width,Height));image.Save(Path.Combine(Path.GetDirectoryName(file),Path.GetFileNameWithoutExtension(file)+names[i]+".png"),System.Drawing.Imaging.ImageFormat.Png);}}}
 }
}

