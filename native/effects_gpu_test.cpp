#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <DirectXPackedVector.h>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>
#include <functional>
#include "effects/effects_gpu.h"
#include "tests/gpu_test_device.h"
using Microsoft::WRL::ComPtr;
using namespace DirectX::PackedVector;
// Scene effects on WARP: the Game curve with no grading is an exact round trip
// through the game's tone mapping, exposure doubles the HDR value, bloom spreads
// a bright pixel, glare needs open sky, Filmic stays finite and ordered, and
// metering reads back a plausible luminance.
int main(int argc,char** argv){std::cout<<std::unitbuf;
 // Diagnostics for the laptop run of cfb6a24, killed at 626 s on WARP with no output in the report (the cloud has no
 // WARP or debug layer to reproduce it). Every run prints a TIME line summed over its frames: setup and recording
 // (ensure: root signature and pipelines), the GPU waits, readback and the validation check. A watchdog prints the
 // step still running every 60 s, so a stall names its call. ctest shows both with -V or --output-on-failure, or run
 // the exe directly. Each frame is still its own submission, as in the game.
 static std::atomic<const char*> step{"device"};static std::atomic<int> run_no{0},frame_no{0};static std::atomic<ULONGLONG> step_at{GetTickCount64()};const ULONGLONG start=GetTickCount64();
 auto at=[](const char* s){step=s;step_at=GetTickCount64();};
 if(HANDLE watchdog=CreateThread(nullptr,0,[](void*)->DWORD{ULONGLONG said=0;for(;;){Sleep(1000);ULONGLONG now=GetTickCount64(),since=now-step_at;
  if(since>=60000&&now-said>=60000){said=now;std::printf("WATCHDOG: run %d frame %d still in %s after %u s\n",run_no.load(),frame_no.load(),step.load(),unsigned(since/1000));std::fflush(stdout);}}},nullptr,0,nullptr))CloseHandle(watchdog);
 // K39: shared WARP device with the debug layer and an InfoQueue check per case (GPU-based validation with --gbv).
 // GPU-based validation is opt-in (--gbv): on WARP it slows these shaders to minutes per run (the laptop run of
 // cfb6a24 was killed at 626 s; main's test never enabled it). The debug layer and the InfoQueue check stay on.
 const bool gbv=argc>1&&std::strcmp(argv[1],"--gbv")==0;
 gpu_test::Device test_device;assert(gpu_test::create(test_device,gbv));std::cout<<"Device: WARP, debug layer "<<(test_device.validated?"on":"off")<<", GPU-based validation "<<(test_device.validated&&gbv?"requested":"off")<<'\n';ComPtr<ID3D12Device> device=test_device.device;ComPtr<ID3D12CommandQueue> queue=test_device.queue;ComPtr<ID3D12Fence> fence;assert(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);UINT64 serial{};
 auto wait=[&]{assert(SUCCEEDED(queue->Signal(fence.Get(),++serial)));assert(SUCCEEDED(fence->SetEventOnCompletion(serial,event)));assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);};
 ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;assert(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))));assert(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))));assert(SUCCEEDED(list->Close()));
 const UINT W=64,H=48;
 struct Texture{ComPtr<ID3D12Resource> resource,upload,readback;D3D12_RESOURCE_DESC desc{};D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes{};};
 auto buffer=[&](ComPtr<ID3D12Resource>& out,UINT64 bytes,D3D12_HEAP_TYPE type){D3D12_HEAP_PROPERTIES hp{};hp.Type=type;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;assert(SUCCEEDED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&out))));};
 auto texture=[&](DXGI_FORMAT format){Texture t;t.desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;t.desc.Width=W;t.desc.Height=H;t.desc.DepthOrArraySize=1;t.desc.MipLevels=1;t.desc.Format=format;t.desc.SampleDesc.Count=1;t.desc.Flags=format==DXGI_FORMAT_R16G16B16A16_FLOAT?D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET:D3D12_RESOURCE_FLAG_NONE;D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;assert(SUCCEEDED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&t.desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&t.resource))));device->GetCopyableFootprints(&t.desc,0,1,0,&t.footprint,nullptr,nullptr,&t.bytes);buffer(t.upload,t.bytes,D3D12_HEAP_TYPE_UPLOAD);buffer(t.readback,t.bytes,D3D12_HEAP_TYPE_READBACK);return t;};
 auto copy=[&](Texture& t,bool upload){D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=t.resource.Get();a.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;b.pResource=upload?t.upload.Get():t.readback.Get();b.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;b.PlacedFootprint=t.footprint;if(upload)list->CopyTextureRegion(&a,0,0,0,&b,nullptr);else list->CopyTextureRegion(&b,0,0,0,&a,nullptr);};
 scene_effects_gpu::Gpu gpu;
 // colour(x,y) -> rgb; sky(x,y) -> depth 0 (open sky) instead of 0.5. Options: cloud(x,y) -> gbuffer_1.w = 1,
 // sea(x,y) -> gbuffer_ocean.xy = 1 (each bound only when given); keep: no reset on the first frame.
 struct Options{std::function<bool(UINT,UINT)> cloud,sea;bool keep{};};
 auto run=[&](scene_effects_gpu::Frame f,auto colour,auto sky,int frames=1,Options o={}){std::vector<float> out(W*H*4);++run_no;const ULONGLONG t0=GetTickCount64();ULONGLONG ensured=0,recorded=0,gpu_ms=0;
  for(int n=0;n<frames;++n){frame_no=n;at("setup");ULONGLONG t=GetTickCount64();wait();assert(SUCCEEDED(allocator->Reset()));assert(SUCCEEDED(list->Reset(allocator.Get(),nullptr)));auto color=texture(DXGI_FORMAT_R16G16B16A16_FLOAT),depth=texture(DXGI_FORMAT_R32_FLOAT);
   {const ULONGLONG e=GetTickCount64();at("ensure");assert(gpu.ensure(device.Get(),queue.Get(),color.desc));ensured+=GetTickCount64()-e;at("setup");}
   Texture normals,ocean;if(o.cloud)normals=texture(DXGI_FORMAT_R16G16B16A16_FLOAT);if(o.sea)ocean=texture(DXGI_FORMAT_R16G16B16A16_FLOAT);
   for(auto item:{&color,&depth,&normals,&ocean}){if(!item->resource)continue;unsigned char* data{};D3D12_RANGE none{0,0};assert(SUCCEEDED(item->upload->Map(0,&none,(void**)&data)));
    for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x){auto p=data+item->footprint.Offset+y*item->footprint.Footprint.RowPitch;
     if(item==&color){auto h=(HALF*)p+x*4;auto c=colour(x,y);for(int i=0;i<3;++i)h[i]=XMConvertFloatToHalf(c[i]);h[3]=XMConvertFloatToHalf(.5f);}
     else if(item==&depth)((float*)p)[x]=sky(x,y)?0.f:.5f;
     else{auto h=(HALF*)p+x*4;bool on=item==&normals?o.cloud(x,y):o.sea(x,y);h[0]=h[1]=XMConvertFloatToHalf(item==&ocean&&on?1.f:0.f);h[2]=XMConvertFloatToHalf(0);h[3]=XMConvertFloatToHalf(item==&normals&&on?1.f:0.f);}}
    item->upload->Unmap(0,nullptr);copy(*item,true);}
   D3D12_RESOURCE_BARRIER b[4]={scene_effects_gpu::barrier(color.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_RENDER_TARGET),scene_effects_gpu::barrier(depth.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)};UINT nb=2;
   for(auto item:{&normals,&ocean})if(item->resource)b[nb++]=scene_effects_gpu::barrier(item->resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);list->ResourceBarrier(nb,b);
   using I=scene_effects_gpu::Gpu::Input;const auto psr=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
   at("record");assert(gpu.record(list.Get(),color.resource.Get(),depth.resource.Get(),psr,f,n==0&&!o.keep,I{normals.resource.Get(),psr},I{ocean.resource.Get(),psr}));
   b[0]=scene_effects_gpu::barrier(color.resource.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);list->ResourceBarrier(1,b);copy(color,false);assert(SUCCEEDED(list->Close()));recorded+=GetTickCount64()-t;t=GetTickCount64();at("execute and wait");ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);wait();gpu_ms+=GetTickCount64()-t;at("readback");
   unsigned char* pixels{};D3D12_RANGE range{0,SIZE_T(color.bytes)};assert(SUCCEEDED(color.readback->Map(0,&range,(void**)&pixels)));
   for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x){auto h=(const HALF*)(pixels+color.footprint.Offset+y*color.footprint.Footprint.RowPitch)+x*4;for(int i=0;i<4;++i)out[(y*W+x)*4+i]=XMConvertHalfToFloat(h[i]);}
   D3D12_RANGE none{0,0};color.readback->Unmap(0,&none);}
  at("validation check");gpu_test::expect_clean(test_device,"effects run");const ULONGLONG t3=GetTickCount64();at("test");
  std::printf("TIME run %d frames=%d setup+record=%u ms (ensure %u ms) gpu=%u ms readback+check=%u ms t=%.1f s\n",run_no.load(),frames,unsigned(recorded),unsigned(ensured),unsigned(gpu_ms),unsigned(t3-t0-recorded-gpu_ms),(t3-start)/1000.0);std::fflush(stdout);return out;};
 // The game's display chain applied to a written HDR value, to read back the display colour.
 auto aces=[](float x){return std::clamp(x*(2.51f*x+.03f)/(x*(2.43f*x+.59f)+.14f),0.f,1.f);};
 auto display=[&](const std::vector<float>& o,UINT x,UINT y){std::array<float,3> g{};for(int i=0;i<3;++i){g[i]=(std::pow(aces(.85f*o[(y*W+x)*4+i]),1/2.2f)-.45f)*1.12f+.45f;}
  float l=.2126f*g[0]+.7152f*g[1]+.0722f*g[2];for(auto& v:g)v=l+(v-l)*1.15f;return g;};
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
 // K33: the 32x18 bilinear reduction keeps the weighted log average: left half 1/16, right half 1 -> -2 stops.
 {auto f=base();f.exposure={1,1,1,1.5f};auto halves=[](UINT x,UINT){float v=x<W/2?.0625f:1.f;return std::array<float,3>{v,v,v};};run(f,halves,ground,10);auto m=gpu.measured();std::cout<<"Halves fast="<<m.fast<<'\n';assert(m.valid&&std::abs(m.fast+2)<.1f);++cases;}
 // K25: nothing reads the metering, so it is skipped and measured() stays invalid.
 {auto grey=[](UINT,UINT){return std::array<float,3>{.25f,.25f,.25f};};run(base(),grey,ground,10);assert(!gpu.measured().valid);std::cout<<"Metering skipped when unused\n";++cases;}
 // K21: frozen weights (exposure.yz = 0, as on a lightning frame) hold the adapted value.
 {auto f=base();f.exposure={1,.5f,.05f,1.5f};auto dim=[](UINT,UINT){return std::array<float,3>{.25f,.25f,.25f};};auto bright=[](UINT,UINT){return std::array<float,3>{2.f,2.f,2.f};};
  run(f,dim,ground,10);f.exposure[1]=f.exposure[2]=0;run(f,bright,ground,10,{{},{},true});auto m=gpu.measured();std::cout<<"Frozen fast="<<m.fast<<'\n';assert(m.valid&&std::abs(m.fast+2)<.1f);++cases;}
 // FIN-2: game clouds (gbuffer_1.w) and the ocean are not open sky.
 {auto f=base();f.sun_screen={.5f,.5f,1,.5f};f.sun_colour[3]=1;auto dim=[](UINT,UINT){return std::array<float,3>{.02f,.02f,.02f};};auto sky=[](UINT,UINT){return true;};auto all=[](UINT,UINT){return true;};
  auto open=run(f,dim,sky,2),cloud=run(f,dim,sky,2,{all,{}}),sea=run(f,dim,sky,2,{{},all}),blocked=run(f,dim,ground,2);float c=cloud[(24*W+32)*4],s=sea[(24*W+32)*4],b=blocked[(24*W+32)*4],op=open[(24*W+32)*4];
  std::cout<<"Glare open="<<op<<" cloud="<<c<<" sea="<<s<<" blocked="<<b<<'\n';assert(std::abs(c-b)<=.002f+.02f*b&&std::abs(s-b)<=.002f+.02f*b&&op>b*1.5f);++cases;}
 // FIN-3: a sun half off the right edge keeps full visibility (taps off screen do not count), and the value is held while the sun is off screen.
 {auto f=base();f.exposure={1,.5f,.05f,1.5f};f.sun_screen={.998f,.5f,1,.5f};f.sun_colour[3]=1;f.tuning[3]=.5f;auto dim=[](UINT,UINT){return std::array<float,3>{.02f,.02f,.02f};};auto sky=[](UINT,UINT){return true;};
  run(f,dim,sky,10);auto m=gpu.measured();std::cout<<"Edge visibility="<<m.visibility<<'\n';assert(m.valid&&m.visibility>.95f);
  f.sun_screen[2]=0;run(f,dim,sky,10,{{},{},true});m=gpu.measured();std::cout<<"Held visibility="<<m.visibility<<'\n';assert(m.valid&&m.visibility>.95f);++cases;}
 // FIN-20: falloff r weights level n by r^n and normalises by sum r^n; far from a spot the veil is lower than with r = 1.
 {auto spot=[](UINT x,UINT y){float v=x==32&&y==24?50.f:0.f;return std::array<float,3>{v,v,v};};auto f=base();f.extras[0]=1;auto wide=run(f,spot,ground);f.tuning[0]=.7f;auto tight=run(f,spot,ground);
  float a=wide[(24*W+52)*4],b=tight[(24*W+52)*4];std::cout<<"Bloom far r=1 "<<a<<" r=.7 "<<b<<'\n';assert(b<a);++cases;}
 // FIN-10: tone revision 1 lifts Filmic's mid grey toward the game's (.509); revision 0 is V2 (.481).
 {auto grey=[](UINT,UINT){return std::array<float,3>{.18f,.18f,.18f};};auto f=base();f.grading={1,0,0,0};auto v2=display(run(f,grey,ground),32,24)[1];f.tuning[1]=1;auto v3=display(run(f,grey,ground),32,24)[1];
  std::cout<<"Filmic mid grey v2="<<v2<<" revision 1="<<v3<<'\n';assert(std::abs(v2-.481f)<.012f&&std::abs(v3-.507f)<.012f);++cases;}
 // FIN-9: Soft film keeps a bright orange orange (R/G 1.24 modelled; V2 1.10).
 {auto orange=[](UINT,UINT){return std::array<float,3>{4.f,1.2f,.2f};};auto f=base();f.grading={3,0,0,0};auto d=display(run(f,orange,ground),32,24);std::cout<<"Soft film orange R/G="<<d[0]/d[1]<<'\n';assert(d[0]/d[1]>1.15f);++cases;}
 // FIN-4: dither moves a flat grey by at most about 1 LSB, averages out, and leaves alpha alone.
 {auto grey=[](UINT,UINT){return std::array<float,3>{.18f,.18f,.18f};};auto plain=run(base(),grey,ground);auto f=base();f.tuning[2]=1;auto noisy=run(f,grey,ground);
  float p=display(plain,32,24)[1],lo=1,hi=0,sum=0;for(UINT y=4;y<44;++y)for(UINT x=4;x<60;++x){float v=display(noisy,x,y)[1];lo=std::min(lo,v);hi=std::max(hi,v);sum+=v;assert(noisy[(y*W+x)*4+3]==.5f);}
  float mean=sum/(40*56);std::cout<<"Dither range "<<(lo-p)*255<<".."<<(hi-p)*255<<" LSB mean "<<(mean-p)*255<<'\n';assert(hi>lo&&(hi-p)*255<1.6f&&(p-lo)*255<1.6f&&std::abs(mean-p)*255<.3f);++cases;}
 // K09/XC-3: release() drops everything; the next record re-creates and resets on its own.
 {gpu.release();assert(!gpu.loaded());auto grey=[](UINT,UINT){return std::array<float,3>{.25f,.25f,.25f};};auto f=base();f.exposure={1,.5f,.05f,1.5f};auto o=run(f,grey,ground,10,{{},{},true});for(auto v:o)assert(std::isfinite(v));auto m=gpu.measured();assert(m.valid&&std::abs(m.fast+2)<.1f);std::cout<<"Released and re-created\n";++cases;}
 at("invalid frame");auto bad=base();bad.grading[0]=7;wait();assert(SUCCEEDED(allocator->Reset()));assert(SUCCEEDED(list->Reset(allocator.Get(),nullptr)));assert(!gpu.record(list.Get(),nullptr,nullptr,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,bad,true));assert(SUCCEEDED(list->Close()));
 std::cout<<"PASS: scene effects "<<cases<<" D3D12 cases\n";CloseHandle(event);at("teardown");
}
