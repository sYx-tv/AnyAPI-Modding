#pragma once
#include <windows.h>
#include <map>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include "mod_controls_v1.h"
namespace modcontrols {
struct Action {std::string mod,name,id,label;uint32_t initial{},committed{},draft{};};
// State and persistence belong entirely to the DLL. Callers serialize access.
struct State {
 std::vector<Action> actions;std::map<std::string,uint32_t> saved;std::filesystem::path file;
 std::string status;int capture{-1};uint64_t capture_tick{},last_draw{};bool open{};bool held[256]{};
 static bool id(const char* s){if(!s||!*s||strlen(s)>95)return false;for(;*s;++s)if(!(*s>='a'&&*s<='z'||*s>='0'&&*s<='9'||*s=='.'||*s=='_'||*s=='-'))return false;return true;}
 static bool key_valid(uint32_t k){return !k||(k>=8&&k<255&&k!=VK_ESCAPE&&k!=VK_PROCESSKEY&&k!=VK_PACKET);}
 static std::string identity(const Action& a){return a.mod+"\t"+a.id;}
 uint32_t load(){std::ifstream in(file);std::string line;uint32_t rejected=0;while(std::getline(in,line)){std::istringstream row(line);std::string mod,action,key,extra;if(!std::getline(row,mod,'\t')||!std::getline(row,action,'\t')||!std::getline(row,key,'\t')||std::getline(row,extra,'\t')||!id(mod.c_str())||!id(action.c_str())){++rejected;continue;}
  try{size_t count{};auto value=std::stoul(key,&count);if(count!=key.size()||value>254||!key_valid(uint32_t(value))||saved.size()>=1024){++rejected;continue;}saved[mod+"\t"+action]=uint32_t(value);}catch(...){++rejected;}}
 return rejected;}
 uint64_t add(const ModControlActionV1* c){if(!c||c->struct_size!=sizeof(*c)||!id(c->mod_id)||!id(c->action_id)||!c->mod_name||!*c->mod_name||strlen(c->mod_name)>127||!c->label||!*c->label||strlen(c->label)>127||!key_valid(c->default_key)||actions.size()>=256)return 0;
  if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,c->mod_name,-1,nullptr,0)||!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,c->label,-1,nullptr,0))return 0;
  for(auto& a:actions)if(a.mod==c->mod_id&&a.id==c->action_id)return 0;Action a{c->mod_id,c->mod_name,c->action_id,c->label,c->default_key,c->default_key,c->default_key};auto saved_key=saved.find(identity(a));if(saved_key!=saved.end())a.committed=a.draft=saved_key->second;
  actions.push_back(a);return actions.size();}
 bool dirty()const{for(auto& a:actions)if(a.draft!=a.committed)return true;return false;}
 bool defaults()const{for(auto& a:actions)if(a.draft!=a.initial)return false;return true;}
 void begin(){open=true;capture=-1;status.clear();for(auto& a:actions)a.draft=a.committed;}
 void cancel(){open=false;capture=-1;status.clear();for(auto& a:actions)a.draft=a.committed;}
 void reset(){capture=-1;status="Defaults staged. Select Apply to save.";for(auto& a:actions)a.draft=a.initial;}
 bool stage(size_t index,uint32_t key){if(index>=actions.size()||!key_valid(key))return false;for(size_t i=0;i<actions.size();++i)if(i!=index&&key&&actions[i].draft==key){status="Key already used by "+actions[i].name+": "+actions[i].label;return false;}actions[index].draft=key;status="Binding staged. Select Apply to save.";return true;}
 // Write a complete replacement before changing committed memory. Preserve unloaded mods' bindings.
 bool apply(){if(!dirty()){capture=-1;return true;}auto next=saved;for(auto& a:actions)next[identity(a)]=a.draft;std::string data;for(auto& pair:next)data+=pair.first+"\t"+std::to_string(pair.second)+"\n";
  std::error_code ec;std::filesystem::create_directories(file.parent_path(),ec);if(ec){status="Could not save bindings. Changes remain pending.";return false;}auto temporary=file;temporary+=L".tmp";
  HANDLE h=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE){status="Could not save bindings. Changes remain pending.";return false;}
  DWORD written{};bool ok=WriteFile(h,data.data(),DWORD(data.size()),&written,nullptr)&&written==data.size()&&FlushFileBuffers(h);CloseHandle(h);
  if(ok)ok=MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;if(!ok){DeleteFileW(temporary.c_str());status="Could not save bindings. Changes remain pending.";return false;}
  saved=std::move(next);for(auto& a:actions)a.committed=a.draft;capture=-1;status="Bindings saved.";return true;}
};
inline std::string key_name(uint32_t key){if(!key)return "Unbound";if(key>='A'&&key<='Z'||key>='0'&&key<='9')return std::string(1,char(key));UINT scan=MapVirtualKeyW(key,MAPVK_VK_TO_VSC);LONG bits=LONG(scan<<16);if(key==VK_LEFT||key==VK_RIGHT||key==VK_UP||key==VK_DOWN||key==VK_HOME||key==VK_END||key==VK_PRIOR||key==VK_NEXT||key==VK_INSERT||key==VK_DELETE||key==VK_DIVIDE||key==VK_NUMLOCK)bits|=1<<24;
 wchar_t name[96]{};if(GetKeyNameTextW(bits,name,96)){char utf8[288]{};if(WideCharToMultiByte(CP_UTF8,0,name,-1,utf8,288,nullptr,nullptr))return utf8;}return "Key "+std::to_string(key);}
}
