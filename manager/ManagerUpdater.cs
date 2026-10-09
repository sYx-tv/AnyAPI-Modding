using System;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace AnyApiManager {
 // UI-independent manager update flow. Any front end binds to Changed and reads State, Release,
 // Progress and Message; it calls Check on launch, Install when the user accepts, and exits the
 // app once State is Restarting. ManagerUpdates does the verified download and EXE replacement.
 public enum ManagerUpdateState { Idle, Checking, UpToDate, Available, Downloading, Restarting, Failed }
 public sealed class ManagerUpdateResult { public bool Success;public string Backup,Message; }
 public sealed class ManagerUpdater {
  readonly SynchronizationContext ui=SynchronizationContext.Current;
  public ManagerUpdateState State {get;private set;}
  public ManagerRelease Release {get;private set;}
  // 0..1 while downloading, or -1 when the server sent no size.
  public double Progress {get;private set;}
  public string Message {get;private set;}
  public event Action Changed;
  public bool UpdateAvailable {get{return State==ManagerUpdateState.Available||State==ManagerUpdateState.Downloading||State==ManagerUpdateState.Restarting||(State==ManagerUpdateState.Failed&&Release!=null);}}
  // GitHub release page for "What's new", derived from the verified download URL.
  public string ReleasePage {get{return Release==null?null:Release.Url.Substring(0,Release.Url.LastIndexOf('/')).Replace("/releases/download/","/releases/tag/");}}

  void Set(ManagerUpdateState state,string message,double progress=0){
   State=state;Message=message;Progress=progress;var changed=Changed;if(changed==null)return;
   if(ui==null||SynchronizationContext.Current==ui)changed();else ui.Post(_=>changed(),null);
  }
  // quiet: a launch check that stays silent (Idle) when offline or already current.
  public async Task Check(bool quiet){
   if(State==ManagerUpdateState.Checking||State==ManagerUpdateState.Downloading||State==ManagerUpdateState.Restarting)return;
   Set(ManagerUpdateState.Checking,quiet?null:"Checking for a manager update...");
   try{
    var release=await ManagerUpdates.Check();
    if(ManagerUpdates.IsNewer(release,ManagerUpdates.CurrentVersion)){Release=release;Set(ManagerUpdateState.Available,"Manager "+release.Version+" is available.");}
    else{Release=null;Set(quiet?ManagerUpdateState.Idle:ManagerUpdateState.UpToDate,quiet?null:"Your manager is up to date.");}
   }catch(Exception e){Release=null;if(quiet)Set(ManagerUpdateState.Idle,null);else Set(ManagerUpdateState.Failed,Describe(e));}
  }
  // Downloads, verifies and hands over to the helper. On success State becomes Restarting and the
  // caller must close the app so the helper can replace the EXE. Failures keep the current manager.
  public async Task Install(){
   if(Release==null){await Check(false);if(State!=ManagerUpdateState.Available)return;}
   if(State==ManagerUpdateState.Downloading||State==ManagerUpdateState.Restarting)return;
   var release=Release;Set(ManagerUpdateState.Downloading,"Downloading manager "+release.Version+"...",0);
   try{
    string request=await ManagerUpdates.Stage(release,(done,total)=>{
     double p=total>0?Math.Min(1.0,(double)done/total):-1;if(p<0||p-Progress>=0.01||p>=1)Set(ManagerUpdateState.Downloading,"Downloading manager "+release.Version+"...",p);
    });
    ManagerUpdates.StartHelper(request);Set(ManagerUpdateState.Restarting,"Restarting to finish the update...",1);
   }catch(Exception e){Set(ManagerUpdateState.Failed,Describe(e));}
  }
  static string Describe(Exception e){
   if(e is System.Net.Http.HttpRequestException)return "Couldn't reach GitHub to update the manager. Check your connection and try again.";
   if(e is System.ComponentModel.Win32Exception)return "The manager update was cancelled. Your current manager was kept.";
   return e.Message;
  }

  // Run once at launch: removes finished update folders and the old EXE backup once the new
  // version is the one running. Returns a status line when this launch completed an update.
  public static string FinishPrevious(){return FinishPrevious(Path.Combine(Engine.Data,"manager-updates"),ManagerUpdates.CurrentVersion,DateTime.UtcNow);}
  public static string FinishPrevious(string root,Version running,DateTime now){
   if(!Directory.Exists(root))return null;string note=null;
   foreach(string dir in Directory.GetDirectories(root)){
    try{
     string result=Path.Combine(dir,"request.json.result"),request=Path.Combine(dir,"request.json");
     if(File.Exists(result)){
      var outcome=Json.Read<ManagerUpdateResult>(File.ReadAllText(result));ManagerUpdateRequest asked=null;
      try{asked=Json.Read<ManagerUpdateRequest>(File.ReadAllText(request));}catch{}
      if(outcome!=null&&outcome.Success&&asked!=null&&asked.Release!=null){
       var target=ManagerUpdates.VersionOf(asked.Release.Version);if(running<target)continue; // An older copy is running; leave the backup alone.
       if(running==target)note="Manager updated to "+asked.Release.Version+".";
       if(IsBackupOf(outcome.Backup,asked.Target)&&File.Exists(outcome.Backup))File.Delete(outcome.Backup);
      }
      Directory.Delete(dir,true);
     }else if(now-Directory.GetCreationTimeUtc(dir)>TimeSpan.FromDays(1))Directory.Delete(dir,true); // Abandoned before the helper finished.
    }catch(Exception){} // Locked by a helper that is still exiting; try again next launch.
   }
   return note;
  }
  // Only delete the "<manager>.exe.<guid>.bak" file that ManagerUpdates.Replace created.
  static bool IsBackupOf(string backup,string target){
   if(string.IsNullOrEmpty(backup)||string.IsNullOrEmpty(target))return false;
   string b=Path.GetFullPath(backup),t=Path.GetFullPath(target);
   if(!string.Equals(Path.GetDirectoryName(b),Path.GetDirectoryName(t),StringComparison.OrdinalIgnoreCase))return false;
   string name=Path.GetFileName(b),prefix=Path.GetFileName(t)+".";
   if(!name.StartsWith(prefix,StringComparison.OrdinalIgnoreCase)||!name.EndsWith(".bak",StringComparison.OrdinalIgnoreCase))return false;
   string id=name.Substring(prefix.Length,name.Length-prefix.Length-4);return id.Length==32&&id.All(Uri.IsHexDigit);
  }
 }
}
