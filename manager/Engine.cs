using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Reflection;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using Microsoft.Win32;

namespace AnyApiManager {
 public static partial class Engine {
  public static readonly string Data=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"AnyAPI Manager");
  static readonly HttpClient Http=CreateHttp();
  static HttpClient CreateHttp(){ConfigureTls();var h=new HttpClient{Timeout=TimeSpan.FromMinutes(3)};h.DefaultRequestHeaders.UserAgent.ParseAdd("AnyAPI-Manager/"+ManagerUpdates.DisplayVersion);h.DefaultRequestHeaders.CacheControl=new System.Net.Http.Headers.CacheControlHeaderValue{NoCache=true};return h;}
  // .NET 4.7+ lets Windows pick TLS 1.2/1.3. Older runtimes (early Windows 10 builds) start at TLS 1.0, which GitHub refuses, so ask for 1.2 explicitly there.
  static void ConfigureTls(){int release=ManagerLog.NetFrameworkRelease();try{ServicePointManager.SecurityProtocol=release>=460798?(SecurityProtocolType)0:(SecurityProtocolType)(3072|768);}catch(Exception e){ManagerLog.Error("TLS setup",e);try{ServicePointManager.SecurityProtocol=(SecurityProtocolType)(3072|768);}catch{}}}
  public static string Resource(string name){using(var s=Assembly.GetExecutingAssembly().GetManifestResourceStream(name))using(var r=new StreamReader(s))return r.ReadToEnd();}
  public static Catalog Bundled(){var c=Json.Read<Catalog>(Resource("catalog.json"));Rules.Validate(c);return c;}
  public static string RepositoryUrl(string value){
   value=value.Trim().TrimEnd('/');Uri u;
   if(Uri.TryCreate(value,UriKind.Absolute,out u)){
    if(u.Scheme!="https"||u.Host!="github.com"||!string.IsNullOrEmpty(u.UserInfo)||u.Query!=""||u.Fragment!="")throw new ArgumentException("Enter a GitHub repository URL or owner/repository.");value=u.AbsolutePath.Trim('/');
   }
   if(!Regex.IsMatch(value,@"^[A-Za-z0-9_-]+/[A-Za-z0-9_.-]+$"))throw new ArgumentException("Use github.com/your-name/AnyAPI or your-name/AnyAPI.");
   if(value.EndsWith(".git",StringComparison.OrdinalIgnoreCase))value=value.Substring(0,value.Length-4);
   return "https://raw.githubusercontent.com/"+value+"/main/catalog.json";
  }
  public static async Task<Catalog> Fetch(string repository){
   byte[] bytes=await DownloadBytes(RepositoryUrl(repository),2097152);var c=Json.Read<Catalog>(Encoding.UTF8.GetString(bytes));Rules.Validate(c);return c;
  }
  public static async Task<byte[]> DownloadBytes(string url,int limit,HttpClient transport=null,Action<long,long> progress=null){
   Uri u;if(!Uri.TryCreate(url,UriKind.Absolute,out u)||u.Scheme!="https")throw new InvalidDataException("Downloads require HTTPS.");
   using(var response=await (transport??Http).GetAsync(u,HttpCompletionOption.ResponseHeadersRead)){
    if(response.StatusCode==HttpStatusCode.NotFound){
     throw new IOException(url.EndsWith("/catalog.json",StringComparison.OrdinalIgnoreCase)?"The mod library is temporarily unavailable. Check for updates again shortly.":"The release download is missing. Check for updates and try again.");
    }
    response.EnsureSuccessStatusCode();if(response.RequestMessage.RequestUri.Scheme!="https")throw new InvalidDataException("Download redirected away from HTTPS.");
    if(response.Content.Headers.ContentLength>limit)throw new InvalidDataException("Download exceeds the package limit.");
    using(var input=await response.Content.ReadAsStreamAsync())using(var output=new MemoryStream()){
     byte[] block=new byte[65536];int n;long total=response.Content.Headers.ContentLength??-1;while((n=await input.ReadAsync(block,0,block.Length))>0){if(output.Length+n>limit)throw new InvalidDataException("Download exceeds the package limit.");await output.WriteAsync(block,0,n);if(progress!=null)progress(output.Length,total);}return output.ToArray();
    }
   }
  }
  public static async Task<string> Acquire(Package p,bool embedded){
   Directory.CreateDirectory(Data);string path=Path.Combine(Data,Guid.NewGuid().ToString("N")+".zip");
   try{
    if(embedded){using(var s=Assembly.GetExecutingAssembly().GetManifestResourceStream("api.zip"))using(var o=File.Create(path))s.CopyTo(o);}
    else{if(string.IsNullOrEmpty(p.Url))throw new InvalidOperationException("This mod has no published download yet. Check for updates later.");File.WriteAllBytes(path,await DownloadBytes(p.Url,67108864));}
    if(!string.Equals(Rules.Hash(path),p.Sha256,StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("Download checksum did not match the release. No game files were changed.");return path;
   }catch{if(File.Exists(path))File.Delete(path);throw;}
  }
  public static Preferences UseBundledCatalog(Preferences p){if(p==null)p=new Preferences();p.Repository=Bundled().Repository;return p;}
  public static Preferences LoadPreferences(){Preferences p;try{p=Json.Read<Preferences>(File.ReadAllText(Path.Combine(Data,"settings.json")));}catch{p=new Preferences();}return UseBundledCatalog(p);}
  public static Catalog Cached(string repository){try{var cache=Json.Read<CatalogCache>(File.ReadAllText(Path.Combine(Data,"catalog-cache.json")));if(RepositoryUrl(cache.Repository)!=RepositoryUrl(repository))return null;Rules.Validate(cache.Catalog);return cache.Catalog;}catch{return null;}}
  public static void Cache(string repository,Catalog catalog){Directory.CreateDirectory(Data);File.WriteAllText(Path.Combine(Data,"catalog-cache.json"),Json.Write(new CatalogCache{Repository=repository,Catalog=catalog}));}
  public static void SavePreferences(Preferences p){Directory.CreateDirectory(Data);File.WriteAllText(Path.Combine(Data,"settings.json"),Json.Write(p));}
  public static string DetectGame(){
   string nearby=AppDomain.CurrentDomain.BaseDirectory;if(File.Exists(Path.Combine(nearby,"game.exe"))&&File.Exists(Path.Combine(nearby,"bin/game.gcl")))return nearby;
   var roots=new HashSet<string>(StringComparer.OrdinalIgnoreCase);string steam=null;try{steam=Registry.GetValue(@"HKEY_CURRENT_USER\Software\Valve\Steam","SteamPath",null) as string;}catch(System.Security.SecurityException){}catch(UnauthorizedAccessException){}
   if(steam!=null)roots.Add(steam);roots.Add(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),"Steam"));
   foreach(var root in roots.ToArray()){
    string file=Path.Combine(root,"steamapps/libraryfolders.vdf");if(!File.Exists(file))continue;
    foreach(Match m in Regex.Matches(File.ReadAllText(file),"\"path\"\\s+\"([^\"]+)\""))roots.Add(m.Groups[1].Value.Replace("\\\\","\\"));
   }
   foreach(string root in roots){string path=Path.Combine(root,"steamapps/common/Anymaker");if(File.Exists(Path.Combine(path,"game.exe"))&&File.Exists(Path.Combine(path,"bin/game.gcl"))){ManagerLog.Write("Detected game folder "+path);return path;}}
   ManagerLog.Write("No Anymaker folder found in Steam libraries: "+string.Join("; ",roots));return "";
  }
  public static bool Running(){foreach(var p in Process.GetProcessesByName("game"))using(p){return true;}return false;}
  public static string StateFile(string game){return Rules.Target(game,"AnyAPI and Modding/.manager/installed.json");}
  public static Installed ReadInstalled(string game){string path=StateFile(game);return File.Exists(path)?Json.Read<Installed>(File.ReadAllText(path)):new Installed();}
  public static string PausedApi(string game){return Rules.Target(game,"AnyAPI and Modding/.manager/paused-dinput8.dll");}
  public static bool ApiPaused(string game){return File.Exists(PausedApi(game))&&!File.Exists(Rules.Target(game,"dinput8.dll"));}
  public static Package InstalledApi(string game,Catalog c){
   string path=Rules.Target(game,"dinput8.dll");if(!File.Exists(path))path=PausedApi(game);if(!File.Exists(path))return null;string hash=Rules.Hash(path);
   var state=ReadInstalled(game);Receipt receipt;if(state.Packages.TryGetValue("anyapi",out receipt)&&receipt.Package!=null&&receipt.Package.FileHashes.ContainsKey("dinput8.dll")&&hash==receipt.Package.FileHashes["dinput8.dll"])return receipt.Package;
   return c.Api.Concat(Bundled().Api).FirstOrDefault(p=>hash==p.FileHashes["dinput8.dll"]);
  }
  public static int ApiRevision(string game,Catalog c){
   var known=InstalledApi(game,c);return known==null?0:known.Revision;
  }
  public static Package MatchingInstalledApi(string game,Catalog c,string exe,string gcl){
   var known=InstalledApi(game,c);return known!=null&&Rules.Matches(known,exe,gcl)?known:null;
  }
  public static string Status(string game,Package p){
   string target=Rules.Target(game,Rules.OnlyFile(p));
   if(File.Exists(target))return Rules.Hash(target)==p.FileHashes[Rules.OnlyFile(p)]?"Installed":"Update / different build";
   if(File.Exists(target+".disabled"))return "Disabled";return "Not installed";
  }
  public static Dictionary<string,Package> Incompatible(string game,Catalog catalog){
   var found=new Dictionary<string,Package>();string folder=Rules.Target(game,"AnyAPI and Modding/mods");if(!Directory.Exists(folder))return found;
   string exe=Rules.Hash(Rules.Target(game,"game.exe")),gcl=Rules.Hash(Rules.Target(game,"bin/game.gcl"));var state=ReadInstalled(game);int api=ApiRevision(game,catalog);
   foreach(var file in Directory.GetFiles(folder,"*.dll").Where(f=>Path.GetExtension(f).Equals(".dll",StringComparison.OrdinalIgnoreCase))){
    string relative="AnyAPI and Modding/mods/"+Path.GetFileName(file);Rules.Target(game,relative);string hash=Rules.Hash(file);
    Package known=state.Packages.Values.Where(r=>r.Package!=null).Select(r=>r.Package).Concat(catalog.Mods).Concat(Bundled().Mods).FirstOrDefault(p=>p.FileHashes.ContainsKey(relative)&&p.FileHashes[relative]==hash);
    if(known==null||known.Local){var local=DescribeLocal(game,file,state);if(LocalProblem(game,local,catalog)!=null)found.Add(relative,local);}
    else if(!Rules.Matches(known,exe,gcl)||known.MinimumApi>api)found.Add(relative,known);
   }return found;
  }
  // Extract only explicitly declared DLLs, rejecting extra entries and traversal.
  public static Dictionary<string,byte[]> ReadPackage(Package p,string zip){
   if(new FileInfo(zip).Length>67108864||Rules.Hash(zip)!=p.Sha256)throw new InvalidDataException("Package checksum mismatch.");
   var result=new Dictionary<string,byte[]>(StringComparer.OrdinalIgnoreCase);
   using(var archive=ZipFile.OpenRead(zip)){
    if(archive.Entries.Count!=p.FileHashes.Count)throw new InvalidDataException("Unexpected package contents.");
    foreach(var entry in archive.Entries){string expected;
     if(!p.FileHashes.TryGetValue(entry.FullName,out expected)||result.ContainsKey(entry.FullName)||entry.Length<1||entry.Length>67108864)throw new InvalidDataException("Unexpected package file.");
     using(var input=entry.Open())using(var output=new MemoryStream()){
      byte[] block=new byte[65536];int n;while((n=input.Read(block,0,block.Length))>0){if(output.Length+n>67108864)throw new InvalidDataException("Expanded file is too large.");output.Write(block,0,n);}
      byte[] bytes=output.ToArray();using(var hash=System.Security.Cryptography.SHA256.Create())if(Rules.Hex(hash.ComputeHash(bytes))!=expected)throw new InvalidDataException("File checksum mismatch.");
      if(bytes.Length<64||bytes[0]!='M'||bytes[1]!='Z')throw new InvalidDataException("Package file is not a Windows DLL.");int pe=BitConverter.ToInt32(bytes,60);if(pe<64||pe>bytes.Length-26||BitConverter.ToUInt32(bytes,pe)!=0x4550||BitConverter.ToUInt16(bytes,pe+4)!=0x8664||(BitConverter.ToUInt16(bytes,pe+22)&0x2000)==0||BitConverter.ToUInt16(bytes,pe+24)!=0x20b)throw new InvalidDataException("Package requires an x64 DLL.");result.Add(entry.FullName,bytes);
     }
    }
   }return result;
  }
  public static ApplyResult Apply(ApplyRequest r,Action<int> failAfter=null,bool test=false){
   try{string key;using(var hash=System.Security.Cryptography.SHA256.Create())key=Rules.Hex(hash.ComputeHash(Encoding.UTF8.GetBytes(Path.GetFullPath(r.GamePath).ToLowerInvariant())));
    using(var gate=new System.Threading.Mutex(false,"Local\\AnyAPI.Manager."+key)){bool owned=false;try{try{owned=gate.WaitOne(0);}catch(System.Threading.AbandonedMutexException){owned=true;}if(!owned)return new ApplyResult{Message="Another manager operation is running."};return ApplyCore(r,failAfter,test);}finally{if(owned)gate.ReleaseMutex();}}
   }catch(Exception e){ManagerLog.Error("Apply "+r.Action,e);return new ApplyResult{Message=e.Message};}
  }
  static ApplyResult ApplyCore(ApplyRequest r,Action<int> failAfter,bool test){
   try{
    Rules.Validate(r.Catalog);
    if(!new[]{"install","disable","enable","remove","prepare-modded","prepare-vanilla","import-local","disable-local","enable-local","remove-local"}.Contains(r.Action))throw new InvalidDataException("Unknown manager action.");
    string game=Path.GetFullPath(r.GamePath);Rules.Target(game,"game.exe");Rules.Target(game,"bin/game.gcl");
    if(!File.Exists(Path.Combine(game,"game.exe"))||!File.Exists(Path.Combine(game,"bin/game.gcl")))throw new IOException("Choose the Anymaker folder containing game.exe.");
    if(!test&&Running())throw new IOException("Close Anymaker before changing DLLs.");
    if(r.Action.StartsWith("prepare-",StringComparison.Ordinal))return PrepareLaunch(r,game,failAfter,test);
    if(r.Action.EndsWith("-local",StringComparison.Ordinal))return ApplyLocal(r,game,failAfter,test);
    Rules.Validate(r.Package,r.Package!=null&&r.Package.Id=="anyapi");
    var p=r.Package;string relative=Rules.OnlyFile(p),target=Rules.Target(game,relative),disabled=Rules.Target(game,relative+".disabled");
    var installed=ReadInstalled(game);Receipt previous;installed.Packages.TryGetValue(p.Id,out previous);
    var changes=new Dictionary<string,byte[]>();
    if(r.Action=="install"){
     if(!Rules.Matches(p,Rules.Hash(Path.Combine(game,"game.exe")),Rules.Hash(Path.Combine(game,"bin/game.gcl"))))throw new IOException("This release has not been verified for your installed game build. Check for updates.");
     if(p.Id!="anyapi"&&ApiRevision(game,r.Catalog)<p.MinimumApi)throw new IOException("Install or update AnyAPI first (revision "+p.MinimumApi+" or newer).");
     if(previous!=null&&new Version(previous.Package.Version)>new Version(p.Version))throw new IOException("A newer version is already installed.");var files=ReadPackage(p,r.ZipPath);
     foreach(var f in files){string dest=Rules.Target(game,f.Key);
      if(File.Exists(dest)&&Rules.Hash(dest)!=p.FileHashes[f.Key]&&(previous==null||!previous.Package.FileHashes.ContainsKey(f.Key)||Rules.Hash(dest)!=previous.Package.FileHashes[f.Key])&&!r.ReplaceUnknown)throw new IOException("An unmanaged or changed DLL exists. Back up and explicitly approve replacement first.");
      changes.Add(dest,f.Value);
     }
     if(File.Exists(disabled)){
      if(previous==null||Rules.Hash(disabled)!=previous.Package.FileHashes[relative])throw new IOException("An unmanaged disabled DLL exists. Enable it or resolve it first.");changes.Add(disabled,null);
     }
     installed.Packages[p.Id]=new Receipt{Package=p,Disabled=false};
     if(p.Id=="anyapi"&&File.Exists(PausedApi(game))){
      var known=InstalledApi(game,r.Catalog);if(known==null||Rules.Hash(PausedApi(game))!=known.FileHashes["dinput8.dll"])throw new IOException("The paused API changed outside the manager. Resolve it before installing.");changes.Add(PausedApi(game),null);
     }
     if(p.Id=="anyapi")foreach(var incompatible in Incompatible(game,r.Catalog)){
      if(!r.DisableIncompatible)throw new IOException("Some installed mods are not verified for this game build. Approve temporarily disabling them before updating the API.");
      string active=Rules.Target(game,incompatible.Key),off=Rules.Target(game,incompatible.Key+".disabled");if(File.Exists(off))throw new IOException("A disabled copy already exists: "+Path.GetFileName(off));changes.Add(off,File.ReadAllBytes(active));changes.Add(active,null);
      if(incompatible.Value!=null)installed.Packages[incompatible.Value.Id]=new Receipt{Package=incompatible.Value,Disabled=true};
     }
    }else{
     if(p.Id=="anyapi")throw new IOException("API removal is not a mod toggle.");
     string source=r.Action=="enable"?disabled:File.Exists(target)?target:disabled;
     if(!File.Exists(source))throw new IOException("That mod is not installed.");
     string digest=Rules.Hash(source);Package known=previous==null?p:previous.Package;
     if(!known.FileHashes.ContainsKey(relative)||digest!=known.FileHashes[relative])throw new IOException("The DLL changed outside the manager. Refresh the catalog before changing it.");
     if(r.Action=="enable"&&(!Rules.Matches(known,Rules.Hash(Rules.Target(game,"game.exe")),Rules.Hash(Rules.Target(game,"bin/game.gcl")))||ApiRevision(game,r.Catalog)<known.MinimumApi))throw new IOException("Update this mod for your game build before enabling it.");
     if(r.Action=="remove"){changes.Add(source,null);installed.Packages.Remove(p.Id);}
     else{string dest=r.Action=="enable"?target:disabled;if(File.Exists(dest))throw new IOException("The destination DLL already exists.");changes.Add(dest,File.ReadAllBytes(source));changes.Add(source,null);installed.Packages[p.Id]=new Receipt{Package=known,Disabled=r.Action=="disable"};}
    }
    changes.Add(StateFile(game),Encoding.UTF8.GetBytes(Json.Write(installed)));
    if(!test&&Running())throw new IOException("Anymaker started during preparation. Close it and try again.");
    Commit(game,changes,failAfter);ManagerLog.Write(r.Action+" "+p.Id+" "+p.Version+" in "+game);return new ApplyResult{Success=true,Message=p.Name+": "+(r.Action=="install"?"installed":r.Action=="remove"?"removed (settings kept)":r.Action=="enable"?"enabled":"disabled")+". Ready for the next game launch."};
   }catch(Exception e){ManagerLog.Error(r.Action+(r.Package==null?"":" "+r.Package.Id),e);return new ApplyResult{Success=false,Message=e.Message};}
  }
  static ApplyResult PrepareLaunch(ApplyRequest r,string game,Action<int> fault,bool test){
   string active=Rules.Target(game,"dinput8.dll"),paused=PausedApi(game);bool modded=r.Action=="prepare-modded";
   if(File.Exists(active)&&File.Exists(paused))throw new IOException("Both active and paused API files exist. Resolve the conflict before launching.");
   if(!File.Exists(active)&&!File.Exists(paused)){
    if(modded)throw new IOException("Install AnyAPI before playing with mods.");return new ApplyResult{Success=true,Message="Ready to play without mods."};
   }
   var known=InstalledApi(game,r.Catalog);if(known==null)throw new IOException("This dinput8.dll is not a verified AnyAPI file. It was left unchanged.");
   if(modded){
    if(!Rules.Matches(known,Rules.Hash(Rules.Target(game,"game.exe")),Rules.Hash(Rules.Target(game,"bin/game.gcl"))))throw new IOException("Update AnyAPI for this game build before playing with mods.");
    if(Incompatible(game,r.Catalog).Count>0)throw new IOException("Update or disable incompatible mods before playing with mods.");
   }
   bool moving=modded?File.Exists(paused):File.Exists(active);
   if(moving){
    var changes=new Dictionary<string,byte[]>();string source=modded?paused:active,dest=modded?active:paused;changes.Add(dest,File.ReadAllBytes(source));changes.Add(source,null);
    var installed=ReadInstalled(game);installed.Packages["anyapi"]=new Receipt{Package=known,Disabled=!modded};changes.Add(StateFile(game),Encoding.UTF8.GetBytes(Json.Write(installed)));
    if(!test&&Running())throw new IOException("Anymaker started during preparation. Close it and try again.");Commit(game,changes,fault);
   }
   return new ApplyResult{Success=true,Message=modded?"Ready to play with mods.":"AnyAPI paused. Play with mods will restore it."};
  }
  // Back up every changed file and restore the entire transaction on failure.
  static void Commit(string game,Dictionary<string,byte[]> changes,Action<int> fault){
   string backup=Rules.Target(game,"AnyAPI and Modding/.manager/backups/"+DateTime.UtcNow.ToString("yyyyMMdd-HHmmss")+"-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(backup);
   var old=new Dictionary<string,string>();int i=0;
   foreach(var c in changes){string b=Path.Combine(backup,(i++)+".bak");if(File.Exists(c.Key)){File.Copy(c.Key,b);old[c.Key]=b;}else old[c.Key]=null;}
   File.WriteAllText(Path.Combine(backup,"files.json"),Json.Write(old));
   var touched=new List<string>();
   try{i=0;foreach(var c in changes){Directory.CreateDirectory(Path.GetDirectoryName(c.Key));touched.Add(c.Key);
     if(c.Value==null){if(File.Exists(c.Key))File.Delete(c.Key);}
     else{string temp=c.Key+"."+Guid.NewGuid().ToString("N")+".new";try{File.WriteAllBytes(temp,c.Value);if(File.Exists(c.Key))File.Replace(temp,c.Key,null);else File.Move(temp,c.Key);}finally{if(File.Exists(temp))File.Delete(temp);}}
     if(fault!=null)fault(++i);
    }
   }catch{var errors=new List<string>();foreach(var path in touched.AsEnumerable().Reverse())try{if(old[path]==null){if(File.Exists(path))File.Delete(path);}else File.Copy(old[path],path,true);}catch(Exception e){errors.Add(e.Message);}
    if(errors.Count>0)throw new IOException("Restore needs attention. Backups: "+backup+". "+string.Join("; ",errors));throw;
   }
  }
  public static async Task<ApplyResult> Execute(ApplyRequest request){
   // Probe permissions before staging the operation. UAC is needed only for writes.
   string path=Rules.Target(request.GamePath,"AnyAPI and Modding/.manager/access-test-"+Guid.NewGuid().ToString("N"));bool elevation=false;
   try{Directory.CreateDirectory(Path.GetDirectoryName(path));File.WriteAllText(path,"");File.Delete(path);}catch(UnauthorizedAccessException e){elevation=true;ManagerLog.Write("Game folder needs administrator rights ("+e.Message+"); asking Windows to elevate "+request.Action);}
   if(!elevation)return await Task.Run(()=>Apply(request));
   Directory.CreateDirectory(Data);string req=Path.Combine(Data,Guid.NewGuid().ToString("N")+".request.json");File.WriteAllText(req,Json.Write(request));
   try{var info=new ProcessStartInfo(Assembly.GetExecutingAssembly().Location,"--apply \""+req+"\""){UseShellExecute=true,Verb="runas",WindowStyle=ProcessWindowStyle.Hidden};using(var p=Process.Start(info)){await Task.Run(()=>p.WaitForExit());}
    if(!File.Exists(req+".result"))throw new IOException("The administrator step closed without finishing. No game files were changed. Details: "+ManagerLog.LogFile);return Json.Read<ApplyResult>(File.ReadAllText(req+".result"));}
   finally{if(File.Exists(req))File.Delete(req);if(File.Exists(req+".result"))File.Delete(req+".result");}
  }
 }
}
