#include "effects/sky_gpu.h"
#include "anyapi_scene_timing_v2.h"
// Sky: sun disc, cloud layer, and at night stars, moon and moonlight, drawn at
// the HDR additive stage, before the finishing pass, so tone mapping and bloom treat them like the rest of the scene.
namespace scene_effects {
// Shared by the sky and water passes (anyapi_scene_water.inc is included after this file).
namespace sky_water {
struct Pass {const char* tag;uint32_t index;std::atomic<bool>* ready;uint64_t rejected{},rested{};scene_frame::Backoff retry{};};
static Pass sky_pass{"SCENE_SKY",ANY_PASS_SKY,&sky_ready},water_pass{"SCENE_WATER",ANY_PASS_WATER,&water_ready};
static void mark(const Pass& s,float ms,int32_t code){scene_frame::board.pass_ms[s.index].store(ms,std::memory_order_relaxed);scene_frame::board.pass_error[s.index].store(code,std::memory_order_relaxed);}
// Not recording this frame by design: off (pass_ms -1, error 0) or quiet (0, -1; no WARN, no rejected count).
// After 120 such frames in a row the GPU objects are released (K09); they are re-created when the pass is next needed.
template<class G> static void rest(Pass& s,G& gpu,bool quiet){mark(s,quiet?0.f:-1.f,quiet?-1:0);
 if(++s.rested==120&&gpu.loaded()){{std::lock_guard lock(platform::render_mutex);gpu.release();}s.retry.reset();
  log_line(ANY_LOG_INFO,"framework",(std::string(s.tag)+" released=1 idle_frames=120").c_str());}}
static void done(Pass& s,float ms){s.rested=0;*s.ready=true;mark(s,ms,0);}
static void reject(Pass& s,int code,const char* why){s.rested=0;*s.ready=false;mark(s,-1,code);error=code;++rejected;
 if(++s.rejected==1||s.rejected%600==0)log_line(ANY_LOG_WARN,"framework",(std::string(s.tag)+" bypass="+why+" code="+std::to_string(code)+" count="+std::to_string(s.rejected)).c_str());}
// K43 retry around ensure(): a failed create waits 1, 2, 4 .. 60 s instead of retrying every frame.
template<class G> static bool ensure(Pass& s,G& gpu,ID3D12Device* d,ID3D12CommandQueue* q,const D3D12_RESOURCE_DESC& desc){
 if(gpu.current(d,q,desc))return true;uint64_t now=GetTickCount64();if(!s.retry.ready(now))return false;scene_frame::reset_resource_cache();
 if(gpu.ensure(d,q,desc)){s.retry.succeeded();return true;}s.retry.failed(now);return false;}
// K03: scene+0x668..0x66b; when unreadable, this frame's controls sample.
static uint32_t view(void* scene){uint32_t v=scene_frame::view_flags(uintptr_t(scene));scene_frame::Sample s{};
 if(v==scene_frame::VIEW_UNREADABLE&&scene_frame::board.sample.load(s)&&scene_frame::fresh(s)&&(s.valid&scene_frame::SAMPLE_VIEW))v=s.view;return v;}
static bool inactive(uint32_t v){return v&&v!=scene_frame::VIEW_UNREADABLE;}
// The game's wind (m/s, x and z) as drawn this frame: scene+0xb78, else this frame's controls sample. Null if neither is sane.
static const double* wind(void* scene,double* out){scene_frame::Sample s{};bool ok=scene_frame::read(uintptr_t(scene)+0xb78,out,16);
 if(!ok&&scene_frame::board.sample.load(s)&&scene_frame::fresh(s)&&(s.valid&scene_frame::SAMPLE_WIND)){out[0]=s.wind[0];out[1]=s.wind[1];ok=true;}
 return ok&&std::isfinite(out[0])&&std::isfinite(out[1])&&std::abs(out[0])<=200&&std::abs(out[1])<=200?out:nullptr;}
static int64_t ticks_per_second(){static const int64_t f=[]{LARGE_INTEGER q{};QueryPerformanceFrequency(&q);return int64_t(q.QuadPart);}();return f;}
static int64_t ticks(){LARGE_INTEGER q{};QueryPerformanceCounter(&q);return q.QuadPart;}
// K19: seconds since the previous call for this clock, clamped to [0, .1]; 0 on the first call.
static double step(int64_t& last,int64_t now){double dt=last&&ticks_per_second()?std::clamp(double(now-last)/double(ticks_per_second()),0.,.1):0.;last=now;return dt;}
// Camera world position (graphics-relative plus the graphics origin), in double.
static bool camera(void* scene,double* world){double rel[3]{},origin[3]{};if(!scene_frame::read(uintptr_t(scene)+0x20+0x108,rel,sizeof(rel))||!scene_frame::read(uintptr_t(scene)+8,origin,sizeof(origin)))return false;
 for(int i=0;i<3;++i){world[i]=rel[i]+origin[i];if(!std::isfinite(world[i])||std::abs(world[i])>1e9)return false;}return true;}
static bool weather(const scene_frame::SkyPolicy& p,scene_frame::Weather& w){return p.weather==1&&scene_frame::board.weather.load(w)&&scene_frame::fresh(w);}
}  // namespace sky_water
static scene_sky_gpu::Gpu sky_gpu;
static double sky_cloud_offset[2]{};static int64_t sky_cloud_ticks{};
static void record_sky(void* renderer,void* scene){namespace sw=sky_water;auto& s=sw::sky_pass;auto p=copy();
 if(!p.enabled||(!p.night_sky&&p.sun_disc<=0&&p.clouds<=0)){sky_ready=false;night=0;return sw::rest(s,sky_gpu,false);}
 // K03 (SKY-15): bunkers, underwater, night vision and thermal have no sky to add to.
 if(sw::inactive(sw::view(scene))){sky_ready=false;night=0;return sw::rest(s,sky_gpu,true);}
 scene_frame::SkyPolicy policy{};scene_frame::board.sky_policy.load(policy);   // never stored, or a V1/V2 owner: all 0
 scene_frame::Clock clock{};scene_sky_gpu::ClockSky cs{};
 bool on_clock=policy.sky_model==1&&scene_frame::board.clock.load(clock)&&scene_frame::fresh(clock)&&(clock.source==1||clock.source==2);
 if(on_clock){for(int i=0;i<3;++i){cs.sun[i]=clock.sun[i];cs.moon[i]=clock.moon[i];}cs.night=clock.night;cs.source=clock.source;cs.parked=clock.parked!=0;cs.phase=clock.phase;on_clock=scene_sky_gpu::clock_ok(cs);}
 scene_effects_gpu::Native n;if(!scene_effects_gpu::capture(uintptr_t(renderer),uintptr_t(scene),n,scene_frame::reader))return sw::reject(s,300+int(n.failure),"native_frame_contract");
 // K16: on the clock the night follows the clock's sun, not the parked native vector.
 float sky_night=on_clock?float(std::clamp(std::isfinite(clock.night)?clock.night:0.,0.,1.)):scene_sky_gpu::night_factor(n.sun[1]);
 float f_night=p.night_sky?sky_night:0.f;night=sky_night;
 // Daylight with no sun disc or clouds: nothing to add, so nothing is recorded.
 if(f_night<=0&&p.sun_disc<=0&&p.clouds<=0){sky_ready=true;sky_ms=0;return sw::rest(s,sky_gpu,true);}
 double sky_colour_d[3]{};float sky_colour[3]{};if(!scene_frame::read(uintptr_t(scene)+0xb08,sky_colour_d,sizeof(sky_colour_d)))return sw::reject(s,314,"sky_colour");
 for(int i=0;i<3;++i){if(!std::isfinite(sky_colour_d[i])||sky_colour_d[i]<0||sky_colour_d[i]>10000)return sw::reject(s,314,"sky_colour");sky_colour[i]=float(sky_colour_d[i]);}
 volumetric::NativeTarget g[3];static constexpr uintptr_t offsets[3]={0x1f8,0x1e8,0x208};
 for(int i=0;i<3;++i){uintptr_t object{};if(!scene_frame::read(uintptr_t(renderer)+offsets[i],&object,8)||!volumetric::target(object,g[i],scene_frame::reader)||g[i].width!=n.color.width||g[i].height!=n.color.height)return sw::reject(s,310+i,"gbuffer_contract");}
 auto list=scene_antialiasing::command_list();auto saved=scene_antialiasing::states.find(list);
 if(!scene_antialiasing::tracking||!list||saved==scene_antialiasing::states.end()||!saved->second.ready()||n.color.state!=D3D12_RESOURCE_STATE_RENDER_TARGET)return sw::reject(s,320,"command_bindings_or_target_state");
 if(!scene_frame::resource_valid(n.color.resource)||!scene_frame::resource_valid(n.depth.resource))return sw::reject(s,321,"resources");for(auto& x:g)if(!scene_frame::resource_valid(x.resource))return sw::reject(s,321,"resources");
 std::lock_guard lock(platform::render_mutex);Microsoft::WRL::ComPtr<ID3D12Device> device,owner;auto target=(ID3D12Resource*)n.color.resource;
 if(!platform::queue||FAILED(list->GetDevice(IID_PPV_ARGS(&device))))return sw::reject(s,322,"device");
 for(auto r:{n.color.resource,n.depth.resource,g[0].resource,g[1].resource,g[2].resource})if(FAILED(((ID3D12Resource*)r)->GetDevice(IID_PPV_ARGS(owner.ReleaseAndGetAddressOf())))||owner.Get()!=device.Get())return sw::reject(s,322,"device");
 if(!sw::ensure(s,sky_gpu,device.Get(),platform::queue.Get(),target->GetDesc()))return sw::reject(s,323,"gpu_resources");
 int64_t now=sw::ticks(),hz=sw::ticks_per_second();float seconds=hz?float(double(now%(hz*3600))/double(hz)):0.f;
 float on=p.night_sky?1.f:0.f;auto f=scene_sky_gpu::build(n.right,n.up,n.forward,n.sun,n.color.width,n.color.height,seconds,p.stars*on,p.moon*on,p.moonlight*on,n.sun_colour,sky_colour,p.sun_disc,p.clouds);if(!p.night_sky)f.sun[3]=0;
 // sky_model 1 (K16, SKY-6/10/11/16): the clock's night, moon, star rotation and phase. The disc stays on the native sun by day.
 if(on_clock&&scene_sky_gpu::apply_clock(f,cs,n.sun)){if(!p.night_sky)f.sun[3]=0;if(p.stars>=1.25f)f.model[0]=float(uint32_t(f.model[0])|scene_sky_gpu::MODEL_STAR_SEARCH);}else on_clock=false;
 // K49: the disc at the direction the game lights the night from, opaque, while controls' moon_mode 1 is lighting it.
 scene_frame::MoonState moon{};bool moon_follows=on_clock&&policy.moon_disc_follows_light==1&&scene_frame::board.moon.load(moon)&&scene_frame::fresh(moon)&&moon.active==1&&scene_sky_gpu::apply_moon_light(f,moon.direction,1);
 // K18 consumers (weather on).
 scene_frame::Weather w{};bool wet=sw::weather(policy,w);float cover=wet&&w.source==1?w.cover:0.f;if(wet)scene_sky_gpu::apply_weather(f,cover,w.fog_ratio);
 // Clouds sit in the world: the camera's world position, in double. Without a position the clouds stay put over the
 // origin rather than the pass stopping. cloud_model 1 (K19): the deck drifts with the game's wind.
 bool clouds_v2=policy.cloud_model==1;double dt=sw::step(sky_cloud_ticks,now),wind_now[2]{};const double* wind=nullptr;
 if(clouds_v2){wind=sw::wind(scene,wind_now);scene_sky_gpu::drift(sky_cloud_offset,wind,dt);f.model[0]=float(uint32_t(f.model[0])|scene_sky_gpu::MODEL_CLOUDS_V2);}
 if(p.clouds>0){double world[3];if(sw::camera(scene,world)){auto c=clouds_v2?scene_sky_gpu::cloud_field(world,sky_cloud_offset):scene_sky_gpu::cloud_position(world,hz?double(now)/double(hz):0.0);f.clouds[0]=c[0];f.clouds[1]=c[1];f.clouds[2]=c[2];}}
 auto input=[](const volumetric::NativeTarget& t){return scene_sky_gpu::Gpu::Input{(ID3D12Resource*)t.resource,D3D12_RESOURCE_STATES(t.state)};};
 auto bindings=saved->second;scene_antialiasing::owned=true;bool rendered=sky_gpu.record(list,target,input(n.depth),input(g[0]),input(g[1]),input(g[2]),f);if(rendered)bindings.restore(list);scene_antialiasing::owned=false;
 if(!rendered)return sw::reject(s,324,"gpu_record");
 sky_ms=float(sky_gpu.gpu_milliseconds());sw::done(s,sky_ms.load());static uint64_t count{};
 if(++count==1||count%600==0)log_line(ANY_LOG_INFO,"framework",("SCENE_SKY frames="+std::to_string(count)+" night="+std::to_string(f_night)+" sun_disc="+std::to_string(p.sun_disc)+" clouds="+std::to_string(p.clouds)+" sun_colour="+std::to_string(f.sun_light[0])+" sky_colour="+std::to_string(f.sky_light[0])+" moon_y="+std::to_string(f.moon[1])+" sun_y="+std::to_string(n.sun[1])
  +" model="+std::to_string(int(f.model[0]))+" clock_source="+std::to_string(on_clock?clock.source:0u)+" t="+std::to_string(on_clock?clock.t:-1.)+" true_sun_y="+std::to_string(f.sun[1])+" phase="+std::to_string(on_clock?clock.phase:-1.f)+" moon_follows="+std::to_string(int(moon_follows))
  +" weather="+std::to_string(int(wet))+" cover="+std::to_string(cover)+" wind="+(wind?std::to_string(wind[0])+","+std::to_string(wind[1]):std::string(clouds_v2?"fallback":"fixed"))+" gpu_ms="+std::to_string(sky_ms.load())+" location=HDR_ADDITIVE_BEFORE_FINISH").c_str());
}
}
