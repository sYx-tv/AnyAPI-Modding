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
  static byte[] ModFixture(){byte[] b=new byte[1024];b[0]=(byte)'M';b[1]=(byte)'Z';Action<int,uint> put=(i,v)=>Array.Copy(BitConverter.GetBytes(v),0,b,i,4);put(60,64);put(64,0x4550);b[68]=0x64;b[69]=0x86;b[70]=1;b[84]=240;b[87]=0x20;b[88]=0x0b;b[89]=2;put(200,0x1000);put(204,40);put(340,0x1000);put(344,512);put(348,512);put(536,1);put(544,0x1040);put(576,0x1060);Array.Copy(Encoding.ASCII.GetBytes("AnyAPI_ModInit"),0,b,608,14);return b;}
  public static int Run(string output){string root=Path.Combine(Path.GetDirectoryName(Path.GetFullPath(output)),"manager-test-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(root);
   try{
    var catalog=Engine.Bundled();Rules.Validate(catalog);Check(catalog.Mods.Count==4,"Four browsable mods; API resource contains no mods");
    var preferences=Engine.UseBundledCatalog(new Preferences{GamePath="saved-game-folder",Repository="https://github.com/stale/private-repo"});Check(preferences.Repository==catalog.Repository&&preferences.GamePath=="saved-game-folder","Built-in catalog overrides stale repository preferences while preserving game folder");
    Check(Engine.UseBundledCatalog(null).Repository==catalog.Repository,"Fresh installations require no repository entry");
    var guide=Guide.Read();Check(guide.Revision==catalog.Api[0].Revision&&guide.HeaderCount==21&&guide.Articles.Count>=70,"Offline guide matches bundled API and includes full public headers");
    Check(guide.Articles.Any(a=>a.Group=="Legacy"&&a.Body.Contains("DISABLED"))&&guide.Articles.Any(a=>a.Title=="Settings and keybinds"),"Guide distinguishes disabled hooks and explicit settings registration");
    string sdk=Path.Combine(root,"starter.zip");Guide.Export(sdk);using(var archive=ZipFile.OpenRead(sdk)){Check(archive.Entries.Any(e=>e.FullName=="src/mod.cpp")&&archive.Entries.Any(e=>e.FullName=="CMakeLists.txt")&&archive.Entries.Count(e=>e.FullName.StartsWith("include/"))==guide.HeaderCount,"Exported SDK contains starter and every public header");Check(!archive.Entries.Any(e=>e.FullName.EndsWith(".dll",StringComparison.OrdinalIgnoreCase)||e.FullName.EndsWith(".exe",StringComparison.OrdinalIgnoreCase)),"Starter SDK contains source only, no bundled mods or executables");}
    Check(Engine.RepositoryUrl("https://github.com/example/AnyAPI")=="https://raw.githubusercontent.com/example/AnyAPI/main/catalog.json","GitHub repository normalization");
    Reject(()=>Engine.RepositoryUrl("http://github.com/example/AnyAPI"),"HTTP repository rejected");Reject(()=>Engine.RepositoryUrl("https://evil.example/example/AnyAPI"),"Non-GitHub repository rejected");
    Check(PrivateGitHub.Endpoint("https://raw.githubusercontent.com/owner/repo/main/catalog.json")=="repos/owner/repo/contents/catalog.json?ref=main","Private catalog resolves to fixed GitHub API host");
    Check(PrivateGitHub.Endpoint("https://github.com/owner/repo/releases/download/v1.0/AnyMap-1.0.zip")=="repos/owner/repo/releases/tags/v1.0","Private release resolves to exact tag and asset");
    Check(PrivateGitHub.Endpoint("https://evil.example/owner/repo/main/catalog.json")==null,"Private sign-in never used for another host");
    Check(PrivateGitHub.Endpoint("https://github.com/owner/repo/releases/download/v1.0/AnyMap.zip?redirect=evil")==null,"Private download rejects query injection");
    Check(PrivateGitHub.Endpoint("http://raw.githubusercontent.com/owner/repo/main/catalog.json")==null,"Private sign-in requires HTTPS");
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
    var launch=new ApplyRequest{GamePath=root,Action="prepare-vanilla",Catalog=local};
    Check(Engine.Apply(launch,null,true).Success&&Engine.ApiPaused(root)&&!File.Exists(Rules.Target(root,"dinput8.dll")),"Vanilla launch removes AnyAPI from the game's loader path");
    Check(File.Exists(Rules.Target(root,relative))&&File.ReadAllText(settings)=="user markers"&&Engine.ApiRevision(root,local)==25,"Vanilla preserves mod selection, settings and installed API identity");
    Check(Engine.Apply(launch,null,true).Success,"Repeated vanilla preparation is idempotent");
    launch.Action="prepare-modded";Check(!Engine.Apply(launch,i=>{if(i==2)throw new IOException("Launch fault");},true).Success&&Engine.ApiPaused(root),"Failed mode switch rolls back to paused API");
    Check(Engine.Apply(launch,null,true).Success&&!Engine.ApiPaused(root)&&File.Exists(Rules.Target(root,"dinput8.dll")),"Modded launch restores the exact API bytes");
    File.WriteAllText(Engine.PausedApi(root),"foreign paused file");Check(!Engine.Apply(launch,null,true).Success,"Conflicting active and paused API files are preserved");File.Delete(Engine.PausedApi(root));
    File.WriteAllText(Rules.Target(root,"dinput8.dll"),"foreign proxy");launch.Action="prepare-vanilla";Check(!Engine.Apply(launch,null,true).Success&&File.ReadAllText(Rules.Target(root,"dinput8.dll"))=="foreign proxy","Launch modes never move an unmanaged proxy");File.WriteAllBytes(Rules.Target(root,"dinput8.dll"),dllBytes);
    File.WriteAllText(gcl,"temporarily newer game");launch.Action="prepare-modded";Check(!Engine.Apply(launch,null,true).Success,"Modded launch blocks an unverified game build");launch.Action="prepare-vanilla";Check(Engine.Apply(launch,null,true).Success,"Vanilla launch remains available after a game update");File.WriteAllText(gcl,"fixture game data");launch.Action="prepare-modded";Check(Engine.Apply(launch,null,true).Success,"Paused API survives and restores after compatibility recovers");
    File.WriteAllText(gcl,"updated fixture game");var newer=Clone(api);newer.Version="0.26.0";newer.Revision=26;newer.GameBuilds[0].GclSha256=Rules.Hash(gcl);local.Api=new List<Package>{newer};request.Package=newer;request.Action="install";request.ZipPath=zip;
    Check(!Engine.Apply(request,null,true).Success&&File.Exists(Rules.Target(root,relative)),"Game update requires approval before disabling stale mods");request.DisableIncompatible=true;Check(Engine.Apply(request,null,true).Success&&File.Exists(Rules.Target(root,relative+".disabled")),"API update temporarily disables unverified mods");
    var downgrade=Clone(newer);downgrade.Version="0.25.0";downgrade.Revision=25;request.Package=downgrade;Check(!Engine.Apply(request,null,true).Success,"Newer installed API cannot downgrade");
    request.Package=mod;request.Action="enable";Check(!Engine.Apply(request,null,true).Success,"Incompatible disabled mod cannot be enabled");
    mod=Clone(mod);mod.GameBuilds=newer.GameBuilds;mod.Version="0.26.0";mod.Revision=26;request.Package=mod;request.Action="install";request.ZipPath=modZip;Check(Engine.Apply(request,null,true).Success&&!File.Exists(Rules.Target(root,relative+".disabled")),"Compatible mod update restores its enabled state");
    request.Action="remove";Check(Engine.Apply(request,null,true).Success&&!File.Exists(Rules.Target(root,relative)),"Remove deletes only managed DLL");Check(File.ReadAllText(settings)=="user markers","User settings and markers survive every action");
    // Synthetic PE fixture is inspected only; it is never loaded or executed.
    byte[] handmade=ModFixture();Engine.ValidateModDll(handmade);Check(true,"Local x64 AnyAPI export recognized without executing DLL");
    var wrongArchitecture=(byte[])handmade.Clone();wrongArchitecture[68]=0x4c;Reject(()=>Engine.ValidateModDll(wrongArchitecture),"32-bit local mod rejected");
    Reject(()=>Engine.ValidateModDll(dllBytes),"A generic host DLL without AnyAPI_ModInit cannot be imported as a mod");
    var corrupt=(byte[])handmade.Clone();Array.Copy(BitConverter.GetBytes(uint.MaxValue),0,corrupt,64+24+112,4);Reject(()=>Engine.ValidateModDll(corrupt),"Malformed local export RVA rejected");
    string homemade=Path.Combine(root,"Handmade.dll");File.WriteAllBytes(homemade,handmade);File.WriteAllText(Path.ChangeExtension(homemade,".anymod.json"),Json.Write(new LocalMetadata{Name="My handmade mod",Version="1.0.0",Description="Personal mod",MinimumApi=26}));
    var localMod=Engine.ImportDetails(homemade);Check(localMod.Name=="My handmade mod"&&localMod.MinimumApi==26,"Optional companion metadata read for local mod");Reject(()=>Rules.Validate(localMod,false),"Local metadata cannot bypass public catalog verification");
    var localRequest=new ApplyRequest{GamePath=root,Catalog=local,Package=localMod,Action="import-local",ZipPath=homemade};
    Check(Engine.Apply(localRequest,null,true).Success,"Local DLL imports without catalog registration");string handmadeTarget=Rules.Target(root,Rules.OnlyFile(localMod));
    Check(Engine.Discover(root,local).Any(p=>p.Local&&p.Name=="My handmade mod"),"Imported local mod appears in installed discovery");
    launch.Package=null;launch.Catalog=local;launch.Action="prepare-modded";Check(Engine.Apply(launch,null,true).Success,"Actual launch request without package accepts valid local mods");
    Check(!Engine.Apply(localRequest,null,true).Success,"Local import cannot silently overwrite an existing mod");
    localRequest.Action="disable-local";Check(Engine.Apply(localRequest,null,true).Success&&!File.Exists(handmadeTarget)&&File.Exists(handmadeTarget+".disabled"),"Local disable removes DLL from loader discovery");
    Check(Engine.Discover(root,local).Count(p=>p.Id==localMod.Id)==1,"Disabled local mod stays visible exactly once");
    localRequest.Action="enable-local";Check(!Engine.Apply(localRequest,i=>{if(i==2)throw new IOException("Local transaction fault");},true).Success&&File.Exists(handmadeTarget+".disabled")&&!File.Exists(handmadeTarget),"Local enable failure rolls back files and receipt");
    Check(Engine.Apply(localRequest,null,true).Success,"Local enable restores DLL");
    string manualTarget=Rules.Target(root,"AnyAPI and Modding/mods/Manual.dll");File.WriteAllBytes(manualTarget,handmade);Check(Engine.Discover(root,local).Any(p=>p.Local&&p.Name=="Manual"),"Manually dropped unregistered DLL discovered");Check(Engine.Incompatible(root,local).Count==0,"Unverified valid local mod is not mislabeled incompatible");File.Delete(manualTarget);
    localRequest.Action="remove-local";Check(Engine.Apply(localRequest,null,true).Success&&!File.Exists(handmadeTarget)&&File.ReadAllText(settings)=="user markers","Local removal keeps saved mod data");
    var demanding=Json.Read<Package>(Json.Write(localMod));demanding.MinimumApi=999;localRequest.Package=demanding;localRequest.Action="import-local";Check(!Engine.Apply(localRequest,null,true).Success,"Declared local minimum API enforced");
    Reject(()=>{var traversal=Json.Read<Package>(Json.Write(localMod));traversal.FileHashes=new Dictionary<string,string>{{"AnyAPI and Modding/mods/../evil.dll",new string('a',64)}};Engine.ValidateLocal(traversal);},"Local import rejects path traversal");
    Check(Directory.GetDirectories(Rules.Target(root,"AnyAPI and Modding/.manager/backups")).Length>=5,"Every mutation retains backups");
    File.WriteAllText(output,Json.Write(new{Success=true,Passed=passed.Count,Checks=passed}));return 0;
   }catch(Exception e){File.WriteAllText(output,Json.Write(new{Success=false,Error=e.ToString(),Passed=passed}));return 1;}
   finally{if(Directory.Exists(root))Directory.Delete(root,true);}
  }
 }
}

