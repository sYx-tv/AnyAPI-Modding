#pragma once
#include "volumetric_frame.h"
#include "../anyapi_scene_lighting_v1.h"
#include <cstdint>
#include <cstring>
#include <vector>
namespace volumetric {
struct NativeTarget {uintptr_t resource{};uint32_t state{},width{},height{},srv_format{},srv_dimension{};};
struct NativeFrame {Frame gpu;NativeTarget color,depth,shadow;std::array<NativeTarget,12> cascades{};uint32_t shadow_count{},failure{};};
// Exact 0.1.23 contract: native ref<T> is control block then object pointer.
// target+0x10 is the D3D12 resource, +0x28 its recorded state, +0x50 its SRV.
template<class Read> bool target(uintptr_t object,NativeTarget& out,Read read){unsigned char data[0x60]{};if(!object||!read(object,data,sizeof(data)))return false;memcpy(&out.resource,data+0x10,8);memcpy(&out.state,data+0x28,4);memcpy(&out.width,data+0x34,4);memcpy(&out.height,data+0x38,4);memcpy(&out.srv_format,data+0x50,4);memcpy(&out.srv_dimension,data+0x54,4);return out.resource&&out.width>0&&out.width<=8192&&out.height>0&&out.height<=8192&&out.srv_dimension==4;}
template<class Read> bool capture(uintptr_t renderer,uintptr_t scene,const AnySceneLightingParametersV1& p,NativeFrame& out,Read read,float local_strength=0,uint32_t local_budget=4){
 out.gpu.local_shadow_count=out.gpu.fog_color[3]=out.gpu.sun_color[3]=0;out.gpu.lights={};out.gpu.local_shadows={};
 out.failure=1;if(p.quality>3||!std::isfinite(local_strength)||local_strength<0||local_strength>15||local_budget<1||local_budget>8)return false;
 out.failure=2;unsigned char special[4]{};if(!read(scene+0x668,special,4)||special[0]||special[1]||special[2]||special[3])return false;
 out.failure=3;uintptr_t color{},depth{},shadows{},cameras{};uint32_t count{},camera_count{},shadow_stride{},camera_stride{};
 if(!read(renderer+0x3c0,&color,8)||!read(renderer+0x218,&depth,8)||!read(renderer+0xb8,&shadows,8)||!read(renderer+0xc0,&count,4)||!read(renderer+0xc4,&shadow_stride,4)||!read(scene+0x5c0,&cameras,8)||!read(scene+0x5c8,&camera_count,4)||!read(scene+0x5cc,&camera_stride,4)||shadow_stride!=16||camera_stride!=0x2d0||!count||count>32||!camera_count||camera_count>32||!shadows||!cameras)return false;
 out.failure=4;uintptr_t shadow{};if(!read(shadows+8,&shadow,8)||!target(color,out.color,read)||!target(depth,out.depth,read)||!target(shadow,out.shadow,read)||out.color.width!=out.depth.width||out.color.height!=out.depth.height||out.depth.srv_format!=41||(out.shadow.srv_format!=41&&out.shadow.srv_format!=56)||out.color.srv_format!=10)return false;
 uint32_t cascade_count{},cascade_stride{};uintptr_t radii{};
 if(!read(scene+0x608,&radii,8)||!read(scene+0x610,&cascade_count,4)||!read(scene+0x614,&cascade_stride,4)||!radii||cascade_stride!=8||!cascade_count||cascade_count>8||cascade_count>count||cascade_count>camera_count)return false;
 out.shadow_count=cascade_count;auto& f=out.gpu;double matrix[16]{},v[3]{},offset[3]{};auto vector=[&](uintptr_t addr,std::array<float,4>& destination){if(!read(addr,v,sizeof(v)))return false;for(size_t i=0;i<3;++i){if(!std::isfinite(v[i])||std::abs(v[i])>1e8)return false;destination[i]=float(v[i]);}return true;};
 out.failure=5;uintptr_t main=scene+0x20;if(!vector(main+0x108,f.camera)||!read(scene+8,offset,sizeof(offset))||!vector(main+0xa8,f.right)||!vector(main+0x90,f.up)||!vector(main+0x78,f.forward)||!vector(scene+0xa78,f.sun)||!vector(scene+0xaf0,f.sun_color)||!vector(scene+0xb08,f.fog_color)||!read(main+0x1a0,matrix,sizeof(matrix)))return false;
 out.failure=6;f.camera[3]=float(double(f.camera[1])+offset[1]);f.projection={float(matrix[10]),float(matrix[14]),float(matrix[11]),float(matrix[15])};if(std::abs(matrix[0])<1e-8||std::abs(matrix[5])<1e-8||std::abs(matrix[11])<.5||std::abs(matrix[15])>.001)return false;f.right[3]=float(1/std::abs(matrix[0]));f.up[3]=float(1/std::abs(matrix[5]));
 // Native mat44 stores columns. row_major HLSL treats those columns as rows,
 // so mul(position, copied_matrix) is the native column-vector transform.
 out.failure=7;if(!read(cameras+0x220,matrix,sizeof(matrix)))return false;for(size_t i=0;i<16;++i)f.shadow[i]=float(matrix[i]);for(uint32_t c=0;c<cascade_count;++c){uintptr_t object{};double radius{};
  if(!read(radii+c*8,&radius,8)||!std::isfinite(radius)||radius<=0||!read(shadows+c*16+8,&object,8)||!target(object,out.cascades[c],read)||(out.cascades[c].srv_format!=41&&out.cascades[c].srv_format!=56)||!read(cameras+c*0x2d0+0x220,matrix,sizeof(matrix)))return false;
  auto& destination=c?f.additional_shadows[c-1]:f.shadow;for(size_t i=0;i<16;++i)destination[i]=float(matrix[i]);
 }
 f.forward[3]=float(cascade_count);out.failure=8;float magnitude=0;for(size_t i=0;i<3;++i)magnitude+=f.sun[i]*f.sun[i];if(magnitude<.01f)return false;for(size_t i=0;i<3;++i)f.sun[i]=-f.sun[i]/std::sqrt(magnitude);if(!read(cameras+0x1a0,matrix,sizeof(matrix))||!std::isfinite(matrix[10])||std::abs(matrix[10])<1e-12)return false;f.sun[3]=matrix[10]<0?2.f:1.f;
 f.medium={p.fog_density,p.base_height,p.height_falloff,p.anisotropy};static constexpr float steps[]={16,24,32,48};f.controls={steps[p.quality],p.maximum_distance,p.sun_shafts,p.fog_strength};// The native light vectors are ring buffers, unlike the camera arrays.
 if(local_strength>0){
  struct Candidate{std::array<float,16> data{};std::array<float,16> matrix{};bool spot{};float score{};};std::vector<Candidate> lights;
  auto gather=[&](uintptr_t address,uint32_t expected,bool spot){uintptr_t buffer{};uint32_t start{},size{},capacity{},stride{};
   if(!read(address,&buffer,8)||!read(address+8,&start,4)||!read(address+12,&size,4)||!read(address+16,&capacity,4)||!read(address+20,&stride,4))return;
   if(!size)return;if(!buffer||size>4096||capacity<size||capacity>1048576||start>=capacity||stride!=expected)return;
   for(uint32_t i=0;i<size;++i){float raw[30]{};if(!read(buffer+uintptr_t((start+i)%capacity)*stride,raw,expected))continue;bool finite=true;for(uint32_t j=0;j<(spot?29u:14u);++j)finite&=std::isfinite(raw[j])&&std::abs(raw[j])<=1e8f;if(!finite)continue;
    auto position=raw+(spot?16:0),colour=raw+(spot?24:4);float radius=position[3];if(radius<=.01f||radius>2000||colour[0]<0||colour[1]<0||colour[2]<0)continue;
    Candidate c;c.spot=spot;for(unsigned j=0;j<4;++j)c.data[j]=position[j];for(unsigned j=0;j<3;++j)c.data[8+j]=colour[j];c.data[11]=spot?1.f:0.f;c.data[12]=-1;c.data[14]=1;
    float d2=0;for(unsigned j=0;j<3;++j){float d=position[j]-f.camera[j];d2+=d*d;}if(d2>(radius+p.maximum_distance)*(radius+p.maximum_distance))continue;c.score=std::max({colour[0],colour[1],colour[2]})*radius*radius/(1+d2);
    if(spot){float norm=raw[20]*raw[20]+raw[21]*raw[21]+raw[22]*raw[22];if(norm<.99f||norm>1.01f||raw[28]<0||raw[28]>=1)continue;for(unsigned j=0;j<3;++j)c.data[4+j]=raw[20+j];c.data[7]=raw[28];for(unsigned j=0;j<16;++j)c.matrix[j]=raw[j];}
    lights.push_back(c);
   }
  };gather(scene+0xc48,56,false);gather(scene+0xc70,120,true);std::stable_sort(lights.begin(),lights.end(),[](auto& a,auto& b){return a.score>b.score;});
  unsigned selected=std::min({unsigned(lights.size()),local_budget,8u}),shadow_slots=0;
  for(unsigned i=0;i<selected;++i){auto& c=lights[i];
   // Match the copied spotlight matrix to the current camera rather than
   // guessing whether a shadow index is relative to the sun-cascade count.
   if(c.spot&&shadow_slots<4)for(unsigned n=cascade_count;n<camera_count&&n<count;++n){if(!read(cameras+n*720+0x220,matrix,sizeof(matrix)))continue;bool match=true;for(unsigned j=0;j<16;++j)match&=std::abs(float(matrix[j])-c.matrix[j])<=.0001f*std::max(1.f,std::abs(c.matrix[j]));if(!match)continue;
    uintptr_t object{};NativeTarget local;if(!read(shadows+n*16+8,&object,8)||!target(object,local,read)||(local.srv_format!=41&&local.srv_format!=56))break;
    out.cascades[8+shadow_slots]=local;f.local_shadows[shadow_slots]=c.matrix;c.data[12]=float(8+shadow_slots);if(read(cameras+n*720+0x1a0,matrix,sizeof(matrix)))c.data[13]=matrix[10]<0?2.f:1.f;++shadow_slots;break;
   }
   f.lights[i]=c.data;
  }
  f.local_shadow_count=float(shadow_slots);f.fog_color[3]=float(selected);f.sun_color[3]=local_strength;
 }
 out.failure=9;if(!valid(f))return false;out.failure=0;return true;
}
}
