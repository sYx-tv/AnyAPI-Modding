#include "effects/sky_gpu.h"
// Sky: sun disc, cloud layer, and at night stars, moon and moonlight, drawn at
// the HDR additive stage, before the finishing pass, so tone mapping and bloom treat them like the rest of the scene.
namespace scene_effects {
static scene_sky_gpu::Gpu sky_gpu;
static void sky_bypass(int code,const char* why){sky_ready=false;error=code;auto n=++rejected;if(n==1||n%600==0)log_line(ANY_LOG_WARN,"framework",(std::string("SCENE_SKY bypass=")+why).c_str());}
static void record_sky(void* renderer,void* scene){auto p=copy();if(!p.enabled||(!p.night_sky&&p.sun_disc<=0&&p.clouds<=0)){sky_ready=false;night=0;return;}
 scene_effects_gpu::Native n;if(!scene_effects_gpu::capture(uintptr_t(renderer),uintptr_t(scene),n,safe_read_memory))return sky_bypass(300+int(n.failure),"native_frame_contract");
 float f_night=p.night_sky?scene_sky_gpu::night_factor(n.sun[1]):0.f;night=scene_sky_gpu::night_factor(n.sun[1]);
 // Daylight with no sun disc or clouds: nothing to add, so nothing is recorded.
 if(f_night<=0&&p.sun_disc<=0&&p.clouds<=0){sky_ready=true;sky_ms=0;return;}
 double sky_colour_d[3]{};float sky_colour[3]{};if(!safe_read_memory(uintptr_t(scene)+0xb08,sky_colour_d,sizeof(sky_colour_d)))return sky_bypass(314,"sky_colour");for(int i=0;i<3;++i){if(!std::isfinite(sky_colour_d[i])||sky_colour_d[i]<0||sky_colour_d[i]>10000)return sky_bypass(314,"sky_colour");sky_colour[i]=float(sky_colour_d[i]);}
 volumetric::NativeTarget g[3];static constexpr uintptr_t offsets[3]={0x1f8,0x1e8,0x208};
 for(int i=0;i<3;++i){uintptr_t object{};if(!safe_read_memory(uintptr_t(renderer)+offsets[i],&object,8)||!volumetric::target(object,g[i],safe_read_memory)||g[i].width!=n.color.width||g[i].height!=n.color.height)return sky_bypass(310+i,"gbuffer_contract");}
 auto list=scene_antialiasing::command_list();auto saved=scene_antialiasing::states.find(list);
 if(!scene_antialiasing::tracking||!list||saved==scene_antialiasing::states.end()||!saved->second.ready()||n.color.state!=D3D12_RESOURCE_STATE_RENDER_TARGET)return sky_bypass(320,"command_bindings_or_target_state");
 if(!interface_valid(n.color.resource)||!interface_valid(n.depth.resource))return sky_bypass(321,"resources");for(auto& x:g)if(!interface_valid(x.resource))return sky_bypass(321,"resources");
 std::lock_guard lock(platform::render_mutex);Microsoft::WRL::ComPtr<ID3D12Device> device,owner;auto target=(ID3D12Resource*)n.color.resource;
 if(!platform::queue||FAILED(list->GetDevice(IID_PPV_ARGS(&device))))return sky_bypass(322,"device");
 for(auto r:{n.color.resource,n.depth.resource,g[0].resource,g[1].resource,g[2].resource})if(FAILED(((ID3D12Resource*)r)->GetDevice(IID_PPV_ARGS(owner.ReleaseAndGetAddressOf())))||owner.Get()!=device.Get())return sky_bypass(322,"device");
 if(!sky_gpu.ensure(device.Get(),platform::queue.Get(),target->GetDesc()))return sky_bypass(323,"gpu_resources");
 LARGE_INTEGER now{},frequency{};QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);float seconds=frequency.QuadPart?float(double(now.QuadPart%(frequency.QuadPart*3600))/double(frequency.QuadPart)):0.f;
 float on=p.night_sky?1.f:0.f;auto f=scene_sky_gpu::build(n.right,n.up,n.forward,n.sun,n.color.width,n.color.height,seconds,p.stars*on,p.moon*on,p.moonlight*on,n.sun_colour,sky_colour,p.sun_disc,p.clouds);if(!p.night_sky)f.sun[3]=0;
 // Clouds sit in the world: the camera's world position (graphics-relative plus the
 // graphics origin) and the full counter for the wind, both in double. Without a
 // position the clouds stay put over the origin rather than the pass stopping.
 if(p.clouds>0){double rel[3]{},origin[3]{};if(safe_read_memory(uintptr_t(scene)+0x20+0x108,rel,sizeof(rel))&&safe_read_memory(uintptr_t(scene)+8,origin,sizeof(origin))){double world[3];bool ok=true;for(int i=0;i<3;++i){world[i]=rel[i]+origin[i];ok=ok&&std::isfinite(world[i])&&std::abs(world[i])<1e9;}
  if(ok){auto c=scene_sky_gpu::cloud_position(world,frequency.QuadPart?double(now.QuadPart)/double(frequency.QuadPart):0.0);f.clouds[0]=c[0];f.clouds[1]=c[1];f.clouds[2]=c[2];}}}
 auto input=[](const volumetric::NativeTarget& t){return scene_sky_gpu::Gpu::Input{(ID3D12Resource*)t.resource,D3D12_RESOURCE_STATES(t.state)};};
 auto bindings=saved->second;scene_antialiasing::owned=true;bool rendered=sky_gpu.record(list,target,input(n.depth),input(g[0]),input(g[1]),input(g[2]),f);if(rendered)bindings.restore(list);scene_antialiasing::owned=false;
 if(!rendered)return sky_bypass(324,"gpu_record");
 sky_ready=true;sky_ms=float(sky_gpu.gpu_milliseconds());static uint64_t count{};
 if(++count==1||count%600==0)log_line(ANY_LOG_INFO,"framework",("SCENE_SKY frames="+std::to_string(count)+" night="+std::to_string(f_night)+" sun_disc="+std::to_string(p.sun_disc)+" clouds="+std::to_string(p.clouds)+" sun_colour="+std::to_string(f.sun_light[0])+" sky_colour="+std::to_string(f.sky_light[0])+" moon_y="+std::to_string(f.moon[1])+" sun_y="+std::to_string(n.sun[1])+" gpu_ms="+std::to_string(sky_ms.load())+" location=HDR_ADDITIVE_BEFORE_FINISH").c_str());
}
}
