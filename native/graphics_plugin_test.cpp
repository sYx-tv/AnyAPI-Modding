#define NOMINMAX
#include <windows.h>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <vector>
#include "anyapi_services_v1.h"
#include "anyapi_menu_v2.h"
#include "anyapi_post_process_v1.h"
#include "anygraphics_options.h"
#include "anyhelpers_settings_v2.h"
static std::vector<AnyPostParametersV1> parameters;
static uint64_t add(const AnyPostPassV1* pass){assert(pass&&pass->shader&&pass->shader_bytes);parameters.emplace_back();return parameters.size();}
static bool set(uint64_t token,const AnyPostParametersV1* value){if(!token||token>parameters.size())return false;parameters[token-1]=*value;return true;}
static bool status(AnyPostStatusV1* value){*value={};return true;}
static const AnyPostProcessV1 post{sizeof(AnyPostProcessV1),1,add,set,status};
static void log(uint32_t,const char* id,const char* text){std::cout<<id<<": "<<text<<'\n';}
static void capture(uint32_t){}
int wmain(int argc,wchar_t** argv){assert(argc==4);auto fixture=LoadLibraryW(argv[1]);assert(fixture);auto service=AnyAPI_Services();assert(service&&service->publish("anyapi.post_process",1,&post));auto helpers=LoadLibraryW(argv[2]),graphics=LoadLibraryW(argv[3]);assert(helpers&&graphics);
 auto directory=std::filesystem::temp_directory_path()/(L"AnyGraphicsTest-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));auto path=directory.wstring();AnyModHostV1 host;host.game_directory=path.c_str();host.plugin_directory=path.c_str();host.log=log;host.capture_input=capture;
 AnyModCallbacksV1 h,g;assert(((AnyModInitV1)GetProcAddress(helpers,"AnyAPI_ModInit"))(&host,&h));assert(((AnyModInitV1)GetProcAddress(graphics,"AnyAPI_ModInit"))(&host,&g));((void(*)())GetProcAddress(graphics,"AnyAPI_ModReady"))();assert(parameters.size()==11&&g.render);
 auto settings=(const AnyHelpersSettingsV1*)service->query("anyhelpers.settings",1);assert(settings);AnySettingValueV1 value;for(uint64_t t=1;t<=graphics_options::Count;++t)assert(settings->get(t,&value));assert(!settings->get(graphics_options::Count+1,&value));
 auto ui=(void(*)(int32_t))GetProcAddress(fixture,"FixtureUiState");auto event=(void(*)(uint32_t))GetProcAddress(fixture,"FixtureSettingsEvent");auto stage=(void(*)(uint64_t,double,const char*))GetProcAddress(fixture,"FixtureStage");assert(ui&&event&&stage);
 AnyFrameV1 frame;frame.tick=100;frame.focused=1;AnyCanvasV1 canvas;auto draw=[&](){frame.tick+=16;g.render(&frame,&canvas,nullptr);assert(!canvas.pixels);};auto apply=[&](graphics_options::Index i,double v){event(ANY_MENU_OPEN);stage(i+1,v,nullptr);event(ANY_MENU_APPLY);event(ANY_MENU_CANCEL);draw();};
 apply(graphics_options::Advanced,1);ui(1);draw();assert(parameters[9].enabled&&!parameters[0].enabled&&!parameters[10].enabled);ui(13);draw();for(auto& p:parameters)assert(!p.enabled);ui(2);draw();for(auto& p:parameters)assert(!p.enabled);ui(1);draw();assert(parameters[9].enabled);
 apply(graphics_options::Bloom,1);assert(parameters[1].enabled&&parameters[4].enabled&&!parameters[5].enabled);apply(graphics_options::BloomQuality,1);assert(!parameters[1].enabled&&parameters[5].enabled&&parameters[8].enabled);
 apply(graphics_options::Antialias,2);assert(parameters[0].enabled);apply(graphics_options::Exposure,1);assert(parameters[10].enabled&&parameters[10].values[graphics_options::Exposure]==1);
 // Every numeric setting reaches the actual DLL shader parameters on Apply.
 for(size_t i=0;i<graphics_options::Preset;++i){auto& d=graphics_options::definitions[i];double v=d.initial==d.minimum?d.maximum:d.minimum;if(i==graphics_options::Enabled||i==graphics_options::GameplayOnly)v=1;apply(graphics_options::Index(i),v);if(std::abs(parameters[9].values[i]-graphics_options::clean(i,v))>=.000001f){std::cerr<<"Mismatch index="<<i<<" id="<<d.id<<" actual="<<parameters[9].values[i]<<" expected="<<graphics_options::clean(i,v)<<std::endl;assert(false);}}

 auto rows=(uint32_t(*)())GetProcAddress(fixture,"FixtureRows");assert(rows);
 apply(graphics_options::Advanced,0);event(ANY_MENU_OPEN);stage(0,0,nullptr);assert(rows()==12);event(ANY_MENU_CANCEL);
 for(int look=1;look<=5;++look){apply(graphics_options::Preset,look);auto custom=graphics_options::defaults();for(size_t i=0;i<graphics_options::Count;++i){AnySettingValueV1 v;assert(settings->get(i+1,&v));custom[i]=graphics_options::clean(i,v.number);}auto effective=graphics_options::effective(custom);for(size_t i=0;i<graphics_options::Count;++i)assert(std::abs(parameters[9].values[i]-effective[i])<.000001f);event(ANY_MENU_OPEN);stage(0,0,nullptr);assert(rows()==5);event(ANY_MENU_CANCEL);}
 event(ANY_MENU_OPEN);stage(graphics_options::Preset+1,0,nullptr);assert(rows()==12);event(ANY_MENU_CANCEL);draw();assert(parameters[9].values[graphics_options::Preset]==5);
 apply(graphics_options::Preset,0);assert(parameters[9].values[graphics_options::Exposure]==-2);apply(graphics_options::Advanced,1);event(ANY_MENU_OPEN);stage(0,0,nullptr);assert(rows()==graphics_options::Count+1);event(ANY_MENU_CANCEL);
 auto presentation=(const AnyHelpersSettingsV2*)service->query("anyhelpers.settings",2);assert(presentation&&presentation->visibility);assert(!presentation->visibility(0,1,0,0,0));assert(!presentation->visibility(1,1,0,0,0));assert(!presentation->visibility(1,99999,0,0,0));assert(!presentation->visibility(1,2,NAN,0,0));
 apply(graphics_options::Enabled,0);for(auto& p:parameters)assert(!p.enabled);apply(graphics_options::Enabled,1);ui(13);draw();for(auto& p:parameters)assert(!p.enabled);apply(graphics_options::GameplayOnly,0);assert(parameters[10].enabled||parameters[0].enabled);event(ANY_MENU_OPEN);stage(graphics_options::Exposure+1,1,nullptr);event(ANY_MENU_CANCEL);draw();assert(parameters[9].values[graphics_options::Exposure]==-2);
 for(size_t i=0;i<graphics_options::Count;++i){assert(graphics_options::clean(i,NAN)==graphics_options::definitions[i].initial);assert(graphics_options::clean(i,1e10)<=graphics_options::definitions[i].maximum);}
 FreeLibrary(graphics);FreeLibrary(helpers);FreeLibrary(fixture);std::filesystem::remove_all(directory);std::cout<<"PASS: presets, compact and advanced draft visibility, saved custom restoration, all 42 settings registered, Apply feeds parameters, Cancel keeps committed values, menus bypass immediately\n";
}
