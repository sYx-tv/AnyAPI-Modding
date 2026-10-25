#include "anyapi_scene_timing_v2.h"
#include "lighting/volumetric_gpu.h"
#include "lighting/native_frame.h"
namespace scene_lighting {
static volumetric::Gpu gpu;
// K26: lamp identity for V4 callers with ANY_LAMP_TRACKING. Reset on a device change and when tracking stops.
static lighting::LampTracker tracker;static bool tracking{};
// CPU-13: ExecuteCommandLists skips the render mutex unless a recorded list still waits for its fence signal.
static std::atomic<bool> submit_pending{};
static void submitted(ID3D12CommandQueue* queue,UINT count,ID3D12CommandList*const* lists){if(!submit_pending.load(std::memory_order_acquire))return;std::lock_guard lock(platform::render_mutex);gpu.submitted(queue,count,lists);submit_pending.store(gpu.wants_submit(),std::memory_order_release);}
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
// K21: weather lightning (an omni of radius >= 4096) for every caller, before the enabled check: effects freezes eye
// adaptation on board.flash. Lighting itself only uses it for ANY_ATMOS_FLASH_GLOW.
static scene_frame::Flash flash{};
static void publish_flash(uintptr_t scene){
 lighting::Flash seen{};double c[3]{};float camera[3]{};
 if(scene&&scene_frame::read(scene+0x20+0x108,c,sizeof(c))&&std::isfinite(c[0])&&std::isfinite(c[1])&&std::isfinite(c[2])){for(int i=0;i<3;++i)camera[i]=float(c[i]);lighting::LampReader<decltype(scene_frame::reader)> lamps(scene_frame::reader);seen=lamps.scan_flash(scene,camera);}
 uint32_t run=seen.seen?flash.frames_total+1:0;if(run==1)++flashes_seen;
 flash={};flash.frame=scene_frame::board.frame.load(std::memory_order_relaxed);flash.active=seen.seen;flash.intensity=seen.intensity;flash.frames_total=run;for(int i=0;i<3;++i){flash.colour[i]=seen.colour[i];flash.direction[i]=seen.direction[i];}
 scene_frame::board.flash.store(flash);
}
// V4 extras: matrix row 21 (volumetric_frame.h) and the fog density. V1-V3 callers have Extras{}: nothing changes.
static uint32_t extra_rows(volumetric::NativeFrame& f,const Extras& x){
 if(!x.v4)return 0;auto& t=f.gpu.temporal[1];uint32_t active=0;
 scene_frame::Sample s{};bool sample=scene_frame::board.sample.load(s)&&scene_frame::fresh(s)&&(s.valid&scene_frame::SAMPLE_FOG)&&std::isfinite(s.native_fog)&&s.native_fog>0;
 scene_frame::Weather w{};bool weather=scene_frame::board.weather.load(w)&&scene_frame::fresh(w)&&std::isfinite(w.fog_ratio)&&w.fog_ratio>0;
 // WI-11: lamp air thickens with the game's own fog (0xb60, never the user's FogDensity) and with rain or snow.
 if(x.weather_response>0){float fog=sample?float(std::clamp(s.native_fog/.01,1.,4.)):1.f,wet=weather&&w.source?std::clamp(std::max(w.rain_fx,w.snow_fx),0.f,1.f):0.f;
  float k=std::min(4.f,std::pow(fog,.75f)*(1+wet));t[6]=1+x.weather_response*(k-1);}
 if((x.atmosphere_flags&ANY_ATMOS_WEATHER_DENSITY)&&!f.underground&&(weather||sample)){float ratio=weather?w.fog_ratio:float(std::clamp(s.native_fog/.01,.5,4.));f.gpu.medium[0]=std::min(.1f,f.gpu.medium[0]*ratio);active|=ANY_ATMOS_WEATHER_DENSITY;}
 // K05/WI-12: the ocean writes no depth. Mode 2: the whole ray ends at the sea plane; mode 1: only lamp light does.
 if(f.ocean&&(x.atmosphere_flags&ANY_ATMOS_SEA_PLANE)){t[8]=2;active|=ANY_ATMOS_SEA_PLANE;}else if(f.ocean&&(x.flags&ANY_LAMP_OCEAN))t[8]=1;
 t[9]=0;   // sea level: world height 0 (waves +-0.1)
 if(x.atmosphere_flags&ANY_ATMOS_PHASE_CAP){t[10]=1;active|=ANY_ATMOS_PHASE_CAP;}
 if((x.atmosphere_flags&ANY_ATMOS_FLASH_GLOW)&&(x.flags&ANY_LAMP_FLASHES)){active|=ANY_ATMOS_FLASH_GLOW;if(flash.active)for(int i=0;i<3;++i)t[12+i]=std::clamp(flash.colour[i]*flash.intensity*.5f,0.f,4.f);}
 return active;
}
// Lamp beams on AMD (an RX 6800) blew the whole frame out into flat magenta, green and yellow. AnyAPI 0.39.1 switched
// them off there. 0.39.2 gives AMD its own lamp path instead: lights in a constant buffer read by an unrolled
// loop (IntegrateConstants), and no lamp shadow maps. Every other GPU keeps the exact same path.
// K24: the vendor comes from board.gpu (platform, first execute) when it names this device; otherwise DXGI, once.
static std::atomic<uint32_t> gpu_vendor{},gpu_device{},gpu_known{};
static bool amd_gpu(ID3D12Device* device){static ID3D12Device* checked{};static bool amd{};if(!device||device==checked)return amd;checked=device;amd=false;
 auto luid=device->GetAdapterLuid();uint64_t key=(uint64_t(uint32_t(luid.HighPart))<<32)|luid.LowPart;scene_frame::GpuIdentity id{};const char* source="board";
 if(scene_frame::board.gpu.load(id)&&id.known&&id.luid==key){amd=id.vendor_id==0x1002;gpu_vendor=id.vendor_id;gpu_device=id.device_id;gpu_known=1;}
 else{source="dxgi";Microsoft::WRL::ComPtr<IDXGIFactory4> factory;Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;DXGI_ADAPTER_DESC1 desc{};if(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))&&SUCCEEDED(factory->EnumAdapterByLuid(luid,IID_PPV_ARGS(&adapter)))&&SUCCEEDED(adapter->GetDesc1(&desc))){amd=desc.VendorId==0x1002;gpu_vendor=desc.VendorId;gpu_device=desc.DeviceId;gpu_known=1;}}
 log_line(ANY_LOG_INFO,"framework",("SCENE_LIGHTING gpu_vendor="+std::to_string(gpu_vendor.load())+" source="+source+(amd?" lamp_beams=constant_buffer_path":"")).c_str());return amd;}
static void pass(float ms,int32_t code){scene_frame::board.pass_ms[ANY_PASS_VOLUMETRIC].store(ms,std::memory_order_relaxed);scene_frame::board.pass_error[ANY_PASS_VOLUMETRIC].store(code,std::memory_order_relaxed);}
// Bypass: not drawn this frame for a reason worth a warning (reason codes in docs/scene-lighting.md).
// History survives a bypass, as before: the camera and the selector stay those of the last rendered frame.
static void bypass(uint32_t code,const char* what,int64_t detail=0){gpu_ready=false;reason=code;bypass_reason=code;pass(-1,int32_t(code));auto n=++rejected;if(n==1||n%600==0)log_line(ANY_LOG_WARN,"framework",("SCENE_LIGHTING bypass="+std::string(what)+" reason="+std::to_string(code)+" detail="+std::to_string(detail)).c_str());}
static double seconds(){static LARGE_INTEGER frequency{};if(!frequency.QuadPart)QueryPerformanceFrequency(&frequency);LARGE_INTEGER now;QueryPerformanceCounter(&now);return double(now.QuadPart)/double(frequency.QuadPart);}
static uint64_t diag_frames{};
// K09: 120 lighting frames after the pass went off (disabled, or an inactive owner) the GPU objects go, once: one
// queue drain on that transition, never per frame. A pass that is on but bypassing keeps them (no churn).
static uint32_t off_frames{};
static void release_when_off(){if(off_frames>120||++off_frames<=120)return;
 if(submit_pending.load(std::memory_order_acquire)){off_frames=120;return;}   // a recorded list still waits for its submit
 std::lock_guard lock(platform::render_mutex);if(!gpu.loaded())return;gpu.release();history.valid=false;tracker.reset();gpu_ready=false;
 log_line(ANY_LOG_INFO,"framework","SCENE_LIGHTING released=1 off_frames=121");}
// K43: a failed GPU build (PSO compile or allocation) is retried after 1, 2, 4 .. 60 s, never every frame. A new
// device starts a fresh schedule.
static scene_frame::Backoff retry;static ID3D12Device* retry_device{};
static void record(void* renderer,void* scene){
 publish_flash(uintptr_t(scene));
 auto policy=copy_policy();auto& p=policy.v2.scene;auto& x=policy.extras;if(!p.enabled){pass(-1,0);release_when_off();return;}off_frames=0;
 auto list=scene_antialiasing::command_list();Microsoft::WRL::ComPtr<ID3D12Device> device;if(list)list->GetDevice(IID_PPV_ARGS(&device));
 bool amd=amd_gpu(device.Get());uint32_t path=amd?(x.amd_path==2?volumetric::PATH_BRANCH:volumetric::PATH_CONSTANTS):volumetric::PATH_MARCHED;
 volumetric::CaptureOptions o;if(x.v4&&(x.flags&ANY_LAMP_UNDERGROUND)){o.view_mask=0xfe;o.underground_lamps=true;}o.ocean=x.v4&&((x.atmosphere_flags&ANY_ATMOS_SEA_PLANE)||(x.flags&ANY_LAMP_OCEAN));
 bool track=x.v4&&(x.flags&ANY_LAMP_TRACKING);if(track!=tracking){tracker.reset();tracking=track;}if(track){o.tracker=&tracker;o.now=seconds();}
 uint32_t budget=x.v4?x.lamp_budget:policy.v2.local_budget;bool shadows=!amd&&(!x.v4||x.lamp_shadows!=0);
 volumetric::NativeFrame f;static LARGE_INTEGER qf{};if(!qf.QuadPart)QueryPerformanceFrequency(&qf);LARGE_INTEGER t0,t1;QueryPerformanceCounter(&t0);
 bool valid=volumetric::capture(uintptr_t(renderer),uintptr_t(scene),p,f,scene_frame::reader,policy.v2.local_strength,budget,shadows,o);
 QueryPerformanceCounter(&t1);float us=float(double(t1.QuadPart-t0.QuadPart)*1e6/double(qf.QuadPart)),old=capture_us.load();capture_us.store(old<0?us:old*.9f+us*.1f);
 frame_valid=valid;candidates_omni=f.lamps.omni;candidates_spot=f.lamps.spot;game_spot_maps=f.spot_maps;raw29_hits+=f.lamps.raw29_hits;raw29_misses+=f.lamps.raw29_misses;underground_active=f.underground;amd_path_active=amd?path:0;lamp_shadows_active=shadows;
 // K03: underwater, night vision and thermal (and underground without ANY_LAMP_UNDERGROUND) are inactive by design.
 if(!valid&&f.failure==2&&f.view!=scene_frame::VIEW_UNREADABLE){gpu_ready=false;reason=7;history.valid=false;lamps_drawn=lamps_shadowed=0;pass(-1,-1);return;}
 if(valid)temporal_rows(f,p,policy.shaping);
 // The ocean target must be a live resource of this device, or no sea mode is used.
 if(valid&&f.ocean){Microsoft::WRL::ComPtr<ID3D12Device> od;if(!device||!scene_frame::resource_valid(f.ocean_target.resource)||FAILED(((ID3D12Resource*)f.ocean_target.resource)->GetDevice(IID_PPV_ARGS(&od)))||od.Get()!=device.Get())f.ocean=false;}
 if(valid)atmosphere_active=extra_rows(f,x);
 if(!valid||!volumetric::valid(f.gpu)){frame_valid=false;history.valid=false;return bypass(100+f.failure,"native_frame_contract",f.failure);}
 lamps_drawn=uint32_t(f.gpu.fog_color[3]);lamps_shadowed=f.lamps.shadowed;
 if(++diag_frames==1||diag_frames%600==0)log_line(ANY_LOG_INFO,"framework",("LAMP_DIAG omni="+std::to_string(f.lamps.omni)+" spot="+std::to_string(f.lamps.spot)+" flashes="+std::to_string(f.lamps.flashes)+" rejected="+std::to_string(f.lamps.rejected)+" drawn="+std::to_string(lamps_drawn.load())+" shadowed="+std::to_string(f.lamps.shadowed)+" raw29_hits="+std::to_string(raw29_hits.load())+" raw29_misses="+std::to_string(raw29_misses.load())+" tracked="+std::to_string(f.lamps.tracked)+" spot_maps="+std::to_string(f.spot_maps)+" capture_us="+std::to_string(capture_us.load())+" path="+std::to_string(path)+" ring_waits="+std::to_string(gpu.ring_wait_count())+" idle_calls="+std::to_string(gpu.idle_count())).c_str());
 // K25, output-identical only. The sun cascades matter only to shafts and to clarity-lit fog.
 auto& g=f.gpu;if(g.controls[2]<=0&&(g.temporal[1][0]<=0||g.controls[3]<=0))g.sun[3]=0;
 bool fog=g.controls[3]>0&&g.medium[0]>0,sun=g.controls[2]>0&&(g.sun_color[0]!=0||g.sun_color[1]!=0||g.sun_color[2]!=0)&&!(g.temporal[1][2]==1&&g.sun[1]<=-.05f),lamps=g.fog_color[3]>0&&g.sun_color[3]>0,glow=g.temporal[1][12]>0||g.temporal[1][13]>0||g.temporal[1][14]>0;
 // Nothing adds light or fog: the composite would leave the target as it is. The history the skipped frames would
 // have written is (0,0,0,1), so the next rendered frame starts from a cleared history with the camera kept.
 if(!fog&&!sun&&!lamps&&!glow){gpu_ready=false;reason=8;if(g.temporal[1][3]==1)remember(f);else history.valid=false;{std::lock_guard lock(platform::render_mutex);gpu.forget_history();}pass(0,0);return;}
 if(!scene_antialiasing::tracking||!list)return bypass(2,"command_bindings_or_target_state",f.color.state);
 auto saved=scene_antialiasing::states.find(list);if(saved==scene_antialiasing::states.end()||!saved->second.ready()||!scene_frame::resource_valid(f.color.resource)||!scene_frame::resource_valid(f.depth.resource)||!scene_frame::resource_valid(f.shadow.resource)||f.color.state!=D3D12_RESOURCE_STATE_RENDER_TARGET)return bypass(2,"command_bindings_or_target_state",f.color.state);
 std::lock_guard lock(platform::render_mutex);if(!platform::queue||!device)return bypass(3,"queue_or_device");
 platform::after_execute=submitted;volumetric::Gpu::ProbeStats stats;if(gpu.probe(stats))log_line(ANY_LOG_INFO,"framework",("SHAFT_PROBE lit_min="+std::to_string(stats.lit_min)+" lit_max="+std::to_string(stats.lit_max)+" lit_avg="+std::to_string(stats.lit_average)+" coverage="+std::to_string(stats.coverage)+" distance_fraction="+std::to_string(stats.distance)).c_str());
 auto target=(ID3D12Resource*)f.color.resource;auto depth=(ID3D12Resource*)f.depth.resource;auto shadow=(ID3D12Resource*)f.shadow.resource;Microsoft::WRL::ComPtr<ID3D12Device> td,dd,sd;auto before=gpu.current_device();
 if(FAILED(target->GetDevice(IID_PPV_ARGS(&td)))||FAILED(depth->GetDevice(IID_PPV_ARGS(&dd)))||FAILED(shadow->GetDevice(IID_PPV_ARGS(&sd)))||td.Get()!=device.Get()||dd.Get()!=device.Get()||sd.Get()!=device.Get())return bypass(4,"gpu_resource_contract");
 uint64_t now_ms=GetTickCount64();if(device.Get()!=retry_device){retry.reset();retry_device=device.Get();}if(!retry.ready(now_ms))return bypass(4,"gpu_retry",retry.failures);
 if(!gpu.ensure(device.Get(),platform::queue.Get(),target->GetDesc(),path)){retry.failed(now_ms);error=gpu.error();return bypass(4,"gpu_resource_contract",error.load());}retry.succeeded();
 if(before&&before!=device.Get()){scene_frame::reset_resource_cache();tracker.reset();history.valid=false;}
 std::array<ID3D12Resource*,12> shadow_list{};std::array<D3D12_RESOURCE_STATES,12> shadow_states{};
 uint32_t bound_shadows=f.gpu.local_shadow_count>0?8+uint32_t(f.gpu.local_shadow_count):f.shadow_count;for(uint32_t i=0;i<bound_shadows;++i){if(i>=f.shadow_count&&i<8)f.cascades[i]=f.cascades[0];if(!scene_frame::resource_valid(f.cascades[i].resource))return bypass(4,"shadow_resource",i);shadow_list[i]=(ID3D12Resource*)f.cascades[i].resource;shadow_states[i]=D3D12_RESOURCE_STATES(f.cascades[i].state);Microsoft::WRL::ComPtr<ID3D12Device> owner;if(FAILED(shadow_list[i]->GetDevice(IID_PPV_ARGS(&owner)))||owner.Get()!=device.Get())return bypass(4,"shadow_device",i);}
 volumetric::Ocean ocean;if(f.ocean){ocean.resource=(ID3D12Resource*)f.ocean_target.resource;ocean.state=D3D12_RESOURCE_STATES(f.ocean_target.state);}
 auto bindings=saved->second;scene_antialiasing::owned=true;bool rendered=gpu.record(list,target,depth,D3D12_RESOURCE_STATES(f.depth.state),shadow_list,shadow_states,f.gpu,ocean);if(rendered)bindings.restore(list);scene_antialiasing::owned=false;submit_pending.store(gpu.wants_submit(),std::memory_order_release);ring_waits=gpu.ring_wait_count();
 // RING_WAIT: a descriptor table or lamp slot was still in use (a targeted fence wait), or its list was never seen
 // submitted (one idle). Rare by design; logged on the first and every 600th occurrence.
 {static uint32_t seen{},logged{};uint32_t now=gpu.ring_wait_count()+gpu.ring_unconfirmed_count();if(now!=seen){seen=now;if(++logged==1||logged%600==0)log_line(ANY_LOG_WARN,"framework",("SCENE_LIGHTING RING_WAIT waits="+std::to_string(gpu.ring_wait_count())+" unconfirmed="+std::to_string(gpu.ring_unconfirmed_count())+" idle_calls="+std::to_string(gpu.idle_count())).c_str());}}
 width=f.color.width;height=f.color.height;shadow_count=f.shadow_count;
 if(!rendered){history.valid=false;return bypass(5,"gpu_record",gpu.ring_unconfirmed_count());}
 gpu_ready=true;reason=0;remember(f);pass(float(std::max(gpu.gpu_milliseconds(),0.)),0);
 auto count=++frames;if(count==1||count%600==0)log_line(ANY_LOG_INFO,"framework",("SCENE_LIGHTING frames="+std::to_string(count)+" location=HDR_BEFORE_BLOOM_TONEMAP_HUD depth_valid=1 sun_cascades="+std::to_string(f.shadow_count)+" quality="+std::to_string(p.quality)+" camera_world_height="+std::to_string(f.gpu.camera[3])+" sunlight_y="+std::to_string(f.gpu.sun[1])+" sun_rgb="+std::to_string(f.gpu.sun_color[0])+","+std::to_string(f.gpu.sun_color[1])+","+std::to_string(f.gpu.sun_color[2])+" local_lights="+std::to_string(uint32_t(f.gpu.fog_color[3]))+" local_shadows="+std::to_string(uint32_t(f.gpu.local_shadow_count))+" view_sun_cos="+std::to_string(f.gpu.sun[0]*f.gpu.forward[0]+f.gpu.sun[1]*f.gpu.forward[1]+f.gpu.sun[2]*f.gpu.forward[2])+" path="+std::to_string(path)+" sea="+std::to_string(uint32_t(f.gpu.temporal[1][8]))+" underground="+std::to_string(f.underground?1:0)).c_str());
}
}
namespace scene_timing {
static bool copy(AnySceneTimingV1* t){if(!t||t->struct_size!=sizeof(*t)||t->version!=1)return false;// Plain reads of smoothed doubles; a slightly stale value is fine for a readout.
 t->lighting_ms=scene_lighting::gpu_ready?float(scene_lighting::gpu.gpu_milliseconds()):-1;t->antialiasing_ms=scene_antialiasing::gpu_ready?float(scene_antialiasing::gpu.gpu_milliseconds()):-1;t->frame_ms=float(scene_antialiasing::frame_ms);return true;}
static const AnySceneTimingServiceV1 api{sizeof(AnySceneTimingServiceV1),1,copy};
}
static const void* anyapi_scene_timing_service(){return &scene_timing::api;}
namespace gpu_info {
// K11/K24: board.gpu (platform, first execute) when known; else what lighting's own detection found.
static bool copy(AnyGpuInfoV1* g){if(!g||g->struct_size!=sizeof(*g)||g->version!=1)return false;scene_frame::GpuIdentity id{};if(scene_frame::board.gpu.load(id)&&id.known){g->known=1;g->vendor_id=id.vendor_id;g->device_id=id.device_id;}else{g->known=scene_lighting::gpu_known;g->vendor_id=scene_lighting::gpu_vendor;g->device_id=scene_lighting::gpu_device;}g->lamp_beams_supported=1;return true;}
static const AnyGpuInfoServiceV1 api{sizeof(AnyGpuInfoServiceV1),1,copy};
}
static const void* anyapi_gpu_info_service(){return &gpu_info::api;}
