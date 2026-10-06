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
  readonly GuideData guide=Guide.Read();readonly TreeView tree=new TreeView();readonly RichTextBox reader=new RichTextBox();readonly TextBox search=new TextBox();readonly Label badge=new Label();GuideArticle current;
  [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern IntPtr SendMessage(IntPtr handle,int message,IntPtr word,string value);
  public DeveloperPanel(Action<string> status){
   BackColor=Theme.Background;Dock=DockStyle.Fill;
   var header=new Panel{Dock=DockStyle.Top,Height=92};Controls.Add(header);
   header.Controls.Add(new Label{Text="Develop",Location=new Point(0,0),Size=new Size(420,42),ForeColor=Theme.Ink,Font=Theme.Font(28,true)});
   badge.Text="Guide for API "+guide.Revision+" · Anymaker "+guide.GameVersion;badge.Bounds=new Rectangle(0,48,620,30);badge.ForeColor=Theme.Muted;badge.Font=Theme.Font(14);header.Controls.Add(badge);
   var export=new ModernButton{Text="Export starter SDK",Size=new Size(180,40),BackColor=Theme.Blue,ForeColor=Color.White,Font=Theme.Font(15,true),Anchor=AnchorStyles.Top|AnchorStyles.Right};header.Controls.Add(export);header.Resize+=(s,e)=>export.Location=new Point(header.Width-180,10);
   export.Click+=(s,e)=>{using(var dialog=new SaveFileDialog{Filter="ZIP archive|*.zip",FileName="AnyAPI-Starter-SDK-Rev"+guide.Revision+".zip"})if(dialog.ShowDialog(FindForm())==DialogResult.OK)try{Guide.Export(dialog.FileName);status("Starter SDK exported. Extract it and follow the first-mod guide.");}catch(Exception error){status(error.Message);}};
   var split=new SplitContainer{Size=new Size(800,500),SplitterDistance=250,Dock=DockStyle.Fill,SplitterWidth=16,BackColor=Theme.Background,FixedPanel=FixedPanel.Panel1,Panel1MinSize=210,Panel2MinSize=300};Controls.Add(split);split.BringToFront();
   search.Dock=DockStyle.Top;search.Height=38;search.Font=Theme.Font(15);search.BackColor=Theme.Raised;search.ForeColor=Theme.Ink;search.BorderStyle=BorderStyle.FixedSingle;search.HandleCreated+=(s,e)=>SendMessage(search.Handle,0x1501,new IntPtr(1),"Search API, functions, hooks");search.TextChanged+=(s,e)=>Rows();split.Panel1.Controls.Add(search);
   tree.Dock=DockStyle.Fill;tree.BorderStyle=BorderStyle.None;tree.BackColor=Theme.Surface;tree.ForeColor=Theme.Ink;tree.Font=Theme.Font(14);tree.ItemHeight=30;tree.HideSelection=false;tree.ShowLines=false;tree.ShowNodeToolTips=true;tree.FullRowSelect=true;split.Panel1.Controls.Add(tree);tree.BringToFront();tree.AfterSelect+=(s,e)=>{var article=e.Node.Tag as GuideArticle;if(article!=null)Show(article);};
   var tools=new Panel{Dock=DockStyle.Top,Height=42,BackColor=Theme.Surface};var copy=new ModernButton{Text="Copy page",Size=new Size(110,32),Location=new Point(8,5),BackColor=Theme.Raised,ForeColor=Theme.Muted,Font=Theme.Font(13)};tools.Controls.Add(copy);copy.Click+=(s,e)=>{if(current!=null){Clipboard.SetText(current.Body);status("Page copied.");}};split.Panel2.Controls.Add(tools);
   reader.Dock=DockStyle.Fill;reader.ReadOnly=true;reader.BorderStyle=BorderStyle.None;reader.BackColor=Theme.Surface;reader.ForeColor=Theme.Ink;reader.Font=Theme.Font(16);reader.DetectUrls=false;reader.WordWrap=true;reader.ScrollBars=RichTextBoxScrollBars.Vertical;split.Panel2.Controls.Add(reader);reader.BringToFront();Rows();
  }
  public void InstalledRevision(int revision){badge.Text="Guide for API "+guide.Revision+" · "+guide.HeaderCount+" headers"+(revision>guide.Revision?" · Newer API installed: use matching source":" · Anymaker "+guide.GameVersion);}
  void Rows(){tree.BeginUpdate();tree.Nodes.Clear();foreach(var group in new[]{"Start here","Services","Examples","Headers","Hooks","Contracts","Legacy"}){
   var articles=guide.Articles.Where(a=>a.Group==group&&(search.Text==""||(a.Title+" "+a.Body).IndexOf(search.Text,StringComparison.OrdinalIgnoreCase)>=0)).ToArray();if(articles.Length==0)continue;
   var node=tree.Nodes.Add(group);foreach(var article in articles){var item=node.Nodes.Add(article.Title);item.Tag=article;item.ToolTipText=article.Title+" · "+article.Kind;}
   if(group=="Start here"||search.Text!="")node.Expand();
  }tree.EndUpdate();if(tree.Nodes.Count>0&&tree.Nodes[0].Nodes.Count>0)tree.SelectedNode=tree.Nodes[0].Nodes[0];else{reader.Text="No matching documentation.";current=null;}}
  void Show(GuideArticle article){current=article;reader.Clear();bool code=article.Kind=="Native contract data";foreach(var line in article.Body.Replace("\r","").Split('\n')){
   if(line.StartsWith("```")){code=!code;continue;}string value=line;bool heading=!code&&line.StartsWith("#");if(heading)value=line.TrimStart('#',' ');
   using(var font=code?new Font("Consolas",13,FontStyle.Regular,GraphicsUnit.Pixel):Theme.Font(heading?22:16,heading))reader.SelectionFont=font;reader.SelectionColor=heading?Theme.Ink:code?Color.FromArgb(169,201,241):Theme.Muted;reader.SelectionIndent=20;reader.SelectionRightIndent=20;reader.AppendText(value+"\n");
  }reader.SelectionStart=0;reader.ScrollToCaret();}
 }
}
