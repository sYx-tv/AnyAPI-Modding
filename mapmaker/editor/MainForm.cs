using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Windows.Forms;
namespace AnyMapMaker {
 public sealed class MainForm : Form {
  public const string Version="0.2.0";
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
  MapEditor editor; Catalog catalog=Catalog.Load(null);
  readonly FlowLayoutPanel toolButtons=new FlowLayoutPanel{Dock=DockStyle.Top,Height=124,Padding=new Padding(0,2,0,2)};
  readonly FlowLayoutPanel options=new FlowLayoutPanel{Dock=DockStyle.Top,Height=210,FlowDirection=FlowDirection.TopDown,WrapContents=false,AutoScroll=true};
  ToolStripMenuItem undoItem, redoItem;
  // Tool options (kept between tool switches).
  readonly ComboBox pieceBox=Combo(), orientBox=Combo(), roadBox=Combo(), fenceBox=Combo(), propBox=Combo(), filterBox=Combo();
  readonly CheckBox setHeight=new CheckBox{Text="Also set height",AutoSize=true}, eraseThings=new CheckBox{Text="Objects",Checked=true,AutoSize=true}, eraseRoads=new CheckBox{Text="Roads",Checked=true,AutoSize=true};
  readonly NumericUpDown heightBox=Num(-6,16,0), rotBox=Num(0,359,0), sizeBox=Num(0,5,2), radiusBox=Num(1,200,8);

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
   file.DropDownItems.Insert(0,new ToolStripMenuItem("&New blank island...",null,(s,e)=>NewIsland()));
   var edit=new ToolStripMenuItem("&Edit");
   undoItem=new ToolStripMenuItem("&Undo",null,(s,e)=>{if(editor!=null&&editor.Undo())Edited(true);}){ShortcutKeys=Keys.Control|Keys.Z};
   redoItem=new ToolStripMenuItem("&Redo",null,(s,e)=>{if(editor!=null&&editor.Redo())Edited(true);}){ShortcutKeys=Keys.Control|Keys.Y};
   edit.DropDownItems.AddRange(new ToolStripItem[]{undoItem,redoItem});
   var viewMenu=new ToolStripMenuItem("&View");
   ((ToolStripMenuItem)viewMenu.DropDownItems.Add("&Fit whole map",null,(s,e)=>view.FitAll())).ShortcutKeys=Keys.Control|Keys.D0;
   var help=new ToolStripMenuItem("&Help");
   help.DropDownItems.Add("&Controls",null,(s,e)=>MessageBox.Show(this,"Scroll to zoom. Drag with the right or middle mouse button to move around.\nPick a tool on the left; its options appear under it. Ctrl+Z undoes, Ctrl+Y redoes.\n\nTerrain: click or drag over triangles to paint a terrain piece (ocean removes land).\nHeight: click to raise a triangle 32 m, Ctrl+click to lower it.\nRoad: click points along the road, double click or Enter to finish, Backspace removes the last point, Esc cancels.\nFence: drag a line.\nProp, building spot, loot: click to place, or drag to aim it.\nErase: click or drag over things to remove them.\nSelect: click a tile to see and edit its raw data on the right.\n\nSave map pack writes the map to AnyAPI and Modding\\maps in the game folder, where the CustomMap mod finds it.","Controls"));
   help.DropDownItems.Add("&About",null,(s,e)=>MessageBox.Show(this,"Anymaker Map Maker "+Version+"\nPart of AnyAPI-Modding. Map packs are loaded in game by the CustomMap mod.","About"));
   menu.Items.AddRange(new ToolStripItem[]{file,edit,viewMenu,help});
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
   left.Width=270;left.AutoScroll=true;
   foreach(Tool t in Enum.GetValues(typeof(Tool))){
    var b=new RadioButton{Text=ToolName(t),Appearance=Appearance.Button,Width=76,Height=26,TextAlign=ContentAlignment.MiddleCenter,FlatStyle=FlatStyle.Flat,Tag=t,Checked=t==Tool.Select,Margin=new Padding(1)};
    b.FlatAppearance.CheckedBackColor=Color.FromArgb(56,200,240);b.BackColor=Raised;
    b.CheckedChanged+=(s,e)=>{var rb=(RadioButton)s;rb.ForeColor=rb.Checked?Color.FromArgb(3,16,24):Ink;if(rb.Checked)SetTool((Tool)rb.Tag);};
    toolButtons.Controls.Add(b);
   }
   left.Controls.Add(layers);left.Controls.Add(Caption("LAYERS"));left.Controls.Add(options);left.Controls.Add(Caption("TOOL OPTIONS"));
   left.Controls.Add(toolButtons);left.Controls.Add(Caption("TOOLS"));left.Controls.Add(mapBox);
   foreach(var c in new[]{fenceBox,roadBox,filterBox})c.Items.Clear();
   fenceBox.Items.AddRange(FenceStyle.All);fenceBox.SelectedIndex=0;
   roadBox.Items.AddRange(Catalog.RoadTypes);roadBox.SelectedIndex=0;
   filterBox.Items.AddRange(Catalog.BuildingFilters);filterBox.SelectedIndex=0;
   orientBox.Items.AddRange(new object[]{"Keep turn","Turn 0","Turn 1","Turn 2"});orientBox.SelectedIndex=0;
   FillCatalogBoxes();
   view.TilePainted+=OnTilePainted;view.Placed+=OnPlaced;view.LineDrawn+=OnLine;view.RoadFinished+=OnRoad;view.Erased+=OnErase;
   view.StrokeEnded+=()=>{if(editor!=null&&editor.Commit())Edited(true);};
   radiusBox.ValueChanged+=(s,e)=>view.EraseRadius=(double)radiusBox.Value;
   SetTool(Tool.Select);

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

  public static bool Headless;
  void ChooseGame(bool ask){
   if(Headless)return;
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
   catalog=Catalog.Load(game);FillCatalogBoxes();
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
    pack=loader();editor=new MapEditor(pack);var scene=MapScene.Build(pack);view.SetMap(pack,scene);view.ClearRoad();
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

  // ---------------------------------------------------------------- tools
  static ComboBox Combo(){return new ComboBox{DropDownStyle=ComboBoxStyle.DropDownList,Width=232,FlatStyle=FlatStyle.Flat,MaxDropDownItems=24};}
  static NumericUpDown Num(int min,int max,int value){return new NumericUpDown{Minimum=min,Maximum=max,Value=value,Width=80};}
  static string ToolName(Tool t){
   switch(t){case Tool.Select:return "Select";case Tool.Terrain:return "Terrain";case Tool.Height:return "Height";case Tool.Road:return "Road";
    case Tool.Fence:return "Fence";case Tool.Prop:return "Prop";case Tool.Building:return "Building";case Tool.Loot:return "Loot";
    case Tool.Enemy:return "Enemy";default:return "Erase";}
  }
  void FillCatalogBoxes(){
   string piece=pieceBox.SelectedItem as string,prop=propBox.SelectedItem as string;
   pieceBox.Items.Clear();pieceBox.Items.Add("(ocean: remove land)");foreach(var d in catalog.PaletteNames())pieceBox.Items.Add(d);
   pieceBox.SelectedItem=piece!=null&&pieceBox.Items.Contains(piece)?piece:(pieceBox.Items.Contains("tile_flat_a")?"tile_flat_a":pieceBox.Items[0]);
   propBox.Items.Clear();foreach(var d in catalog.Props)propBox.Items.Add(d);
   propBox.SelectedItem=prop!=null&&propBox.Items.Contains(prop)?prop:(propBox.Items.Contains("lamp_post_a")?"lamp_post_a":propBox.Items[0]);
  }
  Label Hint(string text){return new Label{Text=text,ForeColor=Muted,AutoSize=false,Width=232,Height=46};}
  Label Field(string text){return new Label{Text=text,ForeColor=Ink,AutoSize=true,Margin=new Padding(3,6,3,0)};}
  void SetTool(Tool t){
   view.Tool=t;view.ClearRoad();options.Controls.Clear();
   switch(t){
    case Tool.Select:options.Controls.Add(Hint("Click a tile to see its raw data on the right. Edit it there and press Apply."));break;
    case Tool.Terrain:
     options.Controls.AddRange(new Control[]{Field("Terrain piece"),pieceBox,Field("Turn"),orientBox,setHeight,heightBox,
      Hint("Click or drag over triangles. Upper triangles get the matching _in piece. Neighbouring pieces should have matching edges.")});break;
    case Tool.Height:options.Controls.Add(Hint("Click or drag to raise triangles one step (32 m). Hold Ctrl to lower. Things on a tile move with it."));break;
    case Tool.Road:options.Controls.AddRange(new Control[]{Field("Road type"),roadBox,Hint("Click points along the road. Double click or Enter finishes, Backspace removes the last point, Esc cancels.")});break;
    case Tool.Fence:options.Controls.AddRange(new Control[]{Field("Fence"),fenceBox,Hint("Drag a line to build a fence along it.")});break;
    case Tool.Prop:options.Controls.AddRange(new Control[]{Field("Prop (lamp_post_a is a street light)"),propBox,Field("Turn (degrees)"),rotBox,Hint("Click to place. Drag to aim it instead.")});break;
    case Tool.Building:options.Controls.AddRange(new Control[]{Field("Building type"),filterBox,Field("Size (0 = any)"),sizeBox,
     Hint("A building spot. The game picks a building of this type for it when a world is created. Drag to aim the front.")});break;
    case Tool.Loot:options.Controls.Add(Hint("Click to add a loot spot."));break;
    case Tool.Enemy:options.Controls.Add(Hint("Click to add an enemy spawn point."));break;
    case Tool.Erase:options.Controls.AddRange(new Control[]{Field("Radius (m)"),radiusBox,eraseThings,eraseRoads,Hint("Click or drag over things to remove them.")});break;
   }
   foreach(Control c in options.Controls){if(c is ComboBox||c is NumericUpDown){c.BackColor=Raised;c.ForeColor=Ink;}if(c is CheckBox)c.ForeColor=Ink;}
   view.Invalidate();view.Focus();
  }
  bool Ready(){if(pack==null||editor==null){status.Text="Open the stock world, a map pack or a new island first (File menu).";return false;}return true;}
  void Edited(bool rebuild){
   unsaved=true;if(rebuild){view.ShowScene(MapScene.Build(pack));RefreshLayers();}else view.Invalidate();
   UpdateCounts();ShowTile(view.Selected);undoItem.Enabled=editor.CanUndo;redoItem.Enabled=editor.CanRedo;
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
   if(!Ready())return;editor.Begin();bool done=false;double rot=aim??((double)rotBox.Value*Math.PI/180);
   switch(view.Tool){
    case Tool.Prop:done=editor.AddProp((string)propBox.SelectedItem,x,z,rot,0);break;
    case Tool.Building:done=editor.AddBuildingSpot((string)filterBox.SelectedItem,(int)sizeBox.Value,x,z,rot);break;
    case Tool.Loot:done=editor.AddLoot(x,z,rot);break;
    case Tool.Enemy:done=editor.AddEnemySpawn(x,z);break;
   }
   if(!done)status.Text="That spot is ocean. Paint terrain there first.";
  }
  void OnLine(double x0,double z0,double x1,double z1){if(!Ready())return;editor.Begin();status.Text=editor.AddFence((FenceStyle)fenceBox.SelectedItem,x0,z0,x1,z1)+" fence pieces placed";}
  void OnRoad(List<Vec3> points){if(!Ready())return;editor.Begin();status.Text=editor.AddRoad((string)roadBox.SelectedItem,points)+" road links placed";if(editor.Commit())Edited(true);}
  void OnErase(double x,double z){if(!Ready())return;editor.Begin();if(editor.Erase(x,z,(double)radiusBox.Value,eraseThings.Checked,eraseRoads.Checked)>0)unsaved=true;}
  void NewIsland(){
   if(!ConfirmDiscard())return;
   int cells;using(var d=new Form{Text="New blank island",Width=360,Height=170,FormBorderStyle=FormBorderStyle.FixedDialog,StartPosition=FormStartPosition.CenterParent,MinimizeBox=false,MaximizeBox=false}){
    var size=Num(2,120,16);size.Location=new Point(220,18);
    d.Controls.Add(new Label{Text="Island size (grid cells per side, 512 m each)",Location=new Point(12,20),Width=205});d.Controls.Add(size);
    var ok=new Button{Text="Create",DialogResult=DialogResult.OK,Location=new Point(250,90)};d.Controls.Add(ok);d.AcceptButton=ok;
    if(d.ShowDialog(this)!=DialogResult.OK)return;cells=(int)size.Value;
   }
   // Centre it where the normal world's land is, so the game's start area is on the island.
   int cx=87,cz=118;
   try{if(game!=null){var stock=MapPack.LoadTiles(game.StockTiles,"stock");long sx=0,sz=0;foreach(var k in stock.Tiles.Keys){sx+=k.X;sz+=k.Z;}if(stock.Tiles.Count>0){cx=(int)(sx/stock.Tiles.Count);cz=(int)(sz/stock.Tiles.Count);}}}catch{}
   OpenWith(()=>MapEditor.BlankIsland(catalog,cx,cz,cells,cells,0));
  }

  // For --ui-shot: a small island with one of everything, Road tool selected.
  public void Demo(){
   OpenWith(()=>MapEditor.BlankIsland(catalog,10,10,3,3,0));
   var c=Grid.Origin(new TileKey(10,10,0),0);editor.Begin();
   editor.AddRoad("road",new List<Vec3>{new Vec3(c.X-500,0,c.Z-100),new Vec3(c.X,0,c.Z+80),new Vec3(c.X+500,0,c.Z)});
   editor.AddFence(FenceStyle.All[0],c.X-200,c.Z-200,c.X+100,c.Z-150);
   for(int i=0;i<6;i++)editor.AddProp("lamp_post_a",c.X-400+i*160,c.Z+30,0,0);
   editor.AddBuildingSpot("house",2,c.X+150,c.Z+200,0.3);editor.AddLoot(c.X-100,c.Z+250,0);editor.Commit();Edited(true);
   foreach(Control b in toolButtons.Controls)if((Tool)b.Tag==Tool.Road)((RadioButton)b).Checked=true;
   view.FitAll();
  }

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
   var t=pack.Get(key.Value);t.Data=data;t.Dirty=true;unsaved=true;editor=new MapEditor(pack);  // raw edits are not undoable
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
