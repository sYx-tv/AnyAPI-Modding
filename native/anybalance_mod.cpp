#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_gpu_draw_v1.h"
#include "anyapi_ui_state_v1.h"
#include "anyapi_creation_balance_v1.h"
#include "anyhelpers_settings_v1.h"
#include "balance_math.h"
#include <algorithm>
#include <cmath>
#include <mutex>
namespace anybalance {
static AnyModHostV1 host;static const AnyCreationBalanceV1* balance_api;
static const AnyGpuDrawV1* gpu;static const AnyUiStateV1* ui;static const AnyHelpersSettingsV1* settings;
static std::mutex mutex;static uint64_t tokens[7]{},revision=UINT64_MAX;
static double values[7]{1,100,0,1,1,1,85};
static void refresh(){if(!settings)return;auto next=settings->revision();if(next==revision)return;revision=next;
 const double lo[]={0,60,0,0,0,0,25},hi[]={1,180,1,1,1,1,100};
 for(unsigned i=0;i<7;++i){AnySettingValueV1 v;if(settings->get(tokens[i],&v)&&std::isfinite(v.number))values[i]=std::clamp(v.number,lo[i],hi[i]);}}
static void draw(const AnyFrameV1* frame,void*){
 if(!frame||!frame->width||!frame->height)return;std::lock_guard lock(mutex);refresh();
 AnyUiSnapshotV1 state;AnyCreationBalanceSnapshotV1 data;
 if(!values[0]||!frame->focused||!ui->copy(&state)||state.kind!=ANY_UI_GAMEPLAY||!balance_api->copy(&data))return;
 float scale=std::clamp(float(frame->height)/1080.f,.65f,1.75f),alpha=float(values[6]/100),size=11.f*float(values[1]/100)*scale;
 auto label=[&](float x,float y,float w,float h,const wchar_t* text,float font,uint32_t color,uint32_t flags=0){AnyGpuCommandV1 c;c.kind=ANY_GPU_TEXT;c.rect[0]=x;c.rect[1]=y;c.rect[2]=w;c.rect[3]=h;c.text=text;c.text_length=uint32_t(wcslen(text));c.font_size=font;c.color=color;c.opacity=alpha;c.flags=flags|ANY_GPU_NOWRAP;gpu->emit(&c);};
 auto line=[&](float x,float y,float x2,float y2,uint32_t color,float stroke=1.f,uint32_t flags=0){AnyGpuCommandV1 c;c.kind=ANY_GPU_LINE;c.rect[0]=x;c.rect[1]=y;c.rect[2]=x2;c.rect[3]=y2;c.color=color;c.stroke=stroke*scale;c.opacity=alpha;c.flags=flags;gpu->emit(&c);};
 auto point=[&](AnyBalanceScreenV1 p,float& x,float& y){return balance::pixel(p,frame->width,frame->height,x,y);};
 if(values[2]){for(unsigned a=0;a<8;++a)for(unsigned axis=0;axis<3;++axis){unsigned b=a^(1u<<axis);if(a>=b)continue;float x,y,x2,y2;if(point(data.corners[a],x,y)&&point(data.corners[b],x2,y2))line(x,y,x2,y2,0xff93a3b0,.8f);}}
 float x,y;if(point(data.centre,x,y)&&x>=0&&y>=0&&x<float(frame->width)&&y<float(frame->height)){
  float bx,by;if(values[4]&&point(data.base,bx,by)){line(x,y,bx,by,0xffedc56c,1,ANY_GPU_DASH);line(bx-5*scale,by,bx+5*scale,by,0xffedc56c);}
  if(values[3]){const uint32_t colors[]={0xffed7c7c,0xff89d09b,0xff7dbcf0};const wchar_t* names[]={L"X",L"Y",L"Z"};
   for(unsigned i=0;i<3;++i){float ax,ay;if(point(data.axes[i],ax,ay)){line(x,y,ax,ay,colors[i],1.5);label(ax+3*scale,ay-10*scale,20*scale,20*scale,names[i],11*scale,colors[i]);}}}
  AnyGpuCommandV1 c;c.kind=ANY_GPU_ELLIPSE;c.flags=ANY_GPU_STROKE;c.color=0xff10151a;c.stroke=4*scale;c.opacity=alpha;c.rect[0]=x-size;c.rect[1]=y-size;c.rect[2]=c.rect[3]=size*2;gpu->emit(&c);c.stroke=2*scale;c.color=0xffffd47c;gpu->emit(&c);
  line(x-size-4*scale,y,x+size+4*scale,y,0xffffd47c,1.5);line(x,y-size-4*scale,x,y+size+4*scale,0xffffd47c,1.5);
  label(std::clamp(x-75*scale,0.f,std::max(0.f,float(frame->width)-150*scale)),y-size-27*scale,150*scale,21*scale,L"CENTRE OF MASS",11*scale,0xffffdf9b,ANY_GPU_CENTER);
 }
 if(values[5]){
  float w=270*scale,h=(data.valid_fields&ANY_BALANCE_BODY_MASS?154.f:133.f)*scale;
  w=std::min(w,float(frame->width));h=std::min(h,float(frame->height));float px=std::max(0.f,float(frame->width)-w-20*scale),py=std::max(0.f,float(frame->height)-h-105*scale);
  AnyGpuCommandV1 c;c.kind=ANY_GPU_ROUND_RECT;c.rect[0]=px;c.rect[1]=py;c.rect[2]=w;c.rect[3]=h;c.radius=5*scale;c.color=0xff191f25;c.opacity=alpha*.9f;gpu->emit(&c);
  float inset=14*scale;label(px+inset,py+10*scale,w-inset*2,21*scale,L"AnyBalance",13*scale,0xffedf1f5,ANY_GPU_BOLD);
  wchar_t text[100];swprintf_s(text,L"Centre height  %.2f m",data.height_m);label(px+inset,py+37*scale,w-inset*2,21*scale,text,12*scale,0xffffd47c);
  swprintf_s(text,L"X offset  %+.2f m     Z  %+.2f m",data.offset_x_m,data.offset_z_m);label(px+inset,py+60*scale,w-inset*2,21*scale,text,11*scale,0xffced8e2);
  if(data.valid_fields&ANY_BALANCE_BODY_MASS){swprintf_s(text,data.valid_fields&ANY_BALANCE_FLUID_MASS?(data.body_count>1?L"Creation mass  %.1f kg  incl. fluid":L"Body mass  %.1f kg  incl. fluid"):(data.body_count>1?L"Creation mass  %.1f kg":L"Body mass  %.1f kg"),data.body_mass_kg);label(px+inset,py+82*scale,w-inset*2,21*scale,text,11*scale,0xffced8e2);}
  swprintf_s(text,L"Size  %.2f x %.2f m, %.2f m high",data.bounds_max.x-data.bounds_min.x,data.bounds_max.z-data.bounds_min.z,data.bounds_max.y-data.bounds_min.y);
  label(px+inset,py+(data.valid_fields&ANY_BALANCE_BODY_MASS?104.f:82.f)*scale,w-inset*2,21*scale,text,11*scale,0xffced8e2);
  label(px+inset,py+h-25*scale,w-inset*2,18*scale,L"Offsets from the build's bounds centre",9*scale,0xff929fab);
 }
}
}
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* host,AnyModCallbacksV1* out){
 if(!host||!out||host->struct_size!=sizeof(*host)||host->abi!=1||out->struct_size!=sizeof(*out))return false;anybalance::host=*host;out->id="anybalance";return true;
}
extern "C" __declspec(dllexport) void AnyAPI_ModReady(){using namespace anybalance;auto services=AnyAPI_Services();if(!services)return;
 balance_api=(const AnyCreationBalanceV1*)services->query("anyapi.creation_balance",1);gpu=(const AnyGpuDrawV1*)services->query("anyapi.gpu_draw",1);ui=(const AnyUiStateV1*)services->query("anyapi.ui_state",1);
 if(!balance_api||balance_api->struct_size!=sizeof(*balance_api)||balance_api->version!=1||!balance_api->copy||!gpu||gpu->struct_size!=sizeof(*gpu)||gpu->version!=1||!gpu->register_renderer||!gpu->emit||!ui||ui->struct_size!=sizeof(*ui)||ui->version!=1||!ui->copy){if(host.log)host.log(2,"anybalance","AnyBalance is inactive: install AnyAPI 0.33.0 or newer from the manager.");return;}
 settings=(const AnyHelpersSettingsV1*)services->query("anyhelpers.settings",1);
 if(settings&&settings->struct_size==sizeof(*settings)&&settings->version==1&&settings->register_setting&&settings->get&&settings->revision){
  const char* ids[]={"enabled","marker_size","bounds","axes","height_guide","summary","opacity"};
  const char* labels[]={"Enable AnyBalance","Marker size (%)","Show creation bounds","Show X / Y / Z axes","Show centre height guide","Show balance summary","Display opacity (%)"};
  double lo[]={0,60,0,0,0,0,25},hi[]={1,180,1,1,1,1,100};
  for(unsigned i=0;i<7;++i){AnyModSettingV1 d;d.mod_id="anybalance";d.mod_name="AnyBalance";d.setting_id=ids[i];d.label=labels[i];d.order=i;d.kind=(i==1||i==6)?ANY_SETTING_INTEGER:ANY_SETTING_BOOL;d.default_number=values[i];d.minimum=lo[i];d.maximum=hi[i];d.step=(i==1||i==6)?5:1;
   if(i==0)d.description="Appears only with Properties Tool equipped and aimed at a creation. Uses the native client physics-shape centre.";
   if(i==5)d.description="Height above the build's local lower bounds, X/Z offsets from its bounds centre, mass and size (X by Z, then height). These are not axle loads or server cargo totals.";
   tokens[i]=settings->register_setting(&d);
  }
 }else settings=nullptr;
 if(gpu->register_renderer(draw,nullptr)&&host.log)host.log(0,"anybalance","Ready: equip Properties Tool and aim at a creation. Configure AnyBalance in Mod Settings.");
}
