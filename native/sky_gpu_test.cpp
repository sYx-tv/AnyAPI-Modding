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
#include "effects/sky_gpu.h"
#include "scene_celestial.h"
#include "tests/gpu_test_device.h"
using Microsoft::WRL::ComPtr;
using namespace DirectX::PackedVector;
// Night sky on WARP: daylight changes nothing, the night sky gains stars, the
// moon shows where it should, the ocean and the target's alpha are untouched,
// and moonlight lights only surfaces facing the moon.
int main(){std::cout<<std::unitbuf;
 // K39: WARP with the debug layer and GPU-based validation; any ERROR or CORRUPTION message fails the test.
 gpu_test::Device dev;assert(gpu_test::create(dev));ComPtr<ID3D12Device> device=dev.device;ComPtr<ID3D12CommandQueue> queue=dev.queue;ComPtr<ID3D12Fence> fence;assert(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);UINT64 serial{};
 auto wait=[&]{assert(SUCCEEDED(queue->Signal(fence.Get(),++serial)));assert(SUCCEEDED(fence->SetEventOnCompletion(serial,event)));assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);};
 ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;assert(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))));assert(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))));assert(SUCCEEDED(list->Close()));
 const UINT W=64,H=64;
 struct Texture{ComPtr<ID3D12Resource> resource,upload,readback;D3D12_RESOURCE_DESC desc{};D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes{};};
 auto buffer=[&](ComPtr<ID3D12Resource>& out,UINT64 bytes,D3D12_HEAP_TYPE type){D3D12_HEAP_PROPERTIES hp{};hp.Type=type;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;assert(SUCCEEDED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&out))));};
 auto texture=[&](DXGI_FORMAT format,bool target){Texture t;t.desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;t.desc.Width=W;t.desc.Height=H;t.desc.DepthOrArraySize=1;t.desc.MipLevels=1;t.desc.Format=format;t.desc.SampleDesc.Count=1;t.desc.Flags=target?D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET:D3D12_RESOURCE_FLAG_NONE;D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;assert(SUCCEEDED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&t.desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&t.resource))));device->GetCopyableFootprints(&t.desc,0,1,0,&t.footprint,nullptr,nullptr,&t.bytes);buffer(t.upload,t.bytes,D3D12_HEAP_TYPE_UPLOAD);buffer(t.readback,t.bytes,D3D12_HEAP_TYPE_READBACK);return t;};
 auto copy=[&](Texture& t,bool upload){D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=t.resource.Get();a.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;b.pResource=upload?t.upload.Get():t.readback.Get();b.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;b.PlacedFootprint=t.footprint;if(upload)list->CopyTextureRegion(&a,0,0,0,&b,nullptr);else list->CopyTextureRegion(&b,0,0,0,&a,nullptr);};
 auto fill=[&](Texture& t,const std::function<void(unsigned char*,UINT,UINT)>& write){unsigned char* data{};D3D12_RANGE none{0,0};assert(SUCCEEDED(t.upload->Map(0,&none,(void**)&data)));for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x)write(data+t.footprint.Offset+y*t.footprint.Footprint.RowPitch,x,y);t.upload->Unmap(0,nullptr);copy(t,true);};
 scene_sky_gpu::Gpu gpu;
 struct Scene{std::function<float(UINT,UINT)> depth=[](UINT,UINT){return 0.f;};std::function<bool(UINT,UINT)> sea=[](UINT,UINT){return false;};float normal[3]{0,1,0};float base=.1f;};
 // Returns RGBA after the pass; the target starts at 0.1 grey with alpha 0.5.
 auto run=[&](const scene_sky_gpu::Frame& f,const Scene& s){std::vector<float> out(W*H*4);
  wait();assert(SUCCEEDED(allocator->Reset()));assert(SUCCEEDED(list->Reset(allocator.Get(),nullptr)));
  auto target=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,true),depth=texture(DXGI_FORMAT_R32_FLOAT,false),normals=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,false),albedo=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,false),ocean=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,false);
  assert(gpu.ensure(device.Get(),queue.Get(),target.desc));
  auto rgba=[](unsigned char* p,UINT x,float r,float g,float b,float a){auto h=(HALF*)p+x*4;h[0]=XMConvertFloatToHalf(r);h[1]=XMConvertFloatToHalf(g);h[2]=XMConvertFloatToHalf(b);h[3]=XMConvertFloatToHalf(a);};
  fill(target,[&](unsigned char* p,UINT x,UINT){rgba(p,x,s.base,s.base,s.base,.5f);});
  fill(depth,[&](unsigned char* p,UINT x,UINT y){((float*)p)[x]=s.depth(x,y);});
  fill(normals,[&](unsigned char* p,UINT x,UINT){rgba(p,x,s.normal[0],s.normal[1],s.normal[2],0);});
  fill(albedo,[&](unsigned char* p,UINT x,UINT){rgba(p,x,.5f,.5f,.5f,0);});
  fill(ocean,[&](unsigned char* p,UINT x,UINT y){bool w=s.sea(x,y);rgba(p,x,w?1.f:0.f,w?50.f:0.f,0,0);});
  D3D12_RESOURCE_BARRIER b[5]={scene_sky_gpu::barrier(target.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_RENDER_TARGET),scene_sky_gpu::barrier(depth.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),scene_sky_gpu::barrier(normals.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON),scene_sky_gpu::barrier(albedo.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON),scene_sky_gpu::barrier(ocean.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON)};list->ResourceBarrier(5,b);
  using I=scene_sky_gpu::Gpu::Input;const auto read=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,common=D3D12_RESOURCE_STATE_COMMON;
  assert(gpu.record(list.Get(),target.resource.Get(),I{depth.resource.Get(),read},I{normals.resource.Get(),common},I{albedo.resource.Get(),common},I{ocean.resource.Get(),common},f));
  b[0]=scene_sky_gpu::barrier(target.resource.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);list->ResourceBarrier(1,b);copy(target,false);assert(SUCCEEDED(list->Close()));ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);wait();
  unsigned char* pixels{};D3D12_RANGE range{0,SIZE_T(target.bytes)};assert(SUCCEEDED(target.readback->Map(0,&range,(void**)&pixels)));
  for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x){auto h=(const HALF*)(pixels+target.footprint.Offset+y*target.footprint.Footprint.RowPitch)+x*4;for(int i=0;i<4;++i)out[(y*W+x)*4+i]=XMConvertHalfToFloat(h[i]);}
  D3D12_RANGE none{0,0};target.readback->Unmap(0,&none);gpu_test::expect_clean(dev,"sky run");return out;};
 float noon[3]={0,1,0},midnight[3]={0,-1,0};unsigned cases=0;
 // Looking north and up, away from the moon (straight up at midnight).
 float right[4]{1,0,0,.8f},up[4]{0,.866f,.5f,.8f},forward[4]{0,.5f,-.866f,0};
 auto alpha_kept=[&](const std::vector<float>& o){for(UINT i=0;i<W*H;++i)assert(o[i*4+3]==.5f);};
 {auto o=run(scene_sky_gpu::build(right,up,forward,noon,W,H,3,1,1,1),Scene{});for(UINT i=0;i<W*H*4;i+=4)assert(o[i]==.1f||std::abs(o[i]-.1f)<1e-4f);alpha_kept(o);std::cout<<"Daylight unchanged\n";++cases;}
 {auto o=run(scene_sky_gpu::build(right,up,forward,midnight,W,H,3,1,1,1),Scene{});unsigned lit=0;float lowest=1;for(UINT i=0;i<W*H;++i){if(o[i*4+1]>.11f)++lit;lowest=std::min(lowest,o[i*4]);}std::cout<<"Night stars lit="<<lit<<" lowest="<<lowest<<'\n';assert(lit>20&&lowest>=.0999f);alpha_kept(o);++cases;}
 {float r2[4]{1,0,0,.05f},u2[4]{0,0,-1,.05f},f2[4]{0,1,0,0};auto o=run(scene_sky_gpu::build(r2,u2,f2,midnight,W,H,3,0,1,0),Scene{});float centre=o[(H/2*W+W/2)*4],corner=o[(2*W+2)*4];std::cout<<"Moon centre="<<centre<<" corner="<<corner<<'\n';assert(centre>1&&corner<.2f);
  Scene sea;sea.sea=[](UINT,UINT){return true;};o=run(scene_sky_gpu::build(r2,u2,f2,midnight,W,H,3,1,1,1),sea);for(UINT i=0;i<W*H*4;i+=4)assert(std::abs(o[i]-.1f)<1e-4f);std::cout<<"Ocean untouched\n";cases+=2;}
 {Scene ground;ground.depth=[](UINT,UINT){return .5f;};auto o=run(scene_sky_gpu::build(right,up,forward,midnight,W,H,3,1,1,1),ground);float facing=o[(H/2*W+W/2)*4];
  ground.normal[1]=-1;auto away=run(scene_sky_gpu::build(right,up,forward,midnight,W,H,3,1,1,1),ground);float back=away[(H/2*W+W/2)*4];std::cout<<"Moonlight facing="<<facing<<" away="<<back<<'\n';assert(facing>.12f&&std::abs(back-.1f)<1e-4f);++cases;}
 // Daytime sky: a sun disc where the sun is, and a cloud layer above the horizon only.
 {const float sun_colour[3]{1,.9f,.8f},sky_colour[3]{.3f,.4f,.6f};float r2[4]{1,0,0,.05f},u2[4]{0,0,-1,.05f},f2[4]{0,1,0,0};
  auto o=run(scene_sky_gpu::build(r2,u2,f2,noon,W,H,3,0,0,0,sun_colour,sky_colour,1,0),Scene{});float centre=o[(H/2*W+W/2)*4],corner=o[(2*W+2)*4];std::cout<<"Sun centre="<<centre<<" corner="<<corner<<'\n';assert(centre>5&&corner<1);alpha_kept(o);
  o=run(scene_sky_gpu::build(right,up,forward,noon,W,H,3,0,0,0,sun_colour,sky_colour,0,1.5f),Scene{});unsigned covered=0;for(UINT i=0;i<W*H;++i)if(std::abs(o[i*4]-.1f)>.01f)++covered;
  bool below=true;for(UINT x=0;x<W;++x)below=below&&std::abs(o[((H-1)*W+x)*4]-.1f)<1e-4f;std::cout<<"Clouds covered="<<covered<<" below horizon untouched="<<below<<'\n';assert(covered>W*H/20&&below);alpha_kept(o);cases+=2;}
 using namespace scene_sky_gpu;
 // SKY-5 (every version): moonlight never lands on the seabed under the ocean.
 {Scene seabed;seabed.depth=[](UINT,UINT){return .5f;};seabed.sea=[](UINT,UINT){return true;};auto o=run(build(right,up,forward,midnight,W,H,3,1,1,1),seabed);for(UINT i=0;i<W*H*4;i+=4)assert(std::abs(o[i]-.1f)<1e-4f);alpha_kept(o);std::cout<<"Seabed under the ocean unlit\n";++cases;}
 // K16: at t=.9 the game parks its sun at noon. V2 sees daylight and adds nothing; the clock sky has stars and the moon.
 double sun9[3],moon9[3];celestial::path(.9,1,1,sun9);celestial::path(.4,1,1,moon9);float parked[3];{double n[3];celestial::path(.5,1,1,n);for(int i=0;i<3;++i)parked[i]=float(n[i]);}
 auto clock_frame=[&](const float* r,const float* u,const float* fw,float phase,float stars,float moon_strength,float moonlight){ClockSky c{};for(int i=0;i<3;++i){c.sun[i]=sun9[i];c.moon[i]=moon9[i];}c.night=1;c.source=1;c.parked=true;c.phase=phase;
  const float dark[3]{.001f,.001f,.002f};auto f=build(r,u,fw,parked,W,H,3,stars,moon_strength,moonlight,nullptr,dark,0,0);assert(apply_clock(f,c,parked));return f;};
 unsigned v2_lit=0,clock_lit=0,clock_all=0;auto lit=[&](const std::vector<float>& o){unsigned n=0;for(UINT i=0;i<W*H;++i)if(o[i*4+1]>.11f)++n;return n;};
 {auto o=run(build(right,up,forward,parked,W,H,3,1,1,1),Scene{});for(UINT i=0;i<W*H*4;i+=4)assert(std::abs(o[i]-.1f)<1e-4f);
  auto c=run(clock_frame(right,up,forward,0,1,1,1),Scene{});clock_all=lit(c);alpha_kept(c);
  // SKY-6 compares the star fields alone (no moon, no milky way): this view at t=.9 also holds the clock moon (path(.4))
  // and a stretch of the milky way band, about 500 lit pixels together, while V2's midnight view has neither.
  auto sf=clock_frame(right,up,forward,0,1,0,0);sf.params[3]=0;clock_lit=lit(run(sf,Scene{}));
  auto vf=build(right,up,forward,midnight,W,H,3,1,0,0);vf.params[3]=0;v2_lit=lit(run(vf,Scene{}));
  // SKY-6: about a third as many stars, mostly faint.
  std::cout<<"Parked sun: V2 unchanged, clock sky lit="<<clock_all<<", clock stars lit="<<clock_lit<<" (V2 midnight stars lit="<<v2_lit<<")\n";assert(clock_all>clock_lit&&clock_lit>3&&clock_lit<v2_lit&&clock_lit<W*H*8/100);cases+=2;}
 // The unshadowed surface moonlight keeps the game's own sun: none under the parked noon sun on the clock (interiors stay dark).
 {Scene ground;ground.depth=[](UINT,UINT){return .5f;};auto o=run(clock_frame(right,up,forward,0,0,0,1),ground);for(UINT i=0;i<W*H*4;i+=4)assert(std::abs(o[i]-.1f)<1e-4f);std::cout<<"Clock night, parked sun: surfaces unlit\n";++cases;}
 // The moon where the clock puts it, and its phase: full brighter than quarter brighter than new.
 {float m[3]{float(moon9[0]),float(moon9[1]),float(moon9[2])},t[3]{0,0,0};{float l=std::sqrt(m[0]*m[0]+m[2]*m[2]);t[0]=m[2]/l;t[2]=-m[0]/l;}
  float b[3]{m[1]*t[2]-m[2]*t[1],m[2]*t[0]-m[0]*t[2],m[0]*t[1]-m[1]*t[0]};float r2[4]{t[0],t[1],t[2],.05f},u2[4]{b[0],b[1],b[2],.05f},f2[4]{m[0],m[1],m[2],0};
  float total[3]{};const float phases[3]{0,.25f,.5f};
  for(int k=0;k<3;++k){auto o=run(clock_frame(r2,u2,f2,phases[k],0,1,0),Scene{});for(UINT i=0;i<W*H;++i)total[k]+=o[i*4]-.1f;if(k==0)assert(o[(H/2*W+W/2)*4]>1);}
  std::cout<<"Moon phase full="<<total[0]<<" quarter="<<total[1]<<" new="<<total[2]<<'\n';assert(total[0]>total[1]*1.3f&&total[1]>total[2]*1.3f);++cases;
  // K49: an opaque moon hides what lies behind it; the V2 moon only adds.
  Scene bright;bright.base=5;auto add=run(clock_frame(r2,u2,f2,0,0,1,0),bright);auto f=clock_frame(r2,u2,f2,0,0,1,0);const double toward[3]{m[0],m[1],m[2]};assert(apply_moon_light(f,toward,1));auto hide=run(f,bright);
  float ca=add[(H/2*W+W/2)*4],ch=hide[(H/2*W+W/2)*4];std::cout<<"Moon over a bright sky additive="<<ca<<" opaque="<<ch<<'\n';assert(ca>5&&ch<5&&ch>.5f);alpha_kept(hide);++cases;}
 // Clouds v2 (SKY-8): dense cloud overhead hides a bright sky; V2 caps cover at .85 of the fade.
 {const float sun_colour[3]{1,.9f,.8f},sky_colour[3]{.3f,.4f,.6f};float r3[4]{1,0,0,.8f},u3[4]{0,0,-1,.8f},f3[4]{0,1,0,0};Scene bright;bright.base=10;
  auto v1=build(r3,u3,f3,noon,W,H,3,0,0,0,sun_colour,sky_colour,0,2);auto v2=v1;v2.model[0]=MODEL_CLOUDS_V2;
  float low1=1e9f,low2=1e9f;auto o1=run(v1,bright),o2=run(v2,bright);for(UINT i=0;i<W*H;++i){low1=std::min(low1,o1[i*4]);low2=std::min(low2,o2[i*4]);}
  std::cout<<"Dense cloud over 10: V2 min="<<low1<<" v2 min="<<low2<<'\n';assert(low1>1.4f&&low2<1.f);alpha_kept(o2);++cases;
  // SKY-3: cover hardly depends on the resolution with mean-filled octaves.
  auto covered=[&](Frame f,float height){f.metrics[1]=1/height;f.metrics[3]=height;auto o=run(f,Scene{});unsigned n=0;for(UINT i=0;i<W*H;++i)if(std::abs(o[i*4]-.1f)>.01f)++n;return n;};
  auto a1=build(right,up,forward,noon,W,H,3,0,0,0,sun_colour,sky_colour,0,1);auto a2=a1;a2.model[0]=MODEL_CLOUDS_V2;
  unsigned c1=covered(a1,64),c1h=covered(a1,1024),c2=covered(a2,64),c2h=covered(a2,1024);int d1=std::abs(int(c1)-int(c1h)),d2=std::abs(int(c2)-int(c2h));
  std::cout<<"Cover at 64 vs 1024 lines: V2 "<<c1<<"/"<<c1h<<" v2 "<<c2<<"/"<<c2h<<'\n';assert(c2>W*H/20&&d2<d1);++cases;}
 // K09: release drops everything; the next frame re-creates it.
 gpu.release();assert(!gpu.loaded());{auto o=run(build(right,up,forward,midnight,W,H,3,1,1,1),Scene{});unsigned lit=0;for(UINT i=0;i<W*H;++i)if(o[i*4+1]>.11f)++lit;assert(lit>20&&gpu.loaded());std::cout<<"Released and re-created\n";++cases;}
 auto bad=scene_sky_gpu::build(right,up,forward,midnight,W,H,3,1,1,1);bad.params[0]=5;wait();assert(SUCCEEDED(allocator->Reset()));assert(SUCCEEDED(list->Reset(allocator.Get(),nullptr)));using I=scene_sky_gpu::Gpu::Input;assert(!gpu.record(list.Get(),nullptr,I{},I{},I{},I{},bad));bad.params[0]=1;bad.model[0]=9;assert(!scene_sky_gpu::valid(bad));assert(SUCCEEDED(list->Close()));
 std::cout<<"PASS: night sky "<<cases<<" D3D12 cases\n";CloseHandle(event);
}
