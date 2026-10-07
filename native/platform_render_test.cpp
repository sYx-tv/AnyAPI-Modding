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
static const AnyWorldTimeV1* anyapi_world_time_service(){return nullptr;}
static const AnyBuildV1* anyapi_build_service(){return nullptr;}
#include "anyapi_client_tasks.inc"
static const AnyMenuV1* anyapi_menu_service(){return nullptr;}
static const AnyMenuV2* anyapi_menu_service_v2(){return nullptr;}
static const AnyMenuV3* anyapi_menu_service_v3(){return nullptr;}
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
static void render(const AnyFrameV1*,AnyCanvasV1* out,void*){out->width=8;out->height=8;out->pitch=32;out->x=4;out->y=4;out->revision=0;out->pixels=pixels;}
static void gpu_fixture(const AnyFrameV1*,void*){auto api=anyapi_gpu_draw_service();assert(api->texture(1,8,8,32,pixels,1));AnyGpuCommandV1 c;c.kind=ANY_GPU_IMAGE;c.texture=1;c.rect[2]=c.rect[3]=8;c.source[2]=c.source[3]=8;c.matrix[4]=c.matrix[5]=4;assert(api->emit(&c));}
int main(){
 for(int i=0;i<64;++i){pixels[i*4+2]=255;pixels[i*4+3]=255;}platform::plugins[0].callbacks.id="render_fixture";platform::plugins[0].callbacks.render=render;platform::plugin_count.store(1);
 platform::plugins[0].callbacks.input=input;RAWMOUSE mouse{};mouse.lLastX=5;mouse.usButtonFlags=RI_MOUSE_LEFT_BUTTON_DOWN|RI_MOUSE_WHEEL;mouse.usButtonData=USHORT(SHORT(-120));assert(platform::raw_mouse(mouse,17,23));
 assert(input_events.size()==3&&input_events[0].kind==ANY_MOUSE_MOVE&&input_events[1].kind==ANY_MOUSE_DOWN&&input_events[1].button==1&&input_events[2].kind==ANY_MOUSE_WHEEL&&input_events[2].wheel== -120&&input_events[2].x==17&&input_events[2].y==23);
 mouse={};mouse.usButtonFlags=RI_MOUSE_LEFT_BUTTON_UP;platform::raw_mouse(mouse,18,24);assert(input_events.back().kind==ANY_MOUSE_UP);
 input_events.clear();assert(!platform::text_input(0xd83c)&&input_events.empty());assert(platform::text_input(0xdfe0)&&input_events.size()==1&&input_events[0].kind==ANY_TEXT_INPUT&&input_events[0].key==0x1f3e0);assert(platform::text_input('N')&&input_events.back().kind==ANY_TEXT_INPUT&&input_events.back().key=='N');platform::text_decoder.reset();assert(!platform::text_input(0xdfe0));
 AnyCanvasV1 bad{};assert(!platform::valid_canvas(bad));bad.pixels=pixels;bad.width=8193;bad.height=1;bad.pitch=32772;assert(!platform::valid_canvas(bad));
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
  uint8_t* data{};D3D12_RANGE range{0,size_t(total)};assert(SUCCEEDED(readback->Map(0,&range,(void**)&data)));auto at=data+footprint.Offset+6*footprint.Footprint.RowPitch+6*4;assert(at[0]>240&&at[1]<10&&at[2]<10);auto background=data+footprint.Offset+20*footprint.Footprint.RowPitch+20*4;assert(background[0]<10&&background[1]>240&&background[2]<10);D3D12_RANGE none{0,0};readback->Unmap(0,&none);
  std::cout<<"PASS: GPU readback confirms red DLL canvas over green game backbuffer at "<<width<<'x'<<height<<'\n';
 };
 check(64,64);assert(SUCCEEDED(swap->ResizeBuffers(2,96,80,DXGI_FORMAT_UNKNOWN,0)));check(96,80);
 platform::plugins[0].callbacks.render=nullptr;platform::current_plugin=0;auto gpu=anyapi_gpu_draw_service();assert(gpu->register_renderer(gpu_fixture,nullptr));assert(!gpu->register_renderer(gpu_fixture,nullptr));platform::current_plugin=-1;AnyGpuCommandV1 invalid;assert(!gpu->emit(&invalid));check(96,80);auto uploads=gpu_draw::renderers[0].uploads;assert(uploads==1);check(96,80);assert(gpu_draw::renderers[0].uploads==uploads);assert(SUCCEEDED(swap->ResizeBuffers(2,128,96,DXGI_FORMAT_UNKNOWN,0)));check(128,96);assert(gpu_draw::renderers[0].uploads==uploads+1);std::cout<<"PASS: GPU command-only frames, persistent texture reuse and re-upload after device reset\n";gpu_draw::renderers[0].callback=nullptr;gpu_draw::renderers[0].commands.clear();
 auto before=platform::frames.load();platform::plugins[0].callbacks.render=nullptr;assert(SUCCEEDED(swap->Present(0,0)));wait();assert(platform::frames.load()>before);std::cout<<"PASS: native-only mods retain the presentation clock without a canvas\n";
 wait();platform::reset();CloseHandle(event);DestroyWindow(hwnd);
}
