#pragma once
#include <windows.h>
#include "map_math.h"
#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
namespace mapmarkers {
struct Marker {uint64_t id{};atlas::Point position;std::string name;};
inline bool name_valid(const std::string& name){if(name.empty()||name.size()>64||name.find('\0')!=std::string::npos||!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name.c_str(),-1,nullptr,0))return false;for(unsigned char c:name)if(c<32||c==127)return false;return name.find_first_not_of(' ')!=std::string::npos;}
inline bool point_valid(atlas::Point p){atlas::Point uv;return atlas::project(p.x,p.y,uv)&&uv.x>=0&&uv.x<=1&&uv.y>=0&&uv.y<=1;}
inline std::string hex(const std::string& s){const char* alphabet="0123456789abcdef";std::string out;for(unsigned char c:s){out+=alphabet[c>>4];out+=alphabet[c&15];}return out;}
inline bool unhex(const std::string& s,std::string& out){if(s.size()>128||s.size()%2)return false;auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};out.clear();for(size_t i=0;i<s.size();i+=2){int a=digit(s[i]),b=digit(s[i+1]);if(a<0||b<0)return false;out+=char(a*16+b);}return name_valid(out);}
template<class T> inline bool parse(const std::string& text,T& out){auto r=std::from_chars(text.data(),text.data()+text.size(),out);return r.ec==std::errc()&&r.ptr==text.data()+text.size();}
inline std::string decimal(double v){char bytes[64]{};auto r=std::to_chars(bytes,bytes+64,v,std::chars_format::general,17);return {bytes,r.ptr};}
// Persist first, then expose edits. A failed save never destroys the previous file
// or the in-memory markers. Storage is local to this mod, not the API/AnyHelpers.
struct Store {
 std::filesystem::path file;std::vector<Marker> records;uint64_t active{},next=1;unsigned rejected{};std::string error;
 const Marker* find(uint64_t id)const{for(auto& m:records)if(m.id==id)return &m;return nullptr;}
 void load(){std::ifstream in(file);std::string line;bool first=true;while(std::getline(in,line)){std::istringstream row(line);std::string id,x,z,name;if(first){first=false;if(std::getline(row,id,'\t')&&id=="ANYMAP_MARKERS"&&std::getline(row,x,'\t')&&x=="1"&&std::getline(row,z)&&parse(z,active))continue;++rejected;active=0;continue;}
 Marker m;if(std::count(line.begin(),line.end(),'\t')!=3||records.size()>=128||!std::getline(row,id,'\t')||!std::getline(row,x,'\t')||!std::getline(row,z,'\t')||!std::getline(row,name)||!parse(id,m.id)||!m.id||m.id==UINT64_MAX||find(m.id)||!parse(x,m.position.x)||!parse(z,m.position.y)||!point_valid(m.position)||!unhex(name,m.name)){++rejected;continue;}next=std::max(next,m.id+1);records.push_back(std::move(m));}if(!find(active))active=0;}
 bool commit(std::vector<Marker> values,uint64_t route){if(values.size()>128){error="Marker limit reached (128).";return false;}for(auto& m:values)if(!m.id||!name_valid(m.name)||!point_valid(m.position)){error="Enter a name (up to 64 UTF-8 bytes).";return false;}
 std::string data="ANYMAP_MARKERS\t1\t"+std::to_string(route)+"\n";for(auto& m:values)data+=std::to_string(m.id)+"\t"+decimal(m.position.x)+"\t"+decimal(m.position.y)+"\t"+hex(m.name)+"\n";
 std::error_code ec;std::filesystem::create_directories(file.parent_path(),ec);if(ec){error="Could not save markers. Your edit is still pending.";return false;}auto temp=file;temp+=L".tmp";auto h=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE){error="Could not save markers. Your edit is still pending.";return false;}DWORD written{};bool ok=WriteFile(h,data.data(),DWORD(data.size()),&written,nullptr)&&written==data.size()&&FlushFileBuffers(h);CloseHandle(h);if(ok)ok=MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;if(!ok){DeleteFileW(temp.c_str());error="Could not save markers. Your edit is still pending.";return false;}records=std::move(values);active=route;error.clear();return true;}
 bool save(uint64_t id,atlas::Point at,std::string name){auto first=name.find_first_not_of(' '),last=name.find_last_not_of(' ');if(first!=std::string::npos)name=name.substr(first,last-first+1);if(!name_valid(name)){error="Enter a name (up to 64 UTF-8 bytes).";return false;}auto values=records;bool found=false;for(auto& m:values)if(m.id==id){m.name=name;found=true;break;}if(!found){if(next==UINT64_MAX){error="Marker ID limit reached.";return false;}values.push_back({next,at,std::move(name)});}if(!commit(std::move(values),active))return false;if(!found)++next;return true;}
 bool erase(uint64_t id){auto values=records;values.erase(std::remove_if(values.begin(),values.end(),[&](const Marker& m){return m.id==id;}),values.end());return commit(std::move(values),active==id?0:active);}
};
// Append whole Unicode scalars and remove whole UTF-8 codepoints in the editor.
inline bool append(std::string& text,uint32_t cp){if(cp<32||cp==127||cp>0x10ffff||cp>=0xd800&&cp<=0xdfff)return false;std::string bytes;if(cp<0x80)bytes+=char(cp);else if(cp<0x800){bytes+=char(0xc0|(cp>>6));bytes+=char(0x80|(cp&63));}else if(cp<0x10000){bytes+=char(0xe0|(cp>>12));bytes+=char(0x80|((cp>>6)&63));bytes+=char(0x80|(cp&63));}else{bytes+=char(0xf0|(cp>>18));bytes+=char(0x80|((cp>>12)&63));bytes+=char(0x80|((cp>>6)&63));bytes+=char(0x80|(cp&63));}if(text.size()+bytes.size()>64)return false;text+=bytes;return true;}
inline void backspace(std::string& text){if(text.empty())return;size_t i=text.size()-1;while(i&&(static_cast<unsigned char>(text[i])&0xc0)==0x80)--i;text.resize(i);}
}
