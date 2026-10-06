#pragma once
#include <windows.h>
#include "anyhelpers_settings_v1.h"
#include "mod_controls_state.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace helpersettings {
struct Value {
    double number{};
    std::string text;
    bool operator==(const Value&) const = default;
};
struct Setting {
    std::string mod,name,id,label,description;
    uint32_t kind{},text_limit{};
    int32_t order{};
    double minimum{},maximum{},step{};
    std::vector<std::string> choices;
    Value initial,committed,draft;
};
struct Saved {uint32_t kind{};Value value;};

inline bool utf8(const char* text,size_t maximum,bool allow_empty=false) {
    return text&&strlen(text)<=maximum&&(allow_empty||*text)&&
        MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,nullptr,0)>0;
}
inline std::string hex(const std::string& value) {
    constexpr char alphabet[]="0123456789abcdef";
    std::string out;out.reserve(value.size()*2);
    for(unsigned char byte:value){out+=alphabet[byte>>4];out+=alphabet[byte&15];}
    return out;
}
inline bool unhex(const std::string& value,std::string& out) {
    if(value.size()%2||value.size()>512)return false;
    auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
    out.clear();for(size_t i=0;i<value.size();i+=2){int a=digit(value[i]),b=digit(value[i+1]);if(a<0||b<0)return false;out+=char(a*16+b);}
    return out.find('\0')==std::string::npos;
}
inline std::string number_text(double value) {
    char buffer[64]{};auto result=std::to_chars(buffer,buffer+sizeof(buffer),value,
        std::chars_format::general,std::numeric_limits<double>::max_digits10);
    return result.ec==std::errc()?std::string(buffer,result.ptr):std::string();
}
inline bool parse_number(const std::string& text,double& value) {
    auto result=std::from_chars(text.data(),text.data()+text.size(),value);
    return result.ec==std::errc()&&result.ptr==text.data()+text.size()&&std::isfinite(value);
}

struct State {
    std::vector<Setting> settings;
    std::map<std::string,Saved> saved;
    std::filesystem::path file;
    std::string status;
    uint64_t revision{};
    uint32_t rejected{};

    static std::string identity(const Setting& s){return s.mod+"\t"+s.id;}
    // Preserve unknown/unloaded mods; resolve their values only when they register.
    void load() {
        std::ifstream in(file);std::string line;
        while(std::getline(in,line)) {
            std::istringstream row(line);std::string mod,id,kind_text,encoded,extra;
            uint32_t kind{};std::string decoded;
            if(std::count(line.begin(),line.end(),'\t')!=3||!std::getline(row,mod,'\t')||!std::getline(row,id,'\t')||!std::getline(row,kind_text,'\t')||
               !modcontrols::State::id(mod.c_str())||!modcontrols::State::id(id.c_str())||saved.size()>=4096) {++rejected;continue;}
            std::getline(row,encoded);
            auto result=std::from_chars(kind_text.data(),kind_text.data()+kind_text.size(),kind);
            if(result.ec!=std::errc()||result.ptr!=kind_text.data()+kind_text.size()||kind<1||kind>5||!unhex(encoded,decoded)){++rejected;continue;}
            Saved value;value.kind=kind;
            if(kind==ANY_SETTING_TEXT){if(!utf8(decoded.c_str(),256,true)){++rejected;continue;}value.value.text=decoded;}
            else if(!parse_number(decoded,value.value.number)){++rejected;continue;}
            saved[mod+"\t"+id]=std::move(value);
        }
    }
    static bool normalise(const Setting& s,Value& value) {
        if(s.kind==ANY_SETTING_TEXT)return utf8(value.text.c_str(),s.text_limit,true)&&value.text.find('\0')==std::string::npos;
        if(!std::isfinite(value.number))return false;
        if(s.kind==ANY_SETTING_BOOL)return value.number==0||value.number==1;
        if(s.kind==ANY_SETTING_CHOICE)return value.number>=0&&value.number<s.choices.size()&&value.number==std::floor(value.number);
        if(value.number<s.minimum||value.number>s.maximum)return false;
        if(s.kind==ANY_SETTING_INTEGER&&value.number!=std::floor(value.number))return false;
        value.number=std::clamp(s.minimum+std::round((value.number-s.minimum)/s.step)*s.step,s.minimum,s.maximum);
        return std::isfinite(value.number);
    }
    // Copy metadata and options; callers never need to retain descriptor strings.
    uint64_t add(const AnyModSettingV1* definition) {
        if(!definition||definition->struct_size!=sizeof(*definition)||settings.size()>=512||
           !modcontrols::State::id(definition->mod_id)||!modcontrols::State::id(definition->setting_id)||
           !utf8(definition->mod_name,127)||!utf8(definition->label,127)||
           (definition->description&&!utf8(definition->description,255,true))||definition->kind<1||definition->kind>5)return 0;
        for(auto& s:settings)if(s.mod==definition->mod_id&&s.id==definition->setting_id)return 0;
        Setting s;s.mod=definition->mod_id;s.name=definition->mod_name;s.id=definition->setting_id;
        s.label=definition->label;s.description=definition->description?definition->description:"";
        s.kind=definition->kind;s.order=definition->order;s.text_limit=definition->text_limit;
        s.minimum=definition->minimum;s.maximum=definition->maximum;s.step=definition->step;
        if(s.kind==ANY_SETTING_INTEGER||s.kind==ANY_SETTING_NUMBER) {
            if(!std::isfinite(s.minimum)||!std::isfinite(s.maximum)||!std::isfinite(s.step)||s.minimum>=s.maximum||
               s.step<=0||s.step>s.maximum-s.minimum||s.maximum-s.minimum>1e12)return 0;
            if(s.kind==ANY_SETTING_INTEGER&&(s.minimum!=std::floor(s.minimum)||s.maximum!=std::floor(s.maximum)||
               s.step!=std::floor(s.step)||s.step<1||s.minimum<-1e9||s.maximum>1e9))return 0;
        }
        if(s.kind==ANY_SETTING_CHOICE) {
            if(!definition->choices||definition->choice_count<1||definition->choice_count>64)return 0;
            for(uint32_t i=0;i<definition->choice_count;++i){if(!utf8(definition->choices[i],127))return 0;s.choices.emplace_back(definition->choices[i]);}
        }
        if(s.kind==ANY_SETTING_TEXT) {
            if(s.text_limit<1||s.text_limit>256||!utf8(definition->default_text,s.text_limit,true))return 0;
            s.initial.text=definition->default_text;
        }else s.initial.number=definition->default_number;
        if(!normalise(s,s.initial))return 0;s.committed=s.draft=s.initial;
        auto old=saved.find(identity(s));
        if(old!=saved.end()){auto value=old->second.value;if(old->second.kind==s.kind&&normalise(s,value))s.committed=s.draft=value;else ++rejected;}
        settings.push_back(std::move(s));return settings.size();
    }
    bool get(uint64_t token,AnySettingValueV1* out) const {
        if(!token||token>settings.size()||!out||out->struct_size!=sizeof(*out))return false;
        const auto& s=settings[token-1];AnySettingValueV1 value;value.kind=s.kind;value.number=s.committed.number;
        memcpy(value.text,s.committed.text.c_str(),s.committed.text.size()+1);*out=value;return true;
    }
    bool dirty() const {for(auto& s:settings)if(!(s.draft==s.committed))return true;return false;}
    bool defaults() const {for(auto& s:settings)if(!(s.draft==s.initial))return false;return true;}
    void begin(){status.clear();for(auto& s:settings)s.draft=s.committed;}
    void cancel(){begin();}
    void reset(){for(auto& s:settings)s.draft=s.initial;status="Defaults staged. Select Apply to save.";}
    bool stage(size_t index,Value value) {
        if(index>=settings.size()||!normalise(settings[index],value))return false;
        auto& s=settings[index];s.draft=std::move(value);status=s.description.empty()?"Change staged. Select Apply to save.":s.description;return true;
    }
    // Durable replacement succeeds before consumers see new committed values.
    bool apply() {
        if(!dirty())return true;auto next=saved;
        for(auto& s:settings)next[identity(s)]={s.kind,s.draft};
        if(next.size()>4096){status="Could not save settings: storage is full. Changes remain pending.";return false;}
        std::string data;
        for(auto& [id,value]:next){auto payload=value.kind==ANY_SETTING_TEXT?value.value.text:number_text(value.value.number);data+=id+"\t"+std::to_string(value.kind)+"\t"+hex(payload)+"\n";}
        std::error_code error;std::filesystem::create_directories(file.parent_path(),error);
        if(error){status="Could not save settings. Changes remain pending.";return false;}
        auto temporary=file;temporary+=L".tmp";
        HANDLE handle=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle==INVALID_HANDLE_VALUE){status="Could not save settings. Changes remain pending.";return false;}
        DWORD written{};bool ok=WriteFile(handle,data.data(),DWORD(data.size()),&written,nullptr)&&written==data.size()&&FlushFileBuffers(handle);CloseHandle(handle);
        if(ok)ok=MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
        if(!ok){DeleteFileW(temporary.c_str());status="Could not save settings. Changes remain pending.";return false;}
        saved=std::move(next);for(auto& s:settings)s.committed=s.draft;++revision;status="Settings saved.";return true;
    }
};
} // namespace helpersettings
