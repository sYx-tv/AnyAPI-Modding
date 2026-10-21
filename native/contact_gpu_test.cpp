#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <DirectXPackedVector.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "effects/contact_gpu.h"
using Microsoft::WRL::ComPtr;
using namespace DirectX::PackedVector;
// Contact shadows on WARP: a wall beside a slab darkens where the slab blocks
// the sun, open wall and the slab itself keep their light, the result never
// drops below ambient light, alpha is kept, and strength 0 changes nothing.
// Flat ground seen at a grazing angle does not shadow itself, ocean pixels and
// pixels the game already shadows are left alone.
// WARP can drop the device when the machine is busy (seen right after the other
// GPU tests). That is retried once on a fresh device; pixel checks never are.
struct DeviceLost:std::runtime_error{using std::runtime_error::runtime_error;};
static void body(){
 ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
 ComPtr<IDXGIFactory4> factory;assert(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));ComPtr<IDXGIAdapter> adapter;assert(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));ComPtr<ID3D12Device> device;assert(SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))));
 D3D12_COMMAND_QUEUE_DESC qd{};ComPtr<ID3D12CommandQueue> queue;assert(SUCCEEDED(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue))));ComPtr<ID3D12Fence> fence;assert(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);UINT64 serial{};
 auto lost=[&](const char* where,HRESULT hr){char text[160];snprintf(text,sizeof(text),"%s failed hr=0x%08lx removed=0x%08lx",where,(unsigned long)hr,(unsigned long)device->GetDeviceRemovedReason());throw DeviceLost(text);};
 auto wait=[&]{if(HRESULT hr=queue->Signal(fence.Get(),++serial);FAILED(hr))lost("Signal",hr);assert(SUCCEEDED(fence->SetEventOnCompletion(serial,event)));assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);if(HRESULT hr=device->GetDeviceRemovedReason();FAILED(hr))lost("Wait",hr);};
 ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;assert(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))));assert(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))));assert(SUCCEEDED(list->Close()));
 const UINT W=64,H=64;
 struct Texture{ComPtr<ID3D12Resource> resource,upload,readback;D3D12_RESOURCE_DESC desc{};D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes{};};
 auto buffer=[&](ComPtr<ID3D12Resource>& out,UINT64 bytes,D3D12_HEAP_TYPE type){D3D12_HEAP_PROPERTIES hp{};hp.Type=type;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;assert(SUCCEEDED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&out))));};
 auto texture=[&](DXGI_FORMAT format,bool target){Texture t;t.desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;t.desc.Width=W;t.desc.Height=H;t.desc.DepthOrArraySize=1;t.desc.MipLevels=1;t.desc.Format=format;t.desc.SampleDesc.Count=1;t.desc.Flags=target?D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET:D3D12_RESOURCE_FLAG_NONE;D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;assert(SUCCEEDED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&t.desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&t.resource))));device->GetCopyableFootprints(&t.desc,0,1,0,&t.footprint,nullptr,nullptr,&t.bytes);buffer(t.upload,t.bytes,D3D12_HEAP_TYPE_UPLOAD);buffer(t.readback,t.bytes,D3D12_HEAP_TYPE_READBACK);return t;};
 auto copy=[&](Texture& t,bool upload){D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=t.resource.Get();a.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;b.pResource=upload?t.upload.Get():t.readback.Get();b.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;b.PlacedFootprint=t.footprint;if(upload)list->CopyTextureRegion(&a,0,0,0,&b,nullptr);else list->CopyTextureRegion(&b,0,0,0,&a,nullptr);};
 auto fill=[&](Texture& t,const std::function<void(unsigned char*,UINT,UINT)>& write){unsigned char* data{};D3D12_RANGE none{0,0};assert(SUCCEEDED(t.upload->Map(0,&none,(void**)&data)));for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x)write(data+t.footprint.Offset+y*t.footprint.Footprint.RowPitch,x,y);t.upload->Unmap(0,nullptr);copy(t,true);};
 scene_contact_gpu::Gpu gpu;const float near_plane=.1f,lit_colour=.5f*(.70710678f+.2f*.5f)*.4f,floor_colour=.5f*.2f*.5f*.4f;
 // Camera looks along +z at a wall 10 m away; the left half has a slab 0.5 m in front of it.
 // ground: the camera is instead 1.7 m above flat ground, sky above the horizon.
 // ocean: columns from here on are ocean. scene: the game's colour for every pixel (default lit).
 auto run=[&](const scene_contact_gpu::Frame& f,float rim=0,bool ground=false,UINT ocean_from=W,float scene=-1){std::vector<float> out(W*H*4);if(scene<0)scene=lit_colour;
  wait();assert(SUCCEEDED(allocator->Reset()));assert(SUCCEEDED(list->Reset(allocator.Get(),nullptr)));
  auto target=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,true),depth=texture(DXGI_FORMAT_R32_FLOAT,false),normals=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,false),albedo=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,false),ocean=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,false);
  assert(gpu.ensure(device.Get(),queue.Get(),target.desc));
  auto rgba=[](unsigned char* p,UINT x,float r,float g,float b,float a){auto h=(HALF*)p+x*4;h[0]=XMConvertFloatToHalf(r);h[1]=XMConvertFloatToHalf(g);h[2]=XMConvertFloatToHalf(b);h[3]=XMConvertFloatToHalf(a);};
  fill(target,[&](unsigned char* p,UINT x,UINT){rgba(p,x,scene,scene,scene,.25f);});
  fill(depth,[&](unsigned char* p,UINT x,UINT y){float below=((y+.5f)/H*2-1)*.5f;((float*)p)[x]=!ground?near_plane/(x<32?9.5f:10.f):below>0?near_plane*below/1.7f:0;});
  fill(normals,[&](unsigned char* p,UINT x,UINT){if(ground)rgba(p,x,0,1,0,0);else rgba(p,x,0,0,-1,0);});
  fill(ocean,[&](unsigned char* p,UINT x,UINT){rgba(p,x,x>=ocean_from?1.f:0.f,x>=ocean_from?10.f:0.f,0,0);});
  fill(albedo,[&](unsigned char* p,UINT x,UINT){rgba(p,x,.5f,.5f,.5f,rim);});
  D3D12_RESOURCE_BARRIER b[5]={scene_contact_gpu::barrier(target.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_RENDER_TARGET),scene_contact_gpu::barrier(depth.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),scene_contact_gpu::barrier(normals.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON),scene_contact_gpu::barrier(albedo.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON),scene_contact_gpu::barrier(ocean.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON)};list->ResourceBarrier(5,b);
  using I=scene_contact_gpu::Gpu::Input;const auto read=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,common=D3D12_RESOURCE_STATE_COMMON;
  assert(gpu.record(list.Get(),target.resource.Get(),I{depth.resource.Get(),read},I{normals.resource.Get(),common},I{albedo.resource.Get(),common},I{ocean.resource.Get(),common},f));
  b[0]=scene_contact_gpu::barrier(target.resource.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);list->ResourceBarrier(1,b);copy(target,false);assert(SUCCEEDED(list->Close()));ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);wait();
  unsigned char* pixels{};D3D12_RANGE range{0,SIZE_T(target.bytes)};if(HRESULT hr=target.readback->Map(0,&range,(void**)&pixels);FAILED(hr))lost("Map",hr);
  for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x){auto h=(const HALF*)(pixels+target.footprint.Offset+y*target.footprint.Footprint.RowPitch)+x*4;for(int i=0;i<4;++i)out[(y*W+x)*4+i]=XMConvertHalfToFloat(h[i]);}
  D3D12_RANGE none{0,0};target.readback->Unmap(0,&none);return out;};
 // The sun is behind the camera and to the left, so the slab shades the wall just right of it.
 auto base=[&]{scene_contact_gpu::Frame f;f.metrics={1.f/W,1.f/H,float(W),float(H)};f.right={1,0,0,.5f};f.up={0,1,0,.5f};f.forward={0,0,1,5000};f.projection={0,near_plane,1,0};f.sun={-.70710678f,0,-.70710678f,1};f.sun_colour={1,1,1,0};f.sky_colour={.2f,.2f,.2f,0};f.back_colour={0,0,0,0};f.params={.6f,.3f,0,60};return f;};
 unsigned cases=0;
 {auto o=run(base());float beside=1;for(UINT x=33;x<36;++x)beside=std::min(beside,o[(32*W+x)*4]);float open=o[(32*W+60)*4],slab=o[(32*W+16)*4];
  std::cout<<"Contact beside="<<beside<<" open="<<open<<" slab="<<slab<<" floor="<<floor_colour<<'\n';assert(beside<lit_colour*.5f&&beside>=floor_colour-.003f&&std::abs(open-lit_colour)<.002f&&std::abs(slab-lit_colour)<.002f);
  for(UINT i=0;i<W*H;++i)assert(o[i*4+3]==.25f);cases+=2;}
 // Sun behind the wall: the game already shadows it, so even with rim light nothing is darkened.
 {auto f=base();f.sun={.70710678f,0,.70710678f,1};auto o=run(f,.5f);for(UINT i=0;i<W*H;++i)assert(std::abs(o[i*4]-lit_colour)<.002f);std::cout<<"Facing away unchanged\n";++cases;}
 // Sun high ahead: every ground pixel is lit, and point-sampled neighbours on
 // the same ground must not read as occluders however far away they are.
 {auto f=base();f.sun={0,.70710678f,.70710678f,1};auto o=run(f,0,true);for(UINT i=0;i<W*H;++i)assert(std::abs(o[i*4]-lit_colour)<.002f);std::cout<<"Grazing ground unchanged\n";++cases;}
 // The ocean is drawn over the wall without depth: water keeps its colour, the rest still shadows.
 {auto o=run(base(),0,false,33);for(UINT y=0;y<H;++y)for(UINT x=33;x<W;++x)assert(std::abs(o[(y*W+x)*4]-lit_colour)<.002f);assert(o[(32*W+32)*4]<lit_colour*.75f);std::cout<<"Ocean unchanged\n";++cases;}
 // The game already shadows the wall (ambient raised by cloud light): no second shadow.
 {auto o=run(base(),0,false,W,floor_colour*1.8f);for(UINT i=0;i<W*H;++i)assert(std::abs(o[i*4]-floor_colour*1.8f)<.002f);std::cout<<"Native shadow not darkened again\n";++cases;}
 {auto f=base();f.sun[3]=0;auto o=run(f);for(UINT i=0;i<W*H;++i)assert(std::abs(o[i*4]-lit_colour)<.002f);std::cout<<"Strength 0 unchanged\n";++cases;}
 auto bad=base();bad.params[0]=0;wait();assert(SUCCEEDED(allocator->Reset()));assert(SUCCEEDED(list->Reset(allocator.Get(),nullptr)));using I=scene_contact_gpu::Gpu::Input;assert(!gpu.record(list.Get(),nullptr,I{},I{},I{},I{},bad));assert(SUCCEEDED(list->Close()));
 std::cout<<"PASS: contact shadows "<<cases<<" D3D12 cases\n";CloseHandle(event);
}
// D3D12 hands back the same removed WARP device inside one process, so the
// retry runs in a fresh copy of this test.
static int rerun(){wchar_t path[MAX_PATH];GetModuleFileNameW(nullptr,path,MAX_PATH);std::wstring command=L"\""+std::wstring(path)+L"\" --retry";STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};if(!CreateProcessW(path,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&pi))return 1;WaitForSingleObject(pi.hProcess,INFINITE);DWORD code=1;GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return int(code);}
int main(int argc,char**argv){std::cout<<std::unitbuf;bool retry=argc>1&&std::string(argv[1])=="--retry";try{body();}catch(const DeviceLost& lost){if(retry){std::cout<<"FAIL: device lost again ("<<lost.what()<<")"<<std::endl;return 1;}std::cout<<"Device lost ("<<lost.what()<<"), retrying once in a new process"<<std::endl;return rerun();}return 0;}
