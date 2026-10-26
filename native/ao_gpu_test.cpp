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
#include "effects/ao_gpu.h"
#include "tests/gpu_test_device.h"
using Microsoft::WRL::ComPtr;
using namespace DirectX::PackedVector;
// Ambient occlusion on WARP: an open wall stays unoccluded, the inside of a
// step darkens, sky and clouds are never occluded, strength 0 changes nothing,
// and a bad frame is rejected.
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
 scene_ao_gpu::Gpu gpu;const float near_plane=.1f;
 // Camera looks along +z with reversed depth = near / z. distance(x,y): view
 // distance, 0 for sky; cloud(x,y) marks cloud pixels. Returns visibility.
 // normal(x,y): world normal; default faces the camera.
 auto run=[&](scene_ao_gpu::Frame f,auto distance,auto cloud,std::function<std::array<float,3>(UINT,UINT)> normal={}){std::vector<float> out(W*H);
  wait();assert(SUCCEEDED(allocator->Reset()));assert(SUCCEEDED(list->Reset(allocator.Get(),nullptr)));
  auto target=texture(DXGI_FORMAT_R8_UNORM,true),depth=texture(DXGI_FORMAT_R32_FLOAT,false),normals=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,false),albedo=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,false);
  assert(gpu.ensure(device.Get(),queue.Get(),target.desc));
  fill(target,[](unsigned char* p,UINT x,UINT){p[x]=128;});
  fill(depth,[&](unsigned char* p,UINT x,UINT y){float z=distance(x,y);((float*)p)[x]=z>0?near_plane/z:0.f;});
  fill(normals,[&](unsigned char* p,UINT x,UINT y){auto h=(HALF*)p+x*4;std::array<float,3> n{0,0,-1};if(normal)n=normal(x,y);for(int i=0;i<3;++i)h[i]=XMConvertFloatToHalf(n[i]);h[3]=XMConvertFloatToHalf(cloud(x,y)?1.f:0.f);});
  fill(albedo,[](unsigned char* p,UINT x,UINT){auto h=(HALF*)p+x*4;for(int i=0;i<3;++i)h[i]=XMConvertFloatToHalf(.4f);h[3]=XMConvertFloatToHalf(0);});
  // Game-like states: the SSAO target was just rendered, the G-buffer is readable.
  D3D12_RESOURCE_BARRIER b[4]={scene_ao_gpu::barrier(target.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_RENDER_TARGET),scene_ao_gpu::barrier(depth.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),scene_ao_gpu::barrier(normals.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),scene_ao_gpu::barrier(albedo.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON)};list->ResourceBarrier(4,b);
  using I=scene_ao_gpu::Gpu::Input;const auto read=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  assert(gpu.record(list.Get(),I{target.resource.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET},I{depth.resource.Get(),read},I{normals.resource.Get(),read},I{albedo.resource.Get(),D3D12_RESOURCE_STATE_COMMON},f));
  b[0]=scene_ao_gpu::barrier(target.resource.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);list->ResourceBarrier(1,b);copy(target,false);assert(SUCCEEDED(list->Close()));ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);wait();
  unsigned char* pixels{};D3D12_RANGE range{0,SIZE_T(target.bytes)};assert(SUCCEEDED(target.readback->Map(0,&range,(void**)&pixels)));
  for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x)out[y*W+x]=pixels[target.footprint.Offset+y*target.footprint.Footprint.RowPitch+x]/255.f;
  D3D12_RANGE none{0,0};target.readback->Unmap(0,&none);gpu_test::expect_clean(test_device,"ao run");return out;};
 auto base=[&]{scene_ao_gpu::Frame f;f.metrics={1.f/W,1.f/H,float(W),float(H)};f.right={1,0,0,.5f};f.up={0,1,0,.5f};f.forward={0,0,1,5000};f.projection={0,near_plane,1,0};f.params={1.5f,1,0,0};return f;};
 auto wall=[](UINT,UINT){return 10.f;};auto step=[](UINT x,UINT){return x<32?10.f:9.f;};auto none=[](UINT,UINT){return false;};
 unsigned cases=0;
 {auto out=run(base(),wall,none);float low=1;for(UINT y=8;y<56;++y)for(UINT x=8;x<56;++x)low=std::min(low,out[y*W+x]);std::cout<<"Open wall lowest="<<low<<'\n';assert(low>.97f);++cases;}
 {auto out=run(base(),step,none);float inside=out[32*W+30],open=out[32*W+6],raised=out[32*W+40];std::cout<<"Step inside="<<inside<<" open="<<open<<" raised="<<raised<<'\n';assert(inside<open-.05f&&open>.95f&&raised>.95f);++cases;}
 {auto f=base();f.params[1]=0;auto out=run(f,step,none);float low=1;for(auto v:out)low=std::min(low,v);std::cout<<"Strength 0 lowest="<<low<<'\n';assert(low>.99f);++cases;}
 {auto sky=[](UINT x,UINT){return x<32?0.f:9.f;};auto cloud=[](UINT x,UINT){return x>=48;};auto out=run(base(),sky,cloud);std::cout<<"Sky="<<out[32*W+16]<<" cloud="<<out[32*W+56]<<'\n';assert(out[32*W+16]>.99f&&out[32*W+56]>.99f);++cases;}
 // CSG-9: the 4x4 interleave matches the 4x4 blur, so the rotation pattern cancels and the step's shaded columns (28-31)
 // show no grain down them. Only rows whose 9.6 px footprint and blur taps stay on screen count (12-51): off-screen samples
 // fade to open by design. Perspective shifts the raised half's samples by tan*ndc_y per metre of step, a smooth drift of
 // up to 2.6 LSB over those rows (0.3 LSB a row) in 0.40.1's math too, so: adjacent rows within 1 LSB, and the middle
 // 8 rows (two pattern periods, drift < 0.4 LSB) within 1 LSB. 0.40.1's hash rotation fails both (3-5 and 4-8 LSB).
 for(float revision:{0.f,1.f}){auto f=base();f.params[3]=revision;auto out=run(f,step,none);float step_max=0,spread=0;
  for(UINT x=28;x<32;++x){float lo=1,hi=0;for(UINT y=12;y<52;++y){auto v=out[y*W+x];if(y>12)step_max=std::max(step_max,std::abs(v-out[(y-1)*W+x]));if(y>=28&&y<36){lo=std::min(lo,v);hi=std::max(hi,v);}}spread=std::max(spread,hi-lo);}
  std::cout<<"Step columns revision "<<revision<<" row step="<<step_max*255<<" middle spread="<<spread*255<<" LSB\n";assert(step_max<=1.5f/255&&spread<=1.5f/255);++cases;}
 // Revision 1: a floor (1.5 m below the camera) meeting a wall 10 m away darkens on both sides of the crease (CSG-1/CSG-8),
 // and a slab 2 m in front of the wall leaves no halo 4 px outside its edge (CSG-8 thickness).
 {auto corner_z=[](UINT,UINT y){float ndc=1-2*(y+.5f)/H;float floor_z=ndc<0?-1.5f/(ndc*.5f):1e9f;return std::min(10.f,floor_z);};
  auto corner_n=[&](UINT x,UINT y){return corner_z(x,y)<9.999f?std::array<float,3>{0,1,0}:std::array<float,3>{0,0,-1};};
  auto f=base();f.params[3]=1;auto out=run(f,corner_z,none,corner_n);float crease=1;for(UINT y=38;y<46;++y)crease=std::min(crease,out[y*W+32]);float open=out[10*W+32];
  std::cout<<"Corner crease="<<crease<<" open wall="<<open<<'\n';assert(open>.95f&&crease<open-.05f);
  auto slab=[](UINT x,UINT){return x>=24&&x<40?8.f:10.f;};auto v2=run(base(),slab,none),v3=run(f,slab,none);
  std::cout<<"Slab halo revision 0="<<v2[32*W+20]<<" revision 1="<<v3[32*W+20]<<'\n';assert(v3[32*W+20]>.9f&&v3[32*W+44]>.9f);cases+=2;}
 // K09: release() drops everything; the next ensure() re-creates it.
 {gpu.release();assert(!gpu.loaded());auto out=run(base(),wall,none);assert(out[32*W+32]>.97f);std::cout<<"Released and re-created\n";++cases;}
 auto bad=base();bad.params[0]=0;wait();assert(SUCCEEDED(allocator->Reset()));assert(SUCCEEDED(list->Reset(allocator.Get(),nullptr)));using I=scene_ao_gpu::Gpu::Input;assert(!gpu.record(list.Get(),I{},I{},I{},I{},bad));assert(SUCCEEDED(list->Close()));
 std::cout<<"PASS: ambient occlusion "<<cases<<" D3D12 cases\n";CloseHandle(event);
}
