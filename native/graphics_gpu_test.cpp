#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>
#include <cassert>
#include <iostream>
#include "anymaker_mod_api.h"
#include "anyapi_mod_v1.h"
static std::filesystem::path g_game_dir;
static void log_line(AnymakerLogLevel,const char*,const char* msg){std::cout<<msg<<'\n';}
static std::string utf8(const std::filesystem::path& p){return p.filename().string();}
static bool p34_copy_snapshot(AnySessionPlayersV1*,uint32_t*);
#include "anyapi_platform.inc"
#include "anyapi_creation_balance_v1.h"
static const AnyCreationBalanceV1* anyapi_creation_balance_service(){return nullptr;}
static const AnyEquipmentV1* anyapi_equipment_service(){return nullptr;}
static const AnyEquipmentV2* anyapi_equipment_service_v2(){return nullptr;}
static const AnyWorldTimeV1* anyapi_world_time_service(){return nullptr;}
static const AnyBuildV1* anyapi_build_service(){return nullptr;}
static const void* anyapi_scene_timing_service(){return nullptr;}
static const void* anyapi_gpu_info_service(){return nullptr;}
#include "anyapi_client_tasks.inc"
static const AnyMenuV1* anyapi_menu_service(){return nullptr;}
static const AnyMenuV2* anyapi_menu_service_v2(){return nullptr;}
static const AnyMenuV3* anyapi_menu_service_v3(){return nullptr;}static const AnyMenuV4* anyapi_menu_service_v4(){return nullptr;}static const AnyMenuV5* anyapi_menu_service_v5(){return nullptr;}
static bool p34_copy_snapshot(AnySessionPlayersV1*,uint32_t*){return false;}
static const AnyInventoryActionsV1* anyapi_inventory_actions_service(){return nullptr;}
static const AnyScreenLayoutV1* anyapi_screen_layout_service(){return nullptr;}
static const AnySessionV1* anyapi_session_service(){return nullptr;}
static const AnyUiStateV1* anyapi_ui_state_service(){return nullptr;}
static const AnyInventoryUiV1* anyapi_inventory_ui_service(){return nullptr;}
static const AnyInventoryUiV2* anyapi_inventory_ui_service_v2(){return nullptr;}
static const AnyInventoryUiV3* anyapi_inventory_ui_service_v3(){return nullptr;}
static const AnyInventoryActionsV2* anyapi_inventory_actions_service_v2(){return nullptr;}
using Microsoft::WRL::ComPtr;
static uint64_t effect;static bool effect_on=true;
static uint8_t pixels[8*8*4];
static std::vector<AnyInputV1> input_events;
static uint32_t input(const AnyInputV1* e,void*){input_events.push_back(*e);return 1;}
static void render(const AnyFrameV1*,AnyCanvasV1* out,void*){AnyPostParametersV1 params;params.enabled=effect_on;assert(post_process::set(effect,&params));out->width=8;out->height=8;out->pitch=32;out->x=4;out->y=4;out->revision=0;out->pixels=pixels;}
static void gpu_fixture(const AnyFrameV1*,void*){auto api=anyapi_gpu_draw_service();assert(api->texture(1,8,8,32,pixels,1));AnyGpuCommandV1 c;c.kind=ANY_GPU_IMAGE;c.texture=1;c.rect[2]=c.rect[3]=8;c.source[2]=c.source[3]=8;c.matrix[4]=c.matrix[5]=4;assert(api->emit(&c));}
static_assert(sizeof(AnyPostPassV1)==48&&sizeof(AnyPostParametersV1)==264&&sizeof(AnyPostStatusV1)==304&&sizeof(AnyPostProcessV1)==32);
// Timing v2 poll: a swapchain that only answers the refresh-rate queries and counts them (exclusive fullscreen at 60 Hz).
struct PollChain:IDXGISwapChain1{HWND window{};int descs{},fullscreen{},outputs{};
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(id==__uuidof(IDXGISwapChain1)||id==__uuidof(IDXGISwapChain)||id==__uuidof(IUnknown)){*out=this;return S_OK;}*out=nullptr;return E_NOINTERFACE;}
 ULONG STDMETHODCALLTYPE AddRef()override{return 2;}ULONG STDMETHODCALLTYPE Release()override{return 1;}
 HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,UINT,const void*)override{return E_NOTIMPL;}HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID,const IUnknown*)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,UINT*,void*)override{return E_NOTIMPL;}HRESULT STDMETHODCALLTYPE GetParent(REFIID,void**)override{return E_NOTIMPL;}HRESULT STDMETHODCALLTYPE GetDevice(REFIID,void**)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE Present(UINT,UINT)override{return E_NOTIMPL;}HRESULT STDMETHODCALLTYPE GetBuffer(UINT,REFIID,void**)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL,IDXGIOutput*)override{return E_NOTIMPL;}HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL*,IDXGIOutput**)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* d)override{++descs;*d={};d->OutputWindow=window;d->Windowed=FALSE;return S_OK;}
 HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT,UINT,UINT,DXGI_FORMAT,UINT)override{return E_NOTIMPL;}HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC*)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** o)override{++outputs;*o=nullptr;return E_FAIL;}
 HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS*)override{return E_NOTIMPL;}HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT*)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1*)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* d)override{++fullscreen;*d={};d->RefreshRate={60,1};d->Windowed=FALSE;return S_OK;}
 HRESULT STDMETHODCALLTYPE GetHwnd(HWND*)override{return E_NOTIMPL;}HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID,void**)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE Present1(UINT,UINT,const DXGI_PRESENT_PARAMETERS*)override{return E_NOTIMPL;}BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported()override{return FALSE;}
 HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput**)override{return E_NOTIMPL;}HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA*)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA*)override{return E_NOTIMPL;}HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION)override{return E_NOTIMPL;}HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION*)override{return E_NOTIMPL;}};
int main(){
 for(int i=0;i<64;++i){pixels[i*4+2]=255;pixels[i*4+3]=255;}platform::plugins[0].callbacks.id="render_fixture";platform::plugins[0].callbacks.render=render;platform::plugin_count.store(1);
 platform::plugins[0].callbacks.input=input;RAWMOUSE mouse{};mouse.lLastX=5;mouse.usButtonFlags=RI_MOUSE_LEFT_BUTTON_DOWN|RI_MOUSE_WHEEL;mouse.usButtonData=USHORT(SHORT(-120));assert(platform::raw_mouse(mouse,17,23));
 assert(input_events.size()==3&&input_events[0].kind==ANY_MOUSE_MOVE&&input_events[1].kind==ANY_MOUSE_DOWN&&input_events[1].button==1&&input_events[2].kind==ANY_MOUSE_WHEEL&&input_events[2].wheel== -120&&input_events[2].x==17&&input_events[2].y==23);
 mouse={};mouse.usButtonFlags=RI_MOUSE_LEFT_BUTTON_UP;platform::raw_mouse(mouse,18,24);assert(input_events.back().kind==ANY_MOUSE_UP);
 input_events.clear();assert(!platform::text_input(0xd83c)&&input_events.empty());assert(platform::text_input(0xdfe0)&&input_events.size()==1&&input_events[0].kind==ANY_TEXT_INPUT&&input_events[0].key==0x1f3e0);assert(platform::text_input('N')&&input_events.back().kind==ANY_TEXT_INPUT&&input_events.back().key=='N');platform::text_decoder.reset();assert(!platform::text_input(0xdfe0));
 AnyCanvasV1 bad{};assert(!platform::valid_canvas(bad));bad.pixels=pixels;bad.width=8193;bad.height=1;bad.pitch=32772;assert(!platform::valid_canvas(bad));
 platform::current_plugin=0;AnyPostPassV1 pass;pass.id="invert";pass.shader="Texture2D image:register(t0);SamplerState s:register(s0);float4 main(float4 p:SV_Position,float2 uv:TEXCOORD):SV_Target{return float4(1-image.SampleLevel(s,uv,0).rgb,1);}";pass.shader_bytes=uint32_t(strlen(pass.shader));effect=post_process::registration(&pass);assert(effect);
 AnyPostParametersV1 params;params.enabled=1;platform::current_plugin=1;assert(!post_process::set(effect,&params));platform::current_plugin=0;params.values[0]=NAN;assert(!post_process::set(effect,&params));pass.id="invalid";pass.shader="not HLSL";pass.shader_bytes=8;assert(!post_process::registration(&pass));pass.shader=post_process::copy_source;pass.shader_bytes=uint32_t(strlen(pass.shader));pass.id="future";pass.input=999;assert(!post_process::registration(&pass));pass.input=effect;platform::current_plugin=1;assert(!post_process::registration(&pass));platform::current_plugin=0;AnyPostStatusV1 invalid_status;invalid_status.version=2;assert(!post_process::status(&invalid_status));platform::current_plugin=-1;
 // WI-1: nearest-rank percentiles over the last 2 s of presents (synthetic QPC; no GPU).
 {float v[100];for(int i=0;i<100;++i)v[i]=i<90?10.f:i<95?20.f:40.f;std::reverse(v,v+100);
  assert(frame_timing::percentile(v,100,.5f)==10&&frame_timing::percentile(v,100,.95f)==20&&frame_timing::percentile(v,100,.99f)==40&&frame_timing::percentile(v,0,.5f)==-1);
  float one=3;assert(frame_timing::percentile(&one,1,.99f)==3);}
 {auto f=frame_timing::frequency();int64_t t=1000*f;frame_timing::clear();
  for(int i=0;i<50;++i){t+=f/10;frame_timing::presented(nullptr,t,t,t,1);}          // 100 ms frames, later outside the window
  t+=3*f;frame_timing::presented(nullptr,t,t,t,1);                                    // 3 s gap: skipped, not a sample
  for(int i=0;i<100;++i){t+=i<90?f/100:i<95?f/50:f/25;scene_frame::board.cpu_ticks.store(uint64_t(f/2000));scene_frame::rpm_calls.fetch_add(7);frame_timing::presented(nullptr,t,t,t+f/500,2);}
  assert(scene_frame::board.cpu_ticks.load()==0);                                     // consumed per present
  scene_frame::board.pass_ms[ANY_PASS_GI].store(1.25f);
  for(int i=0;i<10;++i){frame_timing::OverlayMarks k;k.start=t;k.collected=t+f/1000;k.recorded=t+2*f/1000;k.ended=t+3*f/1000;k.flushed=t+4*f/1000;frame_timing::overlay_frame(k);}
  AnySceneTimingV2 out;AcquireSRWLockShared(&frame_timing::state.lock);frame_timing::fill(out,t);ReleaseSRWLockShared(&frame_timing::state.lock);
  assert(std::abs(out.frame_ms_p50-10)<.01f&&std::abs(out.frame_ms_p95-20)<.01f&&std::abs(out.frame_ms_p99-40)<.01f&&std::abs(out.present_block_ms-2)<.01f);
  assert(std::abs(out.anyapi_cpu_ms-.5f)<.01f&&out.native_reads==7&&out.sync_interval==2&&out.frame_index==151&&(out.flags&1)&&!(out.flags&2));
  assert(out.pass_ms[ANY_PASS_GI]==1.25f&&std::abs(out.pass_ms[ANY_PASS_OVERLAY]-4)<.01f&&std::abs(out.overlay_ms_p95-4)<.01f&&out.gpu_scene_ms==-1&&out.gpu_busy==-1&&out.pass_ms[ANY_PASS_AO]==-1);
  frame_timing::settle();AnySceneTimingV2 settling;AcquireSRWLockShared(&frame_timing::state.lock);frame_timing::fill(settling,t);ReleaseSRWLockShared(&frame_timing::state.lock);assert(settling.flags&2);
  AnySceneTimingV2 bad;bad.version=1;assert(!frame_timing::api_v2.copy_v2(&bad));AnySceneTimingV1 v1;assert(!frame_timing::api_v2.copy(&v1));   // no v1 provider in this fixture
  AnySceneTimingV2 live;assert(frame_timing::api_v2.copy_v2(&live)&&live.frame_index==151);                                                    // fill at the real QPC time; frame count is window-independent
  AnyGpuInfoV2 g;assert(frame_timing::gpu_info_v2.copy_v2(&g)&&!g.v1.known&&g.lamp_shadows_supported==1);                                     // before any device
  AnyGpuInfoV2 tiny;tiny.struct_size=8;assert(!frame_timing::gpu_info_v2.copy_v2(&tiny));   // not "small": rpcndr.h defines it as char
  scene_frame::board.pass_ms[ANY_PASS_GI].store(-1);frame_timing::clear();}
 // Timing v2 poll cost: the 2 s poll on a hooked window is one MonitorFromWindow; the full query runs for a new chain,
 // WM_DISPLAYCHANGE/settle(), or every poll on a window AnyAPI does not hook. gpu_info v2 copies gate the VRAM poll.
 {PollChain c;c.window=GetDesktopWindow();platform::game_window=c.window;auto f=frame_timing::frequency();int64_t t=5000*f;frame_timing::clear();frame_timing::refresh_due=false;
  frame_timing::presented(&c,t,t,t,1);assert(c.descs==1&&c.fullscreen==1&&c.outputs==0&&frame_timing::state.refresh_hz.load()==60);   // new chain
  for(int i=0;i<3;++i){t+=3*f;frame_timing::presented(&c,t,t,t,1);}assert(c.descs==1&&c.fullscreen==1);                                  // three 2 s polls, same monitor
  frame_timing::refresh_due=true;t+=f/100;frame_timing::presented(&c,t,t,t,1);assert(c.descs==2&&c.fullscreen==2);                        // WM_DISPLAYCHANGE
  t+=f/100;frame_timing::presented(&c,t,t,t,1);assert(c.fullscreen==2);                                                                     // not due
  platform::game_window=nullptr;t+=3*f;frame_timing::presented(&c,t,t,t,1);assert(c.fullscreen==3);                                       // unhooked window: full query
  frame_timing::vram_due=false;frame_timing::vram_wanted_at=0;AnyGpuInfoV2 g;assert(frame_timing::gpu_info_v2.copy_v2(&g)&&frame_timing::vram_due.exchange(false));
  assert(frame_timing::gpu_info_v2.copy_v2(&g)&&!frame_timing::vram_due.load());                                                          // a steady consumer does not re-arm it
  frame_timing::poll_chain=nullptr;frame_timing::poll_window=nullptr;frame_timing::poll_monitor=nullptr;frame_timing::state.refresh_hz=0;frame_timing::clear();}
 assert(scene_frame::find_service("anyapi.scene_timing",2)==&frame_timing::api_v2&&scene_frame::find_service("anyapi.gpu_info",2)==&frame_timing::gpu_info_v2);
 assert(services::query("anyapi.scene_timing",2)==&frame_timing::api_v2&&services::query("anyapi.gpu_info",2)==&frame_timing::gpu_info_v2&&!services::query("anyapi.scene_timing",3));
 std::cout<<"PASS: timing v2 percentiles, 2 s window, gap skip, cpu/native-read sampling, overlay p50/p95, flags, registry\n";
 // K41: layout cache hits without a copy, LRU eviction of layouts unused for 300 frames once 512 are cached, hard cap 1024.
 if(gpu_draw::font(14,false)){auto name=[](int i){return L"layout "+std::to_wstring(i);};
  auto first=gpu_draw::layout(name(0),14,100,20,0);assert(first&&gpu_draw::layout(std::wstring_view(name(0)),14,100,20,0)==first&&gpu_draw::layout(name(0),14,101,20,0)!=first);
  for(int i=1;i<1100;++i)assert(gpu_draw::layout(name(i),14,100,20,0));assert(gpu_draw::layouts.size()<=1024&&gpu_draw::layouts.size()>=512);
  for(int i=0;i<400;++i)gpu_draw::begin_frame();for(int i=0;i<8;++i)gpu_draw::layout(name(5000+i),14,100,20,0);
  auto keep=gpu_draw::layout(name(5000),14,100,20,0);for(int i=0;i<60;++i)gpu_draw::begin_frame();gpu_draw::layout(name(6000),14,100,20,0);
  assert(gpu_draw::layouts.size()<=10&&gpu_draw::layout(name(5000),14,100,20,0)==keep);gpu_draw::layouts.clear();
  std::cout<<"PASS: gpu_draw layout cache lookups and LRU eviction\n";}
 else std::cout<<"NOTE: DirectWrite unavailable; layout cache check skipped\n";
 assert(platform::start());ComPtr<IDXGIFactory4> factory;assert(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));ComPtr<IDXGIAdapter> warp;assert(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))));
 ComPtr<ID3D12Device> device;assert(SUCCEEDED(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))));D3D12_COMMAND_QUEUE_DESC qdesc{};ComPtr<ID3D12CommandQueue> queue;assert(SUCCEEDED(device->CreateCommandQueue(&qdesc,IID_PPV_ARGS(&queue))));
 WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"AnyAPI.RenderTest";RegisterClassW(&wc);HWND hwnd=CreateWindowW(wc.lpszClassName,L"",WS_POPUP,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(hwnd);
 DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=64;desc.Height=64;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
 ComPtr<IDXGISwapChain1> first;assert(SUCCEEDED(factory->CreateSwapChainForHwnd(queue.Get(),hwnd,&desc,nullptr,nullptr,&first)));ComPtr<IDXGISwapChain3> swap;assert(SUCCEEDED(first.As(&swap)));
 ComPtr<ID3D12Fence> fence;assert(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);uint64_t value{};
 auto wait=[&](){assert(SUCCEEDED(queue->Signal(fence.Get(),++value)));if(fence->GetCompletedValue()<value){assert(SUCCEEDED(fence->SetEventOnCompletion(value,event)));assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);}};
 auto check=[&](UINT width,UINT height){
  UINT index=swap->GetCurrentBackBufferIndex();ComPtr<ID3D12Resource> buffer;assert(SUCCEEDED(swap->GetBuffer(index,IID_PPV_ARGS(&buffer))));
  ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;assert(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))));assert(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))));
  D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;ComPtr<ID3D12DescriptorHeap> heap;assert(SUCCEEDED(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap))));auto handle=heap->GetCPUDescriptorHandleForHeapStart();device->CreateRenderTargetView(buffer.Get(),nullptr,handle);
  D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.pResource=buffer.Get();barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;list->ResourceBarrier(1,&barrier);float green[]={0,1,0,1};list->ClearRenderTargetView(handle,green,0,nullptr);std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);list->ResourceBarrier(1,&barrier);assert(SUCCEEDED(list->Close()));ID3D12CommandList* commands[]={list.Get()};queue->ExecuteCommandLists(1,commands);wait();
  assert(SUCCEEDED(swap->Present(0,0)));wait();assert(platform::frames.load()>0);
  assert(SUCCEEDED(allocator->Reset()));assert(SUCCEEDED(list->Reset(allocator.Get(),nullptr)));D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rows{};UINT64 row_bytes{},total{};auto bd=buffer->GetDesc();device->GetCopyableFootprints(&bd,0,1,0,&footprint,&rows,&row_bytes,&total);
  D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=total;rd.Height=1;rd.DepthOrArraySize=1;rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;ComPtr<ID3D12Resource> readback;assert(SUCCEEDED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))));
  barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;list->ResourceBarrier(1,&barrier);D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprint;D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=buffer.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);list->ResourceBarrier(1,&barrier);assert(SUCCEEDED(list->Close()));queue->ExecuteCommandLists(1,commands);wait();
  uint8_t* data{};D3D12_RANGE range{0,size_t(total)};assert(SUCCEEDED(readback->Map(0,&range,(void**)&data)));auto at=data+footprint.Offset+6*footprint.Footprint.RowPitch+6*4;assert(at[0]>240&&at[1]<10&&at[2]<10);auto background=data+footprint.Offset+20*footprint.Footprint.RowPitch+20*4;if(effect_on)assert(background[0]>240&&background[1]<10&&background[2]>240);else assert(background[0]<10&&background[1]>240&&background[2]<10);D3D12_RANGE none{0,0};readback->Unmap(0,&none);
  std::cout<<"PASS: GPU readback confirms red DLL canvas over green game backbuffer at "<<width<<'x'<<height<<'\n';
 };
 check(64,64);assert(SUCCEEDED(swap->ResizeBuffers(2,96,80,DXGI_FORMAT_UNKNOWN,0)));check(96,80);
 effect_on=false;auto effect_frames=post_process::snapshot.frames;check(96,80);assert(post_process::snapshot.frames==effect_frames);effect_on=true;check(96,80);assert(post_process::snapshot.frames>effect_frames);assert(post_process::snapshot.active_passes==1);assert(post_process::back_targets.size()<=2);
 auto timing=(const AnySceneTimingServiceV2*)scene_frame::find_service("anyapi.scene_timing",2);AnySceneTimingV2 t;
 assert(timing&&timing->version==2&&timing->copy_v2(&t)&&t.frame_index>=4&&t.sync_interval==0&&t.pass_ms[ANY_PASS_OVERLAY]>=0&&!(t.flags&8));
 auto info=(const AnyGpuInfoServiceV2*)scene_frame::find_service("anyapi.gpu_info",2);AnyGpuInfoV2 g;
 assert(info&&info->copy_v2(&g)&&g.v1.known&&g.v1.vendor_id==0x1414&&g.adapter_name[0]&&g.lamp_shadows_supported==1);
 std::cout<<"PASS: timing v2 and gpu_info v2 on WARP refresh_hz="<<t.refresh_hz<<" budget_mb="<<g.budget_mb<<" uma="<<g.uma<<" name="<<g.adapter_name<<'\n';
 wait();platform::reset();assert(post_process::back_targets.empty());CloseHandle(event);DestroyWindow(hwnd);std::cout<<"PASS: shaders compile, effects change game pixels while AnyAPI UI stays intact, disable bypasses and resize recovers\n";
}
