#include "effects/water_gpu.h"
// Ocean reflections and sun glint: after bounce light, before volumetric lighting, so fog and beams
// sit on top of the water like the rest of the scene. The ocean writes no depth: gbuffer_ocean.y > 0
// masks it, and the water surface is the sea level plane (y = 0) under the camera.
namespace scene_effects {
static scene_water_gpu::Gpu water_gpu;static uint32_t water_frame_index{};
static void water_bypass(int code,const char* why){water_ready=false;error=code;auto n=++rejected;if(n==1||n%600==0)log_line(ANY_LOG_WARN,"framework",(std::string("SCENE_WATER bypass=")+why).c_str());}
static void record_water(void* renderer,void* scene){auto p=copy();auto w=copy_water();if(!p.enabled||(w.reflections<=0&&w.glint<=0)){water_ready=false;return;}
 scene_effects_gpu::Native n;if(!scene_effects_gpu::capture(uintptr_t(renderer),uintptr_t(scene),n,safe_read_memory))return water_bypass(700+int(n.failure),"native_frame_contract");
 // scene_renderer.m_is_ocean: with the ocean off nothing draws gbuffer_ocean, so there is no mask to trust.
 unsigned char drawn{2};if(!safe_read_memory(uintptr_t(scene)+0xb71,&drawn,1)||drawn>1)return water_bypass(714,"ocean_flag");if(!drawn){water_ready=true;water_ms=0;return;}
 volumetric::NativeTarget sea;uintptr_t object{};if(!safe_read_memory(uintptr_t(renderer)+0x208,&object,8)||!volumetric::target(object,sea,safe_read_memory)||sea.width!=n.color.width||sea.height!=n.color.height)return water_bypass(710,"gbuffer_contract");
 // Camera world position (graphics-relative plus the graphics origin), projection, the game's sky colours
 // (view up, view down, light up, light down) and wind, all as doubles.
 double rel[3]{},origin[3]{},m[16]{},colours[4][3]{},wind[2]{};double world[3];
 if(!safe_read_memory(uintptr_t(scene)+0x20+0x108,rel,sizeof(rel))||!safe_read_memory(uintptr_t(scene)+8,origin,sizeof(origin))||!safe_read_memory(uintptr_t(scene)+0x20+0x1a0,m,sizeof(m))||!safe_read_memory(uintptr_t(scene)+0xa90,colours,sizeof(colours)))return water_bypass(713,"camera_or_colours");
 for(int i=0;i<3;++i){world[i]=rel[i]+origin[i];if(!std::isfinite(world[i])||std::abs(world[i])>1e9)return water_bypass(713,"camera_or_colours");}
 float sky[4][3]{};for(int c=0;c<4;++c)for(int i=0;i<3;++i){if(!std::isfinite(colours[c][i])||colours[c][i]<0||colours[c][i]>10000)return water_bypass(713,"camera_or_colours");sky[c][i]=float(colours[c][i]);}
 // Wind only shapes the ripples: without it the water keeps a light ripple.
 if(!safe_read_memory(uintptr_t(scene)+0xb78,wind,sizeof(wind))||!std::isfinite(wind[0])||!std::isfinite(wind[1])||std::abs(wind[0])>200||std::abs(wind[1])>200)wind[0]=wind[1]=0;
 // Under the surface the game draws its own underwater view (and capture bypasses it); just above, the water is still drawn.
 if(world[1]<-1){water_ready=true;water_ms=0;return;}
 LARGE_INTEGER now{},frequency{};QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);double seconds=frequency.QuadPart?double(now.QuadPart)/double(frequency.QuadPart):0;
 const float projection[4]{float(m[10]),float(m[14]),float(m[11]),float(m[15])};
 auto f=scene_water_gpu::build(n.right,n.up,n.forward,projection,n.color.width,n.color.height,world,wind,seconds,n.sun,n.sun_colour,sky,w.reflections,w.glint,w.quality,water_frame_index);
 auto list=scene_antialiasing::command_list();auto saved=scene_antialiasing::states.find(list);
 if(!scene_antialiasing::tracking||!list||saved==scene_antialiasing::states.end()||!saved->second.ready()||n.color.state!=D3D12_RESOURCE_STATE_RENDER_TARGET)return water_bypass(720,"command_bindings_or_target_state");
 if(!interface_valid(n.color.resource)||!interface_valid(n.depth.resource)||!interface_valid(sea.resource))return water_bypass(721,"resources");
 std::lock_guard lock(platform::render_mutex);Microsoft::WRL::ComPtr<ID3D12Device> device,owner;auto target=(ID3D12Resource*)n.color.resource;
 if(!platform::queue||FAILED(list->GetDevice(IID_PPV_ARGS(&device))))return water_bypass(722,"device");
 for(auto r:{n.color.resource,n.depth.resource,sea.resource})if(FAILED(((ID3D12Resource*)r)->GetDevice(IID_PPV_ARGS(owner.ReleaseAndGetAddressOf())))||owner.Get()!=device.Get())return water_bypass(722,"device");
 if(!water_gpu.ensure(device.Get(),platform::queue.Get(),target->GetDesc()))return water_bypass(723,"gpu_resources");
 auto input=[](const volumetric::NativeTarget& t){return scene_water_gpu::Gpu::Input{(ID3D12Resource*)t.resource,D3D12_RESOURCE_STATES(t.state)};};
 auto bindings=saved->second;scene_antialiasing::owned=true;bool rendered=water_gpu.record(list,target,input(n.depth),input(sea),f);if(rendered)bindings.restore(list);scene_antialiasing::owned=false;
 if(!rendered)return water_bypass(724,"gpu_record");
 ++water_frame_index;water_ready=true;water_ms=float(water_gpu.gpu_milliseconds());auto count=++water_frames;
 if(count==1||count%600==0)log_line(ANY_LOG_INFO,"framework",("SCENE_WATER frames="+std::to_string(count)+" reflections="+std::to_string(w.reflections)+" glint="+std::to_string(w.glint)+" steps="+std::to_string(int(f.params[0]))+" height="+std::to_string(f.camera[0])+" ripple="+std::to_string(f.light_up[3])+" fog_m="+std::to_string(f.view_down[3])+" sun_y="+std::to_string(n.sun[1])+" gpu_ms="+std::to_string(water_ms.load())+" location=HDR_ADDITIVE_BEFORE_VOLUMETRICS").c_str());
}
}
