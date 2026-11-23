using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;
namespace AnyMapMaker {
 // Number that keeps the exact text it was read from, so a tile that is loaded and saved
 // unchanged comes back byte-for-byte equivalent (no 1.0 -> 1 or exponent rewrites).
 public sealed class JsonNumber {
  public readonly string Raw; public readonly double Value;
  public JsonNumber(string raw){Raw=raw;Value=double.Parse(raw,NumberStyles.Float,CultureInfo.InvariantCulture);}
  public JsonNumber(double value){Value=value;Raw=Format(value);}
  public static string Format(double v){
   if(double.IsNaN(v)||double.IsInfinity(v))v=0;
   string s=v.ToString("R",CultureInfo.InvariantCulture);
   if(s.IndexOf('.')<0&&s.IndexOf('E')<0)s+=".0";
   return s;
  }
  public override string ToString(){return Raw;}
 }

 // Object that keeps key order. Unknown keys survive a load/save round trip untouched.
 public sealed class JsonObject : IEnumerable<KeyValuePair<string,object>> {
  readonly List<KeyValuePair<string,object>> items=new List<KeyValuePair<string,object>>();
  public int Count {get{return items.Count;}}
  public bool Has(string key){return IndexOf(key)>=0;}
  int IndexOf(string key){for(int i=0;i<items.Count;i++)if(items[i].Key==key)return i;return -1;}
  public object this[string key]{
   get{int i=IndexOf(key);return i<0?null:items[i].Value;}
   set{int i=IndexOf(key);if(i<0)items.Add(new KeyValuePair<string,object>(key,value));else items[i]=new KeyValuePair<string,object>(key,value);}
  }
  public void Remove(string key){int i=IndexOf(key);if(i>=0)items.RemoveAt(i);}
  public IEnumerator<KeyValuePair<string,object>> GetEnumerator(){return items.GetEnumerator();}
  System.Collections.IEnumerator System.Collections.IEnumerable.GetEnumerator(){return items.GetEnumerator();}
  public string Str(string key){return this[key] as string;}
  public double Num(string key,double fallback){var n=this[key] as JsonNumber;return n==null?fallback:n.Value;}
  public List<object> Arr(string key){return this[key] as List<object>;}
  public JsonObject Obj(string key){return this[key] as JsonObject;}
 }

 public static class Json {
  public static object Parse(string text){
   int i=0;
   if(text.Length>0&&text[0]=='﻿')i=1;
   object v=Value(text,ref i);Skip(text,ref i);
   if(i!=text.Length)throw Error(text,i,"Unexpected text after the JSON value");
   return v;
  }
  static Exception Error(string t,int i,string what){
   int line=1;for(int k=0;k<i&&k<t.Length;k++)if(t[k]=='\n')line++;
   return new FormatException(what+" (line "+line+")");
  }
  static void Skip(string t,ref int i){while(i<t.Length&&(t[i]==' '||t[i]=='\t'||t[i]=='\r'||t[i]=='\n'))i++;}
  static object Value(string t,ref int i){
   Skip(t,ref i);if(i>=t.Length)throw Error(t,i,"Unexpected end of JSON");
   char c=t[i];
   if(c=='{'){
    var o=new JsonObject();i++;Skip(t,ref i);
    if(i<t.Length&&t[i]=='}'){i++;return o;}
    while(true){
     Skip(t,ref i);if(i>=t.Length||t[i]!='"')throw Error(t,i,"Expected a key");
     string k=Str(t,ref i);Skip(t,ref i);
     if(i>=t.Length||t[i]!=':')throw Error(t,i,"Expected ':'");i++;
     o[k]=Value(t,ref i);Skip(t,ref i);
     if(i<t.Length&&t[i]==','){i++;continue;}
     if(i<t.Length&&t[i]=='}'){i++;return o;}
     throw Error(t,i,"Expected ',' or '}'");
    }
   }
   if(c=='['){
    var a=new List<object>();i++;Skip(t,ref i);
    if(i<t.Length&&t[i]==']'){i++;return a;}
    while(true){
     a.Add(Value(t,ref i));Skip(t,ref i);
     if(i<t.Length&&t[i]==','){i++;continue;}
     if(i<t.Length&&t[i]==']'){i++;return a;}
     throw Error(t,i,"Expected ',' or ']'");
    }
   }
   if(c=='"')return Str(t,ref i);
   if(string.CompareOrdinal(t,i,"true",0,4)==0){i+=4;return true;}
   if(string.CompareOrdinal(t,i,"false",0,5)==0){i+=5;return false;}
   if(string.CompareOrdinal(t,i,"null",0,4)==0){i+=4;return null;}
   int s=i;
   while(i<t.Length&&("+-0123456789.eE".IndexOf(t[i])>=0))i++;
   if(s==i)throw Error(t,i,"Unexpected character '"+c+"'");
   return new JsonNumber(t.Substring(s,i-s));
  }
  static string Str(string t,ref int i){
   var b=new StringBuilder();i++;
   while(i<t.Length){
    char c=t[i++];
    if(c=='"')return b.ToString();
    if(c!='\\'){b.Append(c);continue;}
    if(i>=t.Length)break;
    char e=t[i++];
    switch(e){
     case 'n':b.Append('\n');break; case 't':b.Append('\t');break; case 'r':b.Append('\r');break;
     case 'b':b.Append('\b');break; case 'f':b.Append('\f');break;
     case 'u':b.Append((char)Convert.ToInt32(t.Substring(i,4),16));i+=4;break;
     default:b.Append(e);break;
    }
   }
   throw Error(t,i,"Unterminated string");
  }

  public static string Write(object v,bool indent){var b=new StringBuilder();Write(b,v,indent,0);if(indent)b.Append('\n');return b.ToString();}
  static void NewLine(StringBuilder b,bool indent,int depth){if(!indent)return;b.Append('\n');b.Append(' ',depth*2);}
  static void Write(StringBuilder b,object v,bool indent,int depth){
   if(v==null){b.Append("null");return;}
   if(v is bool){b.Append((bool)v?"true":"false");return;}
   if(v is JsonNumber){b.Append(((JsonNumber)v).Raw);return;}
   if(v is double){b.Append(JsonNumber.Format((double)v));return;}
   if(v is int){b.Append(((int)v).ToString(CultureInfo.InvariantCulture));return;}
   if(v is string){Quote(b,(string)v);return;}
   var o=v as JsonObject;
   if(o!=null){
    if(o.Count==0){b.Append("{}");return;}
    b.Append('{');bool first=true;
    foreach(var kv in o){if(!first)b.Append(',');first=false;NewLine(b,indent,depth+1);Quote(b,kv.Key);b.Append(indent?": ":":");Write(b,kv.Value,indent,depth+1);}
    NewLine(b,indent,depth);b.Append('}');return;
   }
   var a=v as List<object>;
   if(a!=null){
    if(a.Count==0){b.Append("[]");return;}
    // Short arrays of numbers (vectors) stay on one line.
    bool flat=a.Count<=4&&a.TrueForAll(x=>x is JsonNumber||x is double||x is int);
    b.Append('[');
    for(int k=0;k<a.Count;k++){if(k>0)b.Append(flat&&indent?", ":",");if(!flat)NewLine(b,indent,depth+1);Write(b,a[k],indent,depth+1);}
    if(!flat)NewLine(b,indent,depth);b.Append(']');return;
   }
   throw new ArgumentException("Cannot write "+v.GetType().Name+" as JSON");
  }
  static void Quote(StringBuilder b,string s){
   b.Append('"');
   foreach(char c in s){
    switch(c){
     case '"':b.Append("\\\"");break; case '\\':b.Append("\\\\");break;
     case '\n':b.Append("\\n");break; case '\r':b.Append("\\r");break; case '\t':b.Append("\\t");break;
     default:if(c<0x20)b.Append("\\u").Append(((int)c).ToString("x4"));else b.Append(c);break;
    }
   }
   b.Append('"');
  }
 }
}
