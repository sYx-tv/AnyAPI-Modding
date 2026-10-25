#include "aa/scene_smaa_gpu.h"
#include "aa/scene_smaa_bindings.h"
#include "aa/scene_taa_frame.h"
#include "aa/scene_jitter.h"
#include "anyapi_scene_timing_v2.h"
#include "runtime_call_cell.h"
namespace scene_antialiasing {
using Microsoft::WRL::ComPtr;
static scene_smaa::Gpu gpu;
static thread_local bool tracking{},owned{};
// The native command-list implementation normally shares one vtable. Cache
// each chained method per thread; other vtables still resolve independently.
template<class T> static T chained(ID3D12GraphicsCommandList* list,size_t index){struct Entry{void** slot{};void* call{};};static thread_local Entry cache[64]{};auto slot=(*(void***)list)+index;auto& entry=cache[index];if(entry.slot!=slot){entry.call=(void*)platform::original<T>(list,index);entry.slot=slot;}return (T)entry.call;}
// K23: see scene_smaa::ListStates (4 lists, oldest replaced, last hit cached).
static scene_smaa::ListStates states;
static scene_smaa::Bindings* capture(ID3D12GraphicsCommandList* l){if(!tracking||owned)return nullptr;auto s=states.find(l);if(!s)s=states.insert(l);return &s->second;}
// Explicit forwarding signatures preserve COM calling convention and existing hooks.
static void STDMETHODCALLTYPE signature(ID3D12GraphicsCommandList* l,ID3D12RootSignature* r){if(auto s=capture(l))s->signature(r);auto fn=chained<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12RootSignature*)>(l,30);fn(l,r);}
static void STDMETHODCALLTYPE pipeline(ID3D12GraphicsCommandList* l,ID3D12PipelineState* p){if(auto s=capture(l))s->pipeline=p;chained<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12PipelineState*)>(l,25)(l,p);}
static void STDMETHODCALLTYPE heaps(ID3D12GraphicsCommandList* l,UINT n,ID3D12DescriptorHeap*const* h){if(auto s=capture(l)){s->heap_count=std::min(n,2u);if(n>2)s->valid=false;for(UINT i=0;i<s->heap_count;++i)s->heaps[i]=h[i];}chained<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,ID3D12DescriptorHeap*const*)>(l,28)(l,n,h);}
static void STDMETHODCALLTYPE topology(ID3D12GraphicsCommandList* l,D3D12_PRIMITIVE_TOPOLOGY p){if(auto s=capture(l))s->topology=p;chained<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,D3D12_PRIMITIVE_TOPOLOGY)>(l,20)(l,p);}
static void STDMETHODCALLTYPE viewports(ID3D12GraphicsCommandList* l,UINT n,const D3D12_VIEWPORT* p){if(auto s=capture(l)){s->viewport_count=std::min(n,16u);if(n>16)s->valid=false;std::copy_n(p,s->viewport_count,s->viewports.begin());}chained<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,const D3D12_VIEWPORT*)>(l,21)(l,n,p);}
static void STDMETHODCALLTYPE scissors(ID3D12GraphicsCommandList* l,UINT n,const D3D12_RECT* p){if(auto s=capture(l)){s->scissor_count=std::min(n,16u);if(n>16)s->valid=false;std::copy_n(p,s->scissor_count,s->scissors.begin());}chained<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,const D3D12_RECT*)>(l,22)(l,n,p);}
static void STDMETHODCALLTYPE targets(ID3D12GraphicsCommandList* l,UINT n,const D3D12_CPU_DESCRIPTOR_HANDLE* p,BOOL contiguous,const D3D12_CPU_DESCRIPTOR_HANDLE* depth){if(auto s=capture(l)){s->target_count=std::min(n,8u);s->has_depth=depth!=nullptr;if(depth)s->depth=*depth;if(n>8)s->valid=false;UINT stride=0;if(contiguous&&n>1){ComPtr<ID3D12Device> dev;if(SUCCEEDED(l->GetDevice(IID_PPV_ARGS(&dev))))stride=dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);else s->valid=false;}for(UINT i=0;i<s->target_count;++i)s->targets[i]=contiguous?D3D12_CPU_DESCRIPTOR_HANDLE{p[0].ptr+SIZE_T(i)*stride}:p[i];}chained<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,const D3D12_CPU_DESCRIPTOR_HANDLE*,BOOL,const D3D12_CPU_DESCRIPTOR_HANDLE*)>(l,46)(l,n,p,contiguous,depth);}
static void STDMETHODCALLTYPE table(ID3D12GraphicsCommandList* l,UINT i,D3D12_GPU_DESCRIPTOR_HANDLE p){if(auto s=capture(l))s->descriptor(i,1,p.ptr);chained<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,D3D12_GPU_DESCRIPTOR_HANDLE)>(l,32)(l,i,p);}
#define AA_ADDRESS(name,slot,kind) static void STDMETHODCALLTYPE name(ID3D12GraphicsCommandList* l,UINT i,D3D12_GPU_VIRTUAL_ADDRESS p){if(auto s=capture(l))s->descriptor(i,kind,p);chained<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,D3D12_GPU_VIRTUAL_ADDRESS)>(l,slot)(l,i,p);}
AA_ADDRESS(cbv,38,2) AA_ADDRESS(srv,40,3) AA_ADDRESS(uav,42,4)
#undef AA_ADDRESS
static void STDMETHODCALLTYPE constant(ID3D12GraphicsCommandList* l,UINT i,UINT value,UINT offset){if(auto s=capture(l))s->constants(i,1,&value,offset);chained<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,UINT,UINT)>(l,34)(l,i,value,offset);}
static void STDMETHODCALLTYPE constants(ID3D12GraphicsCommandList* l,UINT i,UINT n,const void* p,UINT offset){if(auto s=capture(l))s->constants(i,n,p,offset);chained<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,UINT,const void*,UINT)>(l,36)(l,i,n,p,offset);}
// 0.1.27: the graphics context getter returns RVA 0x5e23d0 and the current command list is at +0x2650
// (0.1.25: 0x5e5340 and +0x2648). See docs/development/scene-antialiasing.md. The vtable check only proves the
// pointer is a COM object from a system DLL, so a stale offset cannot hook game memory; a neighbouring D3D12 object
// would still pass, so recheck the offset on every game update.
// K01: two in-process reads per call (the pointer is never cached across hooks); the
// VirtualQuery check runs only when the pointer or its vtable changes.
static thread_local uintptr_t checked_list{},checked_vtable{};
static ID3D12GraphicsCommandList* command_list(){static const uintptr_t exe=uintptr_t(GetModuleHandleW(nullptr));uintptr_t p{},vt{};
 if(!scene_frame::read_value(exe+0x5e23d0+0x2650,p)||!p||!scene_frame::read_value(p,vt)||!vt)return nullptr;if(p==checked_list&&vt==checked_vtable)return (ID3D12GraphicsCommandList*)p;
 uintptr_t method{};MEMORY_BASIC_INFORMATION region{};if(!scene_frame::read_value(vt,method)||!method||!VirtualQuery((void*)method,&region,sizeof(region))||region.State!=MEM_COMMIT||region.Type!=MEM_IMAGE||uintptr_t(region.AllocationBase)==exe)return nullptr;
 checked_list=p;checked_vtable=vt;return (ID3D12GraphicsCommandList*)p;}
// K23: platform::hook_slot returns at once for a slot it already hooked and never
// re-patches it, so the 13 calls are skipped while the list's vtable is the one hooked.
static uintptr_t hooked_vtable{};
static bool hooks(ID3D12GraphicsCommandList* l){uintptr_t vt=*(uintptr_t*)l;if(vt&&vt==hooked_vtable)return true;bool ok=true;for(auto item:{std::pair<size_t,void*>(30,(void*)signature),{25,(void*)pipeline},{28,(void*)heaps},{20,(void*)topology},{21,(void*)viewports},{22,(void*)scissors},{46,(void*)targets},{32,(void*)table},{38,(void*)cbv},{40,(void*)srv},{42,(void*)uav},{34,(void*)constant},{36,(void*)constants}})ok=platform::hook_slot(l,item.first,item.second)&&ok;hooked_vtable=ok?vt:0;return ok;}
// Frames since the replacement last recorded. The game's FXAA stays off until
// the bypass has lasted this long, so an occasional bypassed frame does not
// switch FXAA on and off from one frame to the next.
static constexpr uint32_t fxaa_hysteresis=30;static std::atomic<uint32_t> since_recorded{fxaa_hysteresis};
static bool begin(){tracking=false;states.clear();auto p=copy();if(!p.enabled)since_recorded=fxaa_hysteresis;if(!p.enabled&&!scene_lighting::copy().enabled&&!scene_effects::copy().enabled)return false;auto list=command_list();if(!list||!hooks(list)){gpu_ready=false;return false;}tracking=true;return p.enabled&&since_recorded<fxaa_hysteresis;}
static void finish(){tracking=false;}
// Previous camera for TAA reprojection, world coordinates.
struct History {bool valid{};scene_taa::Camera camera;uint64_t tick{};uint32_t width{},height{};};
static History history;static scene_jitter::State jitter;static uint32_t jitter_index{},aa_frame{};
// History weight by quality plus V3 history_bias: higher keeps more of the past (smoother, more trailing).
static constexpr float history_weights[]={.85f,.88f,.9f,.92f,.94f};
// AA-1: renderer and scene_renderer of the frame being rendered on this thread.
// renderer.render builds and records one stack scene_render on one thread
// (0.1.27 game code), so the scene hook's pair is the postprocess's pair.
// Cleared after the frame's postprocess (record()).
static thread_local void* frame_renderer{};static thread_local void* frame_scene{};static thread_local bool inner_recorded{};
static scene_jitter::OverlayGate overlay;
static const char* reason_name(uint32_t r){static const char* names[]={"none","special_vision","scope","depth","camera","interface","editor_overlay","history_reset","bindings"};return r<9?names[r]:"unknown";}
// Called from the scene hook before _build_scene. Only TAA with jitter moves the camera.
static void apply_jitter(void* renderer,void* scene){frame_renderer=renderer;frame_scene=scene;scene_jitter::current=&jitter;
 auto p=copy();uint32_t index=aa_frame++;bool applied=false;uint32_t reason=ANY_AA_FALLBACK_NONE;
 // Jitter without a working history would visibly shake the image.
 if(p.enabled&&p.method==4&&gpu_ready&&temporal_active){
  unsigned char scope=1;uintptr_t target{};int32_t size[2]{};
  if(overlay.blocks())reason=ANY_AA_FALLBACK_EDITOR_OVERLAY;
  else if(!scene_frame::read_value(uintptr_t(scene)+0xc24,scope)||scope)reason=ANY_AA_FALLBACK_SCOPE;
  else if(scene_frame::view_flags(uintptr_t(scene))&(scene_frame::VIEW_SPECIAL_VISION|scene_frame::VIEW_UNREADABLE))reason=ANY_AA_FALLBACK_SPECIAL_VISION;
  else if(!scene_frame::read_value(uintptr_t(renderer)+0x3c0,target)||!target||!scene_frame::read(target+0x34,size,8)||size[0]<=0||size[1]<=0)reason=ANY_AA_FALLBACK_CAMERA;
  else if(scene_jitter::apply((unsigned char*)scene,uint32_t(size[0]),uint32_t(size[1]),jitter_index++,jitter,scene_frame::VIEW_SPECIAL_VISION))applied=true;
  else reason=ANY_AA_FALLBACK_CAMERA;
  if(!applied){auto n=++jitter_skipped;if(reason==ANY_AA_FALLBACK_CAMERA&&(n==1||n%600==0))log_line(ANY_LOG_WARN,"framework","SCENE_TAA_JITTER skipped=1 reason=camera_contract");}
 }
 // The 0.40.1 code left jitter.active set after a failed size read; every frame without jitter now clears it.
 if(applied)++jittered;else jitter.active=false;
 jitter_active=applied;
 // K13: one record per frame for GI, lens, AO, water and contact.
 scene_frame::Temporal t{};t.frame=scene_frame::board.frame.load(std::memory_order_relaxed);t.taa_active=p.enabled&&p.method>=3&&temporal_active;t.jitter_active=applied;t.index=index;
 if(applied){double x,y;scene_jitter::offset(jitter_index-1,x,y);t.jitter_ndc[0]=float(jitter.applied[0]);t.jitter_ndc[1]=float(jitter.applied[1]);t.jitter_px[0]=float(x);t.jitter_px[1]=float(y);}
 scene_frame::board.temporal.store(t);
}
// K02: a D3D12 resource of the command list's device.
static bool interface_valid(ID3D12Resource* resource,ID3D12Device* device){ComPtr<ID3D12Device> owner;return resource&&device&&scene_frame::resource_valid(uintptr_t(resource))&&SUCCEEDED(resource->GetDevice(IID_PPV_ARGS(&owner)))&&owner.Get()==device;}
static bool surfaces_logged{};
// Fills the TAA inputs. Returns ANY_AA_FALLBACK_NONE, _HISTORY_RESET (TAA runs
// without history) or the reason TAA falls back to SMAA (extra.temporal false).
static uint32_t temporal_extra(void* renderer,void* scene,UINT w,UINT h,scene_smaa::Extra& extra,ID3D12Device* device){
 auto fail=[&](uint32_t reason){history.valid=false;extra.temporal=false;return reason;};
 if(!renderer||!scene)return fail(ANY_AA_FALLBACK_CAMERA);
 // AA-10: scope, binocular and IR overlays (pass 34) are not world content.
 unsigned char scope=1;if(!scene_frame::read_value(uintptr_t(scene)+0xc24,scope)||scope)return fail(ANY_AA_FALLBACK_SCOPE);
 scene_taa::Capture c;if(!scene_taa::capture(uintptr_t(renderer),uintptr_t(scene),c,scene_frame::reader,scene_frame::VIEW_SPECIAL_VISION))return fail(c.failure==1?ANY_AA_FALLBACK_SPECIAL_VISION:c.failure==2?ANY_AA_FALLBACK_DEPTH:ANY_AA_FALLBACK_CAMERA);
 if(c.depth.width!=w||c.depth.height!=h)return fail(ANY_AA_FALLBACK_DEPTH);
 auto depth=(ID3D12Resource*)c.depth.resource;if(!interface_valid(depth,device))return fail(ANY_AA_FALLBACK_INTERFACE);
 extra.temporal=true;extra.depth=depth;extra.depth_state=D3D12_RESOURCE_STATES(c.depth.state);
 // K05 (sea plane, camera above the sea only) and AA-6 (rim flag, still path v2 only).
 scene_taa::capture_surfaces(uintptr_t(renderer),uintptr_t(scene),c,scene_frame::reader);
 double sea=c.camera.position[1];
 if(c.ocean_ok&&sea>.05&&interface_valid((ID3D12Resource*)c.ocean.resource,device)){extra.ocean=(ID3D12Resource*)c.ocean.resource;extra.ocean_state=D3D12_RESOURCE_STATES(c.ocean.state);extra.sea_height=float(sea);}
 if((extra.flags&ANY_TAA_STILL_V2)&&c.albedo_ok&&interface_valid((ID3D12Resource*)c.albedo.resource,device)){extra.albedo=(ID3D12Resource*)c.albedo.resource;extra.albedo_state=D3D12_RESOURCE_STATES(c.albedo.state);}
 if(!surfaces_logged){surfaces_logged=true;log_line(ANY_LOG_INFO,"framework",("SCENE_TAA surfaces ocean="+std::to_string(c.ocean_ok)+" ocean_format="+std::to_string(c.ocean.srv_format)+" albedo="+std::to_string(c.albedo_ok)+" albedo_format="+std::to_string(c.albedo.srv_format)+" camera_y="+std::to_string(sea)).c_str());}
 uint64_t now=GetTickCount64();double moved=0;for(size_t i=0;i<3;++i)moved+=(c.camera.position[i]-history.camera.position[i])*(c.camera.position[i]-history.camera.position[i]);
 extra.history_valid=history.valid&&now-history.tick<250&&history.width==w&&history.height==h&&moved<50.0*50.0;
 auto& k=extra.constants;auto put=[&](size_t at,const std::array<float,4>& v){for(size_t i=0;i<4;++i)k[at+i]=v[i];};
 put(0,c.camera.projection);put(4,c.camera.right);put(8,c.camera.up);put(12,c.camera.forward);
 for(size_t i=0;i<3;++i)k[16+i]=float(history.camera.position[i]-c.camera.position[i]);put(20,history.camera.right);put(24,history.camera.up);put(28,history.camera.forward);
 // AA-7: this frame's jitter in uv (clip offset per w times .5, v down); 0 without jitter this frame.
 bool jittered_now=frame_scene==scene&&jitter.active;k[19]=jittered_now?float(jitter.applied[0]*.5):0;k[31]=jittered_now?float(-jitter.applied[1]*.5):0;
 history.camera=c.camera;history.tick=now;history.width=w;history.height=h;history.valid=true;
 return extra.history_valid?ANY_AA_FALLBACK_NONE:ANY_AA_FALLBACK_HISTORY_RESET;
}
// CPU interval between pre-HUD boundaries, smoothed, for the FPS readout.
static double frame_ms=-1;static LARGE_INTEGER last_frame{};
static void frame_tick(){LARGE_INTEGER now,frequency;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);if(last_frame.QuadPart){double ms=double(now.QuadPart-last_frame.QuadPart)*1000.0/double(frequency.QuadPart);if(ms>0&&ms<1000)frame_ms=frame_ms<0?ms:frame_ms*.9+ms*.1;}last_frame=now;}
// AA-1: editor overlay vectors of this scene_renderer (drawn after the postprocess).
static uint64_t overlay_logged{};
static bool observe_overlay(void* scene){unsigned char block[scene_jitter::overlay_block];int32_t counts[scene_jitter::overlay_renderers]{};
 bool present=scene&&scene_frame::read(uintptr_t(scene)+scene_jitter::overlay_first,block,sizeof(block))&&scene_jitter::overlay_counts(block,counts)&&std::any_of(std::begin(counts),std::end(counts),[](int32_t n){return n>0;});
 overlay.observe(present);
 if(present&&(overlay.seen==1||overlay.seen-overlay_logged>=600)){overlay_logged=overlay.seen;std::string line="SCENE_AA_OVERLAY frames="+std::to_string(overlay.seen)+" counts=";for(auto n:counts)line+=std::to_string(n)+",";line.pop_back();log_line(ANY_LOG_INFO,"framework",line.c_str());}
 return present;}
static constexpr int32_t bypass_bindings=801,bypass_presentation=802,bypass_device=803,bypass_gpu=804;
static std::atomic<uint64_t> recorded_inner{};
static void record_scene(void* renderer,void* scene,bool inner,bool overlay_now){auto& board=scene_frame::board;
 auto policy=copy_v3();auto& p=policy.base;
 auto bypass=[&](int32_t code){gpu_ready=false;board.pass_ms[ANY_PASS_AA].store(-1.f,std::memory_order_relaxed);board.pass_error[ANY_PASS_AA].store(code,std::memory_order_relaxed);gpu_ms=-1.f;};
 if(!p.enabled){history.valid=false;temporal_active=false;fallback_reason=0;editor_overlay=0;board.pass_ms[ANY_PASS_AA].store(-1.f,std::memory_order_relaxed);board.pass_error[ANY_PASS_AA].store(0,std::memory_order_relaxed);gpu_ms=-1.f;return;}
 if(since_recorded<fxaa_hysteresis)++since_recorded;
 editor_overlay=overlay_now?(inner?1u:2u):0u;
 auto list=command_list();auto found=states.find(list);if(!tracking||!list||found==states.end()||!found->second.ready()){bypass(bypass_bindings);fallback_reason=ANY_AA_FALLBACK_BINDINGS;auto n=++rejected;if(n==1||n%600==0)log_line(ANY_LOG_WARN,"framework",("SCENE_SMAA bypass=incomplete_native_bindings tracking="+std::to_string(tracking)+" list="+std::to_string(list!=nullptr)+" captured="+std::to_string(found!=states.end())).c_str());return;}
 std::lock_guard lock(platform::render_mutex);if(!platform::swap||!platform::queue){bypass(bypass_presentation);return;}ComPtr<ID3D12Device> device;ComPtr<ID3D12Resource> back;if(FAILED(list->GetDevice(IID_PPV_ARGS(&device)))||FAILED(platform::swap->GetBuffer(platform::swap->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&back)))){bypass(bypass_presentation);++rejected;return;}
 ComPtr<ID3D12Device> back_device;if(FAILED(back->GetDevice(IID_PPV_ARGS(&back_device)))||back_device.Get()!=device.Get()){bypass(bypass_device);++rejected;return;}
 auto desc=back->GetDesc();if(!gpu.ensure(device.Get(),platform::queue.Get(),desc)){bypass(bypass_gpu);error=gpu.error();auto n=++rejected;if(n==1||n%600==0)log_line(ANY_LOG_WARN,"framework",("SCENE_SMAA bypass=GPU_unavailable error="+std::to_string(error.load())+" format="+std::to_string(desc.Format)).c_str());return;}
 // Use the current buffer from the game presentation swapchain. Inner: right after
 // pipeline_postprocess.cmd_render, before the editor overlays. Slot 37: after the
 // whole postprocess, before frontend_ui.render/mm_ui.render.
 scene_smaa::Extra extra;extra.sharpen=p.sharpening;extra.history_weight=history_weights[std::min(p.quality+policy.extras.history_bias,4u)];extra.flags=policy.extras.taa_flags;extra.dither=float(policy.extras.dither);
 uint32_t reason=ANY_AA_FALLBACK_NONE;
 if(p.method>=3){if(!inner&&overlay_now){history.valid=false;reason=ANY_AA_FALLBACK_EDITOR_OVERLAY;}else reason=temporal_extra(renderer,scene,UINT(desc.Width),desc.Height,extra,device.Get());}
 // ANY_AA_OCEAN_DEPTH (testing switch): no sea plane, so ocean pixels reproject with the depth buffer as in 0.40.1.
 if(policy.extras.flags&ANY_AA_OCEAN_DEPTH)extra.ocean=nullptr;
 const auto& saved=found->second;owned=true;bool depth_ok=gpu.record(list,back.Get(),p.quality,p.method==2,&extra);if(!depth_ok){history.valid=false;if(extra.temporal)reason=ANY_AA_FALLBACK_DEPTH;}temporal_active=extra.temporal&&depth_ok;saved.restore(list);owned=false;
 if(p.method>=3&&!temporal_active&&reason!=ANY_AA_FALLBACK_HISTORY_RESET){
  // Inactive by design: quiet. Contract failures count as rejected and warn every 600.
  bool quiet=reason==ANY_AA_FALLBACK_SPECIAL_VISION||reason==ANY_AA_FALLBACK_SCOPE||reason==ANY_AA_FALLBACK_EDITOR_OVERLAY;
  if(quiet){if(fallback_reason!=reason)log_line(ANY_LOG_INFO,"framework",("SCENE_TAA inactive="+std::string(reason_name(reason))+" fallback=SMAA").c_str());}
  else{auto n=++rejected;if(n==1||n%600==0)log_line(ANY_LOG_WARN,"framework",("SCENE_TAA bypass="+std::string(reason_name(reason))+" fallback=SMAA").c_str());}}
 fallback_reason=reason;gpu_ready=true;since_recorded=0;width=UINT(desc.Width);height=desc.Height;error=0;auto count=++frames;if(inner)++recorded_inner;
 float ms=float(gpu.gpu_milliseconds());gpu_ms=ms;board.pass_ms[ANY_PASS_AA].store(ms,std::memory_order_relaxed);board.pass_error[ANY_PASS_AA].store(0,std::memory_order_relaxed);
 if(count==1||count%600==0)log_line(ANY_LOG_INFO,"framework",("SCENE_SMAA frames="+std::to_string(count)+" method="+std::to_string(p.method)+" quality="+std::to_string(p.quality)+" sharpening="+std::to_string(p.sharpening)+" temporal="+std::to_string(extra.temporal)+" jittered="+std::to_string(jittered.load())+" fallback="+reason_name(reason)+" gpu_ms="+std::to_string(gpu.gpu_milliseconds())+" width="+std::to_string(width.load())+" height="+std::to_string(height.load())+" lists="+std::to_string(states.peak)+" inner_frames="+std::to_string(recorded_inner.load())+(inner?" location=BEFORE_OVERLAY":" location=BEFORE_HUD")+" native_FXAA_replaced=1 bindings_restored=1").c_str());
}
// AA-1: inner call-cell hook on pipeline_postprocess.renderer.cmd_render inside
// renderer._record_commands_postprocess (code end 5944, call cell 15; self is
// scene_render.m_renderer_postprocess = scene+0x2450). It returns before the
// editor, placement and inspection overlays are drawn into m_target_overlay and
// composited into the swapchain (0.1.27 game code).
static anyapi_runtime::CallCellHook postprocess_cell;
static std::atomic<bool> inner_available{};
// All four integer argument registers (rcx, rdx, r8, r9) and rax are forwarded: the prologue stores only rcx and
// rdx, so two arguments is likely, and forwarding r8 and r9 as well is harmless either way.
using PostprocessRender=uintptr_t(*)(void*,void*,void*,void*);
static const unsigned char POSTPROCESS_BODY[32]={0x53,0x55,0x56,0x48,0x81,0xec,0x80,0x04,0,0,0x48,0x89,0x8c,0x24,0x28,0x04,0,0,0x48,0x89,0x94,0x24,0x30,0x04,0,0,0x48,0x8b,0x05,0x17,0x17,0}; // = scene_controls::POST_COMPOSITE
static const unsigned char POSTPROCESS_RENDER[32]={0x53,0x55,0x48,0x81,0xec,0xb8,0,0,0,0x48,0x89,0x4c,0x24,0x58,0x48,0x89,0x54,0x24,0x60,0x48,0x8d,0x44,0x24,0x68,0x48,0x8d,0x19,0x48,0x8b,0x2d,0xb6,0x01};
static constexpr size_t RECORD_COMMANDS_END=2600,RECORD_POSTPROCESS_SLOT=37,POSTPROCESS_END=5944,POSTPROCESS_RENDER_SLOT=15;
static constexpr uintptr_t RENDERER_POSTPROCESS=0x2450;
static std::atomic<uint32_t> inner_mismatch{};
static uintptr_t postprocess_hook(void* self,void* resources,void* third,void* fourth){
 auto result=((PostprocessRender)postprocess_cell.original())(self,resources,third,fourth);
 void* scene=frame_scene;
 // ANY_AA_AFTER_OVERLAYS (testing switch): the hook stays installed but leaves the frame to the slot-37 record.
 if(scene&&!inner_recorded&&uintptr_t(self)==uintptr_t(scene)+RENDERER_POSTPROCESS){if(copy_v3().extras.flags&ANY_AA_AFTER_OVERLAYS)return result;scene_frame::CpuScope cpu;inner_recorded=true;bool present=observe_overlay(scene);record_scene(frame_renderer,scene,true,present);}
 else if(scene&&uintptr_t(self)!=uintptr_t(scene)+RENDERER_POSTPROCESS){auto n=++inner_mismatch;if(n==1||n%600==0)log_line(ANY_LOG_WARN,"framework","SCENE_AA_STAGE inner_scene_mismatch=1 fallback=slot37");}
 return result;
}
// Seam (scene_frame::seams.install_postprocess), called by the controls worker
// with the _record_render_commands candidates after its slot-37 hook. That cell
// then holds the controls hook, so the postprocess body comes from the cell
// when it is still native, otherwise from a unique scan of its prologue.
static void install_postprocess(const std::vector<uintptr_t>& record_command_callers){if(inner_available)return;
 uintptr_t body{};
 for(auto caller:record_command_callers){uintptr_t cell{},target{};unsigned char prefix[32]{};
  if(scene_frame::read_value(caller+RECORD_COMMANDS_END+RECORD_POSTPROCESS_SLOT*8,cell)&&cell&&scene_frame::read_value(cell,target)&&target&&scene_frame::read(target,prefix,32)&&!memcmp(prefix,POSTPROCESS_BODY,32)){body=target;break;}}
 if(!body){auto hits=scan_exact(POSTPROCESS_BODY,sizeof(POSTPROCESS_BODY));if(hits.size()==1)body=hits[0];}
 void** cell=body?scene_frame::resolve_cell(body,POSTPROCESS_END,POSTPROCESS_RENDER_SLOT,POSTPROCESS_RENDER,sizeof(POSTPROCESS_RENDER)):nullptr;
 bool ok=cell&&postprocess_cell.install(cell,(void*)postprocess_hook);inner_available=ok;
 if(ok)log_line(ANY_LOG_INFO,"framework","SCENE_AA_STAGE inner=1 location=AFTER_POSTPROCESS_BEFORE_EDITOR_OVERLAY");
 else{std::string line="SCENE_AA_STAGE inner=0 body="+std::to_string(body!=0)+" location=AFTER_SCENE_COMPOSITION_BEFORE_HUD";
  // Prologue actually found in call cell 15, for updating POSTPROCESS_RENDER.
  uintptr_t slot{},target{};unsigned char prefix[32]{};if(body&&scene_frame::read_value(body+POSTPROCESS_END+POSTPROCESS_RENDER_SLOT*8,slot)&&slot&&scene_frame::read_value(slot,target)&&target&&scene_frame::read(target,prefix,32)){char hex[4];line+=" cell15=";for(auto b:prefix){snprintf(hex,sizeof(hex),"%02x",b);line+=hex;}}
  log_line(ANY_LOG_WARN,"framework",line.c_str());}
}
static const bool postprocess_seam=(scene_frame::seams.install_postprocess=&install_postprocess,true);
// Slot 37 (scene_controls::pre_hud_hook), after the original postprocess.
static void record(void* renderer=nullptr,void* scene=nullptr){frame_tick();
 if(!inner_recorded){bool present=observe_overlay(scene);record_scene(renderer,scene,false,present);}
 inner_recorded=false;frame_renderer=frame_scene=nullptr;
}
}
