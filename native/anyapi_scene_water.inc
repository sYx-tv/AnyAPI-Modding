#include "effects/water_gpu.h"
// Ocean reflections and sun glint: after bounce light, before volumetric lighting, so fog and beams
// sit on top of the water like the rest of the scene. The ocean writes no depth: gbuffer_ocean.y > 0
// masks it, and the water surface is the sea level plane (y = 0) under the camera.
namespace scene_effects {
static scene_water_gpu::Gpu water_gpu;
static double water_phase[4]{};static int64_t water_ticks{};   // K19: ripple offsets (m), integrated in double
static void record_water(void* renderer,void* scene){namespace sw=sky_water;auto& s=sw::water_pass;auto p=copy();auto w=copy_water();
 if(!p.enabled||(w.reflections<=0&&w.glint<=0)){water_ready=false;return sw::rest(s,water_gpu,false);}
 // K03 (WATER-14): bunkers, underwater, night vision and thermal.
 if(sw::inactive(sw::view(scene))){water_ready=false;return sw::rest(s,water_gpu,true);}
 scene_effects_gpu::Native n;if(!scene_effects_gpu::capture(uintptr_t(renderer),uintptr_t(scene),n,scene_frame::reader))return sw::reject(s,700+int(n.failure),"native_frame_contract");
 // scene_renderer.m_is_ocean: with the ocean off nothing draws gbuffer_ocean, so there is no mask to trust.
 unsigned char drawn{2};if(!scene_frame::read(uintptr_t(scene)+0xb71,&drawn,1)||drawn>1)return sw::reject(s,714,"ocean_flag");if(!drawn){water_ready=false;return sw::rest(s,water_gpu,true);}
 volumetric::NativeTarget sea;uintptr_t object{};if(!scene_frame::read(uintptr_t(renderer)+0x208,&object,8)||!volumetric::target(object,sea,scene_frame::reader)||sea.width!=n.color.width||sea.height!=n.color.height)return sw::reject(s,710,"gbuffer_contract");
 // Camera world position, projection and the game's sky colours (view up, view down, light up, light down), all as doubles.
 double m[16]{},colours[4][3]{},world[3];
 if(!sw::camera(scene,world)||!scene_frame::read(uintptr_t(scene)+0x20+0x1a0,m,sizeof(m))||!scene_frame::read(uintptr_t(scene)+0xa90,colours,sizeof(colours)))return sw::reject(s,713,"camera_or_colours");
 float sky[4][3]{};for(int c=0;c<4;++c)for(int i=0;i<3;++i){if(!std::isfinite(colours[c][i])||colours[c][i]<0||colours[c][i]>10000)return sw::reject(s,713,"camera_or_colours");sky[c][i]=float(colours[c][i]);}
 // Under the surface the game draws its own underwater view; just above, the water is still drawn.
 if(world[1]<-1){water_ready=false;return sw::rest(s,water_gpu,true);}
 // Wind only shapes the ripples: without it the water keeps a light ripple. K19: the phase integrates it, so a gust
 // changes the speed of the ripples, never their position.
 double wind_now[2]{};const double* wind=sw::wind(scene,wind_now);scene_water_gpu::advance_ripples(water_phase,wind,sw::step(water_ticks,sw::ticks()));
 // K13/WATER-15: under TAA the step jitter follows the TAA index and the rays follow this frame's projection jitter;
 // without TAA the jitter is a fixed per-pixel pattern (frame 0) and nothing moves.
 scene_frame::Temporal t{};bool taa=scene_frame::board.temporal.load(t)&&scene_frame::fresh(t)&&t.taa_active&&t.jitter_active;
 float jitter[2]{taa?t.jitter_px[0]/float(n.color.width):0.f,taa?t.jitter_px[1]/float(n.color.height):0.f};
 // K18 (weather on): rain roughens the sea and its glint.
 scene_frame::SkyPolicy policy{};scene_frame::board.sky_policy.load(policy);scene_frame::Weather wx{};float rain=sw::weather(policy,wx)&&wx.source==1&&std::isfinite(wx.rain_fx)?std::clamp(wx.rain_fx,0.f,1.f):0.f;
 const float projection[4]{float(m[10]),float(m[14]),float(m[11]),float(m[15])};
 auto f=scene_water_gpu::build(n.right,n.up,n.forward,projection,n.color.width,n.color.height,world,wind,water_phase,n.sun,n.sun_colour,sky,w.reflections,w.glint,w.quality,taa?t.index%64:0,taa?jitter:nullptr,rain);
 auto list=scene_antialiasing::command_list();auto saved=scene_antialiasing::states.find(list);
 if(!scene_antialiasing::tracking||!list||saved==scene_antialiasing::states.end()||!saved->second.ready()||n.color.state!=D3D12_RESOURCE_STATE_RENDER_TARGET)return sw::reject(s,720,"command_bindings_or_target_state");
 if(!scene_frame::resource_valid(n.color.resource)||!scene_frame::resource_valid(n.depth.resource)||!scene_frame::resource_valid(sea.resource))return sw::reject(s,721,"resources");
 std::lock_guard lock(platform::render_mutex);Microsoft::WRL::ComPtr<ID3D12Device> device,owner;auto target=(ID3D12Resource*)n.color.resource;
 if(!platform::queue||FAILED(list->GetDevice(IID_PPV_ARGS(&device))))return sw::reject(s,722,"device");
 for(auto r:{n.color.resource,n.depth.resource,sea.resource})if(FAILED(((ID3D12Resource*)r)->GetDevice(IID_PPV_ARGS(owner.ReleaseAndGetAddressOf())))||owner.Get()!=device.Get())return sw::reject(s,722,"device");
 if(!sw::ensure(s,water_gpu,device.Get(),platform::queue.Get(),target->GetDesc()))return sw::reject(s,723,"gpu_resources");
 auto input=[](const volumetric::NativeTarget& t){return scene_water_gpu::Gpu::Input{(ID3D12Resource*)t.resource,D3D12_RESOURCE_STATES(t.state)};};
 auto bindings=saved->second;scene_antialiasing::owned=true;bool rendered=water_gpu.record(list,target,input(n.depth),input(sea),f);if(rendered)bindings.restore(list);scene_antialiasing::owned=false;
 if(!rendered)return sw::reject(s,724,"gpu_record");
 water_ms=float(water_gpu.gpu_milliseconds());sw::done(s,water_ms.load());auto count=++water_frames;
 if(count==1||count%600==0)log_line(ANY_LOG_INFO,"framework",("SCENE_WATER frames="+std::to_string(count)+" reflections="+std::to_string(w.reflections)+" glint="+std::to_string(w.glint)+" steps="+std::to_string(int(f.params[0]))+" height="+std::to_string(f.camera[0])+" ripple="+std::to_string(f.light_up[3])+" fog_m="+std::to_string(f.view_down[3])+" reach_m="+std::to_string(f.forward[3])
  +" glint_alpha="+std::to_string(f.light_down[3])+" wind="+(wind?std::to_string(wind[0])+","+std::to_string(wind[1]):std::string("none"))+" taa="+std::to_string(int(taa))+" rain="+std::to_string(rain)+" sun_y="+std::to_string(n.sun[1])+" gpu_ms="+std::to_string(water_ms.load())+" location=HDR_ADDITIVE_BEFORE_VOLUMETRICS").c_str());
}
}
