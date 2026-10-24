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
 std::cout<<"PASS: native pre-render settings, exact restoration including exceptions, pre-build scene lighting/fog, bounds, ownership, unavailable bypass, mismatching cells and scene effects v2 water bounds\n";
}
