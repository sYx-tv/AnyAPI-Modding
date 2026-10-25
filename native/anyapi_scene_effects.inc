#include "effects/effects_gpu.h"
#include "anyapi_scene_frame.h"
#include "anyapi_scene_timing_v2.h"
namespace scene_effects {
static scene_effects_gpu::Gpu gpu;static LARGE_INTEGER last_tick{};static bool history{};
// Per-pass bookkeeping for finish, AO, contact, GI and lens: board.pass_ms/pass_error (timing v2, status v3),
// a bypass counter per pass for logging (K46), the last frame it was switched on (K09 release) and the K43 retry.
struct PassState {const char* tag;uint32_t index;uint64_t rejected{},last_wanted{};scene_frame::Backoff retry{};};
static PassState finish_pass{"SCENE_EFFECTS",ANY_PASS_FINISH},ao_pass{"SCENE_AO",ANY_PASS_AO},contact_pass{"SCENE_CONTACT",ANY_PASS_CONTACT},gi_pass{"SCENE_GI",ANY_PASS_GI},lens_pass{"SCENE_LENS",ANY_PASS_LENS};
static uint64_t fx_tick{};   // ++ once per lighting_hook frame, in record() below
static void release_idle_passes();   // anyapi_scene_lens.inc (needs every pass's Gpu)
static void pass_off(PassState& s){auto& b=scene_frame::board;b.pass_ms[s.index].store(-1,std::memory_order_relaxed);b.pass_error[s.index].store(0,std::memory_order_relaxed);}
// Inactive by design (K03 view, night, nothing to do): no WARN, no rejected count.
static void pass_quiet(PassState& s){auto& b=scene_frame::board;b.pass_ms[s.index].store(0,std::memory_order_relaxed);b.pass_error[s.index].store(-1,std::memory_order_relaxed);}
static void pass_done(PassState& s,float ms){auto& b=scene_frame::board;b.pass_ms[s.index].store(ms,std::memory_order_relaxed);b.pass_error[s.index].store(0,std::memory_order_relaxed);}
static void want(PassState& s){s.last_wanted=fx_tick;}   // the pass is switched on this frame (even if it then skips or bypasses)
// V1 status keeps the shared error/rejected fields; each pass also logs its own first and every 600th bypass.
static void pass_reject(PassState& s,int code,const char* why,const std::string& detail={}){auto& b=scene_frame::board;b.pass_ms[s.index].store(-1,std::memory_order_relaxed);b.pass_error[s.index].store(code,std::memory_order_relaxed);
 error=code;++rejected;auto n=++s.rejected;if(n==1||n%600==0)log_line(ANY_LOG_WARN,"framework",(std::string(s.tag)+" bypass="+why+" code="+std::to_string(code)+" count="+std::to_string(n)+detail).c_str());}
// K13: noise moves per frame only while TAA resolves it this frame (aa writes board.temporal); otherwise it is fixed.
static bool taa_resolving(){scene_frame::Temporal t{};return scene_frame::board.temporal.load(t)&&scene_frame::fresh(t)&&t.taa_active;}
static void note_size(const scene_effects_gpu::Native& n){scene_frame::board.scene_width.store(n.color.width,std::memory_order_relaxed);scene_frame::board.scene_height.store(n.color.height,std::memory_order_relaxed);}
// K43 + K09: a failed create retries at 1, 2, 4 .. 60 s instead of every frame; any re-create forgets the cached vtables.
template<class G> static bool ensure_gpu(PassState& s,G& g,ID3D12Device* d,ID3D12CommandQueue* q,const D3D12_RESOURCE_DESC& desc){
 if(g.current(d,q,desc))return true;uint64_t now=GetTickCount64();if(!s.retry.ready(now))return false;scene_frame::reset_resource_cache();
 if(g.ensure(d,q,desc)){s.retry.succeeded();return true;}s.retry.failed(now);return false;}
// K21 (v3 weather 1, "hold exposure on lightning"): while a flash is active (board.flash, lighting) and .25 s after it
// the adaptation weights are 0, at most 1.5 s in any 5 s window so a long storm cannot freeze it.
struct FlashHold {double hold{},window{},used{};};static FlashHold flash_state;
static bool flash_hold(double step,bool enabled){auto& h=flash_state;h.window+=step;if(h.window>=5){h.window=0;h.used=0;}
 scene_frame::Flash flash{};if(enabled&&scene_frame::board.flash.load(flash)&&scene_frame::fresh(flash)&&flash.active)h.hold=.25;else h.hold=std::max(0.,h.hold-step);
 bool frozen=h.hold>0&&h.used<1.5;if(frozen)h.used+=step;return frozen;}
// Seconds since the previous effects frame; long gaps restart eye adaptation.
static double elapsed(){LARGE_INTEGER now{},frequency{};QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);double dt=last_tick.QuadPart&&frequency.QuadPart?double(now.QuadPart-last_tick.QuadPart)/double(frequency.QuadPart):-1;last_tick=now;return dt;}
// K02: frame-core's cached vtable test (any non-exe image module, so wrapper layers pass). Sky and water use it too.
static bool interface_valid(uintptr_t resource){return resource&&scene_frame::resource_valid(resource);}
static void bypass(int code,const char* why){gpu_ready=false;dither_active=false;history=false;pass_reject(finish_pass,code,why);}
// An optional game target (gbuffer_1, gbuffer_ocean): same size and device as the HDR target, else null (the pass falls back).
template<class In> static In optional_input(void* renderer,uintptr_t offset,const scene_effects_gpu::Native& n,ID3D12Device* device){
 uintptr_t object{};volumetric::NativeTarget t;if(!scene_frame::read_value(uintptr_t(renderer)+offset,object)||!volumetric::target(object,t,scene_frame::reader)||t.width!=n.color.width||t.height!=n.color.height||!interface_valid(t.resource))return {};
 Microsoft::WRL::ComPtr<ID3D12Device> owner;if(FAILED(((ID3D12Resource*)t.resource)->GetDevice(IID_PPV_ARGS(&owner)))||owner.Get()!=device)return {};return In{(ID3D12Resource*)t.resource,D3D12_RESOURCE_STATES(t.state)};}
// Runs at the HDR additive stage after volumetric lighting.
static void record(void* renderer,void* scene){++fx_tick;release_idle_passes();auto p=copy();auto x=copy_extras();
 if(!p.enabled||!p.finish){history=false;gpu_ready=false;dither_active=false;pass_off(finish_pass);return;}
 // FIN-12: nothing to change, so no copy and no composite. finish_since_ok is not reset: after 15 frames controls
 // stops forcing the game's bloom off (K04), so this is exactly the game's own image.
 if(p.tonemap==0&&(p.look==0||p.look_strength<=0)&&p.exposure==0&&!p.eye_adaptation&&p.bloom<=0&&p.glare<=0&&p.vignette<=0&&!x.dither){
  history=false;gpu_ready=true;error=0;gpu_ms=0;dither_active=false;adapted_exposure=0;scene_luminance=-1;sun_visibility=0;pass_quiet(finish_pass);return;}
 want(finish_pass);
 scene_effects_gpu::Native n;uint32_t view=0;if(!scene_effects_gpu::capture_view(uintptr_t(renderer),uintptr_t(scene),n,scene_frame::reader,view)){bypass(100+int(n.failure),"native_frame_contract");return;}
 // K03: night vision and thermal are the game's own post looks; leave them alone, quietly.
 if(view&scene_frame::VIEW_SPECIAL_VISION){history=false;gpu_ready=false;dither_active=false;pass_quiet(finish_pass);return;}
 note_size(n);
 auto list=scene_antialiasing::command_list();auto saved=scene_antialiasing::states.find(list);
 if(!scene_antialiasing::tracking||!list||saved==scene_antialiasing::states.end()||!saved->second.ready()||!interface_valid(n.color.resource)||!interface_valid(n.depth.resource)||n.color.state!=D3D12_RESOURCE_STATE_RENDER_TARGET){bypass(2,"command_bindings_or_target_state");return;}
 std::lock_guard lock(platform::render_mutex);Microsoft::WRL::ComPtr<ID3D12Device> device,td,dd;auto target=(ID3D12Resource*)n.color.resource;auto depth=(ID3D12Resource*)n.depth.resource;
 if(!platform::queue||FAILED(list->GetDevice(IID_PPV_ARGS(&device)))||FAILED(target->GetDevice(IID_PPV_ARGS(&td)))||FAILED(depth->GetDevice(IID_PPV_ARGS(&dd)))||td.Get()!=device.Get()||dd.Get()!=device.Get()){bypass(3,"device");return;}
 if(!ensure_gpu(finish_pass,gpu,device.Get(),platform::queue.Get(),target->GetDesc())){bypass(4,"gpu_resources");return;}
 double dt=elapsed();bool reset=!history||dt<0||dt>.5;double step=std::clamp(dt,0.,.1);
 bool frozen=flash_hold(step,x.weather!=0);if(frozen&&history)reset=false;   // K21: no reset onto a lightning frame either
 scene_effects_gpu::Frame f;f.metrics={1.f/n.color.width,1.f/n.color.height,float(n.color.width),float(n.color.height)};f.sun_screen=scene_effects_gpu::sun_screen(n);
 f.sun_colour={n.sun_colour[0],n.sun_colour[1],n.sun_colour[2],p.glare};if(p.glare<=0)f.sun_screen[2]=0;
 // K03: underground and underwater keep curve, look, bloom and exposure; there is no sun to glare at.
 if(view&(scene_frame::VIEW_UNDERGROUND|scene_frame::VIEW_UNDERWATER))f.sun_screen[2]=0;
 f.grading={float(p.tonemap),float(p.look),p.look_strength,p.exposure};
 // Fast blend follows the chosen adaptation time; the slow baseline takes about
 // a minute, so the game's own day/night brightness always comes back.
 f.exposure={float(p.eye_adaptation),frozen?0.f:float(1-std::exp(-step/p.adaptation_seconds)),frozen?0.f:float(1-std::exp(-step/45.)),1.5f};
 f.extras={p.bloom*.12f,p.vignette,0,0};
 // v3 (V1/V2 get r 1, revision 0, dither 1): FIN-20 bloom falloff, FIN-10 Filmic revision, FIN-4 dither, FIN-3 visibility blend (about 40 ms).
 f.tuning={x.bloom_falloff,float(x.tone_revision),float(x.dither),float(1-std::exp(-step/.04))};
 // FIN-2: gbuffer_1 (.w game clouds) and gbuffer_ocean make clouds and open sea block the sun test. Bound every frame
 // (a change of inputs drains the queue once), so a missing one only falls back to the depth-only test.
 auto normals=optional_input<scene_effects_gpu::Input>(renderer,0x1f8,n,device.Get()),ocean=optional_input<scene_effects_gpu::Input>(renderer,0x208,n,device.Get());
 auto bindings=saved->second;scene_antialiasing::owned=true;bool rendered=gpu.record(list,target,depth,D3D12_RESOURCE_STATES(n.depth.state),f,reset,normals,ocean);if(rendered)bindings.restore(list);scene_antialiasing::owned=false;
 if(!rendered){bypass(5,"gpu_record");return;}
 // K25: an unmetered frame leaves no history, so the next metered one starts fresh.
 bool metered=scene_effects_gpu::meters(f);history=metered;gpu_ready=true;error=0;dither_active=x.dither!=0;gpu_ms=float(gpu.gpu_milliseconds());pass_done(finish_pass,gpu_ms);
 scene_frame::board.finish_since_ok.store(0,std::memory_order_relaxed);   // K04: controls keeps the game's bloom off while this renders
 auto m=gpu.measured();
 if(!metered){adapted_exposure=0;scene_luminance=-1;sun_visibility=0;}
 else if(m.valid){adapted_exposure=p.eye_adaptation?std::clamp((m.slow-m.fast)*.75f,-1.5f,1.5f):0.f;scene_luminance=std::exp2(m.fast);sun_visibility=p.glare>0?m.visibility:0.f;}
 auto count=++frames;if(count==1||count%600==0)log_line(ANY_LOG_INFO,"framework",("SCENE_EFFECTS frames="+std::to_string(count)+" tonemap="+std::to_string(p.tonemap)+" look="+std::to_string(p.look)+" bloom="+std::to_string(p.bloom)+" scene_luminance="+std::to_string(scene_luminance.load())+" adapted_stops="+std::to_string(adapted_exposure.load())+" sun_visibility="+std::to_string(sun_visibility.load())+" sun_y="+std::to_string(n.sun[1])+" view="+std::to_string(view)+" metered="+std::to_string(int(metered))+" dither="+std::to_string(x.dither)+" tone_revision="+std::to_string(x.tone_revision)+" bloom_falloff="+std::to_string(x.bloom_falloff)+" frozen="+std::to_string(int(frozen))+" gpu_ms="+std::to_string(gpu_ms.load())+" location=HDR_AFTER_LIGHTING_BEFORE_GAME_TONEMAP").c_str());
}
}
