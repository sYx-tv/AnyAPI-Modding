#define NOMINMAX
#include <windows.h>
#include <cassert>
#include <iostream>
#include <string>
#include <map>
#include <vector>
#include "anyapi_services_v1.h"
#include "anyapi_equipment_v2.h"
#include "anyapi_hud_hint_provider_v1.h"
#include "anyapi_gpu_draw_v1.h"
#include "anyhelpers_settings_v1.h"
#include "mod_controls_v1.h"
static void(*renderer)(const AnyFrameV1*,void*);static std::vector<std::wstring> texts;static unsigned polygons,captured,selected_calls;static int selected_item;static bool available=true;static uint32_t binding='Q';
static bool register_renderer(void(*fn)(const AnyFrameV1*,void*),void*){renderer=fn;return true;}
static bool emit(const AnyGpuCommandV1* c){if(c->kind==ANY_GPU_POLYGON){++polygons;assert(c->point_count>=6);}if(c->text)texts.emplace_back(c->text,c->text_length);return true;}
static const AnyGpuDrawV1 gpu{sizeof(AnyGpuDrawV1),1,register_renderer,nullptr,emit,nullptr};
static AnyEquipmentToolsSnapshotV2 snapshot;
static bool copy(AnyEquipmentToolsSnapshotV2* out){*out=snapshot;return available;}
static uint64_t equip(uint64_t context,int32_t id){assert(context==snapshot.context);for(uint32_t i=0;i<snapshot.count;++i)if(snapshot.tools[i].item_id==id){++selected_calls;selected_item=id;return selected_calls;}assert(false);return 0;}
static uint32_t state(uint64_t){return ANY_EQUIPMENT_APPLIED;}
static const AnyEquipmentV2 equipment_api{sizeof(AnyEquipmentV2),2,copy,equip,state};
static std::map<std::string,uint64_t> ids;static std::map<uint64_t,double> values;static uint64_t revision;
static uint64_t add_setting(const AnyModSettingV1* d){uint64_t token=ids.size()+1;ids[d->setting_id]=token;values[token]=d->default_number;return token;}
static bool get(uint64_t token,AnySettingValueV1* v){v->number=values[token];return true;}
static uint64_t rev(){return revision;}
static const AnyHelpersSettingsV1 settings{sizeof(AnyHelpersSettingsV1),1,add_setting,get,rev};
static uint64_t add_action(const ModControlActionV1* d){assert(d->default_key=='Q');return 1;}
static uint32_t key(uint64_t){return binding;}
static const ModControlsV1 controls{sizeof(ModControlsV1),1,add_action,key,nullptr};
static void capture(uint32_t value){captured=value;}
static void log(uint32_t,const char*,const char* text){std::cout<<text<<'\n';}
int wmain(int argc,wchar_t** argv){assert(argc==3);auto fixture=LoadLibraryW(argv[1]);assert(fixture);auto services=AnyAPI_Services();assert(services);assert(services->publish("anyapi.equipment",2,&equipment_api));assert(services->publish("anyapi.gpu_draw",1,&gpu));assert(services->publish("anyhelpers.controls",1,&controls));assert(services->publish("anyhelpers.settings",1,&settings));
 auto ui=(void(*)(int32_t))GetProcAddress(fixture,"FixtureUiState");auto filter=(uint32_t(*)(const AnyInputV1*))GetProcAddress(fixture,"FixtureInput");ui(1);
 auto mod=LoadLibraryW(argv[2]);assert(mod);AnyModHostV1 host;host.log=log;host.capture_input=capture;AnyModCallbacksV1 cb;assert(((AnyModInitV1)GetProcAddress(mod,"AnyAPI_ModInit"))(&host,&cb));((void(*)())GetProcAddress(mod,"AnyAPI_ModReady"))();assert(renderer&&cb.render);
 snapshot.context=1;snapshot.count=8;for(int i=0;i<8;++i){snapshot.tools[i].item_id=i+100;strcpy_s(snapshot.tools[i].name,"Test tool");}
 auto hints=(const AnyHudHintProviderV1*)services->query("hud.hints.anyquickwheel",1);assert(hints&&hints->copy);AnyHudHintV1 hint;assert(hints->copy(&hint)&&hint.key=='Q'&&!strcmp(hint.label,"Tool wheel (hold)"));binding='R';assert(hints->copy(&hint)&&hint.key=='R');binding=0;assert(!hints->copy(&hint));binding='Q';ui(2);assert(!hints->copy(&hint));ui(1);
 AnyFrameV1 f;f.width=1920;f.height=1080;f.focused=1;AnyCanvasV1 canvas;
 auto render=[&](){f.tick=GetTickCount64();polygons=0;texts.clear();cb.render(&f,&canvas,nullptr);renderer(&f,nullptr);assert(!canvas.pixels);};
 auto input=[&](uint32_t kind,uint32_t key=0,int x=0,int y=0,int wheel=0,uint32_t button=0){AnyInputV1 e;e.kind=kind;e.key=key;e.x=x;e.y=y;e.wheel=wheel;e.button=button;return filter(&e);};
 // Prevent Windows cursor notifications from introducing physical-screen dimensions.
 auto move=[&](int x,int y){input(ANY_MOUSE_MOVE,0,x,y);};
 render();assert(!polygons);assert(input(ANY_KEY_DOWN,'Q'));render();assert(polygons==8&&captured);input(ANY_KEY_UP,'Q');render();assert(!captured&&!polygons&&!selected_calls);
 input(ANY_KEY_DOWN,'Q');move(960,390);render();input(ANY_KEY_UP,'Q');assert(selected_calls==1&&selected_item==100&&!captured);
 input(ANY_KEY_DOWN,'Q');input(ANY_KEY_DOWN,VK_ESCAPE);input(ANY_KEY_UP,'Q');assert(selected_calls==1&&!captured);
 input(ANY_KEY_DOWN,'Q');input(ANY_MOUSE_DOWN,0,0,0,0,2);input(ANY_KEY_UP,'Q');assert(selected_calls==1&&!captured);
 input(ANY_KEY_DOWN,'Q');ui(2);render();input(ANY_KEY_UP,'Q');assert(!captured&&selected_calls==1);ui(1);render();
 input(ANY_KEY_DOWN,'Q');f.focused=0;render();input(ANY_KEY_UP,'Q');assert(!captured&&selected_calls==1);f.focused=1;render();
 available=false;assert(!input(ANY_KEY_DOWN,'Q'));available=true;binding='R';assert(!input(ANY_KEY_DOWN,'Q'));input(ANY_KEY_DOWN,'R');move(960,390);input(ANY_KEY_UP,'R');assert(selected_calls==2);
 input(ANY_KEY_DOWN,'R');++snapshot.context;render();input(ANY_KEY_UP,'R');assert(!captured&&selected_calls==2);
 snapshot.count=25;for(int i=0;i<25;++i){snapshot.tools[i].item_id=i+100;strcpy_s(snapshot.tools[i].name,"Test tool");}
 input(ANY_KEY_DOWN,'R');render();assert(polygons==12);input(ANY_MOUSE_WHEEL,0,0,0,-120);render();assert(polygons==12);input(ANY_MOUSE_WHEEL,0,0,0,-120);render();assert(polygons==1);move(960,390);input(ANY_KEY_UP,'R');assert(selected_item==124&&selected_calls==3);
 // The wheel resizes live as carried tools change, with no primary-hand/part slice.
 snapshot.count=2;input(ANY_KEY_DOWN,'R');render();assert(polygons==2);snapshot.count=3;render();assert(polygons==3);snapshot.count=1;render();assert(polygons==1);input(ANY_KEY_DOWN,VK_ESCAPE);input(ANY_KEY_UP,'R');snapshot.count=0;assert(!input(ANY_KEY_DOWN,'R'));render();assert(!polygons&&!captured);
 values[ids.at("enabled")]=0;++revision;render();assert(!input(ANY_KEY_DOWN,'R')&&!captured);
 assert(!hints->copy(&hint));
 std::cout<<"Wheel rendering, selection, cancellation, menu/focus, keybinds and paging passed.\n";
}
