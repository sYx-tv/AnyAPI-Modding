using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Threading.Tasks;

namespace AnyApiManager {
 public sealed class ManagerRelease { public int Schema;public string Version,Url,Sha256;public long Size; }
 public sealed class ManagerUpdateRequest { public string Target,Staged,OriginalSha256;public int ParentId;public long ParentStarted;public ManagerRelease Release; }
 public static class ManagerUpdates {
  public const string Feed="https://raw.githubusercontent.com/sYx-tv/AnyAPI-Modding/main/manager-update.json";
  public static Version CurrentVersion {get{return Assembly.GetExecutingAssembly().GetName().Version;}}
  public static string DisplayVersion {get{return CurrentVersion.ToString(3);}}
  public static Version VersionOf(string text){Version v;if(!Version.TryParse(text,out v))throw new InvalidDataException("Invalid manager version.");return new Version(v.Major,v.Minor,Math.Max(0,v.Build),Math.Max(0,v.Revision));}
  public static void Validate(ManagerRelease r){
   Uri url;if(r==null||r.Schema!=1||!Rules.Digest(r.Sha256)||r.Size<1024||r.Size>67108864)throw new InvalidDataException("Invalid manager update details.");VersionOf(r.Version);
   if(!Uri.TryCreate(r.Url,UriKind.Absolute,out url)||url.Scheme!="https"||url.Host!="github.com"||url.Port!=443||url.UserInfo!=""||url.Query!=""||url.Fragment!=""||!System.Text.RegularExpressions.Regex.IsMatch(url.AbsolutePath,@"^/sYx-tv/AnyAPI-Modding/releases/download/[A-Za-z0-9_.-]+/AnyAPI\.Manager\.exe$"))throw new InvalidDataException("Manager updates must come from the official release.");
  }
  public static bool IsNewer(ManagerRelease r,Version current){Validate(r);return VersionOf(r.Version)>current;}
  public static async Task<ManagerRelease> Check(){var r=Json.Read<ManagerRelease>(System.Text.Encoding.UTF8.GetString(await Engine.DownloadBytes(Feed,16384)));Validate(r);return r;}
  public static void ValidateBinary(string file,ManagerRelease r){
   Validate(r);if(new FileInfo(file).Length!=r.Size||!string.Equals(Rules.Hash(file),r.Sha256,StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("Manager download checksum differs. Your current manager was kept.");
   byte[] b=File.ReadAllBytes(file);if(b.Length<90||b[0]!='M'||b[1]!='Z')throw new InvalidDataException("Manager download is not an executable.");int pe=BitConverter.ToInt32(b,60);
   if(pe<64||pe>b.Length-26||BitConverter.ToUInt32(b,pe)!=0x4550||BitConverter.ToUInt16(b,pe+4)!=0x8664||(BitConverter.ToUInt16(b,pe+22)&0x2000)!=0||BitConverter.ToUInt16(b,pe+24)!=0x20b)throw new InvalidDataException("Manager update requires a Windows x64 EXE.");
   var identity=AssemblyName.GetAssemblyName(file);if(identity.Name!="AnyAPI Manager"||identity.Version!=VersionOf(r.Version))throw new InvalidDataException("Downloaded manager version differs from the update details.");
  }
  public static async Task<string> Stage(ManagerRelease release,Action<long,long> progress=null){
   if(!IsNewer(release,CurrentVersion))throw new InvalidOperationException("Your manager is already up to date.");
   string dir=Path.Combine(Engine.Data,"manager-updates",Guid.NewGuid().ToString("N"));Directory.CreateDirectory(dir);
   string staged=Path.Combine(dir,"manager-new.exe");File.WriteAllBytes(staged,await Engine.DownloadBytes(release.Url,67108864,null,progress));ValidateBinary(staged,release);
   string target=Assembly.GetExecutingAssembly().Location;File.Copy(target,Path.Combine(dir,"update-helper.exe"));
   using(var parent=Process.GetCurrentProcess()){
    var request=new ManagerUpdateRequest{Target=target,Staged=staged,OriginalSha256=Rules.Hash(target),ParentId=parent.Id,ParentStarted=parent.StartTime.ToUniversalTime().Ticks,Release=release};
    string file=Path.Combine(dir,"request.json");File.WriteAllText(file,Json.Write(request));return file;
   }
  }
  public static void StartHelper(string requestFile){
   var request=Json.Read<ManagerUpdateRequest>(File.ReadAllText(requestFile));
   var start=new ProcessStartInfo(Path.Combine(Path.GetDirectoryName(requestFile),"update-helper.exe"),"--update-manager \""+requestFile+"\""){UseShellExecute=true,WindowStyle=ProcessWindowStyle.Hidden};
   // Ask Windows for elevation only when the manager's own folder needs it.
   string probe=Path.Combine(Path.GetDirectoryName(request.Target),".anyapi-update-"+Guid.NewGuid().ToString("N"));
   try{File.WriteAllText(probe,"");File.Delete(probe);}catch(UnauthorizedAccessException){start.Verb="runas";}
   using(var helper=Process.Start(start)){if(helper==null)throw new IOException("Could not start the manager updater.");}
  }
  public static void WaitForParent(ManagerUpdateRequest request){
   Process parent;try{parent=Process.GetProcessById(request.ParentId);}catch(ArgumentException){return;}
   using(parent){
    if(parent.StartTime.ToUniversalTime().Ticks!=request.ParentStarted)throw new IOException("The manager process changed. Update cancelled.");
    if(!string.Equals(Path.GetFullPath(parent.MainModule.FileName),Path.GetFullPath(request.Target),StringComparison.OrdinalIgnoreCase))throw new IOException("The update parent is not this manager.");
    if(!parent.WaitForExit(30000))throw new IOException("The manager is still open. Close it and try again.");
   }
  }
  public static string Replace(ManagerUpdateRequest request,Action<string> restart){
   ValidateBinary(request.Staged,request.Release);
   string target=Path.GetFullPath(request.Target);
   if(!target.EndsWith(".exe",StringComparison.OrdinalIgnoreCase)||target==Path.GetFullPath(request.Staged)||!string.Equals(Rules.Hash(target),request.OriginalSha256,StringComparison.OrdinalIgnoreCase))throw new IOException("Your manager file changed during the update. It was kept.");
   var old=AssemblyName.GetAssemblyName(target);if(old.Name!="AnyAPI Manager"||VersionOf(request.Release.Version)<=old.Version)throw new IOException("This update cannot replace a newer manager.");
   string temporary=target+"."+Guid.NewGuid().ToString("N")+".new",backup=target+"."+Guid.NewGuid().ToString("N")+".bak";bool swapped=false;
   try{
    File.Copy(request.Staged,temporary);ValidateBinary(temporary,request.Release);
    File.Replace(temporary,target,backup);swapped=true;
    if(!string.Equals(Rules.Hash(backup),request.OriginalSha256,StringComparison.OrdinalIgnoreCase))throw new IOException("Your manager changed during replacement. Update cancelled.");
    ValidateBinary(target,request.Release);restart(target);return backup;
   }catch(Exception failure){if(swapped)try{File.Replace(backup,target,null);}catch(Exception restore){throw new IOException("Restore the manager from "+backup+". "+restore.Message,failure);}throw;}
   finally{if(File.Exists(temporary))File.Delete(temporary);}
  }
  public static int RunHelper(string file){
   try{
    string dir=Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
    if(Path.GetFullPath(file)!=Path.Combine(dir,"request.json"))throw new InvalidDataException("Invalid updater request location.");
    var r=Json.Read<ManagerUpdateRequest>(File.ReadAllText(file));
    if(r==null||Path.GetFullPath(r.Staged)!=Path.Combine(dir,"manager-new.exe")||r.ParentId<=0)throw new InvalidDataException("Invalid updater request.");
    WaitForParent(r);string backup=Replace(r,path=>{using(var p=Process.Start(new ProcessStartInfo(path){UseShellExecute=true})){if(p==null)throw new IOException("Could not reopen the updated manager.");}});
    File.WriteAllText(file+".result",Json.Write(new{Success=true,Backup=backup}));return 0;
   }catch(Exception e){try{File.WriteAllText(file+".result",Json.Write(new{Success=false,Message=e.Message}));}catch{}
    System.Windows.Forms.MessageBox.Show("Manager update could not finish. Your mods and settings are unchanged.\n\n"+e.Message,"AnyAPI Manager",System.Windows.Forms.MessageBoxButtons.OK,System.Windows.Forms.MessageBoxIcon.Information);return 1;
   }
  }
 }
}
