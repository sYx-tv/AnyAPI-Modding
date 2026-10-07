#pragma once
#include "volumetric_frame.h"
#include "../anyapi_scene_lighting_v1.h"
#include <cstdint>
#include <cstring>
namespace volumetric {
struct NativeTarget {uintptr_t resource{};uint32_t state{},width{},height{},srv_format{},srv_dimension{};};
struct NativeFrame {Frame gpu;NativeTarget color,depth,shadow;uint32_t shadow_count{},failure{};};
// Exact 0.1.23 contract: native ref<T> is control block then object pointer.
// target+0x10 is the D3D12 resource, +0x28 its recorded state, +0x50 its SRV.
template<class Read> bool target(uintptr_t object,NativeTarget& out,Read read){unsigned char data[0x60]{};if(!object||!read(object,data,sizeof(data)))return false;memcpy(&out.resource,data+0x10,8);memcpy(&out.state,data+0x28,4);memcpy(&out.width,data+0x34,4);memcpy(&out.height,data+0x38,4);memcpy(&out.srv_format,data+0x50,4);memcpy(&out.srv_dimension,data+0x54,4);return out.resource&&out.width>0&&out.width<=8192&&out.height>0&&out.height<=8192&&out.srv_dimension==4;}
template<class Read> bool capture(uintptr_t renderer,uintptr_t scene,const AnySceneLightingParametersV1& p,NativeFrame& out,Read read){
 out.failure=1;if(p.quality>3)return false;
 out.failure=2;unsigned char special[4]{};if(!read(scene+0x668,special,4)||special[0]||special[1]||special[2]||special[3])return false;
 out.failure=3;uintptr_t color{},depth{},shadows{},cameras{};uint32_t count{},camera_count{},shadow_stride{},camera_stride{};
 if(!read(renderer+0x3c0,&color,8)||!read(renderer+0x218,&depth,8)||!read(renderer+0xb8,&shadows,8)||!read(renderer+0xc0,&count,4)||!read(renderer+0xc4,&shadow_stride,4)||!read(scene+0x5c0,&cameras,8)||!read(scene+0x5c8,&camera_count,4)||!read(scene+0x5cc,&camera_stride,4)||shadow_stride!=16||camera_stride!=0x2d0||!count||count>32||!camera_count||camera_count>32||!shadows||!cameras)return false;
 out.failure=4;uintptr_t shadow{};if(!read(shadows+8,&shadow,8)||!target(color,out.color,read)||!target(depth,out.depth,read)||!target(shadow,out.shadow,read)||out.color.width!=out.depth.width||out.color.height!=out.depth.height||out.depth.srv_format!=41||(out.shadow.srv_format!=41&&out.shadow.srv_format!=56)||out.color.srv_format!=10)return false;
 out.shadow_count=std::min(count,camera_count);auto& f=out.gpu;double matrix[16]{},v[3]{},offset[3]{};auto vector=[&](uintptr_t addr,std::array<float,4>& destination){if(!read(addr,v,sizeof(v)))return false;for(size_t i=0;i<3;++i){if(!std::isfinite(v[i])||std::abs(v[i])>1e8)return false;destination[i]=float(v[i]);}return true;};
 out.failure=5;uintptr_t main=scene+0x20;if(!vector(main+0x108,f.camera)||!read(scene+8,offset,sizeof(offset))||!vector(main+0xa8,f.right)||!vector(main+0x90,f.up)||!vector(main+0x78,f.forward)||!vector(scene+0xa78,f.sun)||!vector(scene+0xaf0,f.sun_color)||!vector(scene+0xb08,f.fog_color)||!read(main+0x1a0,matrix,sizeof(matrix)))return false;
 out.failure=6;f.camera[3]=float(double(f.camera[1])+offset[1]);f.projection={float(matrix[10]),float(matrix[14]),float(matrix[11]),float(matrix[15])};if(std::abs(matrix[0])<1e-8||std::abs(matrix[5])<1e-8||std::abs(matrix[11])<.5||std::abs(matrix[15])>.001)return false;f.right[3]=float(1/std::abs(matrix[0]));f.up[3]=float(1/std::abs(matrix[5]));
 // Native mat44 stores columns. row_major HLSL treats those columns as rows,
 // so mul(position, copied_matrix) is the native column-vector transform.
 out.failure=7;if(!read(cameras+0x220,matrix,sizeof(matrix)))return false;for(size_t i=0;i<16;++i)f.shadow[i]=float(matrix[i]);out.failure=8;float magnitude=0;for(size_t i=0;i<3;++i)magnitude+=f.sun[i]*f.sun[i];if(magnitude<.01f)return false;for(size_t i=0;i<3;++i)f.sun[i]=-f.sun[i]/std::sqrt(magnitude);if(!read(cameras+0x1a0,matrix,sizeof(matrix))||!std::isfinite(matrix[10])||std::abs(matrix[10])<1e-12)return false;f.sun[3]=matrix[10]<0?2.f:1.f;
 f.medium={p.fog_density,p.base_height,p.height_falloff,p.anisotropy};static constexpr float steps[]={16,24,32,48};f.controls={steps[p.quality],p.maximum_distance,p.sun_shafts,p.fog_strength};out.failure=9;if(!valid(f))return false;out.failure=0;return true;
}
}
