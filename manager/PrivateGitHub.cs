using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace AnyApiManager {
 // Private repositories use the user's existing gh sign-in. No token is read or bundled.
 public static class PrivateGitHub {
  public sealed class ReleaseAsset { public long id=0;public string name=null; }
  public sealed class Release { public List<ReleaseAsset> assets=null; }
  public static string Endpoint(string url){
   Uri u;if(!Uri.TryCreate(url,UriKind.Absolute,out u)||u.Scheme!="https"||!u.IsDefaultPort||u.UserInfo!=""||u.Query!=""||u.Fragment!="")return null;
   const string repo=@"/([A-Za-z0-9][A-Za-z0-9_-]*)/([A-Za-z0-9][A-Za-z0-9_.-]*)";
   Match m;
   if(u.Host=="raw.githubusercontent.com"&&(m=Regex.Match(u.AbsolutePath,"^"+repo+@"/main/catalog\.json$")).Success)return "repos/"+m.Groups[1].Value+"/"+m.Groups[2].Value+"/contents/catalog.json?ref=main";
   if(u.Host=="github.com"&&(m=Regex.Match(u.AbsolutePath,"^"+repo+@"/releases/download/([A-Za-z0-9][A-Za-z0-9_.-]*)/([A-Za-z][A-Za-z0-9_.-]*\.zip)$")).Success)return "repos/"+m.Groups[1].Value+"/"+m.Groups[2].Value+"/releases/tags/"+m.Groups[3].Value;
   return null;
  }
  static string FindCli(){
   var paths=new List<string>{Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),"GitHub CLI/gh.exe"),Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),"GitHub CLI/gh.exe"),Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"Programs/GitHub CLI/gh.exe")};
   foreach(var dir in (Environment.GetEnvironmentVariable("PATH")??"").Split(Path.PathSeparator))if(!string.IsNullOrWhiteSpace(dir))try{paths.Add(Path.Combine(dir.Trim('"'),"gh.exe"));}catch(ArgumentException){}
   return paths.FirstOrDefault(File.Exists);
  }
  public static async Task<byte[]> Download(string url,int limit){
   string endpoint=Endpoint(url);if(endpoint==null)return null;
   string cli=FindCli();if(cli==null)throw new IOException("For private downloads, install GitHub CLI and sign in. Public downloads need no sign-in.");
   if(endpoint.Contains("/contents/"))return await Read(cli,endpoint,"application/vnd.github.v3.raw+json",limit);
   var release=Json.Read<Release>(Encoding.UTF8.GetString(await Read(cli,endpoint,"application/vnd.github+json",2097152)));
   string name=new Uri(url).Segments.Last();var asset=release.assets==null?null:release.assets.FirstOrDefault(a=>a.name==name&&a.id>0);
   if(asset==null)throw new IOException("That download is missing from the GitHub release. Check for updates.");
   string repo=endpoint.Substring(0,endpoint.IndexOf("/releases/",StringComparison.Ordinal));
   return await Read(cli,repo+"/releases/assets/"+asset.id,"application/octet-stream",limit);
  }
  static async Task<byte[]> Limited(Stream input,int limit){using(var output=new MemoryStream()){byte[] block=new byte[65536];int n;while((n=await input.ReadAsync(block,0,block.Length))>0){if(output.Length+n>limit)throw new InvalidDataException("Download exceeds the package limit.");await output.WriteAsync(block,0,n);}return output.ToArray();}}
  static async Task<byte[]> Read(string cli,string endpoint,string accept,int limit){
   // Arguments consist only of validated repository names/tags and fixed API paths.
   var info=new ProcessStartInfo(cli,"api \""+endpoint+"\" --hostname github.com --method GET --header \"Accept: "+accept+"\""){UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true};
   info.EnvironmentVariables["GH_PROMPT_DISABLED"]="1";info.EnvironmentVariables["GH_DEBUG"]="";
   using(var p=Process.Start(info))try{
    var output=Limited(p.StandardOutput.BaseStream,limit);var errors=p.StandardError.ReadToEndAsync();var timeout=Task.Delay(TimeSpan.FromMinutes(3));
    if(await Task.WhenAny(output,timeout)!=output)throw new IOException("GitHub download timed out. Try again.");
    var bytes=await output;var completion=Task.WhenAll(errors,Task.Run(()=>p.WaitForExit()));
    if(await Task.WhenAny(completion,timeout)!=completion)throw new IOException("GitHub download timed out. Try again.");await completion;
    if(p.ExitCode!=0)throw new IOException("GitHub CLI could not access this private download. Check your sign-in and repository access.");
    return bytes;
   }finally{if(!p.HasExited)p.Kill();}
  }
 }
}
