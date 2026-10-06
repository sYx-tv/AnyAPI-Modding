using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;

namespace AnyApiManager {
 public sealed class GameBuild { public string Version; public long SteamBuild; public string ExeSha256; public string GclSha256; }
 public sealed class Package {
  public string Id,Name,Version,Description,Url,Sha256;
  public int Revision,MinimumApi;
  public bool Local;
  public Dictionary<string,string> FileHashes=new Dictionary<string,string>();
  public List<GameBuild> GameBuilds=new List<GameBuild>();
 }
 public sealed class Catalog { public int Schema=1;public string Repository="";public List<Package> Api=new List<Package>();public List<Package> Mods=new List<Package>(); }
 public sealed class Preferences { public string GamePath="",Repository=""; }
 public sealed class CatalogCache { public string Repository; public Catalog Catalog; }
 public sealed class Receipt { public Package Package; public bool Disabled; }
 public sealed class Installed { public Dictionary<string,Receipt> Packages=new Dictionary<string,Receipt>(); }
 public sealed class ApplyRequest { public string GamePath,Action,ZipPath;public Package Package;public Catalog Catalog;public bool ReplaceUnknown,DisableIncompatible; }
 public sealed class ApplyResult { public bool Success;public string Message; }
 public static class Json {
  public static string Write(object value){return new JavaScriptSerializer {MaxJsonLength=2097152}.Serialize(value);}
  public static T Read<T>(string text){if(text.Length>2097152)throw new InvalidDataException("Catalog is too large.");return new JavaScriptSerializer{MaxJsonLength=2097152}.Deserialize<T>(text.TrimStart('\ufeff'));}
 }
 public static class Rules {
  public static string Hash(string file){using(var h=SHA256.Create())using(var s=File.OpenRead(file))return Hex(h.ComputeHash(s));}
  public static string Hex(byte[] bytes){return BitConverter.ToString(bytes).Replace("-","").ToLowerInvariant();}
  public static bool Digest(string s){return s!=null&&Regex.IsMatch(s,"^[a-fA-F0-9]{64}$");}
  public static void Validate(Package p,bool api){
   Version version;
   if(p==null||p.Local||p.Id==null||!Regex.IsMatch(p.Id,"^[a-z][a-z0-9-]{0,47}$")||api!=(p.Id=="anyapi")||string.IsNullOrWhiteSpace(p.Name)||p.Name.Length>100||!System.Version.TryParse(p.Version,out version)||p.Revision<1||p.MinimumApi<0||!Digest(p.Sha256)||p.FileHashes==null||p.FileHashes.Count!=1||p.GameBuilds==null||p.GameBuilds.Count==0||p.GameBuilds.Count>32)throw new InvalidDataException("Invalid package metadata.");
   if(p.Description==null||p.Description.Length>1000)throw new InvalidDataException("Invalid package description.");
   p.Sha256=p.Sha256.ToLowerInvariant();
   if(!string.IsNullOrEmpty(p.Url)){Uri u;if(!Uri.TryCreate(p.Url,UriKind.Absolute,out u)||u.Scheme!="https"||!string.IsNullOrEmpty(u.UserInfo))throw new InvalidDataException("Downloads must use HTTPS.");}
   foreach(var f in p.FileHashes){
    bool allowed=api?f.Key=="dinput8.dll":Regex.IsMatch(f.Key,@"^AnyAPI and Modding/mods/[A-Za-z][A-Za-z0-9_-]{0,63}\.dll$");
    if(!allowed||!Digest(f.Value))throw new InvalidDataException("Package contains an unsupported installation path.");
   }
   foreach(var key in new List<string>(p.FileHashes.Keys))p.FileHashes[key]=p.FileHashes[key].ToLowerInvariant();
   foreach(var b in p.GameBuilds){if(b==null||!Digest(b.ExeSha256)||!Digest(b.GclSha256)||string.IsNullOrEmpty(b.Version))throw new InvalidDataException("Missing verified game fingerprint.");b.ExeSha256=b.ExeSha256.ToLowerInvariant();b.GclSha256=b.GclSha256.ToLowerInvariant();}
  }
  public static void Validate(Catalog c){
   if(c==null||c.Schema!=1||c.Api==null||c.Mods==null||c.Api.Count>32||c.Mods.Count>256)throw new InvalidDataException("Unsupported catalog.");
   var ids=new HashSet<string>();var files=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
   foreach(var p in c.Api)Validate(p,true);
   foreach(var p in c.Mods){Validate(p,false);if(!ids.Add(p.Id))throw new InvalidDataException("Duplicate mod ID.");foreach(var f in p.FileHashes)if(!files.Add(f.Key))throw new InvalidDataException("Two mods use the same DLL path.");}
  }
  public static string OnlyFile(Package p){foreach(var f in p.FileHashes)return f.Key;throw new InvalidDataException();}
  public static bool Matches(Package p,string exe,string gcl){foreach(var b in p.GameBuilds)if(string.Equals(b.ExeSha256,exe,StringComparison.OrdinalIgnoreCase)&&string.Equals(b.GclSha256,gcl,StringComparison.OrdinalIgnoreCase))return true;return false;}
  public static string Target(string root,string relative){
   if(string.IsNullOrEmpty(root)||string.IsNullOrEmpty(relative)||Path.IsPathRooted(relative)||relative.Contains(":")||relative.Contains("\\")||Array.Exists(relative.Split('/'),x=>x==".."||x=="."||x.Length==0))throw new InvalidDataException("Invalid installation path.");
   foreach(var segment in relative.Split('/'))if(segment.TrimEnd(' ','.')!=segment||Regex.IsMatch(segment,@"^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)",RegexOptions.IgnoreCase))throw new InvalidDataException("Reserved Windows installation path.");
   string full=Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar)+Path.DirectorySeparatorChar;
   string target=Path.GetFullPath(Path.Combine(full,relative.Replace('/',Path.DirectorySeparatorChar)));
   if(!target.StartsWith(full,StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("Path escaped the game folder.");
   CheckLinks(full);string current=Path.GetDirectoryName(target);
   while(current!=null&&current.StartsWith(full,StringComparison.OrdinalIgnoreCase)){CheckLinks(current);current=Path.GetDirectoryName(current);}
   CheckLinks(target);return target;
  }
  static void CheckLinks(string p){if((File.Exists(p)||Directory.Exists(p))&&(File.GetAttributes(p)&FileAttributes.ReparsePoint)!=0)throw new IOException("Choose the actual game folder rather than a linked installation path.");}
 }
}
