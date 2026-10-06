using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace AnyApiManager {
 public sealed class LocalMetadata {public string Name,Version,Description;public int MinimumApi;}
 public static partial class Engine {
  const string ModFolder="AnyAPI and Modding/mods/";
  public static void ValidateLocal(Package p){
   if(p==null||!p.Local||p.FileHashes==null||p.FileHashes.Count!=1||p.MinimumApi<0||p.MinimumApi>100000||string.IsNullOrWhiteSpace(p.Name)||p.Name.Length>100||p.Description==null||p.Description.Length>1000||p.Version==null||p.Version.Length>64)throw new InvalidDataException("Invalid local mod details.");
   string path=Rules.OnlyFile(p),name=Path.GetFileName(path);if(path!=ModFolder+name||name.Length>120||!name.EndsWith(".dll",StringComparison.Ordinal)||name.IndexOfAny(Path.GetInvalidFileNameChars())>=0||!Rules.Digest(p.FileHashes[path]))throw new InvalidDataException("Choose a DLL with a valid filename and lowercase .dll extension.");
   Rules.Target(Data,path);if(p.Id!=LocalId(path))throw new InvalidDataException("Local mod identity mismatch.");
  }
  static string LocalId(string path){using(var h=System.Security.Cryptography.SHA256.Create())return "local-"+Rules.Hex(h.ComputeHash(Encoding.UTF8.GetBytes(path.ToLowerInvariant()))).Substring(0,24);}
  public static Package ImportDetails(string source){
   if(new FileInfo(source).Length>67108864)throw new InvalidDataException("Mod DLL exceeds 64 MB.");ValidateModDll(File.ReadAllBytes(source));
   string name=Path.GetFileNameWithoutExtension(source)+".dll",path=ModFolder+name;var p=new Package{Local=true,Id=LocalId(path),Name=Path.GetFileNameWithoutExtension(name),Version="Unknown",Description="Locally installed AnyAPI mod.",FileHashes=new Dictionary<string,string>{{path,Rules.Hash(source)}}};
   string metadata=Path.ChangeExtension(source,".anymod.json");if(File.Exists(metadata)){if(new FileInfo(metadata).Length>16384)throw new InvalidDataException("Local mod details exceed 16 KB.");var m=Json.Read<LocalMetadata>(File.ReadAllText(metadata));if(m==null)throw new InvalidDataException("Invalid mod details.");if(!string.IsNullOrWhiteSpace(m.Name))p.Name=m.Name;if(!string.IsNullOrWhiteSpace(m.Version))p.Version=m.Version;if(m.Description!=null)p.Description=m.Description;p.MinimumApi=m.MinimumApi;}
   ValidateLocal(p);return p;
  }
  static Package DescribeLocal(string game,string file,Installed state){
   string active=file.EndsWith(".disabled",StringComparison.OrdinalIgnoreCase)?file.Substring(0,file.Length-9):file,path=ModFolder+Path.GetFileName(active);Receipt receipt;
   Package p;if(state.Packages.TryGetValue(LocalId(path),out receipt)&&receipt.Package!=null&&receipt.Package.Local)p=Json.Read<Package>(Json.Write(receipt.Package));else p=new Package{Local=true,Id=LocalId(path),Name=Path.GetFileNameWithoutExtension(active),Version="Unknown",Description="Locally installed AnyAPI mod."};
   if(File.Exists(active)&&File.Exists(Path.ChangeExtension(active,".anymod.json")))try{p=ImportDetails(active);}catch{}
   p.FileHashes=new Dictionary<string,string>{{path,Rules.Hash(file)}};return p;
  }
  public static List<Package> Discover(string game,Catalog catalog){
   var all=new List<Package>(catalog.Mods);var state=ReadInstalled(game);string folder=Rules.Target(game,ModFolder.TrimEnd('/'));if(!Directory.Exists(folder))return all;
   foreach(string file in Directory.GetFiles(folder).Where(f=>f.EndsWith(".dll",StringComparison.OrdinalIgnoreCase)||f.EndsWith(".dll.disabled",StringComparison.OrdinalIgnoreCase))){
    var p=DescribeLocal(game,file,state);string path=Rules.OnlyFile(p),hash=p.FileHashes[path];Package official=state.Packages.Values.Where(r=>r.Package!=null&&!r.Package.Local).Select(r=>r.Package).Concat(catalog.Mods).FirstOrDefault(m=>m.FileHashes.ContainsKey(path)&&m.FileHashes[path]==hash);
    if(official!=null){var listed=all.FirstOrDefault(m=>m.Id==official.Id);Version actualVersion,listedVersion;if(listed==null)all.Add(official);else if(Version.TryParse(official.Version,out actualVersion)&&Version.TryParse(listed.Version,out listedVersion)&&actualVersion>listedVersion){all.Remove(listed);all.Add(official);}}else all.Add(p);
   }return all.OrderBy(p=>p.Name,StringComparer.OrdinalIgnoreCase).ToList();
  }
  public static string LocalProblem(string game,Package p,Catalog c){try{ValidateLocal(p);string file=Rules.Target(game,Rules.OnlyFile(p));if(!File.Exists(file))file+=".disabled";ValidateModDll(ReadLocalBytes(file));if(ApiRevision(game,c)<p.MinimumApi)return "Requires API revision "+p.MinimumApi;return null;}catch(Exception e){return e.Message;}}
  static byte[] ReadLocalBytes(string path){if(new FileInfo(path).Length>67108864)throw new InvalidDataException("Mod DLL exceeds 64 MB.");return File.ReadAllBytes(path);}
  // Inspect the PE export table without loading or executing a third-party DLL.
  public static void ValidateModDll(byte[] b){
   try{
    if(b.Length<64||b.Length>67108864||b[0]!='M'||b[1]!='Z')throw new InvalidDataException();int pe=BitConverter.ToInt32(b,60);
    if(pe<64||pe>b.Length-264||BitConverter.ToUInt32(b,pe)!=0x4550||BitConverter.ToUInt16(b,pe+4)!=0x8664||(BitConverter.ToUInt16(b,pe+22)&0x2000)==0||BitConverter.ToUInt16(b,pe+24)!=0x20b)throw new InvalidDataException();
    int count=BitConverter.ToUInt16(b,pe+6),sections=pe+24+BitConverter.ToUInt16(b,pe+20);if(count<1||count>96||sections>b.Length-count*40)throw new InvalidDataException();
    Func<uint,int> offset=rva=>{for(int i=0;i<count;i++){int s=sections+i*40;uint va=BitConverter.ToUInt32(b,s+12),size=BitConverter.ToUInt32(b,s+16),raw=BitConverter.ToUInt32(b,s+20);if(rva>=va&&(ulong)rva-va<size){ulong o=(ulong)raw+rva-va;if(o>=(ulong)b.Length)throw new InvalidDataException();return (int)o;}}throw new InvalidDataException();};
    int export=offset(BitConverter.ToUInt32(b,pe+24+112));if(export>b.Length-40)throw new InvalidDataException();uint n=BitConverter.ToUInt32(b,export+24);if(n>65536)throw new InvalidDataException();int names=offset(BitConverter.ToUInt32(b,export+32));if((ulong)names+(ulong)n*4>(ulong)b.Length)throw new InvalidDataException();
    for(int i=0;i<n;i++){int start=offset(BitConverter.ToUInt32(b,names+i*4)),end=start;while(end<b.Length&&end-start<256&&b[end]!=0)end++;if(end<b.Length&&b[end]==0&&Encoding.ASCII.GetString(b,start,end-start)=="AnyAPI_ModInit")return;}
   }catch(Exception e){if(!(e is InvalidDataException||e is ArgumentException||e is IndexOutOfRangeException))throw;}
   throw new InvalidDataException("This file is not an x64 AnyAPI mod exporting AnyAPI_ModInit.");
  }
  static ApplyResult ApplyLocal(ApplyRequest r,string game,Action<int> fault,bool test){
   var p=r.Package;ValidateLocal(p);string relative=Rules.OnlyFile(p),active=Rules.Target(game,relative),off=Rules.Target(game,relative+".disabled");var state=ReadInstalled(game);var changes=new Dictionary<string,byte[]>();
   if(r.Action=="import-local"){
    byte[] bytes=ReadLocalBytes(r.ZipPath);ValidateModDll(bytes);using(var h=System.Security.Cryptography.SHA256.Create())if(Rules.Hex(h.ComputeHash(bytes))!=p.FileHashes[relative])throw new IOException("The source DLL changed. Import it again.");
    if(ApiRevision(game,r.Catalog)<p.MinimumApi)throw new IOException("Update AnyAPI before importing this mod.");
    if((File.Exists(active)||File.Exists(off))&&!r.ReplaceUnknown)throw new IOException("A mod with this filename already exists. Confirm replacement first.");changes.Add(active,bytes);if(File.Exists(off))changes.Add(off,null);state.Packages[p.Id]=new Receipt{Package=p};
   }else{
    string source=r.Action=="enable-local"?off:File.Exists(active)?active:off;if(!File.Exists(source)||Rules.Hash(source)!=p.FileHashes[relative])throw new IOException("The mod changed. Refresh your mod list first.");
    if(r.Action=="remove-local"){changes.Add(source,null);state.Packages.Remove(p.Id);}else{if(r.Action=="enable-local"){string problem=LocalProblem(game,p,r.Catalog);if(problem!=null)throw new IOException(problem);}string dest=r.Action=="enable-local"?active:off;if(File.Exists(dest))throw new IOException("An enabled and disabled copy both exist. Resolve this conflict first.");changes.Add(dest,File.ReadAllBytes(source));changes.Add(source,null);state.Packages[p.Id]=new Receipt{Package=p,Disabled=r.Action=="disable-local"};}
   }
   changes.Add(StateFile(game),Encoding.UTF8.GetBytes(Json.Write(state)));if(!test&&Running())throw new IOException("Close Anymaker before changing mods.");Commit(game,changes,fault);return new ApplyResult{Success=true,Message=p.Name+": "+r.Action.Replace("-local","")+" complete. Saved settings kept."};
  }
 }
}
