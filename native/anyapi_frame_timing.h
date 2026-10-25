#pragma once
// Present-path measurement for anyapi.scene_timing v2 and anyapi.gpu_info v2 (AnyAPI 0.40.2).
// Include only from anyapi_platform.inc after its first platform block (uses platform::overlay_down, log_line and the
// v1 service declarations in anyapi_services.inc). Writers run on the present/submit threads; copy_* run on any thread.
// Lock order: identity_mutex, then state.lock. presented() releases state.lock before poll() takes identity_mutex.
#include "anyapi_scene_frame.h"
#include "anyapi_scene_timing_v2.h"
#include "anyapi_gpu_info_v2.h"
#include <algorithm>
#include <cstdio>
#include <mutex>
#include <string>
namespace frame_timing {
using Microsoft::WRL::ComPtr;
constexpr uint32_t RING=1024,OVERLAY_RING=600,LOG_EVERY=600,MIN_WINDOW=8;   // RING: 2 s of presents at 500 fps
constexpr uint64_t WINDOW_MS=2000,POLL_MS=2000,SETTLE_MS=2000;
static int64_t frequency(){static const int64_t f=[]{LARGE_INTEGER v{};QueryPerformanceFrequency(&v);return v.QuadPart>0?v.QuadPart:1;}();return f;}
static int64_t now(){LARGE_INTEGER v{};QueryPerformanceCounter(&v);return v.QuadPart;}
static float ms(int64_t ticks){return float(double(ticks)*1000.0/double(frequency()));}
struct Sample {int64_t at;float frame_ms,block_ms,cpu_ms;};
struct OverlaySample {float total,collect,record,end,flush;};
struct OverlayMarks {int64_t start{},collected{},recorded{},ended{},flushed{};};
struct State {SRWLOCK lock=SRWLOCK_INIT;
 Sample ring[RING]{};uint32_t head{},count{};int64_t last_entry{};uint64_t frames{};uint32_t sync_interval{},native_reads{},rpm_seen{};bool cpu_seen{};
 OverlaySample overlay[OVERLAY_RING]{};uint32_t overlay_head{},overlay_count{};uint64_t overlay_frames{};
 uint64_t settle_until_ms{};int64_t next_poll{};std::atomic<float> refresh_hz{0};std::atomic<uint32_t> budget_mb{0},usage_mb{0};
 AnySceneTimingV2 cached{};int64_t cached_at{};};
static State state;
static std::atomic<bool> refresh_due{true};                  // WM_DISPLAYCHANGE, settle(): poll on the next present
static ComPtr<IDXGIAdapter3> adapter3;static std::mutex identity_mutex;static ID3D12Device* identity_device{};static uint64_t identity_luid{};static bool identity_presenter{};
// Flag 2 for the next 2 s (resize, new chain, GPU change), and a refresh-rate/VRAM poll on the next present.
static void settle(){AcquireSRWLockExclusive(&state.lock);state.settle_until_ms=GetTickCount64()+SETTLE_MS;ReleaseSRWLockExclusive(&state.lock);refresh_due=true;}
static void clear(){AcquireSRWLockExclusive(&state.lock);state.head=state.count=0;state.last_entry=0;state.frames=0;state.overlay_head=state.overlay_count=0;state.overlay_frames=0;
 state.cached_at=0;state.settle_until_ms=0;state.next_poll=0;state.cpu_seen=false;ReleaseSRWLockExclusive(&state.lock);}   // tests
// Nearest-rank percentile (index round(q*(n-1))); reorders v. n==0 -> -1.
static float percentile(float* v,uint32_t n,float q){if(!n)return -1;uint32_t k=std::min(n-1,uint32_t(q*float(n-1)+.5f));std::nth_element(v,v+k,v+n);return v[k];}

// Fills every v2 field except v1 from the ring and the board. Caller holds state.lock; t starts default-initialised.
static void fill(AnySceneTimingV2& t,int64_t at){
 float f[RING],b[RING],c[RING];uint32_t n=0;int64_t window=int64_t(WINDOW_MS)*frequency()/1000;
 for(uint32_t i=0;i<state.count;++i){auto& s=state.ring[(state.head+RING-1-i)%RING];if(at-s.at>window)break;f[n]=s.frame_ms;b[n]=s.block_ms;c[n]=s.cpu_ms;++n;}
 t.frame_index=state.frames;t.sync_interval=state.sync_interval;t.refresh_hz=state.refresh_hz.load();
 if(n){t.frame_ms_p50=percentile(f,n,.5f);t.frame_ms_p95=percentile(f,n,.95f);t.frame_ms_p99=percentile(f,n,.99f);t.present_block_ms=percentile(b,n,.5f);t.anyapi_cpu_ms=state.cpu_seen?percentile(c,n,.5f):-1;}
 for(uint32_t i=0;i<ANY_PASS_COUNT;++i)t.pass_ms[i]=scene_frame::board.pass_ms[i].load(std::memory_order_relaxed);
 float o[OVERLAY_RING];uint32_t m=state.overlay_count;for(uint32_t i=0;i<m;++i)o[i]=state.overlay[i].total;
 t.pass_ms[ANY_PASS_OVERLAY]=m?percentile(o,m,.5f):-1;t.overlay_ms_p95=m?percentile(o,m,.95f):-1;
 t.native_reads=state.native_reads;t.gpu_scene_ms=-1;t.gpu_busy=-1;
 t.flags=(n>=MIN_WINDOW?1u:0u)|(GetTickCount64()<state.settle_until_ms?2u:0u)|(platform::overlay_down.load()?8u:0u);}   // bit 4 (GPU timestamps) stays 0 in 0.40.2

static std::string format(const AnySceneTimingV2& t){char m[512];
 std::snprintf(m,sizeof(m),"SCENE_TIMING_V2 frames=%llu frame_p50=%.2f p95=%.2f p99=%.2f block_p50=%.2f sync=%u refresh_hz=%.1f cpu_p50=%.3f native_reads=%u overlay_p50=%.3f overlay_p95=%.3f vram_budget_mb=%u vram_usage_mb=%u flags=%u pass_ms=%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f",
  (unsigned long long)t.frame_index,t.frame_ms_p50,t.frame_ms_p95,t.frame_ms_p99,t.present_block_ms,t.sync_interval,t.refresh_hz,t.anyapi_cpu_ms,t.native_reads,t.pass_ms[ANY_PASS_OVERLAY],t.overlay_ms_p95,
  state.budget_mb.load(),state.usage_mb.load(),t.flags,t.pass_ms[0],t.pass_ms[1],t.pass_ms[2],t.pass_ms[3],t.pass_ms[4],t.pass_ms[5],t.pass_ms[6],t.pass_ms[7],t.pass_ms[8]);
 return m;}

// Refresh rate of the output holding the window, and the adapter's local VRAM budget/usage. Present thread, every 2 s
// and after WM_DISPLAYCHANGE or settle(); outside state.lock. Windowed, the display mode is read again only when the
// window's monitor changed or refresh is set (EnumDisplaySettings can take milliseconds, e.g. under Proton); the 2 s
// poll itself is one MonitorFromWindow.
static std::mutex poll_mutex;static HMONITOR polled_monitor{};static float polled_hz{};   // under poll_mutex
static void poll(IDXGISwapChain* chain,bool refresh){float hz=0;ComPtr<IDXGISwapChain1> one;DXGI_SWAP_CHAIN_FULLSCREEN_DESC fd{};
 if(SUCCEEDED(chain->QueryInterface(IID_PPV_ARGS(&one)))&&SUCCEEDED(one->GetFullscreenDesc(&fd))&&!fd.Windowed&&fd.RefreshRate.Denominator)hz=float(fd.RefreshRate.Numerator)/float(fd.RefreshRate.Denominator);
 if(hz<=0){HWND window{};DXGI_SWAP_CHAIN_DESC sd{};if(!one||FAILED(one->GetHwnd(&window)))window=nullptr;if(!window&&SUCCEEDED(chain->GetDesc(&sd)))window=sd.OutputWindow;
  HMONITOR monitor=window?MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST):nullptr;std::lock_guard<std::mutex> p(poll_mutex);
  if(monitor&&monitor==polled_monitor&&!refresh)hz=polled_hz;   // same monitor, no display change: the cached rate (0 stays unknown)
  else if(monitor){MONITORINFOEXW mi{};mi.cbSize=sizeof(mi);DEVMODEW dm{};dm.dmSize=sizeof(dm);
   if(GetMonitorInfoW(monitor,(MONITORINFO*)&mi)&&EnumDisplaySettingsW(mi.szDevice,ENUM_CURRENT_SETTINGS,&dm)&&dm.dmDisplayFrequency>1)hz=float(dm.dmDisplayFrequency);
   polled_monitor=monitor;polled_hz=hz;}}
 state.refresh_hz=hz;   // 0 = unknown
 std::lock_guard<std::mutex> g(identity_mutex);DXGI_QUERY_VIDEO_MEMORY_INFO info{};
 if(adapter3&&SUCCEEDED(adapter3->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&info))){state.budget_mb=uint32_t(info.Budget>>20);state.usage_mb=uint32_t(info.CurrentUsage>>20);}}

// Present thread, after the game's Present returns. entry: hook entry (present-to-present); before/after: around the
// game's own Present (block time). board.cpu_ticks is consumed per present; rpm_calls is read as a delta.
static void presented(IDXGISwapChain* chain,int64_t entry,int64_t before,int64_t after,uint32_t interval){
 uint64_t ticks=scene_frame::board.cpu_ticks.exchange(0,std::memory_order_relaxed);uint32_t reads=scene_frame::rpm_calls.load(std::memory_order_relaxed);
 bool log=false,due=false;AnySceneTimingV2 snapshot;
 AcquireSRWLockExclusive(&state.lock);
 if(!state.frames)state.rpm_seen=reads;
 if(state.last_entry){float dt=ms(entry-state.last_entry);if(dt>0&&dt<float(WINDOW_MS)){state.ring[state.head]={entry,dt,ms(after-before),ms(int64_t(ticks))};state.head=(state.head+1)%RING;state.count=std::min(state.count+1,RING);}}   // gaps over 2 s (loading, minimised) are skipped
 state.last_entry=entry;++state.frames;state.sync_interval=interval;if(ticks)state.cpu_seen=true;state.native_reads=reads-state.rpm_seen;state.rpm_seen=reads;
 if(entry>=state.next_poll){state.next_poll=entry+int64_t(POLL_MS)*frequency()/1000;due=true;}
 if(state.frames%LOG_EVERY==0){log=true;fill(snapshot,after);}
 ReleaseSRWLockExclusive(&state.lock);
 bool refresh=refresh_due.exchange(false);if(chain&&(due||refresh))poll(chain,refresh);
 if(log)log_line(ANY_LOG_INFO,"framework",format(snapshot).c_str());}

// Present thread, once per draw(): overlay CPU split into collect (mod callbacks, gpu_draw collect, canvas uploads and
// any overlay start attempt), record (post-process and D2D calls up to EndDraw), EndDraw, and Release+Flush.
static void overlay_frame(const OverlayMarks& k){auto part=[](int64_t a,int64_t b){return a&&b&&b>=a?ms(b-a):0.f;};
 OverlaySample s{part(k.start,k.flushed?k.flushed:k.collected),part(k.start,k.collected),part(k.collected,k.recorded),part(k.recorded,k.ended),part(k.ended,k.flushed)};
 bool log=false;float p50=-1,p95=-1,c50=-1,r50=-1,e50=-1,f50=-1;
 AcquireSRWLockExclusive(&state.lock);state.overlay[state.overlay_head]=s;state.overlay_head=(state.overlay_head+1)%OVERLAY_RING;state.overlay_count=std::min(state.overlay_count+1,OVERLAY_RING);
 if(++state.overlay_frames%LOG_EVERY==0){log=true;float v[OVERLAY_RING];uint32_t n=state.overlay_count;
  auto pick=[&](float OverlaySample::*field,float q){for(uint32_t i=0;i<n;++i)v[i]=state.overlay[i].*field;return percentile(v,n,q);};
  p50=pick(&OverlaySample::total,.5f);p95=pick(&OverlaySample::total,.95f);c50=pick(&OverlaySample::collect,.5f);r50=pick(&OverlaySample::record,.5f);e50=pick(&OverlaySample::end,.5f);f50=pick(&OverlaySample::flush,.5f);}
 ReleaseSRWLockExclusive(&state.lock);
 if(!log)return;scene_frame::board.pass_ms[ANY_PASS_OVERLAY].store(p50,std::memory_order_relaxed);char m[256];
 std::snprintf(m,sizeof(m),"PLUGIN_RENDER overlay_ms p50=%.3f p95=%.3f collect_p50=%.3f record_p50=%.3f enddraw_p50=%.3f flush_p50=%.3f frames=%u down=%d",p50,p95,c50,r50,e50,f50,LOG_EVERY,platform::overlay_down.load()?1:0);
 log_line(ANY_LOG_INFO,"framework",m);}

// K24: execute thread (first direct-queue submission of a device) or present thread (presenter=true at chain accept).
// The swapchain's device wins over other in-process devices. CreateDXGIFactory1 runs once per device, never per frame.
static void identify(ID3D12Device* device,bool presenter){if(!device)return;LUID l=device->GetAdapterLuid();uint64_t luid=(uint64_t(uint32_t(l.HighPart))<<32)|l.LowPart;
 std::lock_guard<std::mutex> g(identity_mutex);
 if(identity_device==device&&identity_luid==luid){identity_presenter=identity_presenter||presenter;return;}   // steady state: one compare
 if(identity_presenter&&!presenter)return;
 scene_frame::GpuIdentity id{};id.luid=luid;D3D12_FEATURE_DATA_ARCHITECTURE1 a1{};D3D12_FEATURE_DATA_ARCHITECTURE a0{};
 if(SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1,&a1,sizeof(a1))))id.uma=a1.UMA;else if(SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE,&a0,sizeof(a0))))id.uma=a0.UMA;
 ComPtr<IDXGIFactory4> f;ComPtr<IDXGIAdapter1> adapter;DXGI_ADAPTER_DESC1 d{};adapter3.Reset();
 if(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&f)))&&SUCCEEDED(f->EnumAdapterByLuid(l,IID_PPV_ARGS(&adapter)))&&SUCCEEDED(adapter->GetDesc1(&d))){
  id.known=1;id.vendor_id=d.VendorId;id.device_id=d.DeviceId;id.dedicated_mb=uint32_t(d.DedicatedVideoMemory>>20);id.shared_mb=uint32_t(d.SharedSystemMemory>>20);
  WideCharToMultiByte(CP_UTF8,0,d.Description,-1,id.adapter_name,int(sizeof(id.adapter_name))-1,nullptr,nullptr);adapter.As(&adapter3);}
 id.adapter_name[sizeof(id.adapter_name)-1]=0;
 identity_device=device;identity_luid=luid;identity_presenter=presenter;scene_frame::board.gpu.store(id);
 char m[320];std::snprintf(m,sizeof(m),"GPU_IDENTITY known=%u vendor=0x%04X device=0x%04X luid=0x%016llX uma=%u dedicated_mb=%u shared_mb=%u source=%s name=\"%s\"",id.known,id.vendor_id,id.device_id,(unsigned long long)luid,id.uma,id.dedicated_mb,id.shared_mb,presenter?"swapchain":"queue",id.adapter_name);
 log_line(ANY_LOG_INFO,"framework",m);settle();}

// anyapi.scene_timing v2. copy is the v1 call (false without a v1 provider). copy_v2 recomputes at most 10 times a second.
static bool copy_v1(AnySceneTimingV1* t){auto v1=(const AnySceneTimingServiceV1*)anyapi_scene_timing_service();return v1&&v1->copy&&v1->copy(t);}
static bool copy_v2(AnySceneTimingV2* t){if(!t||t->struct_size!=sizeof(*t)||t->version!=2)return false;AnySceneTimingV2 out;copy_v1(&out.v1);int64_t at=now();
 AcquireSRWLockExclusive(&state.lock);
 if(state.cached_at&&at-state.cached_at<frequency()/10){auto v1=out.v1;out=state.cached;out.v1=v1;}else{fill(out,at);state.cached=out;state.cached_at=at;}
 ReleaseSRWLockExclusive(&state.lock);*t=out;return true;}
// anyapi.gpu_info v2: board.gpu is the source of truth once known; v1 (lighting) otherwise.
static bool copy_gpu_v1(AnyGpuInfoV1* g){auto v1=(const AnyGpuInfoServiceV1*)anyapi_gpu_info_service();return v1&&v1->copy&&v1->copy(g);}
static bool copy_gpu_v2(AnyGpuInfoV2* g){if(!g||g->struct_size!=sizeof(*g)||g->version!=2)return false;AnyGpuInfoV2 out;copy_gpu_v1(&out.v1);
 scene_frame::GpuIdentity id{};if(scene_frame::board.gpu.load(id)&&id.known){out.v1.known=1;out.v1.vendor_id=id.vendor_id;out.v1.device_id=id.device_id;
  out.uma=id.uma;out.dedicated_mb=id.dedicated_mb;out.shared_mb=id.shared_mb;memcpy(out.adapter_name,id.adapter_name,sizeof(out.adapter_name));out.adapter_name[sizeof(out.adapter_name)-1]=0;}
 out.lamp_shadows_supported=out.v1.known&&out.v1.vendor_id==0x1002?0:1;   // AMD: lamp beams without spot shadow maps in 0.40.2
 out.budget_mb=state.budget_mb.load();out.usage_mb=state.usage_mb.load();out.width=scene_frame::board.scene_width.load();out.height=scene_frame::board.scene_height.load();*g=out;return true;}
static const AnySceneTimingServiceV2 api_v2{sizeof(AnySceneTimingServiceV2),2,copy_v1,copy_v2};
static const AnyGpuInfoServiceV2 gpu_info_v2{sizeof(AnyGpuInfoServiceV2),2,copy_gpu_v1,copy_gpu_v2};
}
ANYAPI_REGISTER_SERVICE(scene_timing_v2,"anyapi.scene_timing",2,&frame_timing::api_v2);
ANYAPI_REGISTER_SERVICE(gpu_info_v2,"anyapi.gpu_info",2,&frame_timing::gpu_info_v2);
