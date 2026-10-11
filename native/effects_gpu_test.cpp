#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <DirectXPackedVector.h>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
#include "effects/effects_gpu.h"
using Microsoft::WRL::ComPtr;
using namespace DirectX::PackedVector;
// Scene effects on WARP: the Game curve with no grading is an exact round trip
// through the game's tone mapping, exposure doubles the HDR value, bloom spreads
// a bright pixel, glare needs open sky, Filmic stays finite and ordered, and
// metering reads back a plausible luminance.
int main(){std::cout<<std::unitbuf;
 ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
 ComPtr<IDXGIFactory4> factory;assert(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));ComPtr<IDXGIAdapter> adapter;assert(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));ComPtr<ID3D12Device> device;assert(SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))));
 D3D12_COMMAND_QUEUE_DESC qd{};ComPtr<ID3D12CommandQueue> queue;assert(SUCCEEDED(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue))));ComPtr<ID3D12Fence> fence;assert(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);UINT64 serial{};
 auto wait=[&]{assert(SUCCEEDED(queue->Signal(fence.Get(),++serial)));assert(SUCCEEDED(fence->SetEventOnCompletion(serial,event)));assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);};
 ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;assert(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))));assert(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))));assert(SUCCEEDED(list->Close()));
 const UINT W=64,H=48;
 struct Texture{ComPtr<ID3D12Resource> resource,upload,readback;D3D12_RESOURCE_DESC desc{};D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes{};};
 auto buffer=[&](ComPtr<ID3D12Resource>& out,UINT64 bytes,D3D12_HEAP_TYPE type){D3D12_HEAP_PROPERTIES hp{};hp.Type=type;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;assert(SUCCEEDED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&out))));};
 auto texture=[&](DXGI_FORMAT format){Texture t;t.desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;t.desc.Width=W;t.desc.Height=H;t.desc.DepthOrArraySize=1;t.desc.MipLevels=1;t.desc.Format=format;t.desc.SampleDesc.Count=1;t.desc.Flags=format==DXGI_FORMAT_R16G16B16A16_FLOAT?D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET:D3D12_RESOURCE_FLAG_NONE;D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;assert(SUCCEEDED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&t.desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&t.resource))));device->GetCopyableFootprints(&t.desc,0,1,0,&t.footprint,nullptr,nullptr,&t.bytes);buffer(t.upload,t.bytes,D3D12_HEAP_TYPE_UPLOAD);buffer(t.readback,t.bytes,D3D12_HEAP_TYPE_READBACK);return t;};
 auto copy=[&](Texture& t,bool upload){D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=t.resource.Get();a.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;b.pResource=upload?t.upload.Get():t.readback.Get();b.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;b.PlacedFootprint=t.footprint;if(upload)list->CopyTextureRegion(&a,0,0,0,&b,nullptr);else list->CopyTextureRegion(&b,0,0,0,&a,nullptr);};
 scene_effects_gpu::Gpu gpu;
 // colour(x,y) -> rgb; sky(x,y) -> depth 0 (open sky) instead of 0.5.
 auto run=[&](scene_effects_gpu::Frame f,auto colour,auto sky,int frames=1){std::vector<float> out(W*H*4);
  for(int n=0;n<frames;++n){wait();assert(SUCCEEDED(allocator->Reset()));assert(SUCCEEDED(list->Reset(allocator.Get(),nullptr)));auto color=texture(DXGI_FORMAT_R16G16B16A16_FLOAT),depth=texture(DXGI_FORMAT_R32_FLOAT);assert(gpu.ensure(device.Get(),queue.Get(),color.desc));
   for(auto item:{&color,&depth}){unsigned char* data{};D3D12_RANGE none{0,0};assert(SUCCEEDED(item->upload->Map(0,&none,(void**)&data)));
    for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x){auto p=data+item->footprint.Offset+y*item->footprint.Footprint.RowPitch;if(item==&color){auto h=(HALF*)p+x*4;auto c=colour(x,y);for(int i=0;i<3;++i)h[i]=XMConvertFloatToHalf(c[i]);h[3]=XMConvertFloatToHalf(.5f);}else ((float*)p)[x]=sky(x,y)?0.f:.5f;}
    item->upload->Unmap(0,nullptr);copy(*item,true);}
   D3D12_RESOURCE_BARRIER b[2]={scene_effects_gpu::barrier(color.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_RENDER_TARGET),scene_effects_gpu::barrier(depth.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)};list->ResourceBarrier(2,b);
   assert(gpu.record(list.Get(),color.resource.Get(),depth.resource.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,f,n==0));
   b[0]=scene_effects_gpu::barrier(color.resource.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);list->ResourceBarrier(1,b);copy(color,false);assert(SUCCEEDED(list->Close()));ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);wait();
   unsigned char* pixels{};D3D12_RANGE range{0,SIZE_T(color.bytes)};assert(SUCCEEDED(color.readback->Map(0,&range,(void**)&pixels)));
   for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x){auto h=(const HALF*)(pixels+color.footprint.Offset+y*color.footprint.Footprint.RowPitch)+x*4;for(int i=0;i<4;++i)out[(y*W+x)*4+i]=XMConvertHalfToFloat(h[i]);}
   D3D12_RANGE none{0,0};color.readback->Unmap(0,&none);}
  return out;};
 auto base=[]{scene_effects_gpu::Frame f;f.metrics={1.f/W,1.f/H,float(W),float(H)};f.grading={0,0,1,0};f.exposure={0,0,0,1.5f};f.sun_colour={1,.9f,.8f,0};return f;};
 auto ramp=[](UINT x,UINT){float v=.01f*std::pow(1.08f,float(x));return std::array<float,3>{v,v*.8f,v*.6f};};auto ground=[](UINT,UINT){return false;};
 unsigned cases=0;
 {auto out=run(base(),ramp,ground);for(UINT x=0;x<W;++x){auto in=ramp(x,0);for(int c=0;c<3;++c){float o=out[(20*W+x)*4+c];assert(std::abs(o-in[c])<=.004f+.01f*in[c]);}assert(out[(20*W+x)*4+3]==.5f);}std::cout<<"Game curve round trip exact\n";++cases;}
 {auto f=base();f.grading[3]=1;auto out=run(f,ramp,ground);for(UINT x=0;x<20;++x){float in=ramp(x,0)[0],o=out[(20*W+x)*4];assert(std::abs(o-2*in)<=.004f+.02f*in);}std::cout<<"One stop doubles HDR input\n";++cases;}
 for(float tonemap:{1.f,2.f}){auto f=base();f.grading={tonemap,5,1,0};auto out=run(f,ramp,ground);float previous=-1;for(UINT x=0;x<W;++x){float y=out[(20*W+x)*4+1];assert(std::isfinite(y)&&y>=0);assert(y>=previous-5e-3f);previous=y;assert(out[(20*W+x)*4+3]==.5f);}std::cout<<"Curve "<<tonemap<<" finite and ordered, last="<<previous<<'\n';++cases;}
 {auto f=base();f.extras[0]=1;auto spot=[](UINT x,UINT y){float v=x==32&&y==24?50.f:0.f;return std::array<float,3>{v,v,v};};auto plain=run(base(),spot,ground),bloomed=run(f,spot,ground);
  float near_plain=plain[(24*W+40)*4],near_bloom=bloomed[(24*W+40)*4];std::cout<<"Bloom near="<<near_bloom<<" without="<<near_plain<<'\n';// Black does not round-trip to exactly 0 (the game crushes the darkest values), so compare.
  assert(near_bloom>near_plain+1e-3f);++cases;}
 {auto f=base();f.sun_screen={.5f,.5f,1,.5f};f.sun_colour[3]=1;auto dim=[](UINT,UINT){return std::array<float,3>{.02f,.02f,.02f};};auto sky=[](UINT,UINT){return true;};
  auto open=run(f,dim,sky,2),blocked=run(f,dim,ground,2);float centre_open=open[(24*W+32)*4],centre_blocked=blocked[(24*W+32)*4],corner=open[(2*W+2)*4];std::cout<<"Glare open="<<centre_open<<" blocked="<<centre_blocked<<" corner="<<corner<<'\n';assert(centre_open>centre_blocked*1.5f&&centre_open>corner);++cases;}
 {auto f=base();f.exposure={1,.5f,.05f,1.5f};auto grey=[](UINT,UINT){return std::array<float,3>{.25f,.25f,.25f};};run(f,grey,ground,10);auto m=gpu.measured();std::cout<<"Metered fast="<<m.fast<<" slow="<<m.slow<<'\n';assert(m.valid&&std::abs(m.fast-std::log2(.25f))<.1f&&std::abs(m.slow-m.fast)<.2f);++cases;}
 auto bad=base();bad.grading[0]=7;wait();assert(SUCCEEDED(allocator->Reset()));assert(SUCCEEDED(list->Reset(allocator.Get(),nullptr)));assert(!gpu.record(list.Get(),nullptr,nullptr,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,bad,true));assert(SUCCEEDED(list->Close()));
 std::cout<<"PASS: scene effects "<<cases<<" D3D12 cases\n";CloseHandle(event);
}
