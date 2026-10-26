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
#include "effects/lens_gpu.h"
#include "tests/gpu_test_device.h"
using Microsoft::WRL::ComPtr;
using namespace DirectX::PackedVector;
// Lens on WARP: with focus on a near band in the middle, the far striped
// background blurs and the band stays sharp; turning the camera blurs the
// background but not things within a metre; alpha is kept; both off changes
// nothing.
// WARP can drop the device when the machine is busy (seen right after the other
// GPU tests). That is retried once on a fresh device; pixel checks never are.
struct DeviceLost:std::runtime_error{using std::runtime_error::runtime_error;};
static void body(){
 // K39: shared WARP device with the debug layer, GPU-based validation and an InfoQueue check per case.
 gpu_test::Device test_device;assert(gpu_test::create(test_device));ComPtr<ID3D12Device> device=test_device.device;ComPtr<ID3D12CommandQueue> queue=test_device.queue;ComPtr<ID3D12Fence> fence;assert(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);UINT64 serial{};
 auto lost=[&](const char* where,HRESULT hr){char text[160];snprintf(text,sizeof(text),"%s failed hr=0x%08lx removed=0x%08lx",where,(unsigned long)hr,(unsigned long)device->GetDeviceRemovedReason());throw DeviceLost(text);};
 auto wait=[&]{if(HRESULT hr=queue->Signal(fence.Get(),++serial);FAILED(hr))lost("Signal",hr);assert(SUCCEEDED(fence->SetEventOnCompletion(serial,event)));assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);if(HRESULT hr=device->GetDeviceRemovedReason();FAILED(hr))lost("Wait",hr);};
 ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;assert(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))));assert(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))));assert(SUCCEEDED(list->Close()));
 static constexpr UINT W=256,H=256; // static: the local Extra struct below uses H in a default member initializer (MSVC C2512)
 struct Texture{ComPtr<ID3D12Resource> resource,upload,readback;D3D12_RESOURCE_DESC desc{};D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes{};};
 auto buffer=[&](ComPtr<ID3D12Resource>& out,UINT64 bytes,D3D12_HEAP_TYPE type){D3D12_HEAP_PROPERTIES hp{};hp.Type=type;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;assert(SUCCEEDED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&out))));};
 auto texture=[&](DXGI_FORMAT format,bool target){Texture t;t.desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;t.desc.Width=W;t.desc.Height=H;t.desc.DepthOrArraySize=1;t.desc.MipLevels=1;t.desc.Format=format;t.desc.SampleDesc.Count=1;t.desc.Flags=target?D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET:D3D12_RESOURCE_FLAG_NONE;D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;assert(SUCCEEDED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&t.desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&t.resource))));device->GetCopyableFootprints(&t.desc,0,1,0,&t.footprint,nullptr,nullptr,&t.bytes);buffer(t.upload,t.bytes,D3D12_HEAP_TYPE_UPLOAD);buffer(t.readback,t.bytes,D3D12_HEAP_TYPE_READBACK);return t;};
 auto copy=[&](Texture& t,bool upload){D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=t.resource.Get();a.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;b.pResource=upload?t.upload.Get():t.readback.Get();b.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;b.PlacedFootprint=t.footprint;if(upload)list->CopyTextureRegion(&a,0,0,0,&b,nullptr);else list->CopyTextureRegion(&b,0,0,0,&a,nullptr);};
 auto fill=[&](Texture& t,const std::function<void(unsigned char*,UINT,UINT)>& write){unsigned char* data{};D3D12_RANGE none{0,0};assert(SUCCEEDED(t.upload->Map(0,&none,(void**)&data)));for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x)write(data+t.footprint.Offset+y*t.footprint.Footprint.RowPitch,x,y);t.upload->Unmap(0,nullptr);copy(t,true);};
 scene_lens_gpu::Gpu gpu;const float near_plane=.1f;
 // One-pixel black and white columns 100 m away; rows 120..136 hold a band
 // `band_z` metres away, also striped.
 // Extra: spot = HDR value of pixel (128,40); rows from ocean_from down are ocean (no depth, gbuffer_ocean.xy = 1);
 // rim marks the band in gbuffer_0.w. Each extra texture is bound only when used.
 struct Extra{float spot{};UINT ocean_from{H};bool rim{};};
 auto run=[&](const scene_lens_gpu::Frame& f,float band_z,Extra e={}){std::vector<float> out(W*H*4);
  wait();assert(SUCCEEDED(allocator->Reset()));assert(SUCCEEDED(list->Reset(allocator.Get(),nullptr)));
  auto target=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,true),depth=texture(DXGI_FORMAT_R32_FLOAT,false);
  assert(gpu.ensure(device.Get(),queue.Get(),target.desc));
  auto rgba=[](unsigned char* p,UINT x,float r,float g,float b,float a){auto h=(HALF*)p+x*4;h[0]=XMConvertFloatToHalf(r);h[1]=XMConvertFloatToHalf(g);h[2]=XMConvertFloatToHalf(b);h[3]=XMConvertFloatToHalf(a);};
  fill(target,[&](unsigned char* p,UINT x,UINT y){float v=e.spot>0&&x==128&&y==40?e.spot:float(x&1);rgba(p,x,v,v,v,.25f);});
  fill(depth,[&](unsigned char* p,UINT x,UINT y){((float*)p)[x]=y>=e.ocean_from?0.f:near_plane/(y>=120&&y<136?band_z:100.f);});
  Texture ocean,albedo;if(e.ocean_from<H){ocean=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,false);fill(ocean,[&](unsigned char* p,UINT x,UINT y){float v=y>=e.ocean_from?1.f:0.f;rgba(p,x,v,v,0,0);});}
  if(e.rim){albedo=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,false);fill(albedo,[&](unsigned char* p,UINT x,UINT y){rgba(p,x,.5f,.5f,.5f,y>=120&&y<136?1.f:0.f);});}
  D3D12_RESOURCE_BARRIER b[4]={scene_lens_gpu::barrier(target.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_RENDER_TARGET),scene_lens_gpu::barrier(depth.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON)};UINT nb=2;
  for(auto* t:{&ocean,&albedo})if(t->resource)b[nb++]=scene_lens_gpu::barrier(t->resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);list->ResourceBarrier(nb,b);
  using I=scene_lens_gpu::Gpu::Input;
  assert(gpu.record(list.Get(),target.resource.Get(),{depth.resource.Get(),D3D12_RESOURCE_STATE_COMMON},f,I{ocean.resource.Get(),D3D12_RESOURCE_STATE_COMMON},I{albedo.resource.Get(),D3D12_RESOURCE_STATE_COMMON}));
  b[0]=scene_lens_gpu::barrier(target.resource.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);list->ResourceBarrier(1,b);copy(target,false);assert(SUCCEEDED(list->Close()));ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);wait();
  unsigned char* pixels{};D3D12_RANGE range{0,SIZE_T(target.bytes)};if(HRESULT hr=target.readback->Map(0,&range,(void**)&pixels);FAILED(hr))lost("Map",hr);
  for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x){auto h=(const HALF*)(pixels+target.footprint.Offset+y*target.footprint.Footprint.RowPitch)+x*4;for(int i=0;i<4;++i)out[(y*W+x)*4+i]=XMConvertHalfToFloat(h[i]);}
  D3D12_RANGE none{0,0};target.readback->Unmap(0,&none);gpu_test::expect_clean(test_device,"lens run");return out;};
 auto base=[&]{scene_lens_gpu::Frame f;f.metrics={1.f/W,1.f/H,float(W),float(H)};f.right={1,0,0,.5f};f.up={0,1,0,.5f};f.forward={0,0,1,5000};f.projection={0,near_plane,1,0};f.camera_move={0,0,0,1};f.params={0,0,12,0};return f;};
 // Largest step between neighbouring pixels of a row across the middle of the screen.
 auto contrast=[&](const std::vector<float>& o,UINT y){float m=0;for(UINT x=96;x<160;++x)m=std::max(m,std::abs(o[(y*W+x)*4]-o[(y*W+x+1)*4]));return m;};
 unsigned cases=0;
 {auto o=run(base(),1.5f);for(UINT i=0;i<W*H;++i)assert(o[i*4]==float((i%W)&1)&&o[i*4+3]==.25f);std::cout<<"Both off unchanged\n";++cases;}
 {auto f=base();f.params[0]=2;auto o=run(f,1.5f);float distant=contrast(o,40),band=contrast(o,128);std::cout<<"Depth of field far contrast="<<distant<<" focused band contrast="<<band<<'\n';
  assert(distant<.5f&&band>.99f);for(UINT i=0;i<W*H;++i)assert(o[i*4+3]==.25f);cases+=2;}
 {auto f=base();const float a=.05f;f.params[1]=1;f.previous_right={std::cos(a),0,-std::sin(a),.5f};f.previous_up={0,1,0,.5f};f.previous_forward={std::sin(a),0,std::cos(a),0};
  auto o=run(f,.8f);float distant=contrast(o,40),close=contrast(o,128);std::cout<<"Motion blur far contrast="<<distant<<" held-item band contrast="<<close<<'\n';assert(distant<.5f&&close>.99f);cases+=2;
  auto still=base();still.params[1]=1;still.previous_right=still.right;still.previous_up=still.up;still.previous_forward={0,0,1,0};o=run(still,.8f);assert(contrast(o,40)>.99f);std::cout<<"Still camera unchanged\n";++cases;}
 // FIN-5: a held item at 0.8 m with focus at 100 m keeps its edges (no blur circle under 1.5 m).
 {auto f=base();f.params[0]=2;auto o=run(f,.8f);for(UINT y:{120u,135u})for(UINT x=96;x<160;++x)assert(o[(y*W+x)*4]==float(x&1));std::cout<<"Held item edges unchanged under depth of field\n";++cases;}
 // FIN-8: the ocean (no depth) focused at the sea plane: camera 2.83 m up, rows 192+ meet the sea about 10 m away.
 {auto f=base();f.params[0]=2;f.sea={2.83f,1,0,0};auto o=run(f,10,{0,192});float sea=contrast(o,200),distant=contrast(o,40);
  std::cout<<"Ocean at the focus distance contrast="<<sea<<" far contrast="<<distant<<'\n';assert(sea>.9f&&distant<.5f);
  f.sea={0,0,0,0};o=run(f,10);assert(contrast(o,200)<.5f);++cases;}
 // FIN-6: one 1000-HDR pixel in a defocused background cannot light its neighbours (Karis weights).
 {auto f=base();f.params[0]=2;auto o=run(f,1.5f,{1000});float peak=0;for(UINT y=32;y<=48;++y)for(UINT x=120;x<=136;++x)if(x!=128||y!=40)peak=std::max(peak,o[(y*W+x)*4]);
  std::cout<<"Bright tap neighbourhood peak="<<peak<<'\n';assert(peak<5);++cases;}
 // K29: rim-flagged pixels within 25 m get no camera motion blur; at 50 m they blur again.
 {auto f=base();const float a=.05f;f.params[1]=1;f.previous_right={std::cos(a),0,-std::sin(a),.5f};f.previous_up={0,1,0,.5f};f.previous_forward={std::sin(a),0,std::cos(a),0};
  auto o=run(f,10,{0,H,true});float rim=contrast(o,128),distant=contrast(o,40);std::cout<<"Rim band contrast="<<rim<<" far="<<distant<<'\n';assert(rim>.99f&&distant<.5f);
  o=run(f,50,{0,H,true});assert(contrast(o,128)<.5f);o=run(f,10);assert(contrast(o,128)<.5f);++cases;}
 auto bad=base();bad.params[2]=0;wait();assert(SUCCEEDED(allocator->Reset()));assert(SUCCEEDED(list->Reset(allocator.Get(),nullptr)));assert(!gpu.record(list.Get(),nullptr,{},bad));assert(SUCCEEDED(list->Close()));
 std::cout<<"PASS: lens "<<cases<<" D3D12 cases\n";CloseHandle(event);
}
// D3D12 hands back the same removed WARP device inside one process, so the
// retry runs in a fresh copy of this test.
static int rerun(){wchar_t path[MAX_PATH];GetModuleFileNameW(nullptr,path,MAX_PATH);std::wstring command=L"\""+std::wstring(path)+L"\" --retry";STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};if(!CreateProcessW(path,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&pi))return 1;WaitForSingleObject(pi.hProcess,INFINITE);DWORD code=1;GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return int(code);}
int main(int argc,char**argv){std::cout<<std::unitbuf;bool retry=argc>1&&std::string(argv[1])=="--retry";try{body();}catch(const DeviceLost& lost){if(retry){std::cout<<"FAIL: device lost again ("<<lost.what()<<")"<<std::endl;return 1;}std::cout<<"Device lost ("<<lost.what()<<"), retrying once in a new process"<<std::endl;return rerun();}return 0;}
