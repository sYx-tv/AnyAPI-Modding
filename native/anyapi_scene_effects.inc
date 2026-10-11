#include "effects/effects_gpu.h"
namespace scene_effects {
static scene_effects_gpu::Gpu gpu;static LARGE_INTEGER last_tick{};static bool history{};
// Seconds since the previous effects frame; long gaps restart eye adaptation.
static double elapsed(){LARGE_INTEGER now{},frequency{};QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);double dt=last_tick.QuadPart&&frequency.QuadPart?double(now.QuadPart-last_tick.QuadPart)/double(frequency.QuadPart):-1;last_tick=now;return dt;}
static bool interface_valid(uintptr_t resource){uintptr_t vtable{},method{};MEMORY_BASIC_INFORMATION region{};return resource&&safe_read_memory(resource,&vtable,8)&&vtable&&safe_read_memory(vtable+10*8,&method,8)&&method&&VirtualQuery((void*)method,&region,sizeof(region))&&region.State==MEM_COMMIT&&region.Type==MEM_IMAGE&&(region.AllocationBase==GetModuleHandleW(L"D3D12Core.dll")||region.AllocationBase==GetModuleHandleW(L"d3d12.dll"));}
static void bypass(int code,const char* why){gpu_ready=false;error=code;history=false;auto n=++rejected;if(n==1||n%600==0)log_line(ANY_LOG_WARN,"framework",(std::string("SCENE_EFFECTS bypass=")+why).c_str());}
// Runs at the HDR additive stage after volumetric lighting.
static void record(void* renderer,void* scene){auto p=copy();if(!p.enabled){history=false;gpu_ready=false;return;}
 scene_effects_gpu::Native n;if(!scene_effects_gpu::capture(uintptr_t(renderer),uintptr_t(scene),n,safe_read_memory)){bypass(100+int(n.failure),"native_frame_contract");return;}
 auto list=scene_antialiasing::command_list();auto saved=scene_antialiasing::states.find(list);
 if(!scene_antialiasing::tracking||!list||saved==scene_antialiasing::states.end()||!saved->second.ready()||!interface_valid(n.color.resource)||!interface_valid(n.depth.resource)||n.color.state!=D3D12_RESOURCE_STATE_RENDER_TARGET){bypass(2,"command_bindings_or_target_state");return;}
 std::lock_guard lock(platform::render_mutex);Microsoft::WRL::ComPtr<ID3D12Device> device,td,dd;auto target=(ID3D12Resource*)n.color.resource;auto depth=(ID3D12Resource*)n.depth.resource;
 if(!platform::queue||FAILED(list->GetDevice(IID_PPV_ARGS(&device)))||FAILED(target->GetDevice(IID_PPV_ARGS(&td)))||FAILED(depth->GetDevice(IID_PPV_ARGS(&dd)))||td.Get()!=device.Get()||dd.Get()!=device.Get()){bypass(3,"device");return;}
 if(!gpu.ensure(device.Get(),platform::queue.Get(),target->GetDesc())){bypass(4,"gpu_resources");return;}
 double dt=elapsed();bool reset=!history||dt<0||dt>.5;double step=std::clamp(dt,0.,.1);
 scene_effects_gpu::Frame f;f.metrics={1.f/n.color.width,1.f/n.color.height,float(n.color.width),float(n.color.height)};f.sun_screen=scene_effects_gpu::sun_screen(n);
 f.sun_colour={n.sun_colour[0],n.sun_colour[1],n.sun_colour[2],p.glare};if(p.glare<=0)f.sun_screen[2]=0;
 f.grading={float(p.tonemap),float(p.look),p.look_strength,p.exposure};
 // Fast blend follows the chosen adaptation time; the slow baseline takes about
 // a minute, so the game's own day/night brightness always comes back.
 f.exposure={float(p.eye_adaptation),float(1-std::exp(-step/p.adaptation_seconds)),float(1-std::exp(-step/45.)),1.5f};
 f.extras={p.bloom*.12f,p.vignette,0,0};
 auto bindings=saved->second;scene_antialiasing::owned=true;bool rendered=gpu.record(list,target,depth,D3D12_RESOURCE_STATES(n.depth.state),f,reset);if(rendered)bindings.restore(list);scene_antialiasing::owned=false;
 if(!rendered){bypass(5,"gpu_record");return;}
 history=true;gpu_ready=true;error=0;gpu_ms=float(gpu.gpu_milliseconds());auto m=gpu.measured();
 if(m.valid){adapted_exposure=p.eye_adaptation?std::clamp((m.slow-m.fast)*.75f,-1.5f,1.5f):0.f;scene_luminance=std::exp2(m.fast);sun_visibility=m.visibility;}
 auto count=++frames;if(count==1||count%600==0)log_line(ANY_LOG_INFO,"framework",("SCENE_EFFECTS frames="+std::to_string(count)+" tonemap="+std::to_string(p.tonemap)+" look="+std::to_string(p.look)+" bloom="+std::to_string(p.bloom)+" scene_luminance="+std::to_string(scene_luminance.load())+" adapted_stops="+std::to_string(adapted_exposure.load())+" sun_visibility="+std::to_string(sun_visibility.load())+" sun_y="+std::to_string(n.sun[1])+" gpu_ms="+std::to_string(gpu_ms.load())+" location=HDR_AFTER_LIGHTING_BEFORE_GAME_TONEMAP").c_str());
}
}
