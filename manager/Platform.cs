using System;
using System.Collections.Generic;
using System.IO;
using Microsoft.Win32;

namespace AnyApiManager {
 // Where the manager is running: real Windows, Wine/Proton on Linux, or Mono natively on Linux.
 // Native Mono has no user32.dll and no Windows registry, so Windows-only calls must be skipped there.
 public static class Platform {
  public static readonly bool Unix=Environment.OSVersion.Platform==PlatformID.Unix||Environment.OSVersion.Platform==PlatformID.MacOSX;
  static bool? wine;
  public static bool Wine{get{
   if(wine==null){bool found=false;if(!Unix)try{using(var k=Registry.CurrentUser.OpenSubKey(@"Software\Wine"))found=k!=null;if(!found)using(var k=Registry.LocalMachine.OpenSubKey(@"Software\Wine"))found=k!=null;}catch{}wine=found;}
   return wine.Value;
  }}
  public static bool Linux{get{return Unix||Wine;}}
  public static string Name{get{return Unix?"Linux (Mono "+MonoVersion()+")":Wine?"Wine/Proton":"Windows";}}
  static string MonoVersion(){try{var t=Type.GetType("Mono.Runtime");var m=t==null?null:t.GetMethod("GetDisplayName",System.Reflection.BindingFlags.NonPublic|System.Reflection.BindingFlags.Static);return m==null?"?":(string)m.Invoke(null,null);}catch{return "?";}}

  // Steam roots on Linux. Under Wine the Linux home is reachable through the Z: drive.
  public static IEnumerable<string> LinuxSteamRoots(){
   string home=Environment.GetEnvironmentVariable("HOME");
   if(string.IsNullOrEmpty(home)&&!Unix){string user=Environment.GetEnvironmentVariable("USER");if(!string.IsNullOrEmpty(user))home="/home/"+user;}
   if(string.IsNullOrEmpty(home))yield break;
   if(Wine&&home.StartsWith("/"))home="Z:"+home.Replace('/','\\');
   foreach(string rel in new[]{".local/share/Steam",".steam/steam",".steam/root",".var/app/com.valvesoftware.Steam/.local/share/Steam",".var/app/com.valvesoftware.Steam/data/Steam","snap/steam/common/.local/share/Steam","snap/steam/common/.steam/steam"})
    yield return Path.Combine(home,Wine?rel.Replace('/','\\'):rel);
  }
  // libraryfolders.vdf on Linux lists Unix paths; Wine needs them on the Z: drive.
  public static string FromSteamPath(string path){
   if(string.IsNullOrEmpty(path))return path;
   if(Unix)return path;
   if(Wine&&path.StartsWith("/"))return "Z:"+path.Replace('/','\\');
   return path.Replace('/','\\');
  }
  // Proton loads its own dinput8 unless Steam's launch options tell it to prefer ours.
  public const string ProtonLaunchOptions="WINEDLLOVERRIDES=\"dinput8=n,b\" %command%";
 }
}
