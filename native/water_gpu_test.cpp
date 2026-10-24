#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <DirectXPackedVector.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>
#include <iostream>
#include <vector>
#include "effects/water_gpu.h"
using Microsoft::WRL::ComPtr;
using namespace DirectX::PackedVector;
// Ocean reflections on WARP: a camera 5 m above a calm sea looks at a red wall
// standing in the water 40 m away. Pixels that are not ocean stay bit-identical
// (colour and alpha), the wall's reflection appears in the water below it,
// water whose rays find only sky is unchanged, the glint shows only with the
// sun up, and with everything off nothing changes.
int main(){std::cout<<std::unitbuf;
 ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
 ComPtr<IDXGIFactory4> factory;assert(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));ComPtr<IDXGIAdapter> adapter;assert(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));ComPtr<ID3D12Device> device;assert(SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))));
 D3D12_COMMAND_QUEUE_DESC qd{};ComPtr<ID3D12CommandQueue> queue;assert(SUCCEEDED(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue))));ComPtr<ID3D12Fence> fence;assert(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);UINT64 serial{};
 auto wait=[&]{assert(SUCCEEDED(queue->Signal(fence.Get(),++serial)));assert(SUCCEEDED(fence->SetEventOnCompletion(serial,event)));assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);};
 ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;assert(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))));assert(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))));assert(SUCCEEDED(list->Close()));
 const UINT W=96,H=96;
 struct Texture{ComPtr<ID3D12Resource> resource,upload,readback;D3D12_RESOURCE_DESC desc{};D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes{};};
 auto buffer=[&](ComPtr<ID3D12Resource>& out,UINT64 bytes,D3D12_HEAP_TYPE type){D3D12_HEAP_PROPERTIES hp{};hp.Type=type;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;assert(SUCCEEDED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&out))));};
 auto texture=[&](DXGI_FORMAT format,bool target){Texture t;t.desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;t.desc.Width=W;t.desc.Height=H;t.desc.DepthOrArraySize=1;t.desc.MipLevels=1;t.desc.Format=format;t.desc.SampleDesc.Count=1;t.desc.Flags=target?D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET:D3D12_RESOURCE_FLAG_NONE;D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;assert(SUCCEEDED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&t.desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&t.resource))));device->GetCopyableFootprints(&t.desc,0,1,0,&t.footprint,nullptr,nullptr,&t.bytes);buffer(t.upload,t.bytes,D3D12_HEAP_TYPE_UPLOAD);buffer(t.readback,t.bytes,D3D12_HEAP_TYPE_READBACK);return t;};
 auto copy=[&](Texture& t,bool upload){D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=t.resource.Get();a.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;b.pResource=upload?t.upload.Get():t.readback.Get();b.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;b.PlacedFootprint=t.footprint;if(upload)list->CopyTextureRegion(&a,0,0,0,&b,nullptr);else list->CopyTextureRegion(&b,0,0,0,&a,nullptr);};
 auto fill=[&](Texture& t,const std::function<void(unsigned char*,UINT,UINT)>& write){unsigned char* data{};D3D12_RANGE none{0,0};assert(SUCCEEDED(t.upload->Map(0,&none,(void**)&data)));for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x)write(data+t.footprint.Offset+y*t.footprint.Footprint.RowPitch,x,y);t.upload->Unmap(0,nullptr);copy(t,true);};
 // Camera 5 m above sea level, pitched down 0.2 rad, looking along +z. Reversed, infinite depth: depth = 0.1 / z.
 const float pitch=.2f,c=std::cos(pitch),s=std::sin(pitch);const float right[4]{1,0,0,.8f},up[4]{0,c,s,.45f},forward[4]{0,-s,c,0},projection[4]{0,.1f,1,0};
 const double camera[3]{100,5,-40};const double calm[2]{0,0};
 const float sky[4][3]{{.35f,.5f,.8f},{.6f,.7f,.8f},{.25f,.28f,.3f},{.12f,.12f,.12f}},sun_colour[3]{3,2.8f,2.5f},sea_colour[3]{.04f,.08f,.12f},wall_colour[3]{4,.2f,.2f},sky_colour[3]{.5f,.6f,.8f};
 // The wall stands in the water from 20 m below to 12 m above sea level, 40 m ahead, 20 m wide.
 enum Kind {Sky,Wall,Sea};struct Pixel{Kind kind;float depth,colour[3];};
 auto classify=[&](UINT x,UINT y){float nx=(x+.5f)/W*2-1,ny=1-(y+.5f)/H*2;float r[3]={right[0]*nx*right[3]+up[0]*ny*up[3]+forward[0],right[1]*nx*right[3]+up[1]*ny*up[3]+forward[1],right[2]*nx*right[3]+up[2]*ny*up[3]+forward[2]};
  // The ray has a forward component of 1, so its parameter is view z.
  float sea=r[1]<0?5/-r[1]:1e30f,wall=r[2]>0?40/r[2]:1e30f;Pixel p{Sky,0,{sky_colour[0],sky_colour[1],sky_colour[2]}};
  bool on_wall=wall<1e29f&&std::abs(r[0]*wall)<=10&&5+r[1]*wall<=12&&5+r[1]*wall>=-20;
  if(on_wall&&wall<sea){p.kind=Wall;p.depth=.1f/wall;for(int i=0;i<3;++i)p.colour[i]=wall_colour[i];}
  else if(r[1]<0){p.kind=Sea;float below=on_wall?wall:(25/-r[1]);p.depth=.1f/below;for(int i=0;i<3;++i)p.colour[i]=sea_colour[i];}
  return p;};
 scene_water_gpu::Gpu gpu;
 // Returns RGBA after the pass; the target's alpha starts at 0.25 everywhere.
 auto run=[&](const scene_water_gpu::Frame& f){std::vector<float> out(W*H*4);
  wait();assert(SUCCEEDED(allocator->Reset()));assert(SUCCEEDED(list->Reset(allocator.Get(),nullptr)));
  auto target=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,true),depth=texture(DXGI_FORMAT_R32_FLOAT,false),ocean=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,false);
  assert(gpu.ensure(device.Get(),queue.Get(),target.desc));
  auto rgba=[](unsigned char* p,UINT x,float r,float g,float b,float a){auto h=(HALF*)p+x*4;h[0]=XMConvertFloatToHalf(r);h[1]=XMConvertFloatToHalf(g);h[2]=XMConvertFloatToHalf(b);h[3]=XMConvertFloatToHalf(a);};
  fill(target,[&](unsigned char* p,UINT x,UINT y){auto q=classify(x,y);rgba(p,x,q.colour[0],q.colour[1],q.colour[2],.25f);});
  fill(depth,[&](unsigned char* p,UINT x,UINT y){((float*)p)[x]=classify(x,y).depth;});
  fill(ocean,[&](unsigned char* p,UINT x,UINT y){bool w=classify(x,y).kind==Sea;rgba(p,x,w?1.f:0.f,w?30.f:0.f,0,0);});
  D3D12_RESOURCE_BARRIER b[3]={scene_water_gpu::barrier(target.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_RENDER_TARGET),scene_water_gpu::barrier(depth.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),scene_water_gpu::barrier(ocean.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON)};list->ResourceBarrier(3,b);
  using I=scene_water_gpu::Gpu::Input;assert(gpu.record(list.Get(),target.resource.Get(),I{depth.resource.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},I{ocean.resource.Get(),D3D12_RESOURCE_STATE_COMMON},f));
  b[0]=scene_water_gpu::barrier(target.resource.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);list->ResourceBarrier(1,b);copy(target,false);assert(SUCCEEDED(list->Close()));ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);wait();
  unsigned char* pixels{};D3D12_RANGE range{0,SIZE_T(target.bytes)};assert(SUCCEEDED(target.readback->Map(0,&range,(void**)&pixels)));
  for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x){auto h=(const HALF*)(pixels+target.footprint.Offset+y*target.footprint.Footprint.RowPitch)+x*4;for(int i=0;i<4;++i)out[(y*W+x)*4+i]=XMConvertHalfToFloat(h[i]);}
  D3D12_RANGE none{0,0};target.readback->Unmap(0,&none);return out;};
 // The value each pixel started with, rounded to half like the target.
 auto start=[&](UINT x,UINT y,int i){auto q=classify(x,y);return XMConvertHalfToFloat(XMConvertFloatToHalf(i<3?q.colour[i]:.25f));};
 // Exact float equality of the read-back halves: no pixel outside the ocean (and no alpha) may change by even one bit.
 auto others_identical=[&](const std::vector<float>& o){for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x)for(int i=0;i<4;++i)if(classify(x,y).kind!=Sea||i==3)assert(o[(y*W+x)*4+i]==start(x,y,i));};
 const float noon_sun[3]{0,.97f,.24f},low_sun[3]{0,.2f,.98f},set_sun[3]{0,-.3f,.95f};unsigned cases=0;
 auto frame=[&](const float* sun,float reflections,float glint,uint32_t quality,const double* wind){return scene_water_gpu::build(right,up,forward,projection,W,H,camera,wind,12.5,sun,sun_colour,sky,reflections,glint,quality,3);};
 // Everything off: nothing changes anywhere.
 {auto o=run(frame(noon_sun,0,0,1,calm));for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x)for(int i=0;i<4;++i)assert(o[(y*W+x)*4+i]==start(x,y,i));std::cout<<"Off unchanged\n";++cases;}
 // Reflections: the wall's reflection reddens the water below it; water beside it (rays that find only sky) is unchanged.
 for(uint32_t quality=0;quality<3;++quality){auto o=run(frame(noon_sun,1,0,quality,calm));others_identical(o);unsigned reflected=0,sea=0,beside=0;float gain=0;
  for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x){if(classify(x,y).kind!=Sea)continue;++sea;float d=o[(y*W+x)*4]-start(x,y,0);
   if(x>W*2/5&&x<W*3/5){gain=std::max(gain,d);if(d>.2f)++reflected;}
   if(x<W/8||x>W*7/8){++beside;assert(o[(y*W+x)*4]==start(x,y,0)&&o[(y*W+x)*4+1]==start(x,y,1)&&o[(y*W+x)*4+2]==start(x,y,2));}}
  std::cout<<"Reflection quality="<<quality<<" sea="<<sea<<" reflected="<<reflected<<" gain="<<gain<<" beside="<<beside<<'\n';assert(sea>W*H/3&&reflected>20&&gain>.3f&&beside>100);++cases;}
 // Ripples follow the wind but keep the reflection.
 {const double breeze[2]{8,-6};auto o=run(frame(noon_sun,1,0,1,breeze));others_identical(o);unsigned reflected=0;for(UINT y=0;y<H;++y)for(UINT x=W*2/5;x<W*3/5;++x)if(classify(x,y).kind==Sea&&o[(y*W+x)*4]-start(x,y,0)>.2f)++reflected;std::cout<<"Windy reflected="<<reflected<<'\n';assert(reflected>10);++cases;}
 // Glint: a low sun ahead lights a bright path on the water; a set sun adds nothing.
 {auto o=run(frame(low_sun,0,1,1,calm));others_identical(o);float brightest=0;for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x)if(classify(x,y).kind==Sea)brightest=std::max(brightest,o[(y*W+x)*4+1]-start(x,y,1));
  auto night=run(frame(set_sun,0,1,1,calm));others_identical(night);for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x)assert(night[(y*W+x)*4+1]==start(x,y,1));std::cout<<"Glint brightest="<<brightest<<" sun set unchanged\n";assert(brightest>1);cases+=2;}
 auto bad=frame(noon_sun,1,1,1,calm);bad.params[0]=2;wait();assert(SUCCEEDED(allocator->Reset()));assert(SUCCEEDED(list->Reset(allocator.Get(),nullptr)));using I=scene_water_gpu::Gpu::Input;assert(!gpu.record(list.Get(),nullptr,I{},I{},bad));assert(SUCCEEDED(list->Close()));
 std::cout<<"PASS: water "<<cases<<" D3D12 cases\n";CloseHandle(event);
}
