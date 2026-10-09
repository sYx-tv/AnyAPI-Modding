using System;
using System.IO;
using System.Security.Principal;

namespace AnyApiManager {
 // Plain-text log at %LOCALAPPDATA%\AnyAPI Manager\manager.log so a player can send it when something fails on their PC.
 public static class ManagerLog {
  public static readonly string LogFile=Path.Combine(Engine.Data,"manager.log");
  static readonly object Gate=new object();
  public static void Write(string text){
   try{lock(Gate){
    Directory.CreateDirectory(Engine.Data);var info=new FileInfo(LogFile);if(info.Exists&&info.Length>1048576){File.Copy(LogFile,LogFile+".old",true);File.Delete(LogFile);}
    File.AppendAllText(LogFile,DateTime.UtcNow.ToString("yyyy-MM-dd HH:mm:ss")+"Z ["+System.Diagnostics.Process.GetCurrentProcess().Id+"] "+text+Environment.NewLine);
   }}catch{}
  }
  public static void Error(string context,Exception e){Write(context+" failed: "+(e==null?"unknown error":e.ToString()));}
  static string Reg(string key,string name){try{object v=Microsoft.Win32.Registry.GetValue(key,name,null);return v==null?"":v.ToString();}catch{return "";}}
  // Windows 11 still reports itself as 10.0 (and "Windows 10" in ProductName), so the build number decides.
  public static string WindowsName(int build){return build>=22000?"Windows 11":build>=10240?"Windows 10":"Windows (pre-10)";}
  public static string NetFrameworkName(int release){
   return release>=533320?"4.8.1":release>=528040?"4.8":release>=461808?"4.7.2":release>=461308?"4.7.1":release>=460798?"4.7":release>=394802?"4.6.2":release>0?"older than 4.6.2":"unknown";
  }
  public static int NetFrameworkRelease(){int r;return int.TryParse(Reg(@"HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\NET Framework Setup\NDP\v4\Full","Release"),out r)?r:0;}
  public static string Describe(){
   const string nt=@"HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion";
   var os=Environment.OSVersion.Version;int release=NetFrameworkRelease();bool admin=false;
   try{using(var id=WindowsIdentity.GetCurrent())admin=new WindowsPrincipal(id).IsInRole(WindowsBuiltInRole.Administrator);}catch{}
   return "Manager "+ManagerUpdates.DisplayVersion+" on "+WindowsName(os.Build)+" "+Reg(nt,"DisplayVersion")+" (build "+os.Build+"."+Reg(nt,"UBR")+", "+Reg(nt,"EditionID")+")"+
    ", .NET Framework "+NetFrameworkName(release)+" (release "+release+", CLR "+Environment.Version+")"+
    ", "+(Environment.Is64BitOperatingSystem?"64-bit":"32-bit")+" Windows, "+(Environment.Is64BitProcess?"64-bit":"32-bit")+" process"+
    ", "+(admin?"elevated":"not elevated")+", culture "+System.Globalization.CultureInfo.CurrentCulture.Name+
    ", running from "+AppDomain.CurrentDomain.BaseDirectory;
  }
 }
}
