#include "aa/scene_smaa_gpu.h"
#include "aa/scene_smaa_bindings.h"
#include "aa/scene_taa_frame.h"
#include "aa/scene_jitter.h"
namespace scene_antialiasing {
using Microsoft::WRL::ComPtr;
static scene_smaa::Gpu gpu;
static thread_local bool tracking{},owned{};
// The native command-list implementation normally shares one vtable. Cache
// each chained method per thread; other vtables still resolve independently.
template<class T> static T chained(ID3D12GraphicsCommandList* list,size_t index){struct Entry{void** slot{};void* call{};};static thread_local Entry cache[64]{};auto slot=(*(void***)list)+index;auto& entry=cache[index];if(entry.slot!=slot){entry.call=(void*)platform::original<T>(list,index);entry.slot=slot;}return (T)entry.call;}
static thread_local std::unordered_map<ID3D12GraphicsCommandList*,scene_smaa::Bindings> states;
static scene_smaa::Bindings* capture(ID3D12GraphicsCommandList* l){if(!tracking||owned)return nullptr;if(states.size()>=16&&!states.count(l))return nullptr;return &states[l];}
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
static ID3D12GraphicsCommandList* command_list(){uintptr_t p{};if(!safe_read_memory(uintptr_t(GetModuleHandleW(nullptr))+0x5e5340+0x2648,&p,8)||!p)return nullptr;uintptr_t vt{};if(!safe_read_memory(p,&vt,8)||!vt)return nullptr;return (ID3D12GraphicsCommandList*)p;}
static bool hooks(ID3D12GraphicsCommandList* l){bool ok=true;for(auto item:{std::pair<size_t,void*>(30,(void*)signature),{25,(void*)pipeline},{28,(void*)heaps},{20,(void*)topology},{21,(void*)viewports},{22,(void*)scissors},{46,(void*)targets},{32,(void*)table},{38,(void*)cbv},{40,(void*)srv},{42,(void*)uav},{34,(void*)constant},{36,(void*)constants}})ok=platform::hook_slot(l,item.first,item.second)&&ok;return ok;}
static bool begin(){tracking=false;states.clear();auto p=copy();if(!p.enabled&&!scene_lighting::copy().enabled)return false;auto list=command_list();if(!list||!hooks(list)){gpu_ready=false;return false;}tracking=true;return p.enabled&&gpu_ready;}
static void finish(){tracking=false;}
// Previous camera for TAA reprojection, world coordinates.
struct History {bool valid{};scene_taa::Camera camera;uint64_t tick{};uint32_t width{},height{};};
static History history;static scene_jitter::State jitter;static uint32_t jitter_index{};static std::atomic<uint64_t> jittered{},jitter_skipped{};static std::atomic<bool> temporal_active{};
// History weight by quality: higher keeps more of the past (smoother, more trailing).
static constexpr float history_weights[]={.85f,.88f,.9f,.92f};
// Called from the scene hook before _build_scene. Only TAA with jitter moves the camera.
static void apply_jitter(void* renderer,void* scene){auto p=copy();// Jitter without a working history would visibly shake the image.
 if(!p.enabled||p.method!=4||!gpu_ready||!temporal_active){jitter.active=false;return;}
 uintptr_t target{};int32_t size[2]{};if(!safe_read_memory(uintptr_t(renderer)+0x3c0,&target,8)||!target||!safe_read_memory(target+0x34,size,8)||size[0]<=0||size[1]<=0){++jitter_skipped;return;}
 if(scene_jitter::apply((unsigned char*)scene,uint32_t(size[0]),uint32_t(size[1]),jitter_index++,jitter))++jittered;else{jitter.active=false;auto n=++jitter_skipped;if(n==1||n%600==0)log_line(ANY_LOG_WARN,"framework","SCENE_TAA_JITTER skipped=1 reason=camera_contract");}}
static bool interface_valid_depth(ID3D12Resource* resource){uintptr_t vtable{},method{};MEMORY_BASIC_INFORMATION region{};if(!resource||!safe_read_memory(uintptr_t(resource),&vtable,8)||!vtable||!safe_read_memory(vtable+10*8,&method,8)||!method||!VirtualQuery((void*)method,&region,sizeof(region))||region.State!=MEM_COMMIT||region.Type!=MEM_IMAGE||(region.AllocationBase!=GetModuleHandleW(L"D3D12Core.dll")&&region.AllocationBase!=GetModuleHandleW(L"d3d12.dll")))return false;
 ComPtr<ID3D12Device> owner,device;auto list=command_list();return list&&SUCCEEDED(resource->GetDevice(IID_PPV_ARGS(&owner)))&&SUCCEEDED(list->GetDevice(IID_PPV_ARGS(&device)))&&owner.Get()==device.Get();}
static bool temporal_extra(void* renderer,void* scene,UINT w,UINT h,scene_smaa::Extra& extra){
 scene_taa::Capture c;if(!renderer||!scene||!scene_taa::capture(uintptr_t(renderer),uintptr_t(scene),c,safe_read_memory)||c.depth.width!=w||c.depth.height!=h){history.valid=false;return false;}
 extra.temporal=true;extra.depth=(ID3D12Resource*)c.depth.resource;extra.depth_state=D3D12_RESOURCE_STATES(c.depth.state);
 uint64_t now=GetTickCount64();double moved=0;for(size_t i=0;i<3;++i)moved+=(c.camera.position[i]-history.camera.position[i])*(c.camera.position[i]-history.camera.position[i]);
 extra.history_valid=history.valid&&now-history.tick<250&&history.width==w&&history.height==h&&moved<50.0*50.0;
 auto& k=extra.constants;auto put=[&](size_t at,const std::array<float,4>& v){for(size_t i=0;i<4;++i)k[at+i]=v[i];};
 put(0,c.camera.projection);put(4,c.camera.right);put(8,c.camera.up);put(12,c.camera.forward);
 for(size_t i=0;i<3;++i)k[16+i]=float(history.camera.position[i]-c.camera.position[i]);put(20,history.camera.right);put(24,history.camera.up);put(28,history.camera.forward);k[31]=0;
 history.camera=c.camera;history.tick=now;history.width=w;history.height=h;history.valid=true;return true;
}
// CPU interval between pre-HUD boundaries, smoothed, for the FPS readout.
static double frame_ms=-1;static LARGE_INTEGER last_frame{};
static void frame_tick(){LARGE_INTEGER now,frequency;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);if(last_frame.QuadPart){double ms=double(now.QuadPart-last_frame.QuadPart)*1000.0/double(frequency.QuadPart);if(ms>0&&ms<1000)frame_ms=frame_ms<0?ms:frame_ms*.9+ms*.1;}last_frame=now;}
static void record(void* renderer=nullptr,void* scene=nullptr){frame_tick();auto p=copy();if(!p.enabled){history.valid=false;temporal_active=false;return;}auto list=command_list();auto found=states.find(list);if(!tracking||!list||found==states.end()||!found->second.ready()){gpu_ready=false;auto n=++rejected;if(n==1||n%600==0)log_line(ANY_LOG_WARN,"framework",("SCENE_SMAA bypass=incomplete_native_bindings tracking="+std::to_string(tracking)+" list="+std::to_string(list!=nullptr)+" captured="+std::to_string(found!=states.end())).c_str());return;}
 std::lock_guard lock(platform::render_mutex);if(!platform::swap||!platform::queue){gpu_ready=false;return;}ComPtr<ID3D12Device> device;ComPtr<ID3D12Resource> back;if(FAILED(list->GetDevice(IID_PPV_ARGS(&device)))||FAILED(platform::swap->GetBuffer(platform::swap->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&back)))){gpu_ready=false;++rejected;return;}
 ComPtr<ID3D12Device> back_device;if(FAILED(back->GetDevice(IID_PPV_ARGS(&back_device)))||back_device.Get()!=device.Get()){gpu_ready=false;++rejected;return;}
 auto desc=back->GetDesc();if(!gpu.ensure(device.Get(),platform::queue.Get(),desc)){gpu_ready=false;error=gpu.error();auto n=++rejected;if(n==1||n%600==0)log_line(ANY_LOG_WARN,"framework",("SCENE_SMAA bypass=GPU_unavailable error="+std::to_string(error.load())+" format="+std::to_string(desc.Format)).c_str());return;}
 // Use the current buffer from the game presentation swapchain.
 // At this reviewed boundary the game has completed scene composition and has
 // not yet emitted frontend_ui.render/mm_ui.render.
 scene_smaa::Extra extra;extra.sharpen=p.sharpening;extra.history_weight=history_weights[std::min(p.quality,3u)];bool temporal=p.method>=3&&temporal_extra(renderer,scene,UINT(desc.Width),desc.Height,extra);if(p.method>=3&&!temporal){auto n=++rejected;if(n==1||n%600==0)log_line(ANY_LOG_WARN,"framework","SCENE_TAA bypass=depth_or_camera_contract fallback=SMAA");}
 if(temporal&&!interface_valid_depth(extra.depth)){extra.temporal=false;history.valid=false;}
 auto saved=found->second;owned=true;bool depth_ok=gpu.record(list,back.Get(),p.quality,p.method==2,&extra);if(!depth_ok)history.valid=false;temporal_active=extra.temporal&&depth_ok;saved.restore(list);owned=false;gpu_ready=true;width=UINT(desc.Width);height=desc.Height;error=0;auto count=++frames;
 if(count==1||count%600==0)log_line(ANY_LOG_INFO,"framework",("SCENE_SMAA frames="+std::to_string(count)+" method="+std::to_string(p.method)+" quality="+std::to_string(p.quality)+" sharpening="+std::to_string(p.sharpening)+" temporal="+std::to_string(extra.temporal)+" jittered="+std::to_string(jittered.load())+" gpu_ms="+std::to_string(gpu.gpu_milliseconds())+" width="+std::to_string(width.load())+" height="+std::to_string(height.load())+" location=BEFORE_HUD native_FXAA_replaced=1 bindings_restored=1").c_str());
}
}
