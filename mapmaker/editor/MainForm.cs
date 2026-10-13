using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Windows.Forms;
namespace AnyMapMaker {
 public sealed class MainForm : Form {
  public const string Version="0.1.0";
  static readonly Color Panel=Color.FromArgb(14,19,28), Raised=Color.FromArgb(21,27,39), Ink=Color.FromArgb(230,235,243), Muted=Color.FromArgb(125,136,153);
  readonly MapView view=new MapView{Dock=DockStyle.Fill};
  readonly CheckedListBox layers=new CheckedListBox{Dock=DockStyle.Fill,BorderStyle=BorderStyle.None,CheckOnClick=true,IntegralHeight=false};
  readonly TextBox nameBox=new TextBox(), authorBox=new TextBox(), descriptionBox=new TextBox{Multiline=true,Height=60};
  readonly Label tileTitle=new Label{Dock=DockStyle.Top,Height=44,Padding=new Padding(0,6,0,0)};
  readonly TextBox tileJson=new TextBox{Dock=DockStyle.Fill,Multiline=true,ScrollBars=ScrollBars.Both,WordWrap=false,Font=new Font("Consolas",9f),AcceptsTab=true};
  readonly Button applyJson=new Button{Text="Apply tile changes",Dock=DockStyle.Bottom,Height=30,Enabled=false};
  readonly ToolStripStatusLabel status=new ToolStripStatusLabel{Spring=true,TextAlign=ContentAlignment.MiddleLeft};
  readonly ToolStripStatusLabel counts=new ToolStripStatusLabel();
  GameInstall game; MapPack pack; bool unsaved;

  public MainForm(){
   Text="Anymaker Map Maker "+Version;Size=new Size(1400,900);StartPosition=FormStartPosition.CenterScreen;BackColor=Panel;ForeColor=Ink;
   Font=new Font("Segoe UI",9f);
   try{Icon=Icon.ExtractAssociatedIcon(Application.ExecutablePath);}catch{}
   var menu=new MenuStrip();
   var file=new ToolStripMenuItem("&File");
   file.DropDownItems.Add("Open the &stock world",null,(s,e)=>OpenStock());
   file.DropDownItems.Add("&Open map pack...",null,(s,e)=>OpenPack());
   file.DropDownItems.Add(new ToolStripSeparator());
   ((ToolStripMenuItem)file.DropDownItems.Add("&Save map pack",null,(s,e)=>Save(false))).ShortcutKeys=Keys.Control|Keys.S;
   file.DropDownItems.Add("Save map pack &as...",null,(s,e)=>Save(true));
   file.DropDownItems.Add(new ToolStripSeparator());
   file.DropDownItems.Add("Choose &game folder...",null,(s,e)=>ChooseGame(true));
   file.DropDownItems.Add("E&xit",null,(s,e)=>Close());
   var viewMenu=new ToolStripMenuItem("&View");
   ((ToolStripMenuItem)viewMenu.DropDownItems.Add("&Fit whole map",null,(s,e)=>view.FitAll())).ShortcutKeys=Keys.Control|Keys.D0;
   var help=new ToolStripMenuItem("&Help");
   help.DropDownItems.Add("&Controls",null,(s,e)=>MessageBox.Show(this,"Scroll to zoom. Drag with the right or middle mouse button to move around.\nLeft click a tile to inspect it; its data appears on the right, where you can edit it and press Apply.\nTick layers on the left to show or hide them.\n\nSave map pack writes map.json and the tile files to a folder (by default under AnyAPI and Modding\\maps in the game folder).","Controls"));
   help.DropDownItems.Add("&About",null,(s,e)=>MessageBox.Show(this,"Anymaker Map Maker "+Version+"\nPart of AnyAPI-Modding. Map packs are loaded in game by the CustomMap mod.","About"));
   menu.Items.AddRange(new ToolStripItem[]{file,viewMenu,help});
   MainMenuStrip=menu;

   var left=new Panel{Dock=DockStyle.Left,Width=250,Padding=new Padding(10),BackColor=Panel};
   var mapBox=new TableLayoutPanel{Dock=DockStyle.Top,Height=210,ColumnCount=1};
   mapBox.Controls.Add(Caption("MAP"));
   mapBox.Controls.Add(new Label{Text="Name",ForeColor=Muted,AutoSize=true});mapBox.Controls.Add(nameBox);
   mapBox.Controls.Add(new Label{Text="Author",ForeColor=Muted,AutoSize=true});mapBox.Controls.Add(authorBox);
   mapBox.Controls.Add(new Label{Text="Description",ForeColor=Muted,AutoSize=true});mapBox.Controls.Add(descriptionBox);
   foreach(var box in new[]{nameBox,authorBox,descriptionBox}){box.Dock=DockStyle.Fill;box.BackColor=Raised;box.ForeColor=Ink;box.BorderStyle=BorderStyle.FixedSingle;box.TextChanged+=(s,e)=>MetadataChanged();}
   layers.BackColor=Panel;layers.ForeColor=Ink;
   layers.ItemCheck+=(s,e)=>{MapScene.Layers[e.Index].Visible=e.NewValue==CheckState.Checked;view.Invalidate();};
   left.Controls.Add(layers);left.Controls.Add(Caption("LAYERS"));left.Controls.Add(mapBox);

   var right=new Panel{Dock=DockStyle.Right,Width=380,Padding=new Padding(10),BackColor=Panel};
   tileJson.BackColor=Raised;tileJson.ForeColor=Ink;tileJson.TextChanged+=(s,e)=>applyJson.Enabled=view.Selected!=null;
   applyJson.FlatStyle=FlatStyle.Flat;applyJson.BackColor=Color.FromArgb(56,200,240);applyJson.ForeColor=Color.FromArgb(3,16,24);applyJson.Click+=(s,e)=>ApplyTileJson();
   tileTitle.ForeColor=Ink;
   right.Controls.Add(tileJson);right.Controls.Add(applyJson);right.Controls.Add(tileTitle);right.Controls.Add(Caption("SELECTED TILE"));

   var bar=new StatusStrip{BackColor=Raised,ForeColor=Ink};bar.Items.Add(status);bar.Items.Add(counts);
   Controls.Add(view);Controls.Add(right);Controls.Add(left);Controls.Add(bar);Controls.Add(menu);
   view.StatusChanged+=t=>status.Text=t;
   view.SelectionChanged+=ShowTile;
   RefreshLayers();
   Shown+=(s,e)=>{ChooseGame(false);};
   FormClosing+=(s,e)=>{if(unsaved&&MessageBox.Show(this,"You have unsaved changes. Close anyway?","Unsaved changes",MessageBoxButtons.YesNo,MessageBoxIcon.Warning)!=DialogResult.Yes)e.Cancel=true;};
  }

  static Label Caption(string text){return new Label{Text=text,Dock=DockStyle.Top,Height=26,ForeColor=Muted,Font=new Font("Segoe UI",8f,FontStyle.Bold),TextAlign=ContentAlignment.BottomLeft};}

  void RefreshLayers(){
   layers.Items.Clear();
   foreach(var l in MapScene.Layers)layers.Items.Add(l.Title+(l.Count>0?"  ("+l.Count.ToString("N0")+")":""),l.Visible);
  }

  void ChooseGame(bool ask){
   string folder=ask?null:Settings.GameFolder;
   if(folder==null||!new GameInstall(folder).Valid)folder=ask?null:GameInstall.Guess();
   if(folder==null){
    using(var d=new FolderBrowserDialog{Description="Choose the Anymaker game folder (the one with game.exe)"}){
     if(d.ShowDialog(this)!=DialogResult.OK)return;
     folder=d.SelectedPath;
    }
    if(!new GameInstall(folder).Valid){MessageBox.Show(this,"That folder has no game.exe and rom\\data\\tiles. Pick the Anymaker install folder.","Not the game folder");return;}
   }
   game=new GameInstall(folder);Settings.GameFolder=folder;
   status.Text="Game folder: "+folder;
  }

  bool ConfirmDiscard(){return !unsaved||MessageBox.Show(this,"Discard unsaved changes?","Unsaved changes",MessageBoxButtons.YesNo)==DialogResult.Yes;}

  void OpenStock(){
   if(!ConfirmDiscard())return;
   if(game==null)ChooseGame(true);if(game==null)return;
   OpenWith(()=>{var p=MapPack.LoadTiles(game.StockTiles,"My map");p.Base="stock";foreach(var t in p.Tiles.Values)t.Dirty=true;return p;});
  }
  void OpenPack(){
   if(!ConfirmDiscard())return;
   using(var d=new FolderBrowserDialog{Description="Choose a map pack folder (it contains map.json)"}){
    if(game!=null&&Directory.Exists(game.MapsFolder))d.SelectedPath=game.MapsFolder;
    if(d.ShowDialog(this)!=DialogResult.OK)return;
    if(!File.Exists(Path.Combine(d.SelectedPath,"map.json"))){MessageBox.Show(this,"That folder has no map.json.","Not a map pack");return;}
    string folder=d.SelectedPath;OpenWith(()=>MapPack.Open(folder));
   }
  }
  void OpenWith(Func<MapPack> loader){
   Cursor=Cursors.WaitCursor;
   try{
    pack=loader();var scene=MapScene.Build(pack);view.SetMap(pack,scene);
    loading=true;nameBox.Text=pack.Name;authorBox.Text=pack.Author;descriptionBox.Text=pack.Description;loading=false;
    unsaved=pack.Folder==null;RefreshLayers();ShowTile(null);UpdateCounts();
    if(pack.LoadErrors.Count>0)MessageBox.Show(this,pack.LoadErrors.Count+" tile files could not be read. First: "+pack.LoadErrors[0],"Some tiles were skipped",MessageBoxButtons.OK,MessageBoxIcon.Warning);
   }catch(Exception e){MessageBox.Show(this,e.Message,"Could not open the map",MessageBoxButtons.OK,MessageBoxIcon.Error);}
   finally{Cursor=Cursors.Default;}
  }
  bool loading;
  void MetadataChanged(){if(loading||pack==null)return;pack.Name=nameBox.Text.Trim();pack.Author=authorBox.Text.Trim();pack.Description=descriptionBox.Text.Trim();unsaved=true;UpdateCounts();}
  void UpdateCounts(){counts.Text=pack==null?"":pack.Tiles.Count.ToString("N0")+" tiles"+(unsaved?"  •  unsaved":"");Text="Anymaker Map Maker "+Version+(pack==null?"":" - "+pack.Name+(unsaved?" *":""));}

  void Save(bool askFolder){
   if(pack==null)return;
   string folder=pack.Folder;
   if(askFolder||folder==null){
    string parent=game!=null?game.MapsFolder:null;
    if(askFolder||parent==null){
     using(var d=new FolderBrowserDialog{Description="Choose where to save. A folder named after the map is created inside it."}){
      if(parent!=null){Directory.CreateDirectory(parent);d.SelectedPath=parent;}
      if(d.ShowDialog(this)!=DialogResult.OK)return;
      parent=d.SelectedPath;
     }
    }
    folder=Path.Combine(parent,SafeName(pack.Name));
    if(Directory.Exists(folder)&&Directory.GetFileSystemEntries(folder).Length>0){
     if(!File.Exists(Path.Combine(folder,"map.json"))){MessageBox.Show(this,folder+" already has other files in it. Rename the map or pick another place.","Folder not empty");return;}
     if(MessageBox.Show(this,"Replace the map pack in "+folder+"?","Replace map pack",MessageBoxButtons.YesNo)!=DialogResult.Yes)return;
     foreach(var t in pack.Tiles.Values)t.Dirty=true;
    }
   }
   Cursor=Cursors.WaitCursor;
   try{pack.Save(folder,game==null?"":game.BuildId);unsaved=false;UpdateCounts();status.Text="Saved to "+folder;}
   catch(Exception e){MessageBox.Show(this,e.Message,"Could not save",MessageBoxButtons.OK,MessageBoxIcon.Error);}
   finally{Cursor=Cursors.Default;}
  }
  static string SafeName(string name){foreach(char c in Path.GetInvalidFileNameChars())name=name.Replace(c,'_');return name.Length==0?"My map":name;}

  void ShowTile(TileKey? key){
   var t=key==null||pack==null?null:pack.Get(key.Value);
   if(t==null){tileTitle.Text="Click a tile on the map.";tileJson.Text="";applyJson.Enabled=false;return;}
   tileTitle.Text=t.Key.FileName+"\n"+t.Definition+", height "+t.Height+" ("+t.Height*32+" m)";
   tileJson.Text=Json.Write(t.Data,true).Replace("\n","\r\n");applyJson.Enabled=false;
  }
  void ApplyTileJson(){
   var key=view.Selected;if(key==null||pack==null)return;
   JsonObject data;
   try{data=Json.Parse(tileJson.Text) as JsonObject;if(data==null)throw new FormatException("A tile must be a JSON object { ... }");}
   catch(Exception e){MessageBox.Show(this,e.Message,"That isn't valid tile JSON",MessageBoxButtons.OK,MessageBoxIcon.Warning);return;}
   var t=pack.Get(key.Value);t.Data=data;t.Dirty=true;unsaved=true;
   view.ShowScene(MapScene.Build(pack));RefreshLayers();ShowTile(key);UpdateCounts();
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
