#define NOMINMAX
#define ANYAPI_SCENE_CONTROLS_TEST
#include <windows.h>
#include <vector>
#include <mutex>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cassert>
#include <iostream>
#include <string>
namespace platform {static thread_local int current_plugin=0;}
static bool active=true;static bool anyapi_owner_active(int owner){return active&&owner==0;}
#include "../anyapi_scene_controls_state.h"
#include "../anyapi_scene_antialiasing_state.h"
#include "../anyapi_scene_effects_state.h"
static bool safe_read_memory(uintptr_t a,void* p,size_t n){MEMORY_BASIC_INFORMATION m{};if(!VirtualQuery((void*)a,&m,sizeof(m))||m.State!=MEM_COMMIT||(m.Protect&(PAGE_NOACCESS|PAGE_GUARD))||a+n>uintptr_t(m.BaseAddress)+m.RegionSize)return false;memcpy(p,(void*)a,n);return true;}
static std::vector<uintptr_t> scan_exact(const unsigned char*,size_t){return {};}
static constexpr int ANY_LOG_INFO=0,ANY_LOG_WARN=1;static void log_line(int,const char*,const char*){}
#include "../anyapi_scene_controls.inc"
static unsigned boundary_calls{};static void boundary(void*,void*){++boundary_calls;}
static unsigned renders{},scenes{};static bool raise_error{},expect_details{};
static void render(void* r){++renders;auto b=(unsigned char*)r;assert(b[0x638]==0&&b[0x63a]==1);if(expect_details)assert(b[0x63e]==0&&b[0x63f]==0&&b[0x640]==1);double v;memcpy(&v,b+0x650,8);assert(std::abs(v-.4)<.00001);if(raise_error)RaiseException(0xe0010001,0,0,nullptr);}
static bool tinted{};static double wind_seen[2]{};
static void scene(void*,void* s){++scenes;double v;memcpy(&v,(unsigned char*)s+0xaf0,8);memcpy(wind_seen,(unsigned char*)s+0xb78,16);if(v>=0&&!tinted)assert(v==4);}
static bool catch_render(void* r){__try{scene_controls::renderer_hook(r);return false;}__except(EXCEPTION_EXECUTE_HANDLER){return true;}}
int main(){using namespace scene_controls;AnySceneParametersV1 p;p.enabled=1;p.aa=1;p.bloom=2;p.bloom_intensity=.4f;p.sun=2;p.light_exposure=1;p.fog=.5f;assert(set(&p));ready=true;
 auto bad=p;bad.sun=NAN;assert(!set(&bad));bad=p;bad.aa=3;assert(!set(&bad));platform::current_plugin=-1;assert(!set(&p));platform::current_plugin=1;assert(!set(&p));platform::current_plugin=0;
 alignas(8) unsigned char renderer[0x680]{};renderer[0x638]=renderer[0x63e]=renderer[0x63f]=1;double threshold=.8,intensity=.1;memcpy(renderer+0x648,&threshold,8);memcpy(renderer+0x650,&intensity,8);unsigned char baseline[0x680];memcpy(baseline,renderer,sizeof(renderer));
 void* cell=(void*)render;assert(renderer_cell.install(&cell,(void*)renderer_hook));renderer_hook(renderer);assert(renders==1&&!memcmp(renderer,baseline,sizeof(renderer)));raise_error=true;assert(catch_render(renderer));assert(!memcmp(renderer,baseline,sizeof(renderer)));raise_error=false;
 AnySceneParametersV2 v2;v2.scene=p;v2.clouds=v2.grass=1;v2.foliage=2;assert(set_v2(&v2));auto invalid_v2=v2;invalid_v2.grass=3;assert(!set_v2(&invalid_v2));invalid_v2=v2;invalid_v2.version=1;assert(!set_v2(&invalid_v2));platform::current_plugin=1;assert(!set_v2(&v2));platform::current_plugin=0;
 expect_details=true;renderer_hook(renderer);assert(!memcmp(renderer,baseline,sizeof(renderer)));raise_error=true;assert(catch_render(renderer));assert(!memcmp(renderer,baseline,sizeof(renderer)));raise_error=false;expect_details=false;
 active=false;assert((!snapshot().scene.enabled&&snapshot().details==std::array<uint32_t,3>{}));active=true;assert(set(&p));assert((snapshot().details==std::array<uint32_t,3>{}));
 alignas(8) unsigned char data[0x2bf8]{};double one=1;for(size_t off=0xa90;off<0xb38;off+=8)memcpy(data+off,&one,8);memcpy(data+0xb60,&one,8);void* other=(void*)scene;assert(scene_cell.install(&other,(void*)scene_hook));scene_hook(renderer,data);double result;memcpy(&result,data+0xb60,8);assert(result==.5&&scenes==1);
 double invalid=-1;memcpy(data+0xaf0,&invalid,8);auto rejected=rejected_frames.load();scene_hook(renderer,data);assert(rejected_frames==rejected+1);memcpy(&result,data+0xaf0,8);assert(result==-1);
 {AnySceneParametersV3 v3;v3.detailed.scene=p;v3.key_tint[0]=1.2f;v3.key_tint[2]=.8f;v3.shade_tint[2]=1.2f;v3.wind_scale=2;assert(set_v3(&v3));
  auto bad3=v3;bad3.wind_scale=5;assert(!set_v3(&bad3));bad3=v3;bad3.key_tint[1]=NAN;assert(!set_v3(&bad3));bad3=v3;bad3.shade_tint[0]=2;assert(!set_v3(&bad3));
  alignas(8) unsigned char d3[0x2bf8]{};for(size_t off=0xa90;off<0xb38;off+=8)memcpy(d3+off,&one,8);memcpy(d3+0xb60,&one,8);double wind[2]{3,-4};memcpy(d3+0xb78,wind,16);
  tinted=true;scene_hook(renderer,d3);tinted=false;assert(wind_seen[0]==6&&wind_seen[1]==-8);double after[2];memcpy(after,d3+0xb78,16);assert(after[0]==3&&after[1]==-4);
  double sun[3],sky[3];memcpy(sun,d3+0xaf0,24);memcpy(sky,d3+0xb08,24);assert(sun[0]>sun[1]&&sun[1]>sun[2]&&sky[2]>sky[0]);
  assert(std::abs(.2126*sun[0]+.7152*sun[1]+.0722*sun[2]-4)<1e-9);
  assert(set_v2(&v2));tinted=true;scene_hook(renderer,d3);tinted=false;memcpy(after,d3+0xb78,16);assert(wind_seen[0]==3&&after[0]==3);}
 active=false;assert(!copy().enabled);active=true;ready=false;assert(!copy().enabled);ready=true;p.enabled=0;assert(set(&p)&&!copy().enabled);
 alignas(8) unsigned char caller[80]{};void* target=(void*)render;memcpy(caller+40,&target,sizeof(target));assert(resolve_cell((uintptr_t)caller,32,1,RENDER_TARGET,sizeof(RENDER_TARGET))==nullptr);
 // Hundreds of identical wrapper bodies must not prevent resolution. Verify
 // the callee, deduplicate shared cells, and fail closed on distinct valid cells.
 alignas(8) unsigned char body[sizeof(RENDER_TARGET)];memcpy(body,RENDER_TARGET,sizeof(body));void* reviewed=body;void* distinct=body;
 alignas(8) unsigned char match[80]{},duplicate[80]{},ambiguous[80]{};
 void** reviewed_cell=&reviewed;void** distinct_cell=&distinct;memcpy(match+40,&reviewed_cell,8);memcpy(duplicate+40,&reviewed_cell,8);memcpy(ambiguous+40,&distinct_cell,8);
 std::vector<uintptr_t> wrappers(424,(uintptr_t)caller);wrappers.push_back((uintptr_t)match);
 assert(resolve_unique_cell(wrappers,32,1,RENDER_TARGET,sizeof(RENDER_TARGET))==reviewed_cell);
 wrappers.push_back((uintptr_t)duplicate);assert(resolve_unique_cell(wrappers,32,1,RENDER_TARGET,sizeof(RENDER_TARGET))==reviewed_cell);
 wrappers.push_back((uintptr_t)ambiguous);assert(!resolve_unique_cell(wrappers,32,1,RENDER_TARGET,sizeof(RENDER_TARGET)));
 body[sizeof(body)-1]^=1;assert(!resolve_unique_cell({(uintptr_t)match},32,1,RENDER_TARGET,sizeof(RENDER_TARGET)));
 void* boundary_cell=(void*)boundary;assert(pre_hud_cell.install(&boundary_cell,(void*)pre_hud_hook));pre_hud_hook(renderer,data);assert(boundary_calls==1&&before_hud_calls==1);assert(pre_hud_cell.remove());
 AnySceneStatusV1 status;assert(scene_controls::status(&status)&&status.renderer_calls==4&&status.scene_calls==4);assert(renderer_cell.remove()&&scene_cell.remove());
 AnySceneAntialiasingParametersV1 aa;aa.enabled=1;scene_antialiasing::available=true;assert(scene_antialiasing::set(&aa)&&scene_antialiasing::copy().enabled);auto aa_bad=aa;aa_bad.quality=4;assert(!scene_antialiasing::set(&aa_bad));aa_bad=aa;aa_bad.method=3;assert(!scene_antialiasing::set(&aa_bad));platform::current_plugin=1;assert(!scene_antialiasing::set(&aa));platform::current_plugin=-1;assert(!scene_antialiasing::set(&aa));platform::current_plugin=0;active=false;assert(!scene_antialiasing::copy().enabled);active=true;scene_antialiasing::available=false;assert(!scene_antialiasing::copy().enabled);scene_antialiasing::available=true;aa.enabled=0;assert(scene_antialiasing::set(&aa)&&!scene_antialiasing::copy().enabled);AnySceneAntialiasingStatusV1 aas;assert(scene_antialiasing::status(&aas));aas.version=2;assert(!scene_antialiasing::status(&aas));
 AnySceneAntialiasingParametersV2 taa;taa.enabled=1;taa.method=4;taa.sharpening=.5f;assert(scene_antialiasing::set_v2(&taa)&&scene_antialiasing::copy().method==4&&scene_antialiasing::copy().sharpening==.5f);auto taa_bad=taa;taa_bad.method=5;assert(!scene_antialiasing::set_v2(&taa_bad));taa_bad=taa;taa_bad.sharpening=1.5f;assert(!scene_antialiasing::set_v2(&taa_bad));taa_bad=taa;taa_bad.version=1;assert(!scene_antialiasing::set_v2(&taa_bad));platform::current_plugin=1;assert(!scene_antialiasing::set_v2(&taa));platform::current_plugin=0;aa.enabled=1;assert(scene_antialiasing::set(&aa)&&scene_antialiasing::copy().method==1&&scene_antialiasing::copy().sharpening==0);
 // Scene effects policy: bounds, single owner, unavailable bypass.
 AnySceneEffectsParametersV1 fx;fx.enabled=1;assert(scene_effects::set(&fx)&&!scene_effects::copy().enabled);scene_effects::available=true;assert(scene_effects::copy().enabled&&scene_effects::copy().tonemap==1);{auto soft=fx;soft.tonemap=3;soft.look=6;assert(scene_effects::set(&soft));assert(scene_effects::set(&fx));}
 for(auto change:{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17}){auto bad=fx;if(change==17)bad.hide_ui=2;if(change==16)bad.clouds=2.5f;if(change==15)bad.draw_distance=.5f;if(change==13)bad.depth_of_field=4.5f;if(change==14)bad.motion_blur=NAN;if(change==12)bad.bounce_light=3;if(change==11)bad.contact_length=0;if(change==9)bad.stars=3;if(change==10)bad.night_sky=2;if(change==6)bad.ao_radius=5;if(change==7)bad.ao_strength=NAN;if(change==8)bad.ambient_occlusion=2;if(change==0)bad.tonemap=4;if(change==1)bad.look=7;if(change==2)bad.exposure=NAN;if(change==3)bad.bloom=2;if(change==4)bad.adaptation_seconds=0;if(change==5)bad.version=2;assert(!scene_effects::set(&bad));}
 platform::current_plugin=1;assert(!scene_effects::set(&fx));platform::current_plugin=0;active=false;assert(!scene_effects::copy().enabled);active=true;
 AnySceneEffectsStatusV1 fx_status;assert(scene_effects::status(&fx_status)&&fx_status.available&&!fx_status.ready&&fx_status.gpu_ms<0&&!fx_status.ao_ready&&fx_status.ao_ms<0&&!fx_status.sky_ready&&fx_status.sky_ms<0&&!fx_status.contact_ready&&fx_status.contact_ms<0&&!fx_status.gi_ready&&fx_status.gi_ms<0&&!fx_status.lens_ready&&fx_status.lens_ms<0);fx_status.version=2;assert(!scene_effects::status(&fx_status));
 // Scene effects v2: the v1 policy plus ocean reflections and glint; bounds, ownership, and a v1 set turns the water off.
 {AnySceneEffectsParametersV2 wet;wet.effects=fx;wet.water_reflections=1;wet.sun_glint=1.5f;wet.reflection_quality=2;assert(scene_effects::set_v2(&wet));auto w=scene_effects::copy_water();assert(w.reflections==1&&w.glint==1.5f&&w.quality==2&&scene_effects::copy().enabled&&scene_effects::copy().tonemap==1);
  for(int change=0;change<10;++change){auto bad=wet;if(change==0)bad.water_reflections=1.5f;if(change==1)bad.sun_glint=2.5f;if(change==2)bad.reflection_quality=3;if(change==3)bad.water_reflections=NAN;if(change==4)bad.version=1;if(change==5)bad.struct_size=sizeof(bad)-4;if(change==6)bad.effects.version=2;if(change==7)bad.effects.tonemap=4;if(change==8)bad.sun_glint=-1;if(change==9)bad.effects.struct_size=0;assert(!scene_effects::set_v2(&bad));}
  platform::current_plugin=1;assert(!scene_effects::set_v2(&wet));platform::current_plugin=-1;assert(!scene_effects::set_v2(&wet));platform::current_plugin=0;
  active=false;assert(scene_effects::copy_water().reflections==0);active=true;scene_effects::available=false;assert(scene_effects::copy_water().glint==0);scene_effects::available=true;
  assert(scene_effects::set(&fx));w=scene_effects::copy_water();assert(w.reflections==0&&w.glint==0);
  assert(scene_effects::set_v2(&wet));auto off=wet;off.effects.enabled=0;assert(scene_effects::set_v2(&off)&&scene_effects::copy_water().reflections==0);
  AnySceneEffectsStatusV2 ws;assert(scene_effects::status_v2(&ws)&&ws.effects.available&&ws.effects.version==1&&!ws.water_ready&&ws.water_ms<0&&!ws.water_frames);ws.version=1;assert(!scene_effects::status_v2(&ws));
  assert(scene_effects::api_v2.struct_size==sizeof(AnySceneEffectsV2)&&scene_effects::api_v2.version==2&&scene_effects::api_v2.set==scene_effects::set_v2&&scene_effects::api_v2.status==scene_effects::status);}
 // Scene controls v4 (0.40.2): validation and V1-V3 resets; frame publishing (sample, clock, weather, moon);
 // tint targets; time-shaped palettes; moonlight; storm fog keep; K04 counter; K12 seam order; GL-11; P0 literal sites.
 {using namespace scene_frame;tinted=true;auto q=p;q.enabled=1;q.sun=q.sky=q.ambient=q.fog=1;q.light_exposure=0;q.aa=q.bloom=0;
  AnySceneParametersV4 v4;v4.looked.detailed.scene=q;v4.looked.key_tint[0]=1.2f;v4.looked.key_tint[2]=.8f;v4.looked.shade_tint[0]=.8f;v4.looked.shade_tint[2]=1.2f;assert(v4.tint_targets==1&&set_v4(&v4));
  for(int change=0;change<13;++change){auto bad=v4;if(change==0)bad.moon_mode=2;if(change==1)bad.shadow_split=.95f;if(change==2)bad.shadow_steps=7200;if(change==3)bad.grass_far=20;if(change==4)bad.reserved[3]=1;if(change==5)bad.moonlight=.031f;
   if(change==6)bad.golden_key[1]=1.6f;if(change==7)bad.moon_phase_floor=NAN;if(change==8)bad.tint_targets=2;if(change==9)bad.version=3;if(change==10)bad.looked.version=4;if(change==11)bad.struct_size=sizeof(bad)-4;if(change==12)bad.weather_fog_keep=2;assert(!set_v4(&bad));}
  platform::current_plugin=1;assert(!set_v4(&v4));platform::current_plugin=-1;assert(!set_v4(&v4));platform::current_plugin=0;
  {auto all=v4;all.time_shaped=1;all.moon_mode=1;all.moonlight=.012f;all.weather_fog_keep=1;assert(set_v4(&all)&&snapshot().extras.moon_mode==1&&snapshot().extras.palette[MOON_SHADE][2]==1);
   AnySceneParametersV3 v3=all.looked;assert(set_v3(&v3));auto x=snapshot().extras;assert(x.tint_targets==0&&x.time_shaped==0&&x.moon_mode==0&&x.moonlight==0&&x.weather_fog_keep==0);
   assert(set_v4(&all)&&set_v2(&v2)&&snapshot().extras.moon_mode==0);assert(set_v4(&all)&&set(&q)&&snapshot().extras.time_shaped==0&&snapshot().extras.tint_targets==0);assert(set_v4(&v4));}
  assert(api_v4.struct_size==sizeof(AnySceneControlsV4)&&api_v4.version==4&&api_v4.set==set_v4&&api_v4.status==scene_controls::status&&api_v4.status_v2==status_v2&&find_service("anyapi.scene_controls",4)==&api_v4&&!find_service("anyapi.scene_controls",3));
  // Scene buffer: seven lights, base fog, cycle, sun direction (stored pointing away from the sun).
  auto d=(unsigned char*)VirtualAlloc(nullptr,0x3000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(d);auto fill=[&](double light,double t,const double* away,double fog){memset(d,0,0x2bf8);for(size_t off=0xa90;off<0xb38;off+=8)memcpy(d+off,&light,8);memcpy(d+0xb60,&fog,8);memcpy(d+0xba8,&t,8);if(away)memcpy(d+0xa78,away,24);double planet=-4096;memcpy(d+0xb38,&planet,8);};
  auto frame=[&]{board.frame.fetch_add(1);scene_hook(renderer,d);};
  auto lights=[&](size_t i,double c[3]){memcpy(c,d+0xa90+i*24,24);};
  const double L=celestial::LATITUDE;double day[3]{-std::cos(L)*std::sin(2*celestial::PI*.3),std::cos(L)*std::cos(2*celestial::PI*.3),std::sin(L)},parked[3]{0,-celestial::NOON_Y,std::sin(L)};
  // Tint targets 1: key on main and back, shade on light down and sky, 0-2 untouched, luminance kept.
  fill(1,.3,day,.01);frame();double c[3];for(size_t i=0;i<3;++i){lights(i,c);assert(c[0]==1&&c[1]==1&&c[2]==1);}
  for(size_t i:{4,6}){lights(i,c);assert(c[0]>c[1]&&c[1]>c[2]&&std::abs(luma(c)-1)<1e-9);}for(size_t i:{3,5}){lights(i,c);assert(c[2]>c[1]&&c[1]>c[0]&&std::abs(luma(c)-1)<1e-9);}
  assert(tint_targets_active==1);
  // Tint targets 0 under V4 equals the V3 mapping byte for byte.
  {auto v0=v4;v0.tint_targets=0;assert(set_v4(&v0));fill(1,.3,day,.01);frame();unsigned char a4[0xa8];memcpy(a4,d+0xa90,0xa8);
   AnySceneParametersV3 v3=v4.looked;assert(set_v3(&v3));fill(1,.3,day,.01);frame();assert(!memcmp(a4,d+0xa90,0xa8));lights(0,c);assert(c[0]>c[2]);assert(set_v4(&v4));}
  // K16 clock: calibrated on a day frame, then the parked night; the published sample is native (before scaling).
  fill(1,.3,day,.01);frame();Clock k{};assert(board.clock.load(k)&&fresh(k)&&k.source==1&&!k.parked&&std::abs(k.sun_y-.25)<1e-9&&k.night==0&&frame_state.clock.cal.done);
  Sample sm{};assert(board.sample.load(sm)&&fresh(sm)&&(sm.valid&(SAMPLE_CYCLE|SAMPLE_FOG|SAMPLE_LIGHTS|SAMPLE_SUN|SAMPLE_WIND|SAMPLE_FLAGS|SAMPLE_VIEW))==127&&sm.cycle==.3&&sm.native_fog==.01&&sm.view==0);
  fill(.0002,.9,parked,.01);frame();assert(board.clock.load(k)&&k.source==1&&k.parked&&k.night==1&&k.moon[1]>.6&&std::abs(k.t-.9)<1e-12);
  {AnySceneStatusV2 st;assert(status_v2(&st)&&st.clock_source==1&&st.parked==1&&std::abs(st.t-.9f)<1e-6f&&st.moon_y>.6f&&st.base.ready==1&&st.tint_targets_active==1);st.version=1;assert(!status_v2(&st));}
  // Fallback without a cycle: the parked detector, holding the last moon.
  {double nan=NAN;fill(.0002,0,parked,.01);memcpy(d+0xba8,&nan,8);frame();assert(board.clock.load(k)&&k.source==2&&k.night==1&&k.t==-1&&k.moon[1]>.6);
   fill(1,0,parked,.01);memcpy(d+0xba8,&nan,8);frame();assert(board.clock.load(k)&&k.source==0);}
  // Cross-check: 60 frames away from the world_time getter sample invalidate the clock until they agree again.
  for(int i=0;i<60;++i){world_time::shared.sample(.95,GetTickCount64(),1000);fill(.0002,.9,parked,.01);frame();}
  assert(board.clock.load(k)&&k.source==2);world_time::shared.sample(.9,GetTickCount64(),1000);fill(.0002,.9,parked,.01);frame();assert(board.clock.load(k)&&k.source==1&&k.day_index==0);
  // K49 Stage A: lift toward moonlight x max(phase floor, lit) in the parked window, 0.03 per frame; none by day or without a clock.
  {auto m4=v4;m4.moon_mode=1;m4.moonlight=.012f;assert(set_v4(&m4));world_time::shared.sample(.9,GetTickCount64(),-1);
   fill(.0002,.9,parked,.01);frame();lights(4,c);double first=luma(c);assert(std::abs(first-(.0002+.03*(.012-.0002)))<1e-9);
   for(int i=0;i<40;++i){world_time::shared.sample(.9,GetTickCount64(),-1);fill(.0002,.9,parked,.01);frame();}lights(4,c);assert(std::abs(luma(c)-.012)<1e-9);
   MoonState ms{};assert(board.moon.load(ms)&&ms.active&&std::abs(ms.added_luminance-.0118f)<1e-6f&&std::abs(ms.direction[1]-celestial::NOON_Y)<1e-9);
   {AnySceneStatusV2 st;assert(status_v2(&st)&&st.moon_mode_active==1&&st.moon_light>.0117f);}
   world_time::shared.sample(.5,GetTickCount64(),-1);fill(1,.5,parked,.01);frame();lights(4,c);assert(board.moon.load(ms)&&!ms.active);  // noon: the weight slews down, no lift (light_main is brighter than the target)
   assert(moon_weight(.8)==0&&moon_weight(.83)==1&&moon_weight(.5)==0&&moon_weight(.1)==1&&moon_weight(.2)==0&&std::abs(moon_weight(.185)-.5)<1e-9);
   frame_state.moon_w=0;m4.moonlight=0;assert(set_v4(&m4));world_time::shared.sample(.9,GetTickCount64(),-1);fill(.0002,.9,parked,.01);frame();lights(4,c);assert(std::abs(luma(c)-.0002)<1e-12);assert(set_v4(&v4));}
  // K17 time-shaped palettes: weights sum to 1; noon at the top of the sky, moon in the parked window; static without a clock.
  for(double e=-1;e<=1;e+=.01)for(double m:{0.,.5,1.}){double w[4];palette_weights(e,m,w);assert(std::abs(w[0]+w[1]+w[2]+w[3]-1)<1e-12);}
  {double w[4];palette_weights(.5,0,w);assert(w[0]==1);palette_weights(.06,0,w);assert(w[1]==1);palette_weights(-.15,0,w);assert(w[2]==1);palette_weights(-.6,1,w);assert(w[3]==1);}
  {Policy pol;pol.scene=q;pol.extras.time_shaped=1;pol.extras.tint_targets=1;for(int i=0;i<3;++i){pol.extras.palette[NOON_KEY][i]=i==0?1.1f:1;pol.extras.palette[MOON_KEY][i]=i==2?1.2f:1;pol.look.key[i]=1;}
   Sample s0{};Weather w0{};MoonState m0{};float mw=0;Clock ck{};ck.source=1;ck.t=.5;ck.sun_y=celestial::NOON_Y;auto a=plan_apply(pol,s0,ck,w0,mw,m0);assert(a.key[0]==1.1f&&a.key[2]==1&&a.targets==1);
   ck.t=.9;ck.sun_y=-.65;a=plan_apply(pol,s0,ck,w0,mw,m0);assert(a.key[2]==1.2f&&a.key[0]==1);ck.source=2;a=plan_apply(pol,s0,ck,w0,mw,m0);assert(a.key[0]==1&&a.key[2]==1);
   // AG-9 storm part: fog scale 0 gives way to the game's fog in a thick storm; nothing without a weather source or the flag.
   pol.scene.fog=0;pol.extras.weather_fog_keep=1;w0.source=1;w0.fog_ratio=4;w0.rain_fx=1;assert(plan_apply(pol,s0,ck,w0,mw,m0).fog_scale==1);w0.fog_ratio=2.5f;assert(std::abs(plan_apply(pol,s0,ck,w0,mw,m0).fog_scale-.5)<1e-6);
   w0.rain_fx=0;assert(plan_apply(pol,s0,ck,w0,mw,m0).fog_scale==0);w0.rain_fx=1;w0.source=0;assert(plan_apply(pol,s0,ck,w0,mw,m0).fog_scale==0);w0.source=1;pol.extras.weather_fog_keep=0;assert(plan_apply(pol,s0,ck,w0,mw,m0).fog_scale==0);}
  // K18 weather v1 ramps: tau 8 s, wet 40 s up / 480 s down, cover .6 max, epoch restart, fog_ratio clamp.
  {WeatherState ws;world_time::Observed o;o.weather_valid=1;o.epoch=7;Sample s0{};s0.valid=SAMPLE_FOG;s0.native_fog=.02;auto w=make_weather(s0,o,.1,ws);assert(w.source==1&&w.rain_fx==0&&w.fog_ratio==2);
   o.rain=1;for(int i=0;i<80;++i)w=make_weather(s0,o,.1,ws);assert(std::abs(w.rain_fx-(1-std::exp(-1.)))<1e-3&&std::abs(w.wet-(1-std::exp(-.2)))<1e-3&&std::abs(w.cover-.6f*w.rain_fx)<1e-6);
   for(int i=0;i<320;++i)w=make_weather(s0,o,.1,ws);assert(std::abs(w.wet-(1-std::exp(-1.)))<1e-3);float wet=w.wet;
   o.rain=0;for(int i=0;i<4800;++i)w=make_weather(s0,o,.1,ws);assert(std::abs(w.wet-wet*std::exp(-1.))<2e-3&&w.rain_fx<1e-6);
   o.rain=.5;o.snow=.25;o.epoch=8;w=make_weather(s0,o,.1,ws);assert(w.rain_fx==.5f&&w.snow_fx==.25f&&w.wet==.5f);
   o.weather_valid=0;w=make_weather(s0,o,.1,ws);assert(w.source==0&&w.rain_fx==0&&!ws.primed);
   for(double f:{.001,1.,double(NAN)}){s0.native_fog=f;s0.valid=std::isfinite(f)?SAMPLE_FOG:0;w=make_weather(s0,o,.1,ws);assert(w.fog_ratio==(f==.001?.5f:f==1?4.f:1.f));}}
  // Weather through the getter store: published once per scene_hook, raw values in status v2.
  {double raw[4]{.8,0,1,2};world_time::shared.sample_weather(0x1000,raw,true,GetTickCount64());world_time::shared.sample(.3,GetTickCount64(),5000);fill(1,.3,day,.04);frame();
   Weather w{};assert(board.weather.load(w)&&fresh(w)&&w.source==1&&w.rain==.8f&&w.fog_ratio==4);AnySceneStatusV2 st;assert(status_v2(&st)&&st.weather_source==1&&st.rain==.8f&&st.fog_ratio==4);}
  // K04: saturating frame counter and the 15-frame rule.
  board.finish_since_ok=UINT32_MAX;assert(count_finish_frame()==UINT32_MAX&&!finish_replaces_bloom(UINT32_MAX));board.finish_since_ok=0;assert(count_finish_frame()==1&&finish_replaces_bloom(1));
  board.finish_since_ok=13;assert(count_finish_frame()==14&&finish_replaces_bloom(14)&&count_finish_frame()==15&&!finish_replaces_bloom(15));
  // K12: with no seam the native SSAO runs (today's order); the seam skips it only when AnyAPI AO rendered.
  {static unsigned native{};static void* seen[2]{};struct F{static void ssao(void*,void*){++native;}static bool yes(void* r,void* s){seen[0]=r;seen[1]=s;return true;}static bool no(void*,void*){return false;}};
   void* ssao_target=(void*)F::ssao;assert(ssao_cell.install(&ssao_target,(void*)ssao_hook));composite_renderer=renderer;composite_scene=d;
   ssao_hook(nullptr,nullptr);assert(native==1);seams.replace_ssao=F::yes;ssao_hook(nullptr,nullptr);assert(native==1&&seen[0]==renderer&&seen[1]==d);
   seams.replace_ssao=F::no;ssao_hook(nullptr,nullptr);assert(native==2);seams.replace_ssao=nullptr;composite_renderer=composite_scene=nullptr;assert(ssao_cell.remove());}
  // GL-11: a cycle jump or clouds turning on asks the game for a full cloud relight (renderer+0x65c = 2).
  {int32_t counter=0;memcpy(renderer+0x65c,&counter,4);fill(1,.3,day,.01);frame();memcpy(&counter,renderer+0x65c,4);assert(counter==0);
   fill(1,.6,day,.01);frame();memcpy(&counter,renderer+0x65c,4);assert(counter==2);counter=0;memcpy(renderer+0x65c,&counter,4);
   fill(1,.6001,day,.01);frame();memcpy(&counter,renderer+0x65c,4);assert(counter==0);renderer[0x63e]=0;fill(1,.6001,day,.01);frame();memcpy(&counter,renderer+0x65c,4);assert(counter==0);
   renderer[0x63e]=1;fill(1,.6001,day,.01);frame();memcpy(&counter,renderer+0x65c,4);assert(counter==2);counter=0;memcpy(renderer+0x65c,&counter,4);}
  // P0 LITERAL_PROBE: every user site's displacement lands on its literal; a synthetic body verifies and reports, a wrong byte refuses.
  {auto check=[](const LiteralSite* s,size_t n){for(size_t i=0;i<n;++i){int32_t disp;memcpy(&disp,s[i].bytes+s[i].size-4,4);assert(s[i].user+s[i].size+uint32_t(disp)==s[i].literal);}};
   check(SHADOW_SITES,std::size(SHADOW_SITES));check(TILE_SITES,std::size(TILE_SITES));
   static unsigned char code[0x3000];memcpy(code,SHADOWS_PROLOGUE,sizeof(SHADOWS_PROLOGUE));for(auto& s:SHADOW_SITES){memcpy(code+s.user,s.bytes,s.size);memcpy(code+s.literal,&s.expected,8);}
   uint32_t verified{};auto body=literal_body({uintptr_t(code)},SHADOW_SITES,std::size(SHADOW_SITES),verified);assert(body==uintptr_t(code)&&verified==1);
   auto line=literal_report("x",1,body,verified,SHADOW_SITES,std::size(SHADOW_SITES));assert(line.find("cascade_split=0.98(ok)")!=std::string::npos&&line.find("written=0")!=std::string::npos);
   code[0x124]^=1;assert(!literal_body({uintptr_t(code)},SHADOW_SITES,std::size(SHADOW_SITES),verified)&&verified==0);}
  tinted=false;}
 std::cout<<"PASS: native pre-render settings, exact restoration including exceptions, pre-build scene lighting/fog, bounds, ownership, unavailable bypass, mismatching cells, scene effects v2 water bounds and scene controls v4 (clock, weather, moonlight, tint targets, K04, K12, GL-11, literal sites)\n";
}
