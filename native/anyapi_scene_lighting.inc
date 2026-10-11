#include "lighting/volumetric_gpu.h"
#include "lighting/native_frame.h"
namespace scene_lighting {
static volumetric::Gpu gpu;
static void submitted(ID3D12CommandQueue* queue,UINT count,ID3D12CommandList*const* lists){std::lock_guard lock(platform::render_mutex);gpu.submitted(queue,count,lists);}
static bool interface_valid(uintptr_t resource){uintptr_t vtable{},method{};MEMORY_BASIC_INFORMATION region{};return resource&&safe_read_memory(resource,&vtable,8)&&vtable&&safe_read_memory(vtable+10*8,&method,8)&&method&&VirtualQuery((void*)method,&region,sizeof(region))&&region.State==MEM_COMMIT&&region.Type==MEM_IMAGE&&(region.AllocationBase==GetModuleHandleW(L"D3D12Core.dll")||region.AllocationBase==GetModuleHandleW(L"d3d12.dll"));}
// Previous frame's camera for temporal reprojection, in world coordinates.
struct History {bool valid{};double position[3]{};std::array<float,4> right{},up{},forward{};uint64_t tick{};uint32_t frame{},width{},height{};};
static History history;
// History weight by quality: fewer ray samples are noisier and lean on history more.
static constexpr float history_weights[]={.92f,.9f,.88f,.85f};
static void temporal_rows(volumetric::NativeFrame& f,const AnySceneLightingParametersV1& p,const Shaping& s){
 auto& t=f.gpu.temporal;t={};t[1][0]=s.clarity;t[1][1]=s.beam_reach;t[1][2]=float(s.sun_response);if(!s.temporal){history.valid=false;return;}
 uint64_t now=GetTickCount64();double world[3];for(size_t i=0;i<3;++i)world[i]=double(f.gpu.camera[i])+f.origin[i];
 double moved=0;for(size_t i=0;i<3;++i)moved+=(world[i]-history.position[i])*(world[i]-history.position[i]);
 // Drop history after a pause, a resize or a teleport-sized jump.
 bool continuous=history.valid&&now-history.tick<250&&history.width==f.color.width&&history.height==f.color.height&&moved<50.0*50.0;
 uint32_t write=history.frame&1;t[1][3]=1;t[1][4]=write==0?1.f:0.f;t[1][5]=history_weights[std::min(p.quality,3u)];
 if(continuous){for(size_t i=0;i<3;++i){t[0][i]=float(history.position[i]-f.origin[i]);t[0][4+i]=history.right[i];t[0][8+i]=history.up[i];t[0][12+i]=history.forward[i];}t[0][3]=1;t[0][7]=history.right[3];t[0][11]=history.up[3];}
 t[0][15]=float(history.frame%1024);
}
static void remember(const volumetric::NativeFrame& f){history.valid=f.gpu.temporal[1][3]==1;for(size_t i=0;i<3;++i)history.position[i]=double(f.gpu.camera[i])+f.origin[i];history.right=f.gpu.right;history.up=f.gpu.up;history.forward=f.gpu.forward;history.forward[3]=0;history.tick=GetTickCount64();++history.frame;history.width=f.color.width;history.height=f.color.height;}
static void record(void* renderer,void* scene){auto extended=copy_v2();auto p=extended.scene;if(!p.enabled)return;volumetric::NativeFrame f;bool valid=volumetric::capture(uintptr_t(renderer),uintptr_t(scene),p,f,safe_read_memory,extended.local_strength,extended.local_budget);frame_valid=valid;if(valid)temporal_rows(f,p,copy_shaping());if(!valid||!volumetric::valid(f.gpu)){valid=false;frame_valid=false;history.valid=false;gpu_ready=false;reason=100+f.failure;auto n=++rejected;if(n==1||n%600==0)log_line(ANY_LOG_WARN,"framework",("SCENE_LIGHTING bypass=native_frame_contract stage="+std::to_string(f.failure)).c_str());return;}
 auto list=scene_antialiasing::command_list();auto saved=scene_antialiasing::states.find(list);if(!scene_antialiasing::tracking||!list||saved==scene_antialiasing::states.end()||!saved->second.ready()||!interface_valid(f.color.resource)||!interface_valid(f.depth.resource)||!interface_valid(f.shadow.resource)||f.color.state!=D3D12_RESOURCE_STATE_RENDER_TARGET){gpu_ready=false;reason=2;auto n=++rejected;if(n==1||n%600==0)log_line(ANY_LOG_WARN,"framework",("SCENE_LIGHTING bypass=command_bindings_or_target_state target_state="+std::to_string(f.color.state)).c_str());return;}
 std::lock_guard lock(platform::render_mutex);Microsoft::WRL::ComPtr<ID3D12Device> device;if(!platform::queue||FAILED(list->GetDevice(IID_PPV_ARGS(&device)))){gpu_ready=false;reason=3;++rejected;return;}
 platform::after_execute=submitted;volumetric::Gpu::ProbeStats stats;if(gpu.probe(stats))log_line(ANY_LOG_INFO,"framework",("SHAFT_PROBE lit_min="+std::to_string(stats.lit_min)+" lit_max="+std::to_string(stats.lit_max)+" lit_avg="+std::to_string(stats.lit_average)+" coverage="+std::to_string(stats.coverage)+" distance_fraction="+std::to_string(stats.distance)).c_str());
 auto target=(ID3D12Resource*)f.color.resource;auto depth=(ID3D12Resource*)f.depth.resource;auto shadow=(ID3D12Resource*)f.shadow.resource;Microsoft::WRL::ComPtr<ID3D12Device> td,dd,sd;if(FAILED(target->GetDevice(IID_PPV_ARGS(&td)))||FAILED(depth->GetDevice(IID_PPV_ARGS(&dd)))||FAILED(shadow->GetDevice(IID_PPV_ARGS(&sd)))||td.Get()!=device.Get()||dd.Get()!=device.Get()||sd.Get()!=device.Get()||!gpu.ensure(device.Get(),platform::queue.Get(),target->GetDesc())){gpu_ready=false;reason=4;error=gpu.error();auto n=++rejected;if(n==1||n%600==0)log_line(ANY_LOG_WARN,"framework",("SCENE_LIGHTING bypass=gpu_resource_contract error="+std::to_string(error.load())).c_str());return;}
 std::array<ID3D12Resource*,12> shadows{};std::array<D3D12_RESOURCE_STATES,12> shadow_states{};
 uint32_t bound_shadows=f.gpu.local_shadow_count>0?8+uint32_t(f.gpu.local_shadow_count):f.shadow_count;for(uint32_t i=0;i<bound_shadows;++i){if(i>=f.shadow_count&&i<8)f.cascades[i]=f.cascades[0];if(!interface_valid(f.cascades[i].resource)){gpu_ready=false;reason=4;++rejected;return;}shadows[i]=(ID3D12Resource*)f.cascades[i].resource;shadow_states[i]=D3D12_RESOURCE_STATES(f.cascades[i].state);Microsoft::WRL::ComPtr<ID3D12Device> owner;if(FAILED(shadows[i]->GetDevice(IID_PPV_ARGS(&owner)))||owner.Get()!=device.Get()){gpu_ready=false;reason=4;++rejected;return;}}
 auto bindings=saved->second;scene_antialiasing::owned=true;bool rendered=gpu.record(list,target,depth,D3D12_RESOURCE_STATES(f.depth.state),shadows,shadow_states,f.gpu);if(rendered)bindings.restore(list);scene_antialiasing::owned=false;gpu_ready=rendered;reason=rendered?0:5;if(rendered)remember(f);else history.valid=false;width=f.color.width;height=f.color.height;shadow_count=f.shadow_count;
 if(!rendered){auto n=++rejected;if(n==1||n%600==0)log_line(ANY_LOG_WARN,"framework","SCENE_LIGHTING bypass=gpu_record");return;}auto count=++frames;if(count==1||count%600==0)log_line(ANY_LOG_INFO,"framework",("SCENE_LIGHTING frames="+std::to_string(count)+" location=HDR_BEFORE_BLOOM_TONEMAP_HUD depth_valid=1 sun_cascades="+std::to_string(f.shadow_count)+" quality="+std::to_string(p.quality)+" camera_world_height="+std::to_string(f.gpu.camera[3])+" sunlight_y="+std::to_string(f.gpu.sun[1])+" sun_rgb="+std::to_string(f.gpu.sun_color[0])+","+std::to_string(f.gpu.sun_color[1])+","+std::to_string(f.gpu.sun_color[2])+" local_lights="+std::to_string(uint32_t(f.gpu.fog_color[3]))+" local_shadows="+std::to_string(uint32_t(f.gpu.local_shadow_count))+" view_sun_cos="+std::to_string(f.gpu.sun[0]*f.gpu.forward[0]+f.gpu.sun[1]*f.gpu.forward[1]+f.gpu.sun[2]*f.gpu.forward[2])).c_str());
}
}
namespace scene_timing {
static bool copy(AnySceneTimingV1* t){if(!t||t->struct_size!=sizeof(*t)||t->version!=1)return false;// Plain reads of smoothed doubles; a slightly stale value is fine for a readout.
 t->lighting_ms=scene_lighting::gpu_ready?float(scene_lighting::gpu.gpu_milliseconds()):-1;t->antialiasing_ms=scene_antialiasing::gpu_ready?float(scene_antialiasing::gpu.gpu_milliseconds()):-1;t->frame_ms=float(scene_antialiasing::frame_ms);return true;}
static const AnySceneTimingServiceV1 api{sizeof(AnySceneTimingServiceV1),1,copy};
}
static const void* anyapi_scene_timing_service(){return &scene_timing::api;}
