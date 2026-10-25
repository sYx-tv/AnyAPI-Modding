#pragma once
#include "volumetric_frame.h"
#include "lamp_capture.h"
#include "../anyapi_scene_lighting_v1.h"
#include <cstdint>
#include <cstring>
#include <vector>
namespace volumetric {
struct NativeTarget {uintptr_t resource{};uint32_t state{},width{},height{},srv_format{},srv_dimension{};};
// origin: graphics-relative positions plus origin give world positions; the
// origin moves as the player travels, so temporal history must account for it.
struct NativeFrame {Frame gpu;NativeTarget color,depth,shadow;std::array<NativeTarget,12> cascades{};uint32_t shadow_count{},failure{};double origin[3]{};
 // AnyAPI 0.40.2 (appended). view: scene+0x668..0x66b as bits 0-3 (underground, underwater, night vision, thermal),
 // 0x80000000 when unreadable. underground: a lamps-only frame. spot_maps: game shadow maps after the sun cascades.
 // ocean: gbuffer_ocean (renderer+0x208) while scene+0xb71 m_is_ocean is 1, RGBA16F at the colour size; else unset.
 uint32_t view{};bool underground{};uint32_t spot_maps{};lighting::LampStats lamps{};bool ocean{};NativeTarget ocean_target{};};
// view_mask bit i set: a set byte scene+0x668+i bypasses (0xff, the default, is the 0.40.1 rule: any flag bypasses).
// underground_lamps (V4 ANY_LAMP_UNDERGROUND, with bit 0 clear): an underground view keeps the lamps and drops the
// sun, the fog and the daylight washout. tracker (V4 ANY_LAMP_TRACKING): lamps keep identity, hysteresis and fades.
// ocean: also read gbuffer_ocean (never fails the capture).
struct CaptureOptions {uint32_t view_mask{0xff};bool underground_lamps{};lighting::LampTracker* tracker{};double now{};bool ocean{};};
// Exact 0.1.24 contract: native ref<T> is control block then object pointer.
// target+0x10 is the D3D12 resource, +0x28 its recorded state, +0x50 its SRV.
template<class Read> bool target(uintptr_t object,NativeTarget& out,Read read){unsigned char data[0x60]{};if(!object||!read(object,data,sizeof(data)))return false;memcpy(&out.resource,data+0x10,8);memcpy(&out.state,data+0x28,4);memcpy(&out.width,data+0x34,4);memcpy(&out.height,data+0x38,4);memcpy(&out.srv_format,data+0x50,4);memcpy(&out.srv_dimension,data+0x54,4);return out.resource&&out.width>0&&out.width<=8192&&out.height>0&&out.height<=8192&&out.srv_dimension==4;}
template<class Read> bool capture(uintptr_t renderer,uintptr_t scene,const AnySceneLightingParametersV1& p,NativeFrame& out,Read read,float local_strength=0,uint32_t local_budget=4,bool local_shadows=true,const CaptureOptions& o=CaptureOptions{}){
 out.gpu.local_shadow_count=out.gpu.fog_color[3]=out.gpu.sun_color[3]=0;out.gpu.lights={};out.gpu.local_shadows={};out.view=0;out.underground=false;out.spot_maps=0;out.lamps={};out.ocean=false;out.ocean_target={};
 out.failure=1;if(p.quality>3||!std::isfinite(local_strength)||local_strength<0||local_strength>15||local_budget<1||local_budget>8)return false;
 out.failure=2;unsigned char special[4]{};if(!read(scene+0x668,special,4)){out.view=0x80000000u;return false;}
 for(uint32_t i=0;i<4;++i){if(special[i]>1){out.view=0x80000000u;return false;}if(special[i])out.view|=1u<<i;}
 for(uint32_t i=0;i<4;++i)if(special[i]&&(o.view_mask>>i&1))return false;
 out.underground=(out.view&1)&&o.underground_lamps;
 out.failure=3;uintptr_t color{},depth{},shadows{},cameras{};uint32_t count{},camera_count{},shadow_stride{},camera_stride{};
 if(!read(renderer+0x3c0,&color,8)||!read(renderer+0x218,&depth,8)||!read(renderer+0xb8,&shadows,8)||!read(renderer+0xc0,&count,4)||!read(renderer+0xc4,&shadow_stride,4)||!read(scene+0x5c0,&cameras,8)||!read(scene+0x5c8,&camera_count,4)||!read(scene+0x5cc,&camera_stride,4)||shadow_stride!=16||camera_stride!=0x2d0||!count||count>32||!camera_count||camera_count>32||!shadows||!cameras)return false;
 out.failure=4;uintptr_t shadow{};if(!read(shadows+8,&shadow,8)||!target(color,out.color,read)||!target(depth,out.depth,read)||!target(shadow,out.shadow,read)||out.color.width!=out.depth.width||out.color.height!=out.depth.height||out.depth.srv_format!=41||(out.shadow.srv_format!=41&&out.shadow.srv_format!=56)||out.color.srv_format!=10)return false;
 uint32_t cascade_count{},cascade_stride{};uintptr_t radii{};
 if(!read(scene+0x608,&radii,8)||!read(scene+0x610,&cascade_count,4)||!read(scene+0x614,&cascade_stride,4)||!radii||cascade_stride!=8||!cascade_count||cascade_count>8||cascade_count>count||cascade_count>camera_count)return false;
 out.shadow_count=cascade_count;auto& f=out.gpu;double matrix[16]{},v[3]{},offset[3]{};auto vector=[&](uintptr_t addr,std::array<float,4>& destination){if(!read(addr,v,sizeof(v)))return false;for(size_t i=0;i<3;++i){if(!std::isfinite(v[i])||std::abs(v[i])>1e8)return false;destination[i]=float(v[i]);}return true;};
 out.failure=5;uintptr_t main=scene+0x20;if(!vector(main+0x108,f.camera)||!read(scene+8,offset,sizeof(offset))||!vector(main+0xa8,f.right)||!vector(main+0x90,f.up)||!vector(main+0x78,f.forward)||!vector(scene+0xa78,f.sun)||!vector(scene+0xaf0,f.sun_color)||!vector(scene+0xb08,f.fog_color)||!read(main+0x1a0,matrix,sizeof(matrix)))return false;
 out.failure=6;for(size_t i=0;i<3;++i)out.origin[i]=offset[i];f.camera[3]=float(double(f.camera[1])+offset[1]);f.projection={float(matrix[10]),float(matrix[14]),float(matrix[11]),float(matrix[15])};if(std::abs(matrix[0])<1e-8||std::abs(matrix[5])<1e-8||std::abs(matrix[11])<.5||std::abs(matrix[15])>.001)return false;f.right[3]=float(1/std::abs(matrix[0]));f.up[3]=float(1/std::abs(matrix[5]));
 // Native mat44 stores columns. row_major HLSL treats those columns as rows,
 // so mul(position, copied_matrix) is the native column-vector transform.
 out.failure=7;if(!read(cameras+0x220,matrix,sizeof(matrix)))return false;for(size_t i=0;i<16;++i)f.shadow[i]=float(matrix[i]);for(uint32_t c=0;c<cascade_count;++c){uintptr_t object{};double radius{};
  if(!read(radii+c*8,&radius,8)||!std::isfinite(radius)||radius<=0||!read(shadows+c*16+8,&object,8)||!target(object,out.cascades[c],read)||(out.cascades[c].srv_format!=41&&out.cascades[c].srv_format!=56)||!read(cameras+c*0x2d0+0x220,matrix,sizeof(matrix)))return false;
  auto& destination=c?f.additional_shadows[c-1]:f.shadow;for(size_t i=0;i<16;++i)destination[i]=float(matrix[i]);
 }
 f.forward[3]=float(cascade_count);out.failure=8;float magnitude=0;for(size_t i=0;i<3;++i)magnitude+=f.sun[i]*f.sun[i];if(magnitude<.01f)return false;for(size_t i=0;i<3;++i)f.sun[i]=-f.sun[i]/std::sqrt(magnitude);if(!read(cameras+0x1a0,matrix,sizeof(matrix))||!std::isfinite(matrix[10])||std::abs(matrix[10])<1e-12)return false;f.sun[3]=matrix[10]<0?2.f:1.f;
 f.medium={p.fog_density,p.base_height,p.height_falloff,p.anisotropy};static constexpr float steps[]={16,24,32,48};f.controls={steps[p.quality],p.maximum_distance,p.sun_shafts,p.fog_strength};// The native light vectors are ring buffers, unlike the camera arrays.
 // Lamp-only frames never use the sun cascades: shadow_result returns at once (identical output, K10/K25).
 // Underground (V4): no sun shafts, no fog, no daylight washout (sun_colour rgb 0); the lamps stay.
 if(p.sun_shafts<=0&&p.fog_strength<=0)f.sun[3]=0;
 if(out.underground){f.sun[3]=0;f.controls[2]=f.controls[3]=0;f.sun_color[0]=f.sun_color[1]=f.sun_color[2]=0;}
 out.spot_maps=count>cascade_count?count-cascade_count:0;
 if(o.ocean){unsigned char is_ocean=2;uintptr_t object{};out.ocean=read(scene+0xb71,&is_ocean,1)&&is_ocean==1&&read(renderer+0x208,&object,8)&&target(object,out.ocean_target,read)&&out.ocean_target.srv_format==10&&out.ocean_target.width==out.color.width&&out.ocean_target.height==out.color.height;if(!out.ocean)out.ocean_target={};}
 if(local_strength>0){
  static thread_local std::vector<lighting::Lamp> lamps;lighting::LampReader<Read> reader(read);reader.gather(scene,f.camera.data(),p.maximum_distance,lamps,out.lamps);
  // Fade-outs borrow at most half the budget again (2->3, 4->6, 6->8), so a low budget stays low.
  if(o.tracker){o.tracker->select(lamps,local_budget,std::min<uint32_t>(8,local_budget+std::max<uint32_t>(1,local_budget/2)),out.origin,o.now);out.lamps.tracked=o.tracker->active();}else lighting::select_legacy(lamps,local_budget);
  // Spot shadows: the first 4 drawn spots with a matching shadow camera, as before. raw[29] names the game's camera
  // (render_util: m_shadow_cameras[g_shadow_renderer_count] = the spot camera, +0x74 = that index); one matrix
  // compare confirms it, otherwise the old scan runs. Slots are numbered by camera index, so the bound set does not
  // reshuffle when the ranking does (fewer descriptor table changes, same image).
  struct Pick{uint32_t lamp,camera;NativeTarget map;float mode;};std::array<Pick,4> picks{};unsigned shadow_slots=0,limit=std::min(camera_count,count);
  auto matches=[&](unsigned n,const std::array<float,16>& m){if(!read(cameras+n*720+0x220,matrix,sizeof(matrix)))return false;bool match=true;for(unsigned j=0;j<16;++j)match&=std::abs(float(matrix[j])-m[j])<=.0001f*std::max(1.f,std::abs(m[j]));return match;};
  for(uint32_t i=0;i<lamps.size()&&local_shadows&&shadow_slots<4;++i){auto& c=lamps[i];if(!c.spot)continue;int found=-1,g=c.game_shadow;
   if(g>=int(cascade_count)&&g<int(limit)&&matches(unsigned(g),c.matrix)){found=g;++out.lamps.raw29_hits;}
   else{if(g>=0)++out.lamps.raw29_misses;for(unsigned n=cascade_count;n<limit;++n)if(matches(n,c.matrix)){found=int(n);break;}}
   if(found<0)continue;uintptr_t object{};NativeTarget local;if(!read(shadows+uintptr_t(found)*16+8,&object,8)||!target(object,local,read)||(local.srv_format!=41&&local.srv_format!=56))continue;
   float mode=0;if(read(cameras+uintptr_t(found)*720+0x1a0,matrix,sizeof(matrix)))mode=matrix[10]<0?2.f:1.f;picks[shadow_slots++]={i,uint32_t(found),local,mode};
  }
  std::sort(picks.begin(),picks.begin()+shadow_slots,[](const Pick& a,const Pick& b){return a.camera<b.camera;});
  for(unsigned k=0;k<shadow_slots;++k){auto& c=lamps[picks[k].lamp];out.cascades[8+k]=picks[k].map;f.local_shadows[k]=c.matrix;c.data[12]=float(8+k);c.data[13]=picks[k].mode;}
  for(uint32_t i=0;i<lamps.size();++i)f.lights[i]=lamps[i].data;
  out.lamps.shadowed=shadow_slots;unsigned selected=unsigned(lamps.size());
  f.local_shadow_count=float(shadow_slots);f.fog_color[3]=float(selected);f.sun_color[3]=local_strength;
 }
 out.failure=9;if(!valid(f))return false;out.failure=0;return true;
}
}
