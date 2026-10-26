#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>
#include <cassert>
#include <iostream>
#include <cstring>
#include <memory>
#include "anymaker_mod_api.h"
#include "anyapi_mod_v1.h"
static std::filesystem::path g_game_dir;
static int fault_logs{},overlay_down_logs{};
static void log_line(AnymakerLogLevel,const char*,const char* msg){std::cout<<msg<<'\n';if(!strncmp(msg,"PLUGIN_FAULT ",13))++fault_logs;if(!strncmp(msg,"PLUGIN_RENDER overlay=DOWN ",27))++overlay_down_logs;}
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
static uint8_t pixels[8*8*4];
static std::vector<AnyInputV1> input_events;
static uint32_t input(const AnyInputV1* e,void*){input_events.push_back(*e);return 1;}
static int renders{};
// Hook re-entry cases (crash 2026-10-26): stand-in overlay hooks count their calls and forward to the function they
// replaced; a depth limit turns a regression into a failed assert instead of a stack overflow.
struct StandIn {int calls{},depth{};platform::ResizeFn next{};};
static StandIn below,above,reused,compiled,cross;
template<StandIn* o> static HRESULT STDMETHODCALLTYPE overlay_resize(IDXGISwapChain* s,UINT c,UINT w,UINT h,DXGI_FORMAT f,UINT fl){if(o->depth>8)return E_ABORT;++o->calls;++o->depth;HRESULT hr=o->next(s,c,w,h,f,fl);--o->depth;return hr;}
static void poke(void** slot,void* value){DWORD old{};assert(VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old));*slot=value;VirtualProtect(slot,sizeof(void*),old,&old);}
// A table compiled into this exe with an overlay's hook at ResizeBuffers (its other entries are copied at run time).
static void* compiled_table[44]={nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,(void*)&overlay_resize<&compiled>};
// Another class (a proxy's own swap chain class): its ResizeBuffers must never be given DXGI's.
static int proxy_calls{};
static HRESULT STDMETHODCALLTYPE proxy_resize(IDXGISwapChain*,UINT,UINT,UINT,DXGI_FORMAT,UINT){++proxy_calls;return S_OK;}
static HRESULT STDMETHODCALLTYPE proxy_other(void*){return E_NOTIMPL;}
static void* proxy_table[44];struct Proxy {void** table;};static Proxy proxy{proxy_table};
static void render(const AnyFrameV1*,AnyCanvasV1* out,void*){++renders;out->width=8;out->height=8;out->pitch=32;out->x=4;out->y=4;out->revision=0;out->pixels=pixels;}
static void gpu_fixture(const AnyFrameV1*,void*){auto api=anyapi_gpu_draw_service();assert(api->texture(1,8,8,32,pixels,1));AnyGpuCommandV1 c;c.kind=ANY_GPU_IMAGE;c.texture=1;c.rect[2]=c.rect[3]=8;c.source[2]=c.source[3]=8;c.matrix[4]=c.matrix[5]=4;assert(api->emit(&c));}
static void crash_render(const AnyFrameV1*,AnyCanvasV1*,void*){RaiseException(0xE0A10042,0,0,nullptr);}
static uint32_t crash_input(const AnyInputV1*,void*){RaiseException(0xE0A10042,0,0,nullptr);return 0;}
int main(){
 for(int i=0;i<64;++i){pixels[i*4+2]=255;pixels[i*4+3]=255;}platform::plugins[0].callbacks.id="render_fixture";platform::plugins[0].callbacks.render=render;platform::plugin_count.store(1);
 platform::plugins[0].callbacks.input=input;RAWMOUSE mouse{};mouse.lLastX=5;mouse.usButtonFlags=RI_MOUSE_LEFT_BUTTON_DOWN|RI_MOUSE_WHEEL;mouse.usButtonData=USHORT(SHORT(-120));assert(platform::raw_mouse(mouse,17,23));
 assert(input_events.size()==3&&input_events[0].kind==ANY_MOUSE_MOVE&&input_events[1].kind==ANY_MOUSE_DOWN&&input_events[1].button==1&&input_events[2].kind==ANY_MOUSE_WHEEL&&input_events[2].wheel== -120&&input_events[2].x==17&&input_events[2].y==23);
 mouse={};mouse.usButtonFlags=RI_MOUSE_LEFT_BUTTON_UP;platform::raw_mouse(mouse,18,24);assert(input_events.back().kind==ANY_MOUSE_UP);
 input_events.clear();assert(!platform::text_input(0xd83c)&&input_events.empty());assert(platform::text_input(0xdfe0)&&input_events.size()==1&&input_events[0].kind==ANY_TEXT_INPUT&&input_events[0].key==0x1f3e0);assert(platform::text_input('N')&&input_events.back().kind==ANY_TEXT_INPUT&&input_events.back().key=='N');platform::text_decoder.reset();assert(!platform::text_input(0xdfe0));
 AnyCanvasV1 bad{};assert(!platform::valid_canvas(bad));bad.pixels=pixels;bad.width=8193;bad.height=1;bad.pitch=32772;assert(!platform::valid_canvas(bad));
 // K42: a fault releases only the faulting mod's own capture and logs once.
 auto& second=platform::plugins[1];second.callbacks.id="crash_fixture";second.callbacks.input=crash_input;second.display_name=L"Crash";platform::plugin_count.store(2);
 AnyInputV1 key{};key.kind=ANY_KEY_DOWN;key.key='K';input_events.clear();platform::capture_owner.store(0);platform::input(key);
 assert(second.faulted&&second.fault_code==0xE0A10042&&platform::capture_owner.load()==0&&platform::faults_total.load()==1&&input_events.size()==1&&second.fault_tick.load()&&!strcmp(second.fault_where,"input"));
 platform::fault(1,"again",7);assert(platform::faults_total.load()==1&&second.fault_code==0xE0A10042);                                    // once per slot
 second.faulted=false;platform::capture_owner.store(1);platform::input(key);assert(second.faulted&&platform::capture_owner.load()==-1&&platform::faults_total.load()==2); // own capture released
 second.faulted=false;platform::capture_owner.store(1);anyapi_owner_fault(1,"test",3);assert(platform::capture_owner.load()==-1&&second.fault_code==3);
 platform::capture_owner.store(0);platform::fault("crash_fixture","by_id",5);assert(platform::capture_owner.load()==0&&platform::faults_total.load()==3);   // already faulted: no-op
 platform::fault(-1,"bad",1);platform::fault(9,"bad",1);platform::fault((const char*)nullptr,"bad",1);assert(platform::faults_total.load()==3&&!platform::plugins[0].faulted);
 assert(platform::fault_notice_due(GetTickCount64())&&!platform::fault_notice_due(GetTickCount64()+9000));                                // 8 s toast
 platform::capture_owner.store(-1);second.callbacks.render=crash_render;second.fault_tick=0;                                                 // stays faulted; no notice for the pixel checks
 assert(!platform::fault_notice_due(GetTickCount64())&&fault_logs==3);
 std::cout<<"PASS: plugin faults release only their own capture and log once\n";
 // OVL-5: a start failure backs off and is logged once per distinct stage/HRESULT; no queue means no attempt (the seam fails before any COM call).
 platform::chain_cache.queue_bound=true;platform::overlay_fail_stage=platform::STAGE_CHAIN;auto tries=platform::overlay_attempts;
 assert(!platform::ensure_overlay(nullptr)&&platform::overlay_attempts==tries+1&&platform::overlay_backoff.failures==1&&platform::overlay_down.load()&&overlay_down_logs==1
  &&scene_frame::board.pass_error[ANY_PASS_OVERLAY].load()==int32_t(platform::STAGE_CHAIN));
 assert(!platform::ensure_overlay(nullptr)&&platform::overlay_attempts==tries+1);                                                        // inside the 1 s backoff
 platform::overlay_backoff.next_ms=0;assert(!platform::ensure_overlay(nullptr)&&platform::overlay_attempts==tries+2&&platform::overlay_backoff.failures==2&&overlay_down_logs==1);   // same failure: not logged again
 assert(platform::overlay_backoff.next_ms>=GetTickCount64()+1500);                                                                        // 2 s after the second failure
 platform::chain_cache.queue_bound=false;platform::chain_cache.accepted_at=GetTickCount64();platform::overlay_backoff.next_ms=0;
 assert(!platform::ensure_overlay(nullptr)&&platform::overlay_attempts==tries+2&&scene_frame::board.pass_error[ANY_PASS_OVERLAY].load()==int32_t(platform::STAGE_QUEUE));
 platform::overlay_fail_stage=0;platform::overlay_backoff.reset();platform::overlay_logged_stage=0;platform::overlay_logged_hr=S_OK;scene_frame::board.pass_error[ANY_PASS_OVERLAY].store(0);
 std::cout<<"PASS: overlay start failures back off and log once\n";
 ComPtr<IDXGIFactory4> factory;assert(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));ComPtr<IDXGIAdapter> warp;assert(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))));
 ComPtr<ID3D12Device> device;assert(SUCCEEDED(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))));D3D12_COMMAND_QUEUE_DESC qdesc{};ComPtr<ID3D12CommandQueue> queue;assert(SUCCEEDED(device->CreateCommandQueue(&qdesc,IID_PPV_ARGS(&queue))));
 WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"AnyAPI.RenderTest";RegisterClassW(&wc);HWND hwnd=CreateWindowW(wc.lpszClassName,L"",WS_POPUP,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(hwnd);
 DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=64;desc.Height=64;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
 // An overlay that vtable-hooked the system swap chain table's ResizeBuffers before AnyAPI started: it stays below us.
 {HWND early=CreateWindowW(wc.lpszClassName,L"",WS_POPUP,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);ComPtr<IDXGISwapChain1> probe;assert(early&&SUCCEEDED(factory->CreateSwapChainForHwnd(queue.Get(),early,&desc,nullptr,nullptr,&probe)));
  void** table=*(void***)probe.Get();below.next=(platform::ResizeFn)table[13];poke(table+13,(void*)&overlay_resize<&below>);probe.Reset();DestroyWindow(early);}
 assert(platform::start());
 ComPtr<IDXGISwapChain1> first;assert(SUCCEEDED(factory->CreateSwapChainForHwnd(queue.Get(),hwnd,&desc,nullptr,nullptr,&first)));ComPtr<IDXGISwapChain3> swap;assert(SUCCEEDED(first.As(&swap)));
 // Hook re-entry (crash 2026-10-26, Steam overlay): overlays that hook after us keep our function as their original, in
 // the object's own copy of the table or in the system table. Every call must pass each overlay once and reach DXGI.
 {void** table=*(void***)swap.Get();const platform::ResizeFn dxgi=below.next;assert(table[13]==(void*)platform::resize&&table[8]==(void*)platform::present);
  std::vector<std::unique_ptr<void*[]>> copies;auto copy_of=[&](void** from){copies.push_back(std::make_unique<void*[]>(44));std::memcpy(copies.back().get(),from,44*sizeof(void*));return copies.back().get();};
  auto calls=[&](HRESULT expect,int b,int a,int r,int c){HRESULT hr=swap->ResizeBuffers(2,64,64,DXGI_FORMAT_UNKNOWN,0);bool ok=hr==expect&&below.calls==b&&above.calls==a&&reused.calls==r&&compiled.calls==c;
   below.calls=above.calls=reused.calls=compiled.calls=0;return ok;};
  assert(calls(S_OK,1,0,0,0));
  // The overlay gives the chain a copy of the hooked table with its hook at ResizeBuffers; AnyAPI hooks the copy too.
  void** copy=copy_of(table);above.next=(platform::ResizeFn)copy[13];copy[13]=(void*)&overlay_resize<&above>;*(void***)swap.Get()=copy;
  assert(platform::hook_chain(swap.Get())&&copy[13]==(void*)platform::resize&&calls(S_OK,1,1,0,0));
  // Plain copies (our hooks already in them) take no records.
  const size_t records=platform::hook_count;for(int i=0;i<20;++i){*(void***)swap.Get()=copy_of(table);assert(platform::hook_chain(swap.Get()));}assert(platform::hook_count==records&&calls(S_OK,1,0,0,0));
  // The copy's block is freed and reused for a new copy with another overlay hook: its record is refreshed.
  std::memcpy(copy,table,44*sizeof(void*));reused.next=(platform::ResizeFn)copy[13];copy[13]=(void*)&overlay_resize<&reused>;*(void***)swap.Get()=copy;
  assert(platform::hook_chain(swap.Get())&&copy[13]==(void*)platform::resize&&calls(S_OK,1,0,1,0));
  // The overlay swaps in its copy after AnyAPI hooked the chain (no new hook_chain).
  void** late=copy_of(table);above.next=(platform::ResizeFn)late[13];late[13]=(void*)&overlay_resize<&above>;*(void***)swap.Get()=late;assert(calls(S_OK,1,1,0,0));
  // The overlay below us re-hooks the system table over ours, keeping ours as its original.
  *(void***)swap.Get()=table;below.next=(platform::ResizeFn)table[13];poke(table+13,(void*)&overlay_resize<&below>);assert(calls(S_OK,1,0,0,0));
  {void* kept[std::size(platform::hooks)];for(size_t i=0;i<platform::hook_count;++i){kept[i]=platform::hooks[i].pristine;platform::hooks[i].pristine=nullptr;}
   assert(calls(E_FAIL,1,0,0,0));for(size_t i=0;i<platform::hook_count;++i)platform::hooks[i].pristine=kept[i];}   // no file to read: bounded, with an error
  poke(table+13,(void*)platform::resize);below.next=dxgi;assert(calls(S_OK,1,0,0,0));
  // A table compiled into a module with the overlay's hook (its file holds the hook, not DXGI's function).
  for(int k=0;k<44;++k)if(k!=13)compiled_table[k]=table[k];compiled.next=(platform::ResizeFn)platform::resize;*(void***)swap.Get()=compiled_table;
  assert(platform::hook_chain(swap.Get())&&compiled_table[13]==(void*)platform::resize&&calls(S_OK,1,0,0,1));*(void***)swap.Get()=table;
  // Another class hooked at the same slot: its overlay copy must resolve to that class's function, not DXGI's.
  for(auto& e:proxy_table)e=(void*)&proxy_other;proxy_table[13]=(void*)&proxy_resize;assert(platform::hook_slot(&proxy,13,(void*)platform::resize));
  void** proxy_copy=copy_of(proxy_table);cross.next=(platform::ResizeFn)proxy_copy[13];proxy_copy[13]=(void*)&overlay_resize<&cross>;proxy.table=proxy_copy;assert(platform::hook_slot(&proxy,13,(void*)platform::resize));
  assert(SUCCEEDED(((platform::ResizeFn)proxy_copy[13])((IDXGISwapChain*)&proxy,2,64,64,DXGI_FORMAT_UNKNOWN,0))&&proxy_calls==1&&cross.calls==1);
  assert(*(void***)swap.Get()==table&&calls(S_OK,1,0,0,0));
  std::cout<<"PASS: overlays hooking after AnyAPI (table copies, a reused copy, a late copy, a system-table re-hook, a compiled table, another class) each run once and reach DXGI\n";}
 ComPtr<ID3D12Fence> fence;assert(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);uint64_t value{};
 auto wait=[&](){assert(SUCCEEDED(queue->Signal(fence.Get(),++value)));if(fence->GetCompletedValue()<value){assert(SUCCEEDED(fence->SetEventOnCompletion(value,event)));assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);}};
 ComPtr<ID3D12Resource> presented;
 auto present_green=[&](){
  UINT index=swap->GetCurrentBackBufferIndex();ComPtr<ID3D12Resource> buffer;assert(SUCCEEDED(swap->GetBuffer(index,IID_PPV_ARGS(&buffer))));presented=buffer;
  ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;assert(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))));assert(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))));
  D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;ComPtr<ID3D12DescriptorHeap> heap;assert(SUCCEEDED(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap))));auto handle=heap->GetCPUDescriptorHandleForHeapStart();device->CreateRenderTargetView(buffer.Get(),nullptr,handle);
  D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.pResource=buffer.Get();barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;list->ResourceBarrier(1,&barrier);float green[]={0,1,0,1};list->ClearRenderTargetView(handle,green,0,nullptr);std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);list->ResourceBarrier(1,&barrier);assert(SUCCEEDED(list->Close()));ID3D12CommandList* commands[]={list.Get()};queue->ExecuteCommandLists(1,commands);wait();
  assert(SUCCEEDED(swap->Present(0,0)));wait();assert(platform::frames.load()>0);
 };
 // Read one presented pixel back as R,G,B,A (R8G8B8A8).
 auto pixel=[&](UINT x,UINT y){auto buffer=presented;ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
  assert(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))));assert(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))));
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rows{};UINT64 row_bytes{},total{};auto bd=buffer->GetDesc();device->GetCopyableFootprints(&bd,0,1,0,&footprint,&rows,&row_bytes,&total);
  D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=total;rd.Height=1;rd.DepthOrArraySize=1;rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;ComPtr<ID3D12Resource> readback;assert(SUCCEEDED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))));
  D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.pResource=buffer.Get();barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;list->ResourceBarrier(1,&barrier);D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprint;D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=buffer.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);list->ResourceBarrier(1,&barrier);assert(SUCCEEDED(list->Close()));ID3D12CommandList* commands[]={list.Get()};queue->ExecuteCommandLists(1,commands);wait();
  uint8_t* data{};D3D12_RANGE range{0,size_t(total)};assert(SUCCEEDED(readback->Map(0,&range,(void**)&data)));auto at=data+footprint.Offset+y*footprint.Footprint.RowPitch+x*4;std::array<uint8_t,4> value{at[0],at[1],at[2],at[3]};D3D12_RANGE none{0,0};readback->Unmap(0,&none);return value;
 };
 // canvas=false: the overlay drew nothing, so the canvas spot (6,6) keeps the game's green.
 auto check=[&](UINT width,UINT height,bool canvas=true){present_green();auto size=presented->GetDesc();assert(size.Width==width&&size.Height==height);auto at=pixel(6,6),background=pixel(20,20);
  if(canvas)assert(at[0]>240&&at[1]<10&&at[2]<10);else assert(at[0]<10&&at[1]>240);assert(background[0]<10&&background[1]>240&&background[2]<10);
  std::cout<<"PASS: GPU readback confirms "<<(canvas?"red DLL canvas":"untouched game pixels")<<" over green game backbuffer at "<<width<<'x'<<height<<'\n';
 };
 // DXGI fails ResizeBuffers while any back-buffer reference is alive. The test drops its own (presented); the hook must drop the overlay's
 // (wrapped buffers, D2D targets) and keep the devices (OVL-6). ROB-2: the overlay is redrawn by the next check().
 auto resize=[&](UINT width,UINT height){presented.Reset();bool up=platform::context2d.Get()!=nullptr;assert(SUCCEEDED(swap->ResizeBuffers(2,width,height,DXGI_FORMAT_UNKNOWN,0)));
  assert(platform::wrapped.empty()&&platform::targets.empty()&&(platform::context2d.Get()!=nullptr)==up);};
 check(64,64);resize(96,80);check(96,80);
 // ROB-2, OVL-5, K24: the scene queue is the game's and survives overlay resets; callbacks run while the overlay is down; retries back off.
 assert(platform::queue&&platform::queue.Get()==platform::scene_queue.Get()&&platform::scene_queue.Get()==queue.Get());
 scene_frame::GpuIdentity id{};DXGI_ADAPTER_DESC wd{};warp->GetDesc(&wd);uint64_t luid=(uint64_t(uint32_t(wd.AdapterLuid.HighPart))<<32)|wd.AdapterLuid.LowPart;
 assert(scene_frame::board.gpu.load(id)&&id.known&&id.vendor_id==0x1414&&id.luid==luid&&id.adapter_name[0]);
 platform::reset();assert(platform::queue&&platform::swap&&!platform::context2d);
 platform::overlay_fail_stage=platform::STAGE_D2D;platform::overlay_backoff.reset();auto attempts=platform::overlay_attempts;auto calls=renders;
 check(96,80,false);assert(renders==calls+1&&platform::overlay_attempts==attempts+1&&platform::overlay_down.load()&&platform::overlay_backoff.failures==1
  &&scene_frame::board.pass_error[ANY_PASS_OVERLAY].load()==int32_t(platform::STAGE_D2D));
 check(96,80,false);assert(renders==calls+2&&platform::overlay_attempts==attempts+1);                                   // inside the 1 s backoff: no retry
 platform::overlay_fail_stage=0;platform::overlay_backoff.reset();check(96,80);assert(!platform::overlay_down.load()&&platform::overlay_backoff.failures==0&&scene_frame::board.pass_error[ANY_PASS_OVERLAY].load()==0);
 auto d11=platform::device11.Get();
 std::cout<<"PASS: scene queue survives overlay resets, callbacks run while the overlay is down, retry backoff, GPU identity\n";
 platform::plugins[0].callbacks.render=nullptr;platform::current_plugin=0;auto gpu=anyapi_gpu_draw_service();assert(gpu->register_renderer(gpu_fixture,nullptr));assert(!gpu->register_renderer(gpu_fixture,nullptr));platform::current_plugin=-1;AnyGpuCommandV1 invalid;assert(!gpu->emit(&invalid));check(96,80);auto uploads=gpu_draw::renderers[0].uploads;assert(uploads==1);check(96,80);assert(gpu_draw::renderers[0].uploads==uploads);resize(128,96);check(128,96);assert(gpu_draw::renderers[0].uploads==uploads&&platform::device11.Get()==d11&&platform::overlay_backoff.failures==0);
 platform::reset();check(128,96);assert(gpu_draw::renderers[0].uploads==uploads+1);std::cout<<"PASS: GPU command-only frames, persistent texture reuse across resize and re-upload after device reset\n";gpu_draw::renderers[0].callback=nullptr;gpu_draw::renderers[0].commands.clear();
 auto before=platform::frames.load();platform::plugins[0].callbacks.render=nullptr;assert(SUCCEEDED(swap->Present(0,0)));wait();assert(platform::frames.load()>before);std::cout<<"PASS: native-only mods retain the presentation clock without a canvas\n";
 // K42: the notice is drawn after every mod command. 128x96: box x 16..112, y 6..37; (64,8) is inside it, above the text.
 second.fault_tick=GetTickCount64();present_green();auto toast=pixel(64,8);assert(toast[1]<128&&toast[0]<60);second.fault_tick=0;
 std::cout<<"PASS: fault notice drawn after mod commands\n";
 wait();platform::reset();CloseHandle(event);DestroyWindow(hwnd);
}
