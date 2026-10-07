#define NOMINMAX
#include <windows.h>
#include <cassert>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include "anyapi_services_v1.h"
#include "anyapi_gpu_draw_v1.h"
#include "anyapi_world_time_v1.h"
#include "anyhelpers_settings_v1.h"
#include "mod_controls_v1.h"
static void(*renderer)(const AnyFrameV1*,void*);static std::vector<std::wstring> texts;static std::vector<AnyGpuCommandV1> commands;
static bool add_renderer(void(*fn)(const AnyFrameV1*,void*),void*){renderer=fn;return true;}
static bool emit(const AnyGpuCommandV1* c){commands.push_back(*c);if(c->text)texts.emplace_back(c->text,c->text_length);return true;}
static const AnyGpuDrawV1 gpu{sizeof(AnyGpuDrawV1),1,add_renderer,nullptr,emit,nullptr};
static uint32_t seconds=45240;static bool available=true;
static bool copy(AnyWorldTimeSnapshotV1* s){s->seconds_since_midnight=seconds;return available;}
static const AnyWorldTimeV1 clock_api{sizeof(AnyWorldTimeV1),1,copy};
static std::map<std::string,uint64_t> ids;static std::map<uint64_t,double> values;static uint64_t revision;
static uint64_t add_setting(const AnyModSettingV1* d){assert(std::string(d->mod_id)=="anyclock");auto token=ids.size()+1;ids[d->setting_id]=token;values[token]=d->default_number;return token;}
static bool get(uint64_t token,AnySettingValueV1* v){v->number=values[token];return true;}
static uint64_t rev(){return revision;}
static const AnyHelpersSettingsV1 settings{sizeof(AnyHelpersSettingsV1),1,add_setting,get,rev};
static uint32_t binding='O';static unsigned registrations;
static uint64_t add_action(const ModControlActionV1* d){assert(d->default_key=='O');assert(std::string(d->action_id)=="check_time");++registrations;return 1;}
static uint32_t key(uint64_t){return binding;}
static const ModControlsV1 controls{sizeof(ModControlsV1),1,add_action,key,nullptr};
static void log(uint32_t,const char*,const char* text){std::cout<<text<<'\n';}
int wmain(int argc,wchar_t** argv){assert(argc==3||argc==4);bool standalone=argc==4;auto fixture=LoadLibraryW(argv[1]);assert(fixture);auto s=AnyAPI_Services();assert(s);assert(s->publish("anyapi.gpu_draw",1,&gpu));assert(s->publish("anyapi.world_time",1,&clock_api));
 if(!standalone){assert(s->publish("anyhelpers.settings",1,&settings));assert(s->publish("anyhelpers.controls",1,&controls));}
 auto ui=(void(*)(int32_t))GetProcAddress(fixture,"FixtureUiState");ui(1);auto mod=LoadLibraryW(argv[2]);assert(mod);AnyModHostV1 host;host.log=log;AnyModCallbacksV1 cb;assert(((AnyModInitV1)GetProcAddress(mod,"AnyAPI_ModInit"))(&host,&cb));((void(*)())GetProcAddress(mod,"AnyAPI_ModReady"))();assert(renderer&&cb.input);
 AnyFrameV1 f;f.width=1920;f.height=1080;f.focused=1;f.tick=GetTickCount64();
 auto render=[&](uint64_t offset=200){f.tick=GetTickCount64()+offset;texts.clear();commands.clear();renderer(&f,nullptr);};
 auto input=[&](uint32_t kind,uint32_t code){AnyInputV1 e;e.kind=kind;e.key=code;return cb.input(&e,nullptr);};
 auto press=[&](){input(ANY_KEY_UP,binding);assert(input(ANY_KEY_DOWN,binding));};
 auto setting=[&](const char* id,double v){values[ids.at(id)]=v;++revision;};
 render();assert(commands.empty());press();render();assert(texts.back()==L"12:34");assert(commands.front().rect[0]>1600&&commands.front().rect[1]<30);assert(commands.front().opacity>0&&commands.front().opacity<1);
 // Autorepeat does not extend the popup. A fresh press does.
 input(ANY_KEY_DOWN,binding);render(5000);assert(commands.empty());press();render();assert(!commands.empty());ui(2);render();assert(commands.empty());assert(!input(ANY_KEY_DOWN,binding));ui(1);render();assert(commands.empty());press();input(ANY_FOCUS_LOST,0);render();assert(commands.empty());
 render();press();f.focused=0;render();assert(commands.empty());f.focused=1;render();assert(commands.empty());
 if(standalone){press();render();assert(texts.back()==L"12:34");assert(registrations==0);return 0;}
 assert(ids.size()==9&&registrations==1);binding='Y';assert(!input(ANY_KEY_DOWN,'O'));press();render();assert(texts.back()==L"12:34");binding=0;assert(!input(ANY_KEY_DOWN,'Y'));binding='Y';
 setting("format",1);seconds=0;press();render();assert(texts.back()==L"12:00 AM");seconds=43200;render();assert(texts.back()==L"12:00 PM");setting("seconds",1);seconds=45245;render();assert(texts.back()==L"12:34:05 PM");
 setting("duration",1);press();render(1200);assert(commands.empty());setting("duration",15);press();render(5000);assert(!commands.empty());
 setting("corner",4);setting("position_x",0);setting("position_y",100);press();render();assert(commands.front().rect[0]<30&&commands.front().rect[1]>900);
 available=false;render();assert(texts.back()==L"Time unavailable");available=true;setting("enabled",0);render();assert(commands.empty());assert(!input(ANY_KEY_DOWN,binding));
 std::cout<<"Clock binding, fade, expiry, menus, formats, positions, unavailable time and settings passed.\n";
}
