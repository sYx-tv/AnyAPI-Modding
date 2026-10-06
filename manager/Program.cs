using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Forms;
namespace AnyApiManager {
 static class Program {
  [DllImport("user32.dll")]static extern bool SetProcessDPIAware();
  [STAThread]static int Main(string[] args){
   if(args.Length==2&&args[0]=="--apply"){
    ApplyResult result;try{result=Engine.Apply(Json.Read<ApplyRequest>(File.ReadAllText(args[1])));}catch(Exception e){result=new ApplyResult{Success=false,Message=e.Message};}
    File.WriteAllText(args[1]+".result",Json.Write(result));return result.Success?0:1;
   }
   if(args.Length==2&&args[0]=="--self-test")return SelfTests.Run(args[1]);
   SetProcessDPIAware();Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);
   if(args.Length==2&&args[0]=="--capture"){try{using(var window=new MainWindow(true)){window.Show();Application.DoEvents();window.CapturePreview(args[1]);}return 0;}catch(Exception e){File.WriteAllText(args[1]+".error",e.ToString());return 1;}}
   Application.Run(new MainWindow());return 0;
  }
 }
}
