#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include "../anyapi_scene_frame.h"
#include "../scene_celestial.h"
#include "anyapi_scene_lighting_v4.h"
#include "anyapi_scene_effects_v3.h"
#include "anyapi_scene_controls_v4.h"
#include "anyapi_scene_antialiasing_v3.h"
#include "anyapi_scene_timing_v2.h"
#include "anyapi_gpu_info_v2.h"
#include "gpu_test_device.h"

#define CHECK(x) do{if(!(x)){std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);std::fflush(stdout);std::exit(1);}}while(0)
static int logs{};static char last_log[256]{};
static int warnings{};static void sink(bool warning,const char* line){++logs;warnings+=warning;strncpy_s(last_log,line,_TRUNCATE);}

// Static-init registration, exactly as owners do it in their state headers.
static const int table_a=1,table_b=2;
ANYAPI_REGISTER_SERVICE(test_a,"anyapi.test_service",4,&table_a);
ANYAPI_REGISTER_SERVICE(test_b,"anyapi.test_service",5,&table_b);

static void reads(){
 using namespace scene_frame;
 alignas(8) unsigned char data[64];for(int i=0;i<64;++i)data[i]=(unsigned char)i;unsigned char out[64]{};
 CHECK(read((uintptr_t)data,out,64)&&!memcmp(out,data,64));
 uint32_t v{};CHECK(read_value((uintptr_t)data+4,v)&&v==0x07060504u);
 CHECK(reader((uintptr_t)data,out,8));
 CHECK(!read(0,out,8)&&!read(0x1000,out,8)&&!read((uintptr_t)data,nullptr,8)&&!read((uintptr_t)data,out,0));
 CHECK(!read(0x7ffffffff000ull,out,8)&&!read(0xffff800000000000ull,out,8));
 auto base=(unsigned char*)VirtualAlloc(nullptr,3*4096,MEM_RESERVE,PAGE_NOACCESS);CHECK(base);
 CHECK(VirtualAlloc(base,4096,MEM_COMMIT,PAGE_READWRITE));CHECK(VirtualAlloc(base+4096,4096,MEM_COMMIT,PAGE_READWRITE|PAGE_GUARD));
 memset(base,0x5a,4096);
 auto faults=read_faults.load();
 CHECK(!read((uintptr_t)base+8192,out,8));                         // reserved, not committed
 CHECK(read_faults.load()==faults+1);
 CHECK(!read((uintptr_t)base+4096-4,out,8));                       // straddles into the guard page
 CHECK(guard_hits.load()==1&&logs==1&&warnings==1&&strstr(last_log,"guard_page_read=1"));
 MEMORY_BASIC_INFORMATION m{};CHECK(VirtualQuery(base+4096,&m,sizeof(m))&&(m.Protect&PAGE_GUARD));
 CHECK(!read((uintptr_t)base+4096,out,8));                         // still guarded: re-armed, and fails again
 CHECK(guard_hits.load()==2&&logs==1);
 CHECK(read((uintptr_t)base+4096-8,out,8)&&out[0]==0x5a);           // the committed page still reads
 DWORD old{};CHECK(VirtualProtect(base,4096,PAGE_NOACCESS,&old));CHECK(!read((uintptr_t)base,out,1));
 VirtualFree(base,0,MEM_RELEASE);
 // A fault in a nested read must not disturb an outer SEH frame or C++ state.
 std::vector<int> keep(100,7);for(int i=0;i<1000;++i)read(0x10000,out,8);CHECK(keep[99]==7);
 std::cout<<"PASS reads faults="<<read_faults.load()<<" guard="<<guard_hits.load()<<"\n";
}

static void views(){
 using namespace scene_frame;
 alignas(8) unsigned char scene[0x670]{};
 CHECK(view_flags((uintptr_t)scene)==0);
 scene[0x668]=1;CHECK(view_flags((uintptr_t)scene)==VIEW_UNDERGROUND);
 scene[0x669]=1;scene[0x66b]=1;CHECK(view_flags((uintptr_t)scene)==(VIEW_UNDERGROUND|VIEW_UNDERWATER|VIEW_THERMAL));
 CHECK((view_flags((uintptr_t)scene)&VIEW_SPECIAL_VISION)==VIEW_THERMAL);
 scene[0x66a]=2;CHECK(view_flags((uintptr_t)scene)==VIEW_UNREADABLE);
 CHECK(view_flags(0)==VIEW_UNREADABLE&&view_flags(0x10000)==VIEW_UNREADABLE);
 std::cout<<"PASS view_flags\n";
}

static void resources(){
 using namespace scene_frame;
 auto kernel=GetModuleHandleW(L"kernel32.dll");CHECK(kernel);
 uintptr_t code=(uintptr_t)GetProcAddress(kernel,"GetTickCount");CHECK(code);
 static uintptr_t good[16]{},exe[16]{},heap[16]{},header[16]{};
 good[10]=code;exe[10]=(uintptr_t)&resources;heap[10]=(uintptr_t)new int(1);header[10]=(uintptr_t)kernel;
 uintptr_t object_good=(uintptr_t)good,object_exe=(uintptr_t)exe,object_heap=(uintptr_t)heap,object_header=(uintptr_t)header;
 // native-1: code in a non-exe image that is not trusted (not D3D12Core/d3d12 and not the command list's module) fails.
 auto base_of=[](uintptr_t a){MEMORY_BASIC_INFORMATION m{};return VirtualQuery((void*)a,&m,sizeof(m))?uintptr_t(m.AllocationBase):uintptr_t(0);};
 uintptr_t other{};for(auto [module,proc]:{std::pair<const wchar_t*,const char*>{L"ntdll.dll","RtlZeroMemory"},{L"ntdll.dll","NtClose"},{L"user32.dll","GetDesktopWindow"},{L"advapi32.dll","RegCloseKey"}}){
  HMODULE h=GetModuleHandleW(module);if(!h)h=LoadLibraryW(module);uintptr_t f=h?(uintptr_t)GetProcAddress(h,proc):0;if(f&&base_of(f)!=base_of(code)){other=f;break;}}
 CHECK(other);static uintptr_t untrusted[16]{};untrusted[10]=other;uintptr_t object_untrusted=(uintptr_t)untrusted;
 CHECK(!resource_valid((uintptr_t)&object_untrusted));             // another DLL's object (a driver, DXGI or a plugin)
 CHECK(!trust_module((uintptr_t)&resources)&&!trust_module((uintptr_t)heap[10]));  // the exe and the heap are never trusted
 CHECK(trust_module(code)&&trust_module(code));                    // the command list's module (here kernel32's code)
 int before=logs;
 CHECK(resource_valid((uintptr_t)&object_good));                   // code in a trusted non-exe image (a D3D runtime or a wrapper layer)
 CHECK(logs==before+1&&warnings==1&&strstr(last_log,"resource_module="));
 CHECK(!resource_valid((uintptr_t)&object_exe));                   // the game exe's own code
 CHECK(!resource_valid((uintptr_t)&object_heap));                  // not an image
 CHECK(!resource_valid((uintptr_t)&object_header));                // image, but not executable
 CHECK(!resource_valid(0)&&!resource_valid(0x10000));
 uintptr_t zero=0;CHECK(!resource_valid((uintptr_t)&zero));
 good[10]=(uintptr_t)heap;CHECK(resource_valid((uintptr_t)&object_good));  // cached vtable: one read and compares
 reset_resource_cache();CHECK(!resource_valid((uintptr_t)&object_good));    // re-validated after a device change
 good[10]=code;CHECK(resource_valid((uintptr_t)&object_good));CHECK(logs==before+1); // module logged once; trust survives the reset
 CHECK(!resource_valid((uintptr_t)&object_untrusted));
 std::cout<<"PASS resource_valid\n";
}

static void backoff(){
 scene_frame::Backoff b;CHECK(b.ready(0));uint64_t now=0;const uint64_t expected[]={1000,2000,4000,8000,16000,32000,60000,60000,60000};
 for(auto e:expected){b.failed(now);CHECK(!b.ready(now+e-1)&&b.ready(now+e));now+=e;}
 b.succeeded();CHECK(b.ready(now)&&b.failures==0);b.failed(now);CHECK(b.next_ms==now+1000);b.reset();CHECK(b.ready(0));
 std::cout<<"PASS backoff\n";
}

static const unsigned char TARGET[]={0x53,0x48,0x81,0xec,0x30,0x2c,0,0,0x48,0x89,0x4c,0x24,0x20,0x8a,0x81,0x90,0,0,0,0x3c,0,0x0f,0x84,0x30,0,0,0,0x8a,0x05,0x17,0x01,0};
static void cells(){
 using namespace scene_frame;
 alignas(8) unsigned char caller[80]{};void* target=(void*)&cells;memcpy(caller+40,&target,sizeof(target));
 CHECK(resolve_cell((uintptr_t)caller,32,1,TARGET,sizeof(TARGET))==nullptr);
 alignas(8) unsigned char body[sizeof(TARGET)];memcpy(body,TARGET,sizeof(body));void* reviewed=body;void* distinct=body;
 alignas(8) unsigned char match[80]{},duplicate[80]{},ambiguous[80]{};
 void** reviewed_cell=&reviewed;void** distinct_cell=&distinct;memcpy(match+40,&reviewed_cell,8);memcpy(duplicate+40,&reviewed_cell,8);memcpy(ambiguous+40,&distinct_cell,8);
 std::vector<uintptr_t> wrappers(424,(uintptr_t)caller);wrappers.push_back((uintptr_t)match);
 CHECK(resolve_unique_cell(wrappers,32,1,TARGET,sizeof(TARGET))==reviewed_cell);
 wrappers.push_back((uintptr_t)duplicate);CHECK(resolve_unique_cell(wrappers,32,1,TARGET,sizeof(TARGET))==reviewed_cell);
 wrappers.push_back((uintptr_t)ambiguous);CHECK(!resolve_unique_cell(wrappers,32,1,TARGET,sizeof(TARGET)));
 CHECK(!resolve_cell((uintptr_t)match,32,1,TARGET,33));             // longer than the 32-byte prefix buffer
 body[sizeof(body)-1]^=1;CHECK(!resolve_unique_cell({(uintptr_t)match},32,1,TARGET,sizeof(TARGET)));
 std::vector<uintptr_t> wild{0x10000,0x7ffe0000,(uintptr_t)-64};CHECK(!resolve_unique_cell(wild,32,1,TARGET,sizeof(TARGET)));
 std::cout<<"PASS call cells\n";
}

static void registry(){
 using namespace scene_frame;
 static const int c=3;
 CHECK(!register_service("anyapi.test_service",4,&c));             // duplicate id+version
 CHECK(!register_service("mod.other",1,&c)&&!register_service("anyapi.x",0,&c)&&!register_service("anyapi.x",1,nullptr)&&!register_service(nullptr,1,&c));
 uint32_t n=service_entries();CHECK(n==2);
 static const char* names[32]={"anyapi.f0","anyapi.f1","anyapi.f2","anyapi.f3","anyapi.f4","anyapi.f5","anyapi.f6","anyapi.f7","anyapi.f8","anyapi.f9","anyapi.f10","anyapi.f11","anyapi.f12","anyapi.f13","anyapi.f14","anyapi.f15",
  "anyapi.f16","anyapi.f17","anyapi.f18","anyapi.f19","anyapi.f20","anyapi.f21","anyapi.f22","anyapi.f23","anyapi.f24","anyapi.f25","anyapi.f26","anyapi.f27","anyapi.f28","anyapi.f29","anyapi.f30","anyapi.f31"};
 for(uint32_t i=0;n<32;++i,++n)CHECK(register_service(names[i],1,&c));
 CHECK(!register_service("anyapi.full",1,&c));                     // 33rd entry
 CHECK(find_service("anyapi.test_service",4)==&table_a&&find_service("anyapi.test_service",5)==&table_b);
 CHECK(!find_service("anyapi.test_service",3)&&!find_service("anyapi.missing",1)&&!find_service(nullptr,1));
 CHECK(!register_service("anyapi.late",1,&c));                     // sealed by the first find
 CHECK(service_rejected()==7);
 CHECK(service_entry(0)&&!strcmp(service_entry(0)->id,"anyapi.test_service")&&service_entry(0)->version==4&&!service_entry(32));
 std::cout<<"PASS registry entries="<<service_entries()<<"\n";
}

struct Wide {uint64_t frame;uint64_t words[40];};
static void slots(){
 using namespace scene_frame;
 Slot<Wide> slot;Wide w{};CHECK(!slot.load(w));
 std::atomic<bool> stop{false};std::atomic<uint64_t> good{0},torn{0};
 auto readers=[&]{Wide r{};while(!stop.load()){if(!slot.load(r))continue;bool same=true;for(auto x:r.words)same=same&&x==r.frame;(same?good:torn).fetch_add(1);}};
 std::thread a(readers),b(readers);
 uint64_t i=0;auto start=GetTickCount64();while(GetTickCount64()-start<300||(good.load()<1000&&GetTickCount64()-start<5000)){++i;Wide x;x.frame=i;for(auto& word:x.words)word=i;slot.store(x);if(!(i&63))SwitchToThread();}
 stop=true;a.join();b.join();
 CHECK(torn.load()==0&&good.load()>0);CHECK(slot.load(w)&&w.frame==i);
 slot.seq.store(0xfffffffeu);slot.store(w);CHECK(slot.seq.load()==2&&slot.load(w));   // wrap skips the never-stored 0
 std::cout<<"PASS slot loads="<<good.load()<<" torn=0\n";
}

static void board(){
 using namespace scene_frame;
 for(auto& p:scene_frame::board.pass_ms)CHECK(p.load()==-1.f);
 for(auto& e:scene_frame::board.pass_error)CHECK(e.load()==0);
 CHECK(scene_frame::board.finish_since_ok.load()==UINT32_MAX&&scene_frame::board.frame.load()==0);
 Sample s{};CHECK(!fresh(s));scene_frame::board.frame=5;s.frame=5;CHECK(fresh(s));s.frame=4;CHECK(!fresh(s));
 scene_frame::board.sample.store(s);Sample r{};CHECK(scene_frame::board.sample.load(r)&&r.frame==4);
 auto ticks=scene_frame::board.cpu_ticks.load();{CpuScope scope;Sleep(2);}CHECK(scene_frame::board.cpu_ticks.load()>ticks);
 CHECK(!seams.install_postprocess&&!seams.replace_ssao);
 std::cout<<"PASS board\n";
}

static bool close_to(double a,double b,double e=1e-6){return std::abs(a-b)<=e;}
static void sky(){
 using namespace celestial;
 double s[3];path(.5,1,1,s);CHECK(close_to(s[1],.809017));path(.2,1,1,s);CHECK(close_to(s[1],-.25,1e-9));
 CHECK(parked(.1)&&parked(.9)&&!parked(.2)&&!parked(.5)&&!parked(.8));
 // Calibrate from a day frame: the stored vector points away from the sun.
 Calibration cal;double toward[3],native[3];path(.35,-1,1,toward);for(int i=0;i<3;++i)native[i]=-toward[i]*2;
 auto r=evaluate(.35,native,0,cal);CHECK(cal.done&&cal.sx==-1&&cal.sz==1&&r.valid&&!r.parked);
 // Noon is ambiguous in x: no calibration there.
 Calibration noon;path(.5,1,1,toward);for(int i=0;i<3;++i)native[i]=-toward[i];r=evaluate(.5,native,0,noon);CHECK(!noon.done&&!r.valid);
 r=evaluate(.5,native,0,cal);CHECK(r.valid&&close_to(r.sun_y,.809017)&&r.night==0);
 // Parked window: the game's vector sits at noon, the clock does not.
 r=evaluate(.9,native,0,cal);CHECK(r.valid&&r.parked&&r.night==1&&r.moon[1]>0&&close_to(r.sun_y,-.654508,1e-5));
 r=evaluate(.75,native,0,cal);CHECK(std::abs(r.moon[1])<.01);
 r=evaluate(0,native,0,cal);CHECK(close_to(r.moon[1],.809017)&&close_to(r.star_angle,0));
 r=evaluate(.25,native,0,cal);CHECK(close_to(r.star_angle,-PI/2));
 // A calibrated day frame that disagrees with the game's sun is not trusted.
 path(.3,-1,1,toward);for(int i=0;i<3;++i)native[i]=-toward[i];r=evaluate(.36,native,0,cal);CHECK(!r.valid);
 double nan_t=std::nan("");CHECK(!evaluate(-.1,native,0,cal).valid&&!evaluate(1.5,native,0,cal).valid&&!evaluate(nan_t,native,0,cal).valid);
 Calibration none;r=evaluate(.9,native,0,none);CHECK(!r.valid&&r.parked);                  // never calibrated
 // Phase: 8 game days per lunation, stepping at noon while the moon is down.
 r=evaluate(.5,native,-1,cal);CHECK(r.day_index==0&&r.phase==0&&r.lit_fraction==1);
 r=evaluate(.5,native,0,cal);CHECK(r.day_index==0&&r.phase==0);
 CHECK(evaluate(0,native,143999,cal).day_index==0&&evaluate(0,native,144000,cal).day_index==1);
 CHECK(evaluate(0,native,143999,cal).phase==evaluate(0,native,144000,cal).phase);       // no step at midnight
 CHECK(evaluate(.5,native,35999,cal).phase==0&&evaluate(.5,native,36000,cal).phase==.125f); // step at noon
 r=evaluate(.5,native,36000+3*216000,cal);CHECK(r.phase==.5f&&close_to(r.lit_fraction,0,1e-6));
 r=evaluate(.5,native,36000+6*216000,cal);CHECK(r.phase==.875f);r=evaluate(.5,native,36000+7*216000,cal);CHECK(r.phase==0);
 // Fallback detector.
 path(.5,1,1,toward);for(int i=0;i<3;++i)native[i]=-toward[i];
 double dark[3]{.00019,.00019,.00019},bright[3]{3,3,3};CHECK(parked_detector(native,dark)&&!parked_detector(native,bright));
 path(.45,1,1,toward);for(int i=0;i<3;++i)native[i]=-toward[i];CHECK(!parked_detector(native,dark));
 double zero[3]{};CHECK(!parked_detector(zero,dark));
 std::cout<<"PASS celestial\n";
}

static void sdk(){
 AnySceneLightingParametersV4 l;CHECK(l.struct_size==sizeof(l)&&l.version==4&&l.shaped.version==3&&l.lamp_budget==8&&l.flags==15&&l.native_halos==0);
 AnySceneEffectsParametersV3 e;CHECK(e.struct_size==sizeof(e)&&e.version==3&&e.water.version==2&&e.sky_model==1&&e.bloom_falloff==1);
 AnySceneParametersV4 c;CHECK(c.struct_size==sizeof(c)&&c.version==4&&c.looked.version==3&&c.tint_targets==1&&c.moon_phase_floor==.35f);
 AnySceneAntialiasingParametersV3 a;CHECK(a.struct_size==sizeof(a)&&a.version==3&&a.base.version==2&&a.taa_flags==3);
 AnySceneTimingV2 t;CHECK(t.struct_size==sizeof(t)&&t.version==2&&t.v1.version==1&&ANY_PASS_COUNT==16);
 AnyGpuInfoV2 g;CHECK(g.struct_size==sizeof(g)&&g.version==2&&g.v1.version==1&&g.lamp_shadows_supported==1);
 std::cout<<"PASS sdk defaults\n";
}

// gpu_test_device.h self-test: runs where WARP exists (the laptop), skips under Wine.
static void gpu_helper(){
 using Microsoft::WRL::ComPtr;gpu_test::Device d;
 if(!gpu_test::create(d)){std::cout<<"SKIP gpu helper (no WARP device here)\n";return;}
 D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;rd.Width=4;rd.Height=4;rd.DepthOrArraySize=1;rd.MipLevels=1;
 rd.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;rd.SampleDesc.Count=1;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
 ComPtr<ID3D12Resource> rt;CHECK(SUCCEEDED(d.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&rt))));
 D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;ComPtr<ID3D12DescriptorHeap> rtvs;CHECK(SUCCEEDED(d.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtvs))));
 auto rtv=rtvs->GetCPUDescriptorHandleForHeapStart();d.device->CreateRenderTargetView(rt.Get(),nullptr,rtv);
 ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;CHECK(SUCCEEDED(d.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))));
 CHECK(SUCCEEDED(d.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))));
 gpu_test::poison(list.Get(),rt.Get(),rtv);CHECK(SUCCEEDED(list->Close()));ID3D12CommandList* lists[]{list.Get()};d.queue->ExecuteCommandLists(1,lists);CHECK(gpu_test::wait(d));
 gpu_test::expect_clean(d,"poison");
 if(!d.validated){std::cout<<"PASS gpu helper (unvalidated)\n";return;}
 // A deliberate before-state mismatch must be reported.
 CHECK(SUCCEEDED(allocator->Reset())&&SUCCEEDED(list->Reset(allocator.Get(),nullptr)));
 D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=rt.Get();b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
 b.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;b.Transition.StateAfter=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;list->ResourceBarrier(1,&b);
 CHECK(SUCCEEDED(list->Close()));d.queue->ExecuteCommandLists(1,lists);CHECK(gpu_test::wait(d));
 CHECK(gpu_test::errors(d,"deliberate barrier mismatch")>0);
 std::cout<<"PASS gpu helper (validated)\n";
}

int main(){std::cout<<std::unitbuf;scene_frame::log_sink=sink;
 reads();views();resources();backoff();cells();registry();slots();board();sky();sdk();gpu_helper();
 std::cout<<"PASS: scene frame core\n";return 0;}
