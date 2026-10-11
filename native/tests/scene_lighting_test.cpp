#include <vector>
#include <cstring>
#include <cassert>
#include <limits>
#include <iostream>
namespace platform {static thread_local int current_plugin=0;}
static bool active=true;static bool anyapi_owner_active(int who){return active&&who==0;}
#include "../anyapi_scene_lighting_state.h"
#include "../lighting/native_frame.h"
#include "../aa/scene_taa_frame.h"
#include "../aa/scene_jitter.h"
int main(){
 using namespace scene_lighting;
 AnySceneLightingParametersV1 p;p.enabled=1;available=true;
 assert(set(&p)&&copy().enabled);AnySceneLightingParametersV2 extended;extended.scene=p;extended.local_strength=2;assert(set_v2(&extended)&&copy_v2().local_strength==2);extended.local_budget=9;assert(!set_v2(&extended));assert(set(&p)&&copy_v2().local_strength==0);platform::current_plugin=1;assert(!set(&p));platform::current_plugin=0;
 auto bad=p;bad.fog_density=std::numeric_limits<float>::quiet_NaN();assert(!set(&bad));bad=p;bad.quality=4;assert(!set(&bad));bad=p;bad.sun_shafts=15.01f;assert(!set(&bad));bad=p;bad.sun_shafts=12;assert(set(&bad));bad=p;bad.version=2;assert(!set(&bad));
 active=false;assert(!copy().enabled);active=true;available=false;assert(!copy().enabled);available=true;
 std::vector<unsigned char> memory(0x5000);uintptr_t base=0x10000;
 auto write=[&](uintptr_t addr,const auto& value){memcpy(memory.data()+addr-base,&value,sizeof(value));};
 auto read=[&](uintptr_t addr,void* out,size_t n){if(addr<base||addr-base>memory.size()||n>memory.size()-(addr-base))return false;memcpy(out,memory.data()+addr-base,n);return true;};
 uintptr_t renderer=base,scene=base+0x1000,color=base+0x2000,depth=base+0x2100,shadow=base+0x2200,refs=base+0x2300,cameras=base+0x3000;
 write(renderer+0x3c0,color);write(renderer+0x218,depth);write(renderer+0xb8,refs);write(renderer+0xc0,uint32_t(20));write(renderer+0xc4,uint32_t(16));for(unsigned i=0;i<4;++i)write(refs+i*16+8,shadow);uintptr_t radii=base+0x2400;write(scene+0x608,radii);write(scene+0x610,uint32_t(4));write(scene+0x614,uint32_t(8));for(unsigned i=0;i<4;++i)write(radii+i*8,double(10*(i+1)));write(scene+0x5c0,cameras);write(scene+0x5c8,uint32_t(4));write(scene+0x5cc,uint32_t(0x2d0));
 for(auto target:{color,depth,shadow}){write(target+0x10,uintptr_t(0x90000));write(target+0x28,uint32_t(4));write(target+0x34,uint32_t(2560));write(target+0x38,uint32_t(1440));write(target+0x50,uint32_t(target==color?10:41));write(target+0x54,uint32_t(4));}
 double position[]={2,3,4},offset[]={100,200,300},right[]={1,0,0},up[]={0,1,0},forward[]={0,0,1},sun[]={0,-1,0},light[]={1,1,1};
 write(scene+8,offset);write(scene+0x20+0x108,position);write(scene+0x20+0xa8,right);write(scene+0x20+0x90,up);write(scene+0x20+0x78,forward);write(scene+0xa78,sun);write(scene+0xaf0,light);write(scene+0xb08,light);
 double projection[16]{};projection[0]=projection[5]=projection[10]=projection[11]=1;projection[14]=-.1;write(scene+0x20+0x1a0,projection);
 double matrix[16]{};matrix[0]=matrix[5]=matrix[10]=matrix[15]=1;for(unsigned i=0;i<4;++i){write(cameras+i*0x2d0+0x220,matrix);write(cameras+i*0x2d0+0x1a0,matrix);}
 volumetric::NativeFrame frame;assert(volumetric::capture(renderer,scene,p,frame,read));assert(frame.gpu.camera[3]==203&&frame.gpu.sun[1]==1&&frame.shadow_count==4);
 assert(frame.gpu.forward[3]==4&&frame.gpu.additional_shadows[2][15]==1);
 write(scene+0x610,uint32_t(9));assert(!volumetric::capture(renderer,scene,p,frame,read));write(scene+0x610,uint32_t(4));
 assert(frame.gpu.sun[3]==1);matrix[10]=-.001;write(cameras+0x1a0,matrix);assert(volumetric::capture(renderer,scene,p,frame,read)&&frame.gpu.sun[3]==2);
 write(shadow+0x50,uint32_t(56));assert(volumetric::capture(renderer,scene,p,frame,read));write(shadow+0x50,uint32_t(28));assert(!volumetric::capture(renderer,scene,p,frame,read)&&frame.failure==4);write(shadow+0x50,uint32_t(56));
 // Wrapped native ring buffer and light priority. Radius/colour survive packing;
 // the native volume field is a culling volume, not a scattering multiplier.
 uintptr_t lamp=base+0x4500;write(scene+0xc48,lamp);write(scene+0xc50,uint32_t(1));write(scene+0xc54,uint32_t(1));write(scene+0xc58,uint32_t(2));write(scene+0xc5c,uint32_t(56));
 float bulb[14]={2,3,6,10,1,.5f,.25f,999};write(lamp+56,bulb);assert(volumetric::capture(renderer,scene,p,frame,read,2,4));assert(frame.gpu.fog_color[3]==1&&frame.gpu.sun_color[3]==2&&frame.gpu.lights[0][2]==6&&frame.gpu.lights[0][11]==0);
 write(scene+0xc5c,uint32_t(120));assert(volumetric::capture(renderer,scene,p,frame,read,2,4)&&frame.gpu.fog_color[3]==0);write(scene+0xc5c,uint32_t(56));
 float brighter[14]={2,3,5,10,4,2,1,999};write(lamp,brighter);write(scene+0xc54,uint32_t(2));assert(volumetric::capture(renderer,scene,p,frame,read,2,1)&&frame.gpu.fog_color[3]==1&&frame.gpu.lights[0][8]==4);
 assert(volumetric::capture(renderer,scene,p,frame,read,0,4)&&frame.gpu.fog_color[3]==0&&frame.gpu.local_shadow_count==0);
 // A spotlight matches its shadow camera by matrix, including reversed depth.
 write(scene+0xc54,uint32_t(0));uintptr_t spot=base+0x4700;write(scene+0xc70,spot);write(scene+0xc7c,uint32_t(1));write(scene+0xc80,uint32_t(1));write(scene+0xc84,uint32_t(120));
 float cone[30]{};double spot_matrix[16]{};spot_matrix[0]=spot_matrix[5]=spot_matrix[15]=1;spot_matrix[10]=-.001;
 for(unsigned j=0;j<16;++j)cone[j]=float(spot_matrix[j]);cone[16]=2;cone[17]=3;cone[18]=6;cone[19]=10;cone[22]=-1;cone[23]=.5f;cone[24]=cone[25]=cone[26]=1;cone[28]=.9f;write(spot,cone);
 write(scene+0x5c8,uint32_t(5));write(refs+4*16+8,shadow);write(cameras+4*720+0x220,spot_matrix);write(cameras+4*720+0x1a0,spot_matrix);
 assert(volumetric::capture(renderer,scene,p,frame,read,2,4)&&frame.gpu.local_shadow_count==1&&frame.gpu.lights[0][12]==8&&frame.gpu.lights[0][13]==2);
 spot_matrix[0]=2;write(cameras+4*720+0x220,spot_matrix);assert(volumetric::capture(renderer,scene,p,frame,read,2,4)&&frame.gpu.local_shadow_count==0&&frame.gpu.lights[0][12]==-1);
 cone[22]=0;write(spot,cone);assert(volumetric::capture(renderer,scene,p,frame,read,2,4)&&frame.gpu.fog_color[3]==0);write(scene+0x5c8,uint32_t(4));
 write(depth+0x34,uint32_t(1280));assert(!volumetric::capture(renderer,scene,p,frame,read));write(depth+0x34,uint32_t(2560));
 write(scene+0x668,uint32_t(1));assert(!volumetric::capture(renderer,scene,p,frame,read));write(scene+0x668,uint32_t(0));
 write(scene+0x5c8,uint32_t(33));assert(!volumetric::capture(renderer,scene,p,frame,read));write(scene+0x5c8,uint32_t(4));write(scene+0x5cc,uint32_t(0x2d0));
 write(scene+0x5cc,uint32_t(16));assert(!volumetric::capture(renderer,scene,p,frame,read)&&frame.failure==3);write(scene+0x5cc,uint32_t(0x2d0));
 write(refs+8,uintptr_t(0xdeadbeef));assert(!volumetric::capture(renderer,scene,p,frame,read));
 write(refs+8,shadow);
 // V3 shaping is validated and owned like V1/V2; older setters reset it to the original look.
 AnySceneLightingParametersV3 shaped;shaped.lighting=extended;shaped.lighting.local_budget=4;assert(set_v3(&shaped)&&copy_shaping().clarity==.85f&&copy_shaping().beam_reach==120&&copy_shaping().temporal==1&&copy_v2().local_strength==2);
 auto wrong=shaped;wrong.clarity=1.5f;assert(!set_v3(&wrong));wrong=shaped;wrong.beam_reach=5;assert(!set_v3(&wrong));wrong=shaped;wrong.beam_reach=0;assert(set_v3(&wrong));wrong=shaped;wrong.temporal=2;assert(!set_v3(&wrong));wrong=shaped;wrong.sun_response=2;assert(!set_v3(&wrong));wrong=shaped;wrong.lighting.version=1;assert(!set_v3(&wrong));wrong=shaped;wrong.version=2;assert(!set_v3(&wrong));
 platform::current_plugin=1;assert(!set_v3(&shaped));platform::current_plugin=0;assert(set(&p)&&copy_shaping().clarity==0&&copy_shaping().temporal==0);
 // The graphics origin travels with the frame for temporal reprojection.
 assert(volumetric::capture(renderer,scene,p,frame,read)&&frame.origin[0]==100&&frame.origin[1]==200&&frame.origin[2]==300);
 // Temporal rows: zeros are the original look; history needs unit axes; controls are bounded.
 {volumetric::Frame f;assert(volumetric::valid(f));f.temporal[1]={.85f,120,1,1,1,.9f};assert(volumetric::valid(f));f.temporal[1][5]=.99f;assert(!volumetric::valid(f));f.temporal[1][5]=.9f;f.temporal[1][1]=3000;assert(!volumetric::valid(f));f.temporal[1][1]=120;f.temporal[1][2]=.5f;assert(!volumetric::valid(f));f.temporal[1][2]=1;
  f.temporal[0][3]=1;assert(!volumetric::valid(f));f.temporal[0]={1,2,3,1,1,0,0,1,0,1,0,1,0,0,1,7};assert(volumetric::valid(f));f.temporal[0][7]=0;assert(!volumetric::valid(f));}
 // TAA reads depth and the world-space camera at the pre-HUD boundary.
 {scene_taa::Capture c;assert(scene_taa::capture(renderer,scene,c,read)&&c.camera.position[0]==102&&c.camera.position[1]==203&&c.camera.position[2]==304&&c.camera.right[3]==1&&c.camera.up[3]==1&&c.depth.width==2560&&c.camera.projection[2]==1);
  write(scene+0x668,uint32_t(1));assert(!scene_taa::capture(renderer,scene,c,read)&&c.failure==1);write(scene+0x668,uint32_t(0));
  write(depth+0x50,uint32_t(10));assert(!scene_taa::capture(renderer,scene,c,read)&&c.failure==2);write(depth+0x50,uint32_t(41));
  double tilted[]={0,.5,1};write(scene+0x20+0x78,tilted);assert(!scene_taa::capture(renderer,scene,c,read)&&c.failure==4);write(scene+0x20+0x78,forward);}
 // Jitter shifts clip x/y by a subpixel amount, never accumulates on a stale
 // copy, and refuses a view-projection that is not the main camera's.
 {auto at=[&](uintptr_t a){return memory.data()+(a-base);};unsigned char* scene_bytes=at(scene);write(scene+0x20+0x220,projection);scene_jitter::State js;
  auto p8=[&]{double m[16];memcpy(m,at(scene+0x20+0x1a0),sizeof(m));return m[8];};auto vp8=[&]{double m[16];memcpy(m,at(scene+0x20+0x220),sizeof(m));return m[8];};
  double x,y;scene_jitter::offset(0,x,y);assert(std::abs(x)<=.5&&std::abs(y)<=.5);
  assert(scene_jitter::apply(scene_bytes,1000,500,0,js));assert(std::abs(p8()-x*2/1000)<1e-12&&std::abs(vp8()-x*2/1000)<1e-12);
  scene_jitter::offset(1,x,y);assert(scene_jitter::apply(scene_bytes,1000,500,1,js));assert(std::abs(p8()-x*2/1000)<1e-12);
  write(scene+0x20+0x1a0,projection);write(scene+0x20+0x220,projection);scene_jitter::offset(2,x,y);assert(scene_jitter::apply(scene_bytes,1000,500,2,js)&&std::abs(p8()-x*2/1000)<1e-12);
  write(scene+0x20+0x1a0,projection);double sideways[16]{};sideways[0]=sideways[5]=sideways[10]=1;sideways[3]=1;write(scene+0x20+0x220,sideways);assert(!scene_jitter::apply(scene_bytes,1000,500,3,js)&&p8()==0);
  write(scene+0x20+0x220,projection);write(scene+0x668,uint32_t(1));assert(!scene_jitter::apply(scene_bytes,1000,500,3,js));write(scene+0x668,uint32_t(0));write(scene+0x20+0x220,matrix);}
 std::cout<<"PASS: lighting ownership, inactive owners, bounds, native frame dimensions, projection, coordinates, special vision, invalid pointers, V3 shaping, temporal rows, TAA capture and camera jitter\n";
}
