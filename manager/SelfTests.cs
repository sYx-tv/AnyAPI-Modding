using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using System.Net;
using System.Net.Http;
using System.Threading;
using System.Threading.Tasks;
namespace AnyApiManager {
 static class SelfTests {
  static List<string> passed=new List<string>();
  static void Check(bool ok,string name){if(!ok)throw new Exception(name);passed.Add(name);}
  static void Reject(Action action,string name){bool rejected=false;try{action();}catch{rejected=true;}Check(rejected,name);}
  static Package Clone(Package p){return Json.Read<Package>(Json.Write(p));}
  sealed class DownloadFixture:HttpMessageHandler {public bool Error,Redirect;protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request,CancellationToken cancel){return Task.FromResult(new HttpResponseMessage(Error?HttpStatusCode.NotFound:HttpStatusCode.OK){Content=new StringContent("catalog"),RequestMessage=Redirect?new HttpRequestMessage(HttpMethod.Get,"http://insecure.example/file"):request});}}
  public static int Run(string output){string root=Path.Combine(Path.GetDirectoryName(Path.GetFullPath(output)),"manager-test-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(root);
   try{
    var catalog=Engine.Bundled();Rules.Validate(catalog);Check(catalog.Mods.Count==4,"Four browsable mods; API resource contains no mods");
    Check(Engine.RepositoryUrl("https://github.com/example/AnyAPI")=="https://raw.githubusercontent.com/example/AnyAPI/main/catalog.json","GitHub repository normalization");
    Reject(()=>Engine.RepositoryUrl("http://github.com/example/AnyAPI"),"HTTP repository rejected");Reject(()=>Engine.RepositoryUrl("https://evil.example/example/AnyAPI"),"Non-GitHub repository rejected");
    using(var client=new HttpClient(new DownloadFixture())){Check(Encoding.UTF8.GetString(Engine.DownloadBytes("https://example.test/file",16,client).GetAwaiter().GetResult())=="catalog","HTTPS download stream consumed");Reject(()=>Engine.DownloadBytes("https://example.test/file",4,client).GetAwaiter().GetResult(),"Oversized download rejected");}
    using(var client=new HttpClient(new DownloadFixture{Error=true}))Reject(()=>Engine.DownloadBytes("https://example.test/file",16,client).GetAwaiter().GetResult(),"Missing release asset surfaces a clean failure");
    using(var client=new HttpClient(new DownloadFixture{Redirect=true}))Reject(()=>Engine.DownloadBytes("https://example.test/file",16,client).GetAwaiter().GetResult(),"Insecure redirected download rejected");
    Reject(()=>Rules.Target(root,"../outside.dll"),"Path traversal rejected");Reject(()=>Rules.Target(root,"C:/outside.dll"),"Absolute paths rejected");
    var bad=Clone(catalog.Mods[0]);bad.FileHashes=new Dictionary<string,string>{{"AnyAPI and Modding/mods/../game.exe",new string('a',64)}};Reject(()=>Rules.Validate(bad,false),"Executable/path injection rejected");
    string exe=Path.Combine(root,"game.exe"),gcl=Path.Combine(root,"bin/game.gcl");Directory.CreateDirectory(Path.GetDirectoryName(gcl));File.WriteAllText(exe,"fixture executable");File.WriteAllText(gcl,"fixture game data");
    var api=Clone(catalog.Api[0]);api.GameBuilds=new List<GameBuild>{new GameBuild{Version="fixture",ExeSha256=Rules.Hash(exe),GclSha256=Rules.Hash(gcl)}};
    string zip=Path.Combine(root,"api.zip");using(var stream=System.Reflection.Assembly.GetExecutingAssembly().GetManifestResourceStream("api.zip"))using(var file=File.Create(zip))stream.CopyTo(file);
    Check(Engine.ReadPackage(api,zip).Keys.Single()=="dinput8.dll","Bundled API ZIP contains only host DLL");
    var wrong=Clone(api);wrong.Sha256=new string('0',64);Reject(()=>Engine.ReadPackage(wrong,zip),"Bad download checksum rejected before install");
    string evil=Path.Combine(root,"evil.zip");using(var archive=ZipFile.Open(evil,ZipArchiveMode.Create)){using(var writer=new StreamWriter(archive.CreateEntry("../evil.dll").Open()))writer.Write("MZbad");}wrong=Clone(api);wrong.Sha256=Rules.Hash(evil);Reject(()=>Engine.ReadPackage(wrong,evil),"Unexpected ZIP path rejected");
    string settings=Rules.Target(root,"AnyAPI and Modding/mods/AnyMap/markers.tsv");Directory.CreateDirectory(Path.GetDirectoryName(settings));File.WriteAllText(settings,"user markers");
    var local=new Catalog{Api=new List<Package>{api},Mods=catalog.Mods};var request=new ApplyRequest{GamePath=root,Action="install",Package=api,Catalog=local,ZipPath=zip};
    Check(Engine.Apply(request,null,true).Success,"API install with matching game fingerprints");Check(Rules.Hash(Path.Combine(root,"dinput8.dll"))==api.FileHashes["dinput8.dll"],"Installed API bytes match release");
    var unsupported=Clone(api);unsupported.GameBuilds[0].GclSha256=new string('0',64);request.Package=unsupported;Check(!Engine.Apply(request,null,true).Success,"Unverified game update blocks installation");request.Package=api;
    string oldState=File.ReadAllText(Engine.StateFile(root));var changed=Clone(api);changed.Version="0.25.1";string changedZip=Path.Combine(root,"changed.zip");var changedBytes=Engine.ReadPackage(api,zip)["dinput8.dll"];changedBytes[changedBytes.Length-1]^=1;
    using(var hash=System.Security.Cryptography.SHA256.Create())changed.FileHashes["dinput8.dll"]=Rules.Hex(hash.ComputeHash(changedBytes));using(var archive=ZipFile.Open(changedZip,ZipArchiveMode.Create))using(var s=archive.CreateEntry("dinput8.dll").Open())s.Write(changedBytes,0,changedBytes.Length);changed.Sha256=Rules.Hash(changedZip);request.Package=changed;request.ZipPath=changedZip;
    Check(!Engine.Apply(request,i=>{if(i==2)throw new IOException("Injected commit failure");},true).Success,"Injected installation failure surfaced");Check(File.ReadAllText(Engine.StateFile(root))==oldState&&Rules.Hash(Path.Combine(root,"dinput8.dll"))==api.FileHashes["dinput8.dll"],"Failure rolls back changed DLL and changed receipts");request.Package=api;request.ZipPath=zip;
    File.WriteAllText(Path.Combine(root,"dinput8.dll"),"foreign proxy");Check(!Engine.Apply(request,null,true).Success&&File.ReadAllText(Path.Combine(root,"dinput8.dll"))=="foreign proxy","Foreign proxy is preserved without explicit approval");request.ReplaceUnknown=true;Check(Engine.Apply(request,null,true).Success,"Explicit replacement keeps a backup");
    // Use the real x64 host bytes as a DLL fixture without loading it.
    var mod=Clone(catalog.Mods[0]);mod.GameBuilds=api.GameBuilds;string modZip=Path.Combine(root,"mod.zip");var dllBytes=Engine.ReadPackage(api,zip)["dinput8.dll"];string relative=Rules.OnlyFile(mod);
    using(var archive=ZipFile.Open(modZip,ZipArchiveMode.Create))using(var s=archive.CreateEntry(relative).Open())s.Write(dllBytes,0,dllBytes.Length);
    mod.Sha256=Rules.Hash(modZip);using(var hash=System.Security.Cryptography.SHA256.Create())mod.FileHashes[relative]=Rules.Hex(hash.ComputeHash(dllBytes));request.Package=mod;request.ZipPath=modZip;request.ReplaceUnknown=false;
    mod.MinimumApi=999;Check(!Engine.Apply(request,null,true).Success,"Minimum API revision enforced");mod.MinimumApi=25;
    Check(Engine.Apply(request,null,true).Success,"Mod installs independently of API");request.Action="disable";Check(Engine.Apply(request,null,true).Success&&!File.Exists(Rules.Target(root,relative))&&File.Exists(Rules.Target(root,relative+".disabled")),"Disable removes DLL from loader discovery");
    request.Action="enable";Check(Engine.Apply(request,null,true).Success&&File.Exists(Rules.Target(root,relative)),"Enable restores DLL");
    File.WriteAllText(gcl,"updated fixture game");var newer=Clone(api);newer.Version="0.26.0";newer.Revision=26;newer.GameBuilds[0].GclSha256=Rules.Hash(gcl);local.Api=new List<Package>{newer};request.Package=newer;request.Action="install";request.ZipPath=zip;
    Check(!Engine.Apply(request,null,true).Success&&File.Exists(Rules.Target(root,relative)),"Game update requires approval before disabling stale mods");request.DisableIncompatible=true;Check(Engine.Apply(request,null,true).Success&&File.Exists(Rules.Target(root,relative+".disabled")),"API update temporarily disables unverified mods");
    var downgrade=Clone(newer);downgrade.Version="0.25.0";downgrade.Revision=25;request.Package=downgrade;Check(!Engine.Apply(request,null,true).Success,"Newer installed API cannot downgrade");
    request.Package=mod;request.Action="enable";Check(!Engine.Apply(request,null,true).Success,"Incompatible disabled mod cannot be enabled");
    mod=Clone(mod);mod.GameBuilds=newer.GameBuilds;mod.Version="0.26.0";mod.Revision=26;request.Package=mod;request.Action="install";request.ZipPath=modZip;Check(Engine.Apply(request,null,true).Success&&!File.Exists(Rules.Target(root,relative+".disabled")),"Compatible mod update restores its enabled state");
    request.Action="remove";Check(Engine.Apply(request,null,true).Success&&!File.Exists(Rules.Target(root,relative)),"Remove deletes only managed DLL");Check(File.ReadAllText(settings)=="user markers","User settings and markers survive every action");
    Check(Directory.GetDirectories(Rules.Target(root,"AnyAPI and Modding/.manager/backups")).Length>=5,"Every mutation retains backups");
    File.WriteAllText(output,Json.Write(new{Success=true,Passed=passed.Count,Checks=passed}));return 0;
   }catch(Exception e){File.WriteAllText(output,Json.Write(new{Success=false,Error=e.ToString(),Passed=passed}));return 1;}
   finally{if(Directory.Exists(root))Directory.Delete(root,true);}
  }
 }
}
