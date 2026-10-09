using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Reflection.Emit;
using System.Threading;

namespace AnyApiManager {
 static class ManagerUpdateTests {
  static string OldManager(string directory){
   var name=new AssemblyName("AnyAPI Manager"){Version=new Version(1,0,0,0)};
   var assembly=AppDomain.CurrentDomain.DefineDynamicAssembly(name,AssemblyBuilderAccess.Save,directory);
   var module=assembly.DefineDynamicModule("old-manager","old-manager.exe");var type=module.DefineType("Entry",TypeAttributes.Public);
   var main=type.DefineMethod("Main",MethodAttributes.Public|MethodAttributes.Static,typeof(void),Type.EmptyTypes);var il=main.GetILGenerator();
   il.Emit(OpCodes.Ldc_I4,800);il.Emit(OpCodes.Call,typeof(Thread).GetMethod("Sleep",new[]{typeof(int)}));il.Emit(OpCodes.Ret);type.CreateType();assembly.SetEntryPoint(main,PEFileKinds.ConsoleApplication);
   assembly.Save("old-manager.exe",PortableExecutableKinds.ILOnly|PortableExecutableKinds.PE32Plus,ImageFileMachine.AMD64);return Path.Combine(directory,"old-manager.exe");
  }
  public static void Run(string root,Action<bool,string> check,Action<Action,string> reject){
   string dir=Path.Combine(root,"self-update");Directory.CreateDirectory(dir);
   string current=Assembly.GetExecutingAssembly().Location,staged=Path.Combine(dir,"new.exe");File.Copy(current,staged);
   var release=new ManagerRelease{Schema=1,Version=ManagerUpdates.CurrentVersion.ToString(),Url="https://github.com/sYx-tv/AnyAPI-Modding/releases/download/manager-v1.3.0/AnyAPI.Manager.exe",Sha256=Rules.Hash(staged),Size=new FileInfo(staged).Length};
   ManagerUpdates.ValidateBinary(staged,release);check(true,"Manager update checks x64 executable identity, version, size and checksum");
   check(!ManagerUpdates.IsNewer(release,ManagerUpdates.CurrentVersion)&&ManagerUpdates.IsNewer(release,new Version(1,2,1,0)),"Manager versions compare without updating or downgrading the current build");
   release.Version=ManagerUpdates.DisplayVersion;check(!ManagerUpdates.IsNewer(release,ManagerUpdates.CurrentVersion),"Three-part update versions equal the matching four-part assembly version");
   var bad=Json.Read<ManagerRelease>(Json.Write(release));bad.Url="https://evil.example/AnyAPI.Manager.exe";reject(()=>ManagerUpdates.Validate(bad),"Manager updater rejects non-official download hosts");
   bad=Json.Read<ManagerRelease>(Json.Write(release));bad.Url=release.Url+"?other=1";reject(()=>ManagerUpdates.Validate(bad),"Manager updater rejects injected URL parameters");
   bad=Json.Read<ManagerRelease>(Json.Write(release));bad.Sha256=new string('0',64);reject(()=>ManagerUpdates.ValidateBinary(staged,bad),"Manager updater rejects a corrupt download before replacement");
   bad=Json.Read<ManagerRelease>(Json.Write(release));bad.Version="99.0.0";reject(()=>ManagerUpdates.ValidateBinary(staged,bad),"Manager metadata must match the executable's actual version");
   bad=Json.Read<ManagerRelease>(Json.Write(release));bad.Size++;reject(()=>ManagerUpdates.ValidateBinary(staged,bad),"Manager updater rejects mismatching download size");
   string old=OldManager(dir),target=Path.Combine(dir,"manager.exe");File.Copy(old,target);string original=Rules.Hash(target);
   var request=new ManagerUpdateRequest{Target=target,Staged=staged,OriginalSha256=original,Release=release};bool restarted=false;
   string backup=ManagerUpdates.Replace(request,path=>{restarted=path==target;});check(restarted&&Rules.Hash(target)==release.Sha256&&Rules.Hash(backup)==original,"Manager EXE replacement retains an exact backup and requests restart");
   File.Copy(old,target,true);reject(()=>ManagerUpdates.Replace(request,path=>{throw new IOException("Injected restart failure");}),"Manager restart failure is surfaced");check(Rules.Hash(target)==original,"Failed manager restart restores the original executable");
   request.OriginalSha256=new string('0',64);reject(()=>ManagerUpdates.Replace(request,path=>{}),"Changed manager target is preserved");check(Rules.Hash(target)==original,"Changed-target rejection does not overwrite the existing EXE");request.OriginalSha256=original;
   File.Copy(staged,target,true);request.OriginalSha256=Rules.Hash(target);reject(()=>ManagerUpdates.Replace(request,path=>{}),"Manager helper rejects replacing the same or newer version");
   check(Directory.GetFiles(dir,"*.new").Length==0,"Manager updater removes its temporary replacement files");
   using(var parent=Process.Start(new ProcessStartInfo(old){UseShellExecute=false,CreateNoWindow=true})){
    var waiting=new ManagerUpdateRequest{Target=old,ParentId=parent.Id,ParentStarted=parent.StartTime.ToUniversalTime().Ticks};ManagerUpdates.WaitForParent(waiting);check(parent.HasExited,"Manager helper waits for the original EXE to exit");
   }
   using(var self=Process.GetCurrentProcess()){
    var wrong=new ManagerUpdateRequest{Target=current,ParentId=self.Id,ParentStarted=self.StartTime.ToUniversalTime().Ticks+1};reject(()=>ManagerUpdates.WaitForParent(wrong),"Manager helper rejects a reused or mismatching parent process");
   }
   FinishTests(dir,check);
  }
  static string FakeUpdate(string root,string target,string version,bool? success,string backup){
   string dir=Path.Combine(root,Guid.NewGuid().ToString("N"));Directory.CreateDirectory(dir);
   var release=new ManagerRelease{Schema=1,Version=version,Url="https://github.com/sYx-tv/AnyAPI-Modding/releases/download/manager-v"+version+"/AnyAPI.Manager.exe",Sha256=new string('0',64),Size=4096};
   File.WriteAllText(Path.Combine(dir,"request.json"),Json.Write(new ManagerUpdateRequest{Target=target,Staged=Path.Combine(dir,"manager-new.exe"),Release=release}));
   if(success!=null)File.WriteAllText(Path.Combine(dir,"request.json.result"),Json.Write(new ManagerUpdateResult{Success=success.Value,Backup=backup,Message=success.Value?null:"failed"}));
   return dir;
  }
  static void FinishTests(string dir,Action<bool,string> check){
   string root=Path.Combine(dir,"finish"),install=Path.Combine(dir,"install");Directory.CreateDirectory(root);Directory.CreateDirectory(install);
   string target=Path.Combine(install,"AnyAPI Manager.exe"),backup=target+"."+Guid.NewGuid().ToString("N")+".bak",stranger=Path.Combine(install,"notes.bak");
   File.WriteAllText(target,"new");File.WriteAllText(backup,"old");File.WriteAllText(stranger,"keep");
   string done=FakeUpdate(root,target,"1.5.0",true,backup);string note=ManagerUpdater.FinishPrevious(root,new Version(1,5,0,0),DateTime.UtcNow);
   check(note=="Manager updated to 1.5.0."&&!Directory.Exists(done)&&!File.Exists(backup)&&File.Exists(target),"Finished manager update reports itself and removes its folder and EXE backup");
   File.WriteAllText(backup,"old");done=FakeUpdate(root,target,"1.5.0",true,backup);ManagerUpdater.FinishPrevious(root,new Version(1,4,0,0),DateTime.UtcNow);
   check(Directory.Exists(done)&&File.Exists(backup),"An older running manager keeps the update backup");Directory.Delete(done,true);
   string misplaced=FakeUpdate(root,target,"1.5.0",true,stranger);ManagerUpdater.FinishPrevious(root,new Version(1,5,0,0),DateTime.UtcNow);
   check(!Directory.Exists(misplaced)&&File.Exists(stranger),"Update cleanup deletes only the backup the updater created");
   string failed=FakeUpdate(root,target,"1.5.0",false,null),pending=FakeUpdate(root,target,"1.5.0",null,null);
   check(ManagerUpdater.FinishPrevious(root,new Version(1,5,0,0),DateTime.UtcNow)==null&&!Directory.Exists(failed)&&Directory.Exists(pending),"Failed updates are cleared and in-progress updates are left alone");
   ManagerUpdater.FinishPrevious(root,new Version(1,5,0,0),DateTime.UtcNow.AddDays(2));check(!Directory.Exists(pending),"Abandoned update folders are cleared after a day");
  }
 }
}
