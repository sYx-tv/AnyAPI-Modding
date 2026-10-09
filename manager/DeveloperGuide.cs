using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Windows.Forms;
namespace AnyApiManager {
 public sealed class GuideArticle {public string Title,Group,Body,Kind;}
 public sealed class GuideData {public int Revision,HeaderCount,LegacyHookCount;public string GameVersion;public long SteamBuild;public List<GuideArticle> Articles;public Dictionary<string,string> SourceHashes;}
 public static class Guide {
  public static GuideData Read(){return Json.Read<GuideData>(Engine.Resource("guide.json"));}
  public static void Export(string path){using(var source=Assembly.GetExecutingAssembly().GetManifestResourceStream("starter-sdk.zip"))using(var dest=File.Create(path))source.CopyTo(dest);}
 }
 public sealed class DeveloperPanel:Panel {
  readonly GuideData guide=Guide.Read();readonly TreeView tree=new TreeView();readonly RichTextBox reader=new RichTextBox();readonly Font groupFont=Theme.Semibold(12.5f);readonly Field searchField=new Field("Search API, functions, hooks");readonly TextBox search;readonly Label badge=new Label();GuideArticle current;
  public DeveloperPanel(Action<string> status){
   BackColor=Theme.Background;Dock=DockStyle.Fill;search=searchField.Box;
   var header=new Panel{Dock=DockStyle.Top,Height=Theme.S(46)};Controls.Add(header);
   var title=new Label{Text="Develop",Location=new Point(0,0),ForeColor=Theme.Ink,Font=Theme.Font(19,true)};title.Size=new Size(TextRenderer.MeasureText(title.Text,title.Font).Width+Theme.S(6),Theme.S(32));header.Controls.Add(title);
   badge.Text="Guide for API "+guide.Revision+" · Anymaker "+guide.GameVersion;badge.Bounds=new Rectangle(title.Right+Theme.S(8),Theme.S(8),Theme.S(480),Theme.S(20));badge.ForeColor=Theme.Dim;badge.Font=Theme.Font(12);badge.AutoEllipsis=true;header.Controls.Add(badge);
   var export=new ModernButton{Text="Export starter SDK",Kind=ButtonKind.Primary,Size=new Size(Theme.S(150),Theme.S(30))};header.Controls.Add(export);header.Resize+=(s,e)=>{export.Location=new Point(header.Width-export.Width,Theme.S(1));badge.Width=Math.Max(Theme.S(120),export.Left-badge.Left-Theme.S(12));};
   export.Click+=(s,e)=>{using(var dialog=new SaveFileDialog{Filter="ZIP archive|*.zip",FileName="AnyAPI-Starter-SDK-Rev"+guide.Revision+".zip"})if(dialog.ShowDialog(FindForm())==DialogResult.OK)try{Guide.Export(dialog.FileName);status("Starter SDK exported. Extract it and follow the first-mod guide.");}catch(Exception error){status(error.Message);}};
   var split=new SplitContainer{Size=new Size(Theme.S(800),Theme.S(500)),SplitterDistance=Theme.S(260),Dock=DockStyle.Fill,SplitterWidth=Theme.S(14),BackColor=Theme.Background,FixedPanel=FixedPanel.Panel1,Panel1MinSize=Theme.S(210),Panel2MinSize=Theme.S(300)};split.Panel1.BackColor=split.Panel2.BackColor=Theme.Panel;split.Panel1.Padding=split.Panel2.Padding=new Padding(1);Controls.Add(split);split.BringToFront();
   searchField.Dock=DockStyle.Top;searchField.BackColor=Theme.Panel;search.TextChanged+=(s,e)=>Rows();split.Panel1.Controls.Add(searchField);
   tree.Dock=DockStyle.Fill;tree.BorderStyle=BorderStyle.None;tree.BackColor=Theme.Surface;tree.ForeColor=Theme.Ink;tree.Font=Theme.Font(13);tree.ItemHeight=Theme.S(28);tree.Indent=Theme.S(16);tree.HideSelection=false;tree.DrawMode=TreeViewDrawMode.OwnerDrawText;tree.DrawNode+=DrawNode;tree.ShowLines=false;tree.ShowNodeToolTips=true;tree.FullRowSelect=true;split.Panel1.Controls.Add(tree);tree.BringToFront();tree.AfterSelect+=(s,e)=>{var article=e.Node.Tag as GuideArticle;if(article!=null)Show(article);};
   var tools=new Panel{Dock=DockStyle.Top,Height=Theme.S(42),BackColor=Theme.Surface};var copy=new ModernButton{Text="Copy page",Kind=ButtonKind.Ghost,Size=new Size(Theme.S(96),Theme.S(28)),Location=new Point(Theme.S(10),Theme.S(7))};tools.Controls.Add(copy);copy.Click+=(s,e)=>{if(current!=null){Clipboard.SetText(current.Body);status("Page copied.");}};split.Panel2.Controls.Add(tools);
   reader.Dock=DockStyle.Fill;reader.ReadOnly=true;reader.BorderStyle=BorderStyle.None;reader.BackColor=Theme.Surface;reader.ForeColor=Theme.Ink;reader.Font=Theme.Font(14);reader.DetectUrls=false;reader.WordWrap=true;reader.ScrollBars=RichTextBoxScrollBars.Vertical;split.Panel2.Controls.Add(reader);reader.BringToFront();Rows();
  }
  // Selected and hovered nodes use the accent tint instead of the system highlight colour.
  void DrawNode(object sender,DrawTreeNodeEventArgs e){if(e.Bounds.Width==0)return;bool selected=(e.State&TreeNodeStates.Selected)!=0;var r=new Rectangle(e.Bounds.X,e.Bounds.Y,tree.ClientSize.Width-e.Bounds.X,e.Bounds.Height);
   using(var b=new SolidBrush(selected?Theme.Selected:Theme.Surface))e.Graphics.FillRectangle(b,r);if(selected)using(var b=new SolidBrush(Theme.Blue))e.Graphics.FillRectangle(b,r.X,r.Y,Theme.S(2),r.Height);
   TextRenderer.DrawText(e.Graphics,e.Node.Text,e.Node.Parent==null?groupFont:tree.Font,new Rectangle(r.X+Theme.S(6),r.Y,r.Width-Theme.S(8),r.Height),selected?Theme.Ink:e.Node.Parent==null?Theme.Ink:Theme.Muted,TextFormatFlags.VerticalCenter|TextFormatFlags.Left|TextFormatFlags.SingleLine|TextFormatFlags.EndEllipsis);}
  public void InstalledRevision(int revision){badge.Text="Guide for API "+guide.Revision+" · "+guide.HeaderCount+" headers"+(revision>guide.Revision?" · Newer API installed: use matching source":" · Anymaker "+guide.GameVersion);}
  void Rows(){tree.BeginUpdate();tree.Nodes.Clear();foreach(var group in new[]{"Start here","Services","Examples","Headers","Hooks","Contracts","Legacy"}){
   var articles=guide.Articles.Where(a=>a.Group==group&&(search.Text==""||(a.Title+" "+a.Body).IndexOf(search.Text,StringComparison.OrdinalIgnoreCase)>=0)).ToArray();if(articles.Length==0)continue;
   var node=tree.Nodes.Add(group);foreach(var article in articles){var item=node.Nodes.Add(article.Title);item.Tag=article;item.ToolTipText=article.Title+" · "+article.Kind;}
   if(group=="Start here"||search.Text!="")node.Expand();
  }tree.EndUpdate();if(tree.Nodes.Count>0&&tree.Nodes[0].Nodes.Count>0)tree.SelectedNode=tree.Nodes[0].Nodes[0];else{reader.Text="No matching documentation.";current=null;}}
  void Show(GuideArticle article){current=article;reader.Clear();bool code=article.Kind=="Native contract data";foreach(var line in article.Body.Replace("\r","").Split('\n')){
   if(line.StartsWith("```")){code=!code;continue;}string value=line;bool heading=!code&&line.StartsWith("#");if(heading)value=line.TrimStart('#',' ');
   using(var font=code?Theme.Mono(12.5f):Theme.Font(heading?19:14,heading))reader.SelectionFont=font;reader.SelectionColor=heading?Theme.Ink:code?Color.FromArgb(185,196,214):Theme.Muted;reader.SelectionIndent=Theme.S(18);reader.SelectionRightIndent=Theme.S(18);reader.AppendText(value+"\n");
  }reader.SelectionStart=0;reader.ScrollToCaret();}
 }
}
