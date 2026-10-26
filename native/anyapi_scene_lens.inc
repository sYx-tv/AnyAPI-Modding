#include "effects/lens_gpu.h"
// Depth of field and camera motion blur: after the night sky, before finishing.
namespace scene_effects {
static scene_lens_gpu::Gpu lens_gpu;static uint32_t lens_frame_index{};
// Last frame's camera in world coordinates, for camera-only reprojection.
struct LensHistory {bool valid{};double position[3]{};std::array<float,4> right{},up{},forward{};double time{};uint32_t width{},height{};};
static LensHistory lens_history;
static void lens_bypass(int code,const char* why){lens_ready=false;lens_history.valid=false;pass_reject(lens_pass,code,why);}
// K30: QPC seconds (GetTickCount64 steps by 15.6 ms, so focus pulls and blur length were uneven).
static double lens_seconds(){static LARGE_INTEGER frequency{};if(!frequency.QuadPart)QueryPerformanceFrequency(&frequency);LARGE_INTEGER now{};QueryPerformanceCounter(&now);return frequency.QuadPart?double(now.QuadPart)/double(frequency.QuadPart):0;}
static void record_lens(void* renderer,void* scene){auto p=copy();if(!p.enabled||(p.depth_of_field<=0&&p.motion_blur<=0)){lens_ready=false;lens_history.valid=false;pass_off(lens_pass);return;}want(lens_pass);
 // K03: no view check (the lens works the same in bunkers and underwater).
 scene_effects_gpu::Native n;uint32_t view=0;if(!scene_effects_gpu::capture_view(uintptr_t(renderer),uintptr_t(scene),n,scene_frame::reader,view))return lens_bypass(600+int(n.failure),"native_frame_contract");
 note_size(n);
 double m[16]{},camera[3]{},origin[3]{};if(!scene_frame::read(uintptr_t(scene)+0x20+0x1a0,m,sizeof(m))||!scene_frame::read(uintptr_t(scene)+0x20+0x108,camera,sizeof(camera))||!scene_frame::read(uintptr_t(scene)+8,origin,sizeof(origin)))return lens_bypass(610,"camera");
 double world[3];for(int i=0;i<3;++i){world[i]=camera[i]+origin[i];if(!std::isfinite(world[i]))return lens_bypass(610,"camera");}
 scene_lens_gpu::Frame f;f.metrics={1.f/n.color.width,1.f/n.color.height,float(n.color.width),float(n.color.height)};
 for(int i=0;i<4;++i){f.right[i]=n.right[i];f.up[i]=n.up[i];f.forward[i]=n.forward[i];}f.forward[3]=5000;
 f.projection={float(m[10]),float(m[14]),float(m[11]),float(m[15])};
 // Drop history after a pause, a resize, a teleport-sized jump or a cut (K30: a turn of more than 60 degrees or a
 // zoom of more than 30% in one frame).
 double now=lens_seconds();auto& h=lens_history;double moved=0;for(int i=0;i<3;++i)moved+=(world[i]-h.position[i])*(world[i]-h.position[i]);
 float turn=n.forward[0]*h.forward[0]+n.forward[1]*h.forward[1]+n.forward[2]*h.forward[2];
 bool continuous=h.valid&&now>h.time&&now-h.time<.25&&h.width==n.color.width&&h.height==n.color.height&&moved<20.0*20.0&&turn>.5f&&h.right[3]>0&&std::abs(n.right[3]/h.right[3]-1)<.3f;
 float seconds=continuous?float(std::clamp(now-h.time,1e-4,.25)):1.f;
 if(continuous){f.previous_right=h.right;f.previous_up=h.up;f.previous_forward=h.forward;for(int i=0;i<3;++i)f.camera_move[i]=float(world[i]-h.position[i]);}
 f.camera_move[3]=continuous?1-std::exp(-seconds/.25f):1;
 f.params={p.depth_of_field,p.motion_blur,std::clamp(12.f*std::max(p.depth_of_field,1.f)*n.color.height/1080.f,4.f,48.f),taa_resolving()?float(lens_frame_index%64):0.f};
 h.valid=true;for(int i=0;i<3;++i)h.position[i]=world[i];h.right={n.right[0],n.right[1],n.right[2],n.right[3]};h.up={n.up[0],n.up[1],n.up[2],n.up[3]};h.forward={n.forward[0],n.forward[1],n.forward[2],0};h.time=now;h.width=n.color.width;h.height=n.color.height;
 auto list=scene_antialiasing::command_list();auto saved=scene_antialiasing::states.find(list);
 if(!scene_antialiasing::tracking||!list||saved==scene_antialiasing::states.end()||!saved->second.ready()||n.color.state!=D3D12_RESOURCE_STATE_RENDER_TARGET)return lens_bypass(620,"command_bindings_or_target_state");
 if(!interface_valid(n.color.resource)||!interface_valid(n.depth.resource))return lens_bypass(621,"resources");
 std::lock_guard lock(platform::render_mutex);Microsoft::WRL::ComPtr<ID3D12Device> device,owner;auto target=(ID3D12Resource*)n.color.resource;
 if(!platform::queue||FAILED(list->GetDevice(IID_PPV_ARGS(&device))))return lens_bypass(622,"device");
 for(auto r:{n.color.resource,n.depth.resource})if(FAILED(((ID3D12Resource*)r)->GetDevice(IID_PPV_ARGS(owner.ReleaseAndGetAddressOf())))||owner.Get()!=device.Get())return lens_bypass(622,"device");
 if(!ensure_gpu(lens_pass,lens_gpu,device.Get(),platform::queue.Get(),target->GetDesc()))return lens_bypass(623,"gpu_resources");
 // FIN-8: the focus probe sees the sea plane (gbuffer_ocean, which writes no depth) at the camera's height above
 // sea level (world y; sea level is y 0, as the water pass assumes). K29: gbuffer_0.w (vehicles, parts, characters)
 // fades camera blur on the player's own vehicle. Both optional: without them the pass is V2's.
 auto ocean=optional_input<scene_lens_gpu::Input>(renderer,0x208,n,device.Get()),albedo=optional_input<scene_lens_gpu::Input>(renderer,0x1e8,n,device.Get());
 if(std::abs(world[1])>=1e6)ocean={};f.sea={float(world[1]),ocean.resource?1.f:0.f,0,0};if(!ocean.resource)f.sea[0]=0;
 auto bindings=saved->second;scene_antialiasing::owned=true;bool rendered=lens_gpu.record(list,target,{(ID3D12Resource*)n.depth.resource,D3D12_RESOURCE_STATES(n.depth.state)},f,ocean,albedo);if(rendered)bindings.restore(list);scene_antialiasing::owned=false;
 if(!rendered)return lens_bypass(624,"gpu_record");
 ++lens_frame_index;lens_ready=true;lens_ms=float(lens_gpu.gpu_milliseconds());pass_done(lens_pass,lens_ms);static uint64_t count{};
 if(++count==1||count%600==0)log_line(ANY_LOG_INFO,"framework",("SCENE_LENS frames="+std::to_string(count)+" depth_of_field="+std::to_string(p.depth_of_field)+" motion_blur="+std::to_string(p.motion_blur)+" history="+std::to_string(int(continuous))+" ocean="+std::to_string(int(ocean.resource!=nullptr))+" rim="+std::to_string(int(albedo.resource!=nullptr))+" gpu_ms="+std::to_string(lens_ms.load())+" location=HDR_BEFORE_FINISH").c_str());
}
// K09: a pass switched off for 120 lighting frames frees its targets, PSOs and borrowed references (one queue drain
// per release, never per frame); the next record re-creates it. Called at the top of record() (effects.inc).
static void release_idle_passes(){
 auto idle=[](PassState& s,auto& g){if(!g.loaded()||fx_tick-s.last_wanted<=120)return;{std::lock_guard lock(platform::render_mutex);g.release();}s.retry.reset();
  log_line(ANY_LOG_INFO,"framework",(std::string(s.tag)+" released=1 idle_frames="+std::to_string(fx_tick-s.last_wanted)).c_str());};
 idle(finish_pass,gpu);idle(ao_pass,ao_gpu);idle(contact_pass,contact_gpu);idle(gi_pass,gi_gpu);idle(lens_pass,lens_gpu);}
}
