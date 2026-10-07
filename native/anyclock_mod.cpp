#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_gpu_draw_v1.h"
#include "anyapi_ui_state_v1.h"
#include "anyapi_world_time_v1.h"
#include "anyhelpers_settings_v1.h"
#include "mod_controls_v1.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <string>
static AnyModHostV1 host;
static const AnyGpuDrawV1* gpu;
static const AnyUiStateV1* ui;
static const AnyWorldTimeV1* clock_api;
static const AnyHelpersSettingsV1* settings;
static const ModControlsV1* controls;
static uint64_t action,tokens[9]{},revision=UINT64_MAX,shown_at;
static double values[9]{1,4,0,100,0,100,0,85,0};
static bool held[256]{},focused,shown;
static std::mutex mutex;
static bool gameplay(){AnyUiSnapshotV1 state;return ui&&ui->copy(&state)&&state.kind==ANY_UI_GAMEPLAY;}
static void refresh(){if(!settings)return;auto next=settings->revision();if(next==revision)return;revision=next;
 const double lo[]={0,1,0,0,0,75,0,25,0},hi[]={1,15,4,100,100,150,1,100,1};
 for(int i=0;i<9;++i){AnySettingValueV1 v;if(settings->get(tokens[i],&v)&&std::isfinite(v.number))values[i]=std::clamp(v.number,lo[i],hi[i]);}}
static void draw(const AnyFrameV1* f,void*){
 if(!f)return;std::lock_guard lock(mutex);refresh();focused=f->focused!=0;
 if(!focused||!gameplay()||!values[0]){shown=false;std::fill(std::begin(held),std::end(held),false);return;}
 if(!shown||f->tick<shown_at)return;double elapsed=double(f->tick-shown_at),duration=values[1]*1000;
 if(elapsed>=duration){shown=false;return;}
 float fade=float(std::min({1.0,elapsed/140.0,(duration-elapsed)/200.0}));
 float scale=float(values[5]/100.0),w=190*scale,h=66*scale,margin=18*scale;
 w=std::min(w,float(f->width));h=std::min(h,float(f->height));
 float max_x=std::max(0.f,float(f->width)-w-margin),max_y=std::max(0.f,float(f->height)-h-margin);
 float min_x=std::min(margin,max_x),min_y=std::min(margin,max_y),x=max_x,y=min_y;
 switch(int(values[2])){case 1:x=min_x;break;case 2:y=max_y;break;case 3:x=min_x;y=max_y;break;case 4:x=min_x+float(values[3]/100)*(max_x-min_x);y=min_y+float(values[4]/100)*(max_y-min_y);break;}
 auto emit=[&](uint32_t kind,float rx,float ry,float rw,float rh,uint32_t color){AnyGpuCommandV1 c;c.kind=kind;c.rect[0]=rx;c.rect[1]=ry;c.rect[2]=rw;c.rect[3]=rh;c.color=color;c.opacity=fade;c.radius=5*scale;gpu->emit(&c);};
 AnyGpuCommandV1 bg;bg.kind=ANY_GPU_ROUND_RECT;bg.rect[0]=x;bg.rect[1]=y;bg.rect[2]=w;bg.rect[3]=h;bg.radius=5*scale;bg.color=0xff191c20;bg.opacity=fade*float(values[7]/100);gpu->emit(&bg);
 emit(ANY_GPU_ELLIPSE,x+15*scale,y+26*scale,22*scale,22*scale,0xffc6cbd0);
 // Clock face and two hands use native draw primitives rather than font symbols.
 emit(ANY_GPU_ELLIPSE,x+17*scale,y+28*scale,18*scale,18*scale,0xff191c20);
 emit(ANY_GPU_LINE,x+26*scale,y+37*scale,x+26*scale,y+31*scale,0xffe8edf2);
 emit(ANY_GPU_LINE,x+26*scale,y+37*scale,x+31*scale,y+37*scale,0xffe8edf2);
 AnyWorldTimeSnapshotV1 time;wchar_t text[48]{};
 if(clock_api&&clock_api->copy(&time)){
 unsigned hour=time.seconds_since_midnight/3600%24,minute=time.seconds_since_midnight/60%60,second=time.seconds_since_midnight%60;
 // Separate format branches keep the variadic argument types exact.
 if(values[6]&&values[8])swprintf_s(text,L"%u:%02u:%02u %s",hour%12?hour%12:12,minute,second,hour<12?L"AM":L"PM");
 else if(values[6])swprintf_s(text,L"%u:%02u %s",hour%12?hour%12:12,minute,hour<12?L"AM":L"PM");
 else if(values[8])swprintf_s(text,L"%02u:%02u:%02u",hour,minute,second);
 else swprintf_s(text,L"%02u:%02u",hour,minute);
 }else wcscpy_s(text,L"Time unavailable");
 AnyGpuCommandV1 label;label.kind=ANY_GPU_TEXT;label.rect[0]=x+48*scale;label.rect[1]=y+9*scale;label.rect[2]=w-56*scale;label.rect[3]=15*scale;label.font_size=10*scale;label.color=0xffaeb7c2;label.opacity=fade;label.text=L"GAME TIME";label.text_length=9;label.flags=ANY_GPU_NOWRAP;gpu->emit(&label);
 label.rect[1]=y+25*scale;label.rect[3]=32*scale;label.font_size=(text[0]==L'T'?14.f:values[6]&&values[8]?18.f:values[8]?20.f:24.f)*scale;label.color=0xfff5f7fa;label.text=text;label.text_length=uint32_t(wcslen(text));gpu->emit(&label);
}
static uint32_t input(const AnyInputV1* e,void*){if(!e)return 0;std::lock_guard lock(mutex);refresh();
 if(e->kind==ANY_FOCUS_LOST){shown=false;focused=false;std::fill(std::begin(held),std::end(held),false);return 0;}
 if(e->key>=256)return 0;
 if(e->kind==ANY_KEY_UP){bool captured=held[e->key];held[e->key]=false;return captured;}
 if(e->kind!=ANY_KEY_DOWN)return 0;
 auto key=controls&&action?controls->key(action):uint32_t('O');
 if(!gpu||!key||e->key!=key||!values[0]||!focused||!gameplay())return 0;
 if(!held[e->key]){held[e->key]=true;shown=true;shown_at=GetTickCount64();}return 1;
}
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* h,AnyModCallbacksV1* out){
 if(!h||!out||h->struct_size!=sizeof(*h)||h->abi!=1||out->struct_size!=sizeof(*out))return false;host=*h;out->id="anyclock";out->input=input;return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady(){auto s=AnyAPI_Services();if(!s)return;
 ui=(const AnyUiStateV1*)s->query("anyapi.ui_state",1);clock_api=(const AnyWorldTimeV1*)s->query("anyapi.world_time",1);gpu=(const AnyGpuDrawV1*)s->query("anyapi.gpu_draw",1);
 if(!ui||ui->struct_size!=sizeof(*ui)||ui->version!=1||!ui->copy||!clock_api||clock_api->struct_size!=sizeof(*clock_api)||clock_api->version!=1||!clock_api->copy||!gpu||gpu->struct_size!=sizeof(*gpu)||gpu->version!=1||!gpu->emit||!gpu->register_renderer){ui=nullptr;gpu=nullptr;if(host.log)host.log(2,"anyclock","Requires AnyAPI 0.30.0: world time, UI state and GPU drawing.");return;}
 controls=(const ModControlsV1*)s->query("anyhelpers.controls",1);
 if(controls&&controls->struct_size==sizeof(*controls)&&controls->version==1&&controls->register_action&&controls->key){ModControlActionV1 d;d.mod_id="anyclock";d.mod_name="AnyClock";d.action_id="check_time";d.label="Check game time";d.default_key='O';action=controls->register_action(&d);}else controls=nullptr;
 settings=(const AnyHelpersSettingsV1*)s->query("anyhelpers.settings",1);
 if(settings&&settings->struct_size==sizeof(*settings)&&settings->version==1&&settings->register_setting&&settings->get&&settings->revision){
 const char* ids[]={"enabled","duration","corner","position_x","position_y","size","format","opacity","seconds"};
 const char* labels[]={"Enable AnyClock","Display duration (seconds)","Screen corner","Custom horizontal position (%)","Custom vertical position (%)","Display size (%)","Time format","Background opacity (%)","Show seconds"};
 uint32_t kinds[]={ANY_SETTING_BOOL,ANY_SETTING_NUMBER,ANY_SETTING_CHOICE,ANY_SETTING_NUMBER,ANY_SETTING_NUMBER,ANY_SETTING_INTEGER,ANY_SETTING_CHOICE,ANY_SETTING_INTEGER,ANY_SETTING_BOOL};
 double lo[]={0,1,0,0,0,75,0,25,0},hi[]={1,15,4,100,100,150,1,100,1},step[]={1,.5,1,1,1,5,1,5,1};
 const char* corners[]={"Top right","Top left","Bottom right","Bottom left","Custom"};const char* formats[]={"24 hour","12 hour"};
 for(int i=0;i<9;++i){AnyModSettingV1 d;d.mod_id="anyclock";d.mod_name="AnyClock";d.setting_id=ids[i];d.label=labels[i];d.order=i;d.kind=kinds[i];d.default_number=values[i];d.minimum=lo[i];d.maximum=hi[i];d.step=step[i];if(i==2){d.choices=corners;d.choice_count=5;}if(i==6){d.choices=formats;d.choice_count=2;}if(i==3||i==4)d.description="Used when Screen corner is Custom. Move the clock away from other HUD mods.";tokens[i]=settings->register_setting(&d);}
 }else settings=nullptr;
 if(!gpu->register_renderer(draw,nullptr)){gpu=nullptr;return;}if(host.log)host.log(0,"anyclock","Ready: native game time; press O. Configure AnyClock in Mod Settings / Mod Controls.");
}
