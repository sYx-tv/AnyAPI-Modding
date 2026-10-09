using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Forms;
namespace AnyApiManager {
 static class Program {
  [DllImport("user32.dll")]static extern bool SetProcessDPIAware();
  [STAThread]static int Main(string[] args){
   AppDomain.CurrentDomain.UnhandledException+=(s,e)=>ManagerLog.Error("Manager",e.ExceptionObject as Exception);
   ManagerLog.Write((args.Length>0?args[0]+": ":"Start: ")+ManagerLog.Describe());
   if(args.Length==2&&args[0]=="--update-manager")return ManagerUpdates.RunHelper(args[1]);
   if(args.Length==2&&args[0]=="--apply"){
    ApplyResult result;try{result=Engine.Apply(Json.Read<ApplyRequest>(File.ReadAllText(args[1])));}catch(Exception e){result=new ApplyResult{Success=false,Message=e.Message};}
    File.WriteAllText(args[1]+".result",Json.Write(result));return result.Success?0:1;
   }
   if(args.Length==2&&args[0]=="--self-test")return SelfTests.Run(args[1]);
   Application.ThreadException+=(s,e)=>{ManagerLog.Error("Manager window",e.Exception);MessageBox.Show("Something went wrong in the manager. Details were saved to:\n"+ManagerLog.LogFile+"\n\n"+e.Exception.Message,"AnyAPI Manager",MessageBoxButtons.OK,MessageBoxIcon.Warning);};
   SetProcessDPIAware();Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);
   if(args.Length==2&&args[0]=="--capture"){try{using(var window=new MainWindow(true)){window.Show();Application.DoEvents();window.CapturePreview(args[1]);}return 0;}catch(Exception e){File.WriteAllText(args[1]+".error",e.ToString());return 1;}}
   Application.Run(new MainWindow());return 0;
  }
 }
}
