#include <vector>
#include <cstring>
#include <cassert>
#include <limits>
#include <iostream>
namespace platform {static thread_local int current_plugin=0;}
static bool active=true;static bool anyapi_owner_active(int who){return active&&who==0;}
#include "../anyapi_scene_lighting_state.h"
#include "../lighting/native_frame.h"
int main(){
 using namespace scene_lighting;
 AnySceneLightingParametersV1 p;p.enabled=1;available=true;
 assert(set(&p)&&copy().enabled);platform::current_plugin=1;assert(!set(&p));platform::current_plugin=0;
 auto bad=p;bad.fog_density=std::numeric_limits<float>::quiet_NaN();assert(!set(&bad));bad=p;bad.quality=4;assert(!set(&bad));bad=p;bad.version=2;assert(!set(&bad));
 active=false;assert(!copy().enabled);active=true;available=false;assert(!copy().enabled);available=true;
 std::vector<unsigned char> memory(0x5000);uintptr_t base=0x10000;
 auto write=[&](uintptr_t addr,const auto& value){memcpy(memory.data()+addr-base,&value,sizeof(value));};
 auto read=[&](uintptr_t addr,void* out,size_t n){if(addr<base||addr-base>memory.size()||n>memory.size()-(addr-base))return false;memcpy(out,memory.data()+addr-base,n);return true;};
 uintptr_t renderer=base,scene=base+0x1000,color=base+0x2000,depth=base+0x2100,shadow=base+0x2200,refs=base+0x2300,cameras=base+0x3000;
 write(renderer+0x3c0,color);write(renderer+0x218,depth);write(renderer+0xb8,refs);write(renderer+0xc4,uint32_t(4));write(refs+8,shadow);write(scene+0x5c0,cameras);write(scene+0x5cc,uint32_t(4));
 for(auto target:{color,depth,shadow}){write(target+0x10,uintptr_t(0x90000));write(target+0x28,uint32_t(4));write(target+0x34,uint32_t(2560));write(target+0x38,uint32_t(1440));write(target+0x50,uint32_t(target==color?10:41));write(target+0x54,uint32_t(4));}
 double position[]={2,3,4},offset[]={100,200,300},right[]={1,0,0},up[]={0,1,0},forward[]={0,0,1},sun[]={0,-1,0},light[]={1,1,1};
 write(scene+8,offset);write(scene+0x20+0x108,position);write(scene+0x20+0xa8,right);write(scene+0x20+0x90,up);write(scene+0x20+0x78,forward);write(scene+0xa78,sun);write(scene+0xaf0,light);write(scene+0xb08,light);
 double projection[16]{};projection[0]=projection[5]=projection[10]=projection[11]=1;projection[14]=-.1;write(scene+0x20+0x1a0,projection);
 double matrix[16]{};matrix[0]=matrix[5]=matrix[10]=matrix[15]=1;write(cameras+0x220,matrix);
 volumetric::NativeFrame frame;assert(volumetric::capture(renderer,scene,p,frame,read));assert(frame.gpu.camera[3]==203&&frame.gpu.sun[1]==1&&frame.shadow_count==4);
 write(depth+0x34,uint32_t(1280));assert(!volumetric::capture(renderer,scene,p,frame,read));write(depth+0x34,uint32_t(2560));
 write(scene+0x668,uint32_t(1));assert(!volumetric::capture(renderer,scene,p,frame,read));write(scene+0x668,uint32_t(0));
 write(scene+0x5cc,uint32_t(33));assert(!volumetric::capture(renderer,scene,p,frame,read));write(scene+0x5cc,uint32_t(4));
 write(refs+8,uintptr_t(0xdeadbeef));assert(!volumetric::capture(renderer,scene,p,frame,read));
 std::cout<<"PASS: lighting ownership, inactive owners, bounds, native frame dimensions, projection, coordinates, special vision and invalid pointers\n";
}
