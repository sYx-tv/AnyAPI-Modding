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
 assert(volumetric::capture(renderer,scene,p,frame,read,2,4,false)&&frame.gpu.local_shadow_count==0&&frame.gpu.fog_color[3]>0);
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
 // ---- AnyAPI 0.40.2 (package 6) ----
 // WI-2: reads per frame through a counting reader. Lamps add two ring headers plus one read per element; the flash
 // scan adds one header, 4 bytes per omni, and a whole record only for a flash.
 {size_t calls=0,bytes=0;auto counting=[&](uintptr_t a,void* o,size_t n){++calls;bytes+=n;return read(a,o,n);};
  float a_bulb[14]={2,3,6,10,1,.5f,.25f,999},b_bulb[14]={2,3,5,10,4,2,1,999};write(lamp,a_bulb);write(lamp+56,b_bulb);write(scene+0xc50,uint32_t(0));write(scene+0xc54,uint32_t(2));write(scene+0xc58,uint32_t(2));
  cone[22]=-1;int32_t none=-1;memcpy(&cone[29],&none,4);write(spot,cone);
  assert(volumetric::capture(renderer,scene,p,frame,counting,0,4,false));size_t plain=calls;calls=0;
  assert(volumetric::capture(renderer,scene,p,frame,counting,2,4,false)&&frame.lamps.omni==2&&frame.lamps.spot==1&&frame.gpu.fog_color[3]==3);assert(calls==plain+2+3);
  float camera[3]{2,3,4};lighting::LampReader<decltype(counting)> lamps(counting);calls=bytes=0;auto none_seen=lamps.scan_flash(scene,camera);assert(!none_seen.seen&&calls==3&&bytes==24+2*4);
  // K21: lightning (radius 8192) is never a lamp; it goes to the flash record with the game's own falloff.
  float bolt[14]={2,103,4,8192,3,3,4,999};write(lamp+56,bolt);calls=0;auto flash=lamps.scan_flash(scene,camera);
  assert(flash.seen&&calls==4&&std::abs(flash.intensity-lighting::game_falloff(100,8192))<1e-6f&&flash.colour[2]==4&&std::abs(flash.direction[1]-1)<1e-6f);
  assert(volumetric::capture(renderer,scene,p,frame,read,2,4,false)&&frame.lamps.flashes==1&&frame.lamps.omni==1&&frame.gpu.fog_color[3]==2);
  for(unsigned i=0;i<2;++i)assert(frame.gpu.lights[i][3]<=2000);
  // Colour clamp: garbage up to 1e8 used to pass as a lamp.
  float hot[14]={2,3,5,10,5e7f,1,1,999};write(lamp+56,hot);assert(volumetric::capture(renderer,scene,p,frame,read,2,4,false)&&frame.gpu.lights[0][8]==lighting::MAX_COLOUR);write(lamp+56,b_bulb);}
 // Legacy selection equals the 0.40.1 stable_sort for any scores, ties included.
 {uint32_t seed=7;auto next=[&]{seed=seed*1664525u+1013904223u;return seed>>8;};
  for(int round=0;round<200;++round){std::vector<lighting::Lamp> v(next()%40);for(uint32_t i=0;i<v.size();++i){v[i].order=i;v[i].score=float(next()%16);v[i].data[0]=float(i);}
   auto old=v;std::stable_sort(old.begin(),old.end(),[](auto& a,auto& b){return a.score>b.score;});uint32_t budget=1+next()%8;old.resize(std::min<size_t>({old.size(),budget,8}));
   lighting::select_legacy(v,budget);assert(v.size()==old.size());for(size_t i=0;i<v.size();++i)assert(v[i].data[0]==old[i].data[0]);}}
 // raw[29]: the game's own shadow camera index, confirmed by one matrix compare; else the old scan.
 {spot_matrix[0]=1;write(scene+0x5c8,uint32_t(5));write(cameras+4*720+0x220,spot_matrix);write(cameras+4*720+0x1a0,spot_matrix);
  for(int32_t g:{4,3,-1,7}){memcpy(&cone[29],&g,4);write(spot,cone);assert(volumetric::capture(renderer,scene,p,frame,read,2,4));
   assert(frame.gpu.local_shadow_count==1&&frame.lamps.raw29_hits==(g==4?1u:0u)&&frame.lamps.raw29_misses==(g==3||g==7?1u:0u));}
  int32_t g=4;memcpy(&cone[29],&g,4);write(spot,cone);auto moved=spot_matrix;moved[12]=5;write(cameras+4*720+0x220,moved);
  assert(volumetric::capture(renderer,scene,p,frame,read,2,4)&&frame.gpu.local_shadow_count==0&&frame.lamps.raw29_misses==1);write(cameras+4*720+0x220,spot_matrix);}
 // K03/WI-10: view flags. Underground keeps the lamps only with the V4 options; any other flag stays inactive.
 {volumetric::CaptureOptions v4;v4.view_mask=0xfe;v4.underground_lamps=true;unsigned char under[4]{1,0,0,0};write(scene+0x668,under);
  assert(!volumetric::capture(renderer,scene,p,frame,read,2,4)&&frame.failure==2&&frame.view==1);
  assert(volumetric::capture(renderer,scene,p,frame,read,2,4,true,v4)&&frame.underground&&frame.gpu.sun[3]==0&&frame.gpu.controls[2]==0&&frame.gpu.controls[3]==0&&frame.gpu.sun_color[1]==0&&frame.gpu.fog_color[3]>0&&frame.gpu.sun_color[3]==2);
  unsigned char wet[4]{0,1,0,0};write(scene+0x668,wet);assert(!volumetric::capture(renderer,scene,p,frame,read,2,4,true,v4)&&frame.failure==2&&frame.view==2);
  unsigned char bad[4]{0,0,2,0};write(scene+0x668,bad);assert(!volumetric::capture(renderer,scene,p,frame,read,2,4,true,v4)&&frame.view==scene_frame::VIEW_UNREADABLE);write(scene+0x668,uint32_t(0));
  // K25: a lamp-only frame never reads the sun cascades.
  auto dark=p;dark.sun_shafts=0;dark.fog_strength=0;assert(volumetric::capture(renderer,scene,dark,frame,read,2,4)&&frame.gpu.sun[3]==0&&!frame.underground);
  dark.fog_strength=1;assert(volumetric::capture(renderer,scene,dark,frame,read,2,4)&&frame.gpu.sun[3]!=0);}
 // K05: gbuffer_ocean (renderer+0x208) only while m_is_ocean (scene+0xb71) is 1, RGBA16F at the colour size.
 {uintptr_t ocean=base+0x2500;write(renderer+0x208,ocean);write(ocean+0x10,uintptr_t(0x91000));write(ocean+0x28,uint32_t(4));write(ocean+0x34,uint32_t(2560));write(ocean+0x38,uint32_t(1440));write(ocean+0x50,uint32_t(10));write(ocean+0x54,uint32_t(4));
  volumetric::CaptureOptions o;o.ocean=true;write(scene+0xb71,uint8_t(1));assert(volumetric::capture(renderer,scene,p,frame,read,2,4,true,o)&&frame.ocean&&frame.ocean_target.resource==0x91000);
  assert(volumetric::capture(renderer,scene,p,frame,read)&&!frame.ocean);
  write(scene+0xb71,uint8_t(0));assert(volumetric::capture(renderer,scene,p,frame,read,2,4,true,o)&&!frame.ocean);write(scene+0xb71,uint8_t(1));
  write(ocean+0x50,uint32_t(2));assert(volumetric::capture(renderer,scene,p,frame,read,2,4,true,o)&&!frame.ocean);write(ocean+0x50,uint32_t(10));
  write(ocean+0x34,uint32_t(1280));assert(volumetric::capture(renderer,scene,p,frame,read,2,4,true,o)&&!frame.ocean);write(ocean+0x34,uint32_t(2560));write(scene+0xb71,uint8_t(0));}
 // K26/WI-5 tracker (V4 ANY_LAMP_TRACKING): hysteresis, young omnis, fades, dormancy, EMA, snap and reset.
 {using lighting::Lamp;const double origin[3]{};lighting::LampTracker t;double now=0;std::vector<Lamp> v;
  auto lamp=[](float x,float c,float score){Lamp l;l.data={x,0,0,10,0,0,0,0,c,c,c,0,-1,0,1,0};l.score=score;return l;};
  auto frame_of=[&](std::vector<Lamp> in,uint32_t budget,double dt=1/60.){now+=dt;for(uint32_t i=0;i<in.size();++i)in[i].order=i;t.select(in,budget,8,origin,now);return in;};
  for(int i=0;i<5;++i)v=frame_of({lamp(0,1,1),lamp(100,1,.9f)},1);assert(v.size()==1&&v[0].data[0]==0&&v[0].data[8]==1);
  v=frame_of({lamp(0,1,1),lamp(100,1,1.2f)},1);assert(v.size()==1&&v[0].data[0]==0);
  {std::vector<Lamp> l{lamp(0,1,1),lamp(100,1,1.2f)};lighting::select_legacy(l,1);assert(l[0].data[0]==100);}
  v=frame_of({lamp(0,1,1),lamp(100,1,2)},1);assert(v.size()==2&&v[0].data[0]==100&&v[0].data[8]<.2f&&v[1].data[0]==0&&v[1].data[8]>.8f&&v[1].data[8]<1);
  for(int i=0;i<12;++i)v=frame_of({lamp(0,1,1),lamp(100,1,2)},1);assert(v.size()==1&&v[0].data[0]==100&&v[0].data[8]==1);
  v=frame_of({lamp(100,1,2),lamp(50,1,5)},1);assert(v[0].data[0]==100);   // a young omni (muzzle flash) does not evict it
  uint16_t id=v[0].track;v=frame_of({},1,.5);v=frame_of({lamp(100,1,2)},1);assert(v[0].track==id&&v[0].data[8]<.2f);   // dormant, same track
  v=frame_of({},1,.8);v=frame_of({lamp(100,1,2)},1);assert(v[0].data[8]==1);                                          // freed: a new light
  for(int i=0;i<4;++i)v=frame_of({lamp(100,1,2)},1);v=frame_of({lamp(100,3,2)},1);assert(std::abs(t.track(v[0].track).ema[0]-(1+2*(1-std::exp(-1/60./.3))))<1e-3f);
  for(int i=0;i<5;++i)v=frame_of({lamp(0,1,1),lamp(100,1,1)},1);uint16_t first=v[0].track;float other=v[0].data[0]==0?100.f:0.f;
  v=frame_of({lamp(other,1,9),lamp(100-other,1,1)},1,.3);assert(v[0].data[0]==other&&v[0].data[8]==1&&v[0].track!=first);  // a hitch snaps the fade
  v=frame_of({lamp(7,1,1)},1,1.5);assert(t.active()==1&&v[0].data[8]==1);                                               // a long pause resets
  // +-3% alternating scores at budget 1: at most one switch in 60 frames (legacy flips every frame).
  {lighting::LampTracker h;double at=0;int switches=0;float last=-1;for(int i=0;i<60;++i){at+=1/60.;std::vector<Lamp> in{lamp(0,1,i&1?1.03f:.97f),lamp(100,1,i&1?.97f:1.03f)};in[0].order=0;in[1].order=1;h.select(in,1,8,origin,at);float x=in[0].data[0];if(last>=0&&x!=last)++switches;last=x;}assert(switches<=1);}
  // A strobe (0.25 s on, 0.25 s off) keeps its track.
  {lighting::LampTracker h;double at=0;uint16_t id=0xffff;bool kept=true;for(int i=0;i<60;++i){at+=1/60.;bool on=(i/15)%2==0;std::vector<Lamp> in;if(on)in.push_back(lamp(0,1,1));h.select(in,1,8,origin,at);if(on&&!in.empty()){if(id==0xffff)id=in[0].track;kept&=in[0].track==id;}}assert(kept);}
  std::vector<Lamp> many;for(int i=0;i<20;++i)many.push_back(lamp(float(i*20),1,float(i)));for(int i=0;i<5;++i)v=frame_of(many,8);assert(v.size()<=8);}
 // WI-4: V4 setter and status v2. V1-V3 setters reset every extra.
 {AnySceneLightingParametersV4 v4;v4.shaped=shaped;v4.shaped.lighting.scene.enabled=1;assert(set_v4(&v4));auto pol=copy_policy();
  assert(pol.v2.scene.enabled&&pol.extras.v4==1&&pol.extras.flags==15&&pol.extras.lamp_budget==8&&pol.v2.local_budget==8&&pol.v2.local_strength==2&&pol.shaping.clarity==.85f);
  v4.lamp_budget=16;assert(set_v4(&v4)&&copy_policy().extras.lamp_budget==8);v4.lamp_budget=3;assert(set_v4(&v4)&&copy_policy().v2.local_budget==3);
  v4.atmosphere_flags=ANY_ATMOS_SEA_PLANE|ANY_ATMOS_WEATHER_DENSITY|ANY_ATMOS_PHASE_CAP|ANY_ATMOS_FLASH_GLOW;v4.weather_response=1;v4.amd_path=2;assert(set_v4(&v4)&&copy_policy().extras.atmosphere_flags==120&&copy_policy().extras.amd_path==2);
  auto no=[&](auto change){auto w=v4;change(w);return !set_v4(&w);};
  assert(no([](auto& w){w.lamp_model=1;})&&no([](auto& w){w.lamp_quality=4;})&&no([](auto& w){w.lamp_budget=0;})&&no([](auto& w){w.lamp_budget=17;})&&no([](auto& w){w.lamp_shadows=3;})&&no([](auto& w){w.lamp_shape=4;}));
  assert(no([](auto& w){w.weather_response=1.5f;})&&no([](auto& w){w.weather_response=std::numeric_limits<float>::quiet_NaN();})&&no([](auto& w){w.ambient_washout=-.1f;})&&no([](auto& w){w.flags=16;})&&no([](auto& w){w.native_halos=1;})&&no([](auto& w){w.amd_path=3;}));
  assert(no([](auto& w){w.atmosphere_flags=ANY_ATMOS_SKY_FOG;})&&no([](auto& w){w.atmosphere_flags=ANY_ATMOS_SKY_INSCATTER;})&&no([](auto& w){w.atmosphere_flags=ANY_ATMOS_CLARITY_METRES;})&&no([](auto& w){w.sky_inscatter=2.5f;})&&no([](auto& w){w.reserved[3]=1;}));
  assert(no([](auto& w){w.struct_size=8;})&&no([](auto& w){w.version=3;})&&no([](auto& w){w.shaped.version=2;})&&no([](auto& w){w.shaped.lighting.local_budget=9;})&&no([](auto& w){w.shaped.clarity=2;}));
  platform::current_plugin=1;assert(!set_v4(&v4));platform::current_plugin=0;
  active=false;pol=copy_policy();assert(!pol.v2.scene.enabled&&pol.extras.v4==0);active=true;
  assert(set_v3(&shaped));pol=copy_policy();assert(pol.extras.v4==0&&pol.extras.flags==0&&pol.extras.atmosphere_flags==0&&pol.extras.amd_path==0&&pol.extras.weather_response==0&&pol.v2.local_budget==4);
  assert(set_v4(&v4)&&set(&p)&&copy_policy().extras.v4==0);auto v2=extended;v2.local_budget=4;assert(set_v4(&v4)&&set_v2(&v2)&&copy_policy().extras.flags==0);
  AnySceneLightingStatusV2 s;assert(status_v2(&s)&&s.base.available==1&&s.atmosphere_flags_supported==120&&s.lamp_budget_max==8&&s.lamp_model_max==0&&s.capture_us==-1);s.version=1;assert(!status_v2(&s));
  assert(scene_frame::find_service("anyapi.scene_lighting",4)==&api_v4&&api_v4.version==4&&api_v4.status_v2==status_v2);}
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
 std::cout<<"PASS: lighting ownership, inactive owners, bounds, native frame dimensions, projection, coordinates, special vision, invalid pointers, V3 shaping, temporal rows, lamp reads, flash, legacy order, raw29, view flags, ocean, tracker, V4 and status v2, TAA capture and camera jitter\n";
}
