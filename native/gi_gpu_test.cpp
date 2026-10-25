#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <DirectXPackedVector.h>
#include <cassert>
#include <cmath>
#include <functional>
#include <iostream>
#include <vector>
#include "effects/gi_gpu.h"
#include "tests/gpu_test_device.h"
using Microsoft::WRL::ComPtr;
using namespace DirectX::PackedVector;
// Bounce light on WARP: a grey surface facing a bright red one picks up red
// near it and nothing far away, alpha is kept, and strength 0 changes nothing.
int main(){std::cout<<std::unitbuf;
 // K39: shared WARP device with the debug layer, GPU-based validation and an InfoQueue check per case.
 gpu_test::Device test_device;assert(gpu_test::create(test_device));ComPtr<ID3D12Device> device=test_device.device;ComPtr<ID3D12CommandQueue> queue=test_device.queue;ComPtr<ID3D12Fence> fence;assert(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);UINT64 serial{};
 auto wait=[&]{assert(SUCCEEDED(queue->Signal(fence.Get(),++serial)));assert(SUCCEEDED(fence->SetEventOnCompletion(serial,event)));assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);};
 ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;assert(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))));assert(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))));assert(SUCCEEDED(list->Close()));
 const UINT W=64,H=64;
 struct Texture{ComPtr<ID3D12Resource> resource,upload,readback;D3D12_RESOURCE_DESC desc{};D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes{};};
 auto buffer=[&](ComPtr<ID3D12Resource>& out,UINT64 bytes,D3D12_HEAP_TYPE type){D3D12_HEAP_PROPERTIES hp{};hp.Type=type;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;assert(SUCCEEDED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&out))));};
 auto texture=[&](DXGI_FORMAT format,bool target){Texture t;t.desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;t.desc.Width=W;t.desc.Height=H;t.desc.DepthOrArraySize=1;t.desc.MipLevels=1;t.desc.Format=format;t.desc.SampleDesc.Count=1;t.desc.Flags=target?D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET:D3D12_RESOURCE_FLAG_NONE;D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;assert(SUCCEEDED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&t.desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&t.resource))));device->GetCopyableFootprints(&t.desc,0,1,0,&t.footprint,nullptr,nullptr,&t.bytes);buffer(t.upload,t.bytes,D3D12_HEAP_TYPE_UPLOAD);buffer(t.readback,t.bytes,D3D12_HEAP_TYPE_READBACK);return t;};
 auto copy=[&](Texture& t,bool upload){D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=t.resource.Get();a.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;b.pResource=upload?t.upload.Get():t.readback.Get();b.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;b.PlacedFootprint=t.footprint;if(upload)list->CopyTextureRegion(&a,0,0,0,&b,nullptr);else list->CopyTextureRegion(&b,0,0,0,&a,nullptr);};
 auto fill=[&](Texture& t,const std::function<void(unsigned char*,UINT,UINT)>& write){unsigned char* data{};D3D12_RANGE none{0,0};assert(SUCCEEDED(t.upload->Map(0,&none,(void**)&data)));for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x)write(data+t.footprint.Offset+y*t.footprint.Footprint.RowPitch,x,y);t.upload->Unmap(0,nullptr);copy(t,true);};
 scene_gi_gpu::Gpu gpu;const float near_plane=.1f;
 // Two surfaces 10 m away face each other across the middle of the screen:
 // the left one is lit red, the right one grey.
 // Options: pillar = a grey block 8 m away at x 28..31 between the two surfaces (normal toward the camera);
 // ocean_from = columns from here right are ocean in gbuffer_ocean (bound only when < W).
 struct Options{bool pillar{};UINT ocean_from{W};UINT ocean_to{W};};
 auto run=[&](const scene_gi_gpu::Frame& f,Options o={}){std::vector<float> out(W*H*4);
  wait();assert(SUCCEEDED(allocator->Reset()));assert(SUCCEEDED(list->Reset(allocator.Get(),nullptr)));
  auto target=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,true),depth=texture(DXGI_FORMAT_R32_FLOAT,false),normals=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,false),albedo=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,false);
  assert(gpu.ensure(device.Get(),queue.Get(),target.desc));
  auto rgba=[](unsigned char* p,UINT x,float r,float g,float b,float a){auto h=(HALF*)p+x*4;h[0]=XMConvertFloatToHalf(r);h[1]=XMConvertFloatToHalf(g);h[2]=XMConvertFloatToHalf(b);h[3]=XMConvertFloatToHalf(a);};
  auto block=[&](UINT x){return o.pillar&&x>=28&&x<32;};
  fill(target,[&](unsigned char* p,UINT x,UINT){if(x<32&&!block(x))rgba(p,x,1,0,0,.25f);else rgba(p,x,.1f,.1f,.1f,.25f);});
  fill(depth,[&](unsigned char* p,UINT x,UINT){((float*)p)[x]=near_plane/(block(x)?8.f:10.f);});
  fill(normals,[&](unsigned char* p,UINT x,UINT){if(block(x))rgba(p,x,0,0,-1,0);else rgba(p,x,x<32?1.f:-1.f,0,0,0);});
  Texture ocean;if(o.ocean_from<W){ocean=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,false);fill(ocean,[&](unsigned char* p,UINT x,UINT){float v=x>=o.ocean_from&&x<o.ocean_to?1.f:0.f;rgba(p,x,v,v,0,0);});}
  fill(albedo,[&](unsigned char* p,UINT x,UINT){rgba(p,x,.5f,.5f,.5f,0);});
  D3D12_RESOURCE_BARRIER b[4]={scene_gi_gpu::barrier(target.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_RENDER_TARGET),scene_gi_gpu::barrier(depth.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),scene_gi_gpu::barrier(normals.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON),scene_gi_gpu::barrier(albedo.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON)};list->ResourceBarrier(4,b);
  if(ocean.resource){b[0]=scene_gi_gpu::barrier(ocean.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);list->ResourceBarrier(1,b);}
  using I=scene_gi_gpu::Gpu::Input;const auto read=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,common=D3D12_RESOURCE_STATE_COMMON;
  assert(gpu.record(list.Get(),target.resource.Get(),I{depth.resource.Get(),read},I{normals.resource.Get(),common},I{albedo.resource.Get(),common},f,I{ocean.resource.Get(),common}));
  b[0]=scene_gi_gpu::barrier(target.resource.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);list->ResourceBarrier(1,b);copy(target,false);assert(SUCCEEDED(list->Close()));ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);wait();
  unsigned char* pixels{};D3D12_RANGE range{0,SIZE_T(target.bytes)};assert(SUCCEEDED(target.readback->Map(0,&range,(void**)&pixels)));
  for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x){auto h=(const HALF*)(pixels+target.footprint.Offset+y*target.footprint.Footprint.RowPitch)+x*4;for(int i=0;i<4;++i)out[(y*W+x)*4+i]=XMConvertHalfToFloat(h[i]);}
  D3D12_RANGE none{0,0};target.readback->Unmap(0,&none);gpu_test::expect_clean(test_device,"gi run");return out;};
 auto base=[&]{scene_gi_gpu::Frame f;f.metrics={1.f/W,1.f/H,float(W),float(H)};f.right={1,0,0,.5f};f.up={0,1,0,.5f};f.forward={0,0,1,5000};f.projection={0,near_plane,1,0};f.params={2.5f,1,0,120};return f;};
 unsigned cases=0;
 {auto o=run(base());float near_r=o[(32*W+34)*4],near_g=o[(32*W+34)*4+1],far_r=o[(32*W+60)*4];std::cout<<"Bounce near red="<<near_r<<" green="<<near_g<<" far red="<<far_r<<'\n';
  assert(near_r>.1f+.01f&&std::abs(near_g-.1f)<.002f&&std::abs(far_r-.1f)<.002f);for(UINT i=0;i<W*H;++i)assert(o[i*4+3]==.25f);cases+=2;}
 {auto f=base();f.params[1]=0;auto o=run(f);for(UINT x=32;x<W;++x)assert(std::abs(o[(32*W+x)*4]-.1f)<.002f);std::cout<<"Strength 0 unchanged\n";++cases;}
 // CSG-4: ocean receivers get nothing; ocean senders send nothing.
 {auto o=run(base(),{false,32});for(UINT x=32;x<W;++x)assert(std::abs(o[(32*W+x)*4]-.1f)<.002f);o=run(base(),{false,0,32});assert(std::abs(o[(32*W+34)*4]-.1f)<.002f);std::cout<<"Ocean neither receives nor sends\n";++cases;}
 // CSG-13 (revision 1): a pillar in front of the sender-receiver midpoints removes those senders (screen-space
 // heuristic: about half the red samples here); revision 0 keeps them all, and the open case is unchanged.
 {auto f=base();auto v2=run(f,{true});f.extra[0]=1;auto v3=run(f,{true}),open=run(f);float a=v2[(32*W+34)*4],b=v3[(32*W+34)*4],c=open[(32*W+34)*4];
  std::cout<<"Bounce through pillar revision 0="<<a<<" revision 1="<<b<<" open="<<c<<'\n';assert(a>.1f+.01f&&b-.1f<.75f*(a-.1f)&&c>.1f+.01f);++cases;}
 // K09: release() drops everything; the next ensure() re-creates it.
 {gpu.release();assert(!gpu.loaded());auto o=run(base());assert(o[(32*W+34)*4]>.11f);std::cout<<"Released and re-created\n";++cases;}
 auto bad=base();bad.params[0]=0;wait();assert(SUCCEEDED(allocator->Reset()));assert(SUCCEEDED(list->Reset(allocator.Get(),nullptr)));using I=scene_gi_gpu::Gpu::Input;assert(!gpu.record(list.Get(),nullptr,I{},I{},I{},bad));assert(SUCCEEDED(list->Close()));
 std::cout<<"PASS: bounce light "<<cases<<" D3D12 cases\n";CloseHandle(event);
}
